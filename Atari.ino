#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <string.h>

/*** ====== CONFIG ====== ***/
#define LED_PIN        2        // WS2812B DIN -> D4 / GPIO2
#define BUZZER_PIN     4        // Buzzer     -> D2 / GPIO4
#define W              8
#define H              8
#define NUMPIX         (W*H)
#define BRIGHTNESS     52
#define SERPENTINE     true     // false, wenn Matrix nicht serpentin

// Orientierung bei Bedarf
#define FLIP_X     false
#define FLIP_Y     false
#define ROTATE_90  false

// Musik/Timing
#define BPM         65
#define BASE_TICK_MS (60000 / (BPM * 4))  // 16tel: 125 ms bei 120 BPM
#define SWING       0.15f                 // 0..0.35 (odd-Ticks länger)
#define FRAME_MS    40                    // sanftere Animation

// Visual Feinschliff
#define STAR_SPAWN_PCT   35               // % Spawnchance pro Frame
#define STAR_DIM_SHIFT   3                // 2..4 (größer = schnellerer Fade)
#define SPEC_ALPHA       32               // EMA-Faktor (1..64); größer = schneller

Adafruit_NeoPixel strip(NUMPIX, LED_PIN, NEO_GRB + NEO_KHZ800);

/*** ====== Mapping ====== ***/
static inline void transformXY(uint8_t &x, uint8_t &y) {
  if (ROTATE_90) { uint8_t t = x; x = y; y = W - 1 - t; }
  if (FLIP_X)    { x = W - 1 - x; }
  if (FLIP_Y)    { y = H - 1 - y; }
}
static inline uint16_t XY(uint8_t x, uint8_t y) {
  transformXY(x, y);
  if (!SERPENTINE) return y * W + x;
  return (y % 2 == 0) ? (y * W + x) : (y * W + (W - 1 - x));
}

/*** ====== Color helper ====== ***/
static inline uint32_t hsv(uint16_t h, uint8_t s=255, uint8_t v=255) {
  return strip.gamma32(strip.ColorHSV((uint16_t)(h * 182), s, v));
}

/*** ====== Noten (Hz) — ohne D4/D5-Kollision ====== ***/
enum NoteHz {
  N_C4=262, N_D4=294, N_E4=330, N_F4=349, N_G4=392, N_A4=440, N_B4=494,
  N_C5=523, N_D5=587, N_E5=659, N_F5=698, N_G5=784, N_A5=880
};

/*** ====== Chord-Progression (Am – F – C – G) ====== ***/
struct Triad { uint16_t n1, n2, n3; bool minor; };
Triad chordSeq[] = {
  {N_A4, N_C5, N_E5, true},   // Am
  {N_F4, N_A4, N_C5, false},  // F
  {N_C4, N_E4, N_G4, false},  // C
  {N_G4, N_B4, N_D5, false},  // G
};
const int NUM_CHORDS = sizeof(chordSeq)/sizeof(chordSeq[0]);

/*** ====== Tick-/Swing-Engine + Subtone-Scheduler ====== ***/
unsigned long nextTickAt = 0, lastFrame = 0;
int tickNo = 0;        // 16tel-Index
int chordIx = 0;
int arpPhase = 0;      // 0..2

struct SubTone { uint16_t f; uint16_t d; }; // freq, duration(ms)
SubTone sub[4]; int subCount = 0; int subIdx = 0;
unsigned long subEndsAt = 0;

uint16_t kickFreq() { return 95; }         // weicher „Kick“
uint16_t bass(uint16_t root) { return root / 2; }  // tiefer Bass

uint16_t tickDurationMs() {
  // odd Ticks länger (Swing), even kürzer — Summe bleibt 2*BASE_TICK_MS
  if (tickNo & 1) return (uint16_t)(BASE_TICK_MS * (1.0f + SWING));
  else            return (uint16_t)(BASE_TICK_MS * (1.0f - SWING));
}

void startSubtone(uint16_t f, uint16_t dur) {
  tone(BUZZER_PIN, f, dur);
  subEndsAt = millis() + dur;
}

void scheduleTick() {
  Triad &c = chordSeq[chordIx];
  const uint16_t dur = tickDurationMs();

  // Reset Subsequence
  subCount = 0; subIdx = 0;

  if ((tickNo % 2) == 0) {
    // EVEN: Groove/Kick + Bass (Atari-„Drums“)
    uint16_t dKick = min<uint16_t>(25, dur/6);
    uint16_t dBass = (dur > dKick+10) ? (dur - dKick - 5) : (dur/2);
    sub[subCount++] = { kickFreq(), dKick };                       // kurzer Kick
    sub[subCount++] = { bass(c.n1), (uint16_t)dBass };             // Bass auf Akkord-Root
  } else {
    // ODD: Chord-Arpeggio in 3 Substeps (n1,n2,n3)
    uint16_t slice = max<uint16_t>(25, (dur - 6) / 3);
    // leichte Variation pro Takt durch arpPhase-Rotation
    uint16_t triad[3] = { c.n1, c.n2, c.n3 };
    uint8_t order[3]  = { (uint8_t)((arpPhase+0)%3), (uint8_t)((arpPhase+1)%3), (uint8_t)((arpPhase+2)%3) };
    for (int i=0; i<3; ++i) sub[subCount++] = { triad[ order[i] ], slice };
    arpPhase = (arpPhase + 1) % 3;
  }

  // Start erste Subnote
  if (subCount > 0) startSubtone(sub[0].f, sub[0].d);

  // Takt-/Akkord-Fortschritt
  tickNo++;
  if (tickNo % 16 == 0) chordIx = (chordIx + 1) % NUM_CHORDS;

  nextTickAt = millis() + dur;
}

void updateSubtones() {
  if (subIdx >= subCount) return;
  if (millis() >= subEndsAt) {
    subIdx++;
    if (subIdx < subCount) startSubtone(sub[subIdx].f, sub[subIdx].d);
  }
}

/*** ====== Visuals ====== ***/
// A) weiches Starfield (langsamer Drift + Fade)
uint32_t fb[NUMPIX];
void fbClear() { memset(fb, 0, sizeof(fb)); }
void fbSet(uint8_t x, uint8_t y, uint32_t c) { fb[XY(x,y)] = c; }
void fbShiftLeftDim(uint8_t dimShift) {
  for (int y=0; y<H; ++y) {
    for (int x=0; x<W-1; ++x) {
      fb[XY(x,y)] = fb[XY(x+1,y)] >> dimShift;
    }
    fb[XY(W-1,y)] = 0;
  }
}
void fbShow() {
  for (int y=0; y<H; ++y)
    for (int x=0; x<W; ++x)
      strip.setPixelColor(XY(x,y), fb[XY(x,y)]);
  strip.show();
}
uint16_t baseHue = 0;

void animStarfield() {
  fbShiftLeftDim(STAR_DIM_SHIFT);
  if (random(0,100) < STAR_SPAWN_PCT) {
    uint8_t y = random(0, H);
    uint16_t h = (baseHue + y*9) % 360;
    fbSet(W-1, y, hsv(h, 180, 255));
  }
  fbShow();
  baseHue = (baseHue + 1) % 360; // langsamer
}

// B) smoothed Spectrum (EMA auf drei Balken)
uint8_t levelTarget[3] = {1,1,1};
uint8_t levelEMA[3]    = {1,1,1};

uint8_t freqToLevel(uint16_t f) {
  int lvl = map((int)f, 90, 880, 1, 7);
  if (lvl < 1) lvl = 1; if (lvl > 7) lvl = 7;
  return (uint8_t)lvl;
}
void updateSpectrumTargets() {
  Triad &c = chordSeq[chordIx];
  // Lead/Bass-Proxy: letzte geplante Töne (sub[0]..)
  uint16_t fLead = subCount ? sub[subIdx].f : c.n1;
  levelTarget[0] = freqToLevel(fLead);
  levelTarget[1] = freqToLevel(c.n2);
  levelTarget[2] = freqToLevel(c.n3);
  // EMA
  for (int i=0;i<3;i++) {
    levelEMA[i] = (uint8_t)(( (uint16_t)(64 - SPEC_ALPHA) * levelEMA[i] + (uint16_t)SPEC_ALPHA * levelTarget[i] ) >> 6);
  }
}
void animSpectrum() {
  strip.clear();
  uint8_t xs[3] = {1, 3, 5};
  uint16_t hs[3] = { (uint16_t)((baseHue+0)%360), (uint16_t)((baseHue+50)%360), (uint16_t)((baseHue+100)%360) };
  for (int b=0;b<3;b++) {
    uint8_t lv = levelEMA[b];
    for (int y=0; y<lv; ++y) {
      strip.setPixelColor(XY(xs[b],   H-1-y), hsv(hs[b], 200, 220));
      if (y+1 < lv) strip.setPixelColor(XY(xs[b], H-1-(y+1)), hsv(hs[b], 120, 60));
      if (xs[b]+1 < W) strip.setPixelColor(XY(xs[b]+1, H-1-y), hsv(hs[b], 100, 40));
    }
  }
  strip.show();
  baseHue = (baseHue + 1) % 360; // ebenfalls ruhiger
}

/*** ====== Orchestrierung ====== ***/
enum Mode { STAR=0, SPEC=1 };
Mode mode = STAR;
int barsInMode = 0;

void setup() {
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.clear(); strip.show();
  pinMode(BUZZER_PIN, OUTPUT);
  randomSeed(analogRead(A0) ^ micros());
  fbClear();
  nextTickAt = millis();   // sofort starten
  lastFrame  = millis();
  scheduleTick();          // initialer Tick
}

void loop() {
  unsigned long now = millis();

  // Tick/Sequencer mit Swing
  if ((long)(now - nextTickAt) >= 0) {
    scheduleTick();
    // Moduswechsel alle 4 Takte
    if ((tickNo % 16) == 0) {
      barsInMode++;
      if (barsInMode >= 4) {
        mode = (mode == STAR) ? SPEC : STAR;
        barsInMode = 0;
        fbClear();
      }
    }
  }

  // Subnote-Scheduler (Chord-Arp/Kick/Bass)
  updateSubtones();

  // Visuals
  if (now - lastFrame >= FRAME_MS) {
    lastFrame += FRAME_MS;
    updateSpectrumTargets();
    if (mode == STAR) animStarfield();
    else              animSpectrum();
  }
}
