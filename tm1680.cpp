// =============================================================================
// TM1680 LED Driver Library — Implementation
// =============================================================================
// Dual-protocol driver for the TM1680 LED matrix controller.
//
// 3-Wire Protocol (HT1632-style):
//   Uses CLK, DIO, and STB (active-low chip select) with bit-banged serial.
//   Command frame: 100 + 8-bit cmd MSB-first + 1 don't-care = 12 bits
//   Data frame:    101 + 7-bit addr MSB-first + N×8-bit data LSB-first
//   Data is latched on the RISING edge of CLK.
//
// I2C Protocol:
//   Uses standard Wire library. Commands are single-byte writes.
//   Data writes send the start address byte followed by data bytes.
// =============================================================================

#include "tm1680.h"

// Bit-bang timing delay in microseconds.
// 4µs is conservative (~125 kHz effective clock) for reliable operation
// across different ESP8266 clock speeds and wiring lengths.
constexpr uint8_t BIT_DELAY_US = 4;

// =============================================================================
// Constructor
// =============================================================================
TM1680::TM1680(uint8_t pin_clk, uint8_t pin_dio, uint8_t pin_stb)
  : _pin_clk(pin_clk)
  , _pin_dio(pin_dio)
  , _pin_stb(pin_stb)
  , _i2cAddr(TM1680_I2C_ADDR)
  , _protocol(TM1680_Protocol::WIRE3)
  , _comMode(TM1680_ComMode::COM16)
  , _brightness(8)
{
}

// =============================================================================
// begin() — Initialize the driver and display
// =============================================================================
// Sequence: SYS_DIS → set COM mode → set NMOS (3-wire) → RC_OSC → SYS_EN
//           → LED_ON → setBrightness(8)
void TM1680::begin(TM1680_Protocol proto, TM1680_ComMode mode) {
  _protocol = proto;
  _comMode  = mode;

  if (_protocol == TM1680_Protocol::I2C) {
    // Initialize I2C with specified pins
    // Wire.begin(SDA, SCL) — note: ESP8266 Wire uses (SDA, SCL) order
    Wire.begin(_pin_dio, _pin_clk);
    Wire.setClock(400000);  // 400 kHz fast mode (4× faster than default)

    Serial.println(F("[TM1680] I2C mode initialized"));
  } else {
    // 3-Wire: configure GPIO pins
    pinMode(_pin_clk, OUTPUT);
    pinMode(_pin_dio, OUTPUT);
    pinMode(_pin_stb, OUTPUT);

    // Idle state: CLK LOW, STB HIGH (deselected)
    digitalWrite(_pin_clk, LOW);
    digitalWrite(_pin_stb, HIGH);

    Serial.println(F("[TM1680] 3-Wire mode initialized"));
  }

  // --- Initialization command sequence ---
  // Step 1: Disable the system (required before configuration changes)
  sendCommand(TM1680_CMD_SYS_DIS);

  // Step 2: Set COM mode (8 or 16 common lines)
  if (_comMode == TM1680_ComMode::COM16) {
    sendCommand(TM1680_CMD_16COM);
  } else {
    sendCommand(TM1680_CMD_8COM);
  }

  // Step 3: Set N-MOS open drain for COM pins (3-wire only; harmless on I2C)
  if (_protocol == TM1680_Protocol::WIRE3) {
    sendCommand(TM1680_CMD_NMOS);
  }

  // Step 4: Select RC oscillator (on-chip, no external crystal needed)
  sendCommand(TM1680_CMD_RC_OSC);

  // Step 5: Enable the system oscillator
  sendCommand(TM1680_CMD_SYS_EN);

  // Step 6: Turn on LED duty cycle generator
  sendCommand(TM1680_CMD_LED_ON);

  // Step 7: Set default brightness (mid-level)
  setBrightness(8);

  Serial.print(F("[TM1680] Display on, COM="));
  Serial.print((_comMode == TM1680_ComMode::COM16) ? 16 : 8);
  Serial.print(F(", RAM="));
  Serial.print(getRAMSize());
  Serial.println(F(" bytes"));
}

// =============================================================================
// sendCommand() — Protocol-aware command dispatch
// =============================================================================
// For 3-wire: translates to _3w_writeCommand()
// For I2C: maps the 3-wire command constant to the corresponding I2C constant
void TM1680::sendCommand(uint8_t cmd) {
  if (_protocol == TM1680_Protocol::WIRE3) {
    _3w_writeCommand(cmd);
  } else {
    // Map 3-wire command constants to I2C command bytes.
    // The I2C protocol uses different command encoding.
    uint8_t i2cCmd;

    if (cmd == TM1680_CMD_SYS_DIS) {
      i2cCmd = TM1680_I2C_SYS_DIS;
    } else if (cmd == TM1680_CMD_SYS_EN) {
      i2cCmd = TM1680_I2C_SYS_EN;
    } else if (cmd == TM1680_CMD_LED_OFF) {
      i2cCmd = TM1680_I2C_LED_OFF;
    } else if (cmd == TM1680_CMD_LED_ON) {
      i2cCmd = TM1680_I2C_LED_ON;
    } else if (cmd == TM1680_CMD_8COM) {
      i2cCmd = TM1680_I2C_8COM;
    } else if (cmd == TM1680_CMD_16COM) {
      i2cCmd = TM1680_I2C_16COM;
    } else if (cmd == TM1680_CMD_RC_OSC) {
      i2cCmd = TM1680_I2C_RC_OSC;
    } else if ((cmd & 0xF0) == (TM1680_CMD_PWM_BASE & 0xF0)) {
      // PWM brightness command: extract lower nibble and apply to I2C base
      i2cCmd = TM1680_I2C_PWM_BASE | (cmd & 0x0F);
    } else {
      // For unmapped commands (e.g., NMOS), attempt direct I2C send.
      // This may be a no-op on I2C, which is acceptable.
      i2cCmd = cmd;
    }

    _i2c_sendCommand(i2cCmd);
  }
}

// =============================================================================
// writeRAM() — Write a block of data to display RAM
// =============================================================================
void TM1680::writeRAM(const uint8_t* data, uint8_t len, uint8_t startAddr) {
  if (data == nullptr || len == 0) return;

  // Clamp to available RAM
  uint8_t ramSize = getRAMSize();
  if (startAddr >= ramSize) return;
  if (startAddr + len > ramSize) {
    len = ramSize - startAddr;
  }

  if (_protocol == TM1680_Protocol::WIRE3) {
    _3w_writeData(startAddr, data, len);
  } else {
    _i2c_writeData(startAddr, data, len);
  }
}

// =============================================================================
// writeRAMByte() — Write a single byte to a specific RAM address
// =============================================================================
void TM1680::writeRAMByte(uint8_t addr, uint8_t value) {
  writeRAM(&value, 1, addr);
}

// =============================================================================
// setBrightness() — Set PWM brightness level (0-15)
// =============================================================================
void TM1680::setBrightness(uint8_t level) {
  _brightness = (level > 15) ? 15 : level;

  // PWM command = base + level (0x00 to 0x0F)
  sendCommand(TM1680_CMD_PWM_BASE | _brightness);
}

// =============================================================================
// displayOn() / displayOff()
// =============================================================================
void TM1680::displayOn() {
  sendCommand(TM1680_CMD_LED_ON);
}

void TM1680::displayOff() {
  sendCommand(TM1680_CMD_LED_OFF);
}

// =============================================================================
// clear() — Zero all display RAM
// =============================================================================
void TM1680::clear() {
  uint8_t ramSize = getRAMSize();
  uint8_t zeros[TM1680_RAM_SIZE_16COM];  // Max size buffer on stack
  memset(zeros, 0, ramSize);
  writeRAM(zeros, ramSize, 0);
}

// =============================================================================
// setComMode() — Change COM mode and send the command
// =============================================================================
void TM1680::setComMode(TM1680_ComMode mode) {
  _comMode = mode;
  if (_comMode == TM1680_ComMode::COM16) {
    sendCommand(TM1680_CMD_16COM);
  } else {
    sendCommand(TM1680_CMD_8COM);
  }
}

// =============================================================================
// getRAMSize() — Returns display RAM size in bytes for current COM mode
// =============================================================================
uint8_t TM1680::getRAMSize() const {
  return (_comMode == TM1680_ComMode::COM16) ? TM1680_RAM_SIZE_16COM : TM1680_RAM_SIZE_8COM;
}

// =============================================================================
//  3-Wire Protocol Implementation
// =============================================================================

// ---------------------------------------------------------------------------
// _3w_begin() — Start a 3-wire transaction (assert STB LOW)
// ---------------------------------------------------------------------------
void TM1680::_3w_begin() {
  digitalWrite(_pin_stb, LOW);
  delayMicroseconds(BIT_DELAY_US);  // Setup time after STB falls
}

// ---------------------------------------------------------------------------
// _3w_end() — End a 3-wire transaction (release STB HIGH)
// ---------------------------------------------------------------------------
void TM1680::_3w_end() {
  delayMicroseconds(BIT_DELAY_US);  // Hold time before STB rises
  digitalWrite(_pin_stb, HIGH);
  delayMicroseconds(BIT_DELAY_US);  // Inter-transaction gap
}

// ---------------------------------------------------------------------------
// _3w_writeBit() — Clock out a single bit on DIO
// ---------------------------------------------------------------------------
// Data is latched by the TM1680 on the RISING edge of CLK.
// Sequence: CLK LOW → set DIO → delay → CLK HIGH → delay
void TM1680::_3w_writeBit(bool bit) {
  digitalWrite(_pin_clk, LOW);
  delayMicroseconds(BIT_DELAY_US);

  digitalWrite(_pin_dio, bit ? HIGH : LOW);
  delayMicroseconds(BIT_DELAY_US);

  digitalWrite(_pin_clk, HIGH);      // Rising edge latches the data
  delayMicroseconds(BIT_DELAY_US);
}

// ---------------------------------------------------------------------------
// _3w_writeCommand() — Send a 12-bit command frame
// ---------------------------------------------------------------------------
// Frame format (MSB first):
//   Bit 11-9:  100 (command prefix)
//   Bit 8-1:   8-bit command (MSB first)
//   Bit 0:     X (don't care)
//
// Total: 12 bits clocked out with STB held LOW.
void TM1680::_3w_writeCommand(uint8_t cmd) {
  _3w_begin();

  // --- 3-bit prefix: 1, 0, 0 (command mode) ---
  _3w_writeBit(1);
  _3w_writeBit(0);
  _3w_writeBit(0);

  // --- 8-bit command, MSB first ---
  for (int8_t i = 7; i >= 0; i--) {
    _3w_writeBit((cmd >> i) & 0x01);
  }

  // --- 1 don't-care bit ---
  _3w_writeBit(0);

  _3w_end();

  yield();  // Feed ESP8266 watchdog
}

// ---------------------------------------------------------------------------
// _3w_writeData() — Send display RAM data via 3-wire protocol
// ---------------------------------------------------------------------------
// Frame format:
//   Bits [2:0]:  101 (write data prefix)
//   Bits [9:3]:  7-bit RAM address, MSB first
//   Then for each byte: 8 data bits, LSB first (HT1632 convention)
//
// All data bytes are sent consecutively within a single STB transaction.
// The address auto-increments after each byte.
void TM1680::_3w_writeData(uint8_t addr, const uint8_t* data, uint8_t len) {
  _3w_begin();

  // --- 3-bit prefix: 1, 0, 1 (write data mode) ---
  _3w_writeBit(1);
  _3w_writeBit(0);
  _3w_writeBit(1);

  // --- 7-bit address, MSB first ---
  for (int8_t i = 6; i >= 0; i--) {
    _3w_writeBit((addr >> i) & 0x01);
  }

  // --- Data bytes, each sent LSB first ---
  // HT1632-style: data bits within each byte are clocked LSB first.
  // This maps naturally to the column/row structure of the LED matrix.
  for (uint8_t byteIdx = 0; byteIdx < len; byteIdx++) {
    uint8_t b = data[byteIdx];
    for (int8_t bit = 0; bit < 8; bit++) {
      _3w_writeBit((b >> bit) & 0x01);
    }

    // Feed watchdog every 8 bytes to prevent WDT reset on large writes
    if ((byteIdx & 0x07) == 0x07) {
      yield();
    }
  }

  _3w_end();

  yield();  // Feed ESP8266 watchdog after transaction
}

// =============================================================================
//  I2C Protocol Implementation
// =============================================================================

// ---------------------------------------------------------------------------
// _i2c_sendCommand() — Send a single command byte via I2C
// ---------------------------------------------------------------------------
// Format: [START] [ADDR+W] [CMD_BYTE] [STOP]
void TM1680::_i2c_sendCommand(uint8_t cmd) {
  Wire.beginTransmission(_i2cAddr);
  Wire.write(cmd);
  uint8_t err = Wire.endTransmission();

  if (err != 0) {
    Serial.print(F("[TM1680] I2C cmd error: "));
    Serial.println(err);
  }
}

// ---------------------------------------------------------------------------
// _i2c_writeData() — Write display RAM data via I2C
// ---------------------------------------------------------------------------
// IMPORTANT: The TM1680 uses NIBBLE addressing in I2C mode!
// Each RAM byte is at I2C address = byte_address × 2.
// The auto-increment after a byte write goes by 1 nibble (not 1 byte),
// so bulk writes cause data corruption. We write each byte individually.
//
// Format per byte: [START] [ADDR+W] [NIBBLE_ADDR] [DATA] [STOP]
void TM1680::_i2c_writeData(uint8_t addr, const uint8_t* data, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    uint8_t nibbleAddr = (addr + i) * 2;  // *** NIBBLE ADDRESS ***

    Wire.beginTransmission(_i2cAddr);
    Wire.write(nibbleAddr);
    Wire.write(data[i]);
    uint8_t err = Wire.endTransmission();

    if (err != 0) {
      Serial.print(F("[TM1680] I2C write error at 0x"));
      Serial.print(nibbleAddr, HEX);
      Serial.print(F(": "));
      Serial.println(err);
      return;
    }

    // Feed watchdog every 16 bytes
    if ((i & 0x0F) == 0x0F) yield();
  }
}
