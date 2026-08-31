/*
 * PixelSkull — LilyGO T-QT Pro (ESP32-S3, écran 128x128)
 * ------------------------------------------------------
 * Une tête de mort en gros pixels traverse l'écran en rebondissant sur les
 * quatre bords, la mâchoire qui claque.
 *
 * Tout part d'un dessin de 16 pixels de côté, écrit en binaire plus bas : on
 * le lit à l'œil dans le code, chaque « 1 » est un pixel d'os. À l'écran il
 * est agrandi d'un facteur entier — un pixel du dessin devient un carré de 4
 * pixels — et sa position est calée sur cette même grille. Rien n'est donc
 * jamais interpolé : le crâne se déplace par carrés entiers, et l'escalier
 * des contours reste exactement celui du dessin, quelle que soit la taille.
 *
 * Le crâne et la mâchoire sont deux dessins séparés. La mâchoire n'est pas
 * articulée : elle descend, d'un nombre entier de pixels du dessin. Bouche
 * fermée les dents du haut et du bas se touchent et n'en font qu'une rangée ;
 * ouverte, il reste un trou noir entre les deux.
 *
 * Le claquement tourne à son rythme, sans rapport avec le rebond : la bouche
 * mâche, et le crâne se promène. BITE_ON_WALL les accroche l'un à l'autre —
 * chaque impact remet alors la mâchoire sur « grand ouvert », et le crâne a
 * l'air de mordre le mur. C'est laissé à 0 : le claquement régulier a le
 * dernier mot.
 *
 * Noir et blanc stricts : deux couleurs, aucun gris, aucun adoucissement.
 *
 * Libs : TFT_eSPI (version LilyGO du dépôt, Setup211) — rien d'autre.
 *
 * Boutons :
 *   IO00 (BOOT) → taille du crâne : x5 / x4 / x3
 *   IO47 (KEY)  → déplacement : rebond / dérive lente
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include <math.h>

// ============================================================
//  CONFIG — tout se règle ici
// ============================================================

// ---- Le dessin ----------------------------------------------
#define SKULL_W    16       // largeur du dessin, en pixels de dessin
#define CRANIUM_H  13       // hauteur du crâne (dents du haut comprises)
#define JAW_H      3        // hauteur de la mâchoire
#define GAP_MAX    3        // ouverture maximale, en pixels de dessin

// ---- La taille ----------------------------------------------
// Un pixel de dessin devient un carré de SCALE pixels écran. Toujours entier :
// c'est ce qui garde les carrés carrés et les bords nets.
#define SCALE_A    5
#define SCALE_B    4
#define SCALE_C    3

// ---- Le claquement -------------------------------------------
#define CHOMP_S    0.75f    // durée d'un cycle ouvre-ferme, en secondes
#define BITE_ON_WALL 0      // 1 = la mâchoire s'ouvre en grand à chaque impact

// ---- Le déplacement -------------------------------------------
#define SPEED_X    14.0f    // px/s. Deux vitesses proches mais inégales : la
#define SPEED_Y    15.0f    // diagonale se décale lentement à chaque tour.
#define DRIFT_PX   17.0f    // mode dérive : les deux périodes, en secondes
#define DRIFT_PY   11.3f

// ---- Rendu ----------------------------------------------------
#define TARGET_FPS 30

// ---- Boutons --------------------------------------------------
#define PIN_BTN_LEFT   0    // BOOT
#define PIN_BTN_RIGHT  47   // KEY
#define BTN_DEBOUNCE_MS 220

// ============================================================
//  Le dessin
// ============================================================
// Bit de poids fort = colonne de gauche. Les motifs se lisent donc tels quels.

static const uint16_t CRANIUM[CRANIUM_H] = {
  0b0000111111110000,   //     ########
  0b0011111111111100,   //   ############
  0b0111111111111110,   //  ##############
  0b1111111111111111,   // ################
  0b1110011111100111,   // ###  ######  ###   orbites
  0b1100001111000011,   // ##    ####    ##
  0b1100001111000011,   // ##    ####    ##
  0b1110011111100111,   // ###  ######  ###
  0b1111111111111111,   // ################
  0b1111111001111111,   // #######  #######   nez
  0b0111110000111110,   //  #####    #####
  0b0011111111111100,   //   ############     pommettes
  0b0010110110110100,   //   # ## ## ## #     dents du haut
};

static const uint16_t JAW[JAW_H] = {
  0b0010110110110100,   //   # ## ## ## #     dents du bas
  0b0011111111111100,   //   ############
  0b0001111111111000,   //    ##########      menton
};

// ============================================================
//  Écran
// ============================================================
TFT_eSPI tft = TFT_eSPI();

#define SCREEN_W 128
#define SCREEN_H 128
#define TWO_PI_F 6.2831853f

static uint16_t fb[SCREEN_W * SCREEN_H];

// ============================================================
//  État
// ============================================================
static const int scaleTable[3] = { SCALE_A, SCALE_B, SCALE_C };
static uint8_t sizeMode  = 0;
static bool    bouncing  = true;     // false = dérive lente

static float px = 0.0f, py = 0.0f;   // coin haut-gauche, en pixels écran
static float vx = SPEED_X, vy = SPEED_Y;
static float chompT = 0.0f;
static float driftT = 0.0f;

static uint32_t lastFrameUs = 0;
static uint32_t lastBtnMs   = 0;

// ============================================================
//  Le terrain de jeu
// ============================================================
// Le crâne ne doit jamais dépasser, mâchoire grande ouverte comprise : la
// hauteur réservée est donc toujours celle de la bouche ouverte au maximum,
// sinon le rebond du bas changerait de place selon le claquement.
static float maxX(int s) { return (float)(SCREEN_W - SKULL_W * s); }
static float maxY(int s) { return (float)(SCREEN_H - (CRANIUM_H + GAP_MAX + JAW_H) * s); }

static void clampToBounds() {
  int s = scaleTable[sizeMode];
  float mx = maxX(s), my = maxY(s);
  if (px < 0.0f) px = 0.0f; else if (px > mx) px = mx;
  if (py < 0.0f) py = 0.0f; else if (py > my) py = my;
}

// ============================================================
//  Dessin
// ============================================================
// Une ligne du dessin, agrandie en carrés de s côtés. Le calage sur la grille
// est fait par l'appelant : ici on ne fait que remplir des carrés.
static void blitRow(uint16_t bits, int ox, int oy, int s) {
  int yEnd = oy + s;
  if (yEnd > SCREEN_H) yEnd = SCREEN_H;
  for (int y = (oy < 0 ? 0 : oy); y < yEnd; y++) {
    uint16_t *line = fb + y * SCREEN_W;
    for (int c = 0; c < SKULL_W; c++) {
      if (!(bits & (0x8000 >> c))) continue;
      int x0 = ox + c * s;
      int xEnd = x0 + s;
      if (xEnd > SCREEN_W) xEnd = SCREEN_W;
      for (int x = (x0 < 0 ? 0 : x0); x < xEnd; x++) line[x] = TFT_WHITE;
    }
  }
}

static void drawFrame(int ox, int oy, int gap, int s) {
  memset(fb, 0x00, sizeof(fb));                     // 0x0000 = noir
  for (int r = 0; r < CRANIUM_H; r++)
    blitRow(CRANIUM[r], ox, oy + r * s, s);
  for (int r = 0; r < JAW_H; r++)
    blitRow(JAW[r], ox, oy + (CRANIUM_H + gap + r) * s, s);
  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
}

// ============================================================
//  Mouvement
// ============================================================
// Renvoie true si un bord a été touché pendant ce pas.
static bool advanceBounce(float dt, int s) {
  bool hit = false;
  float mx = maxX(s), my = maxY(s);

  px += vx * dt;
  if (px < 0.0f)    { px = -px;           vx = -vx; hit = true; }
  else if (px > mx) { px = 2.0f * mx - px; vx = -vx; hit = true; }

  py += vy * dt;
  if (py < 0.0f)    { py = -py;           vy = -vy; hit = true; }
  else if (py > my) { py = 2.0f * my - py; vy = -vy; hit = true; }

  clampToBounds();   // au cas où un changement de taille vienne de tout réduire
  return hit;
}

static void advanceDrift(float dt, int s) {
  driftT += dt;
  float mx = maxX(s), my = maxY(s);
  px = 0.5f * mx * (1.0f + sinf(TWO_PI_F * driftT / DRIFT_PX));
  py = 0.5f * my * (1.0f + sinf(TWO_PI_F * driftT / DRIFT_PY));
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

  px = maxX(scaleTable[sizeMode]) * 0.5f;
  py = maxY(scaleTable[sizeMode]) * 0.5f;
  lastFrameUs = micros();
}

// ============================================================
//  Boutons (debounce simple, pas de lib externe)
// ============================================================
static void pollButtons() {
  uint32_t now = millis();
  if (now - lastBtnMs < BTN_DEBOUNCE_MS) return;

  if (digitalRead(PIN_BTN_LEFT) == LOW) {
    sizeMode = (sizeMode + 1) % 3;
    clampToBounds();
    lastBtnMs = now;
  } else if (digitalRead(PIN_BTN_RIGHT) == LOW) {
    bouncing = !bouncing;
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

  int s = scaleTable[sizeMode];
  bool hit = false;
  if (bouncing) hit = advanceBounce(dt, s);
  else          advanceDrift(dt, s);

  chompT += dt;
  // BITE_ON_WALL, à 1 : on ne déclenche pas une animation à part, on remet la
  // phase du claquement là où la bouche est grande ouverte, pile sur l'impact.
  if (hit && BITE_ON_WALL) chompT = CHOMP_S * 0.5f;
  if (chompT > CHOMP_S) chompT -= CHOMP_S;

  float open = 0.5f - 0.5f * cosf(TWO_PI_F * chompT / CHOMP_S);
  int gap = (int)(open * GAP_MAX + 0.5f);

  // Calage sur la grille : le crâne ne se pose que sur des multiples de s.
  int ox = ((int)(px + 0.5f) / s) * s;
  int oy = ((int)(py + 0.5f) / s) * s;
  drawFrame(ox, oy, gap, s);

  uint32_t budget = 1000000UL / TARGET_FPS;
  uint32_t spent  = micros() - nowUs;
  if (spent < budget) delayMicroseconds(budget - spent);
}
