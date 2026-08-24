/*
 * RGB Moire - a fixed RGB target with a black ring system drifting across it
 *
 * Two sets of concentric circles, both with the same ring thickness:
 *
 *   - the colour set is nailed to the middle of the screen and cycles red,
 *     green, blue outwards. The bands butt straight up against each other, so
 *     this layer alone already covers every pixel - it is the background.
 *   - the black set is drawn on top, one ring in four, so three colour bands
 *     show through every gap. Its centre is not the screen centre: it rides a
 *     small epicycle, which is what makes the whole picture breathe.
 *
 * The epicycle is two arms of equal length turning at different rates. Their
 * sum sweeps slowly around the middle while its *length* runs all the way from
 * zero - both systems perfectly concentric, the image goes calm - out to twice
 * one arm, where the offset centres beat against each other and the screen
 * fills with interference. The second arm turns very slowly backwards, so the
 * sweep precesses instead of retracing the same path.
 *
 * Nothing here is antialiased on purpose. Wound down to one or two pixels per
 * band the two ring systems alias against each other and against the panel's
 * own subpixel grid, and the flat R/G/B bands break up into the shimmering
 * pastel moire that gives the thing its name.
 *
 * Renders into a static framebuffer pushed with pushImage() rather than a
 * TFT_eSprite: no heap allocation that can fail silently. Black is tested
 * first so a quarter of the pixels never pay for the second square root.
 *
 * Buttons: left  = zoom out (thinner rings, more of them)
 *          right = zoom in  (thicker rings, fewer of them)
 *
 * Hardware: LilyGo T-QT Pro (ESP32-S3, 128x128, TFT_eSPI Setup211)
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include "OneButton.h"

TFT_eSPI tft = TFT_eSPI();

#define SCREEN_W 128
#define SCREEN_H 128

// Half-pixel centre: with an even width the true middle falls between pixels,
// and putting it there keeps the innermost band round instead of square.
#define CENTER_X 63.5f
#define CENTER_Y 63.5f

// 32 KB framebuffer, statically allocated so it can never fail at runtime
static uint16_t fb[SCREEN_W * SCREEN_H];

#define PIN_BTN_L 0
#define PIN_BTN_R 47

OneButton btnLeft(PIN_BTN_L, true, true);
OneButton btnRight(PIN_BTN_R, true, true);

#define COL_BLACK 0x0000

// Pure channels, in the order they run outwards from the centre (RGB565)
static const uint16_t BAND_COL[3] = { 0xF800, 0x07E0, 0x001F };

// Ring thickness in pixels, shared by both systems. Roughly geometric, so
// every click feels like the same amount of zoom: 1.5 px puts about forty
// bands between the centre and the corner, 22 px leaves three.
static const float ZOOM_STEPS[] = {
  1.5f, 2.0f, 2.7f, 3.7f, 5.0f, 6.7f, 9.0f, 12.0f, 16.5f, 22.0f
};
#define ZOOM_COUNT (sizeof(ZOOM_STEPS) / sizeof(ZOOM_STEPS[0]))

// One black ring every DARK_PERIOD rings; the other three are left open and
// the colour layer underneath shows through.
#define DARK_PERIOD 4

// Epicycle carrying the black centre. Two arms of ARM pixels each, so the
// offset from the middle swings between 0 and 2 * ARM = half the screen.
// The difference of the rates sets how fast it breathes (about 8 s), their
// mean sets how fast the offset sweeps round (about 19 s).
#define ORBIT_ARM 16.0f
#define ORBIT_W1   0.72f
#define ORBIT_W2  -0.06f

// Animation state
float animTime = 0;
unsigned long lastFrameTime = 0;

// Look settings
int zoomIndex = 3;

// Heartbeat
unsigned long lastReport = 0;
uint32_t frameCount = 0;

void onLeftClick();
void onRightClick();
void drawFrame();

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== RGB Moire booting ===");

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
  Serial.println("RGB Moire initialized - entering loop");
}

void loop() {
  unsigned long now = millis();
  float dt = (now - lastFrameTime) / 1000.0f;
  lastFrameTime = now;
  animTime += dt;

  // Both rates are rational-ish but never in step; folding the time keeps the
  // float precise over long runs without a visible jump, since the fold is a
  // whole number of turns for neither arm alone but is close enough that the
  // seam lands inside a frame.
  if (animTime > 10000.0f) animTime -= 10000.0f;

  btnLeft.tick();
  btnRight.tick();

  drawFrame();
  frameCount++;

  // Heartbeat: if this keeps printing, the loop is alive and any remaining
  // problem is on the display side rather than a crash or a hang
  if (now - lastReport >= 2000) {
    Serial.printf("alive - %lu fps, thickness=%.1f px\n",
                  (unsigned long)(frameCount * 1000UL / (now - lastReport)),
                  ZOOM_STEPS[zoomIndex]);
    frameCount = 0;
    lastReport = now;
  }

  delay(5);
}

void onLeftClick() {
  if (zoomIndex > 0) zoomIndex--;
  Serial.printf("zoom out - thickness = %.1f px\n", ZOOM_STEPS[zoomIndex]);
}

void onRightClick() {
  if (zoomIndex < (int)ZOOM_COUNT - 1) zoomIndex++;
  Serial.printf("zoom in - thickness = %.1f px\n", ZOOM_STEPS[zoomIndex]);
}

// --- Frame --------------------------------------------------------------

void drawFrame() {
  const float invT = 1.0f / ZOOM_STEPS[zoomIndex];

  float a1 = ORBIT_W1 * animTime;
  float a2 = ORBIT_W2 * animTime;
  const float darkX = CENTER_X + ORBIT_ARM * (cosf(a1) + cosf(a2));
  const float darkY = CENTER_Y + ORBIT_ARM * (sinf(a1) + sinf(a2));

  uint16_t *p = fb;

  for (int y = 0; y < SCREEN_H; y++) {
    // Vertical legs are constant across the row, so square them once
    float cy = y - CENTER_Y;
    float cy2 = cy * cy;
    float dy = y - darkY;
    float dy2 = dy * dy;

    for (int x = 0; x < SCREEN_W; x++) {
      // Black layer first: where it wins, the colour underneath is never
      // sampled and the row skips a square root
      float dx = x - darkX;
      int darkBand = (int)(sqrtf(dx * dx + dy2) * invT);
      if ((darkBand & (DARK_PERIOD - 1)) == 0) {
        *p++ = COL_BLACK;
        continue;
      }

      float cx = x - CENTER_X;
      int colBand = (int)(sqrtf(cx * cx + cy2) * invT);
      *p++ = BAND_COL[colBand % 3];
    }
  }

  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
}
