$ErrorActionPreference = 'Stop'
& "$PSScriptRoot\build.ps1" -Test
exit $LASTEXITCODE
