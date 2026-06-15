#pragma once
// ============================================================================
//  udp_display.h — Raw framebuffer pipe over UDP
// ============================================================================
//  PC sends 48 bytes = complete TM1680 RAM. ESP just writes to display.
//  Zero display logic on ESP. PC is the brain.
// ============================================================================

#include <Arduino.h>
#include <WiFiUdp.h>
#include <IPAddress.h>
#include "config.h"

static constexpr uint16_t UDP_PORT      = 8889;
static constexpr uint16_t STALE_MS      = 3000;
static constexpr uint16_t HEARTBEAT_MS  = 2000;
static constexpr uint8_t  FB_SIZE       = 48;

class UDPDisplay {
public:
  UDPDisplay();

  void begin(const char* pcIP, uint16_t pcPort);
  void update(uint8_t* fb);   // Receives into fb[48], returns via pointer
  bool isReceiving() const;

private:
  WiFiUDP       _udp;
  IPAddress     _pcIP;
  uint16_t      _pcPort;
  unsigned long _lastPacket;
  unsigned long _lastHeartbeat;
  bool          _started;
  bool          _hasPC;
  ClockConfig   _pendingCfg;

  void _handleConfig(const uint8_t* buf, int len);
};
