#include "mqtt_client.h"

#include "transport.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "secrets.h"

// ------------------------------------------------------------
//  Topic contract. Matches mqtt_simulated_devices/
//  ha_placeholder_entities.py, which owns the entities and their
//  HA discovery. The panel is a controller: it reads retained
//  state topics and publishes to command topics.
// ------------------------------------------------------------
#define DEVICES_BASE "homemesh/sim"
static const char *DEVICES_STATUS = DEVICES_BASE "/status";
static const char *TEMPERATURE_STATE = DEVICES_BASE "/temperature/state";
static const char *HUMIDITY_STATE = DEVICES_BASE "/humidity/state";

static const char *SLUGS[ENT_COUNT] = {
  "living_light",
  "kitchen_light",
  "bedroom_light",
  "fan_switch",
};

// Which entities carry a brightness channel. Keep in step with
// SLUGS above - the subscribe loop reads this rather than naming a
// slug, so adding a second dimmer is a one-line change here.
static const bool DIMMABLE[ENT_COUNT] = {
  true,   // living_light
  false,
  false,
  false,
};

// The panel's own presence in HA: a WiFi signal diagnostic sensor
// whose availability is driven by our LWT.
#define PANEL_BASE "homemesh/panel"
static const char *PANEL_STATUS = PANEL_BASE "/status";
static const char *PANEL_RSSI = PANEL_BASE "/rssi";
static const char *PANEL_DISCOVERY = "homeassistant/sensor/homemesh_panel/rssi/config";
static const char *PANEL_DISCOVERY_PAYLOAD =
  "{\"name\":\"WiFi Signal\","
  "\"unique_id\":\"homemesh_panel_rssi\","
  "\"state_topic\":\"" PANEL_BASE "/rssi\","
  "\"unit_of_measurement\":\"dBm\","
  "\"device_class\":\"signal_strength\","
  "\"state_class\":\"measurement\","
  "\"entity_category\":\"diagnostic\","
  "\"availability_topic\":\"" PANEL_BASE "/status\","
  "\"device\":{\"identifiers\":[\"homemesh_panel_01\"],"
  "\"name\":\"HomeMesh Panel\","
  "\"model\":\"ESP32-2432S032\","
  "\"manufacturer\":\"HomeMesh\"}}";

static const uint32_t RETRY_MS = 5000;
static const uint32_t RSSI_PERIOD_MS = 30000;

struct Command {
  uint8_t entity;
  bool brightness;
  uint8_t value;
};

static QueueHandle_t event_q;
static QueueHandle_t cmd_q;
static WiFiClient net;
static PubSubClient mqtt(net);
static char client_id[32];

static void post(EventType type, uint8_t entity, int32_t value, float fvalue = 0) {
  PanelEvent e = {type, entity, value, fvalue};
  if (xQueueSend(event_q, &e, 0) != pdTRUE) {
    Serial.println("[mqtt] event queue full, dropped event");
  }
}

static void on_message(char *topic, byte *payload, unsigned int len) {
  char msg[16];
  if (len >= sizeof(msg)) len = sizeof(msg) - 1;
  memcpy(msg, payload, len);
  msg[len] = '\0';

  if (strcmp(topic, DEVICES_STATUS) == 0) {
    post(EventType::DevicesAvailable, 0, strcmp(msg, "online") == 0);
    return;
  }
  if (strcmp(topic, TEMPERATURE_STATE) == 0) {
    post(EventType::Temperature, 0, 0, atof(msg));
    return;
  }
  if (strcmp(topic, HUMIDITY_STATE) == 0) {
    post(EventType::Humidity, 0, 0, atof(msg));
    return;
  }

  char t[64];
  for (uint8_t i = 0; i < ENT_COUNT; i++) {
    snprintf(t, sizeof(t), DEVICES_BASE "/%s/state", SLUGS[i]);
    if (strcmp(topic, t) == 0) {
      post(EventType::Power, i, strcmp(msg, "ON") == 0);
      return;
    }
    snprintf(t, sizeof(t), DEVICES_BASE "/%s/brightness/state", SLUGS[i]);
    if (strcmp(topic, t) == 0) {
      post(EventType::Brightness, i, constrain(atoi(msg), 0, 255));
      return;
    }
  }
}

static void publish_command(const Command &c) {
  char topic[64];
  if (c.brightness) {
    char val[8];
    snprintf(topic, sizeof(topic), DEVICES_BASE "/%s/brightness/set", SLUGS[c.entity]);
    snprintf(val, sizeof(val), "%u", c.value);
    mqtt.publish(topic, val);
  } else {
    snprintf(topic, sizeof(topic), DEVICES_BASE "/%s/set", SLUGS[c.entity]);
    mqtt.publish(topic, c.value ? "ON" : "OFF");
  }
  Serial.printf("[mqtt] -> %s\n", topic);
}

static void publish_rssi() {
  char val[8];
  snprintf(val, sizeof(val), "%d", WiFi.RSSI());
  mqtt.publish(PANEL_RSSI, val);
}

static bool connect_broker() {
  Serial.printf("[mqtt] connecting to %s:%d as %s\n", MQTT_HOST, MQTT_PORT, client_id);

  const char *user = MQTT_USER[0] ? MQTT_USER : nullptr;
  const char *pass = MQTT_PASSWORD[0] ? MQTT_PASSWORD : nullptr;
  if (!mqtt.connect(client_id, user, pass, PANEL_STATUS, 1, true, "offline")) {
    Serial.printf("[mqtt] connect failed, state=%d\n", mqtt.state());
    return false;
  }

  // Availability before discovery, same order as the simulator, so
  // the entity never registers in an unavailable state.
  mqtt.publish(PANEL_STATUS, "online", true);
  if (!mqtt.publish(PANEL_DISCOVERY, PANEL_DISCOVERY_PAYLOAD, true)) {
    Serial.println("[mqtt] discovery publish failed - check MQTT_MAX_PACKET_SIZE");
  }

  mqtt.subscribe(DEVICES_STATUS);
  mqtt.subscribe(TEMPERATURE_STATE);
  mqtt.subscribe(HUMIDITY_STATE);
  char t[64];
  for (uint8_t i = 0; i < ENT_COUNT; i++) {
    snprintf(t, sizeof(t), DEVICES_BASE "/%s/state", SLUGS[i]);
    mqtt.subscribe(t);
    if (DIMMABLE[i]) {
      snprintf(t, sizeof(t), DEVICES_BASE "/%s/brightness/state", SLUGS[i]);
      mqtt.subscribe(t);
    }
  }

  Serial.println("[mqtt] connected");
  return true;
}

static void net_task(void *) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[wifi] connecting to '%s'\n", WIFI_SSID);

  bool announced = false;
  LinkState last_link = LinkState::NoWifi;
  uint32_t last_try = 0;
  uint32_t last_rssi = 0;
  post(EventType::Link, 0, (int32_t)last_link);

  for (;;) {
    LinkState link;
    if (WiFi.status() != WL_CONNECTED) {
      link = LinkState::NoWifi;
    } else if (mqtt.connected()) {
      link = LinkState::Online;
    } else {
      if (last_try == 0 || millis() - last_try > RETRY_MS) {
        last_try = millis();
        if (connect_broker()) last_rssi = 0;
      }
      link = mqtt.connected() ? LinkState::Online : LinkState::NoBroker;
    }

    if (link != last_link) {
      if (link != LinkState::NoWifi && !announced) {
        Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
        announced = true;
      }
      post(EventType::Link, 0, (int32_t)link);
      last_link = link;
    }

    if (link == LinkState::Online) {
      mqtt.loop();
      Command c;
      while (xQueueReceive(cmd_q, &c, 0) == pdTRUE) publish_command(c);
      if (last_rssi == 0 || millis() - last_rssi > RSSI_PERIOD_MS) {
        last_rssi = millis();
        publish_rssi();
      }
    } else {
      // Don't replay taps made while offline once we reconnect.
      xQueueReset(cmd_q);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void mqtt_start() {
  event_q = xQueueCreate(32, sizeof(PanelEvent));
  cmd_q = xQueueCreate(8, sizeof(Command));

  uint64_t mac = ESP.getEfuseMac();
  snprintf(client_id, sizeof(client_id), "homemesh_panel_%06llx", mac & 0xFFFFFF);

  if (!WIFI_SSID[0] || !MQTT_HOST[0]) {
    Serial.println("[mqtt] WIFI_SSID / MQTT_HOST empty - fill in include/secrets.h");
  }

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(on_message);
  mqtt.setBufferSize(MQTT_MAX_PACKET_SIZE);
  mqtt.setKeepAlive(15);  // HA sees the LWT within ~20s of a dead panel
  mqtt.setSocketTimeout(5);  // cap how long a silent broker can stall a connect

  // Priority 0 (same as IDLE0), not 1. PubSubClient busy-waits for
  // broker replies with yield(), which only yields to equal or higher
  // priority - at 1 it starves IDLE0 and trips the task watchdog.
  xTaskCreatePinnedToCore(net_task, "net", 6144, nullptr, 0, nullptr, 0);
}

bool mqtt_next_event(PanelEvent *e) {
  return xQueueReceive(event_q, e, 0) == pdTRUE;
}

void mqtt_send_power(uint8_t entity, bool on) {
  Command c = {entity, false, on};
  xQueueSend(cmd_q, &c, 0);
}

void mqtt_send_brightness(uint8_t entity, uint8_t value) {
  Command c = {entity, true, value};
  xQueueSend(cmd_q, &c, 0);
}

// ------------------------------------------------------------
//  transport.h shim. Legacy path: no colour, no automations. Kept
//  building so env:panel stays flashable while ha_client lands.
// ------------------------------------------------------------
static const char *MQTT_AREAS[] = { "Home" };

static const EntityDesc MQTT_ENTITIES[ENT_COUNT] = {
  { "Living Room", EntityKind::Dimmable, 0, true },
  { "Kitchen",     EntityKind::Toggle,   0, true },
  { "Bedroom",     EntityKind::Toggle,   0, true },
  { "Ceiling Fan", EntityKind::Toggle,   0, true },
};

void transport_start() { mqtt_start(); }
bool transport_next_event(PanelEvent *e) { return mqtt_next_event(e); }
void transport_send_power(uint8_t entity, bool on) { mqtt_send_power(entity, on); }
void transport_send_brightness(uint8_t entity, uint8_t v) { mqtt_send_brightness(entity, v); }
void transport_send_color(uint8_t, uint8_t, uint8_t, uint8_t) {}
void transport_run_automation(uint8_t) {}
uint8_t transport_entity_count() { return ENT_COUNT; }
const EntityDesc *transport_entity(uint8_t i) { return &MQTT_ENTITIES[i]; }
uint8_t transport_area_count() { return 1; }
const char *transport_area(uint8_t i) { return MQTT_AREAS[i]; }
uint8_t transport_automation_count() { return 0; }
const AutomationDesc *transport_automation(uint8_t) { return nullptr; }
