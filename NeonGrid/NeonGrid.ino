/*
 * Neon Grid - synthwave horizon with a scrolling perspective floor
 *
 * The floor is pure perspective: horizontal lines sit at y = HZ + K/z, so
 * decreasing z over time slides them towards the viewer and they naturally
 * bunch up near the horizon. Verticals are straight lines from the vanishing
 * point, which gives the correct convergence for free.
 *
 * The sun is a disc sliced by horizontal slits that widen towards its base,
 * and its colour ramps yellow -> magenta down the same axis.
 *
 * Buttons: left  = scroll speed
 *          right = trigger a scanline tear
 *
 * Hardware: LilyGo T-QT Pro (ESP32-S3, 128x128, TFT_eSPI Setup211)
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include "OneButton.h"

TFT_eSPI tft = TFT_eSPI();

#define SCREEN_W 128
#define SCREEN_H 128
#define HZ    62          // horizon row
#define SUNX  64.0f
#define SUNY  40.0f
#define SUNR  25.0f
#define GRID_K 66.0f      // perspective constant for the floor

static uint16_t fb[SCREEN_W * SCREEN_H];

// Ridge and stars are fixed scenery, resolved once at boot
static uint8_t ridge[SCREEN_W];
#define N_STARS 45
static uint8_t starX[N_STARS], starY[N_STARS], starPh[N_STARS];

#define PIN_BTN_L 0
#define PIN_BTN_R 47
OneButton btnLeft(PIN_BTN_L, true, true);
OneButton btnRight(PIN_BTN_R, true, true);

float speed = 1.1f;
float animTime = 0;
unsigned long lastFrameTime = 0;

// Scanline tear
float tearT = 0;
int   tearY = 0;

void onLeftClick();
void onRightClick();

static inline void px(int x, int y, uint16_t c) {
  if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) fb[x + y * SCREEN_W] = c;
}

static inline uint16_t rgb(float r, float g, float b) {
  if (r < 0) r = 0; if (r > 1) r = 1;
  if (g < 0) g = 0; if (g > 1) g = 1;
  if (b < 0) b = 0; if (b > 1) b = 1;
  return (((uint16_t)(r * 31)) << 11) | (((uint16_t)(g * 63)) << 5) | ((uint16_t)(b * 31));
}

static void line(float x0, float y0, float x1, float y1, uint16_t c) {
  float dx = x1 - x0, dy = y1 - y0;
  int n = (int)fmaxf(fabsf(dx), fabsf(dy)) + 1;
  for (int i = 0; i <= n; i++) {
    float t = (float)i / n;
    px((int)lroundf(x0 + dx * t), (int)lroundf(y0 + dy * t), c);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== Neon Grid ===");

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true);

  randomSeed(analogRead(4) * millis());

  // Ridge: three sine harmonics, precomputed since it never changes
  for (int x = 0; x < SCREEN_W; x++) {
    float h = 7.0f * sinf(x * 0.09f) + 4.0f * sinf(x * 0.21f + 1.3f)
            + 3.0f * sinf(x * 0.045f + 2.1f);
    ridge[x] = (uint8_t)fabsf(h);
  }
  for (int i = 0; i < N_STARS; i++) {
    starX[i] = random(SCREEN_W);
    starY[i] = random(HZ - 6);
    starPh[i] = random(100);
  }

  btnLeft.attachClick(onLeftClick);
  btnRight.attachClick(onRightClick);
  lastFrameTime = millis();
}

void loop() {
  unsigned long now = millis();
  float dt = (now - lastFrameTime) / 1000.0f;
  lastFrameTime = now;
  animTime += dt * speed;

  btnLeft.tick();
  btnRight.tick();

  if (tearT > 0) tearT -= dt;
  else if (random(1000) < 6) { tearT = 0.18f; tearY = random(SCREEN_H); }

  // --- sky and ground, one colour per row ---
  for (int y = 0; y < SCREEN_H; y++) {
    uint16_t c;
    if (y < HZ) {
      float u = (float)y / HZ;
      c = rgb(0.05f + 0.22f * u, 0.0f, 0.10f + 0.30f * u);
    } else {
      c = rgb(0.04f, 0.0f, 0.07f);
    }
    for (int x = 0; x < SCREEN_W; x++) fb[x + y * SCREEN_W] = c;
  }

  // --- stars ---
  for (int i = 0; i < N_STARS; i++) {
    float tw = 0.45f + 0.55f * sinf(animTime * 2.0f + starPh[i]);
    px(starX[i], starY[i], rgb(tw, tw, tw));
  }

  // --- sun: slits widen towards the base, colour ramps down it ---
  for (int y = (int)(SUNY - SUNR); y <= (int)(SUNY + SUNR); y++) {
    float f = (y - (SUNY - SUNR)) / (2 * SUNR);
    if (fmodf((float)y, 5.0f) < f * 4.2f) continue;
    float dyv = y - SUNY;
    float hw = SUNR * SUNR - dyv * dyv;
    if (hw <= 0) continue;
    hw = sqrtf(hw);
    uint16_t c = rgb(1.0f, 0.85f * (1.0f - f) + 0.05f, 0.10f + 0.85f * f);
    for (int x = (int)(SUNX - hw); x <= (int)(SUNX + hw); x++) px(x, y, c);
  }

  // --- ridge, drawn over the sun so it sinks behind the hills ---
  for (int x = 0; x < SCREEN_W; x++) {
    int top = HZ - ridge[x];
    for (int y = top; y < HZ; y++) px(x, y, rgb(0.03f, 0.0f, 0.05f));
    px(x, top, rgb(0.9f, 0.15f, 0.7f));
  }

  // --- floor ---
  float scroll = fmodf(animTime, 1.0f);
  for (int i = 1; i <= 15; i++) {
    float zz = i + (1.0f - scroll);
    float y = HZ + GRID_K / zz;
    if (y >= SCREEN_H || y < HZ) continue;
    float b = 0.25f + 0.75f * ((y - HZ) / GRID_K);   // fade towards the horizon
    uint16_t c = rgb(0.95f * b, 0.10f * b, 0.80f * b);
    int yy = (int)lroundf(y);
    for (int x = 0; x < SCREEN_W; x++) px(x, yy, c);
  }
  for (int k = -7; k <= 7; k++) {
    line(SUNX, HZ, SUNX + k * 30.0f, SCREEN_H, rgb(0.55f, 0.08f, 0.95f));
  }

  // --- scanline tear: a band of rows shoved sideways ---
  if (tearT > 0) {
    int h = 6 + random(10);
    int off = random(-9, 10);
    for (int y = tearY; y < tearY + h && y < SCREEN_H; y++) {
      uint16_t row[SCREEN_W];
      memcpy(row, &fb[y * SCREEN_W], sizeof(row));
      for (int x = 0; x < SCREEN_W; x++) {
        int sx = x - off;
        fb[x + y * SCREEN_W] = ((unsigned)sx < SCREEN_W) ? row[sx] : 0;
      }
    }
  }

  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
  delay(5);
}

void onLeftClick() {
  speed += 0.8f;
  if (speed > 3.5f) speed = 0.3f;
  Serial.printf("speed = %.1f\n", speed);
}

void onRightClick() {
  tearT = 0.30f;
  tearY = random(SCREEN_H);
}
