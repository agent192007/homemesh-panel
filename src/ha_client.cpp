#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

#include "transport.h"
#include "layout_store.h"
#include "secrets.h"

// ------------------------------------------------------------
//  Home Assistant WebSocket transport.
//
//  The ESP32 half of the PC/ESP32 split described in CLAUDE.md. It
//  never calls get_states - that is hundreds of KB on a real instance
//  and will not fit on a WROOM-32 with no PSRAM. Instead:
//
//    - which entities exist comes from the layout: pushed from the
//      desktop tool into NVS, or the compiled ha_layout.h fallback
//    - state comes from one subscribe_entities naming exactly those
//      entity_ids. HA answers with a snapshot of all of them, then
//      sends only what changes - everything over this one socket.
//
//  Initial state used to be one REST GET per entity, made from inside
//  the WebSocket callback. On the hotspot those GETs time out (HA
//  answers the identical request from the laptop in milliseconds, so
//  the radio path is the problem, not HA), and while they blocked, the
//  heartbeat could not run: the library dropped the socket and the
//  panel looped through reconnects with every tile reading "--".
//  subscribe_entities needs no extra connections, so that whole
//  failure mode is gone rather than handled.
//
//  Build with env:ha.
// ------------------------------------------------------------

// The entity table is no longer compiled in here. layout_store.cpp
// owns both sources - NVS and the ha_layout.h fallback - and this
// file reads whichever one won through g_layout.

static const uint32_t RETRY_MS = 5000;

// HA requires message ids to be strictly increasing per connection;
// 1 is always the entities subscription, 2 the layout event one.
static const uint16_t ID_SUBSCRIBE = 1;
static const uint16_t ID_SUBSCRIBE_EVENTS = 2;
static uint16_t next_id = 3;

struct Command {
  EventType type;
  uint8_t entity;
  int32_t value;
};

static QueueHandle_t event_q;
static QueueHandle_t cmd_q;
static WebSocketsClient ws;

static bool ws_connected = false;   // socket up
static bool authed = false;         // auth_ok seen, subscription live

static void post(EventType type, uint8_t entity, int32_t value, float fvalue = 0) {
  PanelEvent e = {type, entity, value, fvalue};
  if (xQueueSend(event_q, &e, 0) != pdTRUE) {
    Serial.println("[ha] event queue full, dropped event");
  }
}

// ------------------------------------------------------------
//  Entity lookup
// ------------------------------------------------------------

static int entity_index(const char *entity_id) {
  for (uint8_t i = 0; i < g_layout.ent_n; i++)
    if (strcmp(g_layout.ents[i].id, entity_id) == 0) return i;
  return -1;
}

static int sensor_index(const char *entity_id) {
  for (uint8_t i = 0; i < g_layout.sens_n; i++)
    if (strcmp(g_layout.sens[i].id, entity_id) == 0) return i;
  return -1;
}

// "light.bed_light" -> "light". The domain is the only thing that
// differs between driving a light, a switch and a fan - turn_on and
// turn_off are spelled the same in all of them.
static void domain_of(const char *entity_id, char *out, size_t n) {
  const char *dot = strchr(entity_id, '.');
  size_t len = dot ? (size_t)(dot - entity_id) : strlen(entity_id);
  if (len >= n) len = n - 1;
  memcpy(out, entity_id, len);
  out[len] = '\0';
}

// ------------------------------------------------------------
//  Applying state
// ------------------------------------------------------------

// Shared by the REST boot fetch and the live trigger events, so both
// paths interpret HA's attributes identically.
static void apply_entity_state(uint8_t i, const char *state, JsonVariantConst attrs) {
  // State is optional. A change that only moved an attribute - the
  // brightness of a light that was already on - arrives with no "s"
  // at all, and its attributes still have to land.
  //
  // "unavailable" and "unknown" mean the integration cannot reach the
  // device. Both fall through to off rather than being guessed at.
  if (state) post(EventType::Power, i, strcmp(state, "on") == 0);

  if (g_layout.ents[i].kind == EntityKind::Toggle) return;
  if (attrs.isNull()) return;

  // A fan's speed is "percentage", 0-100, and null while it is off; the
  // UI's level is 0-255 as for a light. Null or 0 keeps the last speed,
  // drawn muted, the way an off light keeps its brightness.
  if (strncmp(g_layout.ents[i].id, "fan.", 4) == 0) {
    JsonVariantConst p = attrs["percentage"];
    if (!p.isNull() && p.as<int>() > 0)
      post(EventType::Brightness, i, constrain((p.as<int>() * 255 + 50) / 100, 1, 255));
    return;
  }

  JsonVariantConst b = attrs["brightness"];
  if (!b.isNull()) post(EventType::Brightness, i, constrain(b.as<int>(), 0, 255));

  if (g_layout.ents[i].kind == EntityKind::RGB) {
    JsonArrayConst rgb = attrs["rgb_color"];
    if (!rgb.isNull() && rgb.size() >= 3) {
      post(EventType::Color, i,
           pack_rgb(rgb[0].as<uint8_t>(), rgb[1].as<uint8_t>(), rgb[2].as<uint8_t>()));
    }
  }
}

static void apply_sensor_state(uint8_t i, const char *state) {
  if (!state) return;
  // A sensor that has never reported reads "unknown". Leave the
  // header blank in that case rather than posting 0.0.
  char *end = nullptr;
  float v = strtof(state, &end);
  if (end == state) return;
  post(g_layout.sens[i].humidity ? EventType::Humidity : EventType::Temperature,
       g_layout.sens[i].area, 0, v);
}

// One entity_id from a subscribe_entities frame, whichever table it
// belongs to. State may be null - see apply_entity_state.
static void apply_one(const char *eid, const char *state, JsonVariantConst attrs) {
  int i = entity_index(eid);
  if (i >= 0) {
    apply_entity_state(i, state, attrs);
    return;
  }
  i = sensor_index(eid);
  if (i >= 0) apply_sensor_state(i, state);
}

// ------------------------------------------------------------
//  WebSocket
// ------------------------------------------------------------

static void send_auth() {
  char buf[256];
  snprintf(buf, sizeof(buf), "{\"type\":\"auth\",\"access_token\":\"%s\"}", HA_TOKEN);
  ws.sendTXT(buf);
}

// One text buffer for the two jobs that need a big one: building the
// subscribe message, and re-serialising a pushed layout before it is
// stored. They never overlap - the subscription goes out on auth_ok,
// and a push restarts the board when it is done.
//
// Shared rather than one each because env:ha's static DRAM is within
// ~1.5KB of the linker's limit: a second 2KB buffer alone fails to
// link. At file scope, not on the stack, for the reason in Known traps.
static char text_buf[LAYOUT_MAX_JSON + 1];

// One subscription covering every entity in the layout. Built by hand
// rather than through ArduinoJson so the message is one bounded
// buffer. The old version used 1KB of stack and stopped adding ids
// when it ran short, silently, so whatever fell off the end of a long
// layout simply never updated. This one says so.

static void send_subscribe_entities() {
  char *sub_buf = text_buf;
  const size_t sub_n = sizeof(text_buf);
  int n = snprintf(sub_buf, sub_n,
                   "{\"id\":%u,\"type\":\"subscribe_entities\",\"entity_ids\":[",
                   ID_SUBSCRIBE);
  uint8_t total = g_layout.ent_n + g_layout.sens_n, sent = 0;
  for (uint8_t k = 0; k < total; k++) {
    const char *id = k < g_layout.ent_n ? g_layout.ents[k].id
                                        : g_layout.sens[k - g_layout.ent_n].id;
    if ((size_t)n + strlen(id) + 8 >= sub_n) break;  // room for "]}"
    n += snprintf(sub_buf + n, sub_n - n, "%s\"%s\"", sent ? "," : "", id);
    sent++;
  }
  snprintf(sub_buf + n, sub_n - n, "]}");
  ws.sendTXT(sub_buf);

  if (sent < total)
    Serial.printf("[ha] SUBSCRIPTION TRUNCATED: %u of %u entities - "
                  "the rest will never update\n", sent, total);
  else
    Serial.printf("[ha] subscribed to %u entities\n", sent);
}

// Layout pushes arrive as a custom HA event rather than over a second
// socket or an HTTP server on the panel. Two reasons, both practical:
// this connection is already open and already authenticated, and the
// desktop tool therefore never has to find the panel's IP address on
// the hotspot - it only has to know where HA is, which it does.
static void send_subscribe_events() {
  char buf[96];
  snprintf(buf, sizeof(buf),
           "{\"id\":%u,\"type\":\"subscribe_events\",\"event_type\":\"%s\"}",
           ID_SUBSCRIBE_EVENTS, LAYOUT_EVENT);
  ws.sendTXT(buf);
  Serial.println("[ha] listening for " LAYOUT_EVENT);
}

// The two messages too big for the filtered hot path - a layout push
// and the subscribe_entities snapshot - each get their own parse into
// this. They are handled one at a time on the same task, and a push
// restarts the board, so they never need it at once.
//
// At file scope, NOT on the stack. The WebSockets call chain is
// already ~20 frames deep by the time a frame reaches handle_text; a
// 4KB document on top of that overflows and trips the stack canary.
// GCC also inlines these handlers into handle_text, so a local here
// would be paid on *every* frame, not just the big ones. Only the net
// task touches this.
static StaticJsonDocument<4096> big_doc;

// Bounded substring search over a raw frame. The WebSockets library
// does not promise a NUL terminator on the payload, so this does not
// assume one.
static bool frame_contains(const uint8_t *p, size_t len, const char *needle) {
  size_t n = strlen(needle);
  if (len < n) return false;
  for (size_t i = 0; i + n <= len; i++) {
    if (memcmp(p + i, needle, n) == 0) return true;
  }
  return false;
}

static void handle_layout_push(uint8_t *payload, size_t len) {
  StaticJsonDocument<128> filter;
  filter["event"]["data"]["layout"] = true;

  StaticJsonDocument<4096> &doc = big_doc;
  DeserializationError jerr = deserializeJson(doc, payload, len,
                                              DeserializationOption::Filter(filter));
  if (jerr) {
    Serial.printf("[layout] push will not parse: %s\n", jerr.c_str());
    return;
  }

  JsonVariantConst layout = doc["event"]["data"]["layout"];
  if (layout.isNull()) {
    Serial.println("[layout] push carried no layout object");
    return;
  }

  // Re-serialise just the layout subtree, so the bytes stored are
  // exactly the bytes that boot - no second opinion about the format
  // between receiving it and reading it back.
  char *buf = text_buf;
  size_t n = serializeJson(layout, buf, sizeof(text_buf));
  if (n == 0 || n >= sizeof(text_buf)) {
    Serial.printf("[layout] push too large to store (>%u bytes)\n",
                  (unsigned)LAYOUT_MAX_JSON);
    return;
  }

  char err[160];
  if (!layout_store_json(buf, n, err, sizeof(err))) {
    // Rejected layouts leave the running one alone, so the panel
    // keeps working. Say why - a push that silently does nothing is
    // the worst version of this.
    Serial.printf("[layout] push REJECTED: %s\n", err);
    return;
  }

  // Restart rather than rebuilding the widgets live. LVGL's pool is a
  // fixed 40KB and tearing down a screen's worth of objects to build
  // another is where fragmentation bites - and OOM there is a hang,
  // not an error. A reboot is a few seconds and is deterministic.
  Serial.printf("[layout] stored %u bytes, restarting to apply\n", (unsigned)n);
  delay(250);   // let the serial line drain
  ESP.restart();
}

// subscribe_entities frames - the panel's whole view of state. Two
// shapes, both keyed by entity_id:
//
//   {"event":{"a":{"light.x":{"s":"on","a":{...}}, ...}}}
//       the snapshot: every subscribed entity, once, right after
//       subscribing. 2536 bytes for the 7-entity demo layout.
//   {"event":{"c":{"light.x":{"+":{"s":"off","a":{...}}}}}}
//       a change: only what moved. "s" is absent when only an
//       attribute changed, and turning a light off sends its
//       brightness as null.
//
// Both measured against the real HA (2026.9) before this was written.
// The snapshot is too big for the hot path's small document, so it
// gets its own parse, like a layout push and for the same reason - do
// not widen the shared filter to fit it.
//
// Ceiling worth knowing: the library refuses frames over 15KB and
// drops the connection. A layout of 12 entities with demo-sized
// attributes is ~5KB; an entity with an enormous attribute (a WLED
// light with 100 effects in effect_list) could push a snapshot over.
static void handle_entities_event(uint8_t *payload, size_t len) {
  // "*" is ArduinoJson's wildcard for any key - here, any entity_id.
  StaticJsonDocument<384> filter;
  filter["id"] = true;
  JsonObject add = filter["event"]["a"]["*"].to<JsonObject>();
  add["s"] = true;
  add["a"]["brightness"] = true;
  add["a"]["rgb_color"] = true;
  add["a"]["percentage"] = true;      // fan speed
  JsonObject chg = filter["event"]["c"]["*"]["+"].to<JsonObject>();
  chg["s"] = true;
  chg["a"]["brightness"] = true;
  chg["a"]["rgb_color"] = true;
  chg["a"]["percentage"] = true;
  if (filter.overflowed())
    Serial.println("[ha] ENTITIES FILTER OVERFLOWED - increase its size");

  DeserializationError err = deserializeJson(big_doc, payload, len,
                                             DeserializationOption::Filter(filter));
  if (err) {
    Serial.printf("[ha] entities frame (%u bytes) will not parse: %s\n",
                  (unsigned)len, err.c_str());
    return;
  }
  // An overflowed document drops entities without an error, which
  // would look like some tiles never loading. Say so instead.
  if (big_doc.overflowed())
    Serial.printf("[ha] ENTITIES DOCUMENT OVERFLOWED on a %u-byte frame - "
                  "some entities will not update\n", (unsigned)len);
  if (big_doc["id"].as<unsigned>() != ID_SUBSCRIBE) return;

  JsonObjectConst snap = big_doc["event"]["a"].as<JsonObjectConst>();
  for (JsonPairConst kv : snap) {
    JsonVariantConst e = kv.value();
    apply_one(kv.key().c_str(), e["s"].as<const char *>(), e["a"]);
  }
  JsonObjectConst changes = big_doc["event"]["c"].as<JsonObjectConst>();
  for (JsonPairConst kv : changes) {
    JsonVariantConst plus = kv.value()["+"];
    apply_one(kv.key().c_str(), plus["s"].as<const char *>(), plus["a"]);
  }

  if (!snap.isNull())
    Serial.printf("[ha] initial state: %u entities in one %u-byte frame\n",
                  (unsigned)snap.size(), (unsigned)len);
}

static void handle_text(uint8_t *payload, size_t len) {
  // Filtered so a state event carrying a long attribute list still
  // parses into a small fixed document.
  // Route a layout push on the raw bytes, before any parsing.
  //
  // This used to be decided from the filtered document, and it failed
  // silently: the filter below is a fixed-size document, and when a
  // key does not fit ArduinoJson drops it without an error. The panel
  // then parsed the frame, found no event_type, and ignored the push
  // with nothing on the console to say so. A bounded search of the
  // frame cannot fail that way, and costs one pass over ~900 bytes on
  // a message that arrives once in a while.
  if (frame_contains(payload, len, "\"" LAYOUT_EVENT "\"")) {
    Serial.printf("[layout] push received, %u bytes\n", (unsigned)len);
    handle_layout_push(payload, len);
    return;
  }

  // State takes its own path for the same reason, also decided on the
  // raw bytes. HA serialises compactly, so a snapshot always contains
  // "event":{"a": and a change "event":{"c": - neither of which a
  // control message or a layout push can.
  if (frame_contains(payload, len, "\"event\":{\"a\":") ||
      frame_contains(payload, len, "\"event\":{\"c\":")) {
    handle_entities_event(payload, len);
    return;
  }

  // Everything left is a control message: auth, and command results.
  // Kept as its own small filter so a key added for one purpose can
  // never crowd out another - ArduinoJson drops keys that do not fit
  // a fixed document without an error.
  StaticJsonDocument<256> filter;
  filter["type"] = true;
  filter["success"] = true;
  filter["error"]["message"] = true;

  // Say so loudly rather than quietly misbehaving. An overflowed
  // filter means some state updates stop arriving, which looks like a
  // flaky network rather than a sizing bug.
  if (filter.overflowed()) {
    Serial.println("[ha] FILTER OVERFLOWED - increase its size, "
                   "state updates will be missing");
  }

  StaticJsonDocument<768> doc;
  DeserializationError err = deserializeJson(doc, payload, len,
                                             DeserializationOption::Filter(filter));
  if (err) {
    Serial.printf("[ha] parse: %s\n", err.c_str());
    return;
  }
  if (doc.overflowed()) {
    Serial.printf("[ha] document overflowed on a %u-byte frame\n", (unsigned)len);
  }

  const char *type = doc["type"];
  if (!type) return;

  if (strcmp(type, "auth_required") == 0) {
    send_auth();
    return;
  }

  if (strcmp(type, "auth_ok") == 0) {
    Serial.println("[ha] authenticated");
    authed = true;
    send_subscribe_entities();
    send_subscribe_events();
    // Report the margin rather than waiting to discover it as a boot
    // loop. This is the deepest the task gets in normal operation.
    Serial.printf("[ha] net task stack free: %u bytes\n",
                  (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    post(EventType::Link, 0, (int32_t)LinkState::Online);
    post(EventType::DevicesAvailable, 0, 1);
    // No initial fetch: the subscribe_entities snapshot is the
    // initial state, and it arrives on this socket within ~100ms.
    return;
  }

  if (strcmp(type, "auth_invalid") == 0) {
    // Retrying will not help - the token is wrong. Say so plainly
    // rather than looping silently, which is what this looked like
    // when the failure was a firewall instead.
    Serial.println("[ha] AUTH REJECTED - HA_TOKEN in secrets.h is wrong or expired");
    ws.disconnect();
    return;
  }

  if (strcmp(type, "result") == 0) {
    if (!doc["success"].as<bool>())
      Serial.printf("[ha] command failed: %s\n", doc["error"]["message"] | "?");
    return;
  }
}

static void ws_event(WStype_t type, uint8_t *payload, size_t len) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.println("[ha] websocket open, waiting for auth_required");
      ws_connected = true;
      break;

    case WStype_DISCONNECTED:
      if (ws_connected) Serial.println("[ha] websocket closed");
      ws_connected = false;
      authed = false;
      // There is no LWT equivalent here - the disconnect *is* the
      // availability signal, so grey the UI out as soon as it drops.
      post(EventType::DevicesAvailable, 0, 0);
      post(EventType::Link, 0, (int32_t)LinkState::NoBroker);
      break;

    case WStype_TEXT:
      handle_text(payload, len);
      break;

    default:
      break;
  }
}

// ------------------------------------------------------------
//  Outbound commands
// ------------------------------------------------------------

static void send_service(const char *domain, const char *service,
                         const char *entity_id, const char *service_data) {
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"id\":%u,\"type\":\"call_service\",\"domain\":\"%s\",\"service\":\"%s\","
           "\"target\":{\"entity_id\":\"%s\"}%s%s}",
           next_id++, domain, service, entity_id,
           service_data ? ",\"service_data\":" : "",
           service_data ? service_data : "");
  ws.sendTXT(buf);
}

static void run_command(const Command &c) {
  if (c.type == EventType::AutomationRan) {
    if (c.entity >= g_layout.act_n) return;
    const char *eid = g_layout.acts[c.entity].id;
    char domain[16];
    domain_of(eid, domain, sizeof(domain));
    // Scenes and scripts are "turned on"; automations are triggered.
    const char *service = strcmp(domain, "automation") == 0 ? "trigger" : "turn_on";
    send_service(domain, service, eid, nullptr);
    // Firing a scene produces no state change for the scene itself,
    // so the UI's confirmation flash has to be driven from here.
    post(EventType::AutomationRan, c.entity, 1);
    return;
  }

  if (c.entity >= g_layout.ent_n) return;
  const char *eid = g_layout.ents[c.entity].id;
  char domain[16];
  domain_of(eid, domain, sizeof(domain));
  char data[64];

  switch (c.type) {
    case EventType::Power:
      send_service(domain, c.value ? "turn_on" : "turn_off", eid, nullptr);
      break;

    case EventType::Brightness:
      if (strcmp(domain, "fan") == 0) {
        // Fans take a speed in percent; 1 at least, since 0 means off.
        int pct = ((int)c.value * 100 + 127) / 255;
        snprintf(data, sizeof(data), "{\"percentage\":%d}", pct < 1 ? 1 : pct);
        send_service(domain, "set_percentage", eid, data);
      } else {
        snprintf(data, sizeof(data), "{\"brightness\":%d}", (int)c.value);
        send_service(domain, "turn_on", eid, data);
      }
      break;

    case EventType::Color:
      snprintf(data, sizeof(data), "{\"rgb_color\":[%u,%u,%u]}",
               rgb_r(c.value), rgb_g(c.value), rgb_b(c.value));
      send_service(domain, "turn_on", eid, data);
      break;

    default:
      break;
  }
}

// ------------------------------------------------------------
//  Task
// ------------------------------------------------------------

// Why WiFi dropped or never joined, by the driver's own reason code.
// This path used to print nothing: a panel that could not join the
// hotspot sat at "connecting" with no clue which of a dozen causes it
// was. A repeated reason is counted rather than reprinted, so an hour
// out of range is not a thousand lines. Runs on the WiFi event task.
static volatile uint8_t wifi_last_reason;
static uint16_t wifi_repeats;

static void on_wifi_disconnected(WiFiEvent_t, WiFiEventInfo_t info) {
  uint8_t r = info.wifi_sta_disconnected.reason;
  const char *name = WiFi.disconnectReasonName((wifi_err_reason_t)r);
  if (r == wifi_last_reason) {
    if (++wifi_repeats % 10) return;
    Serial.printf("[wifi] still not connected: reason %u (%s), %u times\n",
                  r, name, wifi_repeats + 1);
    return;
  }
  wifi_last_reason = r;
  wifi_repeats = 0;
  Serial.printf("[wifi] not connected: reason %u (%s)\n", r, name);
}

static void on_wifi_got_ip(WiFiEvent_t, WiFiEventInfo_t) {
  wifi_last_reason = 0;  // so the next drop is reported, whatever its reason
}

static void net_task(void *) {
  WiFi.mode(WIFI_STA);
  WiFi.onEvent(on_wifi_disconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent(on_wifi_got_ip, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[wifi] connecting to '%s'\n", WIFI_SSID);

  bool announced = false;
  bool ws_begun = false;
  LinkState last_link = LinkState::NoWifi;
  post(EventType::Link, 0, (int32_t)last_link);

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      if (last_link != LinkState::NoWifi) {
        post(EventType::Link, 0, (int32_t)LinkState::NoWifi);
        last_link = LinkState::NoWifi;
      }
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    if (!announced) {
      // Modem sleep off. On by default, it has the radio doze between
      // beacons and rely on the access point to hold packets for it -
      // something soft APs like Windows Mobile Hotspot are known to do
      // badly. Not proven to have caused anything here: the link
      // failures of 18 Oct turned out to be the hotspot following the
      // laptop internet link onto 5GHz, where this 2.4GHz-only radio
      // cannot see it (reason 201, NO_AP_FOUND). It is kept because it
      // closes one more way for replies to the panel to go missing,
      // for some tens of mA that a mains-powered panel does not mind.
      //
      // Set once joined rather than before WiFi.begin(), so it can
      // never be a suspect in joining. The driver keeps it across
      // reconnects.
      WiFi.setSleep(false);
      Serial.printf("[wifi] connected, IP %s, RSSI %d dBm, modem sleep off\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
      announced = true;
    }

    // Start the socket only once WiFi is actually up, otherwise the
    // library spends its first retries against a dead stack.
    if (!ws_begun) {
      Serial.printf("[ha] connecting to ws://%s:%d/api/websocket\n", HA_HOST, HA_PORT);
      ws.begin(HA_HOST, HA_PORT, "/api/websocket");
      ws.onEvent(ws_event);
      ws.setReconnectInterval(RETRY_MS);
      // Ping every 15s, 3s to answer, drop after 2 misses. Without
      // this, a hotspot that silently loses the route leaves the
      // socket looking open forever and the panel shows stale state.
      ws.enableHeartbeat(15000, 3000, 2);
      ws_begun = true;
      post(EventType::Link, 0, (int32_t)LinkState::NoBroker);
      last_link = LinkState::NoBroker;
    }

    ws.loop();

    LinkState link = authed ? LinkState::Online : LinkState::NoBroker;
    if (link != last_link) {
      post(EventType::Link, 0, (int32_t)link);
      last_link = link;
    }

    if (authed) {
      Command c;
      while (xQueueReceive(cmd_q, &c, 0) == pdTRUE) run_command(c);
    } else {
      // Don't replay taps made while offline once we reconnect.
      xQueueReset(cmd_q);
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ------------------------------------------------------------
//  transport.h
// ------------------------------------------------------------

// The UI wants EntityDesc; the layout header stores the entity_id
// alongside it. Flattened once at start so transport_entity() can
// hand out a stable pointer.
static EntityDesc entities[MAX_ENTITIES];
static AutomationDesc automations[MAX_AUTOMATIONS];

void transport_start() {
  // Before anything reads the table: NVS if a layout was ever
  // pushed, the compiled ha_layout.h otherwise.
  layout_boot();

  event_q = xQueueCreate(48, sizeof(PanelEvent));
  cmd_q = xQueueCreate(16, sizeof(Command));

  // Built from g_layout, which layout_boot() has already filled
  // from NVS or from the compiled fallback.
  for (uint8_t i = 0; i < g_layout.ent_n; i++) {
    entities[i].name = g_layout.ents[i].name;
    entities[i].kind = g_layout.ents[i].kind;
    entities[i].area = g_layout.ents[i].area;
    entities[i].favourite = g_layout.ents[i].favourite;
    entities[i].icon = g_layout.ents[i].icon;
    entities[i].fan = strncmp(g_layout.ents[i].id, "fan.", 4) == 0;
  }
  for (uint8_t i = 0; i < g_layout.act_n; i++) {
    automations[i].name = g_layout.acts[i].name;
    automations[i].has_color = g_layout.acts[i].has_color;
    automations[i].color = g_layout.acts[i].color;
  }

  if (!WIFI_SSID[0] || !HA_HOST[0] || !HA_TOKEN[0])
    Serial.println("[ha] WIFI_SSID / HA_HOST / HA_TOKEN empty - fill in include/secrets.h");

  // Priority 0 (same as IDLE0), not 1. The WebSocket and HTTP clients
  // both spin waiting on sockets; at priority 1 they starve IDLE0 and
  // trip the task watchdog. Same trap as mqtt_client.cpp.
  // 12KB, not 8KB. The layout push path adds call depth on top of a
  // WebSockets chain that is already ~20 frames deep, and this task
  // overflowing presents as a boot loop with a stack canary panic,
  // not as anything that points at the cause. The big JSON documents
  // are at file scope for the same reason - that is the actual fix,
  // this is the margin. Costs 4KB of a 320KB budget.
  xTaskCreatePinnedToCore(net_task, "net", 12288, nullptr, 0, nullptr, 0);
}

bool transport_next_event(PanelEvent *e) {
  return xQueueReceive(event_q, e, 0) == pdTRUE;
}

static void enqueue(EventType t, uint8_t entity, int32_t value) {
  Command c = {t, entity, value};
  xQueueSend(cmd_q, &c, 0);
}

void transport_send_power(uint8_t entity, bool on) {
  enqueue(EventType::Power, entity, on);
}
void transport_send_brightness(uint8_t entity, uint8_t value) {
  enqueue(EventType::Brightness, entity, value);
}
void transport_send_color(uint8_t entity, uint8_t r, uint8_t g, uint8_t b) {
  enqueue(EventType::Color, entity, pack_rgb(r, g, b));
}
void transport_run_automation(uint8_t index) {
  enqueue(EventType::AutomationRan, index, 1);
}

uint8_t transport_entity_count() { return g_layout.ent_n; }
const EntityDesc *transport_entity(uint8_t i) { return &entities[i]; }
uint8_t transport_area_count() { return g_layout.area_n; }
const char *transport_area(uint8_t i) { return g_layout.areas[i]; }
uint8_t transport_automation_count() { return g_layout.act_n; }
const AutomationDesc *transport_automation(uint8_t i) { return &automations[i]; }
