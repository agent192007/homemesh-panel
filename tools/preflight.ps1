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

# Read from include/ha_layout.json - whatever the designer last wrote,
# which is also what it last pushed. This list used to be typed in by
# hand, and it went stale the first time the layout was re-authored.
$layoutFile = Join-Path $PSScriptRoot '..\include\ha_layout.json'
$Entities = @()
if (Test-Path $layoutFile) {
  $layout = Get-Content $layoutFile -Raw | ConvertFrom-Json
  $Entities = @($layout.entities | ForEach-Object { $_.id }) +
              @($layout.sensors | Where-Object { $_.id } | ForEach-Object { $_.id })
}

$fail = 0
function Ok   ($m) { Write-Host "  PASS  $m" -ForegroundColor Green }
function Note ($m) { Write-Host "  NOTE  $m" -ForegroundColor DarkYellow }
function Bad  ($m, $fix) {
  Write-Host "  FAIL  $m" -ForegroundColor Red
  Write-Host "        -> $fix" -ForegroundColor Yellow
  $script:fail++
}

Write-Host "`nHomeMesh preflight`n" -ForegroundColor Cyan

# 0. The WiFi card's band. This laptop's card (MediaTek MT7921) has one
#    radio, so Mobile Hotspot shares a channel with the laptop's own
#    WiFi link. If that link is on 5 GHz the hotspot follows it there,
#    whatever the hotspot's own band setting says, and the panel - 2.4
#    GHz only - cannot see the network at all (it logs reason 201,
#    NO_AP_FOUND). Pinning the card to 2.4 GHz is the fix (18 Oct), and
#    a driver update can quietly undo it. Reading the setting needs no
#    admin rights; changing it does.
$band = Get-NetAdapterAdvancedProperty -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -eq 'Band Selection' } |
        Select-Object -First 1
if (-not $band) {
  Note "no 'Band Selection' setting on any adapter - cannot check the band on this machine. If the panel logs reason 201, the hotspot is on 5 GHz."
} elseif ($band.DisplayValue -match '2\.4') {
  Ok "WiFi card pinned to 2.4 GHz ($($band.Name): $($band.DisplayValue))"
} else {
  Bad "WiFi card is '$($band.DisplayValue)' - the hotspot can follow it onto 5 GHz, where the panel cannot see it" `
      "In an ADMIN PowerShell: Set-NetAdapterAdvancedProperty -Name '$($band.Name)' -DisplayName 'Band Selection' -DisplayValue '2. 2.4G Only'   then turn Mobile Hotspot off and on."
}

# 1. The demo network. Either Windows Mobile Hotspot (ICS always takes
#    192.168.137.1) or the ESP32 access point (env:ap), which the laptop
#    joins with 192.168.137.1 set by hand. Either way the address being
#    on an adapter means the laptop is on the panel's network.
$hasIp = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
         Where-Object { $_.IPAddress -eq $HaHost }
if ($hasIp) { Ok "demo network up ($HaHost on $($hasIp[0].InterfaceAlias))" }
else { Bad "laptop is not on the demo network - no $HaHost on any adapter" `
           "ESP32 access point: join the WiFi 'Pranesh_laptop' (its IP is set to $HaHost by hand). Windows hotspot instead: Settings > Network & internet > Mobile hotspot, 2.4 GHz - never both at once." }

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
if (-not $Entities.Count) {
  Bad "no entities read from include/ha_layout.json" "Write a layout from tools/layout_designer.py."
} elseif (-not (Test-Path $envFile)) {
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
    $reachable = $true
    foreach ($e in $Entities) {
      try {
        Invoke-RestMethod "http://${HaHost}:$HaPort/api/states/$e" -Headers $headers -TimeoutSec 8 | Out-Null
      } catch {
        # Only a 404 means the entity is gone. No response at all means
        # HA was not reachable - which check 4 already reports - and
        # calling that "entities missing" sends you to re-author a
        # perfectly good layout.
        $status = $_.Exception.Response.StatusCode.value__
        if ($status -eq 401) { $authOk = $false; break }
        if ($status -eq 404) { $missing += $e } else { $reachable = $false; break }
      }
    }
    if (-not $reachable) {
      Note "token and entities not checked - HA did not answer on ${HaHost}:$HaPort (see above)"
    } elseif (-not $authOk) {
      Bad "token rejected (401)" "Create a new token in HA and update tools/.env and include/secrets.h (then reflash)."
    } elseif ($missing.Count) {
      Bad "$($missing.Count) entity(ies) in the layout no longer exist: $($missing -join ', ')" `
          "Re-author in tools/layout_designer.py and push to the panel - no reflash needed."
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
