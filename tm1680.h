#pragma once
#include <Arduino.h>
#include <Wire.h>

// =============================================================================
// TM1680 LED Driver Library
// =============================================================================
// Supports BOTH communication protocols:
//   - 3-Wire (HT1632-style): CLK + DIO + STB bit-banged serial
//   - 2-Wire (I2C):          SCL + SDA via Wire library
//
// The TM1680 drives up to 48 SEG × 16 COM (or 40 SEG × 8 COM) LED matrices.
// =============================================================================

// ---------------------------------------------------------------------------
// 3-Wire (HT1632) command definitions
// ---------------------------------------------------------------------------
// These are 8-bit command values. They are sent with a 3-bit prefix (100)
// followed by the 8-bit command MSB-first, then 1 don't-care bit (12 bits total).
constexpr uint8_t TM1680_CMD_SYS_DIS    = 0x00;  // Turn off system oscillator
constexpr uint8_t TM1680_CMD_SYS_EN     = 0x01;  // Turn on system oscillator
constexpr uint8_t TM1680_CMD_LED_OFF    = 0x02;  // Turn off LED duty cycle generator
constexpr uint8_t TM1680_CMD_LED_ON     = 0x03;  // Turn on LED duty cycle generator
constexpr uint8_t TM1680_CMD_NMOS       = 0x08;  // N-MOS open drain output for COM pins
constexpr uint8_t TM1680_CMD_RC_OSC     = 0x18;  // Use on-chip RC oscillator
constexpr uint8_t TM1680_CMD_8COM       = 0x20;  // 8 COM option (40 SEG × 8 COM)
constexpr uint8_t TM1680_CMD_16COM      = 0x24;  // 16 COM option (48 SEG × 16 COM)
constexpr uint8_t TM1680_CMD_PWM_BASE   = 0xA0;  // PWM brightness: 0xA0-0xAF (16 levels)

// ---------------------------------------------------------------------------
// I2C command definitions
// ---------------------------------------------------------------------------
// These are direct byte values sent as single-byte writes to the I2C address.
constexpr uint8_t TM1680_I2C_SYS_DIS    = 0x80;  // System disable
constexpr uint8_t TM1680_I2C_SYS_EN     = 0x81;  // System enable
constexpr uint8_t TM1680_I2C_LED_OFF    = 0x82;  // LED off
constexpr uint8_t TM1680_I2C_LED_ON     = 0x83;  // LED on
constexpr uint8_t TM1680_I2C_8COM       = 0xA0;  // 8 COM mode
constexpr uint8_t TM1680_I2C_16COM      = 0xA4;  // 16 COM mode
constexpr uint8_t TM1680_I2C_RC_OSC     = 0x9A;  // RC oscillator
constexpr uint8_t TM1680_I2C_PWM_BASE   = 0xE0;  // PWM brightness: 0xE0-0xEF (16 levels)

constexpr uint8_t TM1680_I2C_ADDR       = 0x73;  // 7-bit I2C address (discovered via scan)

// ---------------------------------------------------------------------------
// RAM sizes for each COM mode
// ---------------------------------------------------------------------------
constexpr uint8_t TM1680_RAM_SIZE_8COM  = 32;    // 40 SEG × 8 COM → 32 bytes usable
constexpr uint8_t TM1680_RAM_SIZE_16COM = 48;    // 48 SEG × 16 COM → 48 bytes usable

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------
enum class TM1680_Protocol : uint8_t {
  WIRE3,   // 3-wire bit-banged (CLK, DIO, STB)
  I2C      // 2-wire I2C (SCL, SDA)
};

enum class TM1680_ComMode : uint8_t {
  COM8,    // 8 common lines, 40 segments
  COM16    // 16 common lines, 48 segments
};

// =============================================================================
// TM1680 Class
// =============================================================================
class TM1680 {
public:
  // -------------------------------------------------------------------------
  // Constructor
  // -------------------------------------------------------------------------
  // pin_clk: GPIO for CLK (3-wire) or SCL (I2C)
  // pin_dio: GPIO for DIO (3-wire) or SDA (I2C)
  // pin_stb: GPIO for STB/CS (3-wire only, set 0xFF to disable)
  TM1680(uint8_t pin_clk, uint8_t pin_dio, uint8_t pin_stb = 0xFF);

  // -------------------------------------------------------------------------
  // Initialization
  // -------------------------------------------------------------------------
  // Call once in setup(). Configures pins/I2C, sends init sequence, turns on
  // display at medium brightness.
  void begin(TM1680_Protocol proto = TM1680_Protocol::WIRE3, TM1680_ComMode mode = TM1680_ComMode::COM16);

  // -------------------------------------------------------------------------
  // Display RAM operations
  // -------------------------------------------------------------------------
  // Write a block of data to display RAM starting at startAddr.
  void writeRAM(const uint8_t* data, uint8_t len, uint8_t startAddr = 0);

  // Write a single byte to a specific RAM address.
  void writeRAMByte(uint8_t addr, uint8_t value);

  // -------------------------------------------------------------------------
  // Display control
  // -------------------------------------------------------------------------
  // Send a command appropriate to the current protocol.
  void sendCommand(uint8_t cmd);

  // Set PWM brightness level (0 = dimmest, 15 = brightest).
  void setBrightness(uint8_t level);

  // Turn the LED display on (enable duty cycle generator).
  void displayOn();

  // Turn the LED display off (disable duty cycle generator).
  void displayOff();

  // Clear all display RAM (set to zero).
  void clear();

  // -------------------------------------------------------------------------
  // Configuration
  // -------------------------------------------------------------------------
  // Change COM mode (8 or 16). Sends the command immediately.
  void setComMode(TM1680_ComMode mode);

  // Returns the RAM size in bytes for the current COM mode (32 or 48).
  uint8_t getRAMSize() const;

  // -------------------------------------------------------------------------
  // Getters
  // -------------------------------------------------------------------------
  TM1680_Protocol getProtocol() const { return _protocol; }
  TM1680_ComMode  getComMode()  const { return _comMode; }

private:
  // Pin assignments
  uint8_t _pin_clk;
  uint8_t _pin_dio;
  uint8_t _pin_stb;

  // I2C address (7-bit)
  uint8_t _i2cAddr;

  // Current state
  TM1680_Protocol _protocol;
  TM1680_ComMode  _comMode;
  uint8_t         _brightness;

  // -------------------------------------------------------------------------
  // 3-Wire bit-bang helpers
  // -------------------------------------------------------------------------
  void _3w_begin();                  // Assert STB LOW (start transaction)
  void _3w_end();                    // Release STB HIGH (end transaction)
  void _3w_writeBit(bool bit);       // Clock out one bit on DIO
  void _3w_writeCommand(uint8_t cmd);  // Send 12-bit command frame
  void _3w_writeData(uint8_t addr, const uint8_t* data, uint8_t len);

  // -------------------------------------------------------------------------
  // I2C helpers
  // -------------------------------------------------------------------------
  void _i2c_sendCommand(uint8_t cmd);
  void _i2c_writeData(uint8_t addr, const uint8_t* data, uint8_t len);
};
