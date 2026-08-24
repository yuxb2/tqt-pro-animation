/*
 * SpiralSphere — LilyGO T-QT Pro (ESP32-S3, GC9107 128x128)
 * ---------------------------------------------------------
 * Sphère composée de points disposés le long d'une SPIRALE UNIQUE
 * partant du pôle nord, en rotation libre (tumbling 2 axes),
 * colorée par une vague de teinte arc-en-ciel balayant l'espace.
 *
 * Libs : TFT_eSPI (version patchée LilyGO T-QT) — rien d'autre.
 * Core ESP32 : 2.0.x (2.0.14 max, comme pour GenerativeEye).
 *
 * Boutons :
 *   IO00 (BOOT) → change la loi de couleur (WAVE / LONGITUDE / SPIRAL)
 *   IO47 (KEY)  → gèle / relance la rotation
 *
 * Tous les réglages sont dans le bloc CONFIG ci-dessous. Voir README.md.
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include <math.h>

// ============================================================
//  CONFIG — tout se règle ici
// ============================================================

// ---- Géométrie de la spirale -------------------------------
#define SPIRAL_TURNS      36        // nb de tours du pôle nord au pôle sud
#define NUM_POINTS        2592      // = 2 * SPIRAL_TURNS^2  → maillage carré
#define SPHERE_RADIUS     60.0f     // rayon en pixels (écran = 128)

// ---- Rotation (rad/s) --------------------------------------
#define ROT_SPEED_X       0.30f
#define ROT_SPEED_Y       0.47f     // volontairement non commensurable

// ---- Couleur -----------------------------------------------
#define COLOR_MODE_START  0         // 0 = WAVE, 1 = LONGITUDE, 2 = SPIRAL
#define HUE_DRIFT         0.05f     // dérive globale de teinte (tours/s)
#define HUE_SPAN_BASE     0.50f     // étalement moyen de teinte sur la sphère
#define HUE_SPAN_AMP      0.45f     // amplitude de respiration (0 → pas de flash)
#define HUE_SPAN_PERIOD   9.0f      // période de la respiration (s)
#define WAVE_SPIN         0.90f     // vitesse de rotation de l'axe de la vague (rad/s)
#define SATURATION        1.00f     // 1.0 = couleurs pures ; <1 = pastel
#define SHADE_MIN         0.72f     // luminosité au bord du globe (1.0 = plat)
#define PALETTE_COSINE    0         // 1 = palette cosinus (bandes pâles/pastel)

// ---- Rendu --------------------------------------------------
#define TARGET_FPS        40
#define HUE_LUT_SIZE      96        // pas de la LUT de teinte
#define SHADE_LEVELS      4         // niveaux d'ombrage en profondeur

// ---- Boutons ------------------------------------------------
#define PIN_BTN_LEFT      0         // BOOT
#define PIN_BTN_RIGHT     47        // KEY
#define BTN_DEBOUNCE_MS   220

// ============================================================
//  Écran + sprite (double buffering)
// ============================================================
TFT_eSPI  tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

#define SCREEN_W 128
#define SCREEN_H 128
#define CENTER_X 64
#define CENTER_Y 64

// ============================================================
//  Données précalculées
// ============================================================
static float   ptX[NUM_POINTS];     // coordonnées unitaires sur la sphère
static float   ptY[NUM_POINTS];
static float   ptZ[NUM_POINTS];
static uint8_t idxLongitude[NUM_POINTS];  // index LUT pour le mode LONGITUDE
static uint8_t idxSpiral[NUM_POINTS];     // index LUT pour le mode SPIRAL

static uint16_t lut[SHADE_LEVELS][HUE_LUT_SIZE];

// ============================================================
//  État
// ============================================================
enum ColorMode { MODE_WAVE = 0, MODE_LONGITUDE, MODE_SPIRAL, MODE_COUNT };
static uint8_t colorMode  = COLOR_MODE_START;
static bool    frozen     = false;

static float angX = 0.0f, angY = 0.0f;   // angles de rotation cumulés
static float clockT = 0.0f;              // horloge couleur (s)

static uint32_t lastFrameUs = 0;
static uint32_t lastBtnMs   = 0;

// ============================================================
//  Couleur : conversions
// ============================================================
static uint16_t hsvToRgb565(float h, float s, float v) {
  h -= floorf(h);                 // ramène dans [0,1[
  float hh = h * 6.0f;
  int   i  = (int)hh;
  float f  = hh - i;
  float p = v * (1.0f - s);
  float q = v * (1.0f - s * f);
  float t = v * (1.0f - s * (1.0f - f));
  float r, g, b;
  switch (i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
  return tft.color565((uint8_t)(r * 255), (uint8_t)(g * 255), (uint8_t)(b * 255));
}

// Palette cosinus (style Inigo Quilez) : produit des bandes pastel naturelles
static uint16_t cosPalette565(float h, float v) {
  const float TAU = 6.2831853f;
  float r = 0.5f + 0.5f * cosf(TAU * (h + 0.00f));
  float g = 0.5f + 0.5f * cosf(TAU * (h + 0.33f));
  float b = 0.5f + 0.5f * cosf(TAU * (h + 0.67f));
  return tft.color565((uint8_t)(r * v * 255), (uint8_t)(g * v * 255), (uint8_t)(b * v * 255));
}

// ============================================================
//  Précalcul de la spirale sphérique
// ============================================================
static void buildSpiral() {
  const float TAU = 6.2831853f;
  for (int i = 0; i < NUM_POINTS; i++) {
    // theta : 0 au pôle nord → PI au pôle sud, pas constant
    //         (c'est ce pas constant qui densifie les points aux pôles
    //          et fait apparaître le trait continu du vortex)
    float theta = (float)M_PI * (i + 0.5f) / NUM_POINTS;
    float phi   = TAU * SPIRAL_TURNS * i / (float)NUM_POINTS;

    float st = sinf(theta), ct = cosf(theta);
    ptX[i] = st * cosf(phi);
    ptY[i] = ct;                 // axe polaire = Y (nord vers le haut)
    ptZ[i] = st * sinf(phi);

    float lon = phi / TAU;
    lon -= floorf(lon);
    idxLongitude[i] = (uint8_t)(lon * (HUE_LUT_SIZE - 1));
    idxSpiral[i]    = (uint8_t)((float)i / (NUM_POINTS - 1) * (HUE_LUT_SIZE - 1));
  }
}

// ============================================================
//  LUT de couleur, reconstruite à chaque frame
// ============================================================
static void buildLut(float hueBase, float span) {
  for (int k = 0; k < HUE_LUT_SIZE; k++) {
    float s = (float)k / (HUE_LUT_SIZE - 1);      // 0..1
    float h = hueBase + span * s;
    for (int d = 0; d < SHADE_LEVELS; d++) {
      float shade = SHADE_MIN + (1.0f - SHADE_MIN) * ((d + 0.5f) / SHADE_LEVELS);
#if PALETTE_COSINE
      lut[d][k] = cosPalette565(h, shade);
#else
      lut[d][k] = hsvToRgb565(h, SATURATION, shade);
#endif
    }
  }
}

// ============================================================
//  Setup
// ============================================================
void setup() {
  pinMode(PIN_BTN_LEFT,  INPUT_PULLUP);
  pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
#endif

  spr.setColorDepth(16);
  if (!spr.createSprite(SCREEN_W, SCREEN_H)) {
    tft.setTextColor(TFT_RED);
    tft.drawString("Sprite alloc failed", 4, 60, 2);
    while (true) delay(1000);
  }

  buildSpiral();
  lastFrameUs = micros();
}

// ============================================================
//  Boutons (debounce simple, pas de lib externe)
// ============================================================
static void pollButtons() {
  uint32_t now = millis();
  if (now - lastBtnMs < BTN_DEBOUNCE_MS) return;

  if (digitalRead(PIN_BTN_LEFT) == LOW) {
    colorMode = (colorMode + 1) % MODE_COUNT;
    lastBtnMs = now;
  } else if (digitalRead(PIN_BTN_RIGHT) == LOW) {
    frozen = !frozen;
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
  if (dt > 0.1f) dt = 0.1f;          // garde-fou après un hoquet

  pollButtons();

  if (!frozen) {
    angX += ROT_SPEED_X * dt;
    angY += ROT_SPEED_Y * dt;
  }
  clockT += dt;                       // la couleur continue même en pause

  // --- Matrice de rotation (X puis Y) ---
  float ca = cosf(angX), sa = sinf(angX);
  float cb = cosf(angY), sb = sinf(angY);

  // --- Paramètres couleur de la frame ---
  float span = HUE_SPAN_BASE
             + HUE_SPAN_AMP * sinf(6.2831853f * clockT / HUE_SPAN_PERIOD);
  float hueBase = HUE_DRIFT * clockT;
  buildLut(hueBase, span);

  // --- Axe de la vague de couleur : tourne dans l'espace écran,
  //     donc décorrélé de la rotation de la sphère ---
  float wa = WAVE_SPIN * clockT;
  float wdx = cosf(wa);
  float wdy = sinf(wa) * 0.7f;
  float wdz = sinf(0.61f * wa) * 0.9f;
  float wn  = 1.0f / sqrtf(wdx * wdx + wdy * wdy + wdz * wdz);
  wdx *= wn; wdy *= wn; wdz *= wn;

  const float hueScale = 0.5f * (HUE_LUT_SIZE - 1);

  spr.fillSprite(TFT_BLACK);

  for (int i = 0; i < NUM_POINTS; i++) {
    float x = ptX[i], y = ptY[i], z = ptZ[i];

    // rotation X
    float y1 = y * ca - z * sa;
    float z1 = y * sa + z * ca;
    // rotation Y
    float x2 = x * cb + z1 * sb;
    float z2 = -x * sb + z1 * cb;

    if (z2 <= 0.0f) continue;         // hémisphère arrière masqué

    int sx = CENTER_X + (int)(x2 * SPHERE_RADIUS);
    int sy = CENTER_Y - (int)(y1 * SPHERE_RADIUS);
    if ((unsigned)sx >= SCREEN_W || (unsigned)sy >= SCREEN_H) continue;

    // index de teinte selon le mode
    int k;
    if (colorMode == MODE_WAVE) {
      float w = x2 * wdx + y1 * wdy + z2 * wdz;   // -1 .. +1
      k = (int)((w + 1.0f) * hueScale);
      if (k < 0) k = 0; else if (k >= HUE_LUT_SIZE) k = HUE_LUT_SIZE - 1;
    } else if (colorMode == MODE_LONGITUDE) {
      k = idxLongitude[i];
    } else {
      k = idxSpiral[i];
    }

    int d = (int)(z2 * SHADE_LEVELS);
    if (d >= SHADE_LEVELS) d = SHADE_LEVELS - 1;

    spr.drawPixel(sx, sy, lut[d][k]);
  }

  spr.pushSprite(0, 0);

  // --- Cadence ---
  uint32_t budget = 1000000UL / TARGET_FPS;
  uint32_t spent  = micros() - nowUs;
  if (spent < budget) delayMicroseconds(budget - spent);
}
