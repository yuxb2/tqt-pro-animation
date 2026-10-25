/*
 * PaperPond — LilyGO T-QT Pro (ESP32-S3, écran 128x128)
 * -----------------------------------------------------
 * Une mare en papier découpé : des couches empilées, du bleu pâle au bord
 * jusqu'au bleu nuit au fond, dont les bords ondulent doucement. Un ou deux
 * poissons rouges en papier nagent au-dessus et leur ombre glisse sur les
 * couches.
 *
 * Toutes les découpes sont les lignes de niveau d'un seul champ : une
 * ellipse plus quelques ondes qui dérivent lentement. Deux lignes de niveau
 * ne se croisent jamais, donc les couches restent emboîtées quoi qu'il
 * arrive, et chacune ondule un peu autrement que sa voisine.
 *
 * Le champ est calculé une fois par image sur une grille un peu plus grande
 * que l'écran. On en tire, pour chaque point, la distance au bord de la
 * couche du dessus (champ / pente) : c'est l'ombre de contact. L'ombre
 * portée, c'est la même distance lue quelques pixels vers la lumière, en
 * haut à gauche.
 *
 * Les poissons suivent chacun une courbe fermée (un cercle plus une boucle
 * qui tourne deux fois plus vite en sens inverse) : leur vitesse ne tombe
 * jamais à zéro, ils ralentissent et repartent. Le corps se plie selon la
 * courbure du trajet et la queue bat. Leur ombre est plus décalée sur les
 * couches profondes, puisqu'elles sont plus loin d'eux.
 *
 * Lissage : une couleur par coin de pixel (129 × 129), chaque pixel fait la
 * moyenne de ses quatre coins, comme EscherCube.
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

// ---- La mare -------------------------------------------------
#define PD_LAYERS     6        // couches de papier, fond compris (3 à 12)
#define PD_RX         69.0f    // demi-largeur de la mare, pixels
#define PD_RY         73.0f    // demi-hauteur de la mare, pixels
#define PD_EDGE       0.00f    // place de la première découpe (0 : au bord de l'ellipse)
#define PD_CORE       0.75f    // place de la dernière découpe (1 : au centre)
#define PD_WOBBLE     0.120f   // ondulation des bords
#define PD_DRIFT_MS   10000    // les bords font un cycle complet en tant de ms

// ---- La lumière ----------------------------------------------
#define PD_CURVE      0.60f    // répartition des bleus : sous 1, on fonce plus vite
#define PD_SHADOW     0.62f    // force des ombres entre les couches
#define PD_SHADOW_LEN 5        // longueur de l'ombre portée, pixels (entier)
#define PD_SOFT       3.0f     // flou de l'ombre, pixels
#define PD_RIM        0.10f    // liseré clair sur la tranche éclairée
#define PD_GRAIN      0.040f   // grain du papier

// ---- Les poissons --------------------------------------------
#define FI_COUNT      2        // 1 ou 2
#define FI_LEN        37.0f    // longueur d'un poisson, pixels
#define FI_WIDTH      0.170f   // demi-largeur du corps, en longueurs
#define FI_HUE        6.0f     // teinte, degrés (0 rouge, 30 orange, 60 jaune)
#define FI_RANGE      0.50f    // ils s'éloignent du centre de tant, en fraction de la mare
#define FI_WANDER     0.30f    // 0 : un cercle, 0.45 : des boucles serrées
#define FI_SWIM_MS    26000    // un tour complet, ms
#define FI_WAG        0.130f   // battement de queue, en longueurs
#define FI_WAG_MS     800      // un battement, ms
#define FI_LIFT       6.5f     // hauteur au-dessus de l'eau : décalage de l'ombre, pixels
#define FI_SHADOW     0.45f    // force de l'ombre des poissons
#define FI_FOLD       0.16f    // le pli du dos : une moitié éclairée, l'autre à l'ombre

#define TARGET_FPS    40

// La lumière vient d'en haut à gauche ; les ombres partent vers le bas à droite.
#define LX           -0.6f
#define LY           -0.8f

// ============================================================
//  Écran
// ============================================================
TFT_eSPI tft = TFT_eSPI();

#define SCREEN_W 128
#define SCREEN_H 128
#define NS (SCREEN_W + 1)                    // un échantillon par coin de pixel
#define PD_M 10                              // marge de la grille, pour lire l'ombre hors écran
#define PD_GW (NS + 2 * PD_M)
static uint16_t fb[SCREEN_W * SCREEN_H];
static uint32_t rowA[NS], rowB[NS];          // deux rangées de coins, RGB 8 bits
static float pdF[PD_GW * PD_GW];             // le champ de la mare, un par coin

// ============================================================
//  Petits outils
// ============================================================
static float sinLut[1024];
static inline float sinTurn(float turns) { return sinLut[(int)(turns * 1024.0f) & 1023]; }   // sin(2π·turns)
// Phase 0..1 d'un cycle de periodMs : exacte, sans dérive
static inline float clockPhase(uint32_t clock, uint32_t periodMs) { return (clock % periodMs) / (float)periodMs; }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline uint32_t rgb888(float r, float g, float b) {
  return ((uint32_t)clampf(r, 0, 255) << 16) | ((uint32_t)clampf(g, 0, 255) << 8) | (uint32_t)clampf(b, 0, 255);
}
// Moyenne de quatre couleurs 888, rendue en 565
static inline uint16_t avg4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  uint32_t r = ((a >> 16) + (b >> 16) + (c >> 16) + (d >> 16)) >> 2;
  uint32_t g = (((a >> 8) & 255) + ((b >> 8) & 255) + ((c >> 8) & 255) + ((d >> 8) & 255)) >> 2;
  uint32_t bl = ((a & 255) + (b & 255) + (c & 255) + (d & 255)) >> 2;
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (bl >> 3));
}
// Le grain du papier : un bruit fixe, 0..255
static inline uint32_t grain(uint32_t i, uint32_t j) {
  uint32_t h = i * 374761393u + j * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return (h ^ (h >> 16)) & 255;
}

// ============================================================
//  La mare
// ============================================================
// Le champ : 1 au centre, 0 sur l'ellipse, plus quatre ondes qui dérivent.
// x, y : pixels depuis le milieu, y vers le bas. ph : phase de la dérive.
static float pdField(float x, float y, float ph) {
  float ex = x * (1.0f / PD_RX), ey = y * (1.0f / PD_RY);
  float n = 0.50f * sinTurn( 0.0110f * x + 0.0068f * y + ph        + 0.13f)
          + 0.35f * sinTurn(-0.0072f * x + 0.0125f * y - ph        + 0.57f)
          + 0.18f * sinTurn( 0.0190f * x - 0.0150f * y + 2.0f * ph + 0.31f)
          + 0.06f * sinTurn( 0.0310f * x + 0.0270f * y - ph        + 0.82f);
  return 1.0f - sqrtf(ex * ex + ey * ey) + PD_WOBBLE * n;
}

static float pdLv[12];                       // niveau de la découpe qui ouvre chaque couche
static float pdCol[12][3];                   // couleur de chaque couche

static inline int pdLayer(float f) {
  if (f < PD_EDGE) return 0;
  int k = 1 + (int)((f - PD_EDGE) * (PD_LAYERS - 2) / (PD_CORE - PD_EDGE));
  return k > PD_LAYERS - 1 ? PD_LAYERS - 1 : k;
}

// ============================================================
//  Les poissons
// ============================================================
struct Fish {
  float x, y;                                // centre, pixels depuis le milieu
  float c, s;                                // cap
  float k;                                   // courbure du trajet, par pixel
  float wag, wph;                            // battement : amplitude, phase
};
static Fish fish[2];
static float fiCol[3];

// Distance (approchée, pixels) du point au poisson, négative dedans.
// *bent : la position latérale, une fois le corps redressé. (Le poisson
// passe par son numéro : l'IDE Arduino déclare les fonctions avant la struct.)
static float fiDist(int n, float x, float y, float *bent) {
  const Fish &F = fish[n];
  float dx = x - F.x, dy = y - F.y;
  float u = dx * F.c + dy * F.s, v = -dx * F.s + dy * F.c;
  float s = u * (1.0f / FI_LEN);             // 0.5 : le nez, -0.5 : le bout de la queue
  float w = 0.5f - s;                        // 0 au nez, 1 à la queue
  // L'épine suit la courbe du trajet, et la queue bat
  v -= F.k * u * u * 0.5f + F.wag * FI_LEN * w * w * sinTurn(F.wph - 0.9f * w);
  *bent = v;
  const float W = FI_WIDTH * FI_LEN;
  float av = fabsf(v), d;
  if (s > -0.22f) {
    // Le corps : rond devant, effilé vers la queue
    float x1 = (s - 0.14f) * (1.0f / 0.36f);
    float q = 1.0f - x1 * x1;
    if (q < 0) q = 0;
    float hw = x1 > 0 ? W * sqrtf(q) * (1.0f - 0.15f * x1) : W * (0.28f + 0.72f * q);
    d = av - hw;
  } else {
    // La queue : elle s'ouvre en éventail, fendue au milieu
    float t = (-0.22f - s) * (1.0f / 0.28f);
    float hw = W * (0.28f + 0.8f * t * sqrtf(t > 0 ? t : 0));
    d = av - hw;
    float notch = (t - 1.0f + 0.5f * (1.0f - av / hw)) * (0.17f * FI_LEN);
    if (notch > d) d = notch;
  }
  float e = (s - 0.5f) * FI_LEN;  if (e > d) d = e;
  e = (-0.5f - s) * FI_LEN;       if (e > d) d = e;
  return d;
}

// Où en est le poisson n à cet instant
static void fiPose(int n, uint32_t clock) {
  const float A = 1.0f, B = FI_WANDER;
  float dir = n ? -1.0f : 1.0f;              // le second tourne dans l'autre sens
  uint32_t period = n ? FI_SWIM_MS * 13 / 10 : FI_SWIM_MS;
  float th = dir * clockPhase(clock + n * 7919u, period) * 2.0f * PI + n * 2.4f;
  float ph = n * 1.7f;
  float sx = FI_RANGE * PD_RX / (A + B), sy = FI_RANGE * PD_RY / (A + B);
  float c1 = cosf(th), s1 = sinf(th), c2 = cosf(-2.0f * th + ph), s2 = sinf(-2.0f * th + ph);
  // Position, vitesse, accélération (par radian de trajet)
  float px = A * c1 + B * c2, py = A * s1 + B * s2;
  float vx = -A * s1 + 2.0f * B * s2, vy = A * c1 - 2.0f * B * c2;
  float ax = -A * c1 - 4.0f * B * c2, ay = -A * s1 - 4.0f * B * s2;
  float sp = sqrtf(vx * vx + vy * vy);       // entre A - 2B et A + 2B
  vx *= dir * sx; vy *= dir * sy; ax *= sx; ay *= sy;
  float vl = sqrtf(vx * vx + vy * vy) + 1e-6f;
  Fish &F = fish[n];
  F.x = px * sx; F.y = py * sy;
  F.c = vx / vl; F.s = vy / vl;
  float k = (vx * ay - vy * ax) / (vl * vl * vl);
  F.k = 0.6f * clampf(k, -1.0f / (0.8f * FI_LEN), 1.0f / (0.8f * FI_LEN));
  // La queue bat plus fort quand il accélère
  F.wag = FI_WAG * (0.45f + 0.55f * sp / (A + 2.0f * B));
  F.wph = clockPhase(clock + n * 311u, FI_WAG_MS);
}

// ============================================================
//  Une image
// ============================================================
static void drawPond(uint32_t clock) {
  // Le champ, sur toute la grille
  float ph = clockPhase(clock, PD_DRIFT_MS);
  for (int j = 0; j < PD_GW; j++)
    for (int i = 0; i < PD_GW; i++)
      pdF[j * PD_GW + i] = pdField(i - PD_M - SCREEN_W * 0.5f, j - PD_M - SCREEN_H * 0.5f, ph);

  // Les couches : un niveau et un bleu chacune, du pâle au bord au bleu nuit
  const float out[3] = { 172, 189, 207 }, deep[3] = { 14, 30, 52 };
  for (int k = 0; k < PD_LAYERS; k++) {
    pdLv[k] = k ? PD_EDGE + (k - 1) * (PD_CORE - PD_EDGE) / (PD_LAYERS - 2) : -1e9f;
    float t = powf((float)k / (PD_LAYERS - 1), PD_CURVE);
    for (int c = 0; c < 3; c++) pdCol[k][c] = out[c] + (deep[c] - out[c]) * t;
  }

  // Les poissons, et leur rouge
  for (int n = 0; n < FI_COUNT; n++) fiPose(n, clock);
  float h = fmodf(FI_HUE, 360.0f) * (1.0f / 60.0f);
  if (h < 0) h += 6.0f;
  int hi = (int)h;
  float hf = h - hi, pure[3];
  switch (hi) {
    case 0:  pure[0] = 1;      pure[1] = hf;     pure[2] = 0;      break;
    case 1:  pure[0] = 1 - hf; pure[1] = 1;      pure[2] = 0;      break;
    case 2:  pure[0] = 0;      pure[1] = 1;      pure[2] = hf;     break;
    case 3:  pure[0] = 0;      pure[1] = 1 - hf; pure[2] = 1;      break;
    case 4:  pure[0] = hf;     pure[1] = 0;      pure[2] = 1;      break;
    default: pure[0] = 1;      pure[1] = 0;      pure[2] = 1 - hf; break;
  }
  for (int c = 0; c < 3; c++) fiCol[c] = (0.18f + 0.82f * pure[c]) * 225.0f;

  const int ox = (int)lroundf(-LX * PD_SHADOW_LEN), oy = (int)lroundf(-LY * PD_SHADOW_LEN);
  const float reach = 0.65f * FI_LEN + 2.0f;

  uint32_t *prev = rowA, *cur = rowB;
  for (int j = 0; j <= SCREEN_H; j++) {
    float y = j - SCREEN_H * 0.5f;
    for (int i = 0; i <= SCREEN_W; i++) {
      float x = i - SCREEN_W * 0.5f;
      int idx = (j + PD_M) * PD_GW + i + PD_M;
      float f = pdF[idx];
      float gx = (pdF[idx + 1] - pdF[idx - 1]) * 0.5f, gy = (pdF[idx + PD_GW] - pdF[idx - PD_GW]) * 0.5f;
      float g = sqrtf(gx * gx + gy * gy) + 1e-4f;
      int k = pdLayer(f);
      float r = pdCol[k][0], gg = pdCol[k][1], b = pdCol[k][2];

      float shade = 1.0f + 0.06f * (LX * x + LY * y) * (1.0f / 64.0f);
      if (k > 0) {
        // Ombre de contact au pied de la couche du dessus, et ombre portée
        float du = (f - pdLv[k]) / g;
        float ds = (pdF[idx - oy * PD_GW - ox] - pdLv[k]) / g;
        float sh = clampf(0.5f - ds / PD_SOFT, 0, 1);
        sh = sh * sh * (3.0f - 2.0f * sh);
        float ao = clampf(1.0f - du * 0.3f, 0, 1);
        shade *= 1.0f - PD_SHADOW * (0.75f * sh + 0.35f * ao * ao);
      }
      float rim = 0;
      if (k < PD_LAYERS - 1) {
        // La tranche du papier : claire du côté de la lumière
        float din = (pdLv[k + 1] - f) / g;
        if (din < 1.5f) rim = PD_RIM * 255.0f * (-(LX * gx + LY * gy) / g) * (1.0f - din * (1.0f / 1.5f));
      }
      r = r * shade + rim; gg = gg * shade + rim; b = b * shade + rim;

      // L'ombre des poissons, plus décalée sur les couches profondes
      if (FI_COUNT > 0) {
        float lift = FI_LIFT * (0.7f + 0.6f * k / (PD_LAYERS - 1));
        float sx = x + LX * lift, sy = y + LY * lift, soft = 1.0f + 0.35f * lift, best = 0;
        for (int n = 0; n < FI_COUNT; n++) {
          if (fabsf(sx - fish[n].x) > reach + soft || fabsf(sy - fish[n].y) > reach + soft) continue;
          float bent;
          float sh = clampf(0.5f - fiDist(n, sx, sy, &bent) / soft, 0, 1);
          if (sh > best) best = sh;
        }
        best = best * best * (3.0f - 2.0f * best);
        float m = 1.0f - FI_SHADOW * best;
        r *= m; gg *= m; b *= m;
      }

      // Les poissons, le second sous le premier
      for (int n = FI_COUNT - 1; n >= 0; n--) {
        const Fish &F = fish[n];
        if (fabsf(x - F.x) > reach || fabsf(y - F.y) > reach) continue;
        float bent;
        float d = fiDist(n, x, y, &bent);
        float a = clampf(0.5f - d, 0, 1);
        if (a <= 0) continue;
        // Deux pans de part et d'autre du dos : celui tourné vers la lumière est clair
        float side = clampf(bent * 1.25f, -1, 1);
        float lit = (-F.s * LX + F.c * LY);
        float fs = 1.0f + FI_FOLD * side * lit;
        fs *= 1.0f - 0.18f * clampf(1.0f + d * (1.0f / 1.2f), 0, 1);   // le bord, un peu plus sombre
        r += (fiCol[0] * fs - r) * a;
        gg += (fiCol[1] * fs - gg) * a;
        b += (fiCol[2] * fs - b) * a;
      }

      float gr = 1.0f + PD_GRAIN * ((grain(i, j) * (2.0f / 255.0f)) - 1.0f);
      cur[i] = rgb888(r * gr, gg * gr, b * gr);
    }
    if (j > 0) {
      uint16_t *o = fb + (j - 1) * SCREEN_W;
      for (int i = 0; i < SCREEN_W; i++) o[i] = avg4(prev[i], prev[i + 1], cur[i], cur[i + 1]);
    }
    uint32_t *t = prev; prev = cur; cur = t;
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
  drawPond(now);
  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);

  uint32_t budget = 1000UL / TARGET_FPS;
  uint32_t spent  = millis() - now;
  if (spent < budget) delay(budget - spent);
}
