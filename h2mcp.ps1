# Forward CLI arguments without rebuilding a shell command string.
& "$PSScriptRoot\.venv\Scripts\python.exe" -m h2mcp --config "$PSScriptRoot\h2mcp.local.json" @args
exit $LASTEXITCODE
