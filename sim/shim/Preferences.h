#pragma once
// The ESP32's NVS (Preferences), held in memory for the simulator.
//
// The simulator's JavaScript keeps a copy and seeds it into each fresh
// module instance before boot, so a stored layout survives the simulated
// restart exactly as it survives a real one. Semantics copied from the
// ESP32 core where layout_store.cpp depends on them:
//   - begin(ns, readOnly=true) fails if the namespace does not exist
//   - getString returns the length *including* the terminator, 0 on miss
//   - putString returns the bytes written, 0 on failure
#include <stddef.h>
#include <map>
#include <string>

class Preferences {
 public:
  bool begin(const char *ns, bool readOnly = false);
  void end() {}
  size_t getString(const char *key, char *value, size_t maxLen);
  size_t putString(const char *key, const char *value);
  bool remove(const char *key);

 private:
  std::string ns_;
  bool open_ = false;
};

// Whole store, keyed "<namespace>/<key>". For the simulator's boot API.
std::map<std::string, std::string> &sim_nvs();
