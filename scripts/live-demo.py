"""Create and edit a real Valve template through MCP; optional deploy/compile."""
import argparse
import asyncio
import json
from pathlib import Path

from h2mcp.client import connect


async def demo(config: str, addon: str, deploy: bool, resume: bool = False) -> dict:
    async with connect(config) as session:
        async def call(tool_name, **arguments):
            result = await session.call_tool(tool_name, arguments)
            if result.isError:
                raise RuntimeError(str(result.content))
            return result.structuredContent or json.loads(result.content[0].text)

        if not resume:
            await call("create_addon", addon=addon)
        await call("create_map", addon=addon, name="de_h2mcp_demo", template="template_wingman")
        inspected = await call("inspect_map", addon=addon, path="maps/de_h2mcp_demo.vmap")
        added = await call("add_entity", addon=addon, path="maps/de_h2mcp_demo.vmap",
                           classname="info_target", expected_sha256=inspected["sha256"], origin="0 0 128",
                           properties={"targetname": "h2mcp_test_marker"})
        edited = await call("edit_entity", addon=addon, path="maps/de_h2mcp_demo.vmap",
                            entity_id=added["entity_id"], expected_sha256=added["sha256"], origin="64 0 128")
        final = await call("inspect_map", addon=addon, path="maps/de_h2mcp_demo.vmap", classname="info_target")
        marker = next(e for e in final["entities"] if e["id"] == added["entity_id"])
        assert marker["origin"] == "64 0 128"
        script = await call("create_script", addon=addon, name="main")
        attached = await call("add_entity", addon=addon, path="maps/de_h2mcp_demo.vmap", classname="point_script",
                              expected_sha256=edited["sha256"], properties={"targetname": "h2mcp_main", **script["entity_properties"]})
        final = await call("inspect_map", addon=addon, path="maps/de_h2mcp_demo.vmap", classname="point_script")
        result = {"addon": addon, "map_summary": final["summary"], "marker": marker,
                  "map_sha256": attached["sha256"], "mcp_edit_verified": True}
        if deploy:
            result["deployment"] = await call("deploy_addon", addon=addon, apply=True)
            job = await call("compile_resource", addon=addon, path="maps/scripts/main.js")
            result["compile_job_id"] = job["id"]
        return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default=str(Path(__file__).resolve().parents[1] / "h2mcp.local.json"))
    parser.add_argument("--addon", default="h2mcp_demo")
    parser.add_argument("--deploy", action="store_true", help="Deploy demo to CS2 and compile its JavaScript")
    parser.add_argument("--resume", action="store_true", help="Use an existing empty addon scaffold")
    args = parser.parse_args()
    result = asyncio.run(demo(args.config, args.addon, args.deploy, args.resume))
    report = Path(__file__).resolve().parents[1] / ".h2mcp/live-demo.json"
    report.parent.mkdir(exist_ok=True)
    report.write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))
