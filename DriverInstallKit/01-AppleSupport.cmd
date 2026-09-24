@echo off
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Drivers.ps1" -Stage Apple
set "driverKitExit=%ERRORLEVEL%"
echo Exit code: %driverKitExit% (3010 means restart Windows before continuing).
pause
exit /b %driverKitExit%
