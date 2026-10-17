@echo off
if not exist "%~dp0build-v03\Release\AetherUI.exe" (
  echo Aether UI has not been built. Run Build.cmd first.
  pause
  exit /b 1
)
start "Aether UI" "%~dp0build-v03\Release\AetherUI.exe" --settings-dir "%~dp0profile"
