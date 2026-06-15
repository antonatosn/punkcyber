/*
 * config.cpp — Direct flash sector config storage
 * ────────────────────────────────────────────────
 * Writes ClockConfig directly to a flash sector near the end of flash.
 * Bypasses EEPROM library which has issues on 512KB flash.
 */

#include <ESP8266WiFi.h>
#include "config.h"

// Use a flash sector near the end but away from WiFi SDK config area
// SDK uses last 4 sectors. We use sector (total - 5).
static uint32_t getConfigSector() {
  return (ESP.getFlashChipSize() / SPI_FLASH_SEC_SIZE) - 5;
}

static uint32_t getConfigAddr() {
  return getConfigSector() * SPI_FLASH_SEC_SIZE;
}

ConfigManager::ConfigManager() {}

void ConfigManager::begin() {
  uint32_t sector = getConfigSector();
  uint32_t addr   = getConfigAddr();
  Serial.printf_P(PSTR("[CFG] Flash: %uKB, config sector: %u (addr 0x%05X)\n"),
                  static_cast<uint32_t>(ESP.getFlashChipSize() / 1024), sector, addr);
}

void ConfigManager::setDefaults(ClockConfig& cfg) {
  memset(&cfg, 0, sizeof(ClockConfig));
  cfg.magic = CONFIG_MAGIC;
  cfg.version = CONFIG_VERSION;
  cfg.pcPort = 8889;
}

bool ConfigManager::load(ClockConfig& cfg) {
  setDefaults(cfg);

  uint32_t addr = getConfigAddr();

  // Read config from flash (must be 4-byte aligned)
  constexpr uint32_t readLen = (sizeof(ClockConfig) + 3) & ~3;
  uint8_t buf[readLen] __attribute__((aligned(4)));
  memset(buf, 0, readLen);

  if (!ESP.flashRead(addr, reinterpret_cast<uint32_t*>(buf), readLen)) {
    Serial.println(F("[CFG] Flash read failed"));
    return false;
  }

  ClockConfig readCfg;
  memcpy(&readCfg, buf, sizeof(ClockConfig));

  if (readCfg.magic != CONFIG_MAGIC || readCfg.version != CONFIG_VERSION) {
    Serial.println(F("[CFG] No valid config in flash"));
    return false;
  }

  memcpy(&cfg, &readCfg, sizeof(ClockConfig));

  // Ensure null-terminated strings (flash corruption safety)
  cfg.ssid[sizeof(cfg.ssid) - 1] = '\0';
  cfg.password[sizeof(cfg.password) - 1] = '\0';
  cfg.pcIP[sizeof(cfg.pcIP) - 1] = '\0';

  if (strlen(cfg.ssid) > 0) {
    Serial.printf_P(PSTR("[CFG] Loaded: SSID=%s, PC=%s:%d\n"),
                    cfg.ssid, cfg.pcIP, cfg.pcPort);
  }
  return (strlen(cfg.ssid) > 0);
}

bool ConfigManager::save(const ClockConfig& cfg) {
  uint32_t sector = getConfigSector();
  uint32_t addr   = getConfigAddr();
  constexpr uint32_t writeLen = (sizeof(ClockConfig) + 3) & ~3;  // Round up to 4-byte boundary

  Serial.printf_P(PSTR("[CFG] Saving to sector %u (addr 0x%05X), size=%zu, aligned=%u\n"),
                  sector, addr, sizeof(ClockConfig), writeLen);

  // Copy to 4-byte aligned buffer
  uint8_t buf[writeLen] __attribute__((aligned(4)));
  memset(buf, 0, writeLen);
  memcpy(buf, &cfg, sizeof(ClockConfig));

  // Disable WiFi to prevent flash contention
  WiFi.mode(WIFI_OFF);
  delay(100);

  // Erase the sector first
  if (!ESP.flashEraseSector(sector)) {
    Serial.println(F("[CFG] Flash erase failed!"));
    return false;
  }
  delay(10);

  // Write config
  if (!ESP.flashWrite(addr, reinterpret_cast<uint32_t*>(buf), writeLen)) {
    Serial.println(F("[CFG] Flash write failed!"));
    return false;
  }

  // Verify by reading back
  uint8_t vbuf[writeLen] __attribute__((aligned(4)));
  memset(vbuf, 0, writeLen);
  ESP.flashRead(addr, reinterpret_cast<uint32_t*>(vbuf), writeLen);

  if (memcmp(buf, vbuf, sizeof(ClockConfig)) == 0) {
    Serial.println(F("[CFG] Config saved & verified ✓"));
    return true;
  } else {
    Serial.println(F("[CFG] Verify failed!"));
    // Debug: show first difference
    for (uint32_t i = 0; i < sizeof(ClockConfig); i++) {
      if (buf[i] != vbuf[i]) {
        Serial.printf_P(PSTR("[CFG] Diff at byte %u: wrote 0x%02X, read 0x%02X\n"), i, buf[i], vbuf[i]);
        break;
      }
    }
    return false;
  }
}

void ConfigManager::reset() {
  uint32_t sector = getConfigSector();
  WiFi.mode(WIFI_OFF);
  delay(50);
  ESP.flashEraseSector(sector);
  Serial.println(F("[CFG] Flash config erased"));
}
