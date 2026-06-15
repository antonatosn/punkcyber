/*
 * config.h — Minimal persistent config for Cyberpunk Clock
 * ────────────────────────────────────────────────────────
 * Only stores what's needed to connect: WiFi + PC address.
 * Everything else (time, weather, stats) comes from PC via UDP.
 */

#pragma once
#include <Arduino.h>

static constexpr uint16_t CONFIG_MAGIC   = 0xCB0A;
static constexpr uint8_t  CONFIG_VERSION = 1;

struct __attribute__((packed)) ClockConfig {
  uint16_t magic;
  uint8_t  version;
  char     ssid[33];
  char     password[65];
  char     pcIP[16];       // 15 chars max + null terminator
  uint16_t pcPort;         // UDP port (default 8889)
};

class ConfigManager {
public:
  ConfigManager();

  void begin();
  bool load(ClockConfig& cfg);
  bool save(const ClockConfig& cfg);
  void reset();
  void setDefaults(ClockConfig& cfg);
};
