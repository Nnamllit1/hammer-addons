"""Attach the demo's script, deploy it and enqueue a full Valve map build."""
import asyncio
import json
from pathlib import Path

from h2mcp.client import connect


async def main():
    root = Path(__file__).resolve().parents[1]
    async with connect(str(root / "h2mcp.local.json")) as session:
        async def call(tool, **arguments):
            result = await session.call_tool(tool, arguments)
            if result.isError:
                raise ValueError(str(result.content))
            return result.structuredContent or json.loads(result.content[0].text)
        addon, path = "h2mcp_demo", "maps/de_h2mcp_demo.vmap"
        inspected = await call("inspect_map", addon=addon, path=path, classname="point_script")
        if not any(e["properties"].get("targetname") == "h2mcp_main" for e in inspected["entities"]):
            await call("add_entity", addon=addon, path=path, classname="point_script",
                       expected_sha256=inspected["sha256"], properties={"targetname": "h2mcp_main", "cs_script": "maps/scripts/main.vjs"})
        await call("deploy_addon", addon=addon, apply=True)
        job = await call("compile_resource", addon=addon, path=path)
        (root / ".h2mcp/map-build.json").write_text(json.dumps(job, indent=2))
        print(json.dumps(job, indent=2))


if __name__ == "__main__":
    asyncio.run(main())
