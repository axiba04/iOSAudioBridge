@echo off
setlocal
cd /d "%~dp0"
title iOSAudioBridge - Download DDI
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0download-ddi.ps1"
set "result=%errorlevel%"
echo.
pause
exit /b %result%
