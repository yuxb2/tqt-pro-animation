/*
 * Pixel Buddha - a serene face that constantly sheds pixels
 *
 * The face is built once at boot from arcs and strokes into a point list,
 * then redrawn intact every frame. Particles are spawned as *copies* of face
 * pixels and fly radially outward, accelerating and fading cyan -> magenta.
 * The face itself never erodes, which is what sells the effect: everything
 * streams away while the middle stays perfectly still.
 *
 * Emission is biased towards points far from the centre (rejection sampling
 * on distance), so the silhouette sheds much more than the features do.
 *
 * Buttons: left  = emission rate
 *          right = freeze / resume the shedding
 *
 * Hardware: LilyGo T-QT Pro (ESP32-S3, 128x128, TFT_eSPI Setup211)
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include "OneButton.h"

TFT_eSPI tft = TFT_eSPI();

#define SCREEN_W 128
#define SCREEN_H 128
#define CXF 64.0f      // face centre
#define CYF 70.0f
#define A_   30.0f     // skull half width
#define BT_  34.0f     // skull half height, top
#define BB_  40.0f     // skull half height, chin

static uint16_t fb[SCREEN_W * SCREEN_H];

// Face geometry, resolved once at boot
#define MAX_PTS 1500
static int16_t ptx[MAX_PTS], pty[MAX_PTS];
static int nPts = 0;

// Particles in flight
#define MAX_P 700
struct Particle { float x, y, vx, vy; int16_t life, max; };
static Particle ps[MAX_P];
static int nP = 0;

#define PIN_BTN_L 0
#define PIN_BTN_R 47
OneButton btnLeft(PIN_BTN_L, true, true);
OneButton btnRight(PIN_BTN_R, true, true);

#define COL_FACE 0x07FF   // cyan

int  emitRate = 14;       // particles spawned per frame
bool frozen = false;

void onLeftClick();
void onRightClick();
void buildFace();

// --- face construction (runs once) --------------------------------------

// A temporary bitmask keeps the point list free of duplicates where strokes
// cross. It is only needed while building, but lives in .bss so there is no
// allocation to fail.
static uint8_t maskBits[(SCREEN_W * SCREEN_H) / 8];

static inline bool markPixel(int x, int y) {
  if ((unsigned)x >= SCREEN_W || (unsigned)y >= SCREEN_H) return false;
  int idx = x + y * SCREEN_W;
  uint8_t bit = 1 << (idx & 7);
  if (maskBits[idx >> 3] & bit) return false;   // already have it
  maskBits[idx >> 3] |= bit;
  return true;
}

static void addPt(int x, int y) {
  if (nPts >= MAX_PTS) return;
  if (!markPixel(x, y)) return;
  ptx[nPts] = x;
  pty[nPts] = y;
  nPts++;
}

static void stamp(float fx, float fy, int th) {
  int x = (int)lroundf(fx), y = (int)lroundf(fy);
  addPt(x, y);
  if (th > 1) { addPt(x + 1, y); addPt(x, y + 1); }
  if (th > 2) { addPt(x - 1, y); addPt(x, y - 1); }
}

static void arcSeg(float cx, float cy, float rx, float ry,
                   float a0, float a1, int th) {
  float r = fmaxf(rx, ry);
  int n = (int)(r * fabsf(a1 - a0)) + 4;
  for (int i = 0; i <= n; i++) {
    float a = a0 + (a1 - a0) * i / n;
    stamp(cx + rx * cosf(a), cy + ry * sinf(a), th);
  }
}

static void lineSeg(float x0, float y0, float x1, float y1, int th) {
  float dx = x1 - x0, dy = y1 - y0;
  int n = (int)fmaxf(fabsf(dx), fabsf(dy)) + 1;
  for (int i = 0; i <= n; i++) {
    float t = (float)i / n;
    stamp(x0 + dx * t, y0 + dy * t, th);
  }
}

static void disc(float cx, float cy, float r) {
  for (int y = (int)(cy - r); y <= (int)(cy + r); y++) {
    for (int x = (int)(cx - r); x <= (int)(cx + r); x++) {
      float dx = x - cx, dy = y - cy;
      if (dx * dx + dy * dy <= r * r) addPt(x, y);
    }
  }
}

void buildFace() {
  memset(maskBits, 0, sizeof(maskBits));
  nPts = 0;

  // Skull contour. Top and bottom use different radii and the lower half
  // narrows as it descends, which is what gives a jaw and chin rather than
  // a plain egg.
  int n = 220;
  for (int i = 0; i < n; i++) {
    float th = 2.0f * PI * i / n, c = cosf(th), s = sinf(th);
    float ey = (c > 0) ? -BT_ * c : -BB_ * c;
    float ex = A_ * s;
    if (ey > 0) ex *= (1.0f - 0.24f * (ey / BB_));
    stamp(CXF + ex, CYF + ey, 2);
  }

  arcSeg(CXF, CYF - BT_ + 6.0f, 14.0f, 14.0f, PI * 1.10f, PI * 1.90f, 2); // ushnisha
  disc(CXF, CYF - BT_ - 11.0f, 2.2f);                                     // finial

  arcSeg(CXF, CYF - 14.0f, 23.0f, 16.0f, PI * 1.08f, PI * 1.92f, 2);      // hairline
  for (int i = 0; i < 9; i++) {                                           // curls
    float a = PI * (1.10f + 0.80f * i / 8.0f);
    disc(CXF + 23.5f * cosf(a), CYF - 14.0f + 16.5f * sinf(a), 1.4f);
  }

  arcSeg(CXF - A_ + 2.0f, CYF + 3.0f, 9.0f, 20.0f, PI * 0.45f, PI * 1.55f, 2);  // ears
  arcSeg(CXF + A_ - 2.0f, CYF + 3.0f, 9.0f, 20.0f, -PI * 0.55f, PI * 0.55f, 2);

  arcSeg(CXF - 13.0f, CYF - 4.0f, 13.0f, 9.0f, PI * 1.05f, PI * 1.95f, 2); // brows
  arcSeg(CXF + 13.0f, CYF - 4.0f, 13.0f, 9.0f, PI * 1.05f, PI * 1.95f, 2);

  arcSeg(CXF - 12.0f, CYF + 2.0f, 10.0f, 5.0f, PI * 1.10f, PI * 1.90f, 2); // lids
  arcSeg(CXF + 12.0f, CYF + 2.0f, 10.0f, 5.0f, PI * 1.10f, PI * 1.90f, 2);

  disc(CXF, CYF - 10.0f, 2.2f);                                           // urna

  lineSeg(CXF - 2.0f, CYF - 6.0f, CXF - 3.0f, CYF + 11.0f, 2);            // nose
  lineSeg(CXF + 2.0f, CYF - 6.0f, CXF + 3.0f, CYF + 11.0f, 2);
  arcSeg(CXF, CYF + 11.0f, 4.5f, 3.0f, 0.15f * PI, 0.85f * PI, 2);

  arcSeg(CXF, CYF + 19.0f, 9.0f, 4.0f, 0.12f * PI, 0.88f * PI, 2);        // mouth
}

// --- particles ----------------------------------------------------------

// Fresh particles are cyan-white, then cool through cyan and end magenta as
// they dim, so the outward drift reads as a colour gradient too.
static uint16_t ramp(float t) {
  float r, g, b;
  if (t > 0.55f) {
    float u = (t - 0.55f) / 0.45f;
    r = 0.55f * (1 - u) + 0.15f * u; g = 1.0f; b = 1.0f;
  } else {
    float u = t / 0.55f;
    r = 0.95f * (1 - u) + 0.55f * u;
    g = 0.10f * (1 - u) + 1.00f * u;
    b = 0.85f * (1 - u) + 1.00f * u;
  }
  float k = 0.25f + 0.75f * t;
  r *= k; g *= k; b *= k;
  return (((uint16_t)(r * 31)) << 11) | (((uint16_t)(g * 63)) << 5) | ((uint16_t)(b * 31));
}

static void emit() {
  for (int tries = 0; tries < 12; tries++) {
    int i = random(nPts);
    float dx = ptx[i] - CXF, dy = pty[i] - CYF;
    float d = sqrtf(dx * dx + dy * dy);
    // Accept in proportion to distance: the silhouette sheds, the nose does not
    if ((random(1000) / 1000.0f) > d / 46.0f) continue;
    if (nP >= MAX_P) return;

    float inv = 1.0f / (d < 1 ? 1 : d);
    float ux = dx * inv, uy = dy * inv;
    float wob = ((random(200) - 100) / 100.0f) * 0.28f;   // slight spread
    float ca = cosf(wob), sa = sinf(wob);
    float sp = 0.10f + (random(100) / 100.0f) * 0.13f;

    Particle *p = &ps[nP++];
    p->x = ptx[i]; p->y = pty[i];
    p->vx = (ux * ca - uy * sa) * sp;
    p->vy = (ux * sa + uy * ca) * sp;
    p->max = p->life = 60 + random(45);
    return;
  }
}

// --- sketch -------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== Pixel Buddha ===");

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true);

  randomSeed(analogRead(4) * millis());

  btnLeft.attachClick(onLeftClick);
  btnRight.attachClick(onRightClick);

  buildFace();
  Serial.printf("face points: %d\n", nPts);
}

void loop() {
  btnLeft.tick();
  btnRight.tick();

  if (!frozen) {
    for (int e = 0; e < emitRate; e++) emit();

    for (int i = 0; i < nP; i++) {
      Particle *p = &ps[i];
      p->vx *= 1.045f;          // accelerate as they escape
      p->vy *= 1.045f;
      p->x += p->vx;
      p->y += p->vy;
      p->life--;
      if (p->life <= 0 || p->x < -4 || p->x > SCREEN_W + 4 ||
          p->y < -4 || p->y > SCREEN_H + 4) {
        ps[i] = ps[--nP];       // swap-remove, order does not matter
        i--;
      }
    }
  }

  memset(fb, 0, sizeof(fb));

  // Particles first, with a dim one-step trail behind each
  for (int i = 0; i < nP; i++) {
    float t = (float)ps[i].life / ps[i].max;
    int x = (int)ps[i].x, y = (int)ps[i].y;
    int tx = (int)(ps[i].x - ps[i].vx), ty = (int)(ps[i].y - ps[i].vy);
    if ((unsigned)tx < SCREEN_W && (unsigned)ty < SCREEN_H) fb[tx + ty * SCREEN_W] = ramp(t * 0.45f);
    if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) fb[x + y * SCREEN_W] = ramp(t);
  }

  // The face on top, always whole
  for (int i = 0; i < nPts; i++) fb[ptx[i] + pty[i] * SCREEN_W] = COL_FACE;

  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
  delay(5);
}

void onLeftClick() {
  emitRate += 8;
  if (emitRate > 34) emitRate = 2;
  Serial.printf("emit = %d\n", emitRate);
}

void onRightClick() {
  frozen = !frozen;
}
