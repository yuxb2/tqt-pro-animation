/*
 * Stencil Eye - a flat propaganda-poster eye that blinks slowly
 *
 * Screen-print idiom: heavy black linework, sclera filled with horizontal
 * hatching, concentric iris rings, solid pupil with a white highlight wedge.
 * Three inks only: black, white and red on a cream ground.
 *
 * The eyeball never moves during a blink - only the lids travel, and they
 * meet on a line that drifts slightly below centre, the way a real lid does.
 * Blink intervals are randomised and sometimes doubled so it never feels
 * metronomic. Between blinks the gaze drifts on two slow sine waves.
 *
 * Renders into a static framebuffer pushed with pushImage(): no heap
 * allocation, and per-pixel writes are direct array accesses.
 *
 * Buttons: left  = blink now
 *          right = mood (calm / nervous / sleepy)
 *
 * Hardware: LilyGo T-QT Pro (ESP32-S3, 128x128, TFT_eSPI Setup211)
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include "OneButton.h"

TFT_eSPI tft = TFT_eSPI();

#define SCREEN_W 128
#define SCREEN_H 128
#define CX 64      // eye centre
#define CY 66

// 32 KB framebuffer, statically allocated so it can never fail at runtime
static uint16_t fb[SCREEN_W * SCREEN_H];

#define PIN_BTN_L 0
#define PIN_BTN_R 47

OneButton btnLeft(PIN_BTN_L, true, true);
OneButton btnRight(PIN_BTN_R, true, true);

// Three inks plus the paper
#define COL_BLACK 0x0000
#define COL_WHITE 0xFFFF
#define COL_CREAM 0xF75C
#define COL_RED   0xE206

// Eye geometry
#define EYE_W   52.0f   // half width, corner to centre
#define TOP_A   32.0f   // upper lid travel when fully open
#define BOT_A   27.0f   // lower lid travel when fully open
#define IRIS_R  25.0f
#define IRIS_EDGE 5.0f  // solid black rim on the outside of the iris
#define LID_W   6.0f    // lid stroke weight, measured across the stroke
#define PUPIL_R 13.0f

// The paper flips to solid red on a slow cycle, then back
#define BG_PAPER_TIME 20.0f
#define BG_RED_TIME    5.0f
bool  bgRed = false;
float bgT = BG_PAPER_TIME;

// Glitch bars flashing across the paper: red on cream, cream on red
#define MAX_GBARS 10
struct GBar { int16_t x, y, w, h; uint8_t kind; };
GBar gbars[MAX_GBARS];
int   gCount = 0;
bool  glitching = false;
float glitchT = 0;          // seconds left in the current burst
float nextGlitchIn = 1.5f;
unsigned long lastGRegen = 0;

// Blink moods: {seconds between blinks (min), added random range, blink duration}
struct Mood { float gapMin, gapRand, dur; };
const Mood moods[3] = {
  { 2.8f, 3.0f, 0.85f },   // calm
  { 0.9f, 1.2f, 0.32f },   // nervous
  { 5.0f, 4.0f, 1.60f }    // sleepy
};
int mood = 0;

// Blink state
bool  blinking = false;
float blinkT = 0;          // seconds into the current blink
float nextBlinkIn = 2.0f;  // seconds until the next one
bool  doubleBlink = false; // a second blink follows straight after

// Animation
unsigned long lastFrameTime = 0;
float animTime = 0;

void onLeftClick();
void onRightClick();
void scheduleBlink();
void regenGlitch();
void drawEye(float open, float gx, float gy, float pupilR);

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== Stencil Eye ===");

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(COL_CREAM);

  // Byte order for pushImage of an in-memory RGB565 buffer.
  // If red and blue come out swapped, change this to false.
  tft.setSwapBytes(true);

  randomSeed(analogRead(4) * millis());

  btnLeft.attachClick(onLeftClick);
  btnRight.attachClick(onRightClick);

  scheduleBlink();
  lastFrameTime = millis();
}

void loop() {
  unsigned long now = millis();
  float dt = (now - lastFrameTime) / 1000.0f;
  lastFrameTime = now;
  animTime += dt;

  btnLeft.tick();
  btnRight.tick();

  // --- blink state machine ---
  float open = 1.0f;
  if (blinking) {
    blinkT += dt;
    float d = moods[mood].dur;
    if (blinkT >= d) {
      blinking = false;
      if (doubleBlink) {          // a quick second flutter
        doubleBlink = false;
        nextBlinkIn = 0.12f;
      } else {
        scheduleBlink();
      }
    } else {
      // one smooth arc down and back up
      open = 1.0f - sinf((blinkT / d) * PI);
    }
  } else {
    nextBlinkIn -= dt;
    if (nextBlinkIn <= 0) {
      blinking = true;
      blinkT = 0;
    }
  }

  // --- paper flips to red and back, tearing through the switch ---
  bgT -= dt;
  if (bgT <= 0) {
    bgRed = !bgRed;
    bgT = bgRed ? BG_RED_TIME : BG_PAPER_TIME;
    glitching = true;              // the flip itself tears
    glitchT = 0.40f;
    regenGlitch();
    lastGRegen = now;
  }

  // --- glitch bursts: short, abrupt, with calm gaps between ---
  // Gaps are much shorter while the paper is red, so it stays agitated
  if (glitching) {
    glitchT -= dt;
    if (glitchT <= 0) {
      glitching = false;
      gCount = 0;
      nextGlitchIn = bgRed ? 0.30f + (random(1000) / 1000.0f) * 0.80f
                           : 1.20f + (random(1000) / 1000.0f) * 3.00f;
    } else if (now - lastGRegen > 60) {
      regenGlitch();
      lastGRegen = now;
    }
  } else {
    nextGlitchIn -= dt;
    if (nextGlitchIn <= 0) {
      glitching = true;
      glitchT = 0.10f + (random(1000) / 1000.0f) * 0.25f;
      regenGlitch();
      lastGRegen = now;
    }
  }

  // Slow gaze drift on two incommensurate sines, plus a breathing pupil
  float gx = sinf(animTime * 0.37f) * 3.5f + sinf(animTime * 0.13f) * 1.8f;
  float gy = cosf(animTime * 0.29f) * 2.2f;
  float pupilR = PUPIL_R + sinf(animTime * 0.5f) * 1.2f;

  drawEye(open, gx, gy, pupilR);
  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);

  delay(5);
}

void scheduleBlink() {
  nextBlinkIn = moods[mood].gapMin + (random(1000) / 1000.0f) * moods[mood].gapRand;
  doubleBlink = (random(100) < 25);   // one blink in four is a double
}

void onLeftClick() {
  blinking = true;
  blinkT = 0;
  doubleBlink = false;
}

void onRightClick() {
  mood = (mood + 1) % 3;
  Serial.printf("mood = %d\n", mood);
  if (!blinking) scheduleBlink();
}

// --- drawing ------------------------------------------------------------

static inline void px(int x, int y, uint16_t c) {
  if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) fb[x + y * SCREEN_W] = c;
}

// Flat-filled triangle, used for the lashes
static void tri(float ax, float ay, float bx, float by,
                float cx, float cy, uint16_t col) {
  int ymin = (int)floorf(fminf(ay, fminf(by, cy)));
  int ymax = (int)ceilf(fmaxf(ay, fmaxf(by, cy)));
  if (ymin < 0) ymin = 0;
  if (ymax >= SCREEN_H) ymax = SCREEN_H - 1;

  float vx[3] = { ax, bx, cx };
  float vy[3] = { ay, by, cy };

  for (int y = ymin; y <= ymax; y++) {
    float xs[3];
    int n = 0;
    for (int e = 0; e < 3 && n < 3; e++) {
      int f = (e + 1) % 3;
      float y0 = vy[e], y1 = vy[f];
      if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) {
        xs[n++] = vx[e] + (vx[f] - vx[e]) * ((y - y0) / (y1 - y0));
      }
    }
    if (n >= 2) {
      float a = fminf(xs[0], xs[1]);
      float b = fmaxf(xs[0], xs[1]);
      for (int x = (int)floorf(a); x <= (int)ceilf(b); x++) px(x, y, col);
    }
  }
}

// Lid profile across the eye: 1 at the centre, 0 at the corners.
// The exponent below 1 pulls the curve towards a point at each corner
// instead of the soft round end a plain parabola would give.
static inline float lidShape(float t) {
  float u = 1.0f - t * t;
  return u <= 0 ? 0.0f : powf(u, 0.62f);
}

// Pick a fresh set of bars. Called a few times per burst so they flicker
// rather than sit still.
void regenGlitch() {
  gCount = random(3, MAX_GBARS + 1);
  for (int i = 0; i < gCount; i++) {
    gbars[i].kind = random(3);
    if (gbars[i].kind == 2) {          // full-width hairline
      gbars[i].x = 0;
      gbars[i].w = SCREEN_W;
      gbars[i].h = 1;
    } else {
      gbars[i].w = random(12, 82);
      gbars[i].x = random(-8, SCREEN_W - 10);
      gbars[i].h = random(1, 6);
    }
    gbars[i].y = random(0, SCREEN_H);
  }
}

// Drawn straight onto the paper, before the eye, so the eye always stays
// clean on top and the bars read as artefacts behind it
static void drawGlitch() {
  // Bars always contrast with whatever the paper currently is
  uint16_t bar = bgRed ? COL_CREAM : COL_RED;
  for (int i = 0; i < gCount; i++) {
    GBar &g = gbars[i];
    for (int y = g.y; y < g.y + g.h; y++) {
      for (int x = g.x; x < g.x + g.w; x++) px(x, y, bar);
    }
    if (g.kind == 1) {                 // offset black shadow under the bar
      for (int x = g.x + 3; x < g.x + g.w + 3; x++) px(x, g.y + g.h, COL_BLACK);
    }
  }
}

void drawEye(float open, float gx, float gy, float pupilR) {
  uint16_t paper = bgRed ? COL_RED : COL_CREAM;
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) fb[i] = paper;
  drawGlitch();

  float lineY = CY + (1.0f - open) * 5.0f;   // where the lids meet
  float topA = TOP_A * open;
  float botA = BOT_A * open;

  // Lashes fan from the eye corners. The corners sit on lineY whatever the
  // blink state, so the lashes stay attached all the way through the close.
  const float lashAng[3] = { -1.05f, -0.52f, 0.78f };
  const float lashLen[3] = { 17.0f, 21.0f, 15.0f };
  for (int side = 0; side < 2; side++) {
    float s = side ? 1.0f : -1.0f;
    float cxr = CX + s * EYE_W;
    for (int i = 0; i < 3; i++) {
      tri(cxr - s * 8.0f, lineY - 5.0f,
          cxr - s * 8.0f, lineY + 5.0f,
          cxr + s * cosf(lashAng[i]) * lashLen[i],
          lineY + sinf(lashAng[i]) * lashLen[i],
          COL_BLACK);
    }
  }

  // The eyeball itself stays put; the lids simply cover more or less of it
  for (int x = 0; x < SCREEN_W; x++) {
    float t = (x - CX) / EYE_W;
    if (fabsf(t) >= 1.0f) continue;

    float sh = lidShape(t);
    int yTop = (int)lroundf(lineY - topA * sh);
    int yBot = (int)lroundf(lineY + botA * sh);

    for (int y = yTop; y <= yBot; y++) {
      float dx = x - (CX + gx);
      float dy = y - (CY + gy);
      float d = sqrtf(dx * dx + dy * dy);
      uint16_t c;

      if (d < pupilR) {
        c = COL_BLACK;
        float a = atan2f(dy, dx);
        if (a > -2.60f && a < -1.95f && d < pupilR * 0.92f) {
          c = COL_WHITE;                                   // highlight wedge
        } else if (a > 0.5f && a < 2.65f && d > 3.0f && ((int)d) % 3 == 0) {
          c = COL_WHITE;                                   // ripples below it
        }
      } else if (d < IRIS_R) {
        if (d > IRIS_R - IRIS_EDGE) c = COL_BLACK;                 // heavy rim
        else c = (((int)(d - pupilR)) % 4 == 0) ? COL_BLACK : COL_RED;
      } else {
        c = (y % 4 == 0) ? COL_BLACK : COL_WHITE;          // hatched sclera
      }
      px(x, y, c);
    }

    // Heavy lid outline, drawn last so it sits over everything.
    // The stroke is laid down in vertical runs, so a run of constant height
    // looks thinner wherever the lid is steep - its perpendicular width is
    // only height * cos(slope). Scaling the run by sqrt(1 + slope^2) undoes
    // that, giving the same weight at the corners as across the top. The
    // slope goes to infinity at the very corners, hence the clamp.
    float sh2 = lidShape(t + 1.0f / EYE_W);
    float lw = LID_W * (0.65f + 0.35f * open);
    float slT = fabsf((sh2 - sh) * topA);
    float slB = fabsf((sh2 - sh) * botA);
    float mT = sqrtf(1.0f + slT * slT);
    float mB = sqrtf(1.0f + slB * slB);
    if (mT > 4.0f) mT = 4.0f;
    if (mB > 4.0f) mB = 4.0f;

    int thT = (int)lroundf(lw * mT);
    int thB = (int)lroundf(lw * mB);
    for (int k = 0; k < thT; k++) px(x, yTop + k, COL_BLACK);
    for (int k = 0; k < thB; k++) px(x, yBot - k, COL_BLACK);
  }
}
