/*
 * RGB Waves - a sheet of horizontal lines rolling under three misregistered
 * printings of itself
 *
 * The whole picture is one stack of horizontal lines and one displacement
 * field. Every line samples the same field, but a line further down the screen
 * reads it at a later phase, so the crests do not stand vertically above one
 * another: they lean, and the stack reads as a single surface caught at an
 * angle rather than as a pile of unrelated ripples.
 *
 * The field is three travelling sines added together. Two of them are the
 * waves proper, one running right and one running left, and their weights
 * cross-fade against each other on a very slow envelope - the sheet leans one
 * way for the better part of a minute, flattens as the two come level, then
 * leans back. That is the alternation. The third is a long, lazy swell almost
 * as wide as the panel with a full turn of phase from the top row to the
 * bottom; it is what bends the leaning crests into the sweeping curves instead
 * of leaving them as straight diagonal corrugations.
 *
 * The stack is drawn three times, once per channel, and the three printings do
 * not line up. Four separate things pull them apart, and they matter in this
 * order:
 *
 *   - a shift in depth. Each channel reads the field as though its line sat a
 *     little higher or lower in the stack than it really does. Because the
 *     field carries most of a full turn of phase from the top line to the
 *     bottom, this warps the three printings differently rather than merely
 *     moving them - which is the whole point. The gap between the copies is
 *     three or four pixels in some places and nothing at all in others, so
 *     part of a line comes out split into red, green and blue while the rest
 *     of it, or the line below it, closes back up into white.
 *   - a few percent of amplitude, so the copies separate most at the crests
 *     and troughs and least at the nodes - the opposite pattern to the shift
 *     above, which is why the two together leave so few dead stretches.
 *   - a time lag: red reads the field slightly in the past and blue slightly
 *     in the future. For a travelling wave a shift in time is a shift in
 *     space, so this slides the copies horizontally past each other. On a
 *     line that happens to be running flat it does nothing at all; where the
 *     surface is steep it opens the fringe out.
 *   - a flat vertical offset, one channel up and one down. Small, and its only
 *     job is to keep a little colour alive where the other three agree.
 *
 * Where all three copies land on the same pixel the additive blend gives white
 * - a line that runs white along part of its length and splits into red, cyan,
 * blue further along is the normal state of things, not an accident. Only
 * three colours are ever written; every other colour on the panel is overlap.
 *
 * Sampling the field with sinf() at every pixel of every line of every channel
 * would be about twenty-five thousand calls a frame. Instead each component is
 * seeded once per line and then stepped along the row by a rotation - the
 * standard sin/cos recurrence - which is six multiplies instead of a library
 * call. Over 128 steps the drift is far below a pixel.
 *
 * Every period in the sketch divides LOOP_PERIOD, so the animation closes on
 * itself exactly and the folding of animTime is seamless. Twelve minutes is
 * long enough that the repeat is not something you notice.
 *
 * Renders into a static framebuffer pushed with pushImage() rather than a
 * TFT_eSprite: no heap allocation that can fail silently, and the additive
 * blend is a direct array access instead of readPixel/drawPixel per pixel.
 *
 * Buttons: left  = line density (8 / 12 / 16 / 22 / 30 lines)
 *          right = misregistration (tight / normal / wide / torn apart).
 *                  At the last setting the three sheets come fully apart and
 *                  weave through each other as separate coloured surfaces.
 *
 * Hardware: LilyGo T-QT Pro (ESP32-S3, 128x128, TFT_eSPI Setup211)
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include "OneButton.h"

TFT_eSPI tft = TFT_eSPI();

#define SCREEN_W 128
#define SCREEN_H 128
#define CENTER_Y 64.0f

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

// Channel order and which way each copy is pushed: red into the past, green on
// the nominal sheet, blue into the future.
static const float    CHAN_OFF[3] = { -1.0f, 0.0f, 1.0f };
static const uint16_t CHAN_COL[3] = { COL_RED, COL_GREEN, COL_BLUE };

// --- Line stack ----------------------------------------------------------

// Lines across the screen. More lines means less room between them, so the
// amplitude is tied to the spacing below rather than fixed.
static const int ROW_LEVELS[] = { 8, 12, 16, 22, 30 };
#define ROW_LEVEL_COUNT (sizeof(ROW_LEVELS) / sizeof(ROW_LEVELS[0]))

// Swing of a line as a multiple of the gap to its neighbour. Above one the
// lines reach into each other's lanes, which is what makes the sheet read as a
// surface with depth instead of a set of independent ripples; the row-phase
// gradient is gentle enough that they still never actually cross.
#define AMP_FRAC 1.35f
#define AMP_MIN  4.0f
#define AMP_MAX  15.0f

// --- Displacement field --------------------------------------------------

// Three travelling sines. Wavelength in pixels, temporal period in seconds,
// direction (-1 runs right, +1 runs left), and the phase accumulated from the
// top line to the bottom one in radians.
//
// The first two are the waves that alternate. The third is the long swell: not
// quite one wavelength across the panel, slow, and carrying most of a full
// turn down the stack, which is where the curvature comes from.
static const float WAVE_LEN[3]   = {  97.0f,  63.0f, 181.0f };
static const float WAVE_PER[3]   = {  18.0f,  24.0f,  45.0f };
static const float WAVE_DIR[3]   = {  -1.0f,   1.0f,  -1.0f };
static const float WAVE_ROWPH[3] = {   2.1f,  -3.4f,   5.2f };

// Weights. The first two cross-fade on the alternation envelope and the third
// is constant; the three sum to at most 1, so the sheet never swings past the
// amplitude it was given.
#define ALT_PERIOD  80.0f
#define WGT_MID     0.36f
#define WGT_SWING   0.22f
#define WGT_SWELL   0.28f

// Slow motion of the stack as a whole, on top of the waves: the spacing opens
// and closes, and the whole sheet drifts up and down. Both are smooth and
// wrap-free, so no line ever has to jump from one edge to the other.
#define BREATHE_PERIOD 90.0f
#define BREATHE_AMP     0.085f
#define SWAY_PERIOD    144.0f
#define SWAY_AMP         4.5f

// --- Channel misregistration ---------------------------------------------

// One knob, cycled with the right button, driving all four mechanisms at once
// so the three printings always come apart as a piece.
//
// The weighting between them is what decides whether the panel reads as a
// white grid with coloured edges or as three coloured grids. Most of the
// separation has to come from ROWPH and TILT, because those two vary across
// the picture; leaning on SHIFT instead would print every line as the same
// flat red-green-blue sandwich everywhere and lose the white entirely.
static const float MIS_LEVEL[4] = { 0.45f, 1.00f, 1.75f, 2.90f };
#define MIS_LEVEL_COUNT 4

#define MIS_ROWPH 0.16f   // fraction of the stack each copy is read up/down by
#define MIS_TILT  0.12f   // fractional amplitude difference per unit
#define MIS_LAG   0.80f   // seconds of time offset per unit
#define MIS_SHIFT 0.90f   // pixels of flat vertical offset per unit

// --- Loop period ---------------------------------------------------------

// 720 s. Every period above divides it exactly - 18, 24, 45, 80, 90, 144 - so
// folding animTime here returns the sketch to the state it started in with no
// seam at all.
#define LOOP_PERIOD 720.0f

// Derived once in setup()
static float waveK[3];      // radians per pixel
static float waveOmega[3];  // radians per second, sign included
static float waveSK[3];     // sin/cos of waveK, for the along-row recurrence
static float waveCK[3];

// Animation state
float animTime = 0;
unsigned long lastFrameTime = 0;

// Look settings
int rowIndex = 3;
int misIndex = 1;

// Heartbeat
unsigned long lastReport = 0;
uint32_t frameCount = 0;

void onLeftClick();
void onRightClick();
void buildField();
void drawWaves();

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== RGB Waves booting ===");

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(COL_BLACK);

  // Byte order for pushImage of an in-memory RGB565 buffer.
  // If red and blue come out swapped, change this to false.
  tft.setSwapBytes(true);

  btnLeft.attachClick(onLeftClick);
  btnRight.attachClick(onRightClick);

  buildField();

  lastFrameTime = millis();
  lastReport = millis();

  Serial.printf("lines: %d, alternation %.0f s, loop %.0f s\n",
                ROW_LEVELS[rowIndex], ALT_PERIOD, LOOP_PERIOD);
  Serial.printf("free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());
  Serial.println("RGB Waves initialized - entering loop");
}

void loop() {
  unsigned long now = millis();
  float dt = (now - lastFrameTime) / 1000.0f;
  lastFrameTime = now;
  animTime += dt;
  if (animTime >= LOOP_PERIOD) animTime -= LOOP_PERIOD;

  btnLeft.tick();
  btnRight.tick();

  drawWaves();
  frameCount++;

  // Heartbeat: if this keeps printing, the loop is alive and any remaining
  // problem is on the display side rather than a crash or a hang
  if (now - lastReport >= 2000) {
    Serial.printf("alive - %lu fps, lines=%d mis=%.2f loop=%.0f%%\n",
                  (unsigned long)(frameCount * 1000UL / (now - lastReport)),
                  ROW_LEVELS[rowIndex], MIS_LEVEL[misIndex],
                  100.0f * animTime / LOOP_PERIOD);
    frameCount = 0;
    lastReport = now;
  }

  delay(5);
}

void onLeftClick() {
  rowIndex = (rowIndex + 1) % (int)ROW_LEVEL_COUNT;
  Serial.printf("lines = %d\n", ROW_LEVELS[rowIndex]);
}

void onRightClick() {
  misIndex = (misIndex + 1) % MIS_LEVEL_COUNT;
  Serial.printf("misregistration = %.2f (depth %.2f, tilt %.0f%%, lag %.2f s)\n",
                MIS_LEVEL[misIndex],
                MIS_LEVEL[misIndex] * MIS_ROWPH,
                100.0f * MIS_LEVEL[misIndex] * MIS_TILT,
                MIS_LEVEL[misIndex] * MIS_LAG);
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
  if ((unsigned)x >= SCREEN_W || (unsigned)y >= SCREEN_H) return;
  uint16_t *p = &fb[x + y * SCREEN_W];
  *p = addColors(*p, c);
}

// One column of a line, from where the previous sample sat to where this one
// does. Plotting the samples alone would comb the line into dots wherever the
// surface is steeper than a pixel per pixel; filling the gap in the new column
// keeps it continuous without needing a general line routine.
static inline void addVSpan(int x, int y0, int y1, uint16_t col) {
  if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
  if (y1 < 0 || y0 >= SCREEN_H) return;
  if (y0 < 0) y0 = 0;
  if (y1 >= SCREEN_H) y1 = SCREEN_H - 1;

  uint16_t *p = &fb[x + y0 * SCREEN_W];
  for (int y = y0; y <= y1; y++) {
    *p = addColors(*p, col);
    p += SCREEN_W;
  }
}

// --- Field --------------------------------------------------------------

void buildField() {
  for (int i = 0; i < 3; i++) {
    waveK[i] = TWO_PI / WAVE_LEN[i];
    waveOmega[i] = WAVE_DIR[i] * TWO_PI / WAVE_PER[i];

    // Rotation for one pixel of x, used to walk the sine along a row
    waveSK[i] = sinf(waveK[i]);
    waveCK[i] = cosf(waveK[i]);
  }
}

// --- Drawing ------------------------------------------------------------

void drawWaves() {
  memset(fb, 0, sizeof(fb));

  const int rows = ROW_LEVELS[rowIndex];
  const float spacing = (float)SCREEN_H / rows;

  float amp = spacing * AMP_FRAC;
  if (amp < AMP_MIN) amp = AMP_MIN;
  if (amp > AMP_MAX) amp = AMP_MAX;

  const float mis = MIS_LEVEL[misIndex];
  const float rowph = mis * MIS_ROWPH;
  const float tilt  = mis * MIS_TILT;
  const float lag   = mis * MIS_LAG;
  const float shift = mis * MIS_SHIFT;

  // Alternation: the two travelling waves trade strength, the swell holds
  const float env = sinf(TWO_PI * animTime / ALT_PERIOD);
  const float wgt[3] = { WGT_MID + WGT_SWING * env,
                         WGT_MID - WGT_SWING * env,
                         WGT_SWELL };

  // Motion of the stack itself
  const float scale = 1.0f + BREATHE_AMP * sinf(TWO_PI * animTime / BREATHE_PERIOD);
  const float sway  = SWAY_AMP * sinf(TWO_PI * animTime / SWAY_PERIOD);

  for (int j = 0; j < rows; j++) {
    // Position down the stack, 0 at the top line and 1 at the bottom one. The
    // field is indexed by this rather than by pixels, so the pattern keeps its
    // shape when the density changes and only gets finer.
    const float v = (rows > 1) ? (float)j / (rows - 1) : 0.5f;
    const float baseY = CENTER_Y + ((j + 0.5f) * spacing - CENTER_Y) * scale + sway;

    for (int c = 0; c < 3; c++) {
      const float o = CHAN_OFF[c];
      const float tc = animTime + o * lag;
      const float a  = amp * (1.0f + o * tilt);
      const float y0 = baseY + o * shift;

      // Seed the three oscillators at x = 0 for this line and channel. The
      // channel offset goes into v as well as into the time, so this copy is
      // reading the surface from a slightly different depth in the stack.
      const float vc = v + o * rowph;

      float s[3], k[3];
      for (int i = 0; i < 3; i++) {
        float ph = WAVE_ROWPH[i] * vc + waveOmega[i] * tc;
        s[i] = sinf(ph);
        k[i] = cosf(ph);
      }

      int prevY = 0;
      for (int x = 0; x < SCREEN_W; x++) {
        float w = wgt[0] * s[0] + wgt[1] * s[1] + wgt[2] * s[2];
        int y = (int)lroundf(y0 + a * w);

        if (x == 0) addPixel(x, y, CHAN_COL[c]);
        else        addVSpan(x, prevY, y, CHAN_COL[c]);
        prevY = y;

        // Advance each component by one pixel of x
        for (int i = 0; i < 3; i++) {
          float ns = s[i] * waveCK[i] + k[i] * waveSK[i];
          k[i] = k[i] * waveCK[i] - s[i] * waveSK[i];
          s[i] = ns;
        }
      }
    }
  }

  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
}
