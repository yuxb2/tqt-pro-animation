/*
 * SkullTurn — LilyGO T-QT Pro (ESP32-S3, écran 128x128)
 * -----------------------------------------------------
 * Un crâne en volume, éclairé, qui tourne lentement sur lui-même. Deux
 * couleurs, pas une de plus : les demi-teintes sont tramées.
 *
 * Il n'y a ni modèle 3D, ni maillage, ni texture, ni tampon de profondeur.
 * Le crâne est un assemblage de dix-huit ellipsoïdes : neuf qu'on ajoute
 * pour la matière, neuf qu'on retire pour les creux — orbites, nez, fente
 * de la bouche, fosses temporales. Tout tient dans le tableau SK_PRIM, six
 * nombres par ligne : centre puis rayons. Pour changer la tête du crâne, on
 * déplace ces nombres, et rien d'autre.
 *
 * L'arrière tient en deux idées, et aucune n'ajoute de pièce. D'abord la
 * boîte crânienne descend : on l'allonge vers le bas en laissant le sommet
 * du dôme où il est, si bien qu'elle finit au niveau des dents comme sur un
 * vrai crâne, au lieu de s'arrêter à mi-hauteur. Ensuite l'arche de la
 * mâchoire est évidée par l'arrière — un ellipsoïde creusé dans son
 * intérieur, qui s'arrête avant le menton. Ce creux fait deux choses d'un
 * coup : il ouvre le dessous du crâne en U, et il supprime la masse qui
 * pendait sous l'occiput. Le menton, lui, n'est pas touché : le creux
 * s'arrête à z = 0,40 et la face avant est en z = 0,54.
 *
 * Chaque pixel envoie un rayon. Un ellipsoïde traversé par un rayon, c'est
 * une équation du second degré : on les résout toutes les dix-sept d'un
 * coup, et le parcours qui suit ne fait plus que comparer les intervalles
 * obtenus, ce qui ne coûte presque rien. On avance de creux en creux
 * jusqu'au premier morceau de matière qui n'est pas évidé.
 *
 * Deux choses portent le rendu, plus que la géométrie :
 *
 *   - CAVITY. Une paroi creusée reçoit moins de lumière qu'une surface
 *     nue, parce qu'elle ne voit qu'un bout de ciel. Sans ce facteur les
 *     orbites se lisent comme des bols éclairés ; avec, comme des trous.
 *   - la trame ordonnée. Le seuil de noircissement change d'un pixel à
 *     l'autre selon une petite table, ce qui fabrique des demi-teintes
 *     avec deux couleurs, sans jamais afficher de gris.
 *
 * Limite connue : de face et de trois quarts le crâne se tient, de profil
 * beaucoup moins — deux ellipsoïdes qui se croisent font une arête vive et
 * ne se raccordent pas. SWEEP_DEG est la réponse : sous 360, le crâne se
 * balance autour de sa face au lieu de faire le tour.
 *
 * Libs : TFT_eSPI (version LilyGO du dépôt, Setup211) — rien d'autre.
 *
 * Boutons :
 *   IO00 (BOOT) → finesse : 1 rayon par pixel / 1 rayon pour quatre
 *   IO47 (KEY)  → mouvement : tour complet / balancement
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include <math.h>

// ============================================================
//  CONFIG — tout se règle ici
// ============================================================

// ---- Le mouvement --------------------------------------------
#define SPIN_S     12.0f    // durée d'un tour (ou d'un aller-retour), en s
#define SWEEP_DEG  110.0f   // 360 = tour complet ; en dessous, balancement
#define TILT_DEG   8.0f     // inclinaison fixe ; positif = vu d'un peu haut

// ---- Le cadrage ----------------------------------------------
#define ZOOM_PX    64.0f    // pixels écran par unité du modèle
#define BOUND_R    1.45f    // sphère englobante : elle coupe court sur le fond
#define Z0         3.0f     // plan de départ des rayons

// ---- La lumière ----------------------------------------------
#define LIGHT_AZ  -11.0f    // azimut, en degrés. Fixe dans la pièce :
#define LIGHT_EL   37.0f    // c'est le crâne qui tourne dedans.
#define AMBIENT    0.14f    // ce qui reste de matière dans l'ombre
#define RIM        0.14f    // liseré de tranche, qui décolle du fond noir
#define CAVITY     0.42f    // lumière restante au fond d'un creux
#define GAMMA      1.05f    // sous 1 ça éclaircit, au-dessus ça creuse

// ---- Les dents -----------------------------------------------
#define TEETH      0        // rayures dans l'ombrage, pas des volumes
#define TEETH_LO  -0.60f
#define TEETH_HI  -0.40f
#define TEETH_N    5.6f     // nombre de dents par radian d'arcade

// ---- Rendu ----------------------------------------------------
#define HALF_RES   1        // 1 = un rayon pour quatre pixels
#define BAYER_8    0        // 1 = trame 8x8 (plus de nuances), 0 = 4x4
#define TARGET_FPS 30

// ---- Boutons --------------------------------------------------
#define PIN_BTN_LEFT   0    // BOOT
#define PIN_BTN_RIGHT  47   // KEY
#define BTN_DEBOUNCE_MS 220

// ============================================================
//  Le crâne — cx, cy, cz, rx, ry, rz
// ============================================================
// Les N_ADD premiers ajoutent de la matière, les suivants la creusent.

#define N_ADD  9
#define N_SUB  9
#define N_PRIM (N_ADD + N_SUB)

static const float SK_PRIM[N_PRIM][6] = {
  { 0.00f,  0.10f, -0.26f,  0.56f, 0.68f, 0.68f },   // boîte crânienne : descendue
  { 0.00f,  0.20f,  0.14f,  0.49f, 0.47f, 0.42f },   // frontal
  { 0.00f, -0.12f,  0.26f,  0.45f, 0.42f, 0.40f },   // massif facial
  { 0.00f, -0.44f,  0.24f,  0.33f, 0.17f, 0.31f },   // maxillaire
  { 0.00f, -0.60f,  0.18f,  0.36f, 0.16f, 0.36f },   // corps de la mandibule
  { 0.32f, -0.32f,  0.04f,  0.07f, 0.24f, 0.15f },   // branche montante droite
  {-0.32f, -0.32f,  0.04f,  0.07f, 0.24f, 0.15f },   // branche montante gauche
  { 0.38f, -0.04f,  0.00f,  0.10f, 0.07f, 0.36f },   // arcade zygomatique droite
  {-0.38f, -0.04f,  0.00f,  0.10f, 0.07f, 0.36f },   // arcade zygomatique gauche

  { 0.25f, -0.01f,  0.56f,  0.224f, 0.224f, 0.260f },   // orbite droite
  {-0.25f, -0.01f,  0.56f,  0.224f, 0.224f, 0.260f },   // orbite gauche
  { 0.00f, -0.26f,  0.52f,  0.10f, 0.15f, 0.20f },   // ouverture nasale
  { 0.00f, -0.51f,  0.30f,  0.30f, 0.03f, 0.30f },   // fente de la bouche
  { 0.60f,  0.18f, -0.08f,  0.22f, 0.30f, 0.34f },   // fosse temporale droite
  {-0.60f,  0.18f, -0.08f,  0.22f, 0.30f, 0.34f },   // fosse temporale gauche
  { 0.00f, -1.06f,  0.10f,  0.50f, 0.38f, 0.50f },   // dessous du menton
  { 0.00f, -0.62f, -0.02f,  0.26f, 0.22f, 0.42f },   // l'arche de la mâchoire, évidée par l'arrière
  { 0.00f, -1.02f, -0.42f,  0.46f, 0.42f, 0.44f },   // rase le bas de l'arrière
};

// Trame ordonnée. C'est elle qui fabrique les gris avec deux couleurs.
#if BAYER_8
static const uint8_t BAYER[64] = {
   0, 32,  8, 40,  2, 34, 10, 42,
  48, 16, 56, 24, 50, 18, 58, 26,
  12, 44,  4, 36, 14, 46,  6, 38,
  60, 28, 52, 20, 62, 30, 54, 22,
   3, 35, 11, 43,  1, 33,  9, 41,
  51, 19, 59, 27, 49, 17, 57, 25,
  15, 47,  7, 39, 13, 45,  5, 37,
  63, 31, 55, 23, 61, 29, 53, 21,
};
#define BAY_MASK  7
#define BAY_SHIFT 3
#define BAY_DIV   (1.0f / 64.0f)
#else
static const uint8_t BAYER[16] = {
   0,  8,  2, 10,
  12,  4, 14,  6,
   3, 11,  1,  9,
  15,  7, 13,  5,
};
#define BAY_MASK  3
#define BAY_SHIFT 2
#define BAY_DIV   (1.0f / 16.0f)
#endif

// ============================================================
//  Écran
// ============================================================
TFT_eSPI tft = TFT_eSPI();

#define SCREEN_W 128
#define SCREEN_H 128
#define TWO_PI_F 6.2831853f
#define DEG      0.01745329f

static uint16_t fb[SCREEN_W * SCREEN_H];

// ============================================================
//  Tables recalculées à chaque image
// ============================================================
// La direction du rayon est la même pour tout l'écran (vue orthographique),
// donc tout ce qui n'en dépend que se prépare une fois par image et sort de
// la boucle des seize mille pixels.
static float skInvR[N_PRIM][3];   // 1 / rayon
static float skDp[N_PRIM][3];     // direction ramenée dans la sphère unité
static float skA[N_PRIM], skInvA[N_PRIM];
static float skIn[N_PRIM], skOut[N_PRIM];
static bool  skHit[N_PRIM];

// ============================================================
//  État
// ============================================================
static bool  halfRes  = (HALF_RES != 0);
static bool  fullTurn = (SWEEP_DEG >= 360.0f);
static float clockT   = 0.0f;
static uint32_t lastFrameUs = 0;
static uint32_t lastBtnMs   = 0;

// ============================================================
//  Un rayon
// ============================================================
static float shade(float ox, float oy, float oz,
                   float dx, float dy, float dz,
                   float lx, float ly, float lz) {
  // 1. Toutes les intersections d'un coup. Après ça le parcours ne compare
  //    plus que des nombres : c'est ce qui rend la CSG bon marché.
  for (int i = 0; i < N_PRIM; i++) {
    const float *q = SK_PRIM[i];
    float px = (ox - q[0]) * skInvR[i][0];
    float py = (oy - q[1]) * skInvR[i][1];
    float pz = (oz - q[2]) * skInvR[i][2];
    float b = px * skDp[i][0] + py * skDp[i][1] + pz * skDp[i][2];
    float c = px * px + py * py + pz * pz - 1.0f;
    float disc = b * b - skA[i] * c;
    if (disc <= 0.0f) { skHit[i] = false; continue; }
    float sq = sqrtf(disc);
    skHit[i] = true;
    skIn[i]  = (-b - sq) * skInvA[i];
    skOut[i] = (-b + sq) * skInvA[i];
  }

  // 2. Entrée dans l'union des parties pleines.
  float t = 1e30f;
  int k = -1;
  for (int i = 0; i < N_ADD; i++)
    if (skHit[i] && skIn[i] < t) { t = skIn[i]; k = i; }
  if (k < 0) return 0.0f;

  // 3. On sort des creux tant qu'on est dedans, et on reprend plus loin si
  //    on en ressort dans le vide.
  bool inward = false;
  for (int guard = 0; guard < 6; guard++) {
    int jBest = -1;
    float tBest = t;
    for (int j = N_ADD; j < N_PRIM; j++)
      if (skHit[j] && skIn[j] < t && t < skOut[j] && skOut[j] > tBest) {
        tBest = skOut[j]; jBest = j;
      }
    if (jBest < 0) break;                    // à l'air libre : c'est la surface

    t = tBest;
    int inside = -1;
    for (int i = 0; i < N_ADD; i++)
      if (skHit[i] && skIn[i] < t && t < skOut[i]) { inside = i; break; }
    if (inside >= 0) { k = jBest; inward = true; continue; }

    float nt = 1e30f;
    int ni = -1;
    for (int i = 0; i < N_ADD; i++)
      if (skHit[i] && skIn[i] > t + 1e-4f && skIn[i] < nt) { nt = skIn[i]; ni = i; }
    if (ni < 0) return 0.0f;
    t = nt; k = ni; inward = false;
  }

  // 4. La normale, au signe près : la paroi d'un creux regarde vers l'intérieur.
  const float *q = SK_PRIM[k];
  float hx = ox + t * dx, hy = oy + t * dy, hz = oz + t * dz;
  float nx = (hx - q[0]) * skInvR[k][0] * skInvR[k][0];
  float ny = (hy - q[1]) * skInvR[k][1] * skInvR[k][1];
  float nz = (hz - q[2]) * skInvR[k][2] * skInvR[k][2];
  float inv = 1.0f / sqrtf(nx * nx + ny * ny + nz * nz);
  if (inward) inv = -inv;
  nx *= inv; ny *= inv; nz *= inv;

  // 5. L'os. Diffus, un liseré sur la tranche pour décoller la silhouette du
  //    fond noir, et un reflet large et mat.
  float diff = nx * lx + ny * ly + nz * lz;
  if (diff < 0.0f) diff = 0.0f;
  float ndv = -(nx * dx + ny * dy + nz * dz);
  if (ndv < 0.0f) ndv = 0.0f;
  float om = 1.0f - ndv;
  float rim = RIM * om * om * om;

  float ux = lx - dx, uy = ly - dy, uz = lz - dz;
  float ul = 1.0f / sqrtf(ux * ux + uy * uy + uz * uz);
  float spec = nx * ux * ul + ny * uy * ul + nz * uz * ul;
  if (spec > 0.0f) {
    float s2 = spec * spec, s4 = s2 * s2, s8 = s4 * s4;
    spec = s8 * s4 * s2 * 0.22f;             // ^14, sans powf
  } else spec = 0.0f;

  float v = AMBIENT + (1.0f - AMBIENT) * diff + rim + spec;
  if (inward) v *= CAVITY;

#if TEETH
  if (hy > TEETH_LO && hy < TEETH_HI && hz > 0.05f) {
    float a = atan2f(hx, hz + 0.2f) * TEETH_N;
    float f = fabsf(a - roundf(a)) * 4.5f;
    if (f > 1.0f) f = 1.0f;
    v *= 0.55f + 0.45f * f;
  }
#endif

  if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;
  // GAMMA vaut 1 par défaut : le compilateur voit la constante et sort
  // le powf du binaire tout seul.
  if (GAMMA != 1.0f) v = powf(v, GAMMA);
  return v;
}

// ============================================================
//  Une image
// ============================================================
static void drawSkull(float t) {
  float yaw = fullTurn
            ? TWO_PI_F * t / SPIN_S
            : 0.5f * SWEEP_DEG * DEG * sinf(TWO_PI_F * t / SPIN_S);
  float tilt = TILT_DEG * DEG;

  float cy = cosf(yaw),  sy = sinf(yaw);
  float cx = cosf(tilt), sx = sinf(tilt);

  // M fait passer le rayon dans l'espace du modèle. C'est ce qui évite de
  // faire tourner les dix-sept primitives à chaque image.
  float m00 = cy,      m01 = 0.0f, m02 = -sy;
  float m10 = sy * sx, m11 = cx,   m12 =  cy * sx;
  float m20 = sy * cx, m21 = -sx,  m22 =  cy * cx;
  float dx = -m02, dy = -m12, dz = -m22;

  for (int i = 0; i < N_PRIM; i++) {
    const float *q = SK_PRIM[i];
    float ix = 1.0f / q[3], iy = 1.0f / q[4], iz = 1.0f / q[5];
    skInvR[i][0] = ix; skInvR[i][1] = iy; skInvR[i][2] = iz;
    float ax = dx * ix, ay = dy * iy, az = dz * iz;
    skDp[i][0] = ax; skDp[i][1] = ay; skDp[i][2] = az;
    float a = ax * ax + ay * ay + az * az;
    skA[i] = a;
    skInvA[i] = 1.0f / a;
  }

  // La lumière est fixe dans la pièce : on la ramène, elle aussi, dans
  // l'espace du modèle.
  float le = LIGHT_EL * DEG, la = LIGHT_AZ * DEG;
  float wx = cosf(le) * sinf(la), wy = sinf(le), wz = cosf(le) * cosf(la);
  float lx = m00 * wx + m01 * wy + m02 * wz;
  float ly = m10 * wx + m11 * wy + m12 * wz;
  float lz = m20 * wx + m21 * wy + m22 * wz;

  const float sc = 1.0f / ZOOM_PX;
  const float half = SCREEN_W * 0.5f;
  const int step = halfRes ? 2 : 1;

  // sx, sy comptent les rayons, pas les pixels : c'est sur eux qu'on indexe
  // la trame. Sinon, en demi-résolution, on ne tirerait qu'un seuil sur
  // quatre — tous du côté sombre — et l'image partirait en blanc.
  int rayY = 0;
  for (int py = 0; py < SCREEN_H; py += step, rayY++) {
    float sv = -((float)py + 0.5f - half) * sc;
    int rayX = 0;
    for (int px = 0; px < SCREEN_W; px += step, rayX++) {
      float su = ((float)px + 0.5f - half) * sc;

      float v = 0.0f;
      if (su * su + sv * sv < BOUND_R * BOUND_R) {
        v = shade(m00 * su + m01 * sv + m02 * Z0,
                  m10 * su + m11 * sv + m12 * Z0,
                  m20 * su + m21 * sv + m22 * Z0,
                  dx, dy, dz, lx, ly, lz);
      }

      // Trame ordonnée : le seuil change d'un rayon à l'autre.
      float th = ((float)BAYER[((rayY & BAY_MASK) << BAY_SHIFT) | (rayX & BAY_MASK)] + 0.5f) * BAY_DIV;
      uint16_t col = (v > th) ? TFT_WHITE : TFT_BLACK;

      if (step == 1) {
        fb[py * SCREEN_W + px] = col;
      } else {
        uint16_t *r0 = fb + py * SCREEN_W + px;
        r0[0] = col; r0[1] = col;
        r0[SCREEN_W] = col; r0[SCREEN_W + 1] = col;
      }
    }
  }
  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
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
  tft.setSwapBytes(true);

#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
#endif

  lastFrameUs = micros();
}

// ============================================================
//  Boutons (debounce simple, pas de lib externe)
// ============================================================
static void pollButtons() {
  uint32_t now = millis();
  if (now - lastBtnMs < BTN_DEBOUNCE_MS) return;

  if (digitalRead(PIN_BTN_LEFT) == LOW) {
    halfRes = !halfRes;
    lastBtnMs = now;
  } else if (digitalRead(PIN_BTN_RIGHT) == LOW) {
    fullTurn = !fullTurn;
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
  if (dt > 0.1f) dt = 0.1f;

  pollButtons();
  clockT += dt;
  drawSkull(clockT);

  uint32_t budget = 1000000UL / TARGET_FPS;
  uint32_t spent  = micros() - nowUs;
  if (spent < budget) delayMicroseconds(budget - spent);
}
