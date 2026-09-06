$ErrorActionPreference = 'Stop'
$python = Join-Path $PSScriptRoot '.venv\Scripts\python.exe'
$config = Join-Path $PSScriptRoot 'h2mcp.local.json'
$state = Join-Path $PSScriptRoot '.h2mcp'
New-Item -ItemType Directory -Force -Path $state | Out-Null
$status = & $python -m h2mcp --config $config doctor | ConvertFrom-Json
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$state = Join-Path $status.workspace '.h2mcp'
if ($status.build_worker.running -and $status.build_worker.version -ge 2) {
    Write-Host "Build worker already running (PID $($status.build_worker.pid))."
    exit 0
}
if ($status.build_worker.running) {
    & $python -m h2mcp --config $config stop-worker | Out-Null
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        Start-Sleep -Milliseconds 100
        $ready = Get-Content -LiteralPath (Join-Path $state 'worker.json') -Raw | ConvertFrom-Json
        if (-not $ready.running) { break }
    }
    if ($ready.running) { throw 'The old worker is finishing a build. Run this launcher again when it finishes.' }
}
# Start outside the MCP client, which may terminate its entire subprocess tree.
$worker = Start-Process -FilePath $python -ArgumentList @('-m', 'h2mcp', '--config', "`"$config`"", 'worker') -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -RedirectStandardOutput (Join-Path $state 'worker.stdout.log') -RedirectStandardError (Join-Path $state 'worker.stderr.log') -PassThru
for ($attempt = 0; $attempt -lt 50; $attempt++) {
    Start-Sleep -Milliseconds 100
    $worker.Refresh()
    if ($worker.HasExited) { throw "Worker failed; see $state\worker.stderr.log" }
    $statusPath = Join-Path $state 'worker.json'
    if (Test-Path -LiteralPath $statusPath) {
        $ready = Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json
        # Windows venv python.exe can be a redirector with a separate child PID.
        $now = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
        if ($ready.running -and ($now - $ready.heartbeat) -lt 5) {
            Write-Host "Build worker started (PID $($ready.pid))."
            exit 0
        }
    }
}
throw "Worker did not report ready; see $state\worker.stderr.log"
