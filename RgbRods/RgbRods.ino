/*
 * RGB Rods - a square grid of little bars, each turning at its own slow rate
 *
 * Every cell of the grid holds one short bar. The bars all turn, none of them
 * at the same rate, and the rate field is smooth across the grid: neighbours
 * differ only slightly, so at any instant the orientations form continuous
 * bands. As the slow corner falls further and further behind the fast one
 * those bands wind up into ever finer chevrons - broad and combed out where
 * the rates are close, herringbone where they have drifted a full turn apart.
 *
 * Each bar is drawn three times, once per channel, the red copy pushed one way
 * and the blue copy the other with green left on the nominal position. A bar
 * lying along the split reads as coloured segments down its length - red tip,
 * yellow, white core, cyan, blue tip - while a bar across the split breaks
 * into separate pure red, green and blue rails. Every bar therefore recolours
 * itself continuously just by turning, and where two neighbours touch the
 * additive blend fills in the magentas. Only three colours are ever written.
 *
 * Renders into a static framebuffer pushed with pushImage() rather than a
 * TFT_eSprite: no heap allocation that can fail silently, and the additive
 * blend is a direct array access instead of readPixel/drawPixel per pixel.
 *
 * Buttons: left  = chroma split width (subtle / normal / wide / split rails)
 *          right = split axis (screen horizontal / radial / along each bar)
 *
 * Hardware: LilyGo T-QT Pro (ESP32-S3, 128x128, TFT_eSPI Setup211)
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include "OneButton.h"

TFT_eSPI tft = TFT_eSPI();

#define SCREEN_W 128
#define SCREEN_H 128
#define CENTER_X 64.0f
#define CENTER_Y 64.0f

// ===========================================================================
// GRID SIZE - the knob to turn
//
// Number of bars along one side of the square grid; the screen carries
// RODS_PER_SIDE * RODS_PER_SIDE of them. Cell size, bar length, bar thickness
// and chroma split are all derived from this, so it is the only value that has
// to change. 8 gives fat poster-like bars, 10 is the default, 14 and up turns
// the panel into fine texture.
// ===========================================================================
#define RODS_PER_SIDE 10

// Array sizing only - raising it costs 8 bytes of RAM per rod
#define RODS_MAX 20

#if RODS_PER_SIDE < 2 || RODS_PER_SIDE > RODS_MAX
#error "RODS_PER_SIDE must be between 2 and RODS_MAX"
#endif

#define ROD_COUNT (RODS_PER_SIDE * RODS_PER_SIDE)

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

// Bar geometry as a fraction of one grid cell. 0.84 leaves a hair of black
// between neighbours when they line up, so the combed-out stretches read as
// dashed rows rather than solid lines. Length stays under the cell, so a bar
// never leaves its own square however it turns.
#define ROD_LEN_FRAC   0.84f
#define ROD_THICK_FRAC 0.17f
#define ROD_THICK_MIN  1.0f
#define ROD_THICK_MAX  3.0f

// Spacing of the parallel strokes that make up one bar's thickness. Half a
// pixel, so a diagonal bar comes out solid instead of combed.
#define STROKE_STEP 0.5f

// Peak angular speed of the fastest bar (rad/s) and the fraction of it the
// slowest one gets. Both deliberately slow: 0.30 rad/s is about 17 deg/s.
#define SPD_MAX      0.30f
#define SPD_MIN_FRAC 0.12f

// No two bars may share a period, so every rate gets a small unique nudge on
// top of the smooth field. It has to stay tiny: this offset is multiplied by
// the wind drive just like the rate itself is, so anything above a fraction of
// a percent turns into a random per-bar angle of a radian or more and shakes
// the bands apart into plain noise.
#define SPD_JITTER 0.002f

// The grid winds up and unwinds over this cycle, in seconds. Fixed speeds
// would eventually leave every neighbouring pair out of phase and the screen
// would settle into permanent noise; driving the rotation with one very slow
// sine instead means the picture keeps passing back through the smooth combed
// state.
//
// Rate times WIND_K is how far a bar swings from rest, so this period also
// sets how deep the wind-up goes. At 450 s the steepest part of the rate field
// pulls neighbours about 1.8 rad apart at full wind - tight herringbone - while
// the shallow corner stays inside half a radian and reads as smooth bands.
// Much longer and the whole panel spends its time saturated into noise.
#define WIND_PERIOD 450.0f
#define WIND_K      (WIND_PERIOD / TWO_PI)

// Where in that cycle the sketch starts. A sixth of a turn in is half wound,
// so the first frame after a reset already has chevrons instead of showing the
// flat combed field.
#define WIND_START (PI / 6.0f)

// Rest orientation: a gentle fan across the anti-diagonal, so even fully
// unwound the bars sweep from one corner to the other rather than standing in
// a plain comb.
#define PHI0_SPAN 1.15f

// Chroma split at level 1, as a fraction of the cell, with a floor in pixels
// so a dense grid still splits into visible colour.
#define SPLIT_FRAC 0.20f
#define SPLIT_MIN  1.0f
static const float SPLIT_LEVEL[4] = { 0.45f, 1.00f, 1.60f, 2.40f };
#define SPLIT_LEVEL_COUNT 4

// Channel band centres, in units of the split width: red pulled one way, blue
// the other, green left on the bar itself.
static const float    CHAN_OFF[3] = { -1.0f, 0.0f, 1.0f };
static const uint16_t CHAN_COL[3] = { COL_RED, COL_GREEN, COL_BLUE };

// Split axes, cycled with the right button
#define SPLIT_AXIS_H     0   // fixed screen horizontal
#define SPLIT_AXIS_RADIAL 1  // outward from the centre, widening with radius
#define SPLIT_AXIS_ROD   2   // along each bar, so the split always runs length
#define SPLIT_AXIS_COUNT 3

// Radius at which the radial split reaches its nominal width
#define RADIAL_RIM 60.0f

// Per-rod constants, built once in setup()
static float rodRate[RODS_MAX * RODS_MAX];
static float rodPhi0[RODS_MAX * RODS_MAX];

// Derived grid metrics
static float cellSize;
static float rodHalfLen;
static int   rodStrokes;

// Animation state
float animTime = 0;
unsigned long lastFrameTime = 0;

// Look settings
int splitLevel = 1;
int splitAxis  = SPLIT_AXIS_H;

// Heartbeat
unsigned long lastReport = 0;
uint32_t frameCount = 0;

void onLeftClick();
void onRightClick();
void buildGrid();
void drawRods();

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== RGB Rods booting ===");

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(COL_BLACK);

  // Byte order for pushImage of an in-memory RGB565 buffer.
  // If red and blue come out swapped, change this to false.
  tft.setSwapBytes(true);

  btnLeft.attachClick(onLeftClick);
  btnRight.attachClick(onRightClick);

  buildGrid();

  lastFrameTime = millis();
  lastReport = millis();

  Serial.printf("grid: %dx%d = %d rods, cell %.2f px, bar %.1f x %.1f px\n",
                RODS_PER_SIDE, RODS_PER_SIDE, ROD_COUNT,
                cellSize, rodHalfLen * 2.0f,
                (rodStrokes - 1) * STROKE_STEP + 1.0f);
  Serial.printf("free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());
  Serial.println("RGB Rods initialized - entering loop");
}

void loop() {
  unsigned long now = millis();
  float dt = (now - lastFrameTime) / 1000.0f;
  lastFrameTime = now;
  animTime += dt;
  if (animTime > WIND_PERIOD) animTime -= WIND_PERIOD;

  btnLeft.tick();
  btnRight.tick();

  drawRods();
  frameCount++;

  // Heartbeat: if this keeps printing, the loop is alive and any remaining
  // problem is on the display side rather than a crash or a hang
  if (now - lastReport >= 2000) {
    Serial.printf("alive - %lu fps, split=%d axis=%d wind=%.0f%%\n",
                  (unsigned long)(frameCount * 1000UL / (now - lastReport)),
                  splitLevel, splitAxis,
                  100.0f * animTime / WIND_PERIOD);
    frameCount = 0;
    lastReport = now;
  }

  delay(5);
}

void onLeftClick() {
  splitLevel = (splitLevel + 1) % SPLIT_LEVEL_COUNT;
  Serial.printf("split level = %d (%.2f x cell)\n",
                splitLevel, SPLIT_FRAC * SPLIT_LEVEL[splitLevel]);
}

void onRightClick() {
  splitAxis = (splitAxis + 1) % SPLIT_AXIS_COUNT;
  Serial.printf("split axis = %d\n", splitAxis);
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

static inline void addPixel(int x, int y, uint16_t c) {
  if ((unsigned)x >= SCREEN_W || (unsigned)y >= SCREEN_H || c == 0) return;
  uint16_t *p = &fb[x + y * SCREEN_W];
  *p = addColors(*p, c);
}

// --- Drawing ------------------------------------------------------------

// Flat single-pixel line, blended additively. The bars are drawn at full
// intensity with no shading: the colour comes entirely from which channels
// land on a given pixel, which is what keeps the whole picture to the six
// saturated mixes plus white.
static void addLine(float x0, float y0, float x1, float y1, uint16_t col) {
  float dx = x1 - x0;
  float dy = y1 - y0;
  int steps = (int)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy));
  if (steps < 1) steps = 1;

  float ix = dx / steps;
  float iy = dy / steps;
  float x = x0;
  float y = y0;

  for (int i = 0; i <= steps; i++) {
    addPixel((int)lroundf(x), (int)lroundf(y), col);
    x += ix;
    y += iy;
  }
}

// One bar: a run of parallel strokes half a pixel apart, laid across the
// direction (ux, uy). Stamping a round brush along a single line would round
// the ends off; the bars in this piece want to stay hard-edged rectangles.
static void addBar(float cx, float cy, float ux, float uy,
                   float halfLen, uint16_t col, int strokes) {
  float px = -uy;   // unit perpendicular
  float py =  ux;
  float off = -(strokes - 1) * 0.5f * STROKE_STEP;

  for (int s = 0; s < strokes; s++) {
    float o = off + s * STROKE_STEP;
    float ox = cx + px * o;
    float oy = cy + py * o;
    addLine(ox - ux * halfLen, oy - uy * halfLen,
            ox + ux * halfLen, oy + uy * halfLen, col);
  }
}

// --- Grid ---------------------------------------------------------------

// Small integer hash, used only to make every rate unique
static float hash01(uint32_t n) {
  n = (n ^ 61u) ^ (n >> 16);
  n *= 9u;
  n ^= n >> 4;
  n *= 0x27d4eb2du;
  n ^= n >> 15;
  return (n & 0xFFFFFFu) / (float)0x1000000u;
}

void buildGrid() {
  cellSize = (float)SCREEN_W / RODS_PER_SIDE;
  rodHalfLen = cellSize * ROD_LEN_FRAC * 0.5f;

  float thick = cellSize * ROD_THICK_FRAC;
  if (thick < ROD_THICK_MIN) thick = ROD_THICK_MIN;
  if (thick > ROD_THICK_MAX) thick = ROD_THICK_MAX;
  rodStrokes = (int)lroundf((thick - 1.0f) / STROKE_STEP) + 1;
  if (rodStrokes < 1) rodStrokes = 1;

  float spdMin = SPD_MAX * SPD_MIN_FRAC;

  for (int j = 0; j < RODS_PER_SIDE; j++) {
    for (int i = 0; i < RODS_PER_SIDE; i++) {
      int k = j * RODS_PER_SIDE + i;
      float fx = (float)i / (RODS_PER_SIDE - 1);
      float fy = (float)j / (RODS_PER_SIDE - 1);

      // Rate field. It has to be smooth for the orientations to band up at
      // all; the product term bends the lines of equal rate into hyperbolas,
      // which is what curves the chevrons instead of leaving them as straight
      // diagonal stripes. The jitter is small enough not to break the bands
      // but large enough that no two bars ever come back into step.
      float u = 0.35f * (fx + fy) * 0.5f + 0.65f * fx * fy;
      float jit = SPD_JITTER * (2.0f * hash01((uint32_t)k * 2654435761u) - 1.0f);

      rodRate[k] = (spdMin + (SPD_MAX - spdMin) * u) * (1.0f + jit);
      rodPhi0[k] = PHI0_SPAN * (fx - fy);
    }
  }
}

void drawRods() {
  memset(fb, 0, sizeof(fb));

  // One shared drive for the whole grid; each bar scales it by its own rate,
  // so every bar turns at its own speed and they all pass through rest
  // together at the ends of the cycle.
  float drive = WIND_K * sinf(TWO_PI * animTime / WIND_PERIOD + WIND_START);

  float split = cellSize * SPLIT_FRAC * SPLIT_LEVEL[splitLevel];
  if (split < SPLIT_MIN) split = SPLIT_MIN;

  for (int j = 0; j < RODS_PER_SIDE; j++) {
    float cy = (j + 0.5f) * cellSize;

    for (int i = 0; i < RODS_PER_SIDE; i++) {
      int k = j * RODS_PER_SIDE + i;
      float cx = (i + 0.5f) * cellSize;

      float a = rodPhi0[k] + rodRate[k] * drive;
      float ux = cosf(a);
      float uy = sinf(a);

      // Direction the three channel copies are pushed apart along
      float sx, sy;
      if (splitAxis == SPLIT_AXIS_ROD) {
        sx = ux;
        sy = uy;
      } else if (splitAxis == SPLIT_AXIS_RADIAL) {
        // Left unnormalised: the fringe is zero at the middle of the screen
        // and widest out on the rim, the way a real lens disperses.
        sx = (cx - CENTER_X) / RADIAL_RIM;
        sy = (cy - CENTER_Y) / RADIAL_RIM;
      } else {
        sx = 1.0f;
        sy = 0.0f;
      }

      for (int c = 0; c < 3; c++) {
        float o = CHAN_OFF[c] * split;
        addBar(cx + sx * o, cy + sy * o, ux, uy,
               rodHalfLen, CHAN_COL[c], rodStrokes);
      }
    }
  }

  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
}
