# Build and flash one device, then show its log.
#
#   powershell -ExecutionPolicy Bypass -File devices\flash.ps1 smart-switch COM5
#       first time: over USB (the COM port Device Manager shows)
#   powershell -ExecutionPolicy Bypass -File devices\flash.ps1 smart-switch
#       after that: over Wi-Fi, to the device's fixed address in its yaml
#
# Over Wi-Fi needs the laptop on the demo network. If a board will not
# start flashing over USB, hold BOOT while it connects.
param(
  [Parameter(Mandatory = $true)][string]$Device,
  [string]$Port
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$esphome = "$here\.venv\Scripts\esphome.exe"
if (-not (Test-Path $esphome)) { throw "Run devices\setup.ps1 first." }
if (-not (Test-Path "$here\secrets.yaml")) { throw "devices\secrets.yaml is missing - copy secrets.example.yaml and fill it in." }
$yaml = Join-Path $here (($Device -replace '\.yaml$', '') + '.yaml')
if (-not (Test-Path $yaml)) { throw "No $yaml" }

if (-not $Port) {
  # The fixed address from the yaml: OTA straight to it, no mDNS needed.
  $m = Select-String -Path $yaml -Pattern 'static_ip:\s*([\d.]+)' | Select-Object -First 1
  if (-not $m) { throw "No static_ip in $yaml - pass a COM port." }
  $Port = $m.Matches[0].Groups[1].Value
}
Write-Host "flashing $Device via $Port"
& $esphome run $yaml --device $Port
