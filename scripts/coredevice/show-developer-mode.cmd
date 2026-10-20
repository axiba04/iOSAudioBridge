@echo off
setlocal
cd /d "%~dp0"
title iOSAudioBridge - Show Developer Mode
echo Connect the unlocked iPhone to this PC by USB and accept Trust This Computer.
echo This tool only shows the switch. You enable it on the phone yourself.
echo.
"%~dp0reveal-developer-mode.exe" %*
set "result=%errorlevel%"
echo.
pause
exit /b %result%
