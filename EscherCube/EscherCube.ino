/*
 * EscherCube — LilyGO T-QT Pro (ESP32-S3, écran 128x128)
 * ------------------------------------------------------
 * Le cube impossible d'Escher, en métal, qui pivote en allers-retours et
 * flotte de haut en bas, comme un cristal de l'End dans Minecraft.
 *
 * Le cube est vraiment en 3D : douze barres de section carrée, une par
 * arête. Chaque pixel lance un rayon (projection orthogonale) et garde la
 * barre qu'il touche en premier. Dans le repère du cube, toutes les barres
 * sont des boîtes alignées sur les axes : le test du rayon est trois
 * divisions précalculées et quelques comparaisons.
 *
 * L'illusion tient en une règle. Deux barres qui ne se touchent pas et se
 * croisent à l'écran se croisent toujours deux fois : une fois au-dessus du
 * centre (la barre verticale du fond derrière une arête du dessus), une fois
 * en dessous (la verticale de devant devant une arête du dessous). Au-dessus
 * du centre, la barre la plus proche passe devant, comme dans un vrai cube.
 * En dessous, c'est l'inverse. C'est le dessin d'Escher : le haut du cube est
 * vu d'en haut, le bas est vu d'en bas. Les barres qui partagent un coin
 * gardent toujours leur vraie profondeur, sinon les coins se déchirent.
 *
 * Quand le cube passe de face, les croisements disparaissent et c'est un
 * cube ordinaire : l'illusion se défait et se refait tous les quarts de
 * tour. Vu pile sur l'arête (45°), le coin du fond se cache derrière la
 * verticale de devant et l'illusion se brouille aussi. Le cube ne fait donc
 * pas de tour complet : il va et vient entre ces deux vues, autour de
 * CU_AIM (67,5°, à mi-chemin), sans jamais passer ni de face ni sur
 * l'arête. Il faut pour ça que CU_SWEEP reste sous 45°.
 *
 * Lissage : on calcule une couleur par coin de pixel (129 × 129) et chaque
 * pixel fait la moyenne de ses quatre coins. Les bords sont adoucis pour
 * presque rien de plus qu'une image brute.
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

// ---- Le cube -------------------------------------------------
#define CU_SIZE       33.5f    // demi-côté du cube, pixels
#define CU_BEAM       0.34f    // épaisseur des barres, en demi-côtés
#define CU_ELEV       25.5f    // on regarde le cube d'en haut, de tant de degrés

// ---- Le mouvement --------------------------------------------
#define CU_AIM        62.5f    // le cube pivote autour de cet angle, degrés (0 et 90 : de face, 45 : sur l'arête)
#define CU_SWEEP      17.0f    // angle total de l'aller-retour, degrés (sous 45 : ni de face ni sur l'arête)
#define CU_SWING_MS   14500    // un aller-retour complet, ms
#define CU_BOB        2.5f     // le cube monte et descend de ± tant, pixels
#define CU_BOB_MS     3200     // une oscillation, ms

// ---- L'illusion ----------------------------------------------
#define CU_IMPOSSIBLE 1        // 1 : les croisements du bas s'inversent, 0 : vrai cube

// ---- La lumière ----------------------------------------------
#define CU_GRAD       0.35f    // dégradé de lumière du haut-gauche vers le bas-droite
#define CU_FOG        0.43f    // les barres du fond s'assombrissent de tant
#define CU_BG         0.28f    // gris du bas de l'écran (le haut est noir)
#define CU_SHADOW     0.55f    // ombre au sol

// ---- La couleur ----------------------------------------------
#define CU_HUE        253.0f   // teinte, degrés (0 rouge, 60 jaune, 120 vert, 180 cyan, 240 bleu, 300 magenta)
#define CU_SAT        0.53f    // 0 : métal argenté, 1 : couleur pure (le fond prend la même teinte)

#define TARGET_FPS    40

// Hauteur du centre du cube et du sol, en pixels au-dessus du milieu de
// l'écran. La lumière vient d'en haut à gauche, un peu de face.
#define CU_CY         3.0f
// Un demi-pixel à droite : vu sur l'arête, le pli de la barre de devant
// tomberait pile sur une colonne d'échantillons et ferait des dents de scie.
#define CU_CX         0.5f
#define CU_GROUND   -56.0f
static const float CU_LIGHT[3] = { -0.45f, 0.72f, 0.53f };
#define CU_AMB        0.16f

// ============================================================
//  Écran
// ============================================================
TFT_eSPI tft = TFT_eSPI();

#define SCREEN_W 128
#define SCREEN_H 128
#define NS (SCREEN_W + 1)                    // un échantillon par coin de pixel
static uint16_t fb[SCREEN_W * SCREEN_H];
static uint32_t rowA[NS], rowB[NS];          // deux rangées de coins, RGB 8 bits

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

// ============================================================
//  Le cube : 8 sommets, 12 barres
// ============================================================
// Sommet v : x = ±1 selon le bit 0, y selon le bit 1, z selon le bit 2.
static uint8_t cuEdge[12][2];
static bool cuAdj[12][12];                   // les deux barres partagent un coin
static float cuMin[12][3], cuMax[12][3];     // la boîte de chaque barre, repère du cube

static void cuBuild() {
  int n = 0;
  for (int v = 0; v < 8; v++)
    for (int k = 0; k < 3; k++)
      if (!(v & (1 << k))) { cuEdge[n][0] = v; cuEdge[n][1] = v | (1 << k); n++; }
  const float hw = CU_BEAM * 0.5f;
  for (int e = 0; e < 12; e++) {
    int a = cuEdge[e][0], b = cuEdge[e][1];
    for (int k = 0; k < 3; k++) {
      float pa = (a >> k) & 1 ? 1.0f : -1.0f, pb = (b >> k) & 1 ? 1.0f : -1.0f;
      cuMin[e][k] = (pa < pb ? pa : pb) - hw;
      cuMax[e][k] = (pa > pb ? pa : pb) + hw;
    }
  }
  for (int i = 0; i < 12; i++)
    for (int j = 0; j < 12; j++)
      cuAdj[i][j] = cuEdge[i][0] == cuEdge[j][0] || cuEdge[i][0] == cuEdge[j][1] ||
                    cuEdge[i][1] == cuEdge[j][0] || cuEdge[i][1] == cuEdge[j][1];
}

// État d'une image
static float cuR[3][3];                      // repère du cube -> écran
static float cuO[3], cuEx[3], cuEy[3], cuInv[3], cuD[3];
static float cuFace[3][2];                   // lumière de chaque face (axe, côté)
static float cuBx0[12], cuBx1[12], cuBy0[12], cuBy1[12];   // cadre de chaque barre à l'écran
static bool cuFlip[12][12];                  // ce croisement-là s'inverse
static float cuCyb, cuBobN;
static float cuTint[3];                      // la couleur du métal en pleine lumière

#define CU_Z0 3.0f                           // le rayon part de là, en demi-côtés devant le centre

// Couleur d'un coin de pixel. sx, sy : pixels depuis le milieu, y vers le haut.
static uint32_t cuSample(float sx, float sy) {
  float ox = cuO[0] + sx * cuEx[0] + sy * cuEy[0];
  float oy = cuO[1] + sx * cuEx[1] + sy * cuEy[1];
  float oz = cuO[2] + sx * cuEx[2] + sy * cuEy[2];
  int best = -1, bestAx = 0;
  float bestT = 0;
  for (int e = 0; e < 12; e++) {
    if (sx < cuBx0[e] || sx > cuBx1[e] || sy < cuBy0[e] || sy > cuBy1[e]) continue;
    float t1 = (cuMin[e][0] - ox) * cuInv[0], t2 = (cuMax[e][0] - ox) * cuInv[0];
    float tn = t1 < t2 ? t1 : t2, tf = t1 < t2 ? t2 : t1;
    int ax = 0;
    t1 = (cuMin[e][1] - oy) * cuInv[1]; t2 = (cuMax[e][1] - oy) * cuInv[1];
    float a = t1 < t2 ? t1 : t2, b = t1 < t2 ? t2 : t1;
    if (a > tn) { tn = a; ax = 1; }
    if (b < tf) tf = b;
    t1 = (cuMin[e][2] - oz) * cuInv[2]; t2 = (cuMax[e][2] - oz) * cuInv[2];
    a = t1 < t2 ? t1 : t2; b = t1 < t2 ? t2 : t1;
    if (a > tn) { tn = a; ax = 2; }
    if (b < tf) tf = b;
    if (tn >= tf) continue;
    // Qui passe devant : la plus proche, sauf aux croisements inversés
    bool wins;
    if (best < 0) wins = true;
    else if (cuFlip[e][best]) wins = tn > bestT;
    else wins = tn < bestT;
    if (wins) { best = e; bestT = tn; bestAx = ax; }
  }

  if (best < 0) {
    // Le fond : noir en haut, gris en bas, et l'ombre du cube au sol
    float j = (SCREEN_H * 0.5f - sy) * (1.0f / SCREEN_H);
    float v = CU_BG * j * j;
    float rx = CU_SIZE * 1.3f * (1.0f - 0.15f * cuBobN), ry = rx * 0.22f;
    float qx = (sx - CU_CX) / rx, qy = (sy - CU_GROUND) / ry;
    float q = 1.0f - (qx * qx + qy * qy);
    if (q > 0) v *= 1.0f - CU_SHADOW * (0.8f - 0.2f * cuBobN) * q * (2.0f - q);
    return rgb888(v * cuTint[0], v * cuTint[1], v * cuTint[2]);
  }

  float v = cuFace[bestAx][cuD[bestAx] > 0 ? 0 : 1];
  float rr = 1.0f / (CU_SIZE * 1.6f);
  float g = (-0.55f * (sx - CU_CX) + 0.83f * (sy - cuCyb)) * rr;
  v *= 1.0f + CU_GRAD * g;
  float dz = (bestT - CU_Z0) * (1.0f / 1.8f);   // -1 devant, +1 au fond
  v *= 1.0f - CU_FOG * (0.5f + 0.5f * dz);
  // Les reflets les plus forts tirent vers le blanc, comme sur du métal
  float hl = clampf((v - 0.8f) * 1.5f, 0, 0.5f);
  return rgb888(v * cuTint[0] + (255 - cuTint[0]) * hl,
                v * cuTint[1] + (255 - cuTint[1]) * hl,
                v * cuTint[2] + (255 - cuTint[2]) * hl);
}

// ============================================================
//  Une image
// ============================================================
static void drawCube(uint32_t clock) {
  // Entre la vue de face et la vue sur l'arête, ralenti aux deux bouts
  float th = (CU_AIM + 0.5f * CU_SWEEP * sinf(clockPhase(clock, CU_SWING_MS) * 2.0f * PI)) * (PI / 180);
  cuBobN = sinTurn(clockPhase(clock, CU_BOB_MS));
  cuCyb = CU_CY + CU_BOB * cuBobN;
  float ct = cosf(th), st = sinf(th);
  float ce = cosf(CU_ELEV * (PI / 180)), se = sinf(CU_ELEV * (PI / 180));

  // Rotation autour de la verticale, puis on bascule pour voir d'en haut
  cuR[0][0] = ct;        cuR[0][1] = 0;  cuR[0][2] = st;
  cuR[1][0] = se * st;   cuR[1][1] = ce; cuR[1][2] = -se * ct;
  cuR[2][0] = -ce * st;  cuR[2][1] = se; cuR[2][2] = ce * ct;

  // Le rayon dans le repère du cube : origine linéaire en (sx, sy),
  // direction constante (vers le fond de l'écran)
  const float ih = 1.0f / CU_SIZE;
  for (int k = 0; k < 3; k++) {
    cuEx[k] = cuR[0][k] * ih;
    cuEy[k] = cuR[1][k] * ih;
    cuO[k] = CU_Z0 * cuR[2][k] - CU_CX * cuEx[k] - cuCyb * cuEy[k];
    cuD[k] = -cuR[2][k];
    float d = fabsf(cuD[k]) < 1e-6f ? (cuD[k] < 0 ? -1e-6f : 1e-6f) : cuD[k];
    cuInv[k] = 1.0f / d;
  }

  // La teinte : du gris argenté (CU_SAT = 0) à la couleur pure
  float h = fmodf(CU_HUE, 360.0f) * (1.0f / 60.0f);
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
  const float silver[3] = { 238, 242, 250 };
  for (int k = 0; k < 3; k++) cuTint[k] = silver[k] + (pure[k] * 255 - silver[k]) * CU_SAT;

  // La lumière de chaque face
  for (int k = 0; k < 3; k++)
    for (int s = 0; s < 2; s++) {
      float sg = s ? 1.0f : -1.0f;   // côté 0 : la face qu'on voit quand cuD > 0, donc tournée vers -k
      float l = sg * (cuR[0][k] * CU_LIGHT[0] + cuR[1][k] * CU_LIGHT[1] + cuR[2][k] * CU_LIGHT[2]);
      cuFace[k][s] = CU_AMB + (1.0f - CU_AMB) * (l > 0 ? l : 0);
    }

  // Les sommets à l'écran
  float px[8], py[8];
  for (int v = 0; v < 8; v++) {
    float x = v & 1 ? 1.0f : -1.0f, y = v & 2 ? 1.0f : -1.0f, z = v & 4 ? 1.0f : -1.0f;
    px[v] = (cuR[0][0] * x + cuR[0][1] * y + cuR[0][2] * z) * CU_SIZE + CU_CX;
    py[v] = (cuR[1][0] * x + cuR[1][1] * y + cuR[1][2] * z) * CU_SIZE + cuCyb;
  }
  const float pad = CU_BEAM * 0.87f * CU_SIZE + 1.0f;
  for (int e = 0; e < 12; e++) {
    int a = cuEdge[e][0], b = cuEdge[e][1];
    cuBx0[e] = (px[a] < px[b] ? px[a] : px[b]) - pad; cuBx1[e] = (px[a] > px[b] ? px[a] : px[b]) + pad;
    cuBy0[e] = (py[a] < py[b] ? py[a] : py[b]) - pad; cuBy1[e] = (py[a] > py[b] ? py[a] : py[b]) + pad;
  }

  // Les croisements : deux barres sans coin commun dont les axes se coupent
  // à l'écran. Sous le centre du cube, l'ordre s'inverse. On compte aussi
  // les croisements qui tombent juste au-delà d'un bout, sur le coin : vu
  // sur l'arête, le coin du fond est pile derrière la verticale de devant,
  // et il doit passer devant lui aussi.
  const float m = 0.5f * CU_BEAM;
  for (int i = 0; i < 12; i++)
    for (int j = 0; j < 12; j++) {
      cuFlip[i][j] = false;
      if (i == j || cuAdj[i][j] || !CU_IMPOSSIBLE) continue;
      float x1 = px[cuEdge[i][0]], y1 = py[cuEdge[i][0]], x2 = px[cuEdge[i][1]], y2 = py[cuEdge[i][1]];
      float x3 = px[cuEdge[j][0]], y3 = py[cuEdge[j][0]], x4 = px[cuEdge[j][1]], y4 = py[cuEdge[j][1]];
      float d = (x2 - x1) * (y4 - y3) - (y2 - y1) * (x4 - x3);
      if (fabsf(d) < 1e-3f) continue;
      float ta = ((x3 - x1) * (y4 - y3) - (y3 - y1) * (x4 - x3)) / d;
      float tb = ((x3 - x1) * (y2 - y1) - (y3 - y1) * (x2 - x1)) / d;
      if (ta <= -m || ta >= 1 + m || tb <= -m || tb >= 1 + m) continue;
      cuFlip[i][j] = y1 + ta * (y2 - y1) < cuCyb;
    }

  // Une rangée de coins à la fois ; chaque pixel moyenne ses quatre coins
  uint32_t *prev = rowA, *cur = rowB;
  for (int j = 0; j <= SCREEN_H; j++) {
    float sy = SCREEN_H * 0.5f - j;
    for (int i = 0; i <= SCREEN_W; i++) cur[i] = cuSample(i - SCREEN_W * 0.5f, sy);
    if (j > 0) {
      uint16_t *out = fb + (j - 1) * SCREEN_W;
      for (int i = 0; i < SCREEN_W; i++) out[i] = avg4(prev[i], prev[i + 1], cur[i], cur[i + 1]);
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
  cuBuild();
}

// ============================================================
//  Boucle
// ============================================================
void loop() {
  uint32_t now = millis();
  drawCube(now);
  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);

  uint32_t budget = 1000UL / TARGET_FPS;
  uint32_t spent  = millis() - now;
  if (spent < budget) delay(budget - spent);
}
