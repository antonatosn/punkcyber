#pragma once
// ============================================================================
//  buttons.h — Debounced button handler with long-press & auto-repeat
// ============================================================================
//  Designed for ESP8266 where delay() is forbidden in the main loop.
//  All timing uses millis() and is fully non-blocking.
//
//  Usage:
//    Button btn(D3);
//    btn.begin();
//    // in loop():
//    ButtonEvent evt = btn.update();
//    if (evt == ButtonEvent::SHORT_PRESS) { ... }
// ============================================================================

#include <Arduino.h>

static constexpr unsigned long BTN_DEBOUNCE_MS   = 50;     // Debounce window
static constexpr unsigned long BTN_LONG_PRESS_MS = 3000;   // Hold threshold for long press
static constexpr unsigned long BTN_REPEAT_MS     = 200;    // Auto-repeat interval while held

enum class ButtonEvent {
  NONE,          // Nothing happened
  SHORT_PRESS,   // Released before long-press threshold
  LONG_PRESS,    // Held past threshold (fires once)
  REPEAT         // Fires repeatedly while held after long press
};

class Button {
public:
  Button(uint8_t pin, bool activeLow = true);

  void        begin();     // Configure pin mode
  ButtonEvent update();    // Poll — call every loop()
  bool        isPressed() const;

private:
  uint8_t       _pin;
  bool          _activeLow;
  bool          _lastReading;     // Raw pin state from previous read
  bool          _state;           // Debounced logical state (true = pressed)
  bool          _longPressFired;  // Guards one-shot long-press event
  unsigned long _lastDebounce;    // Timestamp of last state-change
  unsigned long _pressStart;      // Timestamp when button was pressed
  unsigned long _lastRepeat;      // Timestamp of last repeat event
};
