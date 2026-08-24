/*
 * RGB Lissajous - a thin Lissajous curve with radial chromatic aberration
 *
 * The figure is x = sin(a*t + phase), y = sin(b*t), sampled as a closed
 * polyline. The phase drifts continuously, so the curve keeps folding through
 * itself instead of standing still.
 *
 * The colour split mimics a cheap lens rather than a fixed sideways smear:
 * each channel is drawn as a copy of the curve *scaled* about the centre -
 * red slightly smaller, green nominal, blue slightly larger. The fringe is
 * therefore zero where the curve passes through the middle of the screen and
 * widest out on the rim, exactly like real chromatic aberration, and the
 * diagonals running through the centre stay white while the outer arcs break
 * into a full spectrum. Where the channel bands overlap the additive blend
 * fills in the yellow and cyan steps between them, so only three colours are
 * ever written.
 *
 * Renders into a static framebuffer pushed with pushImage() rather than a
 * TFT_eSprite: no heap allocation that can fail silently, and the additive
 * blend is a direct array access instead of readPixel/drawPixel per pixel.
 *
 * Buttons: left  = chroma fringe width (0 .. 3.5 px)
 *          right = frequency ratio preset
 *
 * Hardware: LilyGo T-QT Pro (ESP32-S3, 128x128, TFT_eSPI Setup211)
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include "OneButton.h"

TFT_eSPI tft = TFT_eSPI();

#define SCREEN_W 128
#define SCREEN_H 128
#define CENTER_X 64
#define CENTER_Y 64

// 32 KB framebuffer, statically allocated so it can never fail at runtime
static uint16_t fb[SCREEN_W * SCREEN_H];

#define PIN_BTN_L 0
#define PIN_BTN_R 47

OneButton btnLeft(PIN_BTN_L, true, true);
OneButton btnRight(PIN_BTN_R, true, true);

// Pure channel colors (RGB565)
#define COL_BLACK 0x0000
#define COL_RED   0xF800
#define COL_GREEN 0x07E0
#define COL_BLUE  0x001F

// Curve size in pixels. The blue copy is drawn at 1 + 1.6 * fringe / AMP, so
// this plus the widest fringe has to stay under 64 or the rim gets clipped.
#define AMP_X 56.0f
#define AMP_Y 56.0f

// Speed of the phase drift (rad/s) and of the fringe breathing
#define PHASE_SPD  0.35f
#define BREATH_SPD 1.30f

// Depth of the brightness wave travelling along the curve (0 = flat stroke)
#define SHIMMER 0.18f

// Channel band centres, in units of the fringe width: red pulled in, blue
// pushed out, green left on the nominal curve.
static const float    CHAN_POS[3] = { -1.0f, 0.0f, 1.0f };
static const uint16_t CHAN_COL[3] = { COL_RED, COL_GREEN, COL_BLUE };

// Each channel is itself smeared over a short span, so neighbouring bands
// overlap and blend instead of showing as three separate hairlines. Every
// copy is a single-pixel stroke: nine to fifteen of them side by side already
// give the stroke its width, and a fat brush on top would just wash the whole
// thing out to white.
#define CHROMA_SUB 5
static const float SUB_POS[CHROMA_SUB] = { -0.60f, -0.30f, 0.00f, 0.30f, 0.60f };
static const float SUB_W  [CHROMA_SUB] = {  0.45f,  0.80f, 1.00f, 0.80f, 0.45f };

// Frequency ratios, cycled with the right button. Both terms are coprime, so
// the curve closes after one period; a and b differ, so no phase ever
// collapses the figure down to a single stroke.
struct LissRatio { uint8_t a, b; };
static const LissRatio RATIOS[] = { {3, 4}, {2, 3}, {3, 5}, {4, 5}, {5, 6}, {5, 7} };
#define RATIO_COUNT (sizeof(RATIOS) / sizeof(RATIOS[0]))

// Sampled curve, in pixels relative to the centre, at scale 1.0. The sample
// count follows the highest frequency, since that is what sets how far the
// curve sweeps between two samples.
#define MAX_POINTS 600
static float ptX[MAX_POINTS + 1];
static float ptY[MAX_POINTS + 1];
static int   pointCount = 0;

// Animation state
float phase = 0;
float animTime = 0;
unsigned long lastFrameTime = 0;

// Look settings
float splitAmount = 1.75f;   // chroma fringe width in pixels, measured at the rim
int   ratioIndex = 0;

// Heartbeat
unsigned long lastReport = 0;
uint32_t frameCount = 0;

void onLeftClick();
void onRightClick();
void buildCurve();
void drawCurve();

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== RGB Lissajous booting ===");

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(COL_BLACK);

  // Byte order for pushImage of an in-memory RGB565 buffer.
  // If red and blue come out swapped, change this to false.
  tft.setSwapBytes(true);

  btnLeft.attachClick(onLeftClick);
  btnRight.attachClick(onRightClick);

  lastFrameTime = millis();
  lastReport = millis();

  Serial.printf("free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());
  Serial.println("RGB Lissajous initialized - entering loop");
}

void loop() {
  unsigned long now = millis();
  float dt = (now - lastFrameTime) / 1000.0f;
  lastFrameTime = now;
  animTime += dt;
  phase += PHASE_SPD * dt;
  if (phase > TWO_PI) phase -= TWO_PI;

  btnLeft.tick();
  btnRight.tick();

  buildCurve();
  drawCurve();
  frameCount++;

  // Heartbeat: if this keeps printing, the loop is alive and any remaining
  // problem is on the display side rather than a crash or a hang
  if (now - lastReport >= 2000) {
    Serial.printf("alive - %lu fps, fringe=%.2f ratio=%u:%u pts=%d\n",
                  (unsigned long)(frameCount * 1000UL / (now - lastReport)),
                  splitAmount,
                  RATIOS[ratioIndex].a, RATIOS[ratioIndex].b, pointCount);
    frameCount = 0;
    lastReport = now;
  }

  delay(5);
}

void onLeftClick() {
  splitAmount += 0.875f;
  if (splitAmount > 3.5f) splitAmount = 0.0f;
  Serial.printf("fringe = %.2f\n", splitAmount);
}

void onRightClick() {
  ratioIndex = (ratioIndex + 1) % RATIO_COUNT;
  Serial.printf("ratio = %u:%u\n", RATIOS[ratioIndex].a, RATIOS[ratioIndex].b);
}

// --- Color helpers ------------------------------------------------------

// Additive blend of two RGB565 colors, saturating per channel
static inline uint16_t addColors(uint16_t a, uint16_t b) {
  uint16_t r = ((a >> 11) & 0x1F) + ((b >> 11) & 0x1F);
  uint16_t g = ((a >> 5) & 0x3F) + ((b >> 5) & 0x3F);
  uint16_t bl = (a & 0x1F) + (b & 0x1F);
  if (r > 0x1F) r = 0x1F;
  if (g > 0x3F) g = 0x3F;
  if (bl > 0x1F) bl = 0x1F;
  return (r << 11) | (g << 5) | bl;
}

// Scale an RGB565 color by a 0..1 factor
static inline uint16_t scaleColor(uint16_t c, float f) {
  if (f <= 0) return 0;
  if (f > 1) f = 1;
  uint16_t r = (uint16_t)(((c >> 11) & 0x1F) * f);
  uint16_t g = (uint16_t)(((c >> 5) & 0x3F) * f);
  uint16_t b = (uint16_t)((c & 0x1F) * f);
  return (r << 11) | (g << 5) | b;
}

static inline void addPixel(int x, int y, uint16_t c) {
  if ((unsigned)x >= SCREEN_W || (unsigned)y >= SCREEN_H || c == 0) return;
  uint16_t *p = &fb[x + y * SCREEN_W];
  *p = addColors(*p, c);
}

// Single-pixel line with per-endpoint brightness, blended additively.
// `inten` is the overall weight of the stroke.
void addLineShaded(float x0, float y0, float b0,
                   float x1, float y1, float b1,
                   uint16_t col, float inten) {
  float dx = x1 - x0;
  float dy = y1 - y0;
  int steps = (int)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy));
  if (steps < 1) steps = 1;

  for (int i = 0; i <= steps; i++) {
    float t = (float)i / steps;
    int px = (int)lroundf(x0 + dx * t);
    int py = (int)lroundf(y0 + dy * t);
    addPixel(px, py, scaleColor(col, (b0 + (b1 - b0) * t) * inten));
  }
}

// --- Curve --------------------------------------------------------------

// Sample one full period of the current ratio.
void buildCurve() {
  uint8_t a = RATIOS[ratioIndex].a;
  uint8_t b = RATIOS[ratioIndex].b;
  uint8_t maxF = (a > b) ? a : b;

  int n = 128 * maxF;
  if (n < 256) n = 256;
  if (n > MAX_POINTS) n = MAX_POINTS;
  pointCount = n;

  float step = TWO_PI / n;
  for (int i = 0; i <= n; i++) {
    float t = i * step;
    ptX[i] = AMP_X * sinf(a * t + phase);
    ptY[i] = AMP_Y * sinf(b * t);
  }
}

void drawCurve() {
  memset(fb, 0, sizeof(fb));

  // The fringe breathes slightly so the dispersion feels alive. Dividing by
  // the amplitude turns "pixels of fringe at the rim" into the scale factor
  // each channel copy is drawn at.
  float fringe = splitAmount * (0.85f + 0.15f * sinf(animTime * BREATH_SPD));
  float k = fringe / AMP_X;

  // Brightness wave travelling backwards along the curve
  float shimPhase = animTime * 3.0f;
  float shimStep = 6.0f * TWO_PI / pointCount;

  for (int c = 0; c < 3; c++) {
    for (int j = 0; j < CHROMA_SUB; j++) {
      float scale = 1.0f + (CHAN_POS[c] + SUB_POS[j]) * k;
      float inten = SUB_W[j];

      float x0 = CENTER_X + ptX[0] * scale;
      float y0 = CENTER_Y + ptY[0] * scale;
      float s0 = 1.0f - SHIMMER + SHIMMER * sinf(-shimPhase);

      for (int i = 1; i <= pointCount; i++) {
        float x1 = CENTER_X + ptX[i] * scale;
        float y1 = CENTER_Y + ptY[i] * scale;
        float s1 = 1.0f - SHIMMER + SHIMMER * sinf(i * shimStep - shimPhase);

        addLineShaded(x0, y0, s0, x1, y1, s1, CHAN_COL[c], inten);

        x0 = x1; y0 = y1; s0 = s1;
      }
    }
  }

  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
}
