# Working on h2mcp

- This is a Python stdio MCP server for saved CS2 addon files and Valve tools.
- Run `test.bat` for automated tests and a real MCP protocol smoke test.
- Use `h2mcp.bat call <tool> --args-file <json>` to exercise the same tools as an MCP client.
- Source projects live under `projects/`; generated state/logs/backups live under `.h2mcp/`. Both are ignored. Never commit Valve's templates or local path configuration.
- Preserve unknown DMX fields and unrelated bytes. Use Valve's installed dmxconvert for binary maps; do not guess their binary format.
- Keep file paths confined, require current hashes on replacement, and preserve backups.
- The build queue worker must be launched separately with `start-worker.bat`. MCP clients on Windows can kill their subprocess trees when disconnecting.
- Do not claim that a map was visually verified or playtested from a successful parse, conversion, or compile alone.
