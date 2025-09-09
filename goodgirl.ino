#include <Adafruit_NeoPixel.h>
#include <string.h>

// ==== PANEL/BOARD ====
#define LED_PIN        2      // ESP8266 D4 / GPIO2
#define W              8
#define H              8
#define NUMPIX         (W*H)
#define BRIGHTNESS     26
#define SERPENTINE     true   // false bei linearer Verdrahtung

// ==== ORIENTIERUNG ====
#define FLIP_X     true
#define FLIP_Y     false
#define ROTATE_90  false

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

// ==== 5x7 FONT: SPACE, A,B,D,G,I,L,M,O,R ====
static const char GL[][7][6] = {
  { "00000","00000","00000","00000","00000","00000","00000" }, // 0: SPACE
  { "01110","10001","10001","11111","10001","10001","10001" }, // 1: A
  { "11110","10001","10001","11110","10001","10001","11110" }, // 2: B
  { "11110","10001","10001","10001","10001","10001","11110" }, // 3: D
  { "01110","10001","10000","10111","10001","10001","01110" }, // 4: G
  { "11111","00100","00100","00100","00100","00100","11111" }, // 5: I
  { "10000","10000","10000","10000","10000","10000","11111" }, // 6: L
  { "10001","11011","10101","10101","10001","10001","10001" }, // 7: M
  { "01110","10001","10001","10001","10001","10001","01110" }, // 8: O
  { "11110","10001","10001","11110","10100","10010","10001" }  // 9: R
};

int glyphIndex(char c) {
  switch (c) {
    case 'A': return 1; case 'B': return 2; case 'D': return 3; case 'G': return 4;
    case 'I': return 5; case 'L': return 6; case 'M': return 7; case 'O': return 8;
    case 'R': return 9; case ' ': default: return 0;
  }
}
uint8_t glyphColBits(int g, int x) {
  if (g < 0 || x < 0 || x > 4) return 0;
  uint8_t col = 0;
  for (int y = 0; y < 7; ++y) if (GL[g][y][x] == '1') col |= (1 << y);
  return col;
}

// Nachricht als Spaltenstrom (5 Spalten + 1 Abstand je Zeichen)
int msgColsLen(const char* t) { return (int)strlen(t) * 6; }
uint8_t msgColAt(const char* t, int idx) {
  if (idx < 0) return 0;
  int total = msgColsLen(t);
  if (idx >= total) return 0;
  int ch  = idx / 6;
  int col = idx % 6;
  if (col == 5) return 0;
  return glyphColBits(glyphIndex(t[ch]), col);
}

// ==== PINK ====
uint32_t PINK(uint8_t v=255) { return strip.gamma32(strip.Color(255, 40, v)); }
uint32_t OFF() { return strip.Color(0,0,0); }

// ==== Render-Helfer ====
void blitCols(const char* text, int offsetCols, uint32_t onColor, uint32_t offColor) {
  for (int x = 0; x < W; ++x) {
    uint8_t colBits = msgColAt(text, offsetCols + x);
    for (int y = 0; y < 7; ++y) {
      bool on = (colBits >> y) & 0x01;
      strip.setPixelColor(XY(x, y), on ? onColor : offColor);
    }
    strip.setPixelColor(XY(x, 7), OFF()); // Bodenlinie aus
  }
}

int scrollIn(const char* word, uint16_t frameDelayMs=30) {
  const int total = msgColsLen(word);
  const int endOffset = (total > W) ? (total - W) : 0;  // letzter Vollbild-Index
  for (int off = -W; off <= endOffset; ++off) {         // <= wichtig!
    strip.clear();
    blitCols(word, off, PINK(255), OFF());
    strip.show();
    delay(frameDelayMs);
  }
  return endOffset;
}

void flashOnLastFrame(const char* word, int lastOffset, int flashes=3, int onMs=140, int offMs=120) {
  // statisches Vollbild rendern
  strip.clear();
  blitCols(word, lastOffset, PINK(255), OFF());
  strip.show();

  for (int i=0; i<flashes; ++i) {
    // AN
    strip.show(); delay(onMs);
    // AUS
    strip.clear(); strip.show(); delay(offMs);
    // Wieder AN fürs nächste Blinken
    blitCols(word, lastOffset, PINK(255), OFF()); strip.show();
  }
  // Separator
  strip.clear(); strip.show(); delay(120);
}

void showWordSequence(const char* word, uint16_t scrollSpeed=30, int flashes=3, int onMs=140, int offMs=120) {
  int last = scrollIn(word, scrollSpeed);
  flashOnLastFrame(word, last, flashes, onMs, offMs);
}

// ==== SETUP/LOOP ====
void setup() {
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.clear();
  strip.show();
}

void loop() {
  showWordSequence(" GOOD ", 130, 3, 140, 80);
  showWordSequence(" GIRL ", 130, 3, 140, 80);
  showWordSequence(" BAMBI ", 130, 3, 160, 140);
}
