/*
 * CurtainEye — LilyGO T-QT Pro (ESP32-S3, écran 128x128)
 * ------------------------------------------------------
 * Le thème « Rideaux » des Yeux, ramené sur un seul écran : l'affiche op-art
 * d'origine, avec l'œil au milieu et ses cercles autour.
 *
 * Le fond : deux familles de cercles concentriques noirs, épais, sur papier
 * orange, centrées au-dessus et au-dessous de l'œil. Leurs anneaux
 * grandissent sans fin et se croisent en treillis. Sur le mur, ces centres
 * étaient posés entre les deux écrans ; ici ils reviennent sur l'œil, comme
 * sur l'affiche.
 *
 * L'œil : que des formes simples. Une amande à deux arcs de cercle cernée de
 * noir, un disque d'iris noir qui touche le contour en haut et en bas, un
 * anneau crème, l'iris bleu, la pupille, et un reflet rond. Il ne bouge pas
 * et ne cligne pas : seul le reflet fait le tour de la pupille, comme si la
 * lumière tournait autour.
 *
 * Sur le mur, l'œil était calculé une fois au démarrage, lissé sur seize
 * échantillons par pixel, et rangé en PSRAM. Ici il est recalculé à chaque
 * image, ses bords lissés à partir de leur distance au pixel : pas de table,
 * pas de PSRAM, et la taille de l'œil se règle comme le reste. Une unité
 * d'œil vaut un pixel quand RI_SC = 1.
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

// ---- L'œil ---------------------------------------------------
#define RI_SC       1.15f    // taille de l'œil : à 1 il fait 100 px de large
#define RI_W_OUT   (50.0f * RI_SC)  // amande extérieure (le contour noir) : demi-largeur, demi-hauteur
#define RI_H_OUT   (28.5f * RI_SC)
#define RI_W_IN    (40.0f * RI_SC)  // amande intérieure (le blanc de l'œil)
#define RI_H_IN    (22.0f * RI_SC)
#define RI_IRIS    (27.0f * RI_SC)  // disque noir de l'iris
#define RI_RING    (20.6f * RI_SC)  // anneau crème
#define RI_BLUE    (18.8f * RI_SC)  // iris bleu
#define RI_PUP     (12.2f * RI_SC)  // pupille
#define RI_SHINE_D  (7.7f * RI_SC)  // reflet : distance au centre de la pupille
#define RI_SHINE_R  (5.0f * RI_SC)  // et rayon
#define RI_SPIN_MS  5600            // un tour du reflet, ms

// ---- Les cercles, en pixels ----------------------------------
#define RI_PITCH   20.0f     // écart entre deux cercles
#define RI_LINE     3.0f     // épaisseur des cercles
#define RI_OFFSET  39.0f     // centres des deux familles : au-dessus et au-dessous de l'œil
#define RI_FLOW_MS 3000      // un cercle avance d'un écart en tant de ms

#define TARGET_FPS 40

static const uint16_t RI_ORANGE = 0xF204, RI_CREAM = 0xFFF8, RI_RINGC = 0xFFFC, RI_BLUEC = 0x2BD5;

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
static float sinLut[1024];
static inline float sinTurn(float turns) { return sinLut[(int)(turns * 1024.0f) & 1023]; }   // sin(2π·turns)
// Phase 0..1 d'un cycle de periodMs : exacte, sans dérive
static inline float clockPhase(uint32_t clock, uint32_t periodMs) { return (clock % periodMs) / (float)periodMs; }
// floorf est un appel de bibliothèque : cette version suffit ici
static inline float ffloor(float x) { float i = (float)(int)x; return i > x ? i - 1.0f : i; }
static inline float frac(float x) { return x - ffloor(x); }
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

// Une amande à deux arcs (vesica) de demi-largeur W et demi-hauteur H : ses
// deux arcs ont pour rayon R = (W² + H²) / 2H, et leurs centres sont à
// ±c = ±(R − H) sur l'axe vertical.
static inline float vesicaR(float W, float H) { return (W * W + H * H) / (2.0f * H); }
// Distance signée au bord de l'amande, négative dedans
static inline float vesicaDist(float u, float v, float R, float c) {
  float d1 = sqrtf(u * u + (v - c) * (v - c)), d2 = sqrtf(u * u + (v + c) * (v + c));
  return (d1 > d2 ? d1 : d2) - R;
}

// Le dedans de l'œil à la distance r du centre, de la crème à la pupille,
// chaque bord lissé
static inline uint16_t riInside(float r) {
  uint16_t c = mix565(RI_CREAM, 0x0000, cover(RI_IRIS - r));
  c = mix565(c, RI_RINGC, cover(RI_RING - r));
  c = mix565(c, RI_BLUEC, cover(RI_BLUE - r));
  return mix565(c, 0x0000, cover(RI_PUP - r));
}

// ============================================================
//  Une image
// ============================================================
static void drawCurtain(uint32_t clock) {
  const float flow = clockPhase(clock, RI_FLOW_MS);
  const float invP = 1.0f / RI_PITCH, halfLine = RI_LINE * 0.5f;
  // le reflet fait le tour de la pupille, en partant d'en haut à gauche
  const float sa = clockPhase(clock, RI_SPIN_MS) - 0.375f;
  const float shX = RI_SHINE_D * sinTurn(sa + 0.25f), shY = RI_SHINE_D * sinTurn(sa);
  const float rOut = vesicaR(RI_W_OUT, RI_H_OUT), cOut = rOut - RI_H_OUT;
  const float rIn = vesicaR(RI_W_IN, RI_H_IN), cIn = rIn - RI_H_IN;

  uint16_t *out = fb;
  for (int py = 0; py < SCREEN_H; py++) {
    const float v = (float)py + 0.5f - SCREEN_H * 0.5f;
    const float ya = v + RI_OFFSET, yb = v - RI_OFFSET;   // centres au-dessus et au-dessous
    for (int px = 0; px < SCREEN_W; px++) {
      const float u = (float)px + 0.5f - SCREEN_W * 0.5f;

      // le fond : deux familles de cercles, bords lissés
      float rA = sqrtf(u * u + ya * ya), rB = sqrtf(u * u + yb * yb);
      float fa = frac(rA * invP - flow), fB = frac(rB * invP - flow);
      float da = (fa < 0.5f ? fa : 1.0f - fa) * RI_PITCH, db = (fB < 0.5f ? fB : 1.0f - fB) * RI_PITCH;
      float dl = da < db ? da : db;                       // distance au trait le plus proche
      uint16_t col = mix565(RI_ORANGE, 0x0000, cover(halfLine - dl));

      // l'œil, s'il est assez près
      if (fabsf(u) < RI_W_OUT + 1.0f && fabsf(v) < RI_H_OUT + 1.0f) {
        int aOut = cover(-vesicaDist(u, v, rOut, cOut));
        if (aOut) {
          uint16_t c = 0x0000;
          int aIn = cover(-vesicaDist(u, v, rIn, cIn));
          if (aIn) {
            float r = sqrtf(u * u + v * v);
            c = riInside(r);
            float sx = u - shX, sy = v - shY;
            if (fabsf(sx) < RI_SHINE_R + 1.0f && fabsf(sy) < RI_SHINE_R + 1.0f && r < RI_RING)
              c = mix565(c, RI_CREAM, cover(RI_SHINE_R - sqrtf(sx * sx + sy * sy)));
            c = mix565(0x0000, c, aIn);
          }
          col = mix565(col, c, aOut);
        }
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
}

// ============================================================
//  Boucle
// ============================================================
void loop() {
  uint32_t now = millis();
  drawCurtain(now);
  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);

  uint32_t budget = 1000UL / TARGET_FPS;
  uint32_t spent  = millis() - now;
  if (spent < budget) delay(budget - spent);
}
