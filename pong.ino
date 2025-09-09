#include <Adafruit_NeoPixel.h>

// ==== PANEL/BOARD ====
#define LED_PIN     2        // ESP8266 D4 / GPIO2
#define W           8
#define H           8
#define NUMPIX      (W*H)
#define BRIGHTNESS  48
#define SERPENTINE  true     // false bei linearer Verdrahtung

// ==== ORIENTIERUNG ====
#define FLIP_X     false
#define FLIP_Y     false
#define ROTATE_90  false

// ==== TIMING (Tuning) ====
#define STEP_DELAY_MS   60    // Zeit zwischen einzelnen Pixel-Schritten
#define LINE_PAUSE_MS   90    // kurze Pause nach jeder Linie
#define CYCLE_PAUSE_MS  250   // Pause nach kompletter Füllung

#include <Arduino.h>
Adafruit_NeoPixel strip(NUMPIX, LED_PIN, NEO_GRB + NEO_KHZ800);

// ---- Mapping mit Flip/Rotate ----
static inline void transformXY(uint8_t &x, uint8_t &y) {
  if (ROTATE_90) { uint8_t t = x; x = y; y = W - 1 - t; }
  if (FLIP_X)    { x = W - 1 - x; }
  if (FLIP_Y)    { y = H - 1 - y; }
}
uint16_t XY(uint8_t x, uint8_t y) {
  transformXY(x, y);
  if (!SERPENTINE) return y * W + x;
  return (y % 2 == 0) ? (y * W + x) : (y * W + (W - 1 - x));
}

// ---- Farbhelper (HSV mit Gamma) ----
uint32_t colorHSV(uint16_t hDeg, uint8_t s=255, uint8_t v=255) {
  return strip.gamma32(strip.ColorHSV((uint16_t)(hDeg * 182), s, v));
}

void clearAll() { strip.clear(); strip.show(); }

void fill_from_right(uint16_t baseHue) {
  // Zeile für Zeile; jede Zeile eigene Farbe; pro Zeile von rechts nach links
  for (int y = 0; y < H; ++y) {
    uint16_t hue = (baseHue + y * 28) % 360;      // Farbsprung je Zeile
    uint32_t col = colorHSV(hue, 240, 255);
    for (int x = W - 1; x >= 0; --x) {
      strip.setPixelColor(XY(x, y), col);
      strip.show();
      delay(STEP_DELAY_MS);
    }
    delay(LINE_PAUSE_MS);
  }
}

void fill_from_bottom(uint16_t baseHue) {
  // Spalte für Spalte; jede Spalte eigene Farbe; pro Spalte von unten nach oben
  for (int x = 0; x < W; ++x) {
    uint16_t hue = (baseHue + x * 28) % 360;      // Farbsprung je Spalte
    uint32_t col = colorHSV(hue, 240, 255);
    for (int y = H - 1; y >= 0; --y) {
      strip.setPixelColor(XY(x, y), col);
      strip.show();
      delay(STEP_DELAY_MS);
    }
    delay(LINE_PAUSE_MS);
  }
}

void setup() {
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  clearAll();
  // Zufall initialisieren (leichtes Rauschen auf A0 + Zeit)
  randomSeed(analogRead(A0) ^ micros());
}

void loop() {
  static uint16_t baseHue = 0;

  clearAll();
  if (random(0, 2) == 0) {
    fill_from_right(baseHue);   // horizontal (rechts → links)
  } else {
    fill_from_bottom(baseHue);  // vertikal (unten → oben)
  }

  strip.show();
  delay(CYCLE_PAUSE_MS);

  // Farbe für nächsten Durchlauf verschieben
  baseHue = (baseHue + 73) % 360; // 73 = hübscher, ungerader Schritt
}
