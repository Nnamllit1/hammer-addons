import argparse
import asyncio
import json
import sys
from pathlib import Path

from .config import Config, discover_cs2


def main() -> None:
    parser = argparse.ArgumentParser(description="Hammer / CS2 Workshop Tools MCP server")
    parser.add_argument("--config", default=None, help="Path to h2mcp.local.json")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("serve", help="Run the MCP server over stdio")
    init = sub.add_parser("init", help="Write a local configuration, detecting Steam/CS2")
    init.add_argument("--cs2-root")
    sub.add_parser("doctor", help="Inspect the local installation")
    sub.add_parser("smoke", help="Test tools, resource and prompt over an actual stdio MCP connection")
    sub.add_parser("worker", help="Run the independent build queue worker (foreground)")
    sub.add_parser("stop-worker", help="Stop the build worker after its current compile")
    play = sub.add_parser("play", help="Deploy, build and open a map in CS2 (defaults to the demo)")
    play.add_argument("addon", nargs="?", default="h2mcp_demo")
    play.add_argument("map_name", nargs="?", default="de_h2mcp_demo")
    play.add_argument("--no-build", action="store_true", help="Play the existing compiled map")
    call = sub.add_parser("call", help="Call any tool through MCP and print JSON")
    call.add_argument("tool")
    arguments = call.add_mutually_exclusive_group()
    arguments.add_argument("--args", default="{}", help="JSON object")
    arguments.add_argument("--args-file", type=Path, help="Read JSON arguments from a file")
    args = parser.parse_args()
    try:
        if args.command == "init":
            from .files import atomic_write
            path = Path(args.config or "h2mcp.local.json").resolve()
            if path.exists():
                raise ValueError(f"Configuration already exists: {path}")
            cs2 = Path(args.cs2_root).resolve() if args.cs2_root else discover_cs2()
            data = {"workspace": str(path.parent), "cs2_root": str(cs2) if cs2 else None}
            atomic_write(path, json.dumps(data, indent=2).encode())
            result = {"config": str(path), **data}
        elif args.command == "worker":
            from .worker import serve
            serve(Config.load(args.config).state)
            return
        elif args.command == "stop-worker":
            (Config.load(args.config).state / "worker.stop").touch()
            result = {"stop_requested": True, "note": "Worker exits after its current compile."}
        elif args.command == "serve":
            from .server import create_server
            create_server(Config.load(args.config)).run(transport="stdio")
            return
        elif args.command == "doctor":
            from .core import Workshop
            result = Workshop(Config.load(args.config)).doctor()
        else:
            from .client import call, smoke
            config = str(Path(args.config or "h2mcp.local.json").resolve())
            if args.command == "smoke":
                result = asyncio.run(smoke(config))
            elif args.command == "play":
                result = asyncio.run(call(config, "play_map", {"addon": args.addon, "map_name": args.map_name,
                                                              "rebuild": not args.no_build}))
            else:
                values = json.loads(args.args_file.read_text(encoding="utf-8-sig") if args.args_file else args.args)
                if not isinstance(values, dict):
                    raise ValueError("Tool arguments must be a JSON object")
                result = asyncio.run(call(config, args.tool, values))
        print(json.dumps(result, indent=2, ensure_ascii=False))
    except (ValueError, OSError) as error:
        print(f"h2mcp: {error}", file=sys.stderr)
        raise SystemExit(1) from None


if __name__ == "__main__":
    main()
