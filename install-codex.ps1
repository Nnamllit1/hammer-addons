$ErrorActionPreference = 'Stop'
$python = Join-Path $PSScriptRoot '.venv\Scripts\python.exe'
$config = Join-Path $PSScriptRoot 'h2mcp.local.json'
if (-not (Test-Path -LiteralPath $python) -or -not (Test-Path -LiteralPath $config)) {
    throw 'Run .\setup.ps1 first.'
}
# Verify the actual MCP connection before registering this command in Codex.
& $python -m h2mcp --config $config smoke
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
codex mcp add h2mcp -- $python -m h2mcp --config $config serve
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host 'Registered h2mcp. Reload MCP servers or start a new Codex session to load the tools.'
