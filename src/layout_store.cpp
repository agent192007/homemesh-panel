#include "layout_store.h"
#include "panel_theme.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <string.h>

#if !__has_include("ha_layout.h")
#error "include/ha_layout.h is missing. Generate it first: python tools/ha_discover.py --list, then --include <entity_ids>"
#endif
#include "ha_layout.h"

#ifndef HA_SENSOR_COUNT
#define HA_SENSOR_COUNT 0
struct HaSensorCfg { const char *id; uint8_t area; bool humidity; };
static const HaSensorCfg HA_SENSORS[1] = { { "", 0, false } };
#endif

Layout g_layout;

// NVS namespace and key. Kept short - NVS keys are capped at 15 chars.
static const char *NVS_NS = "homemesh";
static const char *NVS_KEY = "layout";

// Worst case for a full layout: 12 entities (id + name), 6 areas,
// 8 actions and 12 sensor ids. Measured against the demo layout at
// ~600 bytes, so this has roughly 4x headroom.
static const size_t ARENA_N = 2560;

// Shared by layout_boot() and layout_store_json(), which never run at
// the same time - one is boot, the other is a push arriving later. At
// file scope rather than on the stack: 4KB would overflow both the
// setup task and the net task, which each get 8KB.
static StaticJsonDocument<4096> parse_doc;

static char arena[ARENA_N];
static size_t arena_used;

// Holds the NVS copy for the lifetime of the process: ArduinoJson
// parses it zero-copy, so this must outlive the document.
static char nvs_buf[LAYOUT_MAX_JSON + 1];

static const char *intern(const char *s) {
  if (!s) return nullptr;
  size_t n = strlen(s) + 1;
  if (arena_used + n > ARENA_N) return nullptr;
  char *dst = arena + arena_used;
  memcpy(dst, s, n);
  arena_used += n;
  return dst;
}

// A colour may arrive as "#RRGGBB", "RRGGBB", or a plain number.
// Accepts all three rather than being strict about it - the cost of
// being wrong is a colour, and rejecting a whole layout over one
// would be out of proportion.
static bool colour_from(JsonVariantConst v, uint32_t *out) {
  if (v.isNull()) return false;
  if (v.is<unsigned int>()) {
    *out = v.as<unsigned int>() & 0xFFFFFF;
    return true;
  }
  const char *s = v.as<const char *>();
  if (!s) return false;
  if (*s == '#') s++;
  if (strlen(s) != 6) return false;
  char *end = nullptr;
  unsigned long n = strtoul(s, &end, 16);
  if (end != s + 6) return false;
  *out = (uint32_t)(n & 0xFFFFFF);
  return true;
}

// Fills g_theme from an optional "theme" object. Anything absent or
// malformed keeps its compiled default, so a partial theme is valid
// and there is no state where a colour is undefined.
static void apply_theme(JsonObjectConst root) {
  theme_defaults(&g_theme);

  JsonObjectConst t = root["theme"];
  if (t.isNull()) return;

  struct { const char *key; uint32_t *field; } map[] = {
    { "bg",     &g_theme.bg     },
    { "tile",   &g_theme.tile   },
    { "on",     &g_theme.on     },
    { "accent", &g_theme.accent },
    { "text",   &g_theme.text   },
    { "muted",  &g_theme.muted  },
    { "ok",     &g_theme.ok     },
    { "warn",   &g_theme.warn   },
    { "bad",    &g_theme.bad    },
  };

  uint8_t n = 0;
  for (auto &m : map) {
    if (colour_from(t[m.key], m.field)) n++;
  }
  if (n) Serial.printf("[layout] theme: %u of %u colours set\n",
                       n, (unsigned)PANEL_THEME_FIELDS);
}

// Mirrors DOMAIN_ICONS in tools/ha_discover.py. Change one, change
// the other, or the designer preview and the panel will disagree.
//
// Only reached when a layout names no icon - which means a layout
// stored before icons existed. The tool always writes one now.
struct DomainIcon { const char *domain; const char *icon; };

static const DomainIcon DOMAIN_ICONS[] = {
  { "light",         "light"  },
  { "fan",           "fan"    },
  { "switch",        "plug"   },
  { "input_boolean", "toggle" },
  { "media_player",  "tv"     },
  { "lock",          "lock"   },
  { "cover",         "blinds" },
};

static const char *icon_for_domain(const char *entity_id) {
  if (!entity_id) return "unknown";
  const char *dot = strchr(entity_id, '.');
  size_t n = dot ? (size_t)(dot - entity_id) : strlen(entity_id);
  for (auto &d : DOMAIN_ICONS) {
    if (strlen(d.domain) == n && strncmp(d.domain, entity_id, n) == 0)
      return d.icon;
  }
  return "unknown";
}

static EntityKind kind_from(const char *s) {
  if (!s) return EntityKind::Toggle;
  if (strcmp(s, "RGB") == 0) return EntityKind::RGB;
  if (strcmp(s, "Dimmable") == 0) return EntityKind::Dimmable;
  if (strcmp(s, "Status") == 0) return EntityKind::Status;
  return EntityKind::Toggle;
}

static bool kind_valid(const char *s) {
  return s && (strcmp(s, "RGB") == 0 || strcmp(s, "Dimmable") == 0 ||
               strcmp(s, "Toggle") == 0 || strcmp(s, "Status") == 0);
}

// ------------------------------------------------------------
//  The compiled fallback
// ------------------------------------------------------------

static void load_builtin() {
  arena_used = 0;
  memset(&g_layout, 0, sizeof(g_layout));

  g_layout.area_n = HA_AREA_COUNT;
  for (uint8_t i = 0; i < HA_AREA_COUNT; i++) g_layout.areas[i] = HA_AREAS[i];

  g_layout.ent_n = HA_ENTITY_COUNT;
  for (uint8_t i = 0; i < HA_ENTITY_COUNT; i++) {
    g_layout.ents[i].id = HA_ENTITIES[i].id;
    g_layout.ents[i].name = HA_ENTITIES[i].name;
#ifdef HA_LAYOUT_HAS_ICONS
    g_layout.ents[i].icon = HA_ENTITIES[i].icon;
#else
    // Generated before icons existed. Points into flash via the
    // domain table, so nothing is copied.
    g_layout.ents[i].icon = icon_for_domain(HA_ENTITIES[i].id);
#endif
    g_layout.ents[i].kind = HA_ENTITIES[i].kind;
    g_layout.ents[i].area = HA_ENTITIES[i].area;
    g_layout.ents[i].favourite = HA_ENTITIES[i].favourite;
  }

  g_layout.act_n = HA_ACTION_COUNT;
  for (uint8_t i = 0; i < HA_ACTION_COUNT; i++) {
    g_layout.acts[i].id = HA_ACTIONS[i].id;
    g_layout.acts[i].name = HA_ACTIONS[i].name;
    g_layout.acts[i].has_color = false;   // colours travel only in pushed layouts
  }

  g_layout.sens_n = HA_SENSOR_COUNT;
  for (uint8_t i = 0; i < HA_SENSOR_COUNT; i++) {
    g_layout.sens[i].id = HA_SENSORS[i].id;
    g_layout.sens[i].area = HA_SENSORS[i].area;
    g_layout.sens[i].humidity = HA_SENSORS[i].humidity;
  }

#ifdef HA_STATUS_ENTITY
  g_layout.status = HA_STATUS_ENTITY;
#endif

  // No pushed layout means no pushed theme either.
  theme_defaults(&g_theme);

  // These point into flash, not the arena, so nothing is copied.
  g_layout.from_nvs = false;
}

// ------------------------------------------------------------
//  Validation
//
//  Runs against a pushed layout *before* it is stored, so a bad push
//  is rejected while the panel keeps running on the layout it has.
//  Every rejection names its reason - a layout that silently does not
//  arrive is the worst version of this feature.
// ------------------------------------------------------------

#define REJECT(...) do { snprintf(err, err_n, __VA_ARGS__); return false; } while (0)

static bool validate(JsonObjectConst root, char *err, size_t err_n) {
  int v = root["v"] | -1;
  if (v != 1) REJECT("unsupported layout version %d (this firmware speaks 1)", v);

  JsonArrayConst areas = root["areas"];
  JsonArrayConst ents = root["entities"];
  JsonArrayConst acts = root["actions"];
  JsonArrayConst sens = root["sensors"];

  if (areas.isNull() || ents.isNull()) REJECT("missing areas[] or entities[]");
  if (areas.size() == 0) REJECT("layout has no areas");
  if (areas.size() > MAX_AREAS)
    REJECT("%u areas, max %u", (unsigned)areas.size(), MAX_AREAS);
  if (ents.size() == 0) REJECT("layout has no entities");
  if (ents.size() > MAX_ENTITIES)
    REJECT("%u entities, max %u", (unsigned)ents.size(), MAX_ENTITIES);
  if (acts.size() > MAX_AUTOMATIONS)
    REJECT("%u actions, max %u", (unsigned)acts.size(), MAX_AUTOMATIONS);
  if (sens.size() > MAX_ENTITIES)
    REJECT("%u sensors, max %u", (unsigned)sens.size(), MAX_ENTITIES);

  // Count every string the arena would have to hold. Doing this up
  // front means a layout cannot be half-adopted and then run out of
  // room partway through the table.
  size_t need = 0;

  for (JsonVariantConst a : areas) {
    const char *s = a.as<const char *>();
    if (!s || !*s) REJECT("an area name is empty");
    need += strlen(s) + 1;
  }

  uint8_t i = 0;
  for (JsonObjectConst e : ents) {
    const char *id = e["id"];
    const char *name = e["name"];
    const char *kind = e["kind"];
    if (!id || !*id) REJECT("entity %u has no id", i);
    if (!name || !*name) REJECT("entity %s has no name", id);
    if (!kind_valid(kind)) REJECT("entity %s has unknown kind \"%s\"", id, kind ? kind : "");
    int area = e["area"] | -1;
    if (area < 0 || area >= (int)areas.size())
      REJECT("entity %s has area %d, only %u areas", id, area, (unsigned)areas.size());
    const char *icon = e["icon"];
    need += strlen(id) + strlen(name) + 2;
    need += strlen(icon && *icon ? icon : "unknown") + 1;
    i++;
  }

  for (JsonObjectConst a : acts) {
    const char *id = a["id"];
    const char *name = a["name"];
    // ha_discover emits one empty placeholder row when there are no
    // actions, because an empty C++ initialiser is invalid. Skip it
    // rather than rejecting the whole layout over it.
    if (!id || !*id) continue;
    if (!name) REJECT("action %s has no name", id);
    need += strlen(id) + strlen(name) + 2;
  }

  for (JsonObjectConst s : sens) {
    const char *id = s["id"];
    if (!id || !*id) continue;
    int area = s["area"] | -1;
    if (area < 0 || area >= (int)areas.size())
      REJECT("sensor %s has area %d, only %u areas", id, area, (unsigned)areas.size());
    need += strlen(id) + 1;
  }

  const char *status = root["status"];
  if (status && *status) need += strlen(status) + 1;

  if (need > ARENA_N)
    REJECT("layout needs %u bytes of names, arena is %u",
           (unsigned)need, (unsigned)ARENA_N);

  return true;
}

// ------------------------------------------------------------
//  Population
// ------------------------------------------------------------

// Assumes validate() has already passed, so the only failure left is
// the arena, which is checked anyway rather than trusted.
static bool populate(JsonObjectConst root) {
  arena_used = 0;
  memset(&g_layout, 0, sizeof(g_layout));

  JsonArrayConst areas = root["areas"];
  for (JsonVariantConst a : areas) {
    const char *s = intern(a.as<const char *>());
    if (!s) return false;
    g_layout.areas[g_layout.area_n++] = s;
  }

  JsonArrayConst ents = root["entities"];
  for (JsonObjectConst e : ents) {
    LayoutEntity &d = g_layout.ents[g_layout.ent_n];
    d.id = intern(e["id"]);
    d.name = intern(e["name"]);
    const char *icon = e["icon"];
    d.icon = intern(icon && *icon ? icon : icon_for_domain(e["id"]));
    if (!d.id || !d.name || !d.icon) return false;
    d.kind = kind_from(e["kind"]);
    d.area = (uint8_t)(e["area"] | 0);
    d.favourite = e["fav"] | false;
    g_layout.ent_n++;
  }

  JsonArrayConst acts = root["actions"];
  for (JsonObjectConst a : acts) {
    const char *id = a["id"];
    if (!id || !*id) continue;
    LayoutAction &d = g_layout.acts[g_layout.act_n];
    d.id = intern(id);
    d.name = intern(a["name"]);
    if (!d.id || !d.name) return false;
    // Optional, and lenient like the theme: a malformed colour costs
    // the button its colour, not the layout.
    d.has_color = colour_from(a["color"], &d.color);
    g_layout.act_n++;
  }

  JsonArrayConst sens = root["sensors"];
  for (JsonObjectConst s : sens) {
    const char *id = s["id"];
    if (!id || !*id) continue;
    LayoutSensor &d = g_layout.sens[g_layout.sens_n];
    d.id = intern(id);
    if (!d.id) return false;
    d.area = (uint8_t)(s["area"] | 0);
    d.humidity = s["hum"] | false;
    g_layout.sens_n++;
  }

  const char *status = root["status"];
  if (status && *status) {
    g_layout.status = intern(status);
    if (!g_layout.status) return false;
  }

  apply_theme(root);

  g_layout.from_nvs = true;
  return true;
}

// ------------------------------------------------------------
//  Public
// ------------------------------------------------------------

void layout_boot() {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, true)) {   // read-only; absent namespace is normal
    Serial.println("[layout] no NVS namespace yet, using the compiled layout");
    load_builtin();
    return;
  }

  size_t n = prefs.getString(NVS_KEY, nvs_buf, sizeof(nvs_buf));
  prefs.end();

  if (n == 0) {
    Serial.println("[layout] nothing stored, using the compiled layout");
    load_builtin();
    return;
  }

  // Zero-copy: the document points into nvs_buf, which is static.
  StaticJsonDocument<4096> &doc = parse_doc;
  DeserializationError jerr = deserializeJson(doc, nvs_buf);
  if (jerr) {
    Serial.printf("[layout] stored layout will not parse (%s), using the compiled one\n",
                  jerr.c_str());
    load_builtin();
    return;
  }

  char err[160];
  if (!validate(doc.as<JsonObjectConst>(), err, sizeof(err))) {
    // Reachable if the firmware was downgraded under a stored layout.
    Serial.printf("[layout] stored layout rejected: %s\n", err);
    Serial.println("[layout] using the compiled layout");
    load_builtin();
    return;
  }

  if (!populate(doc.as<JsonObjectConst>())) {
    Serial.println("[layout] stored layout did not fit the arena, using the compiled one");
    load_builtin();
    return;
  }

  Serial.printf("[layout] loaded from NVS: %u entities, %u areas, %u sensors (%u/%u arena)\n",
                g_layout.ent_n, g_layout.area_n, g_layout.sens_n,
                (unsigned)arena_used, (unsigned)ARENA_N);
}

bool layout_store_json(const char *json, size_t len, char *err, size_t err_n) {
  if (len == 0) {
    snprintf(err, err_n, "empty layout");
    return false;
  }
  if (len > LAYOUT_MAX_JSON) {
    snprintf(err, err_n, "layout is %u bytes, max %u", (unsigned)len, (unsigned)LAYOUT_MAX_JSON);
    return false;
  }

  StaticJsonDocument<4096> &doc = parse_doc;
  DeserializationError jerr = deserializeJson(doc, json, len);
  if (jerr) {
    snprintf(err, err_n, "will not parse: %s", jerr.c_str());
    return false;
  }
  if (!doc.is<JsonObject>()) {
    snprintf(err, err_n, "layout is not an object");
    return false;
  }
  if (!validate(doc.as<JsonObjectConst>(), err, err_n)) return false;

  Preferences prefs;
  if (!prefs.begin(NVS_NS, false)) {
    snprintf(err, err_n, "could not open NVS for writing");
    return false;
  }
  size_t written = prefs.putString(NVS_KEY, json);
  prefs.end();

  if (written == 0) {
    snprintf(err, err_n, "NVS write failed (partition full?)");
    return false;
  }
  return true;
}

void layout_forget() {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, false)) return;
  prefs.remove(NVS_KEY);
  prefs.end();
  Serial.println("[layout] stored layout cleared");
}
