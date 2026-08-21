@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0test-addon.ps1"
exit /b %ERRORLEVEL%
