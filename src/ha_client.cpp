#include <Arduino.h>
#include <HTTPClient.h>
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
//    - which entities exist comes from include/ha_layout.h, which
//      tools/ha_discover.py generates on the PC
//    - live updates come from one subscribe_trigger naming exactly
//      those entity_ids, so every frame is small and bounded
//    - initial state comes from one REST GET per entity at boot.
//      subscribe_trigger only fires on *change*, so without this the
//      panel would show everything off until something moved.
//
//  Build with env:ha.
// ------------------------------------------------------------

// The entity table is no longer compiled in here. layout_store.cpp
// owns both sources - NVS and the ha_layout.h fallback - and this
// file reads whichever one won through g_layout.

static const uint32_t RETRY_MS = 5000;

// HA requires message ids to be strictly increasing per connection;
// 1 is always the trigger subscription, 2 the layout event one.
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
  if (!state) return;

  // "unavailable" and "unknown" mean the integration cannot reach the
  // device. Both fall through to off rather than being guessed at.
  post(EventType::Power, i, strcmp(state, "on") == 0);

  if (g_layout.ents[i].kind == EntityKind::Toggle) return;
  if (attrs.isNull()) return;

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

// ------------------------------------------------------------
//  Initial state, over REST
// ------------------------------------------------------------

static bool fetch_one_state(const char *entity_id, char *state_out, size_t state_n,
                            DynamicJsonDocument &doc) {
  char url[128];
  snprintf(url, sizeof(url), "http://%s:%d/api/states/%s", HA_HOST, HA_PORT, entity_id);

  HTTPClient http;
  http.setTimeout(3000);
  if (!http.begin(url)) return false;
  http.addHeader("Authorization", "Bearer " HA_TOKEN);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[ha] GET %s -> %d\n", entity_id, code);
    http.end();
    return false;
  }

  // Only the fields we use, so a light with a long effect_list cannot
  // overflow the document.
  StaticJsonDocument<128> filter;
  filter["state"] = true;
  filter["attributes"]["brightness"] = true;
  filter["attributes"]["rgb_color"] = true;

  doc.clear();
  DeserializationError err = deserializeJson(doc, http.getStream(),
                                             DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("[ha] parse %s: %s\n", entity_id, err.c_str());
    return false;
  }

  const char *s = doc["state"];
  if (!s) return false;
  strncpy(state_out, s, state_n - 1);
  state_out[state_n - 1] = '\0';
  return true;
}

static void fetch_initial_states() {
  DynamicJsonDocument doc(768);
  char state[24];

  for (uint8_t i = 0; i < g_layout.ent_n; i++) {
    if (fetch_one_state(g_layout.ents[i].id, state, sizeof(state), doc))
      apply_entity_state(i, state, doc["attributes"]);
    vTaskDelay(pdMS_TO_TICKS(20));  // keep IDLE0 fed between requests
  }
  for (uint8_t i = 0; i < g_layout.sens_n; i++) {
    if (fetch_one_state(g_layout.sens[i].id, state, sizeof(state), doc))
      apply_sensor_state(i, state);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  Serial.println("[ha] initial state loaded");
}

// ------------------------------------------------------------
//  WebSocket
// ------------------------------------------------------------

static void send_auth() {
  char buf[256];
  snprintf(buf, sizeof(buf), "{\"type\":\"auth\",\"access_token\":\"%s\"}", HA_TOKEN);
  ws.sendTXT(buf);
}

// One subscription covering every entity in the layout. Built by hand
// rather than through ArduinoJson so the whole message is one bounded
// stack buffer.
static void send_subscribe() {
  char buf[1024];
  int n = snprintf(buf, sizeof(buf),
                   "{\"id\":%u,\"type\":\"subscribe_trigger\","
                   "\"trigger\":{\"platform\":\"state\",\"entity_id\":[",
                   ID_SUBSCRIBE);

  bool first = true;
  for (uint8_t i = 0; i < g_layout.ent_n; i++) {
    n += snprintf(buf + n, sizeof(buf) - n, "%s\"%s\"", first ? "" : ",", g_layout.ents[i].id);
    first = false;
    if (n >= (int)sizeof(buf) - 64) break;
  }
  for (uint8_t i = 0; i < g_layout.sens_n; i++) {
    n += snprintf(buf + n, sizeof(buf) - n, "%s\"%s\"", first ? "" : ",", g_layout.sens[i].id);
    first = false;
    if (n >= (int)sizeof(buf) - 64) break;
  }
  snprintf(buf + n, sizeof(buf) - n, "]}}");

  ws.sendTXT(buf);
  Serial.printf("[ha] subscribed to %d entities\n", g_layout.ent_n + g_layout.sens_n);
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

// The one message too big for the filtered hot path, so it gets its
// own parse. That cost is paid only when a push actually arrives;
// every state event still goes through the small bounded document.
// At file scope, NOT on the stack. The net task gets 8KB and the
// WebSockets call chain is already ~20 frames deep by the time a
// frame reaches handle_text; a 4KB document on top of that overflows
// and trips the stack canary. GCC also inlines handle_layout_push
// into handle_text, so the cost would be paid on *every* frame, not
// just a push. Only the net task touches this.
static StaticJsonDocument<4096> layout_doc;

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

  StaticJsonDocument<4096> &doc = layout_doc;
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
  static char buf[LAYOUT_MAX_JSON + 1];
  size_t n = serializeJson(layout, buf, sizeof(buf));
  if (n == 0 || n >= sizeof(buf)) {
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

  // 512, not 256. The filter is about 14 nested keys and was close
  // enough to full that adding one more silently truncated it.
  StaticJsonDocument<512> filter;
  filter["type"] = true;
  filter["success"] = true;
  filter["error"]["message"] = true;
  JsonObject trig = filter["event"]["variables"]["trigger"].to<JsonObject>();
  trig["entity_id"] = true;
  trig["to_state"]["state"] = true;
  trig["to_state"]["attributes"]["brightness"] = true;
  trig["to_state"]["attributes"]["rgb_color"] = true;

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
    send_subscribe();
    send_subscribe_events();
    // Report the margin rather than waiting to discover it as a boot
    // loop. This is the deepest the task gets in normal operation.
    Serial.printf("[ha] net task stack free: %u bytes\n",
                  (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    post(EventType::Link, 0, (int32_t)LinkState::Online);
    post(EventType::DevicesAvailable, 0, 1);
    fetch_initial_states();
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

  if (strcmp(type, "event") == 0) {
    JsonVariantConst t = doc["event"]["variables"]["trigger"];
    const char *eid = t["entity_id"];
    const char *state = t["to_state"]["state"];
    if (!eid || !state) return;

    int i = entity_index(eid);
    if (i >= 0) {
      apply_entity_state(i, state, t["to_state"]["attributes"]);
      return;
    }
    i = sensor_index(eid);
    if (i >= 0) apply_sensor_state(i, state);
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
      snprintf(data, sizeof(data), "{\"brightness\":%d}", (int)c.value);
      send_service(domain, "turn_on", eid, data);
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

static void net_task(void *) {
  WiFi.mode(WIFI_STA);
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
      Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
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
  }
  for (uint8_t i = 0; i < g_layout.act_n; i++)
    automations[i].name = g_layout.acts[i].name;

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
