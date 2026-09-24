@echo off
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Update-LibUsb.ps1" -Mode Restore
set "driverKitExit=%ERRORLEVEL%"
echo Exit code: %driverKitExit%
pause
exit /b %driverKitExit%
