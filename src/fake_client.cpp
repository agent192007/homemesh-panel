#include <Arduino.h>

#include "transport.h"

// ------------------------------------------------------------
//  Simulated transport. No WiFi, no broker, no Home Assistant.
//
//  Exists so UI work is not blocked on infrastructure, and so there
//  is always a demo that runs with the network unplugged. It mirrors
//  the real transport's shape - own task, queue to the LVGL loop -
//  so the swap to ha_client.cpp exercises the same paths.
//
//  Build with env:demo.
// ------------------------------------------------------------

static const EntityDesc ENTITIES[] = {
  { "Living Room", EntityKind::RGB },
  { "Kitchen",     EntityKind::Dimmable },
  { "Bedroom",     EntityKind::Toggle },
  { "Ceiling Fan", EntityKind::Toggle },
};
static const uint8_t ENTITY_N = sizeof(ENTITIES) / sizeof(ENTITIES[0]);

static const AutomationDesc AUTOMATIONS[] = {
  { "Good Night" },
  { "Movie Mode" },
  { "All Off" },
  { "Morning" },
};
static const uint8_t AUTOMATION_N = sizeof(AUTOMATIONS) / sizeof(AUTOMATIONS[0]);

// A command heading "out to the network", echoed back as state after a
// short delay so the UI sees the same round-trip it will see for real.
struct Command {
  EventType type;
  uint8_t entity;
  int32_t value;
};

static QueueHandle_t event_q;
static QueueHandle_t cmd_q;

// Simulated device state.
static bool s_power[MAX_ENTITIES];
static uint8_t s_bright[MAX_ENTITIES];
static int32_t s_color[MAX_ENTITIES];

static void post(EventType type, uint8_t entity, int32_t value, float fvalue = 0) {
  PanelEvent e = {type, entity, value, fvalue};
  xQueueSend(event_q, &e, 0);
}

static void fake_task(void *) {
  // Come up "offline" briefly so the greyed-out state is visible and
  // gets exercised on every boot.
  post(EventType::Link, 0, (int32_t)LinkState::NoWifi);
  vTaskDelay(pdMS_TO_TICKS(1200));
  post(EventType::Link, 0, (int32_t)LinkState::Online);
  post(EventType::DevicesAvailable, 0, 1);

  // Retained-state replay, as a real broker or HA would send on connect.
  for (uint8_t i = 0; i < ENTITY_N; i++) {
    post(EventType::Power, i, s_power[i]);
    if (ENTITIES[i].kind != EntityKind::Toggle)
      post(EventType::Brightness, i, s_bright[i]);
    if (ENTITIES[i].kind == EntityKind::RGB)
      post(EventType::Color, i, s_color[i]);
  }

  uint32_t last_sensor = 0;
  uint32_t last_extern = millis();
  float temp = 24.4f, hum = 47.0f;

  for (;;) {
    Command c;
    while (xQueueReceive(cmd_q, &c, 0) == pdTRUE) {
      // Round-trip delay, as if HA had acknowledged it.
      vTaskDelay(pdMS_TO_TICKS(60));
      switch (c.type) {
        case EventType::Power:
          s_power[c.entity] = c.value != 0;
          post(EventType::Power, c.entity, s_power[c.entity]);
          break;
        case EventType::Brightness:
          s_bright[c.entity] = (uint8_t)c.value;
          if (!s_power[c.entity]) {  // HA turns a light on when dimmed
            s_power[c.entity] = true;
            post(EventType::Power, c.entity, 1);
          }
          post(EventType::Brightness, c.entity, s_bright[c.entity]);
          break;
        case EventType::Color:
          s_color[c.entity] = c.value;
          post(EventType::Color, c.entity, s_color[c.entity]);
          break;
        case EventType::AutomationRan:
          post(EventType::AutomationRan, c.entity, 1);
          // Make automations visibly do something.
          if (c.entity == 0 || c.entity == 2) {  // Good Night / All Off
            for (uint8_t i = 0; i < ENTITY_N; i++) {
              s_power[i] = false;
              post(EventType::Power, i, 0);
            }
          } else if (c.entity == 1) {  // Movie Mode
            for (uint8_t i = 0; i < ENTITY_N; i++) {
              s_power[i] = (i == 0);
              post(EventType::Power, i, s_power[i]);
            }
            s_bright[0] = 40;
            s_color[0] = pack_rgb(120, 40, 200);
            post(EventType::Brightness, 0, s_bright[0]);
            post(EventType::Color, 0, s_color[0]);
          } else {  // Morning
            for (uint8_t i = 0; i < ENTITY_N; i++) {
              s_power[i] = true;
              post(EventType::Power, i, 1);
            }
            s_bright[0] = 255;
            s_color[0] = pack_rgb(255, 214, 170);
            post(EventType::Brightness, 0, s_bright[0]);
            post(EventType::Color, 0, s_color[0]);
          }
          break;
        default:
          break;
      }
    }

    uint32_t now = millis();

    if (now - last_sensor > 4000) {
      last_sensor = now;
      temp += (float)(random(-10, 11)) / 100.0f;
      hum += (float)(random(-10, 11)) / 50.0f;
      if (temp < 21) temp = 21; if (temp > 28) temp = 28;
      if (hum < 35) hum = 35; if (hum > 65) hum = 65;
      post(EventType::Temperature, 0, 0, temp);
      post(EventType::Humidity, 0, 0, hum);
    }

    // Every 20s, change something "from Home Assistant" so the demo
    // shows state arriving that the panel did not ask for.
    if (now - last_extern > 20000) {
      last_extern = now;
      uint8_t i = random(0, ENTITY_N);
      s_power[i] = !s_power[i];
      post(EventType::Power, i, s_power[i]);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void transport_start() {
  event_q = xQueueCreate(48, sizeof(PanelEvent));
  cmd_q = xQueueCreate(16, sizeof(Command));

  randomSeed(esp_random());

  s_power[0] = true;  s_bright[0] = 200; s_color[0] = pack_rgb(255, 170, 60);
  s_power[1] = true;  s_bright[1] = 130;
  s_power[2] = false;
  s_power[3] = false;

  Serial.println("[demo] simulated transport - no network");
  xTaskCreatePinnedToCore(fake_task, "fake", 4096, nullptr, 0, nullptr, 0);
}

bool transport_next_event(PanelEvent *e) {
  return xQueueReceive(event_q, e, 0) == pdTRUE;
}

static void send(EventType t, uint8_t entity, int32_t value) {
  Command c = {t, entity, value};
  xQueueSend(cmd_q, &c, 0);
}

void transport_send_power(uint8_t entity, bool on) {
  send(EventType::Power, entity, on);
}
void transport_send_brightness(uint8_t entity, uint8_t value) {
  send(EventType::Brightness, entity, value);
}
void transport_send_color(uint8_t entity, uint8_t r, uint8_t g, uint8_t b) {
  send(EventType::Color, entity, pack_rgb(r, g, b));
}
void transport_run_automation(uint8_t index) {
  send(EventType::AutomationRan, index, 1);
}

uint8_t transport_entity_count() { return ENTITY_N; }
const EntityDesc *transport_entity(uint8_t i) { return &ENTITIES[i]; }
uint8_t transport_automation_count() { return AUTOMATION_N; }
const AutomationDesc *transport_automation(uint8_t i) { return &AUTOMATIONS[i]; }
