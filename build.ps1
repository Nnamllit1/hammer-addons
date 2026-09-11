param([ValidateSet('Debug','Release')][string]$Configuration = 'Release', [switch]$Test)
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json)[0]
if (-not $vs) { throw 'Install Visual Studio Build Tools with Desktop development with C++.' }
$cmake = Join-Path $vs.installationPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
$major = ([version]$vs.installationVersion).Major
$generator = if ($major -ge 18) { 'Visual Studio 18 2026' } else { 'Visual Studio 17 2022' }
$cache = Join-Path $PSScriptRoot 'build\native\CMakeCache.txt'
if (Test-Path -LiteralPath $cache) {
    $previous = Select-String -LiteralPath $cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$'
    if ($previous -and $previous.Matches[0].Groups[1].Value -ne $PSScriptRoot.Replace('\','/')) {
        # CMake caches absolute paths. Keep the old tree when the checkout is renamed.
        $sourceTree = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'build\native'))
        $savedTree = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ('build\native-before-rename-' + [guid]::NewGuid().ToString('N'))))
        $buildRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'build')) + '\'
        if (-not $sourceTree.StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase) -or -not $savedTree.StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid build tree path' }
        Move-Item -LiteralPath $sourceTree -Destination $savedTree
    }
}
& python "$PSScriptRoot\scripts\fetch-qt.py" --cmake $cmake
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$qt = Join-Path $PSScriptRoot 'build\deps\5.15.2\msvc2019_64'
& $cmake -S $PSScriptRoot -B "$PSScriptRoot\build\native" -G $generator -A x64 "-DCMAKE_PREFIX_PATH=$qt"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $cmake --build "$PSScriptRoot\build\native" --config $Configuration --parallel
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $cmake --install "$PSScriptRoot\build\native" --config $Configuration --prefix "$PSScriptRoot\dist"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
# Retire the prior generated distribution entry point; only Asset Browser is installed.
$legacyProxy = Join-Path $PSScriptRoot 'dist\hammer.dll'
if (Test-Path -LiteralPath $legacyProxy) { Remove-Item -LiteralPath $legacyProxy -Force }
if ($Test) {
    & "$PSScriptRoot\build\native\$Configuration\approval_test.exe" $PSScriptRoot
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    & python "$PSScriptRoot\scripts\test-signing.py" --bin "$PSScriptRoot\build\native\$Configuration"
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    & python "$PSScriptRoot\scripts\test-release.py"
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    & "$PSScriptRoot\build\native\$Configuration\diagnostics_test.exe"
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    & "$PSScriptRoot\build\native\$Configuration\runtime_test.exe" $PSScriptRoot
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    & "$PSScriptRoot\build\native\$Configuration\steam_test.exe" $PSScriptRoot
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    & "$PSScriptRoot\build\native\$Configuration\tool_logs_test.exe"
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    $savedPath = $env:PATH
    $savedPlatform = $env:QT_QPA_PLATFORM
    $savedPlugins = $env:QT_PLUGIN_PATH
    try {
        $env:PATH = "$qt\bin;" + $env:PATH
        $env:QT_QPA_PLATFORM = 'offscreen'
        $env:QT_PLUGIN_PATH = "$qt\plugins"
        & "$PSScriptRoot\build\native\$Configuration\ui_test.exe"
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & "$PSScriptRoot\build\native\$Configuration\build_output_test.exe" $PSScriptRoot
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & "$PSScriptRoot\build\native\$Configuration\jobs_test.exe" $PSScriptRoot
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & "$PSScriptRoot\build\native\$Configuration\editor_test.exe" $PSScriptRoot
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & "$PSScriptRoot\build\native\$Configuration\extensions_test.exe" $PSScriptRoot
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
    } finally {
        $env:PATH = $savedPath
        $env:QT_QPA_PLATFORM = $savedPlatform
        $env:QT_PLUGIN_PATH = $savedPlugins
    }
    & python "$PSScriptRoot\scripts\test-launcher.py" --bin "$PSScriptRoot\build\native\$Configuration"
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    & python "$PSScriptRoot\scripts\test-loader.py" --bin "$PSScriptRoot\build\native\$Configuration"
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
& python "$PSScriptRoot\scripts\package-launcher.py" --dist "$PSScriptRoot\dist"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
Write-Host "Hammer Addons is ready in $PSScriptRoot\dist"
