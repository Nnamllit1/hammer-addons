@echo off
call "%~dp0start-worker.bat"
if errorlevel 1 exit /b %errorlevel%
call "%~dp0h2mcp.bat" play %*
