# Create the Python environment ESPHome runs in (devices\.venv), once.
#   powershell -ExecutionPolicy Bypass -File devices\setup.ps1
# ESPHome needs Python 3.12 - 3.14. The first build of each device also
# downloads the ESP32 toolchain (a few hundred MB) into %USERPROFILE%\.platformio.
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if (-not (Test-Path "$here\.venv\Scripts\python.exe")) {
  Write-Host "creating $here\.venv"
  if (Get-Command py -ErrorAction SilentlyContinue) { & py -3 -m venv "$here\.venv" }
  else { & python -m venv "$here\.venv" }
}
& "$here\.venv\Scripts\python.exe" -m pip install --upgrade pip --quiet
& "$here\.venv\Scripts\python.exe" -m pip install -r "$here\requirements.txt"
& "$here\.venv\Scripts\esphome.exe" version
if (-not (Test-Path "$here\secrets.yaml")) {
  Write-Host "Now copy secrets.example.yaml to secrets.yaml and fill it in." -ForegroundColor Yellow
}
