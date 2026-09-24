@echo off
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Drivers.ps1" -Stage Check
set "driverKitExit=%ERRORLEVEL%"
echo Exit code: %driverKitExit%
pause
exit /b %driverKitExit%
