/*
 * HypnoEye — LilyGO T-QT Pro (ESP32-S3, GC9A01 128x128)
 * -----------------------------------------------------
 * Œil op-art : grosse pupille noire, anneaux concentriques autour, et des
 * bandes qui épousent la paupière puis s'aplatissent vers les bords.
 *
 * Rien n'est dessiné trait par trait. Chaque pixel calcule une « phase »,
 * et la bande est blanche ou noire selon la partie fractionnaire de cette
 * phase. Tout le dessin est donc dans la façon dont la phase est construite :
 *
 *   - dans l'œil   : phase = distance à la pupille, penchée par le regard,
 *   - en dehors    : phase = distance à la paupière, décalée dans le temps.
 *
 * C'est ce qui rend le regard presque gratuit : décaler la pupille et pencher
 * la phase suffit à resserrer les anneaux du côté visé et à les écarter de
 * l'autre, sans replacer une seule courbe. Ce penchement est accroché au bord
 * de la pupille — nul là-bas, croissant vers l'extérieur — pour que rien ne
 * bouge tant que la pupille ne bouge pas.
 *
 * La paupière, elle, est tracée à part : c'est le seul trait qui reste en
 * place pendant que tout le reste dérive vers elle.
 *
 * Libs : TFT_eSPI (version LilyGO du dépôt, Setup211) — rien d'autre.
 *
 * Boutons :
 *   IO00 (BOOT) → épaisseur des bandes : moyenne / fine / large
 *   IO47 (KEY)  → fige ou relance le mouvement vers l'œil
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include <math.h>

// ============================================================
//  CONFIG — tout se règle ici
// ============================================================

// ---- La forme de l'œil -------------------------------------
#define W_LENS     60.0f    // demi-largeur : les coins sont à 64 ± celle-ci
#define H_LENS     30.0f    // demi-hauteur au milieu
#define CORNER_P   1.6f     // 1 = coin pointu ; au-dessus, l'angle s'arrondit
#define R_PUPIL    17.0f    // rayon de la pupille

// ---- Les bandes ---------------------------------------------
#define PITCH_MID  13.0f    // une bande noire + une blanche, en pixels
#define PITCH_FINE 9.0f
#define PITCH_WIDE 16.0f
#define DUTY       0.18f    // >0 = trait blanc plus épais que le noir
#define AA_GAIN    0.125f   // douceur du bord ; plus bas = plus flou
// Le trait de paupière et son épaule noire se déduisent de DUTY : la paupière
// porte donc exactement le poids de tous les autres traits de l'image.

// ---- Le regard ----------------------------------------------
#define GAZE_AMP   5.0f     // débattement de la pupille, en pixels
#define GAZE_P1    11.0f    // deux périodes sans rapport simple, pour que
#define GAZE_P2    7.3f     // le va-et-vient ne se répète pas à l'œil
#define K_MAX      0.22f    // resserrement des anneaux du côté regardé

// ---- Le flux ------------------------------------------------
#define FLOW       7.0f     // px/s ; les bandes extérieures rentrent vers l'œil

// ---- Rendu ---------------------------------------------------
#define TARGET_FPS 30

// ---- Boutons -------------------------------------------------
#define PIN_BTN_LEFT   0    // BOOT
#define PIN_BTN_RIGHT  47   // KEY
#define BTN_DEBOUNCE_MS 220

// ============================================================
//  Écran
// ============================================================
TFT_eSPI tft = TFT_eSPI();

#define SCREEN_W 128
#define SCREEN_H 128
#define CX 64.0f
#define CY 64.0f
#define TWO_PI_F 6.2831853f

static uint16_t fb[SCREEN_W * SCREEN_H];
static uint16_t greyLut[32];

// La paupière ne dépend que de x et ne bouge jamais : sa hauteur et la pente
// qui va avec sont calculées une fois pour toutes, colonne par colonne.
static float lensH[SCREEN_W];
static float lensInvG[SCREEN_W];

// ============================================================
//  État
// ============================================================
static const float pitchTable[3] = { PITCH_MID, PITCH_FINE, PITCH_WIDE };
static uint8_t  pitchMode = 0;
static bool     flowing   = true;
static float    clockT    = 0.0f;
static float    flowT     = 0.0f;
static uint32_t lastFrameUs = 0;
static uint32_t lastBtnMs   = 0;

// ============================================================
//  Tables
// ============================================================
static void buildTables() {
  for (int i = 0; i < 32; i++) {
    uint8_t v = (uint8_t)(i * 255 / 31);
    greyLut[i] = tft.color565(v, v, v);
  }
  for (int x = 0; x < SCREEN_W; x++) {
    float u = (float)x + 0.5f - CX;
    float s = u / W_LENS;
    if (fabsf(s) < 1.0f) {
      float base = 1.0f - s * s;
      lensH[x] = H_LENS * powf(base, CORNER_P);
      float slope = H_LENS * CORNER_P * powf(base, CORNER_P - 1.0f)
                  * (-2.0f * u / (W_LENS * W_LENS));
      lensInvG[x] = 1.0f / sqrtf(1.0f + slope * slope);
    } else {
      lensH[x] = 0.0f;
      lensInvG[x] = 1.0f;
    }
  }
}

// ============================================================
//  Une image
// ============================================================
static void drawEye(float t) {
  float gaze = GAZE_AMP * (0.7f * sinf(TWO_PI_F * t / GAZE_P1)
                         + 0.3f * sinf(TWO_PI_F * t / GAZE_P2));
  float k  = K_MAX * gaze / GAZE_AMP;
  float px = CX + gaze;

  float pitch    = pitchTable[pitchMode];
  float invPitch = 1.0f / pitch;
  float edge     = AA_GAIN * pitch;   // (tri + DUTY) * edge / gradient
  float flow     = FLOW * flowT;
  float halfW    = (1.0f + DUTY) * 0.25f;   // demi-largeur d'un trait blanc
  float lidW     = halfW * pitch;           // la paupière est un trait comme un autre
  float lidGap   = (0.5f - halfW) * pitch;  // et garde l'écart noir habituel
  // Début du trait blanc : le premier anneau vient alors buter contre la
  // pupille sur toute sa largeur, au lieu d'être coupé en deux par elle.
  float phaseIn  = 0.5f - halfW;

  uint16_t *out = fb;
  for (int y = 0; y < SCREEN_H; y++) {
    float dyc = (float)y + 0.5f - CY;
    float ady = fabsf(dyc);
    for (int x = 0; x < SCREEN_W; x++) {
      float s, invGrad, pupil = 1.0f;

      if (ady <= lensH[x]) {
        // Dans l'œil : anneaux sur la pupille.
        float dx = (float)x + 0.5f - px;
        float r  = sqrtf(dx * dx + dyc * dyc);
        if (r < 0.01f) r = 0.01f;
        float invR  = 1.0f / r;
        float invR3 = invR * invR * invR;
        // Le resserrement est accroché au bord de la pupille : nul là-bas,
        // il grandit vers l'extérieur. Les anneaux ne bougent donc que parce
        // que la pupille bouge, jamais d'eux-mêmes.
        float b  = r + k * dx * (1.0f - R_PUPIL * invR);
        float gx = dx * invR + k * (1.0f - R_PUPIL * dyc * dyc * invR3);
        float gy = dyc * invR + k * R_PUPIL * dx * dyc * invR3;
        invGrad = 1.0f / sqrtf(gx * gx + gy * gy);
        s = (b - R_PUPIL) * invPitch + phaseIn;
        pupil = (b - R_PUPIL) * invGrad;       // bord adouci sur un pixel
      } else {
        // Dehors : bandes parallèles à la paupière, qui glissent vers elle.
        invGrad = lensInvG[x];
        s = (ady - lensH[x] + flow) * invPitch;
      }

      // Onde triangulaire plutôt que créneau : sa pente donne l'anticrénelage
      // gratuitement, et là où les bandes se serrent trop pour l'écran elle
      // s'affaisse vers le gris au lieu de partir en moiré.
      float fr  = s - floorf(s);
      float tri = 1.0f - 2.0f * fabsf(2.0f * fr - 1.0f);
      float v   = 0.5f + (tri + DUTY) * edge * invGrad;
      if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;
      if (pupil < 1.0f) {
        if (pupil < 0.0f) pupil = 0.0f;
        v *= pupil;
      }

      // La seule ligne qui ne bouge jamais : un trait blanc, tenu lisible par
      // une épaule noire de chaque côté, contre laquelle viennent mourir les
      // bandes qui dérivent vers l'œil.
      float dl = fabsf(ady - lensH[x]) * lensInvG[x];
      float shoulder = lidW + lidGap - dl;
      if (shoulder > 0.0f) {
        v *= 1.0f - (shoulder > 1.0f ? 1.0f : shoulder);
        float white = lidW - dl;
        if (white > 0.0f) v = fmaxf(v, white > 1.0f ? 1.0f : white);
      }
      *out++ = greyLut[(int)(v * 31.0f + 0.5f)];
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

  buildTables();
  lastFrameUs = micros();
}

// ============================================================
//  Boutons (debounce simple, pas de lib externe)
// ============================================================
static void pollButtons() {
  uint32_t now = millis();
  if (now - lastBtnMs < BTN_DEBOUNCE_MS) return;

  if (digitalRead(PIN_BTN_LEFT) == LOW) {
    pitchMode = (pitchMode + 1) % 3;
    lastBtnMs = now;
  } else if (digitalRead(PIN_BTN_RIGHT) == LOW) {
    flowing = !flowing;
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

  clockT += dt;                       // le regard continue même en pause
  if (flowing) flowT += dt;

  drawEye(clockT);

  uint32_t budget = 1000000UL / TARGET_FPS;
  uint32_t spent  = micros() - nowUs;
  if (spent < budget) delayMicroseconds(budget - spent);
}
