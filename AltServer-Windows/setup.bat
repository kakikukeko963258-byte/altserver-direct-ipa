@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Scripts\build-release.ps1" -OutputDirectory "%TEMP%\AltForge-Windows"
exit /b %ERRORLEVEL%
