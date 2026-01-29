# Daisy Bio - Project Mira
## ESP32 Control System

### Overview
This Arduino sketch provides comprehensive control for the ESP32-based system with:
- Heartbeat LED indicator
- SD card file system
- I2C LCD display
- Rotary encoder input
- PWM-controlled outputs (motor, heater, blower)
- NeoPixel status LED
- Thermistor analog reading
- Serial CLI for remote control

---

## ⚠️ REQUIRED HARDWARE MODIFICATIONS

**CRITICAL:** The current PCB revision requires the following hand modifications before use:

1. **Remove R11** (located near the SD card)
2. **Add power wire:** Connect bottom of C20 (VBUS) to top pin of U1 (+5V)
3. **Add GPIO wire:** Connect ESP32 pin 7 (IO35) to ESP32 pin 11 (IO26)
4. **Add GPIO wire:** Connect ESP32 pin 6 (IO34) to ESP32 pin 16 (IO13)

**Why these modifications are needed:**
- GPIO 34 and 35 are input-only pins on the ESP32 and cannot output PWM signals
- These wires reroute the blower and heater control signals to output-capable pins (26 and 13)

**Note:** These modifications will be incorporated in the next PCBA revision.

---

## Hardware Configuration

## Hardware Configuration

### Main Components
- **Microcontroller:** ESP32-WROOM-32E (U2)
- **USB-to-Serial:** CH340C (U1)
- **Display:** 20x4 I2C OLED with US2066 controller (DMW-20-07-T-S-200)
- **Rotary Encoder:** PIC16-F4215F-5002K (direct GPIO connection)
- **Storage:** MicroSD card via SPI
- **Status Indicators:** 
  - D1: Heartbeat LED
  - D2: NeoPixel RGB LED
- **Power:** 
  - 5V via USB Type-C
  - 3.3V regulated via MCP1825ST-3302E/DB LDO

### Pin Assignments

**Verified and corrected against PCB schematic:**

```
LEDs:
  D1 (Heartbeat):     GPIO 5
  D2 (NeoPixel):      GPIO 12

Rotary Encoder (PIC16-F4215F-5002K):
  CLK (A):            GPIO 33
  DT (B):             GPIO 32
  SW (Button):        GPIO 25

SD Card (SPI):
  CS:                 GPIO 2
  MOSI:               GPIO 17
  MISO:               GPIO 19
  SCLK:               GPIO 18

I2C OLED Display (US2066):
  SDA:                GPIO 21 (SDA_DISP)
  SCL:                GPIO 22 (SCL_DISP)
  RESET:              GPIO 14 (Hardware reset)
  I2C Address:        0x3C

PWM Outputs:
  MTR_PID1:           GPIO 16 ✓
  MTR_PID2:           GPIO 15 ✓
  BLWR_PID:           GPIO 26 ✓
  HTR_PID:            GPIO 13 ✓

Analog Input:
  THRM:               GPIO 27 (ADC2_CH7)
```

All pins verified and functional. No hardware conflicts.

### Important Display Notes
- **Controller:** US2066 (NOT standard HD44780)
- **I2C Address:** 0x3C (NOT 0x27)
- **Initialization:** Requires specific US2066 command sequence
- **3.3V Mode:** Special function selection byte
- **Hardware Reset:** Uses GPIO13 for reliable startup
- **Raw I2C:** Does not use LiquidCrystal_I2C library

---

## Required Libraries

Install these libraries via Arduino Library Manager:

```
- Wire (built-in)
- SPI (built-in)
- SD (built-in)
- Adafruit NeoPixel by Adafruit
```

**Note:** This code does NOT use the LiquidCrystal_I2C library. The US2066 OLED controller requires direct I2C register writes, which are implemented in the sketch.

**ESP32 Arduino Core Version:**
- Requires **ESP32 Arduino Core 3.x** or newer
- Uses the new `ledcAttach()` API (replaces old `ledcSetup()` + `ledcAttachPin()`)
- If using older core (2.x), you'll need to update via Arduino Boards Manager

To update ESP32 core in Arduino IDE:
1. Tools → Board → Boards Manager
2. Search for "esp32"
3. Update to version 3.0.0 or newer

---

## Upload Instructions

1. **Board Selection**: 
   - Board: "ESP32 Dev Module" or "ESP32-WROOM-DA Module"
   - Upload Speed: 921600
   - Flash Frequency: 80MHz
   - Flash Mode: QIO
   - Flash Size: 4MB
   - Partition Scheme: Default 4MB with spiffs

2. **Port Selection**: Select your COM/serial port

3. **Update Pin Definitions**: Open the sketch and update all pins marked with ⚠️ to match your actual hardware

4. **Upload**: Click upload button

---

## CLI Command Reference

### PWM Control Commands
```
mtr1 <0-100>        Set MTR_PID1 PWM percentage
mtr2 <0-100>        Set MTR_PID2 PWM percentage  
blwr <0-100>        Set BLWR_PID PWM percentage
htr <0-100>         Set HTR_PID PWM percentage
pwm <ch> <val>      Set PWM channel (0-3) to value (0-100%)
off                 Turn all PWM outputs off
```

**Examples:**
```
mtr1 50             // Set motor 1 to 50%
htr 75              // Set heater to 75%
off                 // Turn everything off
```

### Sensor Commands
```
thrm                Read thermistor voltage
```

**Example:**
```
thrm                // Returns: "Thermistor voltage: 1.234 V"
```

### LED Control Commands
```
heartbeat <on|off>  Enable/disable heartbeat LED
led <color>         Set status LED to named color
led #RRGGBB         Set status LED to hex color
```

**Named Colors:**
- red, green, blue, yellow, cyan, magenta, white, orange, purple, pink, off

**Examples:**
```
heartbeat off       // Disable heartbeat
led red             // Set status LED to red
led #FF8800         // Set status LED to orange (hex)
```

### Encoder Commands
```
enc                 Get current encoder position
enc reset           Reset encoder position to 0
```

The encoder will also automatically print events:
- `[ENCODER] Position: 5 (CW)` when rotated clockwise
- `[ENCODER] Position: 4 (CCW)` when rotated counter-clockwise
- `[ENCODER] Button PRESSED` when button is pressed

### Display Commands
```
disp <l1>|<l2>|<l3>|<l4>    Send text to display lines
disp clear                   Clear display
```

**Examples:**
```
disp Hello|World|Line 3|Line 4
disp Temperature: 25C
disp clear
```

### SD Card Commands
```
sd ls [path]        List directory contents
sd cd <path>        Change directory
sd pwd              Print working directory
```

**Examples:**
```
sd ls               // List current directory
sd ls /logs         // List /logs directory
sd cd /data         // Change to /data directory
sd cd ..            // Go up one directory
sd pwd              // Show current path
```

### System Commands
```
status              Show complete system status
help                Show command help
```

---

## System Behavior

### On Startup:
1. LED D1 starts heartbeat pattern (0.5 Hz blink)
2. Status LED (D2) turns BLUE
3. System attempts to initialize LCD
   - If successful: displays "Daisy Bio" and "Project Mira"
   - If failed: displays warning and continues without LCD
4. System attempts to initialize SD card
   - If successful: lists root directory contents
   - If failed: displays warning and continues without SD card
5. All PWM outputs are set to 0%
6. Status LED turns GREEN when ready
7. Serial prompt appears: `>`

### Graceful Degradation:
The system is designed to work even if peripherals are missing:
- **No LCD**: Display commands will return error messages but system continues
- **No SD Card**: SD commands will return error messages but system continues
- **Missing peripherals do not block startup** - system remains fully functional

Use the `status` command to check which peripherals are connected.

### Heartbeat Pattern:
- 1000ms ON
- 1000ms OFF
(0.5 Hz blink)

### Encoder Events:
- Rotation events are printed to Serial automatically
- Button press events are printed to Serial automatically
- Position is tracked and can be queried with `enc` command

---

## Troubleshooting

### Checking Peripheral Status
Run the `status` command to see which peripherals are detected:
```
status
```

This will show whether LCD and SD card are connected.

### LCD Not Working
**Note:** The system will continue to operate normally even without an LCD connected. You'll see "LCD not connected" in the startup messages, but all other features remain functional.

**For US2066 OLED Display:**
1. I2C address is **0x3C** (not 0x27)
2. Requires hardware reset on **GPIO14**
3. Verify SDA/SCL wiring (GPIO21/22)
4. Check 5V power to OLED module
5. Uses raw I2C commands (not LiquidCrystal library)
6. If text appears upside down, the display may be mounted rotated

**If display still not detected:**
- Run I2C scanner to confirm address 0x3C
- Check that RESET pin (GPIO14) is connected properly
- Verify US2066 controller (not HD44780)

### SD Card Not Detected
**Note:** The system will continue to operate normally even without an SD card. You'll see "SD card not present" in the startup messages, but all other features remain functional.

1. Verify SPI wiring (MOSI, MISO, SCLK, CS)
2. Check SD card is formatted as FAT32
3. Verify CS pin definition matches schematic (GPIO 2)
4. Try different SD card
5. Check 3.3V power to SD card
6. Use `status` command to verify detection status

### Encoder Not Responding
1. Verify pull-up resistors are enabled (code does this)
2. Test encoder by manually rotating and checking Serial output
3. May need to swap CLK/DT pins if rotation direction is backwards
4. Check encoder common pin is connected to GND

### NeoPixel Not Working
1. Verify GPIO pin assignment
2. Check 5V power supply
3. Ensure data pin has proper signal level (may need level shifter)

### PWM Not Controlling Outputs
1. Verify all PWM pins are correctly wired (GPIO16, 15, 13, 26)
2. Check PWM frequency (5kHz) is appropriate for your load
3. Use oscilloscope to verify PWM signal on all channels
4. Ensure proper gate driver circuitry
5. Test each channel individually with `mtr1 50`, `mtr2 50`, etc.

---

## Advanced Modifications

### Adding WiFi/Web Interface
The system can be extended with WiFi to provide:
- Web-based control panel
- REST API
- OTA firmware updates
- Remote monitoring

### Temperature Calculation
The `readThermistorVoltage()` function returns raw voltage. You can add temperature calculation using the Steinhart-Hart equation based on your specific thermistor.

### Additional Thermal Printer Pins
The schematic shows MTR_PID1-6 pins. The current sketch uses 4 channels. Add additional channels as needed:

```cpp
#define MTR_PID3_PIN        XX
#define MTR_PID4_PIN        XX
#define MTR_PID5_PIN        XX
#define MTR_PID6_PIN        XX
```

---

## Safety Notes

⚠️ **IMPORTANT SAFETY WARNINGS:**

1. **Heater Control**: The HTR_PID output controls a heater. Always:
   - Implement temperature limits
   - Add thermal runaway protection
   - Use external safety cutoffs
   - Never leave unattended

2. **Motor Control**: PWM outputs drive motors. Ensure:
   - Proper motor driver circuitry
   - Current limiting
   - Emergency stop capability

3. **Power Requirements**: Verify your power supply can handle:
   - ESP32: ~500mA @ 5V
   - LCD: ~100mA @ 5V
   - NeoPixel: Up to 60mA per LED
   - Motors/heater: Check specifications

---

## License
This code is provided as-is for the Daisy Bio Project Mira system.

---

## Version History
- v1.0 (2024): Initial release with full CLI and peripheral support
