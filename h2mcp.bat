@echo off
"%~dp0.venv\Scripts\python.exe" -m h2mcp --config "%~dp0h2mcp.local.json" %*
