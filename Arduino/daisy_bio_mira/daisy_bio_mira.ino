/*
 * Daisy Bio - Project Mira
 * ESP32-WROOM-32E Control System
 *
 * Compatible with ESP32 Arduino Core 3.x
 * (Uses new ledcAttach API)
 *
 * ⚠️ REQUIRED HARDWARE MODIFICATIONS:
 * The current PCB revision requires these hand modifications:
 * 1. Remove R11 (near SD card)
 * 2. Wire: Bottom of C20 (VBUS) to top pin of U1 (+5V)
 * 3. Wire: ESP32 pin 7 (IO35) to pin 11 (IO26)
 * 4. Wire: ESP32 pin 6 (IO34) to pin 16 (IO13)
 *
 * These fixes will be incorporated in the next PCBA revision.
 * GPIO 34/35 are input-only and cannot output PWM, so we reroute
 * to GPIO 26/13 which are output-capable.
 *
 * Features:
 * - Heartbeat LED on D1 (IO5)
 * - SD Card file system with directory navigation
 * - LCD Display (I2C)
 * - Rotary Encoder with button
 * - PWM control for motor/heater/blower
 * - NeoPixel status LED
 * - Thermistor analog reading
 * - Serial CLI for system control
 */

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_NeoPixel.h>

// ===== PIN DEFINITIONS =====
// LEDs
#define LED_D1_PIN          5    // Heartbeat LED (IO5)
#define LED_D2_PIN          12   // NeoPixel Status LED (RGB)

// Rotary Encoder (PIC16-F4215F-5002K)
#define ENCODER_CLK_PIN     33   // Encoder A (ENC_A)
#define ENCODER_DT_PIN      32   // Encoder B (ENC_B)
#define ENCODER_SW_PIN      25   // Encoder button (ENC_SW)

// SD Card SPI
#define SD_CS_PIN           2    // SD Card Chip Select
#define SD_MOSI_PIN         17   // MOSI
#define SD_MISO_PIN         19   // MISO
#define SD_SCLK_PIN         18   // SCK

// I2C Display
#define I2C_SDA_PIN         21   // SDA_DISP
#define I2C_SCL_PIN         22   // SCL_DISP
#define OLED_RESET_PIN      14   // Hardware reset pin for OLED

// PWM Outputs
#define MTR_PID1_PIN        16   // Motor PID 1
#define MTR_PID2_PIN        15   // Motor PID 2
#define BLWR_PID_PIN        26   // Blower PID
#define HTR_PID_PIN         13   // Heater PID

// Analog Input
#define THRM_PIN            27   // Thermistor (ADC2_CH7)

// ===== CONFIGURATION =====
#define LCD_ADDRESS         0x3C // US2066 OLED I2C address
#define LCD_COLS            20
#define LCD_ROWS            4
#define OLED_RESET_PIN      14   // Hardware reset pin for OLED
#define NEOPIXEL_COUNT      1
#define PWM_FREQUENCY       5000
#define PWM_RESOLUTION      8    // 8-bit: 0-255

// US2066 OLED control bytes
#define US2066_CTRL_CMD     0x00
#define US2066_CTRL_DATA    0x40

// US2066 row addresses for 20x4 display
static const uint8_t US2066_ROW_ADDR[4] = { 0x00, 0x20, 0x40, 0x60 };

// ===== GLOBAL OBJECTS =====
// No LiquidCrystal_I2C object needed - we use raw I2C for US2066
Adafruit_NeoPixel neopixel(NEOPIXEL_COUNT, LED_D2_PIN, NEO_GRB + NEO_KHZ800);

// ===== GLOBAL VARIABLES =====
// Heartbeat
bool heartbeatEnabled = true;
unsigned long lastHeartbeat = 0;
bool heartbeatState = false;

// NeoPixel
uint32_t currentNeoPixelColor = 0;
uint32_t targetNeoPixelColor = 0;
unsigned long fadeStartTime = 0;
bool isFading = false;
const int FADE_DURATION = 600; // ms

// Rotary Encoder
volatile int encoderPosition = 0;
volatile bool encoderPressed = false;
int lastEncoderPosition = 0;
volatile uint8_t encoderStateHistory = 0; // 4-bit state history
volatile int encoderRawCount = 0; // Raw count before dividing by steps

// PWM Values
int pwmValues[] = {0, 0, 0, 0}; // Current PWM values (0-100%)

// SD Card
File currentDir;
String currentPath = "/";
bool sdCardPresent = false;

// LCD Display
bool lcdPresent = false;

// Serial CLI
String serialBuffer = "";
bool cliEchoEnabled = true;

// ===== FUNCTION PROTOTYPES =====
// US2066 OLED low-level functions
void oledWrite(uint8_t control, const uint8_t* data, size_t len);
void oledCmd(uint8_t c);
void oledData(uint8_t d);
void oledSetCursor(uint8_t col, uint8_t row);
void oledHardwareReset();
void oledClearAndHome();
void oledInitOnce();
void oledInitDeterministic();
void oledWriteLine(uint8_t row, const char* text);
void oledWriteLineAnimated(uint8_t row, const char* text, int delayMs);

// System functions
void setupPWM();
void setupEncoder();
void setupSDCard();
void setupLCD();
void setupNeoPixel();
void updateHeartbeat();
void updateNeoPixelFade();
void updateEncoder();
void encoderISR();
void processSerialCommand(String cmd);
void setPWM(int channel, int percent);
void setNeoPixelColor(uint32_t color);
void setNeoPixelColorByName(String colorName);
float readThermistorVoltage();
void printSDCardDirectory(String path);
void navigateSDCard(String path);
void sendToDisplay(String line1, String line2, String line3, String line4);

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(500);
  
  Serial.println("\n\n========================================");
  Serial.println("  Daisy Bio - Project Mira");
  Serial.println("  ESP32 Control System v1.0");
  Serial.println("========================================\n");

  // Initialize GPIO
  pinMode(LED_D1_PIN, OUTPUT);
  digitalWrite(LED_D1_PIN, LOW);

  // Initialize NeoPixel first (before OLED animation)
  setupNeoPixel();
  setNeoPixelColor(neopixel.Color(0, 0, 255)); // Blue on startup

  // Setup I2C for LCD
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  // Initialize LCD (includes swipe-in animation)
  setupLCD();
  
  // Initialize PWM
  setupPWM();
  
  // Initialize Encoder
  setupEncoder();
  
  // Initialize SD Card
  setupSDCard();
  
  Serial.println("\nSystem Ready!");
  Serial.println("Type 'help' for available commands\n");
  Serial.print("> ");
  
  setNeoPixelColor(neopixel.Color(0, 255, 0)); // Green when ready
}

// ===== MAIN LOOP =====
void loop() {
  updateHeartbeat();
  updateNeoPixelFade();
  updateEncoder();

  // Process serial commands
  while (Serial.available()) {
    char c = Serial.read();
    
    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() > 0) {
        Serial.println(); // New line after command
        processSerialCommand(serialBuffer);
        serialBuffer = "";
        Serial.print("> ");
      }
    } else if (c == 8 || c == 127) { // Backspace
      if (serialBuffer.length() > 0) {
        serialBuffer.remove(serialBuffer.length() - 1);
        if (cliEchoEnabled) {
          Serial.print("\b \b");
        }
      }
    } else {
      serialBuffer += c;
      if (cliEchoEnabled) {
        Serial.print(c);
      }
    }
  }
  
  delay(10); // Small delay to prevent tight looping
}

// ===== SETUP FUNCTIONS =====

// ===== US2066 OLED LOW-LEVEL FUNCTIONS =====

void oledWrite(uint8_t control, const uint8_t* data, size_t len) {
  Wire.beginTransmission(LCD_ADDRESS);
  Wire.write(control);
  Wire.write(data, len);
  Wire.endTransmission();
}

void oledCmd(uint8_t c) {
  oledWrite(US2066_CTRL_CMD, &c, 1);
  delayMicroseconds(50);
}

void oledData(uint8_t d) {
  oledWrite(US2066_CTRL_DATA, &d, 1);
}

void oledSetCursor(uint8_t col, uint8_t row) {
  if (row > 3) row = 3;
  if (col > 19) col = 19;
  oledCmd(0x80 | (US2066_ROW_ADDR[row] + col));
}

void oledHardwareReset() {
  pinMode(OLED_RESET_PIN, OUTPUT);
  digitalWrite(OLED_RESET_PIN, LOW);
  delay(50);  // Hold reset low
  digitalWrite(OLED_RESET_PIN, HIGH);
  delay(100); // Allow controller to come out of reset
}

void oledClearAndHome() {
  oledCmd(0x01); // Clear display
  delay(3);
  oledCmd(0x02); // Return home
  delay(3);
}

void oledInitOnce() {
  // US2066 initialization sequence for I2C, 3.3V mode
  oledCmd(0x2A);    // Extended instruction set (RE=1)
  oledCmd(0x71);    // Function selection A
  oledData(0x00);   // CRITICAL: Send as DATA byte for 3.3V mode
  oledCmd(0x06);    // Entry mode with reverse display (fixes mirrored characters)
  oledCmd(0x28);    // Fundamental instruction set (RE=0)
  oledCmd(0x08);    // Display off

  oledClearAndHome();

  oledCmd(0x06);    // Entry mode: increment, no shift
  oledCmd(0x0C);    // Display on, cursor off, blink off
}

void oledInitDeterministic() {
  // Give power rails time to settle
  delay(200);
  
  oledHardwareReset();
  
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000); // 100kHz I2C
  delay(50);
  
  // Initialize twice for robustness
  oledInitOnce();
  delay(20);
  oledInitOnce();
}

void oledWriteLine(uint8_t row, const char* text) {
  char buf[21];
  memset(buf, ' ', 20);  // Pad with spaces
  buf[20] = '\0';

  size_t n = strlen(text);
  if (n > 20) n = 20;
  memcpy(buf, text, n);

  // Write all 20 characters normally
  for (uint8_t col = 0; col < 20; col++) {
    oledSetCursor(col, row);
    oledData((uint8_t)buf[col]);
  }
}

void oledWriteLineAnimated(uint8_t row, const char* text, int delayMs) {
  char buf[21];
  size_t textLen = strlen(text);
  if (textLen > 20) textLen = 20;

  // Swipe in from right: start with text off-screen to the right
  // and slide it left into position
  for (int offset = 20; offset >= 0; offset--) {
    memset(buf, ' ', 20);  // Clear buffer with spaces
    buf[20] = '\0';

    // Calculate where to place the text
    if (offset < 20) {
      int copyStart = (offset > 0) ? 0 : 0;
      int copyLen = (offset + textLen <= 20) ? textLen : (20 - offset);

      if (offset + textLen > 20) {
        // Text is partially off the right edge
        memcpy(buf + offset, text, 20 - offset);
      } else {
        // Text fits on screen
        memcpy(buf + offset, text, textLen);
      }
    }

    // Write the entire line
    for (uint8_t col = 0; col < 20; col++) {
      oledSetCursor(col, row);
      oledData((uint8_t)buf[col]);
    }

    delay(delayMs);
  }
}

// ===== SETUP FUNCTIONS =====

void setupLCD() {
  Serial.print("Initializing US2066 OLED Display... ");
  
  oledInitDeterministic();
  
  // Test if OLED is responding
  Wire.beginTransmission(LCD_ADDRESS);
  byte error = Wire.endTransmission();
  
  if (error == 0) {
    Serial.println("OK");
    lcdPresent = true;

    // Display startup message with swipe-in animation
    oledWriteLineAnimated(0, "Daisy Bio", 40);
    oledWriteLineAnimated(1, "Project Mira", 40);
  } else {
    Serial.println("NOT FOUND");
    Serial.print("  I2C error code: ");
    Serial.println(error);
    Serial.println("  Display commands will be disabled");
    lcdPresent = false;
  }
}

void setupNeoPixel() {
  Serial.print("Initializing NeoPixel... ");
  neopixel.begin();
  neopixel.setBrightness(50); // 50/255 brightness
  neopixel.show();
  Serial.println("OK");
}

void setupPWM() {
  Serial.print("Initializing PWM outputs... ");
  
  // New ESP32 Arduino 3.x API - use ledcAttach instead of ledcSetup + ledcAttachPin
  // ledcAttach(pin, frequency, resolution) returns the channel number
  ledcAttach(MTR_PID1_PIN, PWM_FREQUENCY, PWM_RESOLUTION);
  ledcAttach(MTR_PID2_PIN, PWM_FREQUENCY, PWM_RESOLUTION);
  ledcAttach(BLWR_PID_PIN, PWM_FREQUENCY, PWM_RESOLUTION);
  ledcAttach(HTR_PID_PIN, PWM_FREQUENCY, PWM_RESOLUTION);
  
  // Set all to 0
  ledcWrite(MTR_PID1_PIN, 0);
  ledcWrite(MTR_PID2_PIN, 0);
  ledcWrite(BLWR_PID_PIN, 0);
  ledcWrite(HTR_PID_PIN, 0);
  
  Serial.println("OK");
}

void setupEncoder() {
  Serial.print("Initializing Rotary Encoder... ");
  pinMode(ENCODER_CLK_PIN, INPUT_PULLUP);
  pinMode(ENCODER_DT_PIN, INPUT_PULLUP);
  pinMode(ENCODER_SW_PIN, INPUT_PULLUP);

  // Initialize encoder state history
  encoderStateHistory = 0;

  // Attach interrupts to both encoder pins for responsive quadrature decoding
  attachInterrupt(digitalPinToInterrupt(ENCODER_CLK_PIN), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_DT_PIN), encoderISR, CHANGE);

  Serial.println("OK");
}

void setupSDCard() {
  Serial.print("Initializing SD Card... ");
  
  SPI.begin(SD_SCLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("FAILED");
    Serial.println("  No SD card detected. Note: SD card must be formatted as FAT32.");
    sdCardPresent = false;
    return;
  }
  
  sdCardPresent = true;
  
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    sdCardPresent = false;
    return;
  }
  
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.print("OK (");
  Serial.print(cardSize);
  Serial.print(" MB, Type: ");
  
  if (cardType == CARD_MMC) {
    Serial.print("MMC");
  } else if (cardType == CARD_SD) {
    Serial.print("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.print("SDHC");
  } else {
    Serial.print("UNKNOWN");
  }
  Serial.println(")");
  
  Serial.println("\nSD Card Root Directory:");
  Serial.println("----------------------------------------");
  printSDCardDirectory("/");
  Serial.println("----------------------------------------\n");
}

// ===== UPDATE FUNCTIONS =====

void updateHeartbeat() {
  if (!heartbeatEnabled) {
    digitalWrite(LED_D1_PIN, LOW);
    return;
  }

  unsigned long currentMillis = millis();

  // Simple 0.5 Hz blink: 1000ms on, 1000ms off
  if (currentMillis - lastHeartbeat >= 1000) {
    lastHeartbeat = currentMillis;
    heartbeatState = !heartbeatState;
    digitalWrite(LED_D1_PIN, heartbeatState ? HIGH : LOW);
  }
}

// Encoder ISR - called on any change to encoder pins A or B
// Implementation inspired by Ai Esp32 Rotary Encoder library by Igor Antolic
void IRAM_ATTR encoderISR() {
  // Shift previous state left by 2 bits to make room for current state
  encoderStateHistory <<= 2;

  // Read current pin states: A on bit 0, B on bit 1
  // Note: Pins are active LOW with pull-ups, so we read them directly
  uint8_t currentPinState = 0;
  if (digitalRead(ENCODER_CLK_PIN)) currentPinState |= 0x01; // A pin
  if (digitalRead(ENCODER_DT_PIN)) currentPinState |= 0x02;  // B pin

  // Combine with history (keep only lower 4 bits)
  encoderStateHistory |= (currentPinState & 0x03);

  // State transition lookup table from Ai Esp32 Rotary Encoder library
  // Maps 4-bit state history to direction: -1 (CCW), 0 (invalid/no change), 1 (CW)
  static const int8_t enc_states[16] = {
    0, -1, 1, 0,
    1, 0, 0, -1,
    -1, 0, 0, 1,
    0, 1, -1, 0
  };

  // Get direction from lookup table
  int8_t direction = enc_states[encoderStateHistory & 0x0F];

  // Accumulate raw counts (negate direction to reverse CW/CCW)
  encoderRawCount -= direction;

  // Convert raw counts to position (divide by 2 for detents)
  // Most rotary encoders have 2 steps per detent
  encoderPosition = encoderRawCount / 2;
}

void updateEncoder() {
  // Check if encoder position changed and print message
  if (encoderPosition != lastEncoderPosition) {
    int diff = encoderPosition - lastEncoderPosition;
    if (diff > 0) {
      Serial.println("\n[ENCODER] Position: " + String(encoderPosition) + " (CW)");
    } else {
      Serial.println("\n[ENCODER] Position: " + String(encoderPosition) + " (CCW)");
    }
    Serial.print("> ");
    lastEncoderPosition = encoderPosition;
  }

  // Read encoder button with proper debouncing
  static bool lastButtonState = HIGH;
  static bool stableButtonState = HIGH;
  static unsigned long lastDebounceTime = 0;
  bool buttonState = digitalRead(ENCODER_SW_PIN);

  // If button state changed, reset the debounce timer
  if (buttonState != lastButtonState) {
    lastDebounceTime = millis();
    lastButtonState = buttonState;
  }

  // If enough time has passed, accept the new stable state
  if ((millis() - lastDebounceTime) > 50) {
    // Only process if stable state has changed
    if (buttonState != stableButtonState) {
      stableButtonState = buttonState;

      // Detect press (transition from HIGH to LOW)
      if (stableButtonState == LOW) {
        Serial.println("\n[ENCODER] Button PRESSED");
        Serial.print("> ");
        encoderPressed = true;
      }
    }
  }
}

// ===== PWM CONTROL =====

void setPWM(int channel, int percent) {
  if (channel < 0 || channel > 3) {
    Serial.println("Error: Invalid channel (0-3)");
    return;
  }
  
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  
  pwmValues[channel] = percent;
  int dutyCycle = map(percent, 0, 100, 0, 255);
  
  // Map channel to pin and write PWM
  const char* names[] = {"MTR_PID1", "MTR_PID2", "BLWR_PID", "HTR_PID"};
  int pins[] = {MTR_PID1_PIN, MTR_PID2_PIN, BLWR_PID_PIN, HTR_PID_PIN};
  
  ledcWrite(pins[channel], dutyCycle);
  
  Serial.print(names[channel]);
  Serial.print(" set to ");
  Serial.print(percent);
  Serial.println("%");
}

// ===== NEOPIXEL CONTROL =====

void setNeoPixelColor(uint32_t color) {
  // Start a non-blocking fade to the target color
  targetNeoPixelColor = color;
  fadeStartTime = millis();
  isFading = true;
}

void updateNeoPixelFade() {
  if (!isFading) {
    return;
  }

  unsigned long elapsed = millis() - fadeStartTime;

  if (elapsed >= FADE_DURATION) {
    // Fade complete - set final color
    neopixel.setPixelColor(0, targetNeoPixelColor);
    neopixel.show();
    currentNeoPixelColor = targetNeoPixelColor;
    isFading = false;
  } else {
    // Calculate interpolated color based on progress
    float progress = (float)elapsed / (float)FADE_DURATION;

    uint8_t currentR = (currentNeoPixelColor >> 16) & 0xFF;
    uint8_t currentG = (currentNeoPixelColor >> 8) & 0xFF;
    uint8_t currentB = currentNeoPixelColor & 0xFF;

    uint8_t targetR = (targetNeoPixelColor >> 16) & 0xFF;
    uint8_t targetG = (targetNeoPixelColor >> 8) & 0xFF;
    uint8_t targetB = targetNeoPixelColor & 0xFF;

    uint8_t r = currentR + (targetR - currentR) * progress;
    uint8_t g = currentG + (targetG - currentG) * progress;
    uint8_t b = currentB + (targetB - currentB) * progress;

    neopixel.setPixelColor(0, neopixel.Color(r, g, b));
    neopixel.show();
  }
}

void setNeoPixelColorByName(String colorName) {
  colorName.toLowerCase();
  uint32_t color = 0;
  
  if (colorName == "red") color = neopixel.Color(255, 0, 0);
  else if (colorName == "green") color = neopixel.Color(0, 255, 0);
  else if (colorName == "blue") color = neopixel.Color(0, 0, 255);
  else if (colorName == "yellow") color = neopixel.Color(255, 255, 0);
  else if (colorName == "cyan") color = neopixel.Color(0, 255, 255);
  else if (colorName == "magenta") color = neopixel.Color(255, 0, 255);
  else if (colorName == "white") color = neopixel.Color(255, 255, 255);
  else if (colorName == "orange") color = neopixel.Color(255, 165, 0);
  else if (colorName == "purple") color = neopixel.Color(128, 0, 128);
  else if (colorName == "pink") color = neopixel.Color(255, 192, 203);
  else if (colorName == "off" || colorName == "black") color = neopixel.Color(0, 0, 0);
  else {
    Serial.println("Unknown color name");
    return;
  }
  
  setNeoPixelColor(color);
  Serial.print("LED set to ");
  Serial.println(colorName);
}

uint32_t hexToColor(String hexString) {
  // Remove # if present
  if (hexString.startsWith("#")) {
    hexString = hexString.substring(1);
  }
  
  if (hexString.length() != 6) {
    return 0;
  }
  
  long number = strtol(hexString.c_str(), NULL, 16);
  int r = (number >> 16) & 0xFF;
  int g = (number >> 8) & 0xFF;
  int b = number & 0xFF;
  
  return neopixel.Color(r, g, b);
}

// ===== THERMISTOR READING =====

float readThermistorVoltage() {
  int rawValue = analogRead(THRM_PIN);
  float voltage = (rawValue / 4095.0) * 3.3; // ESP32 ADC is 12-bit, 3.3V reference
  return voltage;
}

// ===== SD CARD FUNCTIONS =====

void printSDCardDirectory(String path) {
  if (!sdCardPresent) {
    Serial.println("Error: SD Card not present");
    return;
  }
  
  File dir = SD.open(path);
  if (!dir) {
    Serial.print("Error: Failed to open directory: ");
    Serial.println(path);
    return;
  }
  
  if (!dir.isDirectory()) {
    Serial.print("Error: Not a directory: ");
    Serial.println(path);
    dir.close();
    return;
  }
  
  Serial.print("Contents of ");
  Serial.print(path);
  Serial.println(":");
  
  File file = dir.openNextFile();
  int count = 0;
  while (file) {
    Serial.print("  ");
    if (file.isDirectory()) {
      Serial.print("[DIR]  ");
      Serial.println(file.name());
    } else {
      Serial.print("[FILE] ");
      Serial.print(file.name());
      Serial.print(" (");
      Serial.print(file.size());
      Serial.println(" bytes)");
    }
    count++;
    file = dir.openNextFile();
  }
  
  if (count == 0) {
    Serial.println("  (empty)");
  }
  
  dir.close();
}

void navigateSDCard(String path) {
  if (!sdCardPresent) {
    Serial.println("Error: SD Card not present");
    return;
  }
  
  if (path == "..") {
    // Go up one level
    int lastSlash = currentPath.lastIndexOf('/', currentPath.length() - 2);
    if (lastSlash > 0) {
      currentPath = currentPath.substring(0, lastSlash + 1);
    } else {
      currentPath = "/";
    }
  } else if (path.startsWith("/")) {
    // Absolute path
    currentPath = path;
    if (!currentPath.endsWith("/")) {
      currentPath += "/";
    }
  } else {
    // Relative path
    currentPath += path;
    if (!currentPath.endsWith("/")) {
      currentPath += "/";
    }
  }
  
  // Verify the path exists
  File dir = SD.open(currentPath);
  if (!dir) {
    Serial.print("Error: Path does not exist: ");
    Serial.println(currentPath);
    // Revert to root
    currentPath = "/";
    return;
  }
  
  if (!dir.isDirectory()) {
    Serial.print("Error: Not a directory: ");
    Serial.println(currentPath);
    dir.close();
    // Revert to root
    currentPath = "/";
    return;
  }
  
  dir.close();
  
  Serial.print("Current directory: ");
  Serial.println(currentPath);
  printSDCardDirectory(currentPath);
}

// ===== DISPLAY FUNCTIONS =====

void sendToDisplay(String line1, String line2, String line3, String line4) {
  if (!lcdPresent) {
    Serial.println("Error: LCD not connected");
    return;
  }
  
  oledClearAndHome();
  
  oledWriteLine(0, line1.c_str());
  oledWriteLine(1, line2.c_str());
  oledWriteLine(2, line3.c_str());
  oledWriteLine(3, line4.c_str());
  
  Serial.println("Display updated");
}

// ===== SERIAL COMMAND PROCESSOR =====

void processSerialCommand(String cmd) {
  cmd.trim();

  if (cmd.length() == 0) {
    return;
  }

  // Parse command and arguments BEFORE lowercasing
  // This preserves capitalization in arguments (e.g., for display text)
  int spaceIndex = cmd.indexOf(' ');
  String command = cmd;
  String args = "";

  if (spaceIndex > 0) {
    command = cmd.substring(0, spaceIndex);
    args = cmd.substring(spaceIndex + 1);
    args.trim();
  }

  // Only lowercase the command, not the arguments
  command.toLowerCase();
  
  // ===== HELP =====
  if (command == "help" || command == "?") {
    Serial.println("\nAvailable Commands:");
    Serial.println("-------------------");
    Serial.println("PWM Control:");
    Serial.println("  mtr1 <0-100>     - Set MTR_PID1 PWM (%)");
    Serial.println("  mtr2 <0-100>     - Set MTR_PID2 PWM (%)");
    Serial.println("  blwr <0-100>     - Set BLWR_PID PWM (%)");
    Serial.println("  htr <0-100>      - Set HTR_PID PWM (%)");
    Serial.println("  pwm <ch> <val>   - Set PWM channel (0-3) to value (0-100%)");
    Serial.println("  off              - Turn all PWM outputs off");
    Serial.println();
    Serial.println("Sensors:");
    Serial.println("  thrm             - Read thermistor voltage");
    Serial.println();
    Serial.println("LED Control:");
    Serial.println("  heartbeat <on|off> - Enable/disable heartbeat LED");
    Serial.println("  led <color>      - Set status LED (red/green/blue/etc)");
    Serial.println("  led #RRGGBB      - Set status LED to hex color");
    Serial.println();
    Serial.println("Encoder:");
    Serial.println("  enc              - Get encoder position");
    Serial.println("  enc reset        - Reset encoder to 0");
    Serial.println();
    
    if (lcdPresent) {
      Serial.println("Display:");
      Serial.println("  disp <l1>|<l2>|<l3>|<l4> - Send text to display");
      Serial.println("  disp clear       - Clear display");
    } else {
      Serial.println("Display: (NOT CONNECTED)");
    }
    Serial.println();
    
    if (sdCardPresent) {
      Serial.println("SD Card:");
      Serial.println("  sd ls [path]     - List directory contents");
      Serial.println("  sd cd <path>     - Change directory");
      Serial.println("  sd pwd           - Print working directory");
    } else {
      Serial.println("SD Card: (NOT PRESENT)");
    }
    Serial.println();
    
    Serial.println("System:");
    Serial.println("  status           - Show system status");
    Serial.println("  help             - Show this help");
    Serial.println();
  }
  
  // ===== PWM COMMANDS =====
  else if (command == "mtr1") {
    int val = args.toInt();
    setPWM(0, val);
  }
  else if (command == "mtr2") {
    int val = args.toInt();
    setPWM(1, val);
  }
  else if (command == "blwr") {
    int val = args.toInt();
    setPWM(2, val);
  }
  else if (command == "htr") {
    int val = args.toInt();
    setPWM(3, val);
  }
  else if (command == "pwm") {
    int ch = args.substring(0, args.indexOf(' ')).toInt();
    int val = args.substring(args.indexOf(' ') + 1).toInt();
    setPWM(ch, val);
  }
  else if (command == "off") {
    Serial.println("Turning all PWM outputs OFF");
    for (int i = 0; i < 4; i++) {
      setPWM(i, 0);
    }
  }
  
  // ===== SENSOR COMMANDS =====
  else if (command == "thrm") {
    float voltage = readThermistorVoltage();
    Serial.print("Thermistor voltage: ");
    Serial.print(voltage, 3);
    Serial.println(" V");
  }
  
  // ===== LED COMMANDS =====
  else if (command == "heartbeat") {
    if (args == "on" || args == "1" || args == "true") {
      heartbeatEnabled = true;
      Serial.println("Heartbeat enabled");
    } else if (args == "off" || args == "0" || args == "false") {
      heartbeatEnabled = false;
      digitalWrite(LED_D1_PIN, LOW);
      Serial.println("Heartbeat disabled");
    } else {
      Serial.println("Usage: heartbeat <on|off>");
    }
  }
  else if (command == "led") {
    if (args.startsWith("#")) {
      uint32_t color = hexToColor(args);
      setNeoPixelColor(color);
      Serial.print("LED set to ");
      Serial.println(args);
    } else {
      setNeoPixelColorByName(args);
    }
  }
  
  // ===== ENCODER COMMANDS =====
  else if (command == "enc") {
    if (args == "reset") {
      encoderPosition = 0;
      Serial.println("Encoder position reset to 0");
    } else {
      Serial.print("Encoder position: ");
      Serial.println(encoderPosition);
    }
  }
  
  // ===== DISPLAY COMMANDS =====
  else if (command == "disp") {
    if (!lcdPresent) {
      Serial.println("Error: LCD not connected");
      return;
    }
    
    if (args == "clear") {
      oledClearAndHome();
      Serial.println("Display cleared");
    } else {
      // Parse lines separated by |
      String lines[4] = {"", "", "", ""};
      int lineIndex = 0;
      int lastPos = 0;
      
      for (int i = 0; i < args.length() && lineIndex < 4; i++) {
        if (args.charAt(i) == '|') {
          lines[lineIndex] = args.substring(lastPos, i);
          lineIndex++;
          lastPos = i + 1;
        }
      }
      if (lineIndex < 4) {
        lines[lineIndex] = args.substring(lastPos);
      }
      
      sendToDisplay(lines[0], lines[1], lines[2], lines[3]);
    }
  }
  
  // ===== SD CARD COMMANDS =====
  else if (command == "sd") {
    if (!sdCardPresent) {
      Serial.println("Error: SD card not present or failed to initialize");
      return;
    }
    
    int spaceIdx = args.indexOf(' ');
    String subCmd = args;
    String subArgs = "";
    
    if (spaceIdx > 0) {
      subCmd = args.substring(0, spaceIdx);
      subArgs = args.substring(spaceIdx + 1);
      subArgs.trim();
    }
    
    if (subCmd == "ls") {
      if (subArgs.length() > 0) {
        printSDCardDirectory(subArgs);
      } else {
        printSDCardDirectory(currentPath);
      }
    }
    else if (subCmd == "cd") {
      if (subArgs.length() > 0) {
        navigateSDCard(subArgs);
      } else {
        Serial.println("Usage: sd cd <path>");
      }
    }
    else if (subCmd == "pwd") {
      Serial.print("Current directory: ");
      Serial.println(currentPath);
    }
    else {
      Serial.println("SD subcommands: ls, cd, pwd");
    }
  }
  
  // ===== STATUS =====
  else if (command == "status") {
    Serial.println("\n===== SYSTEM STATUS =====");
    
    Serial.println("\nPeripherals:");
    Serial.print("  LCD Display:  ");
    Serial.println(lcdPresent ? "CONNECTED" : "NOT CONNECTED");
    Serial.print("  SD Card:      ");
    Serial.println(sdCardPresent ? "PRESENT" : "NOT PRESENT");
    if (sdCardPresent) {
      Serial.print("    Path: ");
      Serial.println(currentPath);
    }
    
    Serial.print("\nHeartbeat: ");
    Serial.println(heartbeatEnabled ? "ENABLED" : "DISABLED");
    
    Serial.println("\nPWM Outputs:");
    Serial.print("  MTR_PID1: ");
    Serial.print(pwmValues[0]);
    Serial.println("%");
    Serial.print("  MTR_PID2: ");
    Serial.print(pwmValues[1]);
    Serial.println("%");
    Serial.print("  BLWR_PID: ");
    Serial.print(pwmValues[2]);
    Serial.println("%");
    Serial.print("  HTR_PID:  ");
    Serial.print(pwmValues[3]);
    Serial.println("%");
    
    Serial.print("\nEncoder Position: ");
    Serial.println(encoderPosition);
    
    Serial.print("\nThermistor: ");
    Serial.print(readThermistorVoltage(), 3);
    Serial.println(" V");
    
    Serial.println("========================\n");
  }
  
  // ===== UNKNOWN COMMAND =====
  else {
    Serial.print("Unknown command: ");
    Serial.println(command);
    Serial.println("Type 'help' for available commands");
  }
}
