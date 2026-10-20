// The simulator's transport: transport.h implemented for the browser.
//
// The firmware's ha_client.cpp builds its tables from g_layout and moves
// events through FreeRTOS queues filled by a network task. Here the
// tables are built the same way, from the same layout_store.cpp, but
// events arrive from JavaScript (sim_event) and commands leave to
// JavaScript (sim_on_command) - which forwards them to Home Assistant
// when the designer is in live mode. dashboard.cpp cannot tell.
#include <cstring>
#include <deque>
#include <emscripten.h>

#include "../src/layout_store.h"
#include "../src/transport.h"

static EntityDesc entities[MAX_ENTITIES];
static AutomationDesc automations[MAX_AUTOMATIONS];
static std::deque<PanelEvent> events;

// Commands go to JavaScript as (kind, index, value). Kind mirrors
// EventType so both sides share one vocabulary; entity ids are looked
// up on the JS side from the layout it sent.
EM_JS(void, sim_on_command, (int kind, int index, int value), {
  if (Module.onCommand) Module.onCommand(kind, index, value);
});

void transport_start() {
  layout_boot();
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
}

bool transport_next_event(PanelEvent *e) {
  if (events.empty()) return false;
  *e = events.front();
  events.pop_front();
  return true;
}

void sim_push_event(const PanelEvent &e) { events.push_back(e); }

void transport_send_power(uint8_t entity, bool on) {
  sim_on_command((int)EventType::Power, entity, on);
}
void transport_send_brightness(uint8_t entity, uint8_t value) {
  sim_on_command((int)EventType::Brightness, entity, value);
}
void transport_send_color(uint8_t entity, uint8_t r, uint8_t g, uint8_t b) {
  sim_on_command((int)EventType::Color, entity, pack_rgb(r, g, b));
}
void transport_run_automation(uint8_t index) {
  sim_on_command((int)EventType::AutomationRan, index, 1);
}

uint8_t transport_entity_count() { return g_layout.ent_n; }
const EntityDesc *transport_entity(uint8_t i) { return &entities[i]; }
uint8_t transport_area_count() { return g_layout.area_n; }
const char *transport_area(uint8_t i) { return g_layout.areas[i]; }
uint8_t transport_automation_count() { return g_layout.act_n; }
const AutomationDesc *transport_automation(uint8_t i) { return &automations[i]; }
