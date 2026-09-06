import json
import os
from pathlib import Path

import pytest

from h2mcp.config import Config
from h2mcp.core import Workshop
from h2mcp.dmx import Document
from h2mcp.files import contained, digest

MAP = '''<!-- dmx encoding keyvalues2 4 format vmap 40 -->
"CMapRootElement"
{
"id" "elementid" "00000000-0000-0000-0000-000000000001"
"world" "CMapWorld"
{
"id" "elementid" "00000000-0000-0000-0000-000000000002"
"nodeID" "int" "1"
"children" "element_array"
[
"CMapEntity"
{
"id" "elementid" "00000000-0000-0000-0000-000000000003"
"nodeID" "int" "2"
"children" "element_array" []
"entity_properties" "EditGameClassProps"
{
"id" "elementid" "00000000-0000-0000-0000-000000000004"
"classname" "string" "info_player_terrorist"
"targetname" "string" "old"
}
"origin" "vector3" "0 0 0"
"angles" "qangle" "0 90 0"
}
]
"unknownFutureAttribute" "string" "Keep { this } exactly"
}
}
'''


@pytest.fixture
def workshop(tmp_path):
    cs2 = tmp_path / "cs2"
    (cs2 / "game/csgo").mkdir(parents=True)
    (cs2 / "game/csgo/gameinfo.gi").write_text('"GameInfo" {}')
    templates = cs2 / "content/csgo/maps/templates"
    templates.mkdir(parents=True)
    (templates / "template_wingman.vmap").write_text(MAP)
    api = Workshop(Config(tmp_path / "workspace", cs2))
    api.create_addon("demo")
    return api


@pytest.mark.parametrize("path", ["../outside", "/absolute", "C:/file", "x:stream", "a/../../b", "a\\..\\b", "NUL.txt", "a./b", "a//b", "a/CON", "file\n.txt"])
def test_paths_cannot_escape(tmp_path, path):
    with pytest.raises(ValueError):
        contained(tmp_path, path)


def test_junction_escape(tmp_path):
    root = tmp_path / "root"
    outside = tmp_path / "outside"
    root.mkdir()
    outside.mkdir()
    try:
        (root / "linked").symlink_to(outside, target_is_directory=True)
    except OSError:
        if os.name != "nt":
            raise
        import subprocess
        result = subprocess.run(["cmd", "/c", "mklink", "/J", str(root / "linked"), str(outside)], capture_output=True)
        if result.returncode:
            pytest.skip("Windows did not permit creating a test junction")
    with pytest.raises(ValueError):
        contained(root, "linked/escaped.txt")


def test_write_requires_current_hash_and_keeps_backup(workshop):
    first = workshop.write_file("demo", "maps/scripts/a.js", "first")
    with pytest.raises(ValueError, match="Read it first"):
        workshop.write_file("demo", "maps/scripts/a.js", "second")
    second = workshop.write_file("demo", "maps/scripts/a.js", "second", expected_sha256=first["sha256"])
    assert Path(second["backup"]).read_text() == "first"
    with pytest.raises(ValueError, match="changed"):
        workshop.write_file("demo", "maps/scripts/a.js", "third", expected_sha256=first["sha256"])
    assert workshop.read_file("demo", "maps/scripts/a.js")["text"] == "second"


def test_entity_edit_preserves_every_other_byte(workshop):
    created = workshop.create_map("demo", "test")
    inspected = workshop.inspect_map("demo", "maps/test.vmap")
    eid = inspected["entities"][0]["id"]
    changed = workshop.edit_entity("demo", "maps/test.vmap", eid, created["sha256"], {"targetname": "new"})
    text = workshop.source("demo", "maps/test.vmap").read_text()
    assert text == MAP.replace('"targetname" "string" "old"', '"targetname" "string" "new"')
    assert Path(changed["backup"]).read_text() == MAP
    with pytest.raises(ValueError, match="changed"):
        workshop.edit_entity("demo", "maps/test.vmap", eid, created["sha256"], origin="1 2 3")


def test_add_entity_is_in_world_and_unique(workshop):
    created = workshop.create_map("demo", "test")
    result = workshop.add_entity("demo", "maps/test.vmap", "info_player_counterterrorist", created["sha256"],
                                origin="128 64 32", properties={"targetname": 'a "quoted" name'})
    inspected = workshop.inspect_map("demo", "maps/test.vmap")
    assert inspected["summary"]["warnings"] == []
    entity = next(e for e in inspected["entities"] if e["id"] == result["entity_id"])
    assert entity["origin"] == "128 64 32"
    assert entity["properties"]["targetname"] == 'a "quoted" name'
    assert len({e["node_id"] for e in inspected["entities"]}) == 2


@pytest.mark.parametrize("origin", ["1 2", "1 2 nan", "1 2 inf", "1 two 3"])
def test_invalid_transform_never_changes_map(workshop, origin):
    created = workshop.create_map("demo", "test")
    with pytest.raises(ValueError):
        workshop.add_entity("demo", "maps/test.vmap", "info_target", created["sha256"], origin=origin)
    assert digest(workshop.source("demo", "maps/test.vmap").read_bytes()) == created["sha256"]


def test_deploy_preview_conflict_detection_and_no_deletion(workshop):
    written = workshop.write_file("demo", "maps/scripts/a.js", "initial")
    preview = workshop.deploy_addon("demo")
    assert not preview["applied"]
    assert not workshop.installed("demo", "content").exists()
    workshop.deploy_addon("demo", apply=True)
    deployed = workshop.installed("demo", "content") / "maps/scripts/a.js"
    assert deployed.read_text() == "initial"
    deployed.write_text("changed in Hammer")
    workshop.write_file("demo", "maps/scripts/a.js", "new", expected_sha256=written["sha256"])
    workshop.write_file("demo", "maps/scripts/b.js", "another file")
    with pytest.raises(ValueError, match="changed externally"):
        workshop.deploy_addon("demo", apply=True)
    assert deployed.read_text() == "changed in Hammer"
    assert not deployed.with_name("b.js").exists()
    deployed.write_text("initial")
    deployed.with_name("unmanaged.txt").write_text("keep")
    workshop.deploy_addon("demo", apply=True)
    assert deployed.read_text() == "new"
    assert deployed.with_name("unmanaged.txt").read_text() == "keep"


def test_existing_unowned_addon_is_protected(workshop):
    workshop.installed("demo", "content").mkdir(parents=True)
    with pytest.raises(ValueError, match="not owned"):
        workshop.deploy_addon("demo", apply=True)


def test_import_is_a_copy(workshop):
    source = workshop.installed("existing", "content") / "maps/original.vmap"
    source.parent.mkdir(parents=True)
    source.write_text(MAP)
    workshop.import_map("demo", "existing", "maps/original.vmap", "maps/copy.vmap")
    assert source.read_text() == MAP
    assert workshop.inspect_map("demo", "maps/copy.vmap")["summary"]["entities"] == 1


def test_malformed_dmx_is_rejected():
    with pytest.raises(ValueError):
        Document(MAP[:-4])


def test_quoted_brace_is_not_structure():
    doc = Document(MAP.replace('"old"', '"}"'))
    assert doc.entities()[0]["properties"]["targetname"] == "}"


def test_worker_records_success_and_failure(tmp_path):
    import sys
    from h2mcp.worker import run
    for code in (0, 7):
        directory = tmp_path / str(code)
        directory.mkdir()
        (directory / "status.json").write_text(json.dumps({
            "command": [sys.executable, "-c", f"print('compiler log'); raise SystemExit({code})"], "cwd": str(tmp_path)}))
        run(directory)
        job = json.loads((directory / "status.json").read_text())
        assert job["exit_code"] == code
        assert job["status"] == ("succeeded" if code == 0 else "failed")
        assert "compiler log" in (directory / "build.log").read_text()


def test_every_tool_has_structured_schema(workshop):
    import asyncio
    from h2mcp.server import create_server
    server = create_server(workshop.config)
    tools = asyncio.run(server.list_tools())
    assert len(tools) == 22
    assert all(tool.outputSchema is not None for tool in tools)


def test_interrupted_job_is_not_reported_running(workshop):
    job_id = "a" * 32
    directory = workshop.config.state / "jobs" / job_id
    directory.mkdir(parents=True)
    (directory / "status.json").write_text(json.dumps({"id": job_id, "status": "running"}))
    assert workshop.job_status(job_id)["status"] == "interrupted"


def test_worker_heartbeat_expires(tmp_path):
    import time
    from h2mcp.worker import worker_status
    assert not worker_status(tmp_path)["running"]
    path = tmp_path / "worker.json"
    path.write_text(json.dumps({"running": True, "heartbeat": time.time() - 30}))
    assert not worker_status(tmp_path)["running"]
    path.write_text(json.dumps({"running": True, "heartbeat": time.time()}))
    assert worker_status(tmp_path)["running"]
