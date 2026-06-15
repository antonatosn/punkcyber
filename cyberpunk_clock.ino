// ============================================================================
//  cyberpunk_clock.ino — PUNKCYBER Clock Thin Client
// ============================================================================
//  ESP8266 is a pure display pipe:
//    PC sends 48-byte framebuffer via UDP → ESP writes to TM1680.
//
//  ESP only manages two LEDs locally:
//    - WiFi icon:     blinks during connection, solid when connected
//    - Computer icon: solid when receiving UDP data from PC
//
//  Everything else (time, VU meters, weather, animations) is built by the PC.
// ============================================================================

#include <ESP8266WiFi.h>
#include "tm1680.h"
#include "config.h"
#include "webportal.h"
#include "udp_display.h"
#include "buttons.h"

// ── Buttons (active LOW, internal pullup) ───────────────────────────────────
// Button C is the only one currently wired up (hold 3s = WiFi reset).
// Buttons A (D7/GPIO13) and B (D6/GPIO12) are reserved for future use.
static constexpr uint8_t BTN_C_PIN = 14;   // D5 = Button C  (hold 3s = WiFi reset)

// ── Hardware ────────────────────────────────────────────────────────────────
TM1680        display(5, 4);   // SCL=D1(5), SDA=D2(4)
ConfigManager cfgMgr;
ClockConfig   clockCfg;
WebPortal*    portal = nullptr;
UDPDisplay    udpDisp;
Button        buttonC(BTN_C_PIN);

uint8_t fb[FB_SIZE];           // Raw framebuffer

// Helper to write to display RAM with dirty-checking cache
void updateDisplay(const uint8_t* buffer) {
  static uint8_t lastFb[FB_SIZE] = {0};
  static bool firstFrame = true;

  if (firstFrame || memcmp(buffer, lastFb, FB_SIZE) != 0) {
    display.writeRAM(buffer, FB_SIZE);
    memcpy(lastFb, buffer, FB_SIZE);
    firstFrame = false;
  }
}

// ── ESP-managed LED positions ───────────────────────────────────────────────
static constexpr uint8_t WIFI_LED_BYTE = 0x0A;
static constexpr uint8_t WIFI_LED_BIT  = 0x08;  // byte 0x0A, bit 3
static constexpr uint8_t PC_LED_BYTE   = 0x04;
static constexpr uint8_t PC_LED_BIT    = 0x08;  // byte 0x04, bit 3

// ── State ───────────────────────────────────────────────────────────────────────────
bool inAPMode = false;
unsigned long lastReconnect = 0;
unsigned long wifiLostTime  = 0;
bool          wifiWasConnected = false;

// ── Timing Constants ────────────────────────────────────────────────────────
static constexpr unsigned long RESET_HOLD_MS       = 3000;
static constexpr unsigned long WIFI_CONNECT_TIMEOUT = 15000;
static constexpr unsigned long WIFI_BLINK_FAST     = 100;
static constexpr unsigned long WIFI_BLINK_SLOW     = 300;
static constexpr unsigned long WIFI_BLINK_AP       = 500;
static constexpr unsigned long WIFI_RECONNECT_INT  = 10000;
static constexpr unsigned long WIFI_LOST_TIMEOUT   = 5000;

// ════════════════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println(F("╔══════════════════════════════════════╗"));
  Serial.println(F("║   PUNKCYBER CLOCK — Thin Client     ║"));
  Serial.println(F("╚══════════════════════════════════════╝"));

  // ── Init display ────────────────────────────────────────────────────────
  display.begin(TM1680_Protocol::I2C, TM1680_ComMode::COM16);
  display.setBrightness(8);
  memset(fb, 0, sizeof(fb));
  updateDisplay(fb);

  // ── Buttons ─────────────────────────────────────────────────────────────
  buttonC.begin();

  // ── Load config ───────────────────────────────────────────────────────
  cfgMgr.begin();

  if (!cfgMgr.load(clockCfg)) {
    // No config — start AP portal
    Serial.println(F("[BOOT] No config — starting AP portal"));
    inAPMode = true;
    portal = new WebPortal();
    if (portal) {
      portal->begin(cfgMgr);
    } else {
      Serial.println(F("[BOOT] WebPortal allocation failed!"));
    }
    return;
  }

  // ── Connect to WiFi ──────────────────────────────────────────────────
  Serial.printf_P(PSTR("[BOOT] Connecting to: %s\n"), clockCfg.ssid);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(clockCfg.ssid, clockCfg.password);

  // Blink WiFi LED while connecting (max 15 seconds)
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT) {
    bool on = (millis() / WIFI_BLINK_SLOW) % 2;
    memset(fb, 0, sizeof(fb));
    if (on) fb[WIFI_LED_BYTE] |= WIFI_LED_BIT;
    updateDisplay(fb);
    delay(50);
    yield();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf_P(PSTR("[BOOT] Connected! IP: %s\n"), WiFi.localIP().toString().c_str());
    wifiWasConnected = true;
    // Solid WiFi LED
    memset(fb, 0, sizeof(fb));
    fb[WIFI_LED_BYTE] |= WIFI_LED_BIT;
    updateDisplay(fb);

    // Start UDP listener
    udpDisp.begin(clockCfg.pcIP, clockCfg.pcPort);
  } else {
    Serial.println(F("[BOOT] WiFi failed — starting AP portal"));
    inAPMode = true;
    portal = new WebPortal();
    if (portal) {
      portal->begin(cfgMgr);
    } else {
      Serial.println(F("[BOOT] WebPortal allocation failed!"));
    }
  }
}

// ════════════════════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════════════════════
void loop() {
  // ── Button C: hold 3s = wipe WiFi config ───────────────────────
  if (buttonC.update() == ButtonEvent::LONG_PRESS) {
    Serial.println(F("[BTN] 3s hold — wiping config!"));
    cfgMgr.reset();
    // Flash all LEDs to confirm
    memset(fb, 0xFF, sizeof(fb));
    updateDisplay(fb);
    delay(500);
    memset(fb, 0, sizeof(fb));
    updateDisplay(fb);
    delay(500);
    ESP.restart();
  }

  // ── AP mode: run portal ───────────────────────────────────────────
  if (inAPMode) {
    if (portal) {
      portal->update();
      // Blink WiFi LED in AP mode
      bool on = (millis() / WIFI_BLINK_AP) % 2;
      memset(fb, 0, sizeof(fb));
      if (on) fb[WIFI_LED_BYTE] |= WIFI_LED_BIT;
      updateDisplay(fb);
      if (portal->isSaved()) {
        delay(1000);
        ESP.restart();
      }
    }
    return;
  }

  // ── STA mode: WiFi reconnect ─────────────────────────────────────
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiWasConnected && wifiLostTime == 0) {
      wifiLostTime = millis();
      Serial.println(F("[WIFI] Connection lost"));
    }
    unsigned long downTime = millis() - wifiLostTime;

    if (downTime < WIFI_LOST_TIMEOUT) {
      // First 5 seconds: WiFi LED OFF
      fb[WIFI_LED_BYTE] &= ~WIFI_LED_BIT;
    } else {
      // After 5s: rapid flash (100ms) while retrying
      bool on = (millis() / WIFI_BLINK_FAST) % 2;
      if (on) fb[WIFI_LED_BYTE] |= WIFI_LED_BIT;
      else    fb[WIFI_LED_BYTE] &= ~WIFI_LED_BIT;

      if (millis() - lastReconnect > WIFI_RECONNECT_INT) {
        Serial.println(F("[WIFI] Reconnecting..."));
        WiFi.reconnect();
        lastReconnect = millis();
      }
    }
    fb[PC_LED_BYTE] &= ~PC_LED_BIT;
    updateDisplay(fb);
    return;
  }

  // WiFi just reconnected
  if (wifiLostTime != 0) {
    wifiLostTime = 0;
    Serial.printf_P(PSTR("[WIFI] Reconnected! IP: %s\n"), WiFi.localIP().toString().c_str());
  }

  // ── Receive framebuffer from PC ───────────────────────────────────────
  udpDisp.update(fb);

  // ── Overlay ESP-managed LEDs ──────────────────────────────────────────
  // WiFi: solid ON when connected
  fb[WIFI_LED_BYTE] |= WIFI_LED_BIT;

  // Computer: ON when receiving UDP from PC
  if (udpDisp.isReceiving()) {
    fb[PC_LED_BYTE] |= PC_LED_BIT;
  } else {
    fb[PC_LED_BYTE] &= ~PC_LED_BIT;
  }

  // ── Write to TM1680 (with dirty frame check) ─────────────────────────
  updateDisplay(fb);
}
