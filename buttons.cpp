// ============================================================================
//  buttons.cpp — Debounced button handler implementation
// ============================================================================
#include "buttons.h"

// ── Constructor ─────────────────────────────────────────────────────────────
Button::Button(uint8_t pin, bool activeLow)
  : _pin(pin),
    _activeLow(activeLow),
    _lastReading(false),
    _state(false),
    _longPressFired(false),
    _lastDebounce(0),
    _pressStart(0),
    _lastRepeat(0)
{}

// ── begin ───────────────────────────────────────────────────────────────────
//  Configure the GPIO.  Active-low buttons use INPUT_PULLUP so they read
//  HIGH when open and LOW when pressed.
void Button::begin() {
  if (_activeLow) {
    pinMode(_pin, INPUT_PULLUP);
  } else {
    pinMode(_pin, INPUT);
  }

  // Initialise state from current pin reading
  bool raw = digitalRead(_pin);
  _lastReading = raw;
  _state = _activeLow ? !raw : raw;
}

// ── isPressed ───────────────────────────────────────────────────────────────
bool Button::isPressed() const {
  return _state;
}

// ── update ──────────────────────────────────────────────────────────────────
//  Call once per loop().  Returns the highest-priority event detected this
//  iteration (NONE in most iterations).
//
//  State machine:
//    1. Read pin, debounce.
//    2. On debounced press   → record _pressStart.
//    3. While held > 3 s     → fire LONG_PRESS (once), then REPEAT.
//    4. On debounced release → fire SHORT_PRESS if < 3 s.
//
ButtonEvent Button::update() {
  unsigned long now = millis();

  // ── 1. Read & debounce ─────────────────────────────────────────────────
  bool raw = digitalRead(_pin);
  bool reading = _activeLow ? !raw : raw;  // true = pressed

  // If reading changed, reset debounce timer
  if (reading != _lastReading) {
    _lastDebounce = now;
    _lastReading  = reading;
  }

  // Only accept the new state after it's been stable for BTN_DEBOUNCE_MS
  if ((now - _lastDebounce) < BTN_DEBOUNCE_MS) {
    return ButtonEvent::NONE;
  }

  bool prevState = _state;
  _state = reading;

  // ── 2. Rising edge — button just pressed ───────────────────────────────
  if (_state && !prevState) {
    _pressStart     = now;
    _longPressFired = false;
    _lastRepeat     = now;
    return ButtonEvent::NONE;  // Don't fire anything on press-down
  }

  // ── 3. Falling edge — button just released ─────────────────────────────
  if (!_state && prevState) {
    // If long press was already fired, do nothing on release
    if (_longPressFired) {
      return ButtonEvent::NONE;
    }
    // Short press — released before the long-press threshold
    if ((now - _pressStart) < BTN_LONG_PRESS_MS) {
      return ButtonEvent::SHORT_PRESS;
    }
    return ButtonEvent::NONE;
  }

  // ── 4. Button held down — check for long press / repeat ────────────────
  if (_state) {
    unsigned long held = now - _pressStart;

    if (!_longPressFired && held >= BTN_LONG_PRESS_MS) {
      _longPressFired = true;
      _lastRepeat     = now;
      return ButtonEvent::LONG_PRESS;
    }

    if (_longPressFired && (now - _lastRepeat) >= BTN_REPEAT_MS) {
      _lastRepeat = now;
      return ButtonEvent::REPEAT;
    }
  }

  return ButtonEvent::NONE;
}
