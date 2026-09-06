from __future__ import annotations

import json
import math
import os
import re
import subprocess
import tempfile
import threading
import time
import uuid
from pathlib import Path
from typing import Any

from .config import Config
from .dmx import Document
from .files import addon_name, atomic_write, contained, digest, save

TEXT_EXTENSIONS = {".js", ".ts", ".json", ".cfg", ".txt", ".vmat", ".vtex", ".vmdl",
                   ".vsndevts", ".vsndstck", ".vdata", ".kv3", ".xml", ".css", ".vmap", ".md"}
COMPILE_EXTENSIONS = {".js", ".vmap", ".vmat", ".vtex", ".vmdl", ".vsndevts", ".vdata", ".xml", ".css"}
MAX_MAP_BYTES = 128 * 1024 * 1024


def vector(value: str | None) -> str | None:
    if value is None:
        return None
    try:
        numbers = [float(v) for v in value.split()]
    except ValueError:
        raise ValueError("Expected three finite numbers separated by spaces.") from None
    if len(numbers) != 3 or not all(math.isfinite(v) for v in numbers):
        raise ValueError("Expected three finite numbers separated by spaces.")
    return " ".join(f"{v:g}" for v in numbers)


class Workshop:
    def __init__(self, config: Config):
        self.config = config
        self.lock = threading.RLock()

    def project(self, addon: str, exists: bool = True) -> Path:
        root = contained(self.config.workspace, f"projects/{addon_name(addon)}")
        if exists and not (root / "project.json").is_file():
            raise ValueError(f"No managed project named {addon}. Use create_addon first.")
        return root

    def source(self, addon: str, path: str, area: str = "content") -> Path:
        if area not in ("content", "game"):
            raise ValueError("area must be content or game")
        return contained(self.project(addon), f"{area}/{path}")

    def installed(self, addon: str, area: str) -> Path:
        return contained(self.config.game_root(), f"{area}/csgo_addons/{addon_name(addon)}")

    def doctor(self) -> dict[str, Any]:
        from .worker import worker_status
        cs2 = self.config.cs2
        tools = {n: bool(cs2 and (cs2 / "game/bin/win64" / n).is_file())
                 for n in ("cs2.exe", "resourcecompiler.exe", "dmxconvert.exe", "vconsole2.exe")}
        return {"workspace": str(self.config.workspace), "cs2_root": str(cs2) if cs2 else None,
                "tools": tools, "workshop_tools_ready": all(tools.values()),
                "transport": "stdio", "build_worker": worker_status(self.config.state), "projects": self.list_projects(),
                "limitations": ["Edits operate on saved addon files, not Hammer's unsaved document.",
                                "Mesh construction and viewport control remain in Hammer.",
                                "Compilation requires deploying the addon into the CS2 installation."]}

    def list_projects(self) -> list[dict[str, Any]]:
        root = self.config.workspace / "projects"
        return [{"name": p.parent.name, "path": str(p.parent)} for p in sorted(root.glob("*/project.json"))]

    def list_installed_addons(self) -> list[dict[str, Any]]:
        root = self.config.game_root() / "content/csgo_addons"
        return [{"name": p.name, "content": str(p)} for p in sorted(root.iterdir()) if p.is_dir()] if root.exists() else []

    def create_addon(self, addon: str) -> dict[str, Any]:
        with self.lock:
            root = self.project(addon, exists=False)
            if root.exists():
                raise ValueError("Project directory already exists; choose a new name.")
            root.mkdir(parents=True)
            for folder in ("maps", "maps/scripts", "materials", "models", "soundevents"):
                (root / "content" / folder).mkdir(parents=True, exist_ok=True)
            atomic_write(root / "project.json", json.dumps({"name": addon, "format": 1}, indent=2).encode())
            atomic_write(root / "game/addoninfo.txt", b'"AddonInfo"\n{\n\t"IsPlayable" "1"\n}\n')
            atomic_write(root / "game/cfg/h2mcp_practice.cfg", (
                "// Execute in a local practice session: exec h2mcp_practice\n"
                "sv_cheats 1\nbot_kick\nmp_freezetime 0\nmp_roundtime_defuse 60\n"
                "mp_buy_anywhere 1\nmp_buytime 9999\nmp_restartgame 1\n").encode())
            return {"addon": addon, "path": str(root), "next": "create_map, create_script, then deploy_addon"}

    def list_files(self, addon: str, area: str = "content", query: str = "", offset: int = 0, limit: int = 100) -> dict[str, Any]:
        if area not in ("content", "game") or not 1 <= limit <= 500 or offset < 0:
            raise ValueError("Invalid area or pagination (limit 1..500, offset >= 0).")
        root = contained(self.project(addon), area)
        rows = [{"path": p.relative_to(root).as_posix(), "bytes": p.stat().st_size} for p in sorted(root.rglob("*"))
                if p.is_file() and query.lower() in p.relative_to(root).as_posix().lower() and p.resolve().is_relative_to(root)]
        return {"files": rows[offset:offset + limit], "total": len(rows)}

    def read_file(self, addon: str, path: str, area: str = "content") -> dict[str, Any]:
        file = self.source(addon, path, area)
        if file.stat().st_size > 2 * 1024 * 1024:
            raise ValueError("File exceeds 2 MiB text limit. Use inspect_map for maps.")
        data = file.read_bytes()
        try:
            text = data.decode("utf-8-sig")
            if "\x00" in text:
                raise UnicodeError()
        except UnicodeError:
            raise ValueError("Not a UTF-8 text file. Use inspect_map for binary VMAPs.") from None
        return {"path": path, "text": text, "sha256": digest(data)}

    def write_file(self, addon: str, path: str, text: str, area: str = "content", expected_sha256: str | None = None) -> dict[str, Any]:
        file = self.source(addon, path, area)
        if file.suffix.lower() not in TEXT_EXTENSIONS or len(text.encode()) > 2 * 1024 * 1024 or "\x00" in text:
            raise ValueError("Unsupported text extension, NUL, or file exceeds 2 MiB.")
        if file.suffix.lower() == ".vmap":
            Document(text)
        with self.lock:
            return save(file, text.encode("utf-8"), self.config.state / "backups", expected_sha256)

    def templates(self) -> list[dict[str, Any]]:
        root = self.config.game_root() / "content/csgo/maps/templates"
        return [{"name": p.stem, "bytes": p.stat().st_size} for p in sorted(root.glob("*.vmap"))]

    def convert(self, source: Path, target: Path, encoding: str) -> None:
        target.parent.mkdir(parents=True, exist_ok=True)
        command = [str(self.config.executable("dmxconvert.exe")), "-i", str(source), "-o", str(target), "-oe", encoding, "-of", "vmap"]
        result = subprocess.run(command, capture_output=True, timeout=120, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        if result.returncode or not target.is_file():
            raise ValueError("dmxconvert failed: " + (result.stdout + result.stderr).decode(errors="replace")[-4000:])

    def map_document(self, source: Path) -> tuple[Document, str]:
        if source.suffix.lower() != ".vmap" or source.stat().st_size > MAX_MAP_BYTES:
            raise ValueError("Expected a VMAP smaller than 128 MiB.")
        data = source.read_bytes()
        sha = digest(data)
        if b"encoding binary" in data[:100]:
            with tempfile.TemporaryDirectory(dir=self.config.state) as temp:
                textfile = Path(temp) / "map.vmap"
                self.convert(source, textfile, "keyvalues2")
                text = textfile.read_text(encoding="utf-8-sig")
        else:
            text = data.decode("utf-8-sig")
        return Document(text), sha

    def create_map(self, addon: str, name: str, template: str = "template_wingman") -> dict[str, Any]:
        addon_name(name)
        addon_name(template)
        target = self.source(addon, f"maps/{name}.vmap")
        source = contained(self.config.game_root(), f"content/csgo/maps/templates/{template}.vmap")
        doc, _ = self.map_document(source)
        with self.lock:
            result = save(target, doc.text.encode(), self.config.state / "backups")
        return {**result, "summary": doc.summary(), "template": template}

    def import_map(self, addon: str, installed_addon: str, path: str, destination: str) -> dict[str, Any]:
        source = contained(self.installed(installed_addon, "content"), path)
        target = self.source(addon, destination)
        if target.suffix != ".vmap":
            raise ValueError("Destination must end in .vmap")
        doc, _ = self.map_document(source)
        with self.lock:
            return save(target, doc.text.encode(), self.config.state / "backups")

    def inspect_map(self, addon: str, path: str, classname: str = "", offset: int = 0, limit: int = 50) -> dict[str, Any]:
        if offset < 0 or not 1 <= limit <= 200:
            raise ValueError("limit must be 1..200; offset >= 0")
        doc, sha = self.map_document(self.source(addon, path))
        entities = [e for e in doc.entities() if not classname or e["classname"] == classname]
        return {"sha256": sha, "summary": doc.summary(), "entities": entities[offset:offset + limit], "matched": len(entities)}

    def edit_entity(self, addon: str, path: str, entity_id: str, expected_sha256: str,
                    properties: dict[str, str] | None = None, origin: str | None = None, angles: str | None = None) -> dict[str, Any]:
        with self.lock:
            target = self.source(addon, path)
            doc, sha = self.map_document(target)
            if sha != expected_sha256:
                raise ValueError("Map changed; inspect it again before editing.")
            text = doc.edit_entity(entity_id, properties or {}, vector(origin), vector(angles))
            return save(target, text.encode(), self.config.state / "backups", expected_sha256)

    def add_entity(self, addon: str, path: str, classname: str, expected_sha256: str,
                   origin: str = "0 0 0", angles: str = "0 0 0", properties: dict[str, str] | None = None) -> dict[str, Any]:
        if not re.fullmatch(r"[a-z][a-z0-9_]+", classname):
            raise ValueError("Invalid entity classname.")
        if classname.startswith(("func_", "trigger_")):
            raise ValueError("Brush entities require mesh geometry; create those in Hammer.")
        with self.lock:
            target = self.source(addon, path)
            doc, sha = self.map_document(target)
            if sha != expected_sha256:
                raise ValueError("Map changed; inspect it again before editing.")
            text, eid = doc.add_entity(classname, properties or {}, vector(origin), vector(angles))
            return {**save(target, text.encode(), self.config.state / "backups", expected_sha256), "entity_id": eid}

    def create_script(self, addon: str, name: str) -> dict[str, Any]:
        addon_name(name)
        text = ('import { Instance } from "cs_script/point_script";\n\n'
                f'// Attach maps/scripts/{name}.js to a point_script entity in Hammer.\n'
                f'Instance.Msg("[{addon}] {name} loaded");\n\n'
                'Instance.OnPlayerActivate(({ player }) => {\n'
                '    Instance.Msg(`Player activated: ${player.GetPlayerSlot()}`);\n'
                '});\n')
        result = self.write_file(addon, f"maps/scripts/{name}.js", text)
        # Use the installed API declaration instead of vendoring a stale copy.
        if self.config.cs2:
            definitions = self.config.cs2 / "content/csgo_addons/cs_script_demo/maps/scripts/point_script.d.ts"
            target = self.source(addon, "maps/scripts/point_script.d.ts")
            if definitions.is_file() and not target.exists():
                atomic_write(target, definitions.read_bytes())
        return {**result, "entity_class": "point_script", "entity_properties": {"cs_script": f"maps/scripts/{name}.vjs"}}

    def search_definitions(self, query: str, limit: int = 10) -> dict[str, Any]:
        if not query or not 1 <= limit <= 50:
            raise ValueError("Supply a query and limit 1..50")
        result = []
        root = self.config.game_root() / "game"
        for folder in ("csgo", "core", "csgo_core"):
            for file in sorted((root / folder).glob("*.fgd")):
                lines = file.read_text(encoding="utf-8-sig", errors="replace").splitlines()
                for index, line in enumerate(lines):
                    if query.lower() in line.lower():
                        result.append({"file": str(file), "line": index + 1,
                                       "snippet": "\n".join(lines[max(0, index - 2):index + 24])})
                        if len(result) >= limit:
                            return {"matches": result, "limited": True}
        return {"matches": result, "limited": False}

    def search_assets(self, query: str, limit: int = 100) -> dict[str, Any]:
        if not query or not 1 <= limit <= 500:
            raise ValueError("Supply a query and limit 1..500")
        root = self.config.game_root() / "content"
        result = []
        for base, dirs, files in os.walk(root, followlinks=False):
            dirs[:] = sorted(d for d in dirs if not (Path(base) / d).is_symlink())
            for name in sorted(files):
                path = Path(base) / name
                relative = path.relative_to(root).as_posix()
                if query.lower() in relative.lower():
                    result.append(relative)
                    if len(result) >= limit:
                        return {"assets": result, "limited": True, "scope": "Loose content files; packed VPK assets excluded."}
        return {"assets": result, "limited": False, "scope": "Loose content files; packed VPK assets excluded."}

    def deploy_addon(self, addon: str, apply: bool = False) -> dict[str, Any]:
        """Only update our own previous deployment; detect external Hammer edits."""
        with self.lock:
            project = self.project(addon)
            manifest_file = contained(self.config.state, f"deployments/{addon}.json")
            manifest = json.loads(manifest_file.read_text()) if manifest_file.exists() else {}
            previous = manifest.get("files", {})
            root_id = str(self.config.game_root())
            if manifest and manifest.get("cs2_root") != root_id:
                manifest = {}
                previous = {}
            entries, conflicts, pending = [], [], []
            for area in ("content", "game"):
                local = contained(project, area)
                destination = self.installed(addon, area)
                if destination.exists() and not manifest:
                    raise ValueError(f"Installed addon {addon} already exists and is not owned by this project; use another name.")
                for file in sorted(local.rglob("*")):
                    if not file.is_file():
                        continue
                    relative = file.relative_to(local).as_posix()
                    file = contained(local, relative)
                    target = contained(destination, relative)
                    key = f"{area}/{relative}"
                    data = file.read_bytes()
                    sha = digest(data)
                    old = digest(target.read_bytes()) if target.exists() else None
                    if old == sha:
                        state = "unchanged"
                    elif old and old != previous.get(key):
                        state = "conflict"
                        conflicts.append(key)
                    else:
                        state = "update" if old else "create"
                        pending.append((target, data, old))
                    entries.append({"path": key, "action": state})
                    previous[key] = sha
            if apply and conflicts:
                raise ValueError("Deployment stopped before writing: installed files changed externally: " + ", ".join(conflicts))
            if apply:
                # Save progress per file so a failed/partial deployment can resume.
                recorded = dict(manifest.get("files", {}))
                atomic_write(manifest_file, json.dumps({"cs2_root": root_id, "files": recorded}).encode())
                for target, data, old in pending:
                    save(target, data, self.config.state / "backups", old)
                    area = "content" if target.is_relative_to(self.installed(addon, "content")) else "game"
                    key = area + "/" + target.relative_to(self.installed(addon, area)).as_posix()
                    recorded[key] = digest(data)
                    atomic_write(manifest_file, json.dumps({"cs2_root": root_id, "files": recorded}).encode())
                atomic_write(manifest_file, json.dumps({"cs2_root": root_id, "files": previous}, indent=2).encode())
            return {"applied": apply, "files": entries, "conflicts": conflicts,
                    "content": str(self.installed(addon, "content")), "game": str(self.installed(addon, "game"))}

    def compile_resource(self, addon: str, path: str, force: bool = False) -> dict[str, Any]:
        return self._queue_compile(addon, path, force)

    def _queue_compile(self, addon: str, path: str, force: bool = False,
                       launch_after: list[str] | None = None) -> dict[str, Any]:
        from .worker import worker_status
        if not worker_status(self.config.state)["running"]:
            raise ValueError("Build worker is not running. Run start-worker.ps1 from the project directory, then retry.")
        source = contained(self.installed(addon, "content"), path)
        local = self.source(addon, path)
        if source.suffix.lower() not in COMPILE_EXTENSIONS:
            raise ValueError("Unsupported resource extension.")
        if not source.exists() or digest(source.read_bytes()) != digest(local.read_bytes()):
            raise ValueError("Deploy the current addon source before compiling.")
        command = [str(self.config.executable("resourcecompiler.exe")), "-i", str(source),
                   "-game", str(self.config.game_root() / "game/csgo"), "-nop4"]
        if force:
            command.append("-f")
        with self.lock:
            for file in (self.config.state / "jobs").glob("*/status.json"):
                job = json.loads(file.read_text())
                if job.get("addon") == addon and job.get("status") in ("queued", "running"):
                    raise ValueError("A compile for this addon is already active. Check its job status.")
            job_id = uuid.uuid4().hex
            directory = contained(self.config.state, f"jobs/{job_id}")
            directory.mkdir(parents=True)
            job = {"id": job_id, "addon": addon, "resource": path, "command": command,
                   "cwd": str(self.config.game_root() / "game/bin/win64"), "status": "queued", "created": time.time()}
            if launch_after:
                job.update(kind="play_map", phase="queued", launch_command=launch_after,
                           expected_map=str(contained(self.installed(addon, "game"), str(Path(path).with_suffix(".vpk")))))
            atomic_write(directory / "status.json", json.dumps(job).encode())
            return job

    def play_map(self, addon: str, map_name: str, rebuild: bool = True) -> dict[str, Any]:
        """Deploy/build/run in one job; never launch after a failed compile."""
        addon_name(map_name)
        self.project(addon)
        if not rebuild:
            return self.launch(addon, mode="play", map_name=map_name, apply=True)
        from .worker import worker_status
        worker = worker_status(self.config.state)
        if not worker["running"] or worker.get("version", 1) < 2:
            raise ValueError("Run start-worker.bat to start or update the build worker before playing.")
        source = self.source(addon, f"maps/{map_name}.vmap")
        if not source.is_file():
            raise ValueError(f"Map source does not exist: maps/{map_name}.vmap")
        command = self._play_command(addon, map_name)
        self.config.executable("resourcecompiler.exe")
        with self.lock:
            if any(j.get("status") in ("queued", "running") for j in self.list_jobs(addon, limit=100)):
                raise ValueError("This addon already has an active job. Check its status before playing.")
            deployment = self.deploy_addon(addon, apply=True)
            job = self._queue_compile(addon, f"maps/{map_name}.vmap", launch_after=command)
        return {**job, "deployment": deployment, "note": "CS2 will open this map automatically after a successful build. Check job_status for the result."}

    def _play_command(self, addon: str, map_name: str) -> list[str]:
        return [str(self.config.executable("cs2.exe")), "-addon", addon, "-insecure", "-condebug", "+map", map_name]

    def job_status(self, job_id: str, tail_lines: int = 80) -> dict[str, Any]:
        if not re.fullmatch(r"[0-9a-f]{32}", job_id) or not 1 <= tail_lines <= 300:
            raise ValueError("Invalid job ID or tail_lines (1..300).")
        directory = contained(self.config.state, f"jobs/{job_id}")
        job = json.loads((directory / "status.json").read_text())
        from .worker import worker_status
        worker = worker_status(self.config.state)
        if job["status"] == "running" and not worker["running"]:
            job.update(status="interrupted", error="Build worker stopped; this result is not a successful build.")
        log = directory / "build.log"
        if log.exists():
            with log.open("rb") as stream:
                stream.seek(max(0, log.stat().st_size - 64000))
                tail = stream.read().decode(errors="replace").splitlines()[-tail_lines:]
        else:
            tail = []
        return {**job, "worker": worker, "log_path": str(log), "log_tail": tail}

    def list_jobs(self, addon: str = "", limit: int = 20) -> list[dict[str, Any]]:
        if addon:
            addon_name(addon)
        if not 1 <= limit <= 100:
            raise ValueError("limit must be 1..100")
        jobs = [json.loads(p.read_text()) for p in (self.config.state / "jobs").glob("*/status.json")]
        return sorted((j for j in jobs if not addon or j.get("addon") == addon),
                      key=lambda j: j.get("created", 0), reverse=True)[:limit]

    def launch(self, addon: str, mode: str = "hammer", map_name: str | None = None, apply: bool = False) -> dict[str, Any]:
        self.project(addon)
        if mode not in ("hammer", "play", "vconsole"):
            raise ValueError("mode must be hammer, play or vconsole")
        if map_name:
            addon_name(map_name)
        if mode == "vconsole":
            command = [str(self.config.executable("vconsole2.exe"))]
        else:
            command = [str(self.config.executable("cs2.exe")), "-addon", addon, "-insecure"]
            if mode == "hammer":
                command.extend(["-tools", "-nop4"])
            else:
                if not map_name:
                    raise ValueError("play requires map_name")
                if not (self.installed(addon, "game") / f"maps/{map_name}.vpk").is_file():
                    raise ValueError("Compile the map first; its maps/<name>.vpk is missing.")
                command = self._play_command(addon, map_name)
        result = {"command": command, "started": False}
        if apply:
            if not self.installed(addon, "game").is_dir():
                raise ValueError("Deploy the addon before launching.")
            from .worker import worker_status
            if not worker_status(self.config.state)["running"]:
                raise ValueError("Run start-worker.bat before launching Workshop Tools.")
            job_id = uuid.uuid4().hex
            directory = contained(self.config.state, f"jobs/{job_id}")
            job = {"id": job_id, "addon": addon, "kind": "launch", "command": command,
                   "cwd": str(self.config.game_root() / "game/bin/win64"), "status": "queued", "created": time.time()}
            atomic_write(directory / "status.json", json.dumps(job).encode())
            result.update(job_id=job_id, status="queued", note="Check job_status for launch PID or error.")
        return result
