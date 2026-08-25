/*
 * WorldRing — LilyGO T-QT Pro (ESP32-S3, GC9A01 128x128)
 * ------------------------------------------------------
 * Globe filaire blanc sur fond noir, continents pleins, anneau orbital,
 * et un anneau de texte fixe autour de l'image ("THE WORLD IS WATCHING",
 * répété deux fois pour boucler le cercle).
 *
 * Le texte se change sur une seule ligne : RING_TEXT ci-dessous. La taille
 * des lettres s'ajuste toute seule au nombre de caractères, donc une phrase
 * plus longue ou plus courte reste centrée et bouclée.
 *
 * Libs : TFT_eSPI (version LilyGO du dépôt, Setup211) — rien d'autre.
 *
 * Boutons :
 *   IO00 (BOOT) → style du globe : complet / filaire nu / continents seuls
 *   IO47 (KEY)  → fait tourner l'anneau de texte (ou le refige)
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include <math.h>
#include "vecfont.h"
#include "world_map.h"

// ============================================================
//  CONFIG — tout se règle ici
// ============================================================

// ---- Le texte de l'anneau ----------------------------------
// \x07 dessine le petit losange séparateur. Mets ce que tu veux :
// la taille des glyphes s'adapte pour que le tour soit exactement rempli.
#define RING_TEXT      "THE WORLD IS WATCHING\x07"
#define RING_REPEAT    2          // nb de fois que la phrase fait le tour
#define TEXT_R         57.0f      // rayon de la ligne médiane du texte
#define TEXT_PHASE_DEG 0.0f       // où commence la phrase ; 0 = à midi
#define TEXT_SPIN_ON   0          // 0 = anneau fixe, comme la référence
#define TEXT_SPIN_DPS  5.0f       // °/s quand il tourne (bouton droit)
#define TEXT_BOLD      1          // 1 = trait doublé (plus lisible en petit)
#define TEXT_FILL      0.86f      // part de l'espace par lettre réellement utilisée
#define TEXT_SCALE_MAX 2.2f

// ---- Le globe ----------------------------------------------
#define GLOBE_R        38.0f      // rayon en pixels
#define TILT_DEG       16.0f      // inclinaison : >0 = pôle nord vers nous
#define SPIN_DPS       22.0f      // °/s ; tour complet en ~16 s
#define MERIDIANS      18         // méridiens (tous les 20°)
#define PARALLEL_STEP  20         // parallèles, en degrés
#define SHOW_LIMB      1          // cercle de contour du globe

// ---- L'anneau orbital (celui du logo, pas celui du texte) ---
#define SHOW_ORBIT     1
#define ORBIT_R        49.0f
#define ORBIT_SQUASH   0.28f      // 0 = vu par la tranche, 1 = vu de face
#define ORBIT_ROLL0    -22.0f     // inclinaison de départ, en degrés
#define ORBIT_PREC_DPS 6.0f       // précession, °/s

// ---- Rendu --------------------------------------------------
#define TARGET_FPS     30
#define COL_LINE_LEVEL 105        // gris des lignes du globe (0..255)

// ---- Boutons ------------------------------------------------
#define PIN_BTN_LEFT   0          // BOOT
#define PIN_BTN_RIGHT  47         // KEY
#define BTN_DEBOUNCE_MS 220

// ============================================================
//  Écran + sprite
// ============================================================
TFT_eSPI    tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

#define SCREEN_W 128
#define SCREEN_H 128
#define CX 64
#define CY 64

static uint16_t COL_LAND;
static uint16_t COL_LINE;

// ============================================================
//  Table écran → (latitude, longitude)
//  L'inclinaison du globe ne change jamais et la rotation se fait autour
//  de l'axe des pôles : pour un pixel donné, la latitude est donc fixe et
//  la longitude n'est qu'un décalage. Toute la trigonométrie inverse est
//  faite une seule fois au démarrage, la boucle ne fait plus qu'un décalage
//  d'entier — d'où le remplissage des continents à plein régime.
// ============================================================
#define LUT_R  ((int)GLOBE_R + 1)
#define LUT_W  (2 * LUT_R + 1)
#define LUT_OUTSIDE 255

static uint8_t lutLat[LUT_W * LUT_W];
static uint8_t lutLon[LUT_W * LUT_W];

// ============================================================
//  État
// ============================================================
enum GlobeStyle { STYLE_FULL = 0, STYLE_WIRE, STYLE_LAND, STYLE_COUNT };
static uint8_t  globeStyle = STYLE_FULL;
static bool     textSpins  = TEXT_SPIN_ON;

static float    clockT     = 0.0f;
static float    textAngle  = TEXT_PHASE_DEG * 0.017453293f;
static uint32_t lastFrameUs = 0;
static uint32_t lastBtnMs   = 0;

static const float TILT = TILT_DEG * 0.017453293f;
static float CT, ST;

// ============================================================
//  Construction de la table
// ============================================================
static void buildLut() {
  for (int y = 0; y < LUT_W; y++) {
    for (int x = 0; x < LUT_W; x++) {
      int i = y * LUT_W + x;
      lutLat[i] = LUT_OUTSIDE;
      float dx = (float)(x - LUT_R) + 0.5f;
      float dy = (float)(y - LUT_R) + 0.5f;
      float rr = dx * dx + dy * dy;
      if (rr > GLOBE_R * GLOBE_R) continue;

      float xs =  dx / GLOBE_R;
      float ys = -dy / GLOBE_R;
      float zs = sqrtf(fmaxf(0.0f, 1.0f - xs * xs - ys * ys));

      // on défait l'inclinaison pour retomber dans le repère du globe
      float gx = xs;
      float gy = ys * CT + zs * ST;
      float gz = -ys * ST + zs * CT;
      if (gy > 1.0f) gy = 1.0f; else if (gy < -1.0f) gy = -1.0f;

      float lat = asinf(gy);
      float lon = atan2f(gz, gx);

      int li = (int)((1.5707963f - lat) / 3.1415927f * MAP_LAT);
      if (li < 0) li = 0; else if (li >= MAP_LAT) li = MAP_LAT - 1;
      int gi = (int)((lon + 3.1415927f) / 6.2831853f * MAP_LON);
      gi &= (MAP_LON - 1);

      lutLat[i] = (uint8_t)li;
      lutLon[i] = (uint8_t)gi;
    }
  }
}

// ============================================================
//  Projection : (x,y,z) du repère globe → écran
//  Renvoie true si le point est sur la face visible.
// ============================================================
static inline bool project(float x, float y, float z, float *sx, float *sy) {
  float ys = y * CT - z * ST;
  float zs = y * ST + z * CT;
  if (zs <= 0.0f) return false;
  *sx = CX + GLOBE_R * x;
  *sy = CY - GLOBE_R * ys;
  return true;
}

// ============================================================
//  Le maillage du globe
// ============================================================
static void drawGraticule(float spinRad) {
  // méridiens
  for (int m = 0; m < MERIDIANS; m++) {
    float lon = 6.2831853f * m / MERIDIANS + spinRad;
    float cl = cosf(lon), sl = sinf(lon);
    bool  have = false;
    float px = 0, py = 0;
    for (int k = 0; k <= 48; k++) {
      float lat = -1.5707963f + 3.1415927f * k / 48.0f;
      float cla = cosf(lat), sla = sinf(lat);
      float sx, sy;
      if (project(cla * cl, sla, cla * sl, &sx, &sy)) {
        if (have) spr.drawLine((int)px, (int)py, (int)sx, (int)sy, COL_LINE);
        px = sx; py = sy; have = true;
      } else {
        have = false;
      }
    }
  }
  // parallèles
  for (int lat = -90 + PARALLEL_STEP; lat <= 90 - PARALLEL_STEP; lat += PARALLEL_STEP) {
    float la = lat * 0.017453293f;
    float cla = cosf(la), sla = sinf(la);
    bool  have = false;
    float px = 0, py = 0;
    for (int k = 0; k <= 64; k++) {
      float lon = 6.2831853f * k / 64.0f + spinRad;
      float sx, sy;
      if (project(cla * cosf(lon), sla, cla * sinf(lon), &sx, &sy)) {
        if (have) spr.drawLine((int)px, (int)py, (int)sx, (int)sy, COL_LINE);
        px = sx; py = sy; have = true;
      } else {
        have = false;
      }
    }
  }
}

// ============================================================
//  Les continents, en pleins
// ============================================================
static void drawLand(float spinDeg) {
  int shift = (int)lroundf(spinDeg * (MAP_LON / 360.0f));
  uint8_t s = (uint8_t)(shift & (MAP_LON - 1));
  for (int y = 0; y < LUT_W; y++) {
    int py = CY - LUT_R + y;
    if ((unsigned)py >= SCREEN_H) continue;
    const uint8_t *rowLat = &lutLat[y * LUT_W];
    const uint8_t *rowLon = &lutLon[y * LUT_W];
    for (int x = 0; x < LUT_W; x++) {
      uint8_t la = rowLat[x];
      if (la == LUT_OUTSIDE) continue;
      if (landAt(la, (uint8_t)(rowLon[x] - s))) {
        spr.drawPixel(CX - LUT_R + x, py, COL_LAND);
      }
    }
  }
}

// ============================================================
//  L'anneau orbital
// ============================================================
static void drawOrbit(float t) {
  float roll = (ORBIT_ROLL0 + ORBIT_PREC_DPS * t) * 0.017453293f;
  float cr = cosf(roll), sr = sinf(roll);
  float cz = sqrtf(1.0f - ORBIT_SQUASH * ORBIT_SQUASH);
  const int N = 120;
  float px = 0, py = 0;
  bool  phid = true;
  for (int i = 0; i <= N; i++) {
    float u  = 6.2831853f * i / N;
    float cu = cosf(u), su = sinf(u);
    float x1 = cu;
    float y1 = su * ORBIT_SQUASH;
    float z1 = su * cz;
    float x2 = x1 * cr - y1 * sr;
    float y2 = x1 * sr + y1 * cr;
    float sx = CX + ORBIT_R * x2;
    float sy = CY - ORBIT_R * y2;
    float dx = sx - CX, dy = sy - CY;
    bool  hid = (z1 < 0.0f) && (dx * dx + dy * dy < GLOBE_R * GLOBE_R);
    if (i > 0 && !hid && !phid) {
      spr.drawLine((int)px, (int)py, (int)sx, (int)sy, COL_LAND);
      spr.drawLine((int)px, (int)py - 1, (int)sx, (int)sy - 1, COL_LAND);
    }
    px = sx; py = sy; phid = hid;
  }
}

// ============================================================
//  L'anneau de texte
//  Chaque lettre est posée sur le cercle, pieds vers le centre, et tracée
//  en segments : c'est ce qui permet de la faire tourner proprement.
// ============================================================
static void drawRingText(float angle0) {
  const char *base = RING_TEXT;
  int len = strlen(base);
  int total = len * RING_REPEAT;
  if (total == 0) return;

  float step  = 6.2831853f / total;
  float pitch = TEXT_R * step;
  float scale = pitch * TEXT_FILL / VF_W;
  if (scale < 1.0f) scale = 1.0f;
  if (scale > TEXT_SCALE_MAX) scale = TEXT_SCALE_MAX;

  for (int n = 0; n < total; n++) {
    uint8_t c = (uint8_t)base[n % len];
    uint16_t off = pgm_read_word(&VF_INDEX[c]);
    if (off == 0xFFFF) continue;

    float th = angle0 + (n + 0.5f) * step;
    float ct = cosf(th), st = sinf(th);
    float rx =  st, ry = -ct;      // vers l'extérieur
    float tx =  ct, ty =  st;      // sens de lecture

    const int8_t *p = &VF_STROKES[off];
    bool  have = false;
    float lx = 0, ly = 0;
    for (;;) {
      int8_t gx = (int8_t)pgm_read_byte(p++);
      if (gx == VF_END_GLYPH) break;
      if (gx == VF_END_POLY) { have = false; continue; }
      int8_t gy = (int8_t)pgm_read_byte(p++);

      float u = ((float)gx - 2.0f) * scale;
      float v = ((float)gy - 3.0f) * scale;
      float x = CX + rx * (TEXT_R - v) + tx * u;
      float y = CY + ry * (TEXT_R - v) + ty * u;
      if (have) {
        spr.drawLine((int)lx, (int)ly, (int)x, (int)y, COL_LAND);
#if TEXT_BOLD
        spr.drawLine((int)lx + 1, (int)ly, (int)x + 1, (int)y, COL_LAND);
#endif
      }
      lx = x; ly = y; have = true;
    }
  }
}

// ============================================================
//  Setup
// ============================================================
void setup() {
  pinMode(PIN_BTN_LEFT,  INPUT_PULLUP);
  pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);

  CT = cosf(TILT);
  ST = sinf(TILT);

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
#endif

  COL_LAND = TFT_WHITE;
  COL_LINE = tft.color565(COL_LINE_LEVEL, COL_LINE_LEVEL, COL_LINE_LEVEL);

  spr.setColorDepth(16);
  if (!spr.createSprite(SCREEN_W, SCREEN_H)) {
    tft.setTextColor(TFT_RED);
    tft.drawString("Sprite alloc failed", 4, 60, 2);
    while (true) delay(1000);
  }

  buildLut();
  lastFrameUs = micros();
}

// ============================================================
//  Boutons (debounce simple, pas de lib externe)
// ============================================================
static void pollButtons() {
  uint32_t now = millis();
  if (now - lastBtnMs < BTN_DEBOUNCE_MS) return;

  if (digitalRead(PIN_BTN_LEFT) == LOW) {
    globeStyle = (globeStyle + 1) % STYLE_COUNT;
    lastBtnMs = now;
  } else if (digitalRead(PIN_BTN_RIGHT) == LOW) {
    textSpins = !textSpins;
    lastBtnMs = now;
  }
}

// ============================================================
//  Boucle
// ============================================================
void loop() {
  uint32_t nowUs = micros();
  float dt = (nowUs - lastFrameUs) * 1e-6f;
  lastFrameUs = nowUs;
  if (dt > 0.1f) dt = 0.1f;            // garde-fou après un hoquet
  clockT += dt;

  pollButtons();

  // Le texte tourne doucement quand on le demande ; sinon il reste posé.
  float spinRate = textSpins ? TEXT_SPIN_DPS : 0.0f;
  textAngle += spinRate * 0.017453293f * dt;
  if (textAngle > 6.2831853f) textAngle -= 6.2831853f;

  float spinDeg = SPIN_DPS * clockT;
  spinDeg -= 360.0f * floorf(spinDeg / 360.0f);
  float spinRad = spinDeg * 0.017453293f;

  spr.fillSprite(TFT_BLACK);

  if (globeStyle != STYLE_LAND) drawGraticule(spinRad);
  if (globeStyle != STYLE_WIRE) drawLand(spinDeg);
#if SHOW_LIMB
  if (globeStyle != STYLE_LAND) spr.drawCircle(CX, CY, (int)GLOBE_R, COL_LINE);
#endif
#if SHOW_ORBIT
  drawOrbit(clockT);
#endif
  drawRingText(textAngle);

  spr.pushSprite(0, 0);

  uint32_t budget = 1000000UL / TARGET_FPS;
  uint32_t spent  = micros() - nowUs;
  if (spent < budget) delayMicroseconds(budget - spent);
}
