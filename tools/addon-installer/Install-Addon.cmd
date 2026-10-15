@echo off
rem WoW Forever Bots: installs the ForeverBots add-on into your game folder. Double-click.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Addon.ps1" %*
echo.
pause
