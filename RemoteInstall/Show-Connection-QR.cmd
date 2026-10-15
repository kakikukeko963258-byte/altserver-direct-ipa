@echo off
"%~dp0app\altforge-remote.exe" connection
if errorlevel 1 (
  pause
  exit /b 1
)
start "" "%LOCALAPPDATA%\AltServerDirectIPA\Remote\iphone-connection.svg"
