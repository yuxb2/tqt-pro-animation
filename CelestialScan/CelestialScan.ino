/*
 * CelestialScan — LilyGO T-QT Pro (ESP32-S3, GC9A01 128x128)
 * ----------------------------------------------------------
 * Un corps céleste qu'on ausculte : sphère en pointillés, parcourue de
 * courbes de niveau blanches, avec des relevés de température et de
 * coordonnées dans les coins.
 *
 * Les courbes ne sont pas dessinées à la main : un champ scalaire vit sur
 * la sphère (somme de quelques ondes concentriques autour d'axes qui
 * précessent), et on en extrait les isolignes par marching squares. C'est
 * ce qui donne les boucles emboîtées et les creux en spirale — la même
 * mécanique qu'une carte isobarique, d'où les repères H et L.
 *
 * Les chiffres ne sont pas décoratifs non plus : les coordonnées sont le
 * point de la sphère qui nous fait face, et la température est la valeur
 * du champ à cet endroit. Tout bouge donc ensemble.
 *
 * Libs : TFT_eSPI (version LilyGO du dépôt, Setup211) — rien d'autre.
 *
 * Boutons :
 *   IO00 (BOOT) → nouveau relevé (le champ est retiré au sort)
 *   IO47 (KEY)  → densité des courbes : moyenne / fine / grossière
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include <math.h>

// ============================================================
//  CONFIG — tout se règle ici
// ============================================================

// ---- La sphère ---------------------------------------------
#define SPHERE_R       48.0f     // rayon en pixels
#define SPIN_DPS       12.0f     // °/s ; tour complet en 30 s
#define TILT_DEG       14.0f     // inclinaison moyenne
#define TILT_WOBBLE    12.0f     // amplitude du balancement de l'axe
#define TILT_PERIOD    47.0f     // période de ce balancement (s)

// ---- Le champ scalaire -------------------------------------
#define N_TERMS        4         // nb d'ondes qui composent le relief
#define FIELD_DRIFT    0.23f     // vitesse de déformation du champ (rad/s)
#define OMEGA_MIN      1.9f      // fréquences : bas = grandes plages
#define OMEGA_MAX      4.3f      //              haut = motif serré

// ---- Les courbes de niveau ---------------------------------
#define N_LON          96        // maillage en longitude
#define N_LAT          48        // maillage en latitude
#define LEVELS_MAX     22
#define DETAIL_START   0         // 0 = moyen, 1 = fin, 2 = grossier
#define LIMB_Z         0.10f     // marge au bord, sinon les courbes s'y empilent
#define SHOW_HL        1         // repères H / L sur les extrema

// ---- Le maillage en pointillés ------------------------------
#define GRAT_LON_STEP  30        // méridiens, en degrés
#define GRAT_LAT_STEP  30        // parallèles, en degrés
#define GRAT_DOT_GAP   3         // 1 point tous les N pas (plus haut = plus aéré)

// ---- Les relevés --------------------------------------------
#define SHOW_HUD       1
#define TEMP_BASE      -78.0f    // °C au repos
#define TEMP_SPAN      9.0f      // écart apporté par le champ

// ---- Rendu ---------------------------------------------------
#define TARGET_FPS     25
#define COL_GRAT_LEVEL 150       // gris des pointillés (0..255)

// ---- Boutons -------------------------------------------------
#define PIN_BTN_LEFT   0         // BOOT
#define PIN_BTN_RIGHT  47        // KEY
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

#define TWO_PI_F 6.2831853f
#define DEG2RAD  0.017453293f
#define DEG_CHAR 0xF8            // le petit rond de la police 1 (CP437)

static uint16_t COL_GRAT;

// ============================================================
//  Sinus tabulé — le champ en réclame ~20 000 par image
// ============================================================
#define SIN_N 1024
static float sinTab[SIN_N + 1];

static inline float fastSin(float a) {
  float u = a * (SIN_N / TWO_PI_F);
  int32_t i = (int32_t)floorf(u);
  float f = u - (float)i;
  uint32_t k = (uint32_t)(i & (SIN_N - 1));
  float s0 = sinTab[k], s1 = sinTab[k + 1];
  return s0 + (s1 - s0) * f;
}

// ============================================================
//  Le maillage
// ============================================================
#define NL (N_LON + 1)
#define NA (N_LAT + 1)

static float cosLat[NA], sinLat[NA];
static float cosLon[NL], sinLon[NL];    // longitudes du repère sphère
static float cosLonS[NL], sinLonS[NL];  // les mêmes, une fois la sphère tournée

static float fld[NA * NL];              // le champ, ~19 ko

// Deux rangées projetées suffisent : marching squares n'a jamais besoin que
// de la rangée courante et de la suivante. En tableaux parallèles plutôt qu'en
// struct, parce que le préprocesseur .ino remonte les prototypes tout en haut
// du fichier, avant toute déclaration de type faite ici.
static float rowSX[2][NL], rowSY[2][NL], rowZ[2][NL];

// ============================================================
//  Les termes du champ
// ============================================================
struct Term {
  float amp, omega;
  float ax, ay, az;      // axe de l'onde
  float precess;         // vitesse de précession de cet axe (rad/s)
  float phase;
};
static Term terms[N_TERMS];

static void seedField() {
  for (int k = 0; k < N_TERMS; k++) {
    float x = (float)random(-1000, 1001) / 1000.0f;
    float y = (float)random(-1000, 1001) / 1000.0f;
    float z = (float)random(-1000, 1001) / 1000.0f;
    float n = sqrtf(x * x + y * y + z * z);
    if (n < 0.05f) { x = 0; y = 0; z = 1; n = 1; }
    terms[k].ax = x / n;
    terms[k].ay = y / n;
    terms[k].az = z / n;
    terms[k].amp = 0.45f + (float)random(0, 601) / 1000.0f;
    terms[k].omega = OMEGA_MIN + (OMEGA_MAX - OMEGA_MIN) * (float)random(0, 1001) / 1000.0f;
    terms[k].precess = ((float)random(-140, 141)) / 1000.0f;
    terms[k].phase = (float)random(0, 6283) / 1000.0f;
  }
}

// ============================================================
//  Densité des courbes
// ============================================================
static const uint8_t DETAIL_COUNT[3] = { 14, 20, 8 };
static const float   DETAIL_STEP[3]  = { 0.36f, 0.25f, 0.62f };
static uint8_t detail = DETAIL_START;
static float   levels[LEVELS_MAX];
static uint8_t levelCount;

static void buildLevels() {
  levelCount = DETAIL_COUNT[detail];
  float step = DETAIL_STEP[detail];
  for (int k = 0; k < levelCount; k++) {
    levels[k] = ((float)k - (levelCount - 1) * 0.5f) * step;
  }
}

// ============================================================
//  État
// ============================================================
static float    clockT = 0.0f;
static float    tiltNow = TILT_DEG;
static float    CT, ST;
static uint32_t lastFrameUs = 0;
static uint32_t lastBtnMs = 0;

// ============================================================
//  Le champ, recalculé à chaque image
//  x·d se factorise : à latitude fixe, seule la part en longitude change,
//  donc on la sort de la boucle interne.
// ============================================================
static float projLon[NL];

static void computeField(float t) {
  for (int i = 0; i < NA * NL; i++) fld[i] = 0.0f;

  for (int k = 0; k < N_TERMS; k++) {
    Term &tm = terms[k];
    float a = tm.precess * t;
    float ca = cosf(a), sa = sinf(a);
    float dx = tm.ax * ca - tm.az * sa;
    float dy = tm.ay;
    float dz = tm.ax * sa + tm.az * ca;
    float ph = tm.phase + FIELD_DRIFT * t;

    for (int i = 0; i < NL; i++) projLon[i] = cosLon[i] * dx + sinLon[i] * dz;

    for (int j = 0; j < NA; j++) {
      float base = sinLat[j] * dy;
      float cla  = cosLat[j];
      float *row = &fld[j * NL];
      for (int i = 0; i < NL; i++) {
        row[i] += tm.amp * fastSin(tm.omega * (base + cla * projLon[i]) + ph);
      }
    }
  }
}

// ============================================================
//  Projection d'une rangée de latitude
// ============================================================
static void projectRow(int j, int slot) {
  float cla = cosLat[j], sla = sinLat[j];
  for (int i = 0; i < NL; i++) {
    float x = cla * cosLonS[i];
    float z = cla * sinLonS[i];
    float ys = sla * CT - z * ST;
    rowSX[slot][i] = CX + SPHERE_R * x;
    rowSY[slot][i] = CY - SPHERE_R * ys;
    rowZ[slot][i]  = sla * ST + z * CT;
  }
}

// ============================================================
//  Marching squares
//  Coins : v0 haut-gauche, v1 haut-droite, v2 bas-droite, v3 bas-gauche.
//  Arêtes : e0 haut, e1 droite, e2 bas, e3 gauche.
// ============================================================
static const int8_t MS_SEG[16][4] = {
  {-1,-1,-1,-1}, { 3, 0,-1,-1}, { 0, 1,-1,-1}, { 3, 1,-1,-1},
  { 1, 2,-1,-1}, { 3, 0, 1, 2}, { 0, 2,-1,-1}, { 3, 2,-1,-1},
  { 2, 3,-1,-1}, { 2, 0,-1,-1}, { 0, 1, 2, 3}, { 2, 1,-1,-1},
  { 1, 3,-1,-1}, { 1, 0,-1,-1}, { 0, 3,-1,-1}, {-1,-1,-1,-1}
};

// cx[4] / cy[4] = les quatre coins de la cellule dans l'ordre v0..v3,
// v[4] la valeur du champ à ces coins.
static inline void edgePoint(int e, const float *cx, const float *cy, const float *v,
                             float L, float *ox, float *oy) {
  int a = e;                 // e0: v0->v1, e1: v1->v2, e2: v2->v3, e3: v3->v0
  int b = (e + 1) & 3;
  float dv = v[b] - v[a];
  float t = (fabsf(dv) < 1e-9f) ? 0.5f : (L - v[a]) / dv;
  if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
  *ox = cx[a] + (cx[b] - cx[a]) * t;
  *oy = cy[a] + (cy[b] - cy[a]) * t;
}

static void drawContours() {
  int cur = 0;
  projectRow(0, 0);

  for (int j = 0; j < N_LAT; j++) {
    int nxt = cur ^ 1;
    projectRow(j + 1, nxt);

    const float *fa = &fld[j * NL];
    const float *fb = &fld[(j + 1) * NL];

    for (int i = 0; i < N_LON; i++) {
      float za = rowZ[cur][i],     zb = rowZ[cur][i + 1];
      float zc = rowZ[nxt][i + 1], zd = rowZ[nxt][i];
      if (za <= LIMB_Z || zb <= LIMB_Z || zc <= LIMB_Z || zd <= LIMB_Z) continue;

      float v0 = fa[i], v1 = fa[i + 1], v2 = fb[i + 1], v3 = fb[i];
      float lo = v0, hi = v0;
      if (v1 < lo) lo = v1;
      if (v1 > hi) hi = v1;
      if (v2 < lo) lo = v2;
      if (v2 > hi) hi = v2;
      if (v3 < lo) lo = v3;
      if (v3 > hi) hi = v3;

      for (int k = 0; k < levelCount; k++) {
        float L = levels[k];
        if (L <= lo || L > hi) continue;
        int idx = (v0 > L ? 1 : 0) | (v1 > L ? 2 : 0) | (v2 > L ? 4 : 0) | (v3 > L ? 8 : 0);
        const int8_t *seg = MS_SEG[idx];
        if (seg[0] < 0) continue;
        const float cx[4] = { rowSX[cur][i], rowSX[cur][i + 1], rowSX[nxt][i + 1], rowSX[nxt][i] };
        const float cy[4] = { rowSY[cur][i], rowSY[cur][i + 1], rowSY[nxt][i + 1], rowSY[nxt][i] };
        const float vv[4] = { v0, v1, v2, v3 };
        for (int s = 0; s < 4; s += 2) {
          if (seg[s] < 0) break;
          float x0, y0, x1, y1;
          edgePoint(seg[s],     cx, cy, vv, L, &x0, &y0);
          edgePoint(seg[s + 1], cx, cy, vv, L, &x1, &y1);
          spr.drawLine((int)x0, (int)y0, (int)x1, (int)y1, TFT_WHITE);
        }
      }
    }
    cur = nxt;
  }
}

// ============================================================
//  Le maillage en pointillés
// ============================================================
static void drawGraticule(float spinRad) {
  for (int m = 0; m * GRAT_LON_STEP < 360; m++) {
    float lon = m * GRAT_LON_STEP * DEG2RAD + spinRad;
    float cl = cosf(lon), sl = sinf(lon);
    for (int k = 0; k <= 120; k += GRAT_DOT_GAP) {
      float lat = -1.5707963f + 3.1415927f * k / 120.0f;
      float cla = cosf(lat), sla = sinf(lat);
      float z = cla * sl;
      float ys = sla * CT - z * ST;
      float zs = sla * ST + z * CT;
      if (zs <= 0.0f) continue;
      spr.drawPixel((int)(CX + SPHERE_R * cla * cl), (int)(CY - SPHERE_R * ys), COL_GRAT);
    }
  }
  for (int lat = -90 + GRAT_LAT_STEP; lat <= 90 - GRAT_LAT_STEP; lat += GRAT_LAT_STEP) {
    float la = lat * DEG2RAD;
    float cla = cosf(la), sla = sinf(la);
    for (int k = 0; k < 180; k += GRAT_DOT_GAP) {
      float lon = TWO_PI_F * k / 180.0f + spinRad;
      float z = cla * sinf(lon);
      float ys = sla * CT - z * ST;
      float zs = sla * ST + z * CT;
      if (zs <= 0.0f) continue;
      spr.drawPixel((int)(CX + SPHERE_R * cla * cosf(lon)), (int)(CY - SPHERE_R * ys), COL_GRAT);
    }
  }
}

// ============================================================
//  Repères H / L sur les quatre extrema les plus marqués
// ============================================================
#define HL_MAX 4
#define HL_MIN_GAP 16     // écart minimum entre deux lettres, en pixels

static void drawExtrema() {
  int   bi[HL_MAX], bj[HL_MAX];
  char  bc[HL_MAX];
  float bs[HL_MAX];
  int   n = 0;

  for (int j = 2; j < N_LAT - 1; j++) {
    const float *row = &fld[j * NL];
    const float *up  = &fld[(j - 1) * NL];
    const float *dn  = &fld[(j + 1) * NL];
    for (int i = 0; i < N_LON; i++) {
      int ip = (i + 1) % N_LON, im = (i + N_LON - 1) % N_LON;
      float v = row[i];
      char  ch = 0;
      if (v > row[ip] && v > row[im] && v > up[i] && v > dn[i]) ch = 'H';
      else if (v < row[ip] && v < row[im] && v < up[i] && v < dn[i]) ch = 'L';
      if (!ch) continue;

      float score = fabsf(v);
      int slot = -1;
      if (n < HL_MAX) { slot = n++; }
      else {
        int worst = 0;
        for (int k = 1; k < HL_MAX; k++) if (bs[k] < bs[worst]) worst = k;
        if (score > bs[worst]) slot = worst;
      }
      if (slot >= 0) { bi[slot] = i; bj[slot] = j; bc[slot] = ch; bs[slot] = score; }
    }
  }

  int placedX[HL_MAX], placedY[HL_MAX], placed = 0;
  for (int k = 0; k < n; k++) {
    float cla = cosLat[bj[k]], sla = sinLat[bj[k]];
    float x = cla * cosLonS[bi[k]];
    float z = cla * sinLonS[bi[k]];
    float ys = sla * CT - z * ST;
    float zs = sla * ST + z * CT;
    if (zs < 0.35f) continue;               // trop près du bord, illisible
    int px = (int)(CX + SPHERE_R * x);
    int py = (int)(CY - SPHERE_R * ys);

    // deux extrema voisins donneraient deux lettres collées : on n'en garde qu'une
    bool crowded = false;
    for (int q = 0; q < placed; q++) {
      if (abs(px - placedX[q]) < HL_MIN_GAP && abs(py - placedY[q]) < HL_MIN_GAP) {
        crowded = true;
        break;
      }
    }
    if (crowded) continue;
    placedX[placed] = px;
    placedY[placed] = py;
    placed++;

    spr.fillRect(px - 3, py - 4, 7, 9, TFT_BLACK);   // on dégage la lettre
    spr.drawChar((uint8_t)bc[k], px - 2, py - 3, 1);
  }
}

// ============================================================
//  Les relevés
// ============================================================
static int hudText(int x, int y, const char *s) {
  while (*s) {
    spr.drawChar((uint8_t)*s++, x, y, 1);
    x += 6;
  }
  return x;
}

static void formatDM(char *out, float deg, char pos, char neg) {
  char h = (deg >= 0.0f) ? pos : neg;
  float v = fabsf(deg);
  int d = (int)v;
  int m = (int)((v - d) * 60.0f);
  sprintf(out, "%d%c%02d'%c", d, DEG_CHAR, m, h);
}

static void drawHud(float spinDeg) {
  char buf[16];

  // le point de la sphère qui nous fait face
  float subLat = tiltNow;
  float subLon = -spinDeg;
  while (subLon < -180.0f) subLon += 360.0f;
  while (subLon >= 180.0f) subLon -= 360.0f;

  int j = (int)((90.0f - subLat) / 180.0f * N_LAT);
  if (j < 0) j = 0; else if (j > N_LAT) j = N_LAT;
  int i = (int)((subLon + 180.0f) / 360.0f * N_LON);
  if (i < 0) i = 0; else if (i >= N_LON) i = N_LON - 1;

  float degC = TEMP_BASE + TEMP_SPAN * fld[j * NL + i];
  float degF = degC * 9.0f / 5.0f + 32.0f;

  sprintf(buf, "%.1f%cC", degC, DEG_CHAR);
  hudText(1, 1, buf);
  sprintf(buf, "%.1f%cF", degF, DEG_CHAR);
  hudText(SCREEN_W - 1 - 6 * (int)strlen(buf), 1, buf);

  formatDM(buf, subLat, 'N', 'S');
  hudText(1, SCREEN_H - 8, buf);
  formatDM(buf, subLon, 'E', 'W');
  hudText(SCREEN_W - 1 - 6 * (int)strlen(buf), SCREEN_H - 8, buf);

  hudText(CX - 2, 1, "N");
  hudText(CX - 2, SCREEN_H - 8, "S");
  hudText(1, CY - 4, "W");
  hudText(SCREEN_W - 6, CY - 4, "E");
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

  COL_GRAT = tft.color565(COL_GRAT_LEVEL, COL_GRAT_LEVEL, COL_GRAT_LEVEL);

  spr.setColorDepth(16);
  if (!spr.createSprite(SCREEN_W, SCREEN_H)) {
    tft.setTextColor(TFT_RED);
    tft.drawString("Sprite alloc failed", 4, 60, 2);
    while (true) delay(1000);
  }
  spr.setAttribute(CP437_SWITCH, 1);   // pour que 0xF8 donne bien le petit rond
  spr.setTextColor(TFT_WHITE);         // fond = couleur → texte transparent
  spr.setTextSize(1);

  for (int k = 0; k <= SIN_N; k++) sinTab[k] = sinf(TWO_PI_F * k / SIN_N);

  for (int j = 0; j < NA; j++) {
    float lat = 1.5707963f - 3.1415927f * j / N_LAT;
    cosLat[j] = cosf(lat);
    sinLat[j] = sinf(lat);
  }
  for (int i = 0; i < NL; i++) {
    float lon = TWO_PI_F * i / N_LON;
    cosLon[i] = cosf(lon);
    sinLon[i] = sinf(lon);
  }

  randomSeed(esp_random());
  seedField();
  buildLevels();
  lastFrameUs = micros();
}

// ============================================================
//  Boutons
// ============================================================
static void pollButtons() {
  uint32_t now = millis();
  if (now - lastBtnMs < BTN_DEBOUNCE_MS) return;

  if (digitalRead(PIN_BTN_LEFT) == LOW) {
    seedField();
    lastBtnMs = now;
  } else if (digitalRead(PIN_BTN_RIGHT) == LOW) {
    detail = (detail + 1) % 3;
    buildLevels();
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
  clockT += dt;

  pollButtons();

  tiltNow = TILT_DEG + TILT_WOBBLE * sinf(TWO_PI_F * clockT / TILT_PERIOD);
  float tr = tiltNow * DEG2RAD;
  CT = cosf(tr);
  ST = sinf(tr);

  float spinDeg = SPIN_DPS * clockT;
  spinDeg -= 360.0f * floorf(spinDeg / 360.0f);
  float spinRad = spinDeg * DEG2RAD;

  float cs = cosf(spinRad), ss = sinf(spinRad);
  for (int i = 0; i < NL; i++) {
    cosLonS[i] = cosLon[i] * cs - sinLon[i] * ss;
    sinLonS[i] = cosLon[i] * ss + sinLon[i] * cs;
  }

  computeField(clockT);

  spr.fillSprite(TFT_BLACK);
  drawGraticule(spinRad);
  drawContours();
#if SHOW_HL
  drawExtrema();
#endif
#if SHOW_HUD
  drawHud(spinDeg);
#endif
  spr.pushSprite(0, 0);

  uint32_t budget = 1000000UL / TARGET_FPS;
  uint32_t spent  = micros() - nowUs;
  if (spent < budget) delayMicroseconds(budget - spent);
}
