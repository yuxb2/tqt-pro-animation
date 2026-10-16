/*
 * RainbowEye — LilyGO T-QT Pro (ESP32-S3, écran 128x128)
 * ------------------------------------------------------
 * L'œil arc-en-ciel du thème « Couleurs » des Yeux — celui de gauche, sur
 * le T-Display —, posé seul sur l'écran du T-QT.
 *
 * Autour du blanc de l'œil, seize amandes de plus en plus grandes : vert,
 * bleu, violets, roses, orangés, jaunes. Chacune est un peu plus tournée que
 * la précédente, et c'est ce décalage qui fait le tourbillon de l'affiche.
 * Les couches pivotent doucement : la torsion respire, et une onde part du
 * centre vers l'extérieur.
 *
 * Dans le blanc, seules bougent la sphère rouge et sa pupille noire. La
 * pupille glisse un peu dans le rouge du côté où l'œil regarde. Le regard
 * est celui de l'œil bleu (BlueEye) : la boucle relevée image par image sur
 * le gif, qui y dure 4,27 s et qu'on ralentit ici à 5,05 s.
 *
 * Les amandes ont des bords paraboliques, |v| < H (1 - (u/W)²) : il ne faut
 * pas de racine pour savoir si un pixel est dedans. Elles sont emboîtées,
 * donc on part de la couche du pixel précédent — presque toujours la même —
 * et on descend ou on monte : deux tests par pixel en moyenne.
 *
 * Toutes les tailles sont en pixels de l'affiche d'origine, où le blanc de
 * l'œil fait 2 × 295 px. CO_SCALE dit combien de pixels d'écran ça fait.
 *
 * Pas de boutons : tout se règle dans les constantes ci-dessous, et dans
 * preview.html, qui exécute le même code dans le navigateur.
 *
 * Libs : TFT_eSPI (version LilyGO du dépôt, Setup211) — rien d'autre.
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include <math.h>

// ============================================================
//  CONFIG — tout se règle ici
// ============================================================

// ---- Le cadrage ----------------------------------------------
#define CO_SCALE      0.125f   // pixels d'écran par pixel de l'affiche

// ---- Le tourbillon -------------------------------------------
#define CO_TWIST     -0.029f   // torsion d'une couche à la suivante, en tours (−10,4°)
#define CO_BREATH     0.30f    // la torsion respire de ± tant
#define CO_BREATH_MS  8000     // durée d'une respiration, ms
#define CO_WAVE       0.006f   // l'onde qui part du centre, en tours

// ---- La sphère -----------------------------------------------
#define CO_RED_R      133.0f   // sphère rouge, px de l'affiche
#define CO_PUP_R       92.0f   // pupille noire
#define CO_LOOK_X     109.0f   // course de la sphère quand l'œil regarde de côté
#define CO_LOOK_Y      31.0f   // et quand il regarde en haut ou en bas
#define CO_LOOP_MS    5050     // une boucle du regard (4267 dans le gif bleu), ms

#define TARGET_FPS    40

// Les couleurs des couches, du blanc de l'œil vers l'extérieur, relevées
// sur l'affiche. La dernière remplit tout ce qui dépasse.
#define CO_LAYERS 16
static const uint8_t CO_RGB[CO_LAYERS][3] = {
  { 240, 221, 199 }, {  99, 180, 130 }, {  36, 127, 175 }, {  96,  66, 137 },
  { 114,  92, 148 }, { 140,  90, 122 }, { 172,  82, 100 }, { 200,  82,  72 },
  { 226,  88,  42 }, { 243, 126,  22 }, { 242, 152,  14 }, { 246, 192,  32 },
  { 236, 165,  45 }, { 222, 112,  50 }, { 228,  86,  66 }, { 236,  52,  30 } };
static const uint16_t CO_RED = 0xF123, CO_BLACK = 0x0841;   // (242,37,24), (10,10,10)

// ============================================================
//  Écran
// ============================================================
TFT_eSPI tft = TFT_eSPI();

#define SCREEN_W 128
#define SCREEN_H 128
static uint16_t fb[SCREEN_W * SCREEN_H];
static uint16_t coCol[CO_LAYERS + 1];

// ============================================================
//  Petits outils
// ============================================================
static float sinLut[1024];
static inline float sinTurn(float turns) { return sinLut[(int)(turns * 1024.0f) & 1023]; }   // sin(2π·turns)
// Phase 0..1 d'un cycle de periodMs : exacte, sans dérive
static inline float clockPhase(uint32_t clock, uint32_t periodMs) { return (clock % periodMs) / (float)periodMs; }
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
// Mélange de deux couleurs 565 : t = 0 (a) .. 256 (b)
static inline uint16_t mix565(uint16_t a, uint16_t b, int t) {
  if (t <= 0) return a;
  if (t >= 256) return b;
  int ra = a >> 11, ga = (a >> 5) & 63, ba = a & 31;
  int rb = b >> 11, gb = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)(((ra + (((rb - ra) * t) >> 8)) << 11) | ((ga + (((gb - ga) * t) >> 8)) << 5) | (ba + (((bb - ba) * t) >> 8)));
}
// Couverture 0..256 d'un bord : d = distance signée au bord, en pixels (positive dedans)
static inline int cover(float d) { return d >= 0.5f ? 256 : (d <= -0.5f ? 0 : (int)((d + 0.5f) * 256.0f)); }
static inline float smooth01(float p) { p = p < 0 ? 0 : (p > 1 ? 1 : p); return p * p * (3 - 2 * p); }
static inline float lerpf(float a, float b, float p) { return a + (b - a) * p; }

// ============================================================
//  Le regard : la boucle du gif bleu
// ============================================================
// Position de la pupille à l'instant clock, en px du gif (500 × 500, centre
// au milieu) : en haut à gauche, glisse en haut à droite, en bas, retour au
// centre, puis saut en haut à gauche.
static void coGaze(uint32_t clock, float &gx, float &gy) {
  float t = (clock % CO_LOOP_MS) * (4.267f / CO_LOOP_MS);   // secondes du gif
  const float ULx = -42, ULy = -49, URx = 66, URy = -49, DNx = 0, DNy = 45;
  if (t < 0.10f)      { gx = ULx; gy = ULy; }
  else if (t < 0.60f) { float q = 1 - (t - 0.10f) / 0.50f; gx = lerpf(ULx, URx, 1 - q * q); gy = ULy; }
  else if (t < 1.08f) { gx = URx; gy = URy; }
  else if (t < 1.22f) { float p = smooth01((t - 1.08f) / 0.14f); gx = lerpf(DNx + 14, DNx, p); gy = lerpf(DNy - 8, DNy, p); }
  else if (t < 1.53f) { gx = DNx; gy = DNy; }
  else if (t < 1.80f) { float p = smooth01((t - 1.53f) / 0.27f); gx = lerpf(DNx, 0, p); gy = lerpf(DNy, 0, p); }
  else if (t < 3.40f) { gx = 0; gy = 0; }
  else if (t < 3.60f) { float p = smooth01((t - 3.40f) / 0.20f); gx = lerpf(0, ULx, p); gy = lerpf(0, ULy, p); }
  else                { gx = ULx; gy = ULy; }
}

// Distance signée (approchée, px de l'affiche) au bord d'une amande
// parabolique de demi-hauteur H ; négative dedans.
static inline float coLens(float u, float v, float H, float invW2) {
  float q = 1.0f - u * u * invW2;                     // 1 au centre, 0 aux coins
  float f = (v < 0 ? -v : v) - H * q;
  float gr = 2.0f * H * (u < 0 ? -u : u) * invW2;     // pente du bord
  return f / (gr > 1.0f ? gr : 1.0f);
}

// ============================================================
//  Une image
// ============================================================
static void drawRainbow(uint32_t clock) {
  // Chaque couche : sa taille, et sa rotation (torsion qui respire, onde
  // qui part du centre).
  float lc[CO_LAYERS], ls[CO_LAYERS], lh[CO_LAYERS], liw[CO_LAYERS];
  const float br = clockPhase(clock, CO_BREATH_MS);
  for (int k = 0; k < CO_LAYERS; k++) {
    float a = CO_TWIST * k * (1.0f + CO_BREATH * sinTurn(br - 0.06f * k))
            + CO_WAVE * sinTurn(br * 2.0f - 0.1f * k) * (k > 0);
    lc[k] = sinTurn(a + 0.25f); ls[k] = sinTurn(a);
    float w = 295.0f + 90.0f * k;
    lh[k] = 162.0f + 52.0f * k + 1.5f * k * k;
    liw[k] = 1.0f / (w * w);
  }

  // La sphère suit le regard ; la pupille glisse un peu plus loin, dans le rouge.
  float gx, gy;
  coGaze(clock, gx, gy);
  const float nx = gx * (1.0f / 66.0f), ny = gy * (1.0f / 49.0f);
  const float sx = nx * CO_LOOK_X, sy = ny * CO_LOOK_Y;
  const float px0 = sx + nx * 24.0f, py0 = sy + ny * 8.0f;

  const float pp = 1.0f / CO_SCALE, invPp = CO_SCALE;   // px de l'affiche par pixel d'écran
  const float half = 0.5f * pp;

  // On repart de la couche du pixel précédent : elle change rarement.
  int kc = 0;
  uint16_t *out = fb;
  for (int py = 0; py < SCREEN_H; py++) {
    const float y = ((float)py + 0.5f - SCREEN_H * 0.5f) * pp;
    for (int px = 0; px < SCREEN_W; px++) {
      const float x = ((float)px + 0.5f - SCREEN_W * 0.5f) * pp;
      int k = kc;
      float d = coLens(x * lc[k] + y * ls[k], -x * ls[k] + y * lc[k], lh[k], liw[k]);
      if (d < half) {
        while (k > 0) {
          float d2 = coLens(x * lc[k - 1] + y * ls[k - 1], -x * ls[k - 1] + y * lc[k - 1], lh[k - 1], liw[k - 1]);
          if (d2 >= half) break;
          k--; d = d2;
        }
      } else {
        while (++k < CO_LAYERS) {
          d = coLens(x * lc[k] + y * ls[k], -x * ls[k] + y * lc[k], lh[k], liw[k]);
          if (d < half) break;
        }
      }
      kc = k < CO_LAYERS ? k : CO_LAYERS - 1;

      uint16_t col;
      if (k == CO_LAYERS) col = coCol[CO_LAYERS - 1];
      else {
        uint16_t c = coCol[k];
        if (k == 0) {
          // le blanc : la sphère rouge et la pupille, bords lissés
          float dx = x - sx, dy = y - sy, r2 = dx * dx + dy * dy;
          if (r2 < (CO_RED_R + pp) * (CO_RED_R + pp)) {
            float qx = x - px0, qy = y - py0;
            float dp = sqrtf(qx * qx + qy * qy);
            uint16_t in = mix565(CO_RED, CO_BLACK, cover((CO_PUP_R - dp) * invPp));
            c = mix565(c, in, cover((CO_RED_R - sqrtf(r2)) * invPp));
          }
        }
        col = mix565(coCol[k + 1], c, cover(-d * invPp));
      }
      *out++ = col;
    }
  }
}

// ============================================================
//  Setup
// ============================================================
void setup() {
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true);

#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
#endif

  for (int i = 0; i < 1024; i++) sinLut[i] = sinf(i * (2 * PI / 1024));
  for (int k = 0; k <= CO_LAYERS; k++) {
    const uint8_t *c = CO_RGB[k < CO_LAYERS ? k : CO_LAYERS - 1];
    coCol[k] = rgb565(c[0], c[1], c[2]);
  }
}

// ============================================================
//  Boucle
// ============================================================
void loop() {
  uint32_t now = millis();
  drawRainbow(now);
  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);

  uint32_t budget = 1000UL / TARGET_FPS;
  uint32_t spent  = millis() - now;
  if (spent < budget) delay(budget - spent);
}
