@echo off
rem Double-click to open the HomeMesh Designer.
rem Builds on first run. Clears ELECTRON_RUN_AS_NODE, which VS Code sets in
rem its terminals and which makes Electron run as plain Node.
set ELECTRON_RUN_AS_NODE=
cd /d "%~dp0"
if not exist node_modules (
  echo Installing dependencies - first run only...
  call npm install
)
node scripts\start.mjs
