"""Use the exact same stdio entry point as Codex for diagnostics and tool calls."""
import json
import os
import sys
from contextlib import asynccontextmanager
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


@asynccontextmanager
async def connect(config_path: str):
    params = StdioServerParameters(command=sys.executable,
                                   args=["-m", "h2mcp", "--config", str(Path(config_path).resolve()), "serve"],
                                   env=dict(os.environ))
    failure = None
    async with stdio_client(params) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()
            try:
                yield session
            except Exception as error:
                # Leave AnyIO task groups normally, preserving the useful tool error.
                failure = error
    if failure is not None:
        raise failure


async def call(config_path: str, name: str, arguments: dict) -> dict:
    async with connect(config_path) as session:
        result = await session.call_tool(name, arguments)
        if result.isError:
            raise ValueError("\n".join(c.text for c in result.content if hasattr(c, "text")))
        if result.structuredContent is not None:
            return result.structuredContent
        if len(result.content) == 1 and hasattr(result.content[0], "text"):
            try:
                return json.loads(result.content[0].text)
            except json.JSONDecodeError:
                pass
        return {"content": [c.model_dump() for c in result.content]}


async def smoke(config_path: str) -> dict:
    async with connect(config_path) as session:
        listing = await session.list_tools()
        names = [t.name for t in listing.tools]
        expected = {"doctor", "create_map", "inspect_map", "edit_entity", "compile_resource", "job_status", "play_map"}
        if not expected.issubset(names):
            raise AssertionError("Missing required MCP tools")
        response = await session.call_tool("doctor", {})
        if response.isError:
            raise AssertionError("doctor returned an MCP error")
        resource = await session.read_resource("h2mcp://workflow")
        prompt = await session.get_prompt("mapping_task", {"addon": "demo", "objective": "Inspect the map"})
        return {"ok": True, "transport": "stdio", "tools": names,
                "resource_ok": bool(resource.contents), "prompt_ok": bool(prompt.messages),
                "doctor": response.structuredContent or json.loads(response.content[0].text)}
