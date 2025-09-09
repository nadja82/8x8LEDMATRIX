#include <Adafruit_NeoPixel.h>

// ===== PANEL/BOARD =====
#define LED_PIN       2        // ESP8266 D4 / GPIO2
#define W             8
#define H             8
#define NUMPIX        (W*H)
#define BRIGHTNESS    48
#define SERPENTINE    true     // false bei linearer Verdrahtung

// ===== ORIENTIERUNG =====
#define FLIP_X     false
#define FLIP_Y     false
#define ROTATE_90  false

// ===== SOUND (Buzzer/Speaker) =====
// Aktiviere Sound = true; Buzzer an D2 (GPIO4) empfohlen.
// Für passive Piezo- oder aktiven Buzzer geeignet (tone()).
#define ENABLE_SOUND  true
#define BUZZER_PIN    4        // ESP8266 D2 / GPIO4

// ===== GAME-TUNING (verlustarm) =====
#define PADDLE_MIN_SIZE      3
#define PADDLE_MAX_SIZE      4
#define FRAME_MS_MIN         80     // langsamer = leichter
#define FRAME_MS_MAX         120
#define PADDLE_MOVE_EVERY_MIN 1
#define PADDLE_MOVE_EVERY_MAX 2
#define AI_TRACK_PROB        88     // % Follow-Intelligenz
#define FORGIVE_MARGIN       1      // Toleranzpixel am Paddle-Rand (Assist)

// Farb/Stil
Adafruit_NeoPixel strip(NUMPIX, LED_PIN, NEO_GRB + NEO_KHZ800);

// ---------- Mapping mit Flip/Rotate ----------
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

// ---------- Farbhelper ----------
uint32_t hsv(uint16_t hDeg, uint8_t s=255, uint8_t v=255) {
  return strip.gamma32(strip.ColorHSV((uint16_t)(hDeg * 182), s, v));
}

// ---------- Sound ----------
void buzz(uint16_t f, uint16_t ms) {
#if ENABLE_SOUND
  tone(BUZZER_PIN, f, ms);
  delay(ms);
  noTone(BUZZER_PIN);
#endif
}
void sfxWall()   { buzz(220, 30); }     // soft „thud“
void sfxPaddle() { buzz(330, 50); }     // „tok“
void sfxScore()  { buzz(150, 70); delay(40); buzz(110, 120); } // dezent, tief

// ---------- Game State ----------
int ball_x, ball_y, vx, vy;
int lp_y, rp_y;            // Topkoordinate der Paddles
int lp_size, rp_size;      // Länge
uint16_t hueLeft, hueRight, hueBall;
uint16_t frameDelayMs;
int paddleMoveEvery;
unsigned long frameNo = 0;

// ---------- Utils ----------
int clampi(int v, int lo, int hi) { if (v < lo) return lo; if (v > hi) return hi; return v; }

void drawPaddleLeft() {
  uint32_t c = hsv(hueLeft, 220, 200);
  for (int y = 0; y < lp_size; y++) strip.setPixelColor(XY(0, clampi(lp_y + y, 0, H-1)), c);
}
void drawPaddleRight() {
  uint32_t c = hsv(hueRight, 220, 200);
  for (int y = 0; y < rp_size; y++) strip.setPixelColor(XY(W-1, clampi(rp_y + y, 0, H-1)), c);
}
void drawBall() {
  strip.setPixelColor(XY(ball_x, ball_y), hsv(hueBall, 80, 255));
}

void subtleScoreFlash(bool leftMiss) {
  // Dezente Feedback-Animation: 2 weiche Pulses + Randspalte
  uint32_t wash = hsv(leftMiss ? hueRight : hueLeft, 40, 40);
  uint8_t edgeX = leftMiss ? 0 : (W-1);
  for (int k=0; k<2; k++) {
    // leichter Wash
    for (int y=0; y<H; y++) for (int x=0; x<W; x++) strip.setPixelColor(XY(x,y), wash);
    // Randspalte etwas heller
    for (int y=0; y<H; y++) strip.setPixelColor(XY(edgeX,y), hsv(leftMiss ? hueRight : hueLeft, 180, 120));
    strip.show(); delay(120);
    strip.clear(); strip.show(); delay(120);
  }
}

void resetRound() {
  lp_size = PADDLE_MIN_SIZE + (random(PADDLE_MAX_SIZE - PADDLE_MIN_SIZE + 1));
  rp_size = PADDLE_MIN_SIZE + (random(PADDLE_MAX_SIZE - PADDLE_MIN_SIZE + 1));
  lp_y = clampi(H/2 - lp_size/2, 0, H - lp_size);
  rp_y = clampi(H/2 - rp_size/2, 0, H - rp_size);

  // Serve aus Mitte, leichte Streuung
  ball_x = random(3, W-3);
  ball_y = random(2, H-2);
  vx = (random(0,2) == 0) ? -1 : +1;
  vy = (random(0,2) == 0) ? -1 : +1;

  hueLeft  = random(0, 360);
  hueRight = (hueLeft + 180) % 360;
  hueBall  = random(0, 360);

  frameDelayMs    = random(FRAME_MS_MIN, FRAME_MS_MAX + 1);
  paddleMoveEvery = random(PADDLE_MOVE_EVERY_MIN, PADDLE_MOVE_EVERY_MAX + 1);

  strip.clear(); strip.show();
}

void moveAI() {
  // Linkes Paddle
  if ((frameNo % paddleMoveEvery) == 0) {
    if (vx < 0) {
      if (random(0,100) < AI_TRACK_PROB) {
        int target = ball_y - lp_size/2;
        if (target < lp_y) lp_y--;
        else if (target > lp_y) lp_y++;
      }
    } else {
      int mid = H/2 - lp_size/2;
      if (lp_y < mid && random(0,100)<40) lp_y++;
      else if (lp_y > mid && random(0,100)<40) lp_y--;
    }
    lp_y = clampi(lp_y, 0, H - lp_size);
  }
  // Rechtes Paddle
  if ((frameNo % paddleMoveEvery) == 0) {
    if (vx > 0) {
      if (random(0,100) < AI_TRACK_PROB) {
        int target = ball_y - rp_size/2;
        if (target < rp_y) rp_y--;
        else if (target > rp_y) rp_y++;
      }
    } else {
      int mid = H/2 - rp_size/2;
      if (rp_y < mid && random(0,100)<40) rp_y++;
      else if (rp_y > mid && random(0,100)<40) rp_y--;
    }
    rp_y = clampi(rp_y, 0, H - rp_size);
  }
}

void physicsStep() {
  // Wandkontakt oben/unten
  int next_y = ball_y + vy;
  if (next_y < 0 || next_y > H-1) {
    vy = -vy; next_y = ball_y + vy;
    hueBall = (hueBall + 17) % 360;
    sfxWall();
  }

  // Links – Paddle mit Assist (FORGIVE_MARGIN)
  if (vx < 0 && ball_x == 1) {
    int minY = lp_y - FORGIVE_MARGIN;
    int maxY = lp_y + lp_size - 1 + FORGIVE_MARGIN;
    if (next_y >= minY && next_y <= maxY) {
      vx = -vx;
      // Spin je nach relativer Trefferhöhe
      int center = lp_y + lp_size/2;
      if (next_y < center) vy = -1;
      else if (next_y > center) vy = 1;
      hueBall = (hueBall + 31) % 360;
      sfxPaddle();
    } else {
      subtleScoreFlash(true);
      sfxScore();
      resetRound();
      return;
    }
  }

  // Rechts – Paddle mit Assist
  if (vx > 0 && ball_x == W-2) {
    int minY = rp_y - FORGIVE_MARGIN;
    int maxY = rp_y + rp_size - 1 + FORGIVE_MARGIN;
    if (next_y >= minY && next_y <= maxY) {
      vx = -vx;
      int center = rp_y + rp_size/2;
      if (next_y < center) vy = -1;
      else if (next_y > center) vy = 1;
      hueBall = (hueBall + 31) % 360;
      sfxPaddle();
    } else {
      subtleScoreFlash(false);
      sfxScore();
      resetRound();
      return;
    }
  }

  // Move Ball
  ball_x += vx;
  ball_y = next_y;

  // Clamp
  if (ball_x < 0) ball_x = 0; else if (ball_x > W-1) ball_x = W-1;
  if (ball_y < 0) ball_y = 0; else if (ball_y > H-1) ball_y = H-1;
}

void renderFrame() {
  strip.clear();
  drawPaddleLeft();
  drawPaddleRight();
  drawBall();
  strip.show();
}

// ---------- Setup/Loop ----------
void setup() {
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.clear(); strip.show();
  randomSeed(analogRead(A0) ^ micros());
#if ENABLE_SOUND
  pinMode(BUZZER_PIN, OUTPUT);
#endif
  resetRound();
}

void loop() {
  moveAI();
  physicsStep();
  renderFrame();
  frameNo++;
  delay(frameDelayMs);
}
