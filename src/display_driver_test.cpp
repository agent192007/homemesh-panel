#include <Arduino.h>
/*
 * ESP32-2432S032 display driver identification
 *
 * Run this AFTER configuring TFT_eSPI/User_Setup.h for ILI9341.
 * Read the symptoms below to work out whether that guess was right.
 *
 * ---------------------------------------------------------------
 * HOW TO READ THE RESULT
 * ---------------------------------------------------------------
 *
 * Colours correct, borders align with screen edge, no stripe
 *      -> ILI9341. Good outcome. Proceed with LVGL.
 *
 * Everything is colour-INVERTED (red shows cyan, white shows black)
 *      -> almost certainly ST7789V.
 *         Switch to ST7789_DRIVER and add #define TFT_INVERSION_ON
 *
 * Colours correct but image SHIFTED, garbage stripe at an edge
 *      -> ST7789V needing column/row offsets.
 *         Switch driver, then adjust CGRAM offsets.
 *
 * Pure white screen, nothing drawn
 *      -> backlight is on but the controller never initialised.
 *         Wrong SPI pins, or backlight pin correct but init failed.
 *
 * Completely black, no backlight glow
 *      -> backlight pin wrong. See BL_CANDIDATES below.
 * ---------------------------------------------------------------
 */

#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

// The 3.2" board's backlight pin differs from the 2.8" (which uses 21).
// We drive all plausible candidates HIGH so a wrong guess can't
// masquerade as a dead display.
const int BL_CANDIDATES[] = {27, 21, 32, 5};
const int NUM_BL = sizeof(BL_CANDIDATES) / sizeof(BL_CANDIDATES[0]);

void forceBacklightOn() {
  for (int i = 0; i < NUM_BL; i++) {
    pinMode(BL_CANDIDATES[i], OUTPUT);
    digitalWrite(BL_CANDIDATES[i], HIGH);
  }
}

void solidWithBorder(uint16_t fill, const char *name, uint16_t textColour) {
  tft.fillScreen(fill);

  // A 2px border hugging the physical edge. If the panel needs offsets,
  // one or two sides of this border will be missing or clipped.
  tft.drawRect(0, 0, tft.width(), tft.height(), TFT_WHITE);
  tft.drawRect(1, 1, tft.width() - 2, tft.height() - 2, TFT_WHITE);

  tft.setTextColor(textColour, fill);
  tft.setTextSize(3);
  tft.setCursor(20, 40);
  tft.println(name);

  Serial.printf("Showing: %s\n", name);
  delay(2500);
}

void cornerMarkers() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);

  // Each corner gets a filled square touching the very edge.
  // Missing squares mean the visible area is offset from what the
  // driver thinks it is - the classic ST7789 offset symptom.
  tft.fillRect(0, 0, 20, 20, TFT_RED);
  tft.fillRect(tft.width() - 20, 0, 20, 20, TFT_GREEN);
  tft.fillRect(0, tft.height() - 20, 20, 20, TFT_BLUE);
  tft.fillRect(tft.width() - 20, tft.height() - 20, 20, 20, TFT_YELLOW);

  tft.setCursor(30, 100);
  tft.println("4 CORNERS?");
  tft.setTextSize(1);
  tft.setCursor(30, 130);
  tft.println("R=topleft  G=topright");
  tft.setCursor(30, 145);
  tft.println("B=botleft  Y=botright");
  tft.setCursor(30, 170);
  tft.println("Any missing = offset problem");

  Serial.println("Showing: corner markers");
  delay(6000);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=================================================");
  Serial.println(" Display driver identification");
  Serial.println("=================================================");

  forceBacklightOn();

  tft.init();
  tft.setRotation(0);

  Serial.printf("Reported geometry: %d x %d\n", tft.width(), tft.height());
  Serial.println("Expected for this panel: 240 x 320\n");

  // Primary colours. Inversion is most obvious on saturated fills.
  solidWithBorder(TFT_RED,   "RED",   TFT_WHITE);
  solidWithBorder(TFT_GREEN, "GREEN", TFT_BLACK);
  solidWithBorder(TFT_BLUE,  "BLUE",  TFT_WHITE);
  solidWithBorder(TFT_WHITE, "WHITE", TFT_BLACK);
  solidWithBorder(TFT_BLACK, "BLACK", TFT_WHITE);

  cornerMarkers();

  Serial.println("\n-------------------------------------------------");
  Serial.println("If RED looked CYAN and WHITE looked BLACK,");
  Serial.println("the panel is ST7789V - switch driver and set");
  Serial.println("TFT_INVERSION_ON, then re-run this sketch.");
  Serial.println("-------------------------------------------------");
}

void loop() {
  // Slow cycle so you can inspect at leisure.
  solidWithBorder(TFT_RED,   "RED",   TFT_WHITE);
  solidWithBorder(TFT_GREEN, "GREEN", TFT_BLACK);
  solidWithBorder(TFT_BLUE,  "BLUE",  TFT_WHITE);
}
