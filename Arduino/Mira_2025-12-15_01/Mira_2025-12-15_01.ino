#include <Wire.h>
#include <string.h>

// ================== HARDWARE CONFIG ==================
constexpr int I2C_SDA   = 21;    // ESP32 GPIO for SDA
constexpr int I2C_SCL   = 22;    // ESP32 GPIO for SCL
constexpr int OLED_RES  = 13;    // ESP32 GPIO for OLED /RES (active-low). Set -1 if not connected.
constexpr uint8_t OLED_ADDR = 0x3C;

// If your text appears "backwards across the line" (same letters, but word order reversed across columns),
// set this to 1. If normal, set to 0.
constexpr int MIRROR_COLUMNS = 0;
// =====================================================

// US2066 I2C control bytes
constexpr uint8_t CTRL_CMD  = 0x00;
constexpr uint8_t CTRL_DATA = 0x40;

// Standard 20x4 DDRAM row offsets (US2066)
static const uint8_t ROW_ADDR[4] = { 0x00, 0x20, 0x40, 0x60 };

// -------- Low-level I2C helpers --------
void oledWrite(uint8_t control, const uint8_t* data, size_t len) {
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(control);
  Wire.write(data, len);
  Wire.endTransmission();
}

void oledCmd(uint8_t c) {
  oledWrite(CTRL_CMD, &c, 1);
  delayMicroseconds(50);
}

void oledData(uint8_t d) {
  oledWrite(CTRL_DATA, &d, 1);
}

// -------- Cursor positioning --------
static inline uint8_t mapCol(uint8_t logicalCol) {
  if (logicalCol > 19) logicalCol = 19;
  return MIRROR_COLUMNS ? (uint8_t)(19 - logicalCol) : logicalCol;
}

void oledSetCursor(uint8_t col, uint8_t row) {
  if (row > 3) row = 3;
  col = mapCol(col);
  oledCmd(0x80 | (ROW_ADDR[row] + col));
}

// -------- Robust reset/init --------
void oledHardwareReset() {
  if (OLED_RES < 0) return;
  pinMode(OLED_RES, OUTPUT);
  digitalWrite(OLED_RES, LOW);
  delay(50);               // hold reset low long enough
  digitalWrite(OLED_RES, HIGH);
  delay(100);              // allow controller to come out of reset
}

void oledClearAndHome() {
  oledCmd(0x01);           // clear
  delay(3);
  oledCmd(0x02);           // return home
  delay(3);
}

void oledInitOnce() {
  // US2066 init (I2C, 3.3V mode)
  oledCmd(0x2A);           // extended instruction set
  oledCmd(0x71);           // function selection A
  oledData(0x00);          // IMPORTANT: send as DATA byte (3.3V mode)
  oledCmd(0x28);           // fundamental instruction set
  oledCmd(0x08);           // display off

  oledClearAndHome();

  oledCmd(0x06);           // entry mode: increment, NO shift
  oledCmd(0x0C);           // display on, cursor off, blink off
}

void oledInitDeterministic() {
  // Give rails time to settle before touching I2C/OLED
  delay(200);

  oledHardwareReset();

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  delay(50);

  // Init twice for robustness against weird power-up states
  oledInitOnce();
  delay(20);
  oledInitOnce();
}

// -------- "No-overlap" line writer (pads to 20 chars) --------
void oledWriteLine(uint8_t row, const char* text) {
  char buf[21];
  memset(buf, ' ', 20);     // pad whole line with spaces
  buf[20] = '\0';

  size_t n = strlen(text);
  if (n > 20) n = 20;
  memcpy(buf, text, n);

  // Overwrite ALL 20 character positions
  for (uint8_t col = 0; col < 20; col++) {
    oledSetCursor(col, row);
    oledData((uint8_t)buf[col]);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("OLED bring-up (deterministic init + padded lines)");

  oledInitDeterministic();

  // Example: these will NOT "overlap" because we overwrite all 20 chars each row
  oledWriteLine(0, "HELLO WORLD");
  oledWriteLine(1, "ESP32 + OLED OK");
  oledWriteLine(2, "I2C addr: 0x3C");
  oledWriteLine(3, "Thermocycler");
}

void loop() {
  // Demo: update one line repeatedly without overlap
  // (Uncomment to test stability.)
  /*
  static int counter = 0;
  char msg[21];
  snprintf(msg, sizeof(msg), "Count: %d", counter++);
  oledWriteLine(3, msg);
  delay(500);
  */
}

