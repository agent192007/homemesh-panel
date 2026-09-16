# HomeMesh demo preflight.
#
# The panel depends on four things that are easy to forget and that all
# fail the same way from the outside - the panel joins WiFi and then sits
# there not connecting, which reads as a firmware bug and is not one.
# This checks each of them separately so the failure names itself.
#
#   powershell -ExecutionPolicy Bypass -File tools\preflight.ps1

$ErrorActionPreference = 'Continue'
$HaHost = '192.168.137.1'
$HaPort = 8123

# Must match include/ha_layout.h. If the panel shows blank tiles, an
# entity here was renamed or removed in HA and the header is stale.
$Entities = @(
  'switch.ac', 'light.bed_light', 'fan.ceiling_fan',
  'light.kitchen_lights', 'input_boolean.panel_demo_switch',
  'light.ceiling_lights', 'switch.decorative_lights',
  'fan.living_room_fan', 'light.living_room_rgbww_lights'
)

$fail = 0
function Ok   ($m) { Write-Host "  PASS  $m" -ForegroundColor Green }
function Bad  ($m, $fix) {
  Write-Host "  FAIL  $m" -ForegroundColor Red
  Write-Host "        -> $fix" -ForegroundColor Yellow
  $script:fail++
}

Write-Host "`nHomeMesh preflight`n" -ForegroundColor Cyan

# 1. Hotspot. ICS always takes 192.168.137.1, so its presence is a
#    reliable proxy for "the hotspot is actually running".
$hasIp = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
         Where-Object { $_.IPAddress -eq $HaHost }
if ($hasIp) { Ok "hotspot up ($HaHost on $($hasIp[0].InterfaceAlias))" }
else { Bad "hotspot is not running - no $HaHost on any adapter" `
           "Settings > Network & internet > Mobile hotspot. Band must be 2.4 GHz, and turn power saving off." }

# 2. Firewall. Without a path through it the panel joins WiFi fine and
#    then times out on the WebSocket, which reads as a code bug.
#
#    Two things can provide that path, and only one needs to be true.
#    Docker Desktop installs its own inbound allow rule for
#    com.docker.backend.exe, and because the published port is served by
#    that process, the rule already covers 8123 - which is why this demo
#    works with no port rule of its own. A hand-made port rule also
#    works. Accept either; only complain when neither exists.
$portRule = Get-NetFirewallRule -Direction Inbound -Enabled True -ErrorAction SilentlyContinue |
            Where-Object { $_.Action -eq 'Allow' } |
            Where-Object { ($_ | Get-NetFirewallPortFilter -ErrorAction SilentlyContinue).LocalPort -contains "$HaPort" }
$dockerRule = Get-NetFirewallApplicationFilter -ErrorAction SilentlyContinue |
              Where-Object { $_.Program -match 'com\.docker\.backend\.exe' } |
              ForEach-Object { $_ | Get-NetFirewallRule -ErrorAction SilentlyContinue } |
              Where-Object { $_.Enabled -eq 'True' -and $_.Action -eq 'Allow' -and $_.Direction -eq 'Inbound' } |
              Select-Object -First 1

if ($portRule)        { Ok "firewall allows inbound TCP $HaPort ($($portRule[0].DisplayName))" }
elseif ($dockerRule)  { Ok "firewall path via Docker Desktop's own rule ($($dockerRule.DisplayName)) - no port rule needed" }
else { Bad "nothing in the firewall permits inbound $HaPort" `
           "In an ADMIN PowerShell: New-NetFirewallRule -DisplayName 'HomeMesh: Home Assistant 8123' -Direction Inbound -Action Allow -Protocol TCP -LocalPort $HaPort -Profile Any" }

# 3. The container. restart:unless-stopped brings HA back with Docker,
#    but Docker Desktop itself may not have started.
$running = docker ps --filter 'name=homeassistant' --format '{{.Names}}' 2>$null
if ($LASTEXITCODE -ne 0) {
  Bad "cannot talk to Docker" "Start Docker Desktop and wait for the whale icon to settle."
} elseif ($running -match 'homeassistant') {
  Ok "HA container is running"
} else {
  Bad "HA container is not running" "docker start homeassistant"
}

# 4. HA answering on the hotspot address specifically - not localhost.
#    localhost can work while the hotspot binding does not.
try {
  $code = (Invoke-WebRequest "http://${HaHost}:$HaPort/" -TimeoutSec 8 -UseBasicParsing).StatusCode
  Ok "HA answering on http://${HaHost}:$HaPort (HTTP $code)"
} catch {
  Bad "HA not reachable on ${HaHost}:$HaPort" "Check items above first; then: docker logs --tail 40 homeassistant"
}

# 5. Token still valid, and every entity in the layout still exists.
#    A long-lived token survives restarts but can be revoked, and an
#    entity renamed in HA leaves a blank tile on the panel.
$envFile = Join-Path $PSScriptRoot '.env'
if (-not (Test-Path $envFile)) {
  Bad "tools/.env missing" "Recreate it with HA_URL and HA_TOKEN."
} else {
  $token = (Get-Content $envFile | Where-Object { $_ -match '^\s*HA_TOKEN\s*=' } |
            Select-Object -First 1) -replace '^\s*HA_TOKEN\s*=\s*', '' -replace '^["'']|["'']$', ''
  if (-not $token) {
    Bad "HA_TOKEN is empty in tools/.env" "HA > your user > Security > Long-lived access tokens."
  } else {
    $headers = @{ Authorization = "Bearer $token" }
    $missing = @()
    $authOk = $true
    foreach ($e in $Entities) {
      try {
        Invoke-RestMethod "http://${HaHost}:$HaPort/api/states/$e" -Headers $headers -TimeoutSec 8 | Out-Null
      } catch {
        if ($_.Exception.Response.StatusCode.value__ -eq 401) { $authOk = $false; break }
        $missing += $e
      }
    }
    if (-not $authOk) {
      Bad "token rejected (401)" "Create a new token in HA and update tools/.env and include/secrets.h (then reflash)."
    } elseif ($missing.Count) {
      Bad "$($missing.Count) entity(ies) in the layout no longer exist: $($missing -join ', ')" `
          "Regenerate: python tools/ha_discover.py --list, then re-run with --include/--favourite, then reflash."
    } else {
      Ok "token valid, all $($Entities.Count) layout entities present"
    }
  }
}

Write-Host ""
if ($fail -eq 0) {
  Write-Host "Ready. Power the panel and it should connect." -ForegroundColor Green
} else {
  Write-Host "$fail check(s) failed - fix the arrows above before demoing." -ForegroundColor Red
}
Write-Host ""
exit $fail
