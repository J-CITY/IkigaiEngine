@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0utils\environment\python_env.ps1" %*
exit /b %errorlevel%
