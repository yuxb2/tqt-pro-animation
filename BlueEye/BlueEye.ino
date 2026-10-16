/*
 * BlueEye — LilyGO T-QT Pro (ESP32-S3, écran 128x128)
 * ---------------------------------------------------
 * L'œil bleu du thème « Couleurs » des Yeux — celui de droite, qui tournait
 * déjà sur un T-QT —, seul sur son écran.
 *
 * C'est l'icône du gif, et il rejoue sa boucle, relevée image par image sur
 * la vidéo — 4,27 s dans le gif, ralentie ici à 5 s : le regard part en
 * haut à gauche, glisse en haut à droite, l'œil cligne en s'aplatissant, se
 * rouvre en regardant en bas, revient au centre, plisse des deux paupières,
 * puis saute en haut à gauche. Le reflet suit la pupille.
 *
 * La forme de l'amande suit le regard, tout de suite : quand l'œil regarde
 * en bas, la paupière du bas s'ouvre un peu et celle du haut descend ; en
 * haut, l'inverse. On reste une amande — le bas ne descend pas de plus de
 * BL_DOWN_BOT. C'est le réglage demandé sur le mur : « garde la forme
 * d'amande », et « au moment où la pupille remonte, l'arrondi doit
 * immédiatement changer ».
 *
 * L'amande a des bords paraboliques, |v| < H (1 - (u/W)²), avec une
 * demi-hauteur différente en haut et en bas. Toutes les tailles sont en
 * pixels du gif (500 × 500) ; BL_SCALE dit combien de pixels d'écran ça fait.
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
#define BL_SCALE     0.32f    // pixels d'écran par pixel du gif

// ---- L'œil, en px du gif -------------------------------------
#define BL_W         197.0f   // amande : demi-largeur
#define BL_H         106.0f   // et demi-hauteur au repos
#define BL_IRIS_R     95.0f   // iris bleu
#define BL_PUP_R      67.0f   // pupille

// ---- Le mouvement --------------------------------------------
#define BL_LOOP_MS   5000     // une boucle (4267 dans le gif), ms
#define BL_DOWN_TOP  0.16f    // en regardant en bas, la paupière du haut descend de tant
#define BL_DOWN_BOT  0.16f    // et celle du bas s'ouvre de tant — trop, et ce n'est plus une amande
#define BL_SQUINT    0.57f    // quand l'œil plisse, la paupière du haut se ferme de tant
#define BL_SQUINT_BOT 0.30f   // et celle du bas remonte de tant

#define TARGET_FPS   40

static const uint16_t BL_BG = 0x2B59, BL_WHITE = 0xF7BE, BL_PUP = 0x0063;

// ============================================================
//  Écran
// ============================================================
TFT_eSPI tft = TFT_eSPI();

#define SCREEN_W 128
#define SCREEN_H 128
static uint16_t fb[SCREEN_W * SCREEN_H];

// ============================================================
//  Petits outils
// ============================================================
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
//  Le gif
// ============================================================
// À l'instant clock : le regard (px du gif) et l'ouverture des paupières du
// haut et du bas (1 = repos). Rendus par référence, pas dans une struct :
// l'IDE Arduino écrit les prototypes des fonctions avant les struct du
// croquis, et la compilation casse.
static void blueGif(uint32_t clock, float &gx, float &gy, float &top, float &bot) {
  float t = (clock % BL_LOOP_MS) * (4.267f / BL_LOOP_MS);   // secondes du gif
  const float ULx = -42, ULy = -49, URx = 66, URy = -49, DNx = 0, DNy = 45;
  // regard
  if (t < 0.10f)      { gx = ULx; gy = ULy; }
  else if (t < 0.60f) { float q = 1 - (t - 0.10f) / 0.50f; gx = lerpf(ULx, URx, 1 - q * q); gy = ULy; }
  else if (t < 1.08f) { gx = URx; gy = URy; }
  else if (t < 1.22f) { float p = smooth01((t - 1.08f) / 0.14f); gx = lerpf(DNx + 14, DNx, p); gy = lerpf(DNy - 8, DNy, p); }
  else if (t < 1.53f) { gx = DNx; gy = DNy; }
  else if (t < 1.80f) { float p = smooth01((t - 1.53f) / 0.27f); gx = lerpf(DNx, 0, p); gy = lerpf(DNy, 0, p); }
  else if (t < 3.40f) { gx = 0; gy = 0; }
  else if (t < 3.60f) { float p = smooth01((t - 3.40f) / 0.20f); gx = lerpf(0, ULx, p); gy = lerpf(0, ULy, p); }
  else                { gx = ULx; gy = ULy; }
  // Les paupières suivent le regard, sans retard.
  float ny = gy * (1.0f / 45.0f); ny = ny > 1 ? 1 : (ny < -1.1f ? -1.1f : ny);
  float dn = ny > 0 ? ny : 0, up = ny < 0 ? -ny : 0;
  top = 1.0f - BL_DOWN_TOP * dn + 0.06f * up;
  bot = 1.0f + BL_DOWN_BOT * dn - 0.06f * up;
  // l'œil plisse au centre (2,73 -> 3,40 s), des deux paupières, et se
  // rouvre dès qu'il remonte
  float sq = 0;
  if (t >= 2.73f && t < 2.93f) sq = smooth01((t - 2.73f) / 0.20f);
  else if (t >= 2.93f && t < 3.40f) sq = 1;
  else if (t >= 3.40f && t < 3.50f) sq = 1 - smooth01((t - 3.40f) / 0.10f);
  top *= 1.0f - BL_SQUINT * sq; bot *= 1.0f - BL_SQUINT_BOT * sq;
  // clignement (0,95 -> 1,22 s)
  float o = 1;
  if (t >= 0.95f && t < 1.05f) o = 1 - smooth01((t - 0.95f) / 0.10f);
  else if (t >= 1.05f && t < 1.12f) o = 0;
  else if (t >= 1.12f && t < 1.22f) o = smooth01((t - 1.12f) / 0.10f);
  top *= o; bot *= o;
}

// Distance signée (approchée, px du gif) au bord d'une amande parabolique ;
// négative dedans. ht / hb : demi-hauteurs du haut et du bas.
static inline float blLens(float u, float v, float ht, float hb, float invW2) {
  float q = 1.0f - u * u * invW2;                     // 1 au centre, 0 aux coins
  float H = v < 0 ? ht : hb;
  float f = (v < 0 ? -v : v) - H * q;
  float gr = 2.0f * H * (u < 0 ? -u : u) * invW2;     // pente du bord
  return f / (gr > 1.0f ? gr : 1.0f);
}

// ============================================================
//  Une image
// ============================================================
static void drawBlue(uint32_t clock) {
  float gx, gy, top, bot;
  blueGif(clock, gx, gy, top, bot);
  const float invW2 = 1.0f / (BL_W * BL_W);
  const float ht = BL_H * top, hb = BL_H * bot;
  const float pp = 1.0f / BL_SCALE, invPp = BL_SCALE;   // px du gif par pixel d'écran
  const float irisM = (BL_IRIS_R + pp) * (BL_IRIS_R + pp);
  uint16_t *out = fb;
  for (int py = 0; py < SCREEN_H; py++) {
    const float y = ((float)py + 0.5f - SCREEN_H * 0.5f) * pp;
    for (int px = 0; px < SCREEN_W; px++) {
      const float x = ((float)px + 0.5f - SCREEN_W * 0.5f) * pp;
      float d = (ht + hb > 0.5f) ? blLens(x, y, ht, hb, invW2) : 1e3f;
      uint16_t col;
      if (d > 0.5f * pp) col = BL_BG;
      else {
        // iris bleu (comme le fond), pupille, reflet : tout suit le regard
        float dx = x - gx, dy = y - gy, r2 = dx * dx + dy * dy;
        uint16_t c = BL_WHITE;
        if (r2 < irisM) {
          float r = sqrtf(r2);
          c = mix565(BL_WHITE, BL_BG, cover((BL_IRIS_R - r) * invPp));
          c = mix565(c, BL_PUP, cover((BL_PUP_R - r) * invPp));
          float hx = dx + 51.0f, hy = dy + 37.5f;
          if (hx * hx + hy * hy < 18.0f * 18.0f) c = mix565(c, BL_WHITE, cover((16.0f - sqrtf(hx * hx + hy * hy)) * invPp));
        }
        col = mix565(BL_BG, c, cover(-d * invPp));
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
}

// ============================================================
//  Boucle
// ============================================================
void loop() {
  uint32_t now = millis();
  drawBlue(now);
  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);

  uint32_t budget = 1000UL / TARGET_FPS;
  uint32_t spent  = millis() - now;
  if (spent < budget) delay(budget - spent);
}
