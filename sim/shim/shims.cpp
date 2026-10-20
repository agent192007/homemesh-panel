#include "Arduino.h"
#include "Preferences.h"

SimSerial Serial;

std::map<std::string, std::string> &sim_nvs() {
  static std::map<std::string, std::string> store;
  return store;
}

bool Preferences::begin(const char *ns, bool readOnly) {
  ns_ = ns;
  if (readOnly) {
    // As on the ESP32: a namespace that was never written cannot be
    // opened read-only. layout_store.cpp treats that as "nothing stored".
    std::string prefix = ns_ + "/";
    bool any = false;
    for (auto &kv : sim_nvs())
      if (kv.first.compare(0, prefix.size(), prefix) == 0) any = true;
    if (!any) return false;
  }
  open_ = true;
  return true;
}

size_t Preferences::getString(const char *key, char *value, size_t maxLen) {
  if (!open_) return 0;
  auto it = sim_nvs().find(ns_ + "/" + key);
  if (it == sim_nvs().end()) return 0;
  size_t need = it->second.size() + 1;
  if (need > maxLen) return 0;
  memcpy(value, it->second.c_str(), need);
  return need;
}

size_t Preferences::putString(const char *key, const char *value) {
  if (!open_) return 0;
  sim_nvs()[ns_ + "/" + key] = value;
  return strlen(value);
}

bool Preferences::remove(const char *key) {
  if (!open_) return false;
  return sim_nvs().erase(ns_ + "/" + key) > 0;
}
