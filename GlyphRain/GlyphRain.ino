/*
 * Glyph Rain - falling columns of glyphs with bright heads and fading tails
 *
 * No framebuffer here: only the handful of cells that actually change get
 * redrawn each frame, which is far cheaper than pushing 32 KB and leaves the
 * rest of the screen untouched, so there is nothing to flicker.
 *
 * Glyphs are not stored. Each cell's character comes from a hash of its
 * column, row and the column's generation counter, so redrawing a cell in a
 * dimmer shade always reproduces the same character - the trail fades
 * without a buffer to remember it.
 *
 * Buttons: left  = fall speed
 *          right = colour scheme (green / mixed / magenta)
 *
 * Hardware: LilyGo T-QT Pro (ESP32-S3, 128x128, TFT_eSPI Setup211)
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include "OneButton.h"

TFT_eSPI tft = TFT_eSPI();

#define SCREEN_W 128
#define SCREEN_H 128
#define CELL_W 6          // GLCD font 1 is 6x8
#define CELL_H 8
#define COLS  (SCREEN_W / CELL_W)   // 21
#define ROWS  (SCREEN_H / CELL_H)   // 16

#define PIN_BTN_L 0
#define PIN_BTN_R 47
OneButton btnLeft(PIN_BTN_L, true, true);
OneButton btnRight(PIN_BTN_R, true, true);

static const char glyphs[] =
  "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ<>[]{}/\\|=+*#$%&@!?";
static const int nGlyphs = sizeof(glyphs) - 1;

// Four fading shades per hue; the head is always white
static const uint16_t shades[3][4] = {
  { 0x07E0, 0x05C0, 0x0380, 0x0180 },   // green
  { 0x07FF, 0x05DF, 0x039B, 0x0177 },   // cyan
  { 0xF81F, 0xB817, 0x780F, 0x3807 }    // magenta
};

struct Column {
  float   head;      // fractional row of the leading glyph
  float   speed;     // rows per second
  int16_t lastRow;   // last integer row drawn, to detect a step
  int8_t  trail;
  uint8_t hue;
  uint16_t gen;      // bumped on each pass so glyphs differ every time
};
static Column cols[COLS];

int scheme = 1;            // 0 = all green, 1 = mixed, 2 = all magenta
float speedMul = 1.0f;
unsigned long lastFrameTime = 0;

void onLeftClick();
void onRightClick();

// Deterministic glyph for a cell: lets a trail cell be redrawn dimmer
// later without storing what was there
static inline char glyphAt(int col, int row, uint16_t gen) {
  uint32_t h = (uint32_t)col * 73856093u ^ (uint32_t)(row + 64) * 19349663u
             ^ (uint32_t)gen * 83492791u;
  h ^= h >> 13;
  return glyphs[h % nGlyphs];
}

static uint8_t pickHue() {
  if (scheme == 0) return 0;
  if (scheme == 2) return 2;
  int r = random(100);
  return (r < 70) ? 0 : (r < 88 ? 1 : 2);
}

static void resetColumn(int c) {
  cols[c].head = -(float)random(ROWS * 2);   // start off the top of the screen
  cols[c].speed = 5.0f + random(100) / 100.0f * 16.0f;
  cols[c].trail = 5 + random(10);
  cols[c].hue = pickHue();
  cols[c].gen++;
  cols[c].lastRow = (int16_t)floorf(cols[c].head) - 1;
}

static inline void drawCell(int c, int r, uint16_t colr, uint16_t gen) {
  if (r < 0 || r >= ROWS) return;
  tft.drawChar(c * CELL_W, r * CELL_H, glyphAt(c, r, gen), colr, TFT_BLACK, 1);
}

static inline void clearCell(int c, int r) {
  if (r < 0 || r >= ROWS) return;
  tft.fillRect(c * CELL_W, r * CELL_H, CELL_W, CELL_H, TFT_BLACK);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== Glyph Rain ===");

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  randomSeed(analogRead(4) * millis());

  for (int c = 0; c < COLS; c++) {
    cols[c].gen = random(1000);
    resetColumn(c);
  }

  btnLeft.attachClick(onLeftClick);
  btnRight.attachClick(onRightClick);
  lastFrameTime = millis();
}

void loop() {
  unsigned long now = millis();
  float dt = (now - lastFrameTime) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;              // never leap after a stall
  lastFrameTime = now;

  btnLeft.tick();
  btnRight.tick();

  for (int c = 0; c < COLS; c++) {
    Column &col = cols[c];
    col.head += col.speed * speedMul * dt;
    int r = (int)floorf(col.head);
    if (r == col.lastRow) continue;      // nothing to redraw in this column
    col.lastRow = r;

    const uint16_t *sh = shades[col.hue];
    drawCell(c, r, TFT_WHITE, col.gen);  // leading glyph
    drawCell(c, r - 1, sh[0], col.gen);  // and the fade behind it
    drawCell(c, r - 2, sh[1], col.gen);
    drawCell(c, r - 4, sh[2], col.gen);
    drawCell(c, r - 7, sh[3], col.gen);
    clearCell(c, r - col.trail);

    if (r - col.trail > ROWS) resetColumn(c);
  }

  // A few glyphs mutate in place each frame, so the tails keep shifting
  for (int i = 0; i < 3; i++) {
    int c = random(COLS);
    Column &col = cols[c];
    int r = (int)floorf(col.head) - 1 - random(col.trail > 1 ? col.trail - 1 : 1);
    if (r < 0 || r >= ROWS) continue;
    uint32_t h = (uint32_t)random(65536);
    tft.drawChar(c * CELL_W, r * CELL_H, glyphs[h % nGlyphs],
                 shades[col.hue][1], TFT_BLACK, 1);
  }

  delay(12);
}

void onLeftClick() {
  speedMul += 0.6f;
  if (speedMul > 2.6f) speedMul = 0.4f;
  Serial.printf("speed = %.1f\n", speedMul);
}

void onRightClick() {
  scheme = (scheme + 1) % 3;
  Serial.printf("scheme = %d\n", scheme);
  for (int c = 0; c < COLS; c++) cols[c].hue = pickHue();
}
