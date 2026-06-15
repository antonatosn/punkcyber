// ============================================================================
//  udp_display.cpp — Raw framebuffer pipe over UDP
// ============================================================================
#include <ESP8266WiFi.h>
#include "udp_display.h"
#include "config.h"

extern ConfigManager cfgMgr;

UDPDisplay::UDPDisplay()
  : _pcPort(UDP_PORT), _lastPacket(0), _lastHeartbeat(0),
    _started(false), _hasPC(false)
{}

void UDPDisplay::begin(const char* pcIP, uint16_t pcPort) {
  _pcPort = pcPort ? pcPort : UDP_PORT;

  if (pcIP && strlen(pcIP) > 0) {
    _pcIP.fromString(pcIP);
    _hasPC = true;
  }

  // Load configuration for remote mutations
  if (!cfgMgr.load(_pendingCfg)) {
    cfgMgr.setDefaults(_pendingCfg);
  }

  if (_udp.begin(_pcPort)) {
    _started = true;
    Serial.printf_P(PSTR("[UDP] Listening on port %u"), _pcPort);
    if (_hasPC) Serial.printf_P(PSTR(", heartbeat -> %s"), pcIP);
    Serial.println();
  }
}

void UDPDisplay::update(uint8_t* fb) {
  if (!_started) return;

  // Send heartbeat to PC every 2 seconds
  if (_hasPC && (millis() - _lastHeartbeat) > HEARTBEAT_MS) {
    _udp.beginPacket(_pcIP, _pcPort);
    _udp.write((uint8_t)0xFF);
    _udp.endPacket();
    _lastHeartbeat = millis();
  }

  // Receive packets
  int pktSize;
  while ((pktSize = _udp.parsePacket()) > 0) {
    if (pktSize == FB_SIZE) {
      // 48-byte framebuffer — always write directly, never inspect for config
      _udp.read(fb, FB_SIZE);
      _lastPacket = millis();
    } else if (pktSize >= 2) {
      // NOTE: a config packet is [0xC0, key, value...]. Any packet of exactly
      // FB_SIZE (48) bytes was handled as a framebuffer above, so a config
      // value of exactly 46 bytes (2-byte header + 46 = 48) would be misread.
      // Only the password field can reach that length; avoid 46-char passwords
      // over UDP (use the web portal for those instead).
      uint8_t firstByte = _udp.peek();
      if (firstByte == 0xC0) {
        // Config packet — read into buffer (up to 127 bytes)
        uint8_t buf[128];
        int len = (pktSize > 127) ? 127 : pktSize;
        _udp.read(buf, len);
        buf[len] = '\0';

        // Validate key and handle configuration
        uint8_t key = buf[1];
        if (key == 0x01 || key == 0x02 || key == 0x03 || key == 0x04 || key == 0xFF) {
          _handleConfig(buf, len);
        }
      } else {
        // Discard unknown packet
        while (_udp.available()) {
          _udp.read();
        }
      }
    } else {
      // Discard packet
      while (_udp.available()) {
        _udp.read();
      }
    }
  }

  // Decay — clear display if stale
  if (_lastPacket > 0 && (millis() - _lastPacket) > STALE_MS) {
    memset(fb, 0, FB_SIZE);
    _lastPacket = 0;
  }
}

bool UDPDisplay::isReceiving() const {
  if (!_started || _lastPacket == 0) return false;
  return (millis() - _lastPacket) < STALE_MS;
}

// ── Remote config update via UDP ────────────────────────────────────────────
//  [0xC0, key, ...value]
//  key: 0x01=ssid, 0x02=pass, 0x03=pcIP, 0x04=pcPort(2 bytes LE), 0xFF=save+reboot
void UDPDisplay::_handleConfig(const uint8_t* buf, int len) {
  uint8_t key = buf[1];
  const char* val = (const char*)&buf[2];

  switch (key) {
    case 0x01:
      strlcpy(_pendingCfg.ssid, val, sizeof(_pendingCfg.ssid));
      Serial.printf_P(PSTR("[UDP] Config SSID=%s\n"), _pendingCfg.ssid);
      break;
    case 0x02:
      strlcpy(_pendingCfg.password, val, sizeof(_pendingCfg.password));
      Serial.println(F("[UDP] Config password updated"));
      break;
    case 0x03:
      strlcpy(_pendingCfg.pcIP, val, sizeof(_pendingCfg.pcIP));
      Serial.printf_P(PSTR("[UDP] Config pcIP=%s\n"), _pendingCfg.pcIP);
      break;
    case 0x04:
      if (len >= 4) {
        _pendingCfg.pcPort = buf[2] | (buf[3] << 8);
        Serial.printf_P(PSTR("[UDP] Config pcPort=%u\n"), _pendingCfg.pcPort);
      }
      break;
    case 0xFF:
      cfgMgr.save(_pendingCfg);
      Serial.println(F("[UDP] Config saved — rebooting"));
      delay(500);
      ESP.restart();
      break;
  }
}
