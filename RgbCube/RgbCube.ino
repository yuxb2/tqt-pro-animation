/*
 * RGB Cube - wireframe cube spinning on itself with a tricolor chroma split
 *
 * Every edge is stamped with a soft round brush for thickness. The red core
 * sits in the middle; green and blue are smeared outwards from it as several
 * offset copies that fade with distance, blended additively. The tails start
 * just past the core rim so the middle of each edge stays red instead of
 * saturating to white.
 *
 * Renders into a static framebuffer pushed with pushImage() rather than a
 * TFT_eSprite: no heap allocation that can fail silently, and the additive
 * blend is a direct array access instead of readPixel/drawPixel per pixel.
 *
 * Buttons: left  = chroma smear width (0 .. 6 px)
 *          right = style (thin / thick / thick + vertex dots)
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

// Projection
#define CUBE_SCALE 34.0f   // pixels per unit at z = 0
#define CAM_DIST   3.2f    // camera distance in cube units
#define FOCAL      2.6f    // perspective strength

// Number of offset copies used to build each chroma tail
#define SMEAR_STEPS 4

// Soft round brush, points ordered by distance from the centre, so taking
// the first N points gives a smaller dot: one table serves both the thick
// core and the slightly thinner chroma tails.
#define BRUSH_MAX 13
static const int8_t BRUSH_DX[BRUSH_MAX] = { 0,  1, -1,  0,  0,  1,  1, -1, -1,  2, -2,  0,  0 };
static const int8_t BRUSH_DY[BRUSH_MAX] = { 0,  0,  0,  1, -1,  1, -1,  1, -1,  0,  0,  2, -2 };
static const float  BRUSH_W [BRUSH_MAX] = { 1.0f, .85f, .85f, .85f, .85f,
                                            .55f, .55f, .55f, .55f,
                                            .28f, .28f, .28f, .28f };

// Rotation speeds (rad/s) - deliberately non-commensurate so the tumble
// never repeats exactly
#define SPD_X 0.60f
#define SPD_Y 0.90f
#define SPD_Z 0.35f

// Cube geometry: vertex i has coords from its bit pattern
const int8_t cubeVerts[8][3] = {
  {-1, -1, -1}, { 1, -1, -1}, {-1,  1, -1}, { 1,  1, -1},
  {-1, -1,  1}, { 1, -1,  1}, {-1,  1,  1}, { 1,  1,  1}
};

// Edges connect vertices differing by a single bit
const uint8_t cubeEdges[12][2] = {
  {0, 1}, {2, 3}, {4, 5}, {6, 7},   // along X
  {0, 2}, {1, 3}, {4, 6}, {5, 7},   // along Y
  {0, 4}, {1, 5}, {2, 6}, {3, 7}    // along Z
};

// Projected vertices for the current frame
float projX[8], projY[8], projB[8];  // screen x, screen y, depth brightness

// Animation state
float angX = 0, angY = 0, angZ = 0;
unsigned long lastFrameTime = 0;
float animTime = 0;

// Look settings
float splitAmount = 3.0f;   // chroma smear width in pixels
int   style = 1;            // 0 = thin, 1 = thick, 2 = thick + vertex dots

// Heartbeat
unsigned long lastReport = 0;
uint32_t frameCount = 0;

void onLeftClick();
void onRightClick();
void updateCube(float dt);
void drawCube();

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== RGB Cube booting ===");

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
  Serial.println("RGB Cube initialized - entering loop");
}

void loop() {
  unsigned long now = millis();
  float dt = (now - lastFrameTime) / 1000.0f;
  lastFrameTime = now;
  animTime += dt;

  btnLeft.tick();
  btnRight.tick();

  updateCube(dt);
  drawCube();
  frameCount++;

  // Heartbeat: if this keeps printing, the loop is alive and any remaining
  // problem is on the display side rather than a crash or a hang
  if (now - lastReport >= 2000) {
    Serial.printf("alive - %lu fps, split=%.0f style=%d\n",
                  (unsigned long)(frameCount * 1000UL / (now - lastReport)),
                  splitAmount, style);
    frameCount = 0;
    lastReport = now;
  }

  delay(5);
}

void onLeftClick() {
  splitAmount += 1.5f;
  if (splitAmount > 6.0f) splitAmount = 0.0f;
  Serial.printf("smear = %.1f\n", splitAmount);
}

void onRightClick() {
  style = (style + 1) % 3;
  Serial.printf("style = %d\n", style);
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

// Line with per-endpoint brightness, stamped with the soft brush and blended
// additively. `brushN` sets the stroke thickness, `inten` its overall weight.
void addLineShaded(float x0, float y0, float b0,
                   float x1, float y1, float b1,
                   uint16_t col, float inten, int brushN) {
  float dx = x1 - x0;
  float dy = y1 - y0;
  int steps = (int)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy));
  if (steps < 1) steps = 1;

  for (int i = 0; i <= steps; i++) {
    float t = (float)i / steps;
    int px = (int)lroundf(x0 + dx * t);
    int py = (int)lroundf(y0 + dy * t);
    float b = (b0 + (b1 - b0) * t) * inten;

    for (int k = 0; k < brushN; k++) {
      addPixel(px + BRUSH_DX[k], py + BRUSH_DY[k], scaleColor(col, b * BRUSH_W[k]));
    }
  }
}

// --- Cube ---------------------------------------------------------------

void updateCube(float dt) {
  angX += SPD_X * dt;
  angY += SPD_Y * dt;
  angZ += SPD_Z * dt;

  float sx = sinf(angX), cx = cosf(angX);
  float sy = sinf(angY), cy = cosf(angY);
  float sz = sinf(angZ), cz = cosf(angZ);

  for (int i = 0; i < 8; i++) {
    float x = cubeVerts[i][0];
    float y = cubeVerts[i][1];
    float z = cubeVerts[i][2];

    // Rotate X, then Y, then Z
    float y1 = y * cx - z * sx;
    float z1 = y * sx + z * cx;

    float x2 = x * cy + z1 * sy;
    float z2 = -x * sy + z1 * cy;

    float x3 = x2 * cz - y1 * sz;
    float y3 = x2 * sz + y1 * cz;

    // Perspective projection
    float depth = CAM_DIST + z2;
    if (depth < 0.5f) depth = 0.5f;
    float k = FOCAL / depth;

    projX[i] = CENTER_X + x3 * k * CUBE_SCALE;
    projY[i] = CENTER_Y + y3 * k * CUBE_SCALE;

    // Nearer vertices burn brighter (z2 spans roughly -1.73 .. 1.73)
    projB[i] = 0.45f + 0.55f * (1.0f - (z2 + 1.73f) / 3.46f);
  }
}

void drawCube() {
  memset(fb, 0, sizeof(fb));

  // Smear breathes slightly so the fringes feel alive
  float split = splitAmount * (0.8f + 0.2f * sinf(animTime * 1.7f));
  int coreBrush = (style >= 1) ? BRUSH_MAX : 5;
  int tailBrush = (style >= 1) ? 9 : 5;

  for (int e = 0; e < 12; e++) {
    int a = cubeEdges[e][0];
    int b = cubeEdges[e][1];

    float x0 = projX[a], y0 = projY[a], b0 = projB[a];
    float x1 = projX[b], y1 = projY[b], b1 = projB[b];

    // Green bleeds left, blue bleeds right. Copies start at 1.2 px - just
    // past the core rim - and fade as they move out, so the tails read as a
    // gradient rather than as separate parallel lines.
    for (int s = SMEAR_STEPS; s >= 1; s--) {
      float f = (float)s / SMEAR_STEPS;
      float o = 1.2f + split * f;
      float inten = 0.80f - 0.35f * f;
      addLineShaded(x0 - o, y0, b0, x1 - o, y1, b1, COL_GREEN, inten, tailBrush);
      addLineShaded(x0 + o, y0, b0, x1 + o, y1, b1, COL_BLUE, inten, tailBrush);
    }

    // Red core last, at full weight, so the middle of the edge stays red
    addLineShaded(x0, y0, b0, x1, y1, b1, COL_RED, 1.0f, coreBrush);
  }

  if (style == 2) {
    for (int i = 0; i < 8; i++) {
      int px = (int)lroundf(projX[i]);
      int py = (int)lroundf(projY[i]);
      for (int k = 0; k < BRUSH_MAX; k++) {
        addPixel(px + BRUSH_DX[k], py + BRUSH_DY[k],
                 scaleColor(0xFFFF, projB[i] * BRUSH_W[k]));
      }
    }
  }

  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
}
