@echo off
setlocal DisableDelayedExpansion
if exist "%~dp0tools_launcher.exe" (
  "%~dp0tools_launcher.exe" %*
) else if exist "%~dp0dist\portable\tools_launcher.exe" (
  "%~dp0dist\portable\tools_launcher.exe" %*
) else (
  echo The portable launcher is missing. Extract the complete release package, or run build.bat first.
  pause
  exit /b 1
)
if errorlevel 1 (
  echo.
  pause
  exit /b 1
)
exit /b 0
