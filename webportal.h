/*
 * webportal.h — AP-only captive portal for WiFi config
 * ─────────────────────────────────────────────────────
 * Only runs in AP mode. Minimal config: WiFi + PC IP + port.
 * Dropped in STA mode to save RAM.
 */

#pragma once
#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include "config.h"

static constexpr const char* AP_SSID = "PUNKCYBER-Clock";
static constexpr uint8_t AP_CHANNEL  = 1;
static constexpr uint8_t DNS_PORT    = 53;

class WebPortal {
public:
  WebPortal();

  void begin(ConfigManager& cfgMgr);
  void update();
  bool isSaved() const { return _saved; }

private:
  ESP8266WebServer _server;
  DNSServer        _dns;
  ConfigManager*   _cfgMgr;
  bool             _saved;

  void _handleRoot();
  void _handleScan();
  void _handleSave();
  void _handleNotFound();
  void _setupRoutes();

  static const char PAGE_HTML[] PROGMEM;
};
