/*
 * Ember Tunnel - flying forward down a corridor of rectangular rings
 *
 * A single square shaft seen in one-point perspective. Every ring is the same
 * cross-section at a different depth, drawn at scale s, so the four corners of
 * every ring sit on four straight rays leaving the vanishing point. That is the
 * whole geometry; the corridor comes out of it for free.
 *
 * The vanishing point is deliberately not in the middle of the panel. Put it
 * dead centre and the rings nest symmetrically, which reads as a flat target
 * rather than a shaft - you have to be looking down the corridor off-axis
 * before the two walls, the ceiling and the floor separate into four surfaces.
 * Off-centre, the same ring is 89 px above the vanishing point and only 38 px
 * below it, so the ceiling's edges spread across most of the screen while the
 * floor's crowd into the bottom strip. Everything that makes the picture read
 * as depth rather than as pattern is in that ratio.
 *
 * The ring scales are a geometric ladder, s = SMAX * RATIO^k, rather than
 * evenly spaced depths. Two things follow. The gap between neighbouring rings
 * on screen is then a fixed *fraction* of their size, which is what keeps some
 * eighteen of them separately visible instead of resolving a handful in the
 * foreground and dumping the rest into a smear; and the ladder is self-similar,
 * so advancing every ring by one whole step reproduces the picture exactly. The
 * sketch animates that step continuously and wraps it at 1. A ring is never
 * created or destroyed mid-flight - the wrap hands the innermost one back to the
 * near end at the moment its fade has taken it to nothing - so twenty-eight
 * rings run forever and the loop has no seam anywhere in it.
 *
 * Nothing is drawn in colour. Each edge deposits a soft ridge of *intensity*
 * into an accumulation buffer, and only at the very end is that buffer read
 * through a ramp: dark red, red, orange, yellow, near-white. So the palette is
 * not chosen anywhere - it is a readout of how many rings landed on a pixel. A
 * lone ring in the foreground is plain red; down where the rings close to
 * within a pixel of each other, around the far opening and along the floor, the
 * same ramp runs up into the orange and yellow bloom. Corners are brighter than
 * edges for the same reason, because two edges cross there. Nudge the ring
 * count up and the whole far end warms by itself.
 *
 * Edges are axis-aligned, so an edge is a constant weight added along a
 * contiguous span - a pointer walk, not a per-pixel line routine. The ridge
 * profile is a 14-entry lookup in eighths of a pixel, rescaled once per ring by
 * that ring's depth fade, which keeps the inner loop to a load and an add.
 * Positions stay in floats all the way down to the span, so the far rings slide
 * smoothly through each other instead of snapping from row to row.
 *
 * Renders into a static framebuffer pushed with pushImage() rather than a
 * TFT_eSprite: no heap allocation that can fail silently.
 *
 * Buttons: left  = speed (four forward, then one in reverse - the corridor
 *                  swallows you back out through the far opening)
 *          right = vanishing point (fixed / drifting / wandering wide)
 *
 * Hardware: LilyGo T-QT Pro (ESP32-S3, 128x128, TFT_eSPI Setup211)
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include "OneButton.h"

TFT_eSPI tft = TFT_eSPI();

#define SCREEN_W 128
#define SCREEN_H 128

// 32 KB framebuffer plus 32 KB of accumulator, both statically allocated so
// neither can fail at runtime
static uint16_t fb[SCREEN_W * SCREEN_H];
static uint16_t acc[SCREEN_W * SCREEN_H];

#define PIN_BTN_L 0
#define PIN_BTN_R 47

OneButton btnLeft(PIN_BTN_L, true, true);
OneButton btnRight(PIN_BTN_R, true, true);

#define COL_BLACK 0x0000

// --- The shaft -----------------------------------------------------------

// Scale of the nearest ring. Just over one screen, so it has already passed the
// viewer and is clipped away entirely - which is what leaves room for the next
// one to grow into the border without a ring ever appearing out of nowhere.
#define SMAX 1.18f

// Each ring is this fraction of the one in front of it. Near 1 the corridor is
// long and finely ribbed, lower and it becomes a few big frames.
#define RATIO 0.912f

// Rings alive at any moment. RATIO^RINGS sets how small the far opening is:
// 0.912^28 is about a thirteenth, so the opening is roughly 11 px across.
#define RINGS 28

// Where the corridor points, as a fraction of the panel. Low and to the left,
// so the ceiling and the right-hand wall get most of the screen while the floor
// and the left wall compress into the bottom-left corner.
#define VP_FX 0.31f
#define VP_FY 0.70f

// Drift of the vanishing point. Two slow sines whose periods divide
// LOOP_PERIOD, so the wander closes on itself exactly.
static const float VP_AMP[] = { 0.0f, 0.09f, 0.20f };
#define VP_MODE_COUNT (sizeof(VP_AMP) / sizeof(VP_AMP[0]))
#define VP_PER_X 48.0f
#define VP_PER_Y 72.0f
#define VP_MIN   0.07f
#define VP_MAX   0.93f

// Rings per second. Positive walks them out towards the viewer; the last step
// runs the corridor backwards.
static const float SPEED_STEPS[] = { 0.35f, 0.70f, 1.30f, 2.30f, -0.90f };
#define SPEED_COUNT (sizeof(SPEED_STEPS) / sizeof(SPEED_STEPS[0]))

// --- Ridge profile -------------------------------------------------------

// Half-width of an edge's glow in pixels, and the resolution of the lookup:
// GLOW_N entries covering GLOW_R pixels in eighths.
#define GLOW_R     1.75f
#define GLOW_STEPS 8
#define GLOW_N     14
#define GLOW_PEAK  64.0f

static uint16_t glowLUT[GLOW_N];

// --- Depth fade ----------------------------------------------------------

// How much dimmer a ring is at the far end than at the near end. Kept mild: the
// far rings have to survive being dimmed and still stack into the bloom.
#define FADE_FAR 0.40f

// The innermost rings come down to nothing over the last slice of the ladder,
// which is where the wrap happens - so the ring that gets handed back to the
// near end was already invisible when it went.
#define SPAWN_FRAC 0.08f

// --- Intensity ramp ------------------------------------------------------

// acc >> 2 indexes this, so one ring dead-on a pixel lands at 16 - the top of
// the plain red section - and the table saturates at sixteen overlapping rings.
static uint16_t colMap[256];

// 144 s: both vanishing-point periods divide it, so folding animTime here is
// seamless. The rings carry their own phase and wrap independently.
#define LOOP_PERIOD 144.0f

// Animation state
float animTime = 0;
float ringPhase = 0;
unsigned long lastFrameTime = 0;

// Look settings
int speedIndex = 2;
int vpMode = 1;

// Heartbeat
unsigned long lastReport = 0;
uint32_t frameCount = 0;

void onLeftClick();
void onRightClick();
void buildTables();
void drawFrame();

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== Ember Tunnel booting ===");

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(COL_BLACK);

  // Byte order for pushImage of an in-memory RGB565 buffer.
  // If red and blue come out swapped, change this to false.
  tft.setSwapBytes(true);

  btnLeft.attachClick(onLeftClick);
  btnRight.attachClick(onRightClick);

  buildTables();

  lastFrameTime = millis();
  lastReport = millis();

  Serial.printf("rings: %d, ratio %.3f, far opening %.1f px\n",
                RINGS, RATIO, SMAX * powf(RATIO, RINGS) * SCREEN_W);
  Serial.printf("free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());
  Serial.println("Ember Tunnel initialized - entering loop");
}

void loop() {
  unsigned long now = millis();
  float dt = (now - lastFrameTime) / 1000.0f;
  lastFrameTime = now;

  animTime += dt;
  if (animTime >= LOOP_PERIOD) animTime -= LOOP_PERIOD;

  // One whole step of the ladder reproduces the picture, so this wraps at 1
  ringPhase += SPEED_STEPS[speedIndex] * dt;
  if (ringPhase >= 1.0f) ringPhase -= 1.0f;
  if (ringPhase <  0.0f) ringPhase += 1.0f;

  btnLeft.tick();
  btnRight.tick();

  drawFrame();
  frameCount++;

  // Heartbeat: if this keeps printing, the loop is alive and any remaining
  // problem is on the display side rather than a crash or a hang
  if (now - lastReport >= 2000) {
    Serial.printf("alive - %lu fps, speed=%.2f rings/s, vp drift=%.2f\n",
                  (unsigned long)(frameCount * 1000UL / (now - lastReport)),
                  SPEED_STEPS[speedIndex], VP_AMP[vpMode]);
    frameCount = 0;
    lastReport = now;
  }

  delay(5);
}

void onLeftClick() {
  speedIndex = (speedIndex + 1) % (int)SPEED_COUNT;
  Serial.printf("speed = %.2f rings/s (%s)\n", SPEED_STEPS[speedIndex],
                SPEED_STEPS[speedIndex] >= 0 ? "forward" : "reverse");
}

void onRightClick() {
  vpMode = (vpMode + 1) % (int)VP_MODE_COUNT;
  Serial.printf("vanishing point drift = %.2f\n", VP_AMP[vpMode]);
}

// --- Tables --------------------------------------------------------------

static inline uint16_t rgb(float r, float g, float b) {
  if (r < 0) r = 0; if (r > 1) r = 1;
  if (g < 0) g = 0; if (g > 1) g = 1;
  if (b < 0) b = 0; if (b > 1) b = 1;
  return (((uint16_t)(r * 31)) << 11) | (((uint16_t)(g * 63)) << 5) | ((uint16_t)(b * 31));
}

void buildTables() {
  // Ridge profile: a smooth bump reaching zero exactly at GLOW_R, so an edge
  // has no hard cut-off to alias against as it slides between rows.
  for (int i = 0; i < GLOW_N; i++) {
    float d = (float)i / GLOW_STEPS;
    float u = d / GLOW_R;
    float w = 1.0f - u * u;
    if (w < 0) w = 0;
    glowLUT[i] = (uint16_t)(GLOW_PEAK * w * w + 0.5f);
  }

  // Intensity ramp. v is how many rings' worth of light landed on the pixel:
  // below one it is only ever red, dimming out towards the edge of the ridge,
  // and past one the green and then the blue come up - which is where the
  // orange and the yellow around the far opening come from.
  for (int n = 0; n < 256; n++) {
    float v = n / 16.0f;
    float r, g, b;
    if (v <= 1.0f) {
      r = v;      g = 0.0f;              b = 0.0f;
    } else if (v <= 2.2f) {
      float t = (v - 1.0f) / 1.2f;
      r = 1.0f;   g = 0.58f * t;         b = 0.02f * t;
    } else if (v <= 4.0f) {
      float t = (v - 2.2f) / 1.8f;
      r = 1.0f;   g = 0.58f + 0.34f * t; b = 0.02f + 0.22f * t;
    } else {
      float t = (v - 4.0f) / 4.0f;
      if (t > 1.0f) t = 1.0f;
      r = 1.0f;   g = 0.92f + 0.08f * t; b = 0.24f + 0.60f * t;
    }
    colMap[n] = rgb(r, g, b);
  }
}

// --- Edges ---------------------------------------------------------------

// A horizontal edge at fractional row yc, spanning x0..x1. The weight is
// constant along the span, so each row of the ridge is one contiguous walk.
static void addHEdge(float yc, int x0, int x1, const uint16_t *rw) {
  if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
  if (x1 < 0 || x0 > SCREEN_W - 1) return;
  if (x0 < 0) x0 = 0;
  if (x1 > SCREEN_W - 1) x1 = SCREEN_W - 1;

  int r0 = (int)ceilf(yc - GLOW_R);
  int r1 = (int)floorf(yc + GLOW_R);
  if (r0 < 0) r0 = 0;
  if (r1 > SCREEN_H - 1) r1 = SCREEN_H - 1;

  for (int y = r0; y <= r1; y++) {
    int i = (int)(fabsf(y - yc) * GLOW_STEPS);
    if (i >= GLOW_N) continue;
    uint16_t w = rw[i];
    if (!w) continue;
    uint16_t *p = &acc[y * SCREEN_W + x0];
    for (int x = x0; x <= x1; x++) *p++ += w;
  }
}

// The same for a vertical edge at fractional column xc, spanning y0..y1
static void addVEdge(float xc, int y0, int y1, const uint16_t *rw) {
  if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
  if (y1 < 0 || y0 > SCREEN_H - 1) return;
  if (y0 < 0) y0 = 0;
  if (y1 > SCREEN_H - 1) y1 = SCREEN_H - 1;

  int c0 = (int)ceilf(xc - GLOW_R);
  int c1 = (int)floorf(xc + GLOW_R);
  if (c0 < 0) c0 = 0;
  if (c1 > SCREEN_W - 1) c1 = SCREEN_W - 1;

  for (int x = c0; x <= c1; x++) {
    int i = (int)(fabsf(x - xc) * GLOW_STEPS);
    if (i >= GLOW_N) continue;
    uint16_t w = rw[i];
    if (!w) continue;
    uint16_t *p = &acc[y0 * SCREEN_W + x];
    for (int y = y0; y <= y1; y++) { *p += w; p += SCREEN_W; }
  }
}

// --- Frame ---------------------------------------------------------------

void drawFrame() {
  memset(acc, 0, sizeof(acc));

  // Vanishing point for this frame
  const float amp = VP_AMP[vpMode];
  float fx = VP_FX + amp * sinf(TWO_PI * animTime / VP_PER_X);
  float fy = VP_FY + amp * sinf(TWO_PI * animTime / VP_PER_Y + 1.7f);
  if (fx < VP_MIN) fx = VP_MIN; if (fx > VP_MAX) fx = VP_MAX;
  if (fy < VP_MIN) fy = VP_MIN; if (fy > VP_MAX) fy = VP_MAX;

  // A ring at scale s has its left wall at ax*(1-s) and its right at ax + bx*s.
  // At s = 1 that is 0 and SCREEN_W whatever the vanishing point is, which is
  // why the corridor never leaves a gap at the border as it drifts.
  const float ax = fx * SCREEN_W;
  const float bx = (1.0f - fx) * SCREEN_W;
  const float ay = fy * SCREEN_H;
  const float by = (1.0f - fy) * SCREEN_H;

  uint16_t rw[GLOW_N];

  // Walk the ladder outwards from the nearest ring, one multiply per step
  float s = SMAX * powf(RATIO, ringPhase);

  for (int k = 0; k < RINGS; k++, s *= RATIO) {
    const float u = (k + ringPhase) / RINGS;   // 0 at the near end, 1 at the far

    float fade = 1.0f - FADE_FAR * u;
    if (u > 1.0f - SPAWN_FRAC) fade *= (1.0f - u) / SPAWN_FRAC;
    if (fade <= 0.0f) continue;

    // Rescale the ridge for this ring's depth once, so the span loops below
    // stay a load and an add
    const int scale = (int)(fade * 256.0f);
    for (int i = 0; i < GLOW_N; i++) rw[i] = (uint16_t)((glowLUT[i] * scale) >> 8);

    const float L = ax * (1.0f - s);
    const float R = ax + bx * s;
    const float T = ay * (1.0f - s);
    const float B = ay + by * s;

    const int xL = (int)lroundf(L), xR = (int)lroundf(R);
    const int yT = (int)lroundf(T), yB = (int)lroundf(B);

    addHEdge(T, xL, xR, rw);   // ceiling
    addHEdge(B, xL, xR, rw);   // floor
    addVEdge(L, yT, yB, rw);   // left wall
    addVEdge(R, yT, yB, rw);   // right wall
  }

  // Read the accumulator through the ramp
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
    uint16_t a = acc[i] >> 2;
    fb[i] = colMap[a > 255 ? 255 : a];
  }

  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
}
