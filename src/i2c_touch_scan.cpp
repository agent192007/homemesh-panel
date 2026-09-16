#include <Arduino.h>
/*
 * ESP32-2432S032 touch controller identification
 *
 * Brute-forces candidate SDA/SCL pin pairs and reports any I2C devices found.
 *
 * Expected addresses:
 *   0x5D or 0x14  -> GT911   (the common / well-supported case)
 *   0x15          -> CST820  (the CST8xx family - thinner Arduino support)
 *   0x38          -> FT6236 / FT6336 (occasionally seen on these panels)
 *
 * Board setting in Arduino IDE: "ESP32 Dev Module"
 * Upload speed: if it fails at 921600, drop to 115200.
 */

#include <Wire.h>

// Candidate {SDA, SCL} pairs seen on the CYD 3.2" family.
// The first pair is the most likely for the 2432S032.
const int PIN_PAIRS[][2] = {
  {33, 32},
  {21, 22},
  {18, 19},
  {26, 27},
  {15,  4}
};
const int NUM_PAIRS = sizeof(PIN_PAIRS) / sizeof(PIN_PAIRS[0]);

// Touch reset lines are sometimes held low at boot, which makes the chip
// invisible on the bus. We pulse these high before scanning.
const int RESET_CANDIDATES[] = {25, 5, 4, 16};
const int NUM_RESETS = sizeof(RESET_CANDIDATES) / sizeof(RESET_CANDIDATES[0]);

void releaseResets() {
  for (int i = 0; i < NUM_RESETS; i++) {
    pinMode(RESET_CANDIDATES[i], OUTPUT);
    digitalWrite(RESET_CANDIDATES[i], LOW);
  }
  delay(20);
  for (int i = 0; i < NUM_RESETS; i++) {
    digitalWrite(RESET_CANDIDATES[i], HIGH);
  }
  delay(100);
}

void identify(uint8_t addr) {
  switch (addr) {
    case 0x5D:
    case 0x14:
      Serial.println("        --> GT911. Good outcome, well supported.");
      break;
    case 0x15:
      Serial.println("        --> CST820 (CST8xx family). Use the CST816S-style path.");
      break;
    case 0x38:
      Serial.println("        --> FT6236/FT6336 capacitive touch.");
      break;
    default:
      Serial.println("        --> unknown device (may be unrelated to touch).");
      break;
  }
}

void scanPair(int sda, int scl) {
  Serial.printf("\nScanning SDA=%d  SCL=%d ...\n", sda, scl);

  Wire.end();
  delay(10);
  if (!Wire.begin(sda, scl, 100000)) {
    Serial.println("  could not init I2C on this pair");
    return;
  }

  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  DEVICE FOUND at 0x%02X\n", addr);
      identify(addr);
      found++;
    }
    delay(2);
  }

  if (found == 0) {
    Serial.println("  nothing on this pair");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("\n\n=================================================");
  Serial.println(" ESP32-2432S032 touch controller scan");
  Serial.println("=================================================");

  releaseResets();

  for (int i = 0; i < NUM_PAIRS; i++) {
    scanPair(PIN_PAIRS[i][0], PIN_PAIRS[i][1]);
  }

  Serial.println("\n=================================================");
  Serial.println(" Scan complete.");
  Serial.println(" Note the address AND the pin pair it appeared on -");
  Serial.println(" you need both for the LVGL touch read callback.");
  Serial.println("=================================================");
}

void loop() {
  delay(10000);
}
