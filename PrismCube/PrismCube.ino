/*
 * Prism Cube - a tumbling wireframe cube echoed in nested rainbow shells
 *
 * The cube is drawn fourteen times over, each pass at a slightly larger scale
 * about the same centre, so the copies nest like the layers of an onion. Hue
 * is a function of the layer: pink in the core, running up through red,
 * orange, yellow, green, cyan to blue on the outermost shell. The layers sit
 * close enough together that the bands merge into one continuous spectrum
 * smeared perpendicular to every edge rather than reading as separate lines.
 *
 * The eight cube vertices are rotated once per frame; each shell then only
 * costs a multiply and a perspective divide, so the layer count is cheap -
 * the whole thing lands around 26k pixel writes per frame, less than a single
 * chroma-split cube with a fat brush.
 *
 * Everything is stamped with a soft round brush and blended additively into a
 * static framebuffer pushed with pushImage(): no heap allocation that can
 * fail silently, and the blend is a direct array access rather than
 * readPixel/drawPixel per pixel. Near edges carry an extra white term, so the
 * face turned towards the viewer burns out to white the way it does on a real
 * panel while the far side stays coloured.
 *
 * Buttons: left  = shell spread (tight / medium / wide / very wide)
 *          right = palette (full spectrum / drifting spectrum / warm-to-cool)
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

#define COL_BLACK 0x0000
#define COL_WHITE 0xFFFF

// Projection. The camera sits fairly far back on purpose: a gentle
// perspective keeps the far face large enough that its own set of rainbow
// bands stays visible inside the near one, which is most of the depth cue.
#define CUBE_SCALE 33.0f   // pixels per unit at z = 0
#define CAM_DIST   4.0f    // camera distance in cube units
#define FOCAL      3.3f    // perspective strength

// Nested shells. SHELL_MAX sizes the arrays; SHELLS is what actually gets
// drawn and stays fixed - the look is tuned around this count.
#define SHELL_MAX 16
#define SHELLS    14

// Hue of the outermost shell (blue) and the step taken towards the core.
// 14 shells at 21 deg walk 240 down to 327: blue, cyan, green, yellow,
// orange, red and finally a pink core. The count is set by the need to keep
// the bands touching at the widest spread - any fewer and the rainbow breaks
// up into separate stripes with black between them.
#define HUE_OUTER 240.0f
#define HUE_STEP   21.0f

// Rotation speeds (rad/s) - deliberately non-commensurate so the tumble
// never repeats exactly
#define SPD_X 0.35f
#define SPD_Y 0.52f
#define SPD_Z 0.21f

// Soft round brush, points ordered by distance from the centre, so taking
// the first N points gives a smaller dot: one table serves both the fat
// outer shells and the thinner inner ones.
#define BRUSH_MAX 9
static const int8_t BRUSH_DX[BRUSH_MAX] = { 0,  1, -1,  0,  0,  1,  1, -1, -1 };
static const int8_t BRUSH_DY[BRUSH_MAX] = { 0,  0,  0,  1, -1,  1, -1,  1, -1 };
static const float  BRUSH_W [BRUSH_MAX] = { 1.0f, .80f, .80f, .80f, .80f,
                                            .45f, .45f, .45f, .45f };

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

// Rotated unit-cube vertices for the current frame, shared by every shell
float rotX[8], rotY[8], rotZ[8];

// Per-shell projected vertices, brightness and white burn-out
float projX[SHELL_MAX][8], projY[SHELL_MAX][8];
float projB[SHELL_MAX][8], projWhite[SHELL_MAX][8];
uint16_t shellCol[SHELL_MAX];

// Animation state
float angX = 0, angY = 0, angZ = 0;
unsigned long lastFrameTime = 0;
float animTime = 0;

// Look settings
int   spread = 2;   // 0 tight .. 3 very wide
int   palette = 0;  // 0 full spectrum, 1 drifting, 2 warm-to-cool

// How far apart the shells sit: the innermost shell's scale, the outermost
// always being 1.0
static const float SPREAD_MIN[4] = { 0.70f, 0.55f, 0.40f, 0.25f };

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
  Serial.println("=== Prism Cube booting ===");

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
  Serial.println("Prism Cube initialized - entering loop");
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
    Serial.printf("alive - %lu fps, spread=%d palette=%d\n",
                  (unsigned long)(frameCount * 1000UL / (now - lastReport)),
                  spread, palette);
    frameCount = 0;
    lastReport = now;
  }

  delay(5);
}

void onLeftClick() {
  spread = (spread + 1) % 4;
  Serial.printf("spread = %d (inner scale %.2f)\n", spread, SPREAD_MIN[spread]);
}

void onRightClick() {
  palette = (palette + 1) % 3;
  Serial.printf("palette = %d\n", palette);
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

// Fully saturated hue -> RGB565. h is in degrees, wrapped internally.
static uint16_t hueColor(float h) {
  h = fmodf(h, 360.0f);
  if (h < 0) h += 360.0f;

  int sector = (int)(h / 60.0f);
  float f = h / 60.0f - sector;

  float r, g, b;
  switch (sector) {
    case 0:  r = 1;     g = f;     b = 0;     break;   // red    -> yellow
    case 1:  r = 1 - f; g = 1;     b = 0;     break;   // yellow -> green
    case 2:  r = 0;     g = 1;     b = f;     break;   // green  -> cyan
    case 3:  r = 0;     g = 1 - f; b = 1;     break;   // cyan   -> blue
    case 4:  r = f;     g = 0;     b = 1;     break;   // blue   -> magenta
    default: r = 1;     g = 0;     b = 1 - f; break;   // magenta-> red
  }

  return ((uint16_t)(r * 31) << 11) | ((uint16_t)(g * 63) << 5) | (uint16_t)(b * 31);
}

static inline void addPixel(int x, int y, uint16_t c) {
  if ((unsigned)x >= SCREEN_W || (unsigned)y >= SCREEN_H || c == 0) return;
  uint16_t *p = &fb[x + y * SCREEN_W];
  *p = addColors(*p, c);
}

// Line with per-endpoint brightness and per-endpoint white burn-out, stamped
// with the soft brush and blended additively. `brushN` sets the stroke
// thickness, `inten` its overall weight.
void addLineShaded(float x0, float y0, float b0, float w0,
                   float x1, float y1, float b1, float w1,
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
    float w = (w0 + (w1 - w0) * t) * inten;

    for (int k = 0; k < brushN; k++) {
      uint16_t c = scaleColor(col, b * BRUSH_W[k]);
      if (w > 0) c = addColors(c, scaleColor(COL_WHITE, w * BRUSH_W[k]));
      addPixel(px + BRUSH_DX[k], py + BRUSH_DY[k], c);
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

  // Rotate the unit cube once; the shells are just scaled copies of this
  for (int i = 0; i < 8; i++) {
    float x = cubeVerts[i][0];
    float y = cubeVerts[i][1];
    float z = cubeVerts[i][2];

    // Rotate X, then Y, then Z
    float y1 = y * cx - z * sx;
    float z1 = y * sx + z * cx;

    float x2 = x * cy + z1 * sy;
    float z2 = -x * sy + z1 * cy;

    rotX[i] = x2 * cz - y1 * sz;
    rotY[i] = x2 * sz + y1 * cz;
    rotZ[i] = z2;
  }

  // Shell scales breathe a little so the spectrum feels alive rather than
  // pinned to fixed radii
  float innerBase = SPREAD_MIN[spread];
  float breath = 1.0f + 0.10f * sinf(animTime * 0.9f);
  float inner = 1.0f - (1.0f - innerBase) * breath;
  if (inner < 0.12f) inner = 0.12f;

  float hueDrift = (palette == 1) ? animTime * 22.0f : 0.0f;
  float hueStep  = (palette == 2) ? HUE_STEP * 0.55f : HUE_STEP;

  for (int s = 0; s < SHELLS; s++) {
    // s = 0 is the core, s = SHELLS-1 the outermost shell
    float f = (float)s / (SHELLS - 1);
    float scale = inner + (1.0f - inner) * f;

    shellCol[s] = hueColor(HUE_OUTER + hueDrift - (SHELLS - 1 - s) * hueStep);

    for (int i = 0; i < 8; i++) {
      float z = rotZ[i] * scale;

      float depth = CAM_DIST + z;
      if (depth < 0.5f) depth = 0.5f;
      float k = FOCAL / depth;

      projX[s][i] = CENTER_X + rotX[i] * scale * k * CUBE_SCALE;
      projY[s][i] = CENTER_Y + rotY[i] * scale * k * CUBE_SCALE;

      // Nearness: 1 at the vertex closest to the camera, 0 at the furthest
      float nearness = 1.0f - (z + 1.8f) / 3.6f;
      if (nearness < 0) nearness = 0;
      if (nearness > 1) nearness = 1;

      projB[s][i] = 0.40f + 0.75f * nearness;

      // Only the last stretch towards the camera burns out to white, which
      // keeps the far side of the cube purely chromatic
      float w = (nearness - 0.72f) / 0.28f;
      projWhite[s][i] = (w > 0) ? w * 0.55f : 0.0f;
    }
  }
}

void drawCube() {
  memset(fb, 0, sizeof(fb));

  // Outermost shell first so the core lands on top of the pile
  for (int s = SHELLS - 1; s >= 0; s--) {
    // The outermost shell gets the fat brush to give the silhouette some
    // weight; the rest stay thin so the bands read as separate hues instead
    // of collapsing into a blob
    int brushN = (s == SHELLS - 1) ? BRUSH_MAX : 5;
    uint16_t col = shellCol[s];

    for (int e = 0; e < 12; e++) {
      int a = cubeEdges[e][0];
      int b = cubeEdges[e][1];

      addLineShaded(projX[s][a], projY[s][a], projB[s][a], projWhite[s][a],
                    projX[s][b], projY[s][b], projB[s][b], projWhite[s][b],
                    col, 0.85f, brushN);
    }
  }

  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
}
