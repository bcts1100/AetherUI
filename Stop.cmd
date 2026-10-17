@echo off
if exist "%~dp0build-v03\Release\AetherUI.exe" (
 "%~dp0build-v03\Release\AetherUI.exe" --quit
) else (
 echo No staged Aether executable found. Close older versions using their right-click menu.
 pause
)
