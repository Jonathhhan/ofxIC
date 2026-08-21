@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0smoke-huggingface.ps1"
exit /b %ERRORLEVEL%
