@echo off
if exist "%~dp0build-v03\Release\AetherUI.exe" (
 "%~dp0build-v03\Release\AetherUI.exe" --restore
) else (
 echo Build Aether UI first, or exit the running version using its right-click menu.
 pause
)
