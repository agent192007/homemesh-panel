#include <Arduino.h>
#include <WiFi.h>
#include <limits.h>

#include "secrets.h"

// ------------------------------------------------------------
//  env:ap - a second ESP32 as the demo's own WiFi network.
//
//  Why it exists: the laptop's WiFi card (MediaTek MT7921) has one
//  radio, so Windows Mobile Hotspot has to share a channel with the
//  laptop's own WiFi link. When that link is on 5GHz the hotspot goes
//  with it, whatever its band setting says, and the panel - 2.4GHz
//  only - cannot see it at all (reason 201, NO_AP_FOUND; 18 Oct).
//  Pinning the card to "2.4G Only" fixes that at home. At a venue the
//  hotspot still has two dependencies this board does not: it will
//  not start without a network to share, and its channel is whatever
//  the venue's network uses. This needs nothing upstream and stays on
//  2.4GHz, on a channel it picks for itself.
//
//  A drop-in for the hotspot, not a second network to configure:
//    - same SSID and password, read from the panel's own secrets.h
//    - same subnet, 192.168.137.0/24
//    - the laptop takes 192.168.137.1 by hand, which is HA_HOST
//  so the panel cannot tell the difference and needs no reflash.
//
//  Use one or the other, NEVER both at once. Two networks with one
//  name and the panel joins whichever is louder - possibly the one
//  Home Assistant is not on.
//
//  Laptop setup, once: turn Mobile Hotspot off, join this network,
//  then Settings > Network & internet > Wi-Fi > (this network) >
//  IP assignment > Edit > Manual, IPv4 on: IP 192.168.137.1, subnet
//  mask 255.255.255.0, gateway and DNS blank. Windows keeps that per
//  network, so the laptop's other WiFi stays on normal DHCP. There is
//  no internet on this network; the demo needs none. Docker's
//  firewall rule covers both Public and Private, so nothing to change
//  there.
//
//  Any ESP32 board will do. Build and flash with:
//    pio run -e ap -t upload -t monitor
// ------------------------------------------------------------

static const IPAddress AP_IP(192, 168, 137, 254);
static const IPAddress MASK(255, 255, 255, 0);

// Leases .100 to .110. Set explicitly because the core's default is
// "the address after the AP's", and for an AP this close to the top
// of the subnet it wraps that round to .1 - the laptop's address.
static const IPAddress LEASE_START(192, 168, 137, 100);

// The panel, the laptop, and a phone or two looking at HA.
static const int MAX_CLIENTS = 4;

// The quietest of the three non-overlapping 2.4GHz channels. A room
// full of phone hotspots is the normal state of a demo venue, so the
// AP listens before it picks rather than taking a channel on faith.
// 2.4GHz channels bleed into neighbours up to four away, so every
// network within that distance counts against a candidate, weighted
// by how loud it is.
static uint8_t quietest_channel() {
  static const uint8_t CANDIDATES[] = {1, 6, 11};
  int n = WiFi.scanNetworks(false, true);
  if (n < 0) {
    Serial.println("[ap] scan failed, using channel 6");
    return 6;
  }
  uint8_t best = 6;
  long best_score = LONG_MAX;
  for (uint8_t c : CANDIDATES) {
    long score = 0;
    int heard = 0;
    for (int i = 0; i < n; i++) {
      if (abs((int)WiFi.channel(i) - (int)c) > 4) continue;
      score += max(0, 100 + (int)WiFi.RSSI(i));  // -90 dBm counts 10, -30 counts 70
      heard++;
    }
    Serial.printf("[ap] channel %2u: %d network(s) in range, score %ld\n", c, heard, score);
    if (score < best_score) {
      best_score = score;
      best = c;
    }
  }
  WiFi.scanDelete();
  return best;
}

static void print_mac(const char *what, const uint8_t *m) {
  Serial.printf("[ap] %s %02X:%02X:%02X:%02X:%02X:%02X\n",
                what, m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void on_join(WiFiEvent_t, WiFiEventInfo_t info) {
  print_mac("joined:", info.wifi_ap_staconnected.mac);
}

static void on_leave(WiFiEvent_t, WiFiEventInfo_t info) {
  print_mac("left:  ", info.wifi_ap_stadisconnected.mac);
}

// The panel and phones appear here. The laptop never does: it sets
// its own address, so it asks for no lease.
static void on_lease(WiFiEvent_t, WiFiEventInfo_t info) {
  Serial.printf("[ap] leased %s\n", IPAddress(info.wifi_ap_staipassigned.ip.addr).toString().c_str());
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nHomeMesh access point starting");

  // Nothing about this network belongs in NVS. With persistence on, a
  // later boot would bring the old network up before setup() had a
  // chance to put it on the right subnet.
  WiFi.persistent(false);

  // Listen first, as a station: an AP cannot pick a quiet channel it
  // has not heard.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  uint8_t ch = quietest_channel();

  WiFi.onEvent(on_join, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
  WiFi.onEvent(on_leave, ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);
  WiFi.onEvent(on_lease, ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED);

  // Address first, then name. Switching to AP mode brings an AP up
  // under the chip's default name (persistence is off, so nothing
  // older); softAPConfig() moves it onto our subnet, and only then
  // does softAP() give it the real name. So the real name never
  // exists on the wrong subnet, and the panel can never take a
  // 192.168.4.x lease from a half-built AP.
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAPConfig(AP_IP, AP_IP, MASK, LEASE_START)) {
    Serial.println("[ap] FAILED to set the AP address - not starting");
    return;
  }
  if (!WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, ch, 0, MAX_CLIENTS)) {
    // The core refuses a WPA2 passphrase under 8 characters.
    Serial.println("[ap] FAILED to start - is WIFI_PASSWORD at least 8 characters?");
    return;
  }

  Serial.printf("[ap] up: '%s' on channel %u, AP at %s, leases from %s\n",
                WIFI_SSID, ch, WiFi.softAPIP().toString().c_str(),
                LEASE_START.toString().c_str());
  Serial.println("[ap] laptop: Mobile Hotspot OFF, join this network, manual IP "
                 "192.168.137.1 / 255.255.255.0");
}

void loop() {
  // Say when the number of connected devices changes, and otherwise
  // keep quiet - this is a box that should be boring.
  static int last = -1;
  int now = WiFi.softAPgetStationNum();
  if (now != last) {
    Serial.printf("[ap] %d device(s) connected\n", now);
    last = now;
  }
  delay(1000);
}
