/*
 * Cyber Cycle - twelve cyberpunk animations on a 15-minute rotation
 *
 *   0 Morph     spinning wireframe solid with RGB chroma split (cube/octa)
 *   1 Sphere    "Sphere pointillisme 1": RGB-split dotted wireframe globe,
 *               latitude rings that slide down the sphere and grow in number
 *   2 Waves     a sheet of horizontal lines rolling under three printings of
 *               itself that never quite register: alternating slow waves,
 *               white where the channels agree and split RGB where they do not
 *   3 Buddha    Buddha face spelled out by a cloud of outward-drifting dust
 *   4 Swarm     dense flock painted as fading RGB wakes, wrapped sideways
 *   5 Mosaic    20x20 grid of flat colour tiles: two square wave-fronts
 *               beating against the tile grid, each read at a slightly
 *               different pitch by red, green and blue
 *   6 SphereColor  rotating point sphere with flowing rainbow colour fields
 *   7 Scan      celestial scan: a sphere carved up by the isolines of a
 *               field that lives on it, with temperature and coordinate
 *               readouts wired to the point facing the viewer
 *   8 Eye       stencil eye: blinks, glitches, red/white paper cycle,
 *               cycles its own mood so it varies untouched
 *   9 Tunnel    a square corridor flown forwards, off-axis: rings swell out
 *               past the viewer while new ones open at the vanishing point,
 *               red where they are alone and orange where they pile up
 *  10 World     wireframe globe with solid continents and an orbital ring,
 *               wrapped in a phrase that runs twice around the screen
 *  11 Hypno     op-art eye: rings on a black pupil that tighten on the side
 *               it looks at, ringed by bands drifting in towards the lid
 *
 * Left button  : next animation (resets its 15-minute timer)
 * Right button : per-animation variant - palette, shape, figure, mood, etc.
 *
 * Everything shares one static framebuffer. The celestial scan is the only
 * view that also writes to the panel directly, for its text: the framebuffer
 * has no font.
 *
 * Hardware: LilyGo T-QT Pro (ESP32-S3, 128x128, TFT_eSPI Setup211)
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <time.h>
#include "OneButton.h"
#include "pb_map.h"
#include "vecfont.h"     // stroke font for the World Ring lettering
#include "world_map.h"   // coarse land mask for the World Ring globe

#if __has_include("secrets.h")
  #include "secrets.h"
#endif

// A secrets.h written before multi-network support only defines the one pair.
// Fold it into the list so it keeps working untouched.
#if !defined(OTA_WIFI_NETWORKS) && defined(OTA_WIFI_SSID)
  #define OTA_WIFI_NETWORKS { OTA_WIFI_SSID, OTA_WIFI_PASSWORD },
#endif
#ifndef OTA_WIFI_NETWORKS
  // Copy secrets.example.h to secrets.h and enter the Wi-Fi details there.
  #define OTA_WIFI_NETWORKS { "", "" },
#endif

// Every network the board may meet. Empty entries are ignored, so a list with
// one real network behaves exactly as before.
struct OtaNetwork { const char *ssid; const char *password; };
static const OtaNetwork otaNetworks[] = { OTA_WIFI_NETWORKS };
static const int OTA_NETWORK_COUNT = sizeof(otaNetworks) / sizeof(otaNetworks[0]);

TFT_eSPI tft = TFT_eSPI();

#define SCREEN_W 128
#define SCREEN_H 128

static uint16_t fb[SCREEN_W * SCREEN_H];   // 32 KB main framebuffer

#define PIN_BTN_L 0
#define PIN_BTN_R 47
OneButton btnLeft(PIN_BTN_L, true, true);
OneButton btnRight(PIN_BTN_R, true, true);

#define NUM_ANIMS 12
#define ANIM_MS   (15UL * 60UL * 1000UL)   // 15 minutes per animation

// How many variants each animation cycles through on the right button
static const int variantCount[NUM_ANIMS] = { 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 };

// ===================================================================
//  VIEW ORDER  -  this is the scheduler: reorder the views here.
//  View ids:  0 Morph   1 Grid    2 Waves   3 Buddha  4 Swarm
//             5 Mosaic  6 SphereColor  7 Scan    8 Eye    9 Tunnel
//            10 World   11 Hypno
//  Four of these are spheres (1, 6, 7, 10), so the order below spaces them
//  out rather than running them back to back.
//  Edit this list to change the running order. Entries may be removed
//  or repeated; the cycle just walks the list and wraps around.
// ===================================================================
static int viewOrder[] = { 8, 0, 7, 2, 1, 3, 11, 4, 10, 5, 6, 9 };
static const int N_VIEWS = sizeof(viewOrder) / sizeof(viewOrder[0]);

int   slot = 0;               // index into viewOrder = the current view
int   variant[NUM_ANIMS] = {0};
unsigned long animStart = 0;
unsigned long lastFrameTime = 0;
float animTime = 0;                        // seconds since this animation began

// ===================================================================
// OTA — GitHub-hosted firmware
// ===================================================================
// The GitHub Action replaces this value with the release tag before building.
#define FW_VERSION "0.0.0"
#define OTA_MANIFEST_URL "https://raw.githubusercontent.com/yuxb2/tqt-pro-animation/firmware/ota-manifest.txt"
#define OTA_CHECK_INTERVAL_MS (24UL * 60UL * 60UL * 1000UL)
#define OTA_WIFI_TIMEOUT_MS 12000UL

// ISRG Root X1, the root CA used by raw.githubusercontent.com at the time
// this firmware was configured. HTTPS is verified; do not use setInsecure().
static const char OTA_ROOT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)EOF";

// --- shared helpers -----------------------------------------------------

static inline void px(int x, int y, uint16_t c) {
  if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) fb[x + y * SCREEN_W] = c;
}

static inline uint16_t rgbf(float r, float g, float b) {
  if (r < 0) r = 0; if (r > 1) r = 1;
  if (g < 0) g = 0; if (g > 1) g = 1;
  if (b < 0) b = 0; if (b > 1) b = 1;
  return (((uint16_t)(r * 31)) << 11) | (((uint16_t)(g * 63)) << 5) | ((uint16_t)(b * 31));
}

static inline uint16_t scale565(uint16_t c, float k) {
  return rgbf(((c >> 11) & 0x1F) / 31.0f * k,
              ((c >> 5) & 0x3F) / 63.0f * k,
              (c & 0x1F) / 31.0f * k);
}

// --- OTA helpers -------------------------------------------------------

static unsigned long otaNextCheck = 15000UL;  // let the animation start first

static int otaVersionPart(const String &s, int &at) {
  int value = 0;
  while (at < s.length() && (s[at] < '0' || s[at] > '9')) at++;
  while (at < s.length() && s[at] >= '0' && s[at] <= '9') value = value * 10 + (s[at++] - '0');
  while (at < s.length() && s[at] != '.') at++;
  if (at < s.length()) at++;
  return value;
}

// Returns true only when available is a strictly newer semantic version.
static bool otaIsNewer(const String &available, const String &current) {
  int a = 0, c = 0;
  for (int i = 0; i < 3; i++) {
    int av = otaVersionPart(available, a), cv = otaVersionPart(current, c);
    if (av != cv) return av > cv;
  }
  return false;
}

static String otaManifestValue(const String &manifest, const char *key) {
  String prefix = String(key) + "=";
  int start = manifest.indexOf(prefix);
  if (start < 0) return String();
  start += prefix.length();
  int end = manifest.indexOf('\n', start);
  if (end < 0) end = manifest.length();
  String value = manifest.substring(start, end);
  value.trim();
  return value;
}

static bool otaSyncClock() {
  configTime(0, 0, "time.cloudflare.com", "pool.ntp.org");
  unsigned long started = millis();
  while (time(nullptr) < 1700000000 && millis() - started < 8000UL) delay(100);
  return time(nullptr) >= 1700000000;
}

static void otaCheckForUpdate() {
  // Built here rather than kept around: it holds a copy of every SSID and
  // password, and between two checks that is memory the animations can use.
  WiFiMulti wifiMulti;
  int known = 0;
  for (int i = 0; i < OTA_NETWORK_COUNT; i++) {
    if (otaNetworks[i].ssid[0] == '\0') continue;
    wifiMulti.addAP(otaNetworks[i].ssid, otaNetworks[i].password);
    known++;
  }
  if (known == 0) return;                // secrets.h has not been configured yet

  Serial.printf("OTA: free heap %u bytes, %d network(s) known\n", ESP.getFreeHeap(), known);
  WiFi.mode(WIFI_STA);
  // run() scans first and then joins the strongest network it recognises. That
  // is what makes moving the board around work: an unknown place costs one
  // scan, not one full connection timeout per network in the list.
  if (wifiMulti.run(OTA_WIFI_TIMEOUT_MS) != WL_CONNECTED) {
    Serial.println("OTA: no known Wi-Fi in range");
    WiFi.disconnect(); WiFi.mode(WIFI_OFF);
    return;
  }
  Serial.printf("OTA: joined %s\n", WiFi.SSID().c_str());
  if (!otaSyncClock()) {
    Serial.println("OTA: clock sync failed");
    WiFi.disconnect(); WiFi.mode(WIFI_OFF);
    return;
  }

  WiFiClientSecure manifestClient;
  manifestClient.setCACert(OTA_ROOT_CA);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(manifestClient, OTA_MANIFEST_URL)) {
    Serial.println("OTA: manifest connection failed");
    WiFi.disconnect(); WiFi.mode(WIFI_OFF);
    return;
  }
  int status = http.GET();
  String manifest = (status == HTTP_CODE_OK) ? http.getString() : String();
  http.end();

  String version = otaManifestValue(manifest, "version");
  String url = otaManifestValue(manifest, "url");
  if (version.length() == 0 || url.length() == 0 || !otaIsNewer(version, FW_VERSION)) {
    Serial.printf("OTA: already current (%s)\n", FW_VERSION);
    WiFi.disconnect(); WiFi.mode(WIFI_OFF);
    return;
  }

  Serial.printf("OTA: %s -> %s\n", FW_VERSION, version.c_str());
  WiFiClientSecure firmwareClient;
  firmwareClient.setCACert(OTA_ROOT_CA);
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  t_httpUpdate_return result = httpUpdate.update(firmwareClient, url, FW_VERSION);
  if (result == HTTP_UPDATE_FAILED)
    Serial.printf("OTA failed (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
  WiFi.disconnect(); WiFi.mode(WIFI_OFF);
}

static void otaTick() {
  if ((long)(millis() - otaNextCheck) < 0) return;
  otaNextCheck = millis() + OTA_CHECK_INTERVAL_MS;
  otaCheckForUpdate();
}

static void line(float x0, float y0, float x1, float y1, uint16_t c) {
  float dx = x1 - x0, dy = y1 - y0;
  int n = (int)fmaxf(fabsf(dx), fabsf(dy)) + 1;
  for (int i = 0; i <= n; i++) {
    float t = (float)i / n;
    px((int)lroundf(x0 + dx * t), (int)lroundf(y0 + dy * t), c);
  }
}

// Flat triangle, used for the eye lashes
static void tri(float ax, float ay, float bx, float by, float cx, float cy, uint16_t col) {
  int ymin = (int)floorf(fminf(ay, fminf(by, cy)));
  int ymax = (int)ceilf(fmaxf(ay, fmaxf(by, cy)));
  if (ymin < 0) ymin = 0;
  if (ymax >= SCREEN_H) ymax = SCREEN_H - 1;
  float vx[3] = { ax, bx, cx }, vy[3] = { ay, by, cy };
  for (int y = ymin; y <= ymax; y++) {
    float xs[3]; int n = 0;
    for (int e = 0; e < 3 && n < 3; e++) {
      int f = (e + 1) % 3;
      float y0 = vy[e], y1 = vy[f];
      if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y))
        xs[n++] = vx[e] + (vx[f] - vx[e]) * ((y - y0) / (y1 - y0));
    }
    if (n >= 2) {
      float a = fminf(xs[0], xs[1]), b = fmaxf(xs[0], xs[1]);
      for (int x = (int)floorf(a); x <= (int)ceilf(b); x++) px(x, y, col);
    }
  }
}

// ======================================================================
// 0 - MORPH
// ======================================================================
static const int8_t cubeV[8][3] = {
  {-1,-1,-1},{1,-1,-1},{-1,1,-1},{1,1,-1},{-1,-1,1},{1,-1,1},{-1,1,1},{1,1,1}};
static const uint8_t cubeE[12][2] = {
  {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
static const float octaV[6][3] = {
  {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
static const uint8_t octaE[12][2] = {
  {0,2},{2,1},{1,3},{3,0},{4,0},{4,2},{4,1},{4,3},{5,0},{5,2},{5,1},{5,3}};

static float pjx[8], pjy[8];
static float angx = 0, angy = 0, angz = 0;

static void chromaLine(int a, int b, float split) {
  const int8_t bx[5] = {0, 1, -1, 0, 0};
  const int8_t by[5] = {0, 0, 0, 1, -1};
  struct { float ox; uint16_t col; } pass[3] = {
    { -split, 0x07E0 }, { split, 0x001F }, { 0.0f, 0xF800 }
  };
  for (int p = 0; p < 3; p++) {
    float x0 = pjx[a] + pass[p].ox, y0 = pjy[a];
    float x1 = pjx[b] + pass[p].ox, y1 = pjy[b];
    float dx = x1 - x0, dy = y1 - y0;
    int n = (int)fmaxf(fabsf(dx), fabsf(dy)) + 1;
    for (int i = 0; i <= n; i++) {
      float t = (float)i / n;
      int X = (int)lroundf(x0 + dx * t), Y = (int)lroundf(y0 + dy * t);
      for (int k = 0; k < 5; k++) {
        int xx = X + bx[k], yy = Y + by[k];
        if ((unsigned)xx < SCREEN_W && (unsigned)yy < SCREEN_H) {
          uint16_t o = fb[xx + yy * SCREEN_W];
          uint16_t r = ((o >> 11) & 0x1F) + ((pass[p].col >> 11) & 0x1F);
          uint16_t g = ((o >> 5) & 0x3F) + ((pass[p].col >> 5) & 0x3F);
          uint16_t bl = (o & 0x1F) + (pass[p].col & 0x1F);
          if (r > 0x1F) r = 0x1F; if (g > 0x3F) g = 0x3F; if (bl > 0x1F) bl = 0x1F;
          fb[xx + yy * SCREEN_W] = (r << 11) | (g << 5) | bl;
        }
      }
    }
  }
}

static void animMorph(float dt) {
  memset(fb, 0, sizeof(fb));
  angx += 0.6f * dt; angy += 0.9f * dt; angz += 0.35f * dt;
  float sx = sinf(angx), cx = cosf(angx);
  float sy = sinf(angy), cy = cosf(angy);
  float sz = sinf(angz), cz = cosf(angz);

  int v = variant[0];
  bool octa = (v == 2);
  int nv = octa ? 6 : 8;
  float split = (v == 1) ? 4.0f : 2.0f;

  for (int i = 0; i < nv; i++) {
    float x = octa ? octaV[i][0] : cubeV[i][0];
    float y = octa ? octaV[i][1] : cubeV[i][1];
    float z = octa ? octaV[i][2] : cubeV[i][2];
    float y1 = y * cx - z * sx, z1 = y * sx + z * cx;
    float x2 = x * cy + z1 * sy, z2 = -x * sy + z1 * cy;
    float x3 = x2 * cz - y1 * sz, y3 = x2 * sz + y1 * cz;
    float depth = 3.2f + z2; if (depth < 0.5f) depth = 0.5f;
    float k = 2.6f / depth * 40.0f;
    pjx[i] = 64 + x3 * k;
    pjy[i] = 64 + y3 * k;
  }
  for (int e = 0; e < 12; e++) {
    int a = octa ? octaE[e][0] : cubeE[e][0];
    int b = octa ? octaE[e][1] : cubeE[e][1];
    chromaLine(a, b, split);
  }
  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
}

// ======================================================================
// 1 - SPHERE POINTILLISME 1  (RGB-split dotted wireframe globe)
// ======================================================================
// A see-through sphere built from latitude rings, drawn as dots. It spins
// about its axis while the whole globe tilts and rolls, so the rings breathe
// open into ellipses and collapse edge-on into a line. Each dot is plotted
// three times - a slightly larger red copy, a centred green copy and a
// slightly smaller blue copy - so the wireframe carries a radial chromatic-
// aberration fringe: white where the channels line up (near the centre),
// splitting into pure R/G/B toward the rim.
//
// Two ring behaviours, chosen by variant:
//
//   BREATHING (variant 0, default): the globe starts as one equatorial ring.
//     Rings then fade in while the whole set expands with equal spacing, so
//     the changing count never creates an isolated oversized gap.
//   STATIC (variant 1): the earlier look - rings fade in at fixed latitudes
//     by binary subdivision (thresholds in latThr) without sliding.
//
// Meridians (longitude great circles) are added only by variant 2.

#define GLOBE_R  52.0f
#define NL_MAX   15                // latitude-ring slots
#define NM_MAX    8                // meridian slots
#define PHI_MAX  1.45f             // pole latitude (near pi/2 -> rings vanish there)
#define FLOW     0.14f             // breathing rate for the ring count

// Static mode: reveal threshold per fixed-latitude ring (extremes first).
static const float latThr[NL_MAX] = {
  0.0f, 0.75f, 1.0f, 0.5f, 1.0f, 0.75f, 1.0f, 0.25f,
  1.0f, 0.75f, 1.0f, 0.5f, 1.0f, 0.75f, 0.0f };
static const float merThr[NM_MAX] = {
  0.0f, 1.0f, 0.5f, 1.0f, 0.0f, 1.0f, 0.5f, 1.0f };

// Dot size: each plotted point is a (DOT_SPAN+1) square. 0 = 1px, 1 = 2x2.
#define DOT_SPAN 1

// Additive plot: channels sum and clamp, so overlapping passes go to white.
static inline void addPx(int x, int y, uint16_t add) {
  if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) {
    uint16_t o = fb[x + y * SCREEN_W];
    uint16_t r = ((o >> 11) & 0x1F) + ((add >> 11) & 0x1F);
    uint16_t g = ((o >> 5) & 0x3F) + ((add >> 5) & 0x3F);
    uint16_t b = (o & 0x1F) + (add & 0x1F);
    if (r > 0x1F) r = 0x1F; if (g > 0x3F) g = 0x3F; if (b > 0x1F) b = 0x1F;
    fb[x + y * SCREEN_W] = (r << 11) | (g << 5) | b;
  }
}

// A fatter dot: a small square, centred on (x, y).
static inline void addBlob(int x, int y, uint16_t add) {
  int o = DOT_SPAN / 2;
  for (int dy = 0; dy <= DOT_SPAN; dy++)
    for (int dx = 0; dx <= DOT_SPAN; dx++)
      addPx(x + dx - o, y + dy - o, add);
}

// Rotate one sphere point (spin about Y, tilt about X, roll about Z on screen),
// dim it by depth, then stamp the three chroma-split copies.
static void gPlot(float x, float y, float z,
                  float cS, float sS, float cT, float sT, float cR, float sR,
                  float split, float alpha) {
  float x1 =  x * cS + z * sS;          // spin about Y
  float z1 = -x * sS + z * cS;
  float y2 = y * cT - z1 * sT;          // tilt about X
  float z2 = y * sT + z1 * cT;
  float xs = x1 * cR - y2 * sR;         // roll about Z (screen plane)
  float ys = x1 * sR + y2 * cR;

  float front = 0.5f + 0.5f * (z2 / GLOBE_R);     // 0 back .. 1 front
  if (front < 0) front = 0; if (front > 1) front = 1;
  float bf = (0.30f + 0.70f * front) * alpha;     // near side brighter, fade-in
  uint16_t redC   = (uint16_t)(0x1F * bf) << 11;
  uint16_t greenC = (uint16_t)(0x3F * bf) << 5;
  uint16_t blueC  = (uint16_t)(0x1F * bf);

  addBlob((int)lroundf(64 + xs * (1 + split)), (int)lroundf(64 + ys * (1 + split)), redC);
  addBlob((int)lroundf(64 + xs),               (int)lroundf(64 + ys),               greenC);
  addBlob((int)lroundf(64 + xs * (1 - split)), (int)lroundf(64 + ys * (1 - split)), blueC);
}

static void animGrid(float t) {
  int v = variant[1];
  // v0 (default): evenly-spaced breathing latitudes, no perpendicular rings.
  // v1: static fade-in-place look. v2: breathing latitudes + meridians.
  bool  slide     = (v != 1);
  bool  meridians = (v == 2);
  float split     = 0.075f;
  memset(fb, 0, sizeof(fb));

  float aS = t * 0.85f, aT = t * 0.33f, aR = t * 0.11f;   // spin, tilt, roll
  float cS = cosf(aS), sS = sinf(aS);
  float cT = cosf(aT), sT = sinf(aT);
  float cR = cosf(aR), sR = sinf(aR);

  float reveal = 0.5f - 0.5f * cosf(t * 0.39f);   // detail level for static/meridians
  const float W = 0.18f;
  const int ND = 32;                              // fewer, fatter dots per ring

  if (slide) {
    // The count changes continuously from one to fifteen rings. Every ring is
    // then positioned from that same fractional count, which keeps all gaps
    // evenly distributed instead of leaving a sudden empty band on the globe.
    // At t=0 this deliberately evaluates to one clear equatorial ring.
    const float count = 1.0f + (NL_MAX - 1) * 0.5f *
                        (1.0f - cosf(t * FLOW));
    const int whole = (int)floorf(count);
    const float incoming = count - whole;
    const int drawCount = (whole < NL_MAX) ? whole + 1 : whole;
    for (int k = 0; k < drawCount; k++) {
      float alpha = (k < whole) ? 1.0f : incoming;
      if (alpha <= 0.01f) continue;
      float p = (k + 1.0f) / (count + 1.0f);              // evenly spaced 0..1
      float phi = PHI_MAX * (1.0f - 2.0f * p);           // +top .. -bottom
      float ry = GLOBE_R * sinf(phi), rr = GLOBE_R * cosf(phi);
      for (int j = 0; j < ND; j++) {
        float th = 6.2832f * j / ND;
        gPlot(rr * cosf(th), ry, rr * sinf(th), cS, sS, cT, sT, cR, sR, split, alpha);
      }
    }
  } else {
    for (int i = 0; i < NL_MAX; i++) {               // static fixed-latitude rings
      float a = (reveal - (latThr[i] - W)) / W;
      if (a <= 0) continue; if (a > 1) a = 1;
      float phi = (-1.0f + 2.0f * i / (NL_MAX - 1)) * 1.30f;
      float ry = GLOBE_R * sinf(phi), rr = GLOBE_R * cosf(phi);
      for (int j = 0; j < ND; j++) {
        float th = 6.2832f * j / ND;
        gPlot(rr * cosf(th), ry, rr * sinf(th), cS, sS, cT, sT, cR, sR, split, a);
      }
    }
  }
  if (meridians) {                                   // longitude great circles
    for (int m = 0; m < NM_MAX; m++) {
      float a = (reveal - (merThr[m] - W)) / W;
      if (a <= 0) continue; if (a > 1) a = 1;
      float lam = 3.14159f * m / NM_MAX;
      float cl = cosf(lam), sl = sinf(lam);
      for (int j = 0; j < ND; j++) {
        float th = 6.2832f * j / ND;
        float st = sinf(th), ct = cosf(th);
        gPlot(GLOBE_R * st * cl, GLOBE_R * ct, GLOBE_R * st * sl, cS, sS, cT, sT, cR, sR, split, a);
      }
    }
  }
  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
}

// ======================================================================
// 2 - WAVES (a stack of horizontal lines, printed three times out of register)
// ======================================================================
// One stack of horizontal lines and one displacement field. Every line samples
// the same field, but a line further down the screen reads it at a later
// phase, so the crests do not stand vertically above one another: they lean,
// and the stack reads as a single surface caught at an angle rather than as a
// pile of unrelated ripples.
//
// The field is three travelling sines added together. Two of them are the
// waves proper, one running right and one running left, and their weights
// cross-fade against each other on a very slow envelope - the sheet leans one
// way for most of a minute, flattens as the two come level, then leans back.
// The third is a long, lazy swell almost as wide as the panel carrying most of
// a full turn of phase from the top row to the bottom; it is what bends the
// leaning crests into sweeping curves instead of leaving them as straight
// diagonal corrugations. Nothing here is fast: the wave periods are 18, 24 and
// 45 seconds and the alternation takes 80.
//
// The stack is drawn three times, once per channel, and the three printings do
// not line up. Four separate things pull them apart, and they matter in this
// order:
//
//   - a shift in depth. Each channel reads the field as though its line sat a
//     little higher or lower in the stack than it really does. Because the
//     field carries most of a full turn of phase from top to bottom, this
//     warps the three printings differently rather than merely moving them -
//     which is the whole point. The gap is three or four pixels in some places
//     and nothing at all in others, so part of a line comes out split into
//     red, green and blue while the rest of it, or the line below it, closes
//     back up into white.
//   - a few percent of amplitude, so the copies separate most at the crests
//     and troughs and least at the nodes - the opposite pattern to the shift
//     above, which is why the two together leave so few dead stretches.
//   - a time lag: red reads the field slightly in the past and blue slightly
//     in the future. For a travelling wave a shift in time is a shift in
//     space, so this slides the copies horizontally past each other. On a line
//     running flat it does nothing; where the surface is steep it opens the
//     fringe out.
//   - a flat vertical offset, one channel up and one down. Small, and its only
//     job is to keep a little colour alive where the other three agree.
//
// The weighting between those four is what decides whether the panel reads as
// a white grid with coloured edges or as three coloured grids. Most of the
// separation has to come from the first two, because those vary across the
// picture; leaning on the flat offset instead would print every line as the
// same red-green-blue sandwich everywhere and lose the white entirely.
//
// Only the three pure channels are ever written. Everything else on the panel
// - the white, and the yellow, cyan and magenta - is overlap.
//
// Sampling the field with sinf() at every pixel of every line of every channel
// would be twenty-five thousand calls a frame. Instead each component is
// seeded once per line and then stepped along the row by a rotation - the
// standard sin/cos recurrence - which is six multiplies instead of a library
// call. Over 128 steps the drift is far below a pixel.
//
// Every period divides 720 s exactly - 18, 24, 45, 80, 90, 144 - so the piece
// closes on itself twelve minutes in and replays the last three of its
// fifteen-minute slot.

#define WV_ALT_PERIOD     80.0f   // the two travelling waves trade strength
#define WV_SPEED           1.20f   // travel speed multiplier
#define WV_WGT_MID         0.36f
#define WV_WGT_SWING       0.22f
#define WV_WGT_SWELL       0.28f  // the long swell holds steady

// Swing of a line as a multiple of the gap to its neighbour. Above one the
// lines reach into each other's lanes, which is what makes the sheet read as a
// surface with depth; the row-phase gradient stays gentle enough that they
// still never actually cross.
#define WV_AMP_FRAC        1.35f
#define WV_AMP_MIN         4.0f
#define WV_AMP_MAX        15.0f

// Slow motion of the stack as a whole, on top of the waves: the spacing opens
// and closes, and the whole sheet drifts up and down. Both smooth and
// wrap-free, so no line ever has to jump from one edge to the other.
#define WV_BREATHE_PERIOD 90.0f
#define WV_BREATHE_AMP     0.085f
#define WV_SWAY_PERIOD   144.0f
#define WV_SWAY_AMP        4.5f

// Misregistration, scaled as a group by the variant
#define WV_ROWPH           0.16f  // fraction of the stack each copy is read up/down by
#define WV_TILT            0.12f  // fractional amplitude difference
#define WV_LAG             0.80f  // seconds of time offset
#define WV_SHIFT           0.90f  // pixels of flat vertical offset

// The three components: wavelength in pixels, temporal period in seconds,
// direction (-1 runs right), and the phase accumulated from the top line to
// the bottom one in radians.
static const float wvLen[3]   = {  97.0f,  63.0f, 181.0f };
static const float wvPer[3]   = {  18.0f,  24.0f,  45.0f };
static const float wvDir[3]   = {  -1.0f,   1.0f,  -1.0f };
static const float wvRowPh[3] = {   2.1f,  -3.4f,   5.2f };

// Red into the past, green on the nominal sheet, blue into the future
static const float    wvChanOff[3] = { -1.0f, 0.0f, 1.0f };
static const uint16_t wvChanCol[3] = { 0xF800, 0x07E0, 0x001F };

// Variants: line count paired with how far the three printings come apart.
// 0 is the reference look, 1 trades lines for big slow swells with the sheets
// well separated, 2 is a fine fabric held almost in register.
static const uint8_t wvRows[3] = {   22,    12,    30 };
static const float   wvMis[3]  = { 1.00f, 1.75f, 0.60f };

// One column of a line, from where the previous sample sat to where this one
// does. Plotting the samples alone would comb the line into dots wherever the
// surface is steeper than a pixel per pixel; filling the gap in the new column
// keeps it continuous without needing a general line routine.
static inline void wvSpan(int x, int y0, int y1, uint16_t col) {
  if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
  if (y1 < 0 || y0 >= SCREEN_H) return;
  if (y0 < 0) y0 = 0;
  if (y1 >= SCREEN_H) y1 = SCREEN_H - 1;
  for (int y = y0; y <= y1; y++) addPx(x, y, col);
}

static void animWaves(float t) {
  const int   rows = wvRows[variant[2]];
  const float mis  = wvMis[variant[2]];
  const float spacing = (float)SCREEN_H / rows;

  float amp = spacing * WV_AMP_FRAC;
  if (amp < WV_AMP_MIN) amp = WV_AMP_MIN;
  if (amp > WV_AMP_MAX) amp = WV_AMP_MAX;

  const float rowph = mis * WV_ROWPH;
  const float tilt  = mis * WV_TILT;
  const float lag   = mis * WV_LAG;
  const float shift = mis * WV_SHIFT;

  const float env = sinf(TWO_PI * t / WV_ALT_PERIOD);
  const float wgt[3] = { WV_WGT_MID + WV_WGT_SWING * env,
                         WV_WGT_MID - WV_WGT_SWING * env,
                         WV_WGT_SWELL };

  const float scale = 1.0f + WV_BREATHE_AMP * sinf(TWO_PI * t / WV_BREATHE_PERIOD);
  const float sway  = WV_SWAY_AMP * sinf(TWO_PI * t / WV_SWAY_PERIOD);

  // Wave numbers, rates, and the rotation for one pixel of x
  float k, om[3], sk[3], ck[3];
  for (int i = 0; i < 3; i++) {
    k = TWO_PI / wvLen[i];
    om[i] = WV_SPEED * wvDir[i] * TWO_PI / wvPer[i];
    sk[i] = sinf(k);
    ck[i] = cosf(k);
  }

  memset(fb, 0, sizeof(fb));

  for (int j = 0; j < rows; j++) {
    // Position down the stack, 0 at the top line and 1 at the bottom. The
    // field is indexed by this rather than by pixels, so the pattern keeps its
    // shape when the line count changes and only gets finer.
    const float v = (float)j / (rows - 1);
    const float baseY = 64.0f + ((j + 0.5f) * spacing - 64.0f) * scale + sway;

    for (int c = 0; c < 3; c++) {
      const float o  = wvChanOff[c];
      const float a  = amp * (1.0f + o * tilt);
      const float y0 = baseY + o * shift;

      // The channel offset goes into the depth and the time both, so this copy
      // is reading the surface from somewhere else in the stack and somewhere
      // else in the cycle.
      const float tc = t + o * lag;
      const float vc = v + o * rowph;

      float s[3], q[3];
      for (int i = 0; i < 3; i++) {
        float ph = wvRowPh[i] * vc + om[i] * tc;
        s[i] = sinf(ph);
        q[i] = cosf(ph);
      }

      int prevY = 0;
      for (int x = 0; x < SCREEN_W; x++) {
        float w = wgt[0] * s[0] + wgt[1] * s[1] + wgt[2] * s[2];
        int y = (int)lroundf(y0 + a * w);

        if (x == 0) addPx(x, y, wvChanCol[c]);
        else        wvSpan(x, prevY, y, wvChanCol[c]);
        prevY = y;

        for (int i = 0; i < 3; i++) {      // advance one pixel of x
          float ns = s[i] * ck[i] + q[i] * sk[i];
          q[i] = q[i] * ck[i] - s[i] * sk[i];
          s[i] = ns;
        }
      }
    }
  }

  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
}

// ======================================================================
// 3 - PARTICLE BUDDHA
// ======================================================================
// A Buddha face that exists only as a probability cloud. Nothing here draws a
// face: every particle is born somewhere in the cloud, drifts straight out
// from a source between the brows, and dies a fraction of a second later. The
// face appears because the odds of a particle being *anywhere* come from the
// stored density map - dense along the brow ridge, the eyelids, the bridge of
// the nose and the lips, empty in the eye sockets, the nostrils and the mouth
// line. Each particle only makes a short hop before winking out, so the cloud
// renews itself constantly while the face it spells out never moves.
//
// A particle's brightness is read from the map at wherever it is *now*, not
// where it was born, so one that drifts into an eye socket fades out on the
// way through. That is what keeps the features carved out instead of letting
// the outward drift smear them closed.
//
// pbMap is 64x64, bilinear-sampled up to the screen. It was fitted so the
// running animation time-averages to the reference clip, which is why it is
// a little sharper than what you see - it is pre-compensated for the smear.
// It also carries a radial floor, zero across the face and rising toward the
// corners, so the cloud thins out to the very edge of the panel instead of
// stopping short and leaving a dead border. The map is fitted against this
// exact renderer, so changing PB_BIG0/PB_BIG1 or the speeds wants a regenerated
// pbMap to go with it.

#define PB_N      1350          // particles alive at once
#define PB_EX     63.0f         // emitter: the third eye, between the brows
#define PB_EY     58.0f
#define PB_V0     6.0f          // outward speed at the emitter, px/s
#define PB_EXPAND 1.45f         // extra outward speed per pixel of radius, 1/s
#define PB_LIFE   0.24f         // mean lifetime, seconds
#define PB_JIT    0.7f          // lifetime spread, +/- fraction of the mean
#define PB_GAIN   3.0f          // density -> brightness, saturating
#define PB_DIE    0.012f        // density below which a particle winks out
#define PB_BIG0   0.15f         // below this a particle is a single pixel
#define PB_BIG1   0.75f         // above this it is always a full 2x2 block

struct PbPart { float x, y, age, life, thr; };
static PbPart pbP[PB_N];
static uint32_t pbRng = 0x1234567u;

static inline uint32_t pbRand() {          // xorshift, plenty for a dust cloud
  pbRng ^= pbRng << 13; pbRng ^= pbRng >> 17; pbRng ^= pbRng << 5;
  return pbRng;
}
static inline float pbF() { return (pbRand() >> 8) * (1.0f / 16777216.0f); }

// Bilinear read of the 64x64 map at a 128x128 screen position.
static inline float pbDens(float x, float y) {
  float u = x * 0.5f - 0.25f, v = y * 0.5f - 0.25f;
  if (u < 0) u = 0; else if (u > 62.999f) u = 62.999f;
  if (v < 0) v = 0; else if (v > 62.999f) v = 62.999f;
  int i = (int)u, j = (int)v;
  float fx = u - i, fy = v - j;
  const uint8_t *r0 = &pbMap[j * 64], *r1 = r0 + 64;
  float a = r0[i] + (r0[i + 1] - r0[i]) * fx;
  float b = r1[i] + (r1[i + 1] - r1[i]) * fx;
  return (a + (b - a) * fy) * (1.0f / 255.0f);
}

// Spawn odds are the map raised to ~1.2, so the bright core pulls harder than
// a straight reading would. Tabled once instead of a powf per attempt.
static uint8_t pbSpawnLut[256];

// Rejection-sample a map cell, then jitter inside the 2x2 screen area it covers.
// Takes an index rather than a PbPart& on purpose: the Arduino builder hoists
// generated prototypes above the struct, so a struct parameter fails to compile.
static void pbSpawn(int k) {
  PbPart &p = pbP[k];
  for (int guard = 0; guard < 200; guard++) {
    uint32_t r = pbRand();
    int i = r & 63, j = (r >> 6) & 63;
    if ((int)((r >> 12) & 255) < pbSpawnLut[pbMap[j * 64 + i]]) {
      p.x = 2.0f * i + pbF() * 2.0f - 0.5f;
      p.y = 2.0f * j + pbF() * 2.0f - 0.5f;
      break;
    }
  }
  p.age = 0.0f;
  p.life = PB_LIFE * (1.0f + PB_JIT * (pbF() - 0.5f));
  p.thr = pbF();          // this particle's own size threshold, see animBuddha
}

// Keep the brighter of the two. Every particle is the same tint scaled by one
// brightness, so the packed RGB565 value rises monotonically with it and a plain
// compare is enough. Without this a dim outer particle would punch a black hole
// through a bright cluster it happens to land on.
static inline void pbPlot(int x, int y, uint16_t c) {
  if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) {
    uint16_t *o = &fb[x + y * SCREEN_W];
    if (c > *o) *o = c;
  }
}

static void initBuddha() {
  for (int i = 0; i < 256; i++)
    pbSpawnLut[i] = (uint8_t)(powf(i / 255.0f, 1.2f) * 255.0f + 0.5f);
  for (int k = 0; k < PB_N; k++) {
    pbSpawn(k);
    pbP[k].age = pbF() * pbP[k].life;      // de-sync, so it opens mid-breath
  }
}

static void animBuddha(float dt) {
  // cool white by default; the right button swaps in gold and ice-blue
  const float tint[3][3] = {
    { 0.96f, 0.97f, 1.00f }, { 1.00f, 0.80f, 0.42f }, { 0.42f, 0.86f, 1.00f }
  };
  int v = variant[3];
  float tr = tint[v][0], tg = tint[v][1], tb = tint[v][2];

  memset(fb, 0, sizeof(fb));

  for (int k = 0; k < PB_N; k++) {
    PbPart &p = pbP[k];

    // straight out from the third eye, faster the further out it already is
    float dx = p.x - PB_EX, dy = p.y - PB_EY;
    float r = sqrtf(dx * dx + dy * dy) + 0.001f;
    float step = (PB_V0 + PB_EXPAND * r) * dt / r;
    p.x += dx * step;
    p.y += dy * step;
    p.age += dt;

    float d = pbDens(p.x, p.y);
    // >= 128 rather than > 126: anything stricter kills a particle before it
    // can land on the last row or column, leaving a dead edge on the screen
    if (p.age >= p.life || d < PB_DIE ||
        p.x < 0 || p.x >= 128 || p.y < 0 || p.y >= 128) {
      pbSpawn(k);
      continue;
    }

    float b = d * PB_GAIN;
    if (b > 1.0f) b = 1.0f;
    b = sqrtf(b);
    b *= sqrtf(sqrtf(sinf(p.age / p.life * 3.14159265f)));   // fade in and out

    uint16_t c = rgbf(b * tr, b * tg, b * tb);
    int X = (int)p.x, Y = (int)p.y;
    pbPlot(X, Y, c);

    // Grow to 2x2, but against a threshold this particle drew for itself at
    // birth rather than one value shared by all of them. Brightness falls off
    // smoothly with radius, so a single global threshold makes every particle
    // sitting on one iso-density contour grow at the same instant - which reads
    // as a hard bar ringing the cloud. Spreading the thresholds across the
    // population gives the same average size gradient with the boundary
    // dissolved: near the crossover, neighbours simply differ. Rolled once per
    // life rather than per frame, so a particle keeps its size instead of
    // twinkling on the spot.
    float t = (b - PB_BIG0) * (1.0f / (PB_BIG1 - PB_BIG0));
    if (t > p.thr) {
      pbPlot(X + 1, Y, c); pbPlot(X, Y + 1, c); pbPlot(X + 1, Y + 1, c);
    }
  }
  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
}

// ======================================================================
// 4 - SWARM (dense flock drawn as fading wakes, RGB heat palette)
// ======================================================================
// A swarm of a thousand agents, drawn not as dots but as the glowing wake
// each one drags behind it. Every agent stamps the trail map at full
// brightness on every step and the whole map fades geometrically, so an
// agent reads as a short dash: white-blue at the head, then cyan, green,
// yellow, and a red tip where the wake is one step from dying. Packed side
// by side those dashes comb into the banded rainbow texture that is the
// whole point of the view - the palette is not decoration on top of the
// motion, it *is* the age of the motion.
//
// Flocking is done against a coarse 32x32 grid instead of agent-to-agent,
// which turns the usual O(n^2) neighbour hunt into two linear passes and is
// the only reason a swarm this size fits in the frame budget. Each step the
// agents are binned into the grid (how many, and their mean heading), the
// grid is blurred, and then every agent steers on three rules read straight
// back out of it:
//
//   density : climb the density gradient while the neighbourhood is thinner
//             than SW_DENS, slide back down it once it is thicker. One term
//             covers both cohesion and separation, and it is what gives the
//             flock a hard edge rather than a soft cloud.
//   align   : match the local mean heading. This is what combs the dashes
//             into parallel bands instead of a scribble.
//   swirl   : a weak, slowly turning background field over the whole panel.
//             Without it the flock locks into a flat horizontal stream after
//             half a minute; with it the mass keeps getting folded into new
//             waves, and the black voids keep opening and closing.
//
// Walls: left and right are the same wall. An agent leaving one side comes
// straight back in on the other and the density grid wraps with it, so the
// flock is continuous across the seam - there is no barrier there. Top and
// bottom are solid: agents run right into them and bounce, which is where
// the bright bands that skim along those two edges come from.

#define SW_N     1400              // agents allocated; presets use up to this
#define SW_G     32                // flock grid: 32x32 cells of 4x4 pixels
#define SW_GC    (SCREEN_W / SW_G)
#define SW_EG    16                // swirl field: 16x16 cells, 17x17 corners
#define SW_EGP   (SW_EG + 1)
#define SW_PROBE 6.0f              // density gradient probe distance, px
#define SW_HZ    30.0f             // simulation steps per second
#define SW_CUT   0.06f             // trail level below which a pixel is black
#define SW_GAM   0.70f             // palette gamma: pushes the bulk to blue
#define SW_BLUR  5                 // density blur passes = cohesion reach

struct SwAgent { float x, y, vx, vy; };
static SwAgent  swA[SW_N];
static uint8_t  swTrail[SCREEN_W * SCREEN_H];    // 16 KB wake map
static uint16_t swLut[256];                      // trail age -> RGB565
static float    swD[SW_G * SW_G];                // agents per cell
static float    swVX[SW_G * SW_G], swVY[SW_G * SW_G];
static float    swEX[SW_EGP * SW_EGP], swEY[SW_EGP * SW_EGP];
static float    swAcc = 0;                       // fixed-step accumulator
static uint32_t swRng = 0x2545F491u;

// n, decay, align, grad, dens, wander, swirl
struct SwPreset { int16_t n; uint8_t decay; float align, grad, dens, wander, swirl; };
static const SwPreset swPre[3] = {
  { 1250, 210, 0.35f, 0.030f, 1.20f, 0.020f, 0.030f },   // the reference look
  { 1400, 190, 0.45f, 0.035f, 1.90f, 0.015f, 0.020f },   // denser, shorter dashes
  {  800, 220, 0.28f, 0.025f, 0.90f, 0.030f, 0.055f },   // looser, stronger swirl
};

// The swirl is the curl of three travelling sines, so it circulates instead
// of shoving everything into a corner. All x wavenumbers are whole numbers
// of turns, which is what keeps it seamless across the wrapped left/right edge.
static const float swPsi[3][5] = {     // x waves, y waves, speed, amp, phase
  { 1.0f,  2.3f,  0.083f, 1.00f, 0.0f },
  { 2.0f, -1.7f, -0.061f, 0.55f, 1.7f },
  { 1.0f,  5.1f,  0.047f, 0.35f, 3.1f },
};

static inline uint32_t swRand() {
  swRng ^= swRng << 13; swRng ^= swRng >> 17; swRng ^= swRng << 5;
  return swRng;
}
static inline float swF() { return (swRand() >> 8) * (1.0f / 16777216.0f); }

static void swInitLut() {
  for (int i = 0; i < 256; i++) {
    float u = i / 255.0f;
    if (u < SW_CUT) { swLut[i] = 0; continue; }      // hard black boundary
    float w = powf((u - SW_CUT) / (1.0f - SW_CUT), SW_GAM);
    float s = 1.0f;
    if (w > 0.82f) s = 1.0f - (w - 0.82f) / 0.18f * 0.80f;   // core whitens
    float hh = 0.72f * w * 6.0f;                     // red -> violet, full value
    int   k = (int)hh;
    float f = hh - k;
    float p = 1 - s, q = 1 - s * f, t = 1 - s * (1 - f);
    float r, g, b;
    switch (k) {
      case 0: r = 1; g = t; b = p; break;
      case 1: r = q; g = 1; b = p; break;
      case 2: r = p; g = 1; b = t; break;
      case 3: r = p; g = q; b = 1; break;
      default: r = t; g = p; b = 1; break;
    }
    swLut[i] = rgbf(r, g, b);
  }
}

// 1-2-1 blur of one grid, in place. Wraps in x with the panel, clamps in y
// so the two solid walls do not leak density in from nowhere.
static void swBlur(float *a) {
  for (int j = 0; j < SW_G; j++) {
    float *r = &a[j * SW_G];
    float first = r[0], prev = r[SW_G - 1];
    for (int i = 0; i < SW_G; i++) {
      float cur = r[i];
      float nxt = (i == SW_G - 1) ? first : r[i + 1];
      r[i] = (prev + 2 * cur + nxt) * 0.25f;
      prev = cur;                        // r[i] is overwritten, keep the original
    }
  }
  static float above[SW_G];
  memcpy(above, a, sizeof(above));       // row -1 clamps to row 0
  for (int j = 0; j < SW_G; j++) {
    float *r = &a[j * SW_G];
    const float *bel = (j == SW_G - 1) ? r : r + SW_G;
    for (int i = 0; i < SW_G; i++) {
      float cur = r[i];
      float v = (above[i] + 2 * cur + bel[i]) * 0.25f;
      above[i] = cur;
      r[i] = v;
    }
  }
}

// Bilinear read of a flock grid at a pixel position. Cell centres sit at
// (i + 0.5) * SW_GC; x wraps, y clamps.
static float swSamp(const float *a, float x, float y) {
  float gx = x * (1.0f / SW_GC) - 0.5f, gy = y * (1.0f / SW_GC) - 0.5f;
  int i = (int)floorf(gx), j = (int)floorf(gy);
  float fx = gx - i, fy = gy - j;
  int i0 = i & (SW_G - 1), i1 = (i + 1) & (SW_G - 1);
  int j0 = j < 0 ? 0 : (j > SW_G - 1 ? SW_G - 1 : j);
  int j1 = (j + 1) < 0 ? 0 : ((j + 1) > SW_G - 1 ? SW_G - 1 : j + 1);
  const float *r0 = &a[j0 * SW_G], *r1 = &a[j1 * SW_G];
  float b0 = r0[i0] + (r0[i1] - r0[i0]) * fx;
  float b1 = r1[i0] + (r1[i1] - r1[i0]) * fx;
  return b0 + (b1 - b0) * fy;
}

static float swSampE(const float *a, float x, float y) {
  float gx = x * (SW_EG / (float)SCREEN_W), gy = y * (SW_EG / (float)SCREEN_H);
  int i = (int)gx, j = (int)gy;
  if (i > SW_EG - 1) i = SW_EG - 1;
  if (j > SW_EG - 1) j = SW_EG - 1;
  float fx = gx - i, fy = gy - j;
  const float *p = &a[j * SW_EGP + i];
  float b0 = p[0] + (p[1] - p[0]) * fx;
  float b1 = p[SW_EGP] + (p[SW_EGP + 1] - p[SW_EGP]) * fx;
  return b0 + (b1 - b0) * fy;
}

static void swField(float t) {
  for (int j = 0; j < SW_EGP; j++) {
    float v = (float)j / SW_EG;
    for (int i = 0; i < SW_EGP; i++) {
      float u = (float)i / SW_EG;
      float fx = 0, fy = 0;
      for (int k = 0; k < 3; k++) {
        float nx = swPsi[k][0], ny = swPsi[k][1];
        float c = cosf(6.2831853f * nx * u + ny * v + swPsi[k][2] * t + swPsi[k][4]);
        fx +=  swPsi[k][3] * ny * c;                       //  d(psi)/dy
        fy += -swPsi[k][3] * 6.2831853f * nx * c;          // -d(psi)/dx
      }
      float m = sqrtf(fx * fx + fy * fy);
      if (m < 1e-4f) m = 1e-4f;
      swEX[j * SW_EGP + i] = fx / m;
      swEY[j * SW_EGP + i] = fy / m;
    }
  }
}

static void swStep(int v, float t) {
  int   n      = swPre[v].n;
  float kAlign = swPre[v].align;
  float kGrad  = swPre[v].grad;
  float dens   = swPre[v].dens;
  float wander = swPre[v].wander;
  float swirl  = swPre[v].swirl;

  swField(t);

  // --- bin the swarm: count and mean heading per cell -------------------
  memset(swD,  0, sizeof(swD));
  memset(swVX, 0, sizeof(swVX));
  memset(swVY, 0, sizeof(swVY));
  for (int i = 0; i < n; i++) {
    int ci = (int)swA[i].x / SW_GC, cj = (int)swA[i].y / SW_GC;
    if (ci < 0) ci = 0; else if (ci > SW_G - 1) ci = SW_G - 1;
    if (cj < 0) cj = 0; else if (cj > SW_G - 1) cj = SW_G - 1;
    int c = cj * SW_G + ci;
    swD[c] += 1.0f; swVX[c] += swA[i].vx; swVY[c] += swA[i].vy;
  }
  for (int c = 0; c < SW_G * SW_G; c++) {
    float d = swD[c];
    if (d > 0) { swVX[c] /= d; swVY[c] /= d; }
  }
  swBlur(swVX); swBlur(swVY);
  for (int b = 0; b < SW_BLUR; b++) swBlur(swD);   // wider blur = longer reach

  // --- steer, move, deposit --------------------------------------------
  for (int i = 0; i < n; i++) {
    SwAgent &a = swA[i];
    float d  = swSamp(swD, a.x, a.y);
    float gx = (swSamp(swD, a.x + SW_PROBE, a.y) - swSamp(swD, a.x - SW_PROBE, a.y)) * 0.5f;
    float gy = (swSamp(swD, a.x, a.y + SW_PROBE) - swSamp(swD, a.x, a.y - SW_PROBE)) * 0.5f;

    float k = kGrad * (dens - d);            // in when thin, out when packed
    float ax = k * gx, ay = k * gy;

    ax += kAlign * (swSamp(swVX, a.x, a.y) - a.vx);
    ay += kAlign * (swSamp(swVY, a.x, a.y) - a.vy);

    ax += swirl * swSampE(swEX, a.x, a.y);
    ay += swirl * swSampE(swEY, a.x, a.y);

    ax += (swF() - 0.5f) * wander;
    ay += (swF() - 0.5f) * wander;

    a.vx += ax; a.vy += ay;
    float m = sqrtf(a.vx * a.vx + a.vy * a.vy);   // constant speed: even dashes
    if (m > 1e-4f) { a.vx /= m; a.vy /= m; }

    a.x += a.vx; a.y += a.vy;
    // Both walls sit on the outside of the pixel band - y = 0 and y = SCREEN_H,
    // not y = SCREEN_H - 1 - so the last row is as reachable as the first and
    // the swarm really does pile onto both edges instead of stopping a pixel short.
    if (a.x < 0) a.x += SCREEN_W; else if (a.x >= SCREEN_W) a.x -= SCREEN_W;
    if (a.y < 0)                { a.y = -a.y;                  a.vy = -a.vy; }
    else if (a.y >= SCREEN_H)   { a.y = 2 * SCREEN_H - a.y;    a.vy = -a.vy; }
    if (a.y < 0) a.y = 0; else if (a.y >= SCREEN_H) a.y = SCREEN_H - 0.001f;

    uint8_t *c = &swTrail[(int)a.y * SCREEN_W + (int)a.x];
    int nv = *c + 255;                             // full-brightness stamp
    *c = nv > 255 ? 255 : nv;
  }

  uint8_t decay = swPre[v].decay;
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++)
    swTrail[i] = (uint8_t)((swTrail[i] * decay) >> 8);
}

static void initSwarm() {
  swInitLut();
  memset(swTrail, 0, sizeof(swTrail));
  swAcc = 0;
  for (int i = 0; i < SW_N; i++) {          // seed all of them, so the right
    swA[i].x = swF() * SCREEN_W;            // button can change the count
    swA[i].y = swF() * SCREEN_H;            // without reseeding the swarm
    float a = swF() * 6.2831853f;
    swA[i].vx = cosf(a); swA[i].vy = sinf(a);
  }
}

static void animSwarm(float dt) {
  int v = variant[4];
  // Fixed 30 Hz steps regardless of frame rate, so dash length and the speed
  // of the flock stay put whatever the panel manages to push.
  swAcc += dt;
  for (int s = 0; s < 2 && swAcc >= 1.0f / SW_HZ; s++) {
    swAcc -= 1.0f / SW_HZ;
    swStep(v, animTime);
  }
  if (swAcc > 2.0f / SW_HZ) swAcc = 2.0f / SW_HZ;   // never build up a backlog

  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) fb[i] = swLut[swTrail[i]];
  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
}

// ======================================================================
// 5 - MOSAIC (20x20 tile grid, two square wave-fronts, RGB dispersion)
// ======================================================================
// The panel is thrown away and replaced by twenty tiles across and twenty
// down - chunky blocks of six or seven pixels, each one a single flat colour.
// Nothing is drawn *into* a tile: a tile is one sample of a continuous field,
// and everything you see is that field beating against the coarseness of the
// grid it is being read on.
//
// The field is two wave-fronts, each anchored on its own moving centre and
// each built from one cosine running across the panel and one running down it.
// A crest of the down-cosine is a full-width bar; a crest of the across-cosine
// is a full-height bar; where the two meet the field piles up well past white,
// and where two troughs meet it drops well below black. Around a centre the
// pair reads as a set of concentric squares, and those squares are the same
// bars - they simply run on out to the edge of the panel.
//
//   - the TIDE (front 1) is read identically by red, green and blue. Wherever
//     it bottoms out all three channels clamp together, which is what puts the
//     hard black bars into the picture instead of a dark colour, and it is the
//     layer that carries the concentric squares.
//   - the PRISM (front 2) is where the colour comes from. Its across-the-panel
//     cosine is shared - so a row stays one coherent colour along its length -
//     but its down-the-panel cosine is read at a slightly different pitch by
//     each channel, red widest and blue tightest, eight percent apart. The
//     three copies therefore walk out of step as you go down the glass: they
//     agree near the prism's centre and are most of a cycle apart by the far
//     edge, so the bands come out as a spectrum running top to bottom rather
//     than as a stack of greys.
//   - the two prism cosines are also multiplied together and added back in.
//     A product alternates in x *and* in y at once, so unlike the sums it
//     cannot make a bar: it lays a fine checkerboard over the bands, which is
//     the grain you see inside the brighter rows.
//
// Both centres ride an epicycle - two arms of different rate, the second
// slower and running backwards - on an ellipse taller than it is wide. That
// matters: most of the time a centre sits well above or below the panel, its
// vertical bars are off-screen and only the horizontal ones sweep through, so
// the picture reads as rolling bands. Every so often a centre crosses the
// glass and the bands close up into concentric squares for a few seconds
// before opening out again. The two rates share no common period, so the two
// fronts never settle into a repeat you can catch.
//
// The wave pitch is the whole trick. At the default setting one cycle spans
// 2.2 tiles - just wider than the two-tile limit this grid can resolve - so
// neighbouring tiles land on opposite sides of nearly every crest and the
// field aliases into a checkerboard that crawls as the phase drifts. Pitches
// nearer two tiles shimmer harder; the right button steps through three.
//
// Everything is separable, so a frame costs 120 cosines - twenty positions for
// each of six one-dimensional terms - and then a couple of adds per tile.

#define MO_N     20            // tiles across and down
#define MO_OFF   0.30f         // field level with both fronts at zero
#define MO_A1    0.46f         // tide amplitude
#define MO_A2    0.36f         // prism amplitude
#define MO_A3    0.45f         // prism cross term: the checkerboard grain
#define MO_WX    0.65f         // across-the-panel weight, per front
#define MO_WY    1.35f         // down-the-panel weight: bands beat stripes
#define MO_AX1    9.0f         // tide epicycle, half-width / half-height
#define MO_AY1   13.0f
#define MO_W1A   0.178f        // tide arm rates, rad/s
#define MO_W1B  -0.067f
#define MO_AX2   14.0f         // prism epicycle
#define MO_AY2    8.0f
#define MO_W2A  -0.116f
#define MO_W2B   0.048f
#define MO_DR1   0.264f        // tide phase drift, rad/s
#define MO_DR2  -0.163f        // prism phase drift, backwards
#define MO_TINT  0.06f         // warm bias: red sits highest, blue lowest

// Twenty tiles over 128 pixels is 6.4 each, so they come out six or seven
// wide. Tabled rather than divided per tile, and the last edge is the panel.
static const uint8_t moEdge[MO_N + 1] = {
    0,   6,  13,  19,  26,  32,  38,  45,  51,  58,  64,
   70,  77,  83,  90,  96, 102, 109, 115, 122, 128 };

// Cycles of the wave across the twenty tiles, and how far apart the three
// channels read the prism. Ten cycles would be exactly two tiles - a frozen
// checkerboard that never crawls - so every setting stays under it.
static const float moCycles[3] = { 9.0f, 9.7f, 7.4f };
static const float moDisp[3]   = { 0.08f, 0.11f, 0.09f };

// Red is left alone, green and blue are pulled down a little. Combined with
// MO_TINT this is the warm cast the reference has: red clips to full more
// often than blue does, and blue is the first channel to fall away to black.
static const float moGain[3] = { 1.00f, 0.95f, 0.90f };

static void animMosaic(float t) {
  int v = variant[5];
  float K   = 6.2831853f * moCycles[v] / MO_N;
  float dsp = moDisp[v];

  float a1 = MO_W1A * t, b1 = MO_W1B * t;
  float c1x = MO_AX1 * (cosf(a1) + 0.6f * cosf(b1));
  float c1y = MO_AY1 * (sinf(a1) + 0.6f * sinf(b1));
  float a2 = MO_W2A * t, b2 = MO_W2B * t;
  float c2x = MO_AX2 * (cosf(a2) + 0.5f * cosf(b2));
  float c2y = MO_AY2 * (sinf(a2) + 0.5f * sinf(b2));
  float ph1 = MO_DR1 * t, ph2 = MO_DR2 * t;

  float colTerm[MO_N];                 // tide + prism, everything x-dependent
  float rawX[MO_N];                    // bare prism cosine, for the cross term
  float rowTide[MO_N];                 // tide, y-dependent part
  float rawY[3][MO_N];                 // bare prism cosine per channel, in y

  for (int i = 0; i < MO_N; i++) {
    float u = i + 0.5f;                            // sample the tile's middle
    rawX[i]    = cosf(K * (u - c2x) + ph2);
    colTerm[i] = MO_A1 * MO_WX * cosf(K * (u - c1x) + ph1) + MO_A2 * MO_WX * rawX[i];
    rowTide[i] = MO_A1 * MO_WY * cosf(K * (u - c1y) + ph1);
    for (int c = 0; c < 3; c++)
      rawY[c][i] = cosf(K * (1.0f + dsp * (c - 1)) * (u - c2y) + ph2);
  }

  for (int j = 0; j < MO_N; j++) {
    float yr = rawY[0][j], yg = rawY[1][j], yb = rawY[2][j];
    float rowR = MO_OFF + MO_TINT + rowTide[j] + MO_A2 * MO_WY * yr;
    float rowG = MO_OFF           + rowTide[j] + MO_A2 * MO_WY * yg;
    float rowB = MO_OFF - MO_TINT + rowTide[j] + MO_A2 * MO_WY * yb;
    yr *= MO_A3; yg *= MO_A3; yb *= MO_A3;         // cross term, folded once
    int y0 = moEdge[j], y1 = moEdge[j + 1];
    for (int i = 0; i < MO_N; i++) {
      // rgbf clamps, and the clamp is the point: a third of the tiles are
      // driven past black and a tenth past white, which is what keeps the
      // colours pure instead of washing the panel out to pastel.
      float cx = colTerm[i], rx = rawX[i];
      uint16_t col = rgbf((rowR + cx + yr * rx) * moGain[0],
                          (rowG + cx + yg * rx) * moGain[1],
                          (rowB + cx + yb * rx) * moGain[2]);
      int x0 = moEdge[i], w = moEdge[i + 1] - x0;
      for (int y = y0; y < y1; y++) {
        uint16_t *p = &fb[y * SCREEN_W + x0];
        for (int x = 0; x < w; x++) *p++ = col;
      }
    }
  }
  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
}

// ======================================================================
// 6 - SPHERECOLOR (ported from spherecolors/SpiralSphere)
// ======================================================================
#define SC_TURNS 36
#define SC_POINTS 2592
#define SC_RADIUS 60.0f
static float scX[SC_POINTS], scY[SC_POINTS], scZ[SC_POINTS];

static void initSphereColor() {
  for (int i = 0; i < SC_POINTS; i++) {
    float theta = PI * (i + 0.5f) / SC_POINTS;
    float phi = TWO_PI * SC_TURNS * i / SC_POINTS;
    float st = sinf(theta);
    scX[i] = st * cosf(phi); scY[i] = cosf(theta); scZ[i] = st * sinf(phi);
  }
}

static uint16_t scHsv(float h, float v) {
  h -= floorf(h);
  float x = h * 6.0f; int i = (int)x; float f = x - i;
  float q = v * (1.0f - f), p = 0.0f, u = v * f;
  switch (i % 6) {
    case 0: return rgbf(v, u, p); case 1: return rgbf(q, v, p);
    case 2: return rgbf(p, v, u); case 3: return rgbf(p, q, v);
    case 4: return rgbf(u, p, v); default: return rgbf(v, p, q);
  }
}

static void animSphereColor(float t) {
  float ax = 0.30f * t, ay = 0.47f * t;
  float ca = cosf(ax), sa = sinf(ax), cb = cosf(ay), sb = sinf(ay);
  float span = 0.50f + 0.45f * sinf(TWO_PI * t / 9.0f);
  float wa = 0.90f * t;
  float dx = cosf(wa), dy = 0.7f * sinf(wa), dz = 0.9f * sinf(0.61f * wa);
  float inv = 1.0f / sqrtf(dx * dx + dy * dy + dz * dz);
  dx *= inv; dy *= inv; dz *= inv;
  memset(fb, 0, sizeof(fb));

  for (int i = 0; i < SC_POINTS; i++) {
    float y1 = scY[i] * ca - scZ[i] * sa, z1 = scY[i] * sa + scZ[i] * ca;
    float x2 = scX[i] * cb + z1 * sb, z2 = -scX[i] * sb + z1 * cb;
    if (z2 <= 0.0f) continue;
    int x = 64 + (int)(x2 * SC_RADIUS), y = 64 - (int)(y1 * SC_RADIUS);
    if ((unsigned)x >= SCREEN_W || (unsigned)y >= SCREEN_H) continue;
    float h;
    if (variant[6] == 0) h = 0.05f * t + span * (0.5f + 0.5f * (x2 * dx + y1 * dy + z2 * dz));
    else if (variant[6] == 1) h = 0.05f * t + span * ((float)i / SC_POINTS * SC_TURNS);
    else h = 0.05f * t + span * ((float)i / (SC_POINTS - 1));
    px(x, y, scHsv(h, 0.72f + 0.28f * z2));
  }
  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
}

// ======================================================================
// 7 - CELESTIAL SCAN (ported from CelestialScan/)
//     A scalar field lives on the sphere - four concentric waves around
//     axes that precess slowly - and the white curves are its isolines,
//     pulled out by marching squares. Same mechanism as an isobar chart,
//     hence the H and L markers sitting on the extrema.
//     The corner readouts are not decoration: the coordinates are the
//     point of the sphere facing us, and the temperature is the value of
//     the field there, so every number moves with the picture.
//     Like the glyph rain it replaces, the text is written straight to the
//     panel once the frame has been pushed - the framebuffer has no font.
// ======================================================================
#define CS_LON 80
#define CS_LAT 40
#define CS_NL (CS_LON + 1)
#define CS_NA (CS_LAT + 1)
#define CS_R  48.0f
#define CS_TERMS 4
#define CS_LIMB_Z 0.10f          // margin at the rim, else the curves pile up
#define CS_HL_MAX 4
#define CS_HL_GAP 16             // min pixels between two H/L letters
#define CS_TEMP_BASE (-78.0f)
#define CS_TEMP_SPAN 9.0f
#define CS_DEG 0xF8              // degree ring in font 1, needs cp437 on

// Celestial Scan and World Ring (view 10) each want one big working table,
// and only one view is ever running, so they share this buffer. Each
// rebuilds its own contents in initAnim().
#define CS_SCRATCH_BYTES   (CS_NA * CS_NL * (int)sizeof(float))
#define WR_SCRATCH_BYTES   (2 * 79 * 79)
#define VIEW_SCRATCH_BYTES (CS_SCRATCH_BYTES > WR_SCRATCH_BYTES ? CS_SCRATCH_BYTES : WR_SCRATCH_BYTES)
static uint32_t viewScratch[(VIEW_SCRATCH_BYTES + 3) / 4];

static float *csField = (float *)viewScratch;
static float csCosLat[CS_NA], csSinLat[CS_NA];
static float csCosLon[CS_NL], csSinLon[CS_NL];    // sphere frame
static float csCosLonS[CS_NL], csSinLonS[CS_NL];  // same, once spun
static float csProjLon[CS_NL];
static float csRowX[2][CS_NL], csRowY[2][CS_NL], csRowZ[2][CS_NL];
static float csCT = 1.0f, csST = 0.0f, csTilt = 14.0f;

struct CsTerm { float amp, omega, ax, ay, az, precess, phase; };
static CsTerm csTerm[CS_TERMS];

static const uint8_t csLevelCount[3] = {  14,    20,     8   };
static const float   csLevelStep[3]  = { 0.36f, 0.25f, 0.62f };

// Corners v0 top-left, v1 top-right, v2 bottom-right, v3 bottom-left.
// Edges e0 top, e1 right, e2 bottom, e3 left.
static const int8_t csSeg[16][4] = {
  {-1,-1,-1,-1}, { 3, 0,-1,-1}, { 0, 1,-1,-1}, { 3, 1,-1,-1},
  { 1, 2,-1,-1}, { 3, 0, 1, 2}, { 0, 2,-1,-1}, { 3, 2,-1,-1},
  { 2, 3,-1,-1}, { 2, 0,-1,-1}, { 0, 1, 2, 3}, { 2, 1,-1,-1},
  { 1, 3,-1,-1}, { 1, 0,-1,-1}, { 0, 3,-1,-1}, {-1,-1,-1,-1}
};

static void initCelestial() {
  for (int j = 0; j < CS_NA; j++) {
    float lat = HALF_PI - PI * j / CS_LAT;
    csCosLat[j] = cosf(lat);
    csSinLat[j] = sinf(lat);
  }
  for (int i = 0; i < CS_NL; i++) {
    float lon = TWO_PI * i / CS_LON;
    csCosLon[i] = cosf(lon);
    csSinLon[i] = sinf(lon);
  }
  for (int k = 0; k < CS_TERMS; k++) {          // a fresh object every time
    float x = random(-1000, 1001) / 1000.0f;
    float y = random(-1000, 1001) / 1000.0f;
    float z = random(-1000, 1001) / 1000.0f;
    float n = sqrtf(x * x + y * y + z * z);
    if (n < 0.05f) { x = 0; y = 0; z = 1; n = 1; }
    csTerm[k].ax = x / n;
    csTerm[k].ay = y / n;
    csTerm[k].az = z / n;
    csTerm[k].amp = 0.45f + random(0, 601) / 1000.0f;
    csTerm[k].omega = 1.9f + 2.4f * (random(0, 1001) / 1000.0f);
    csTerm[k].precess = random(-140, 141) / 1000.0f;
    csTerm[k].phase = random(0, 6283) / 1000.0f;
  }
}

static void csComputeField(float t) {
  int total = CS_NA * CS_NL;
  for (int i = 0; i < total; i++) csField[i] = 0.0f;

  for (int k = 0; k < CS_TERMS; k++) {
    CsTerm &tm = csTerm[k];
    float a = tm.precess * t;
    float ca = cosf(a), sa = sinf(a);
    float dx = tm.ax * ca - tm.az * sa;
    float dy = tm.ay;
    float dz = tm.ax * sa + tm.az * ca;
    float ph = fmodf(tm.phase + 0.23f * t, 6.2831853f);

    // x.d factors out: at a fixed latitude only the longitude part varies
    for (int i = 0; i < CS_NL; i++) csProjLon[i] = csCosLon[i] * dx + csSinLon[i] * dz;

    for (int j = 0; j < CS_NA; j++) {
      float base = csSinLat[j] * dy;
      float cla = csCosLat[j];
      float *row = &csField[j * CS_NL];
      for (int i = 0; i < CS_NL; i++)
        row[i] += tm.amp * sinf(tm.omega * (base + cla * csProjLon[i]) + ph);
    }
  }
}

static void csProjectRow(int j, int slot) {
  float cla = csCosLat[j], sla = csSinLat[j];
  for (int i = 0; i < CS_NL; i++) {
    float x = cla * csCosLonS[i];
    float z = cla * csSinLonS[i];
    float ys = sla * csCT - z * csST;
    csRowX[slot][i] = 64.0f + CS_R * x;
    csRowY[slot][i] = 64.0f - CS_R * ys;
    csRowZ[slot][i] = sla * csST + z * csCT;
  }
}

// cx/cy/v hold the four corners in v0..v3 order.
static inline void csEdge(int e, const float *cx, const float *cy, const float *v,
                          float L, float *ox, float *oy) {
  int a = e, b = (e + 1) & 3;
  float dv = v[b] - v[a];
  float t = (fabsf(dv) < 1e-9f) ? 0.5f : (L - v[a]) / dv;
  if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
  *ox = cx[a] + (cx[b] - cx[a]) * t;
  *oy = cy[a] + (cy[b] - cy[a]) * t;
}

static void csContours() {
  int nlev = csLevelCount[variant[7]];
  float step = csLevelStep[variant[7]];
  float first = -(nlev - 1) * 0.5f * step;

  int cur = 0;
  csProjectRow(0, 0);

  for (int j = 0; j < CS_LAT; j++) {
    int nxt = cur ^ 1;
    csProjectRow(j + 1, nxt);

    const float *fa = &csField[j * CS_NL];
    const float *fb = &csField[(j + 1) * CS_NL];

    for (int i = 0; i < CS_LON; i++) {
      // A cell straddling the rim has corners in front and behind; interpolating
      // between them would cut a chord across the disc, so drop it entirely.
      if (csRowZ[cur][i] <= CS_LIMB_Z || csRowZ[cur][i + 1] <= CS_LIMB_Z ||
          csRowZ[nxt][i + 1] <= CS_LIMB_Z || csRowZ[nxt][i] <= CS_LIMB_Z) continue;

      float v0 = fa[i], v1 = fa[i + 1], v2 = fb[i + 1], v3 = fb[i];
      float lo = v0, hi = v0;
      if (v1 < lo) lo = v1;
      if (v1 > hi) hi = v1;
      if (v2 < lo) lo = v2;
      if (v2 > hi) hi = v2;
      if (v3 < lo) lo = v3;
      if (v3 > hi) hi = v3;

      for (int k = 0; k < nlev; k++) {
        float L = first + k * step;
        if (L <= lo || L > hi) continue;
        int idx = (v0 > L ? 1 : 0) | (v1 > L ? 2 : 0) | (v2 > L ? 4 : 0) | (v3 > L ? 8 : 0);
        const int8_t *seg = csSeg[idx];
        if (seg[0] < 0) continue;
        const float cx[4] = { csRowX[cur][i], csRowX[cur][i + 1], csRowX[nxt][i + 1], csRowX[nxt][i] };
        const float cy[4] = { csRowY[cur][i], csRowY[cur][i + 1], csRowY[nxt][i + 1], csRowY[nxt][i] };
        const float vv[4] = { v0, v1, v2, v3 };
        for (int s = 0; s < 4; s += 2) {
          if (seg[s] < 0) break;
          float x0, y0, x1, y1;
          csEdge(seg[s],     cx, cy, vv, L, &x0, &y0);
          csEdge(seg[s + 1], cx, cy, vv, L, &x1, &y1);
          line(x0, y0, x1, y1, TFT_WHITE);
        }
      }
    }
    cur = nxt;
  }
}

static void csGraticule(float spinRad) {
  const uint16_t grey = rgbf(0.59f, 0.59f, 0.59f);
  for (int m = 0; m < 12; m++) {                 // meridians every 30 deg
    float lon = m * (PI / 6.0f) + spinRad;
    float cl = cosf(lon), sl = sinf(lon);
    for (int k = 0; k <= 120; k += 3) {
      float lat = -HALF_PI + PI * k / 120.0f;
      float cla = cosf(lat), sla = sinf(lat);
      float z = cla * sl;
      if (sla * csST + z * csCT <= 0.0f) continue;
      px((int)(64.0f + CS_R * cla * cl), (int)(64.0f - CS_R * (sla * csCT - z * csST)), grey);
    }
  }
  for (int lat = -60; lat <= 60; lat += 30) {    // parallels every 30 deg
    float la = lat * DEG_TO_RAD;
    float cla = cosf(la), sla = sinf(la);
    for (int k = 0; k < 180; k += 3) {
      float lon = TWO_PI * k / 180.0f + spinRad;
      float z = cla * sinf(lon);
      if (sla * csST + z * csCT <= 0.0f) continue;
      px((int)(64.0f + CS_R * cla * cosf(lon)), (int)(64.0f - CS_R * (sla * csCT - z * csST)), grey);
    }
  }
}

static void csText(int x, int y, const char *s) {
  while (*s) {
    tft.drawChar(x, y, (uint8_t)*s++, TFT_WHITE, TFT_BLACK, 1);
    x += 6;
  }
}

// The four strongest highs and lows, marked the way a weather chart does.
static void csExtrema() {
  int   bi[CS_HL_MAX], bj[CS_HL_MAX];
  char  bc[CS_HL_MAX];
  float bs[CS_HL_MAX];
  int   n = 0;

  for (int j = 2; j < CS_LAT - 1; j++) {
    const float *row = &csField[j * CS_NL];
    const float *up  = &csField[(j - 1) * CS_NL];
    const float *dn  = &csField[(j + 1) * CS_NL];
    for (int i = 0; i < CS_LON; i++) {
      int ip = (i + 1) % CS_LON, im = (i + CS_LON - 1) % CS_LON;
      float v = row[i];
      char ch = 0;
      if (v > row[ip] && v > row[im] && v > up[i] && v > dn[i]) ch = 'H';
      else if (v < row[ip] && v < row[im] && v < up[i] && v < dn[i]) ch = 'L';
      if (!ch) continue;

      float score = fabsf(v);
      int slot = -1;
      if (n < CS_HL_MAX) slot = n++;
      else {
        int worst = 0;
        for (int k = 1; k < CS_HL_MAX; k++) if (bs[k] < bs[worst]) worst = k;
        if (score > bs[worst]) slot = worst;
      }
      if (slot >= 0) { bi[slot] = i; bj[slot] = j; bc[slot] = ch; bs[slot] = score; }
    }
  }

  int placedX[CS_HL_MAX], placedY[CS_HL_MAX], placed = 0;
  for (int k = 0; k < n; k++) {
    float cla = csCosLat[bj[k]], sla = csSinLat[bj[k]];
    float x = cla * csCosLonS[bi[k]];
    float z = cla * csSinLonS[bi[k]];
    if (sla * csST + z * csCT < 0.35f) continue;      // too close to the rim
    int sx = (int)(64.0f + CS_R * x);
    int sy = (int)(64.0f - CS_R * (sla * csCT - z * csST));

    bool crowded = false;                             // no two letters touching
    for (int q = 0; q < placed; q++)
      if (abs(sx - placedX[q]) < CS_HL_GAP && abs(sy - placedY[q]) < CS_HL_GAP) crowded = true;
    if (crowded) continue;
    placedX[placed] = sx; placedY[placed] = sy; placed++;

    tft.drawChar(sx - 2, sy - 3, bc[k], TFT_WHITE, TFT_BLACK, 1);
  }
}

static void csHud(float spinDeg) {
  char buf[16];
  float subLat = csTilt;
  float subLon = -spinDeg;
  while (subLon < -180.0f) subLon += 360.0f;
  while (subLon >= 180.0f) subLon -= 360.0f;

  int j = (int)((90.0f - subLat) / 180.0f * CS_LAT);
  if (j < 0) j = 0; else if (j > CS_LAT) j = CS_LAT;
  int i = (int)((subLon + 180.0f) / 360.0f * CS_LON);
  if (i < 0) i = 0; else if (i >= CS_LON) i = CS_LON - 1;

  float degC = CS_TEMP_BASE + CS_TEMP_SPAN * csField[j * CS_NL + i];
  sprintf(buf, "%.1f%cC", degC, CS_DEG);
  csText(1, 1, buf);
  sprintf(buf, "%.1f%cF", degC * 9.0f / 5.0f + 32.0f, CS_DEG);
  csText(SCREEN_W - 1 - 6 * (int)strlen(buf), 1, buf);

  float v = fabsf(subLat);
  sprintf(buf, "%d%c%02d'%c", (int)v, CS_DEG, (int)((v - (int)v) * 60.0f), subLat >= 0 ? 'N' : 'S');
  csText(1, SCREEN_H - 8, buf);
  v = fabsf(subLon);
  sprintf(buf, "%d%c%02d'%c", (int)v, CS_DEG, (int)((v - (int)v) * 60.0f), subLon >= 0 ? 'E' : 'W');
  csText(SCREEN_W - 1 - 6 * (int)strlen(buf), SCREEN_H - 8, buf);

  csText(62, 1, "N");
  csText(62, SCREEN_H - 8, "S");
  csText(1, 60, "W");
  csText(SCREEN_W - 6, 60, "E");
}

static void animCelestial(float t) {
  csTilt = 14.0f + 12.0f * sinf(TWO_PI * t / 47.0f);   // the axis rocks slowly
  float tr = csTilt * DEG_TO_RAD;
  csCT = cosf(tr);
  csST = sinf(tr);

  float spinDeg = 12.0f * t;
  spinDeg -= 360.0f * floorf(spinDeg / 360.0f);
  float spinRad = spinDeg * DEG_TO_RAD;

  float cs = cosf(spinRad), ss = sinf(spinRad);
  for (int i = 0; i < CS_NL; i++) {
    csCosLonS[i] = csCosLon[i] * cs - csSinLon[i] * ss;
    csSinLonS[i] = csCosLon[i] * ss + csSinLon[i] * cs;
  }

  csComputeField(t);

  memset(fb, 0, sizeof(fb));
  csGraticule(spinRad);
  csContours();
  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
  csExtrema();
  csHud(spinDeg);
}

// ======================================================================
// 8 - EYE (full stencil eye: blink, glitch, red/white cycle, self-varying)
// ======================================================================
#define ECX 64
#define ECY 64
#define E_W   52.0f
#define E_TOPA 32.0f
#define E_BOTA 27.0f
#define E_IRIS 25.0f
#define E_RIM   5.0f
#define E_LIDW  6.0f
#define E_PUP  13.0f
#define COL_CREAM 0xF75C
#define COL_RED   0xE206

// Inverted from yesterday: red is the long phase now
#define BG_RED_TIME   15.0f
#define BG_WHITE_TIME  5.0f
static bool  eBgRed = true;
static float eBgT = BG_RED_TIME;

#define E_MAXBARS 10
struct EBar { int16_t x, y, w, h; uint8_t kind; };
static EBar ebars[E_MAXBARS];
static int   eGCount = 0;
static bool  eGlitching = false;
static float eGlitchT = 0, eNextGlitch = 1.5f;
static unsigned long eLastRegen = 0;

struct EMood { float gapMin, gapRand, dur; };
static const EMood emoods[3] = {
  { 2.8f, 3.0f, 0.85f }, { 0.9f, 1.2f, 0.32f }, { 5.0f, 4.0f, 1.60f }
};
static bool  eBlinking = false;
static float eBlinkT = 0, eNextBlink = 2.0f;
static bool  eDoubleBlink = false;
static float eMoodTimer = 10.0f;   // seconds until the mood changes on its own

// One-time slow wake-up when the eye view begins: it starts shut and opens
// gradually, then hands over to the normal blink rhythm.
#define E_INTRO_DUR 3.5f
static bool  eIntro = true;
static float eIntroT = 0;

static inline float lidShape(float t) { float u = 1 - t * t; return u <= 0 ? 0 : powf(u, 0.62f); }

static void eScheduleBlink() {
  int m = variant[8];
  eNextBlink = emoods[m].gapMin + (random(1000) / 1000.0f) * emoods[m].gapRand;
  eDoubleBlink = (random(100) < 25);
}
static void eRegenGlitch() {
  eGCount = random(3, E_MAXBARS + 1);
  for (int i = 0; i < eGCount; i++) {
    ebars[i].kind = random(3);
    if (ebars[i].kind == 2) { ebars[i].x = 0; ebars[i].w = SCREEN_W; ebars[i].h = 1; }
    else { ebars[i].w = random(12, 82); ebars[i].x = random(-8, SCREEN_W - 10); ebars[i].h = random(1, 6); }
    ebars[i].y = random(0, SCREEN_H);
  }
}
static void eDrawGlitch() {
  uint16_t bar = eBgRed ? COL_CREAM : COL_RED;
  for (int i = 0; i < eGCount; i++) {
    EBar &g = ebars[i];
    for (int y = g.y; y < g.y + g.h; y++) for (int x = g.x; x < g.x + g.w; x++) px(x, y, bar);
    if (g.kind == 1) for (int x = g.x + 3; x < g.x + g.w + 3; x++) px(x, g.y + g.h, 0x0000);
  }
}

static void animEye(float dt) {
  unsigned long now = millis();
  int m = variant[8];
  float open;

  if (eIntro) {
    // Slow one-time wake-up: shut -> open on a smoothstep ease
    eIntroT += dt;
    float p = eIntroT / E_INTRO_DUR;
    if (p >= 1.0f) {
      eIntro = false;
      open = 1.0f;
      eScheduleBlink();
      eMoodTimer = 10.0f;
    } else {
      open = p * p * (3.0f - 2.0f * p);   // eased 0 -> 1
    }
  } else {
    // Self-varying mood so it changes without the button
    eMoodTimer -= dt;
    if (eMoodTimer <= 0) {
      variant[8] = (variant[8] + 1) % 3;
      m = variant[8];
      eMoodTimer = 9.0f + (random(1000) / 1000.0f) * 5.0f;
      if (!eBlinking) eScheduleBlink();
    }

    // Blink
    open = 1.0f;
    if (eBlinking) {
      eBlinkT += dt;
      float d = emoods[m].dur;
      if (eBlinkT >= d) {
        eBlinking = false;
        if (eDoubleBlink) { eDoubleBlink = false; eNextBlink = 0.12f; }
        else eScheduleBlink();
      } else open = 1.0f - sinf((eBlinkT / d) * PI);
    } else {
      eNextBlink -= dt;
      if (eNextBlink <= 0) { eBlinking = true; eBlinkT = 0; }
    }
  }

  // Paper red<->white, tearing on the switch
  eBgT -= dt;
  if (eBgT <= 0) {
    eBgRed = !eBgRed;
    eBgT = eBgRed ? BG_RED_TIME : BG_WHITE_TIME;
    eGlitching = true; eGlitchT = 0.40f; eRegenGlitch(); eLastRegen = now;
  }
  if (eGlitching) {
    eGlitchT -= dt;
    if (eGlitchT <= 0) {
      eGlitching = false; eGCount = 0;
      eNextGlitch = eBgRed ? 0.30f + (random(1000) / 1000.0f) * 0.80f
                           : 1.20f + (random(1000) / 1000.0f) * 3.00f;
    } else if (now - eLastRegen > 60) { eRegenGlitch(); eLastRegen = now; }
  } else {
    eNextGlitch -= dt;
    if (eNextGlitch <= 0) { eGlitching = true; eGlitchT = 0.10f + (random(1000) / 1000.0f) * 0.25f; eRegenGlitch(); eLastRegen = now; }
  }

  float gx = sinf(animTime * 0.37f) * 3.5f + sinf(animTime * 0.13f) * 1.8f;
  float gy = cosf(animTime * 0.29f) * 2.2f;
  float pupilR = E_PUP + sinf(animTime * 0.5f) * 1.2f;

  // --- draw ---
  uint16_t paper = eBgRed ? COL_RED : COL_CREAM;
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) fb[i] = paper;
  eDrawGlitch();

  float lineY = ECY + (1.0f - open) * 5.0f;
  float topA = E_TOPA * open, botA = E_BOTA * open;

  const float lashAng[3] = { -1.05f, -0.52f, 0.78f };
  const float lashLen[3] = { 17.0f, 21.0f, 15.0f };
  for (int side = 0; side < 2; side++) {
    float s = side ? 1.0f : -1.0f;
    float cxr = ECX + s * E_W;
    for (int i = 0; i < 3; i++)
      tri(cxr - s * 8.0f, lineY - 5.0f, cxr - s * 8.0f, lineY + 5.0f,
          cxr + s * cosf(lashAng[i]) * lashLen[i], lineY + sinf(lashAng[i]) * lashLen[i], 0x0000);
  }

  for (int x = 0; x < SCREEN_W; x++) {
    float t = (x - ECX) / E_W;
    if (fabsf(t) >= 1.0f) continue;
    float sh = lidShape(t);
    int yTop = (int)lroundf(lineY - topA * sh);
    int yBot = (int)lroundf(lineY + botA * sh);
    for (int y = yTop; y <= yBot; y++) {
      float dx = x - (ECX + gx), dy = y - (ECY + gy);
      float d = sqrtf(dx * dx + dy * dy);
      uint16_t c;
      if (d < pupilR) {
        c = 0x0000;
        float a = atan2f(dy, dx);
        if (a > -2.60f && a < -1.95f && d < pupilR * 0.92f) c = 0xFFFF;
        else if (a > 0.5f && a < 2.65f && d > 3.0f && ((int)d) % 3 == 0) c = 0xFFFF;
      } else if (d < E_IRIS) {
        if (d > E_IRIS - E_RIM) c = 0x0000;
        else c = (((int)(d - pupilR)) % 4 == 0) ? 0x0000 : COL_RED;
      } else {
        c = (y % 4 == 0) ? 0x0000 : 0xFFFF;
      }
      px(x, y, c);
    }
    float sh2 = lidShape(t + 1.0f / E_W);
    float lw = E_LIDW * (0.65f + 0.35f * open);
    float slT = fabsf((sh2 - sh) * topA), slB = fabsf((sh2 - sh) * botA);
    float mT = sqrtf(1 + slT * slT), mB = sqrtf(1 + slB * slB);
    if (mT > 4) mT = 4; if (mB > 4) mB = 4;
    for (int k = 0; k < (int)lroundf(lw * mT); k++) px(x, yTop + k, 0x0000);
    for (int k = 0; k < (int)lroundf(lw * mB); k++) px(x, yBot - k, 0x0000);
  }
  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
}

// ======================================================================
// 9 - TUNNEL (a square corridor of rings, flown forwards)
// ======================================================================
// A single square shaft seen in one-point perspective. Every ring is the same
// cross-section at a different depth, drawn at scale s, so the four corners of
// every ring sit on four straight rays leaving the vanishing point. That is the
// whole geometry; the corridor comes out of it for free.
//
// The vanishing point is deliberately not in the middle of the panel. Put it
// dead centre and the rings nest symmetrically, which reads as a flat target
// rather than a shaft - you have to be looking down the corridor off-axis
// before the two walls, the ceiling and the floor separate into four surfaces.
// Off-centre, the same ring is 89 px above the vanishing point and only 38 px
// below it, so the ceiling's edges spread across most of the screen while the
// floor's crowd into the bottom strip. Everything that makes the picture read
// as depth rather than as pattern is in that ratio.
//
// The ring scales are a geometric ladder, s = TN_SMAX * TN_RATIO^k, rather than
// evenly spaced depths. Two things follow. The gap between neighbouring rings
// on screen is then a fixed *fraction* of their size, which is what keeps some
// eighteen of them separately visible instead of resolving a handful in the
// foreground and dumping the rest into a smear; and the ladder is self-similar,
// so advancing every ring by one whole step reproduces the picture exactly. The
// view animates that step continuously and wraps it at 1. A ring is never
// created or destroyed mid-flight - the wrap hands the innermost one back to
// the near end at the moment its fade has taken it to nothing - so twenty-eight
// rings run for the whole slot with no seam anywhere in it.
//
// Nothing is drawn in colour. Each edge deposits a soft ridge of *intensity*
// into the framebuffer, and only at the very end is that read back through a
// ramp: dark red, red, orange, yellow, near-white. So the palette is not chosen
// anywhere - it is a readout of how many rings landed on a pixel. A lone ring
// in the foreground is plain red; down where the rings close to within a pixel
// of each other, around the far opening and along the floor, the same ramp runs
// up into the orange and yellow bloom. Corners are brighter than edges for the
// same reason, because two edges cross there.
//
// The accumulator *is* fb, counts first and colour second, so this view costs
// no RAM of its own beyond two small tables. The final pass reads each pixel
// once and writes it once, so mapping in place is safe.
//
// Edges are axis-aligned, so an edge is a constant weight added along a
// contiguous span - a pointer walk, not a per-pixel line routine. The ridge
// profile is a 14-entry lookup in eighths of a pixel, rescaled once per ring by
// that ring's depth fade, which keeps the inner loop to a load and an add.

#define TN_SMAX   1.18f        // scale of the nearest ring: just off-screen
#define TN_RATIO  0.912f       // each ring, as a fraction of the one in front
#define TN_RINGS  28           // 0.912^28 puts the far opening at about 11 px
#define TN_VP_FX  0.31f        // vanishing point, as a fraction of the panel
#define TN_VP_FY  0.70f
#define TN_VP_MIN 0.07f
#define TN_VP_MAX 0.93f
#define TN_PER_X  48.0f        // vanishing-point drift periods, seconds
#define TN_PER_Y  72.0f

#define TN_GLOW_R     1.75f    // half-width of an edge's glow, pixels
#define TN_GLOW_STEPS 8        // lookup resolution, entries per pixel
#define TN_GLOW_N     14
#define TN_GLOW_PEAK  64.0f    // one ring dead-on a pixel

#define TN_FADE_FAR   0.40f    // how much dimmer the far end is than the near
#define TN_SPAWN_FRAC 0.08f    // the innermost rings fade out over this much

// Variants: speed in rings per second, paired with how far the vanishing point
// wanders. 0 is the reference look, 1 drifts slowly across a wide arc so the
// corridor keeps changing which way it points, 2 is a fixed-axis rush.
static const float tnSpeed[3] = { 1.30f, 0.55f, 2.30f };
static const float tnDrift[3] = { 0.09f, 0.20f, 0.00f };

static uint16_t tnGlow[TN_GLOW_N];
static uint16_t tnCol[256];
static float    tnPhase = 0;

// A horizontal edge at fractional row yc, spanning x0..x1. The weight is
// constant along the span, so each row of the ridge is one contiguous walk.
static void tnHEdge(float yc, int x0, int x1, const uint16_t *rw) {
  if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
  if (x1 < 0 || x0 > SCREEN_W - 1) return;
  if (x0 < 0) x0 = 0;
  if (x1 > SCREEN_W - 1) x1 = SCREEN_W - 1;

  int r0 = (int)ceilf(yc - TN_GLOW_R);
  int r1 = (int)floorf(yc + TN_GLOW_R);
  if (r0 < 0) r0 = 0;
  if (r1 > SCREEN_H - 1) r1 = SCREEN_H - 1;

  for (int y = r0; y <= r1; y++) {
    int i = (int)(fabsf(y - yc) * TN_GLOW_STEPS);
    if (i >= TN_GLOW_N) continue;
    uint16_t w = rw[i];
    if (!w) continue;
    uint16_t *p = &fb[y * SCREEN_W + x0];
    for (int x = x0; x <= x1; x++) *p++ += w;
  }
}

// The same for a vertical edge at fractional column xc, spanning y0..y1
static void tnVEdge(float xc, int y0, int y1, const uint16_t *rw) {
  if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
  if (y1 < 0 || y0 > SCREEN_H - 1) return;
  if (y0 < 0) y0 = 0;
  if (y1 > SCREEN_H - 1) y1 = SCREEN_H - 1;

  int c0 = (int)ceilf(xc - TN_GLOW_R);
  int c1 = (int)floorf(xc + TN_GLOW_R);
  if (c0 < 0) c0 = 0;
  if (c1 > SCREEN_W - 1) c1 = SCREEN_W - 1;

  for (int x = c0; x <= c1; x++) {
    int i = (int)(fabsf(x - xc) * TN_GLOW_STEPS);
    if (i >= TN_GLOW_N) continue;
    uint16_t w = rw[i];
    if (!w) continue;
    uint16_t *p = &fb[y0 * SCREEN_W + x];
    for (int y = y0; y <= y1; y++) { *p += w; p += SCREEN_W; }
  }
}

static void initTunnel() {
  tnPhase = 0;

  // Ridge profile: a smooth bump reaching zero exactly at TN_GLOW_R, so an edge
  // has no hard cut-off to alias against as it slides between rows.
  for (int i = 0; i < TN_GLOW_N; i++) {
    float u = ((float)i / TN_GLOW_STEPS) / TN_GLOW_R;
    float w = 1.0f - u * u;
    if (w < 0) w = 0;
    tnGlow[i] = (uint16_t)(TN_GLOW_PEAK * w * w + 0.5f);
  }

  // Intensity ramp, indexed by the accumulator shifted down two: one ring dead
  // on a pixel lands at 16, the top of the plain red section, and the table
  // saturates at sixteen overlapping rings. v is how many rings' worth of light
  // reached the pixel - below one it is only ever red, and past one the green
  // and then the blue come up, which is where the bloom comes from.
  for (int n = 0; n < 256; n++) {
    float v = n / 16.0f;
    float r, g, b;
    if (v <= 1.0f) {
      r = v;      g = 0.0f;              b = 0.0f;
    } else if (v <= 2.2f) {
      float t = (v - 1.0f) / 1.2f;
      r = 1.0f;   g = 0.58f * t;         b = 0.02f * t;
    } else if (v <= 4.0f) {
      float t = (v - 2.2f) / 1.8f;
      r = 1.0f;   g = 0.58f + 0.34f * t; b = 0.02f + 0.22f * t;
    } else {
      float t = (v - 4.0f) / 4.0f;
      if (t > 1.0f) t = 1.0f;
      r = 1.0f;   g = 0.92f + 0.08f * t; b = 0.24f + 0.60f * t;
    }
    tnCol[n] = rgbf(r, g, b);
  }
}

static void animTunnel(float dt) {
  const int v = variant[9];

  // One whole step of the ladder reproduces the picture, so this wraps at 1
  tnPhase += tnSpeed[v] * dt;
  if (tnPhase >= 1.0f) tnPhase -= 1.0f;

  const float amp = tnDrift[v];
  float fx = TN_VP_FX + amp * sinf(TWO_PI * animTime / TN_PER_X);
  float fy = TN_VP_FY + amp * sinf(TWO_PI * animTime / TN_PER_Y + 1.7f);
  if (fx < TN_VP_MIN) fx = TN_VP_MIN; if (fx > TN_VP_MAX) fx = TN_VP_MAX;
  if (fy < TN_VP_MIN) fy = TN_VP_MIN; if (fy > TN_VP_MAX) fy = TN_VP_MAX;

  // A ring at scale s has its left wall at ax*(1-s) and its right at ax + bx*s.
  // At s = 1 that is 0 and SCREEN_W whatever the vanishing point is, which is
  // why the corridor never leaves a gap at the border as it drifts.
  const float ax = fx * SCREEN_W, bx = (1.0f - fx) * SCREEN_W;
  const float ay = fy * SCREEN_H, by = (1.0f - fy) * SCREEN_H;

  memset(fb, 0, sizeof(fb));

  uint16_t rw[TN_GLOW_N];
  float s = TN_SMAX * powf(TN_RATIO, tnPhase);

  for (int k = 0; k < TN_RINGS; k++, s *= TN_RATIO) {
    const float u = (k + tnPhase) / TN_RINGS;   // 0 at the near end, 1 at the far

    float fade = 1.0f - TN_FADE_FAR * u;
    if (u > 1.0f - TN_SPAWN_FRAC) fade *= (1.0f - u) / TN_SPAWN_FRAC;
    if (fade <= 0.0f) continue;

    // Rescale the ridge for this ring's depth once, so the span loops below
    // stay a load and an add
    const int scale = (int)(fade * 256.0f);
    for (int i = 0; i < TN_GLOW_N; i++) rw[i] = (uint16_t)((tnGlow[i] * scale) >> 8);

    const float L = ax * (1.0f - s), R = ax + bx * s;
    const float T = ay * (1.0f - s), B = ay + by * s;
    const int xL = (int)lroundf(L), xR = (int)lroundf(R);
    const int yT = (int)lroundf(T), yB = (int)lroundf(B);

    tnHEdge(T, xL, xR, rw);   // ceiling
    tnHEdge(B, xL, xR, rw);   // floor
    tnVEdge(L, yT, yB, rw);   // left wall
    tnVEdge(R, yT, yB, rw);   // right wall
  }

  // Counts to colour, in place: every pixel is read once and written once
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
    uint16_t a = fb[i] >> 2;
    fb[i] = tnCol[a > 255 ? 255 : a];
  }

  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
}

// ======================================================================
// 10 - WORLD RING (ported from WorldRing/)
//      Wireframe globe with solid continents, the orbital ring from the
//      logo, and a phrase running twice around the screen.
//      The lettering is a stroke font rather than a bitmap one: that is
//      what lets every glyph be rotated onto the circle.
//      The globe's tilt never changes and it spins about its own poles,
//      so for a given screen pixel the latitude is fixed and the longitude
//      is only an offset. All the inverse trigonometry happens once in
//      initAnim; the frame loop just subtracts a uint8_t, which wraps on
//      its own - hence the 256-column map.
// ======================================================================
#define WR_R       38.0f
#define WR_LUT_R   ((int)WR_R + 1)
#define WR_LUT_W   (2 * WR_LUT_R + 1)
#define WR_TILT    16.0f
#define WR_SPIN    22.0f         // deg/s, a full turn in about 16 s
#define WR_MERIDIANS 18
#define WR_PAR_STEP  20
#define WR_ORBIT_R      49.0f
#define WR_ORBIT_SQUASH 0.28f
#define WR_ORBIT_ROLL0  (-22.0f)
#define WR_ORBIT_PREC   (-6.0f)  // deg/s of precession, opposite the text ring
#define WR_TEXT_R  57.0f
#define WR_TEXT_SPIN (-6.0f)     // deg/s, negative = counter-clockwise
#define WR_TEXT    "THE WORLD IS WATCHING\x07"   // \x07 is the diamond separator
#define WR_REPEAT  2
#define WR_LUT_OUTSIDE 255

static_assert(2 * WR_LUT_W * WR_LUT_W <= VIEW_SCRATCH_BYTES,
              "view scratch buffer too small for World Ring");

static uint8_t *wrLat = (uint8_t *)viewScratch;
static uint8_t *wrLon = (uint8_t *)viewScratch + WR_LUT_W * WR_LUT_W;
static float wrCT, wrST;

static void initWorldRing() {
  wrCT = cosf(WR_TILT * DEG_TO_RAD);
  wrST = sinf(WR_TILT * DEG_TO_RAD);

  for (int y = 0; y < WR_LUT_W; y++) {
    for (int x = 0; x < WR_LUT_W; x++) {
      int i = y * WR_LUT_W + x;
      wrLat[i] = WR_LUT_OUTSIDE;
      float dx = (float)(x - WR_LUT_R) + 0.5f;
      float dy = (float)(y - WR_LUT_R) + 0.5f;
      if (dx * dx + dy * dy > WR_R * WR_R) continue;

      float xs =  dx / WR_R;
      float ys = -dy / WR_R;
      float zs = sqrtf(fmaxf(0.0f, 1.0f - xs * xs - ys * ys));

      float gy = ys * wrCT + zs * wrST;          // undo the tilt
      float gz = -ys * wrST + zs * wrCT;
      if (gy > 1.0f) gy = 1.0f; else if (gy < -1.0f) gy = -1.0f;

      int li = (int)((HALF_PI - asinf(gy)) / PI * MAP_LAT);
      if (li < 0) li = 0; else if (li >= MAP_LAT) li = MAP_LAT - 1;
      int gi = (int)((atan2f(gz, xs) + PI) / TWO_PI * MAP_LON) & (MAP_LON - 1);

      wrLat[i] = (uint8_t)li;
      wrLon[i] = (uint8_t)gi;
    }
  }
}

// Globe frame -> screen. False when the point is on the far side.
static inline bool wrProject(float x, float y, float z, float *sx, float *sy) {
  float ys = y * wrCT - z * wrST;
  if (y * wrST + z * wrCT <= 0.0f) return false;
  *sx = 64.0f + WR_R * x;
  *sy = 64.0f - WR_R * ys;
  return true;
}

static void wrGraticule(float spinRad, uint16_t col) {
  for (int m = 0; m < WR_MERIDIANS; m++) {
    float lon = TWO_PI * m / WR_MERIDIANS + spinRad;
    float cl = cosf(lon), sl = sinf(lon);
    bool have = false;
    float ax = 0, ay = 0;
    for (int k = 0; k <= 48; k++) {
      float lat = -HALF_PI + PI * k / 48.0f;
      float cla = cosf(lat), sla = sinf(lat);
      float sx, sy;
      if (wrProject(cla * cl, sla, cla * sl, &sx, &sy)) {
        if (have) line(ax, ay, sx, sy, col);
        ax = sx; ay = sy; have = true;
      } else have = false;
    }
  }
  for (int lat = -90 + WR_PAR_STEP; lat <= 90 - WR_PAR_STEP; lat += WR_PAR_STEP) {
    float la = lat * DEG_TO_RAD;
    float cla = cosf(la), sla = sinf(la);
    bool have = false;
    float ax = 0, ay = 0;
    for (int k = 0; k <= 64; k++) {
      float lon = TWO_PI * k / 64.0f + spinRad;
      float sx, sy;
      if (wrProject(cla * cosf(lon), sla, cla * sinf(lon), &sx, &sy)) {
        if (have) line(ax, ay, sx, sy, col);
        ax = sx; ay = sy; have = true;
      } else have = false;
    }
  }
  // the rim
  float px0 = 64.0f + WR_R, py0 = 64.0f;
  for (int k = 1; k <= 48; k++) {
    float a = TWO_PI * k / 48.0f;
    float x = 64.0f + WR_R * cosf(a), y = 64.0f + WR_R * sinf(a);
    line(px0, py0, x, y, col);
    px0 = x; py0 = y;
  }
}

static void wrLand(float spinDeg) {
  uint8_t shift = (uint8_t)(lroundf(spinDeg * (MAP_LON / 360.0f)) & (MAP_LON - 1));
  for (int y = 0; y < WR_LUT_W; y++) {
    const uint8_t *rowLat = &wrLat[y * WR_LUT_W];
    const uint8_t *rowLon = &wrLon[y * WR_LUT_W];
    int sy = 64 - WR_LUT_R + y;
    for (int x = 0; x < WR_LUT_W; x++) {
      if (rowLat[x] == WR_LUT_OUTSIDE) continue;
      if (landAt(rowLat[x], (uint8_t)(rowLon[x] - shift)))
        px(64 - WR_LUT_R + x, sy, TFT_WHITE);
    }
  }
}

static void wrOrbit(float t) {
  float roll = (WR_ORBIT_ROLL0 + WR_ORBIT_PREC * t) * DEG_TO_RAD;
  float cr = cosf(roll), sr = sinf(roll);
  float cz = sqrtf(1.0f - WR_ORBIT_SQUASH * WR_ORBIT_SQUASH);
  float ax = 0, ay = 0;
  bool ahid = true;
  for (int i = 0; i <= 120; i++) {
    float u = TWO_PI * i / 120.0f;
    float cu = cosf(u), su = sinf(u);
    float x1 = cu, y1 = su * WR_ORBIT_SQUASH, z1 = su * cz;
    float sx = 64.0f + WR_ORBIT_R * (x1 * cr - y1 * sr);
    float sy = 64.0f - WR_ORBIT_R * (x1 * sr + y1 * cr);
    float dx = sx - 64.0f, dy = sy - 64.0f;
    bool hid = (z1 < 0.0f) && (dx * dx + dy * dy < WR_R * WR_R);   // behind the globe
    if (i > 0 && !hid && !ahid) {
      line(ax, ay, sx, sy, TFT_WHITE);
      line(ax, ay - 1, sx, sy - 1, TFT_WHITE);
    }
    ax = sx; ay = sy; ahid = hid;
  }
}

// Each letter is laid on the circle, feet towards the centre, and stroked.
static void wrRingText(float angle0) {
  const char *base = WR_TEXT;
  int len = strlen(base);
  int total = len * WR_REPEAT;
  if (!total) return;

  float step = TWO_PI / total;
  float scale = WR_TEXT_R * step * 0.86f / VF_W;
  if (scale < 1.0f) scale = 1.0f; else if (scale > 2.2f) scale = 2.2f;

  for (int n = 0; n < total; n++) {
    uint16_t off = pgm_read_word(&VF_INDEX[(uint8_t)base[n % len]]);
    if (off == 0xFFFF) continue;

    float th = angle0 + (n + 0.5f) * step;
    float ct = cosf(th), st = sinf(th);
    float rx = st, ry = -ct;          // outwards
    float tx = ct, ty = st;           // reading direction

    const int8_t *p = &VF_STROKES[off];
    bool have = false;
    float lx = 0, ly = 0;
    for (;;) {
      int8_t gx = (int8_t)pgm_read_byte(p++);
      if (gx == VF_END_GLYPH) break;
      if (gx == VF_END_POLY) { have = false; continue; }
      int8_t gy = (int8_t)pgm_read_byte(p++);

      float u = ((float)gx - 2.0f) * scale;
      float v = ((float)gy - 3.0f) * scale;
      float x = 64.0f + rx * (WR_TEXT_R - v) + tx * u;
      float y = 64.0f + ry * (WR_TEXT_R - v) + ty * u;
      if (have) {
        line(lx, ly, x, y, TFT_WHITE);
        line(lx + 1, ly, x + 1, y, TFT_WHITE);   // bold, else it is too frail
      }
      lx = x; ly = y; have = true;
    }
  }
}

static void animWorldRing(float t) {
  float spinDeg = WR_SPIN * t;
  spinDeg -= 360.0f * floorf(spinDeg / 360.0f);

  memset(fb, 0, sizeof(fb));
  if (variant[10] != 2) wrGraticule(spinDeg * DEG_TO_RAD, rgbf(0.41f, 0.41f, 0.41f));
  if (variant[10] != 1) wrLand(spinDeg);
  wrOrbit(t);
  float textAngle = WR_TEXT_SPIN * DEG_TO_RAD * t;
  textAngle -= TWO_PI * floorf(textAngle / TWO_PI);
  wrRingText(textAngle);
  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
}

// ======================================================================
// 11 - HYPNO EYE (ported from HypnoEye/)
//      Op-art eye: black pupil, concentric rings, and bands that hug the
//      lid before flattening out towards the edges.
//      Nothing is drawn curve by curve. Every pixel works out a phase and
//      the band is black or white on its fractional part, so the whole
//      drawing lives in how that phase is built: distance to the pupil
//      inside the eye, distance to the lid outside it.
//      That is what makes the gaze free - offsetting the pupil and adding
//      a term in x tightens the rings on the side it looks at and opens
//      them on the other, without moving a single curve.
// ======================================================================
#define HE_W_LENS   60.0f    // half-width: the corners sit at 64 +/- this
#define HE_H_LENS   30.0f    // half-height in the middle
#define HE_CORNER_P 1.6f     // 1 = sharp corner, above that it rounds off
#define HE_R_PUPIL  17.0f
#define HE_DUTY     0.18f    // >0 = white stroke fatter than the black gap
#define HE_AA_GAIN  0.125f
#define HE_LID_W    0.16f    // lid stroke half-thickness, in pitches
#define HE_LID_GAP  0.16f    // black shoulder each side of that stroke
#define HE_GAZE_AMP 5.0f
#define HE_GAZE_P1  11.0f    // two periods with no simple ratio, so the
#define HE_GAZE_P2  7.3f     // wandering never visibly repeats
#define HE_K_MAX    0.22f    // ring squeeze on the side being looked at
#define HE_FLOW     4.0f     // px/s, outer bands travelling into the eye

static const float hePitch[3] = { 9.0f, 6.5f, 12.0f };
static uint16_t heGrey[32];
// The lid depends on x alone and never moves, so its height and the slope
// that goes with it are worked out once, column by column.
static float heLensH[SCREEN_W];
static float heLensInvG[SCREEN_W];

static void initHypnoEye() {
  for (int i = 0; i < 32; i++) {
    uint8_t v = (uint8_t)(i * 255 / 31);
    heGrey[i] = tft.color565(v, v, v);
  }
  for (int x = 0; x < SCREEN_W; x++) {
    float u = (float)x + 0.5f - 64.0f;
    float s = u / HE_W_LENS;
    if (fabsf(s) < 1.0f) {
      float base = 1.0f - s * s;
      heLensH[x] = HE_H_LENS * powf(base, HE_CORNER_P);
      float slope = HE_H_LENS * HE_CORNER_P * powf(base, HE_CORNER_P - 1.0f)
                  * (-2.0f * u / (HE_W_LENS * HE_W_LENS));
      heLensInvG[x] = 1.0f / sqrtf(1.0f + slope * slope);
    } else {
      heLensH[x] = 0.0f;
      heLensInvG[x] = 1.0f;
    }
  }
}

static void animHypnoEye(float t) {
  float gaze = HE_GAZE_AMP * (0.7f * sinf(TWO_PI * t / HE_GAZE_P1)
                            + 0.3f * sinf(TWO_PI * t / HE_GAZE_P2));
  float k  = HE_K_MAX * gaze / HE_GAZE_AMP;
  float pupilX = 64.0f + gaze;

  float pitch    = hePitch[variant[11]];
  float invPitch = 1.0f / pitch;
  float edge     = HE_AA_GAIN * pitch;
  float flow     = HE_FLOW * t;
  float lidW     = HE_LID_W * pitch;
  float lidGap   = HE_LID_GAP * pitch;
  // Start of the white stroke: the first ring then butts against the pupil at
  // full width instead of being sliced in half by it.
  float phaseIn  = 0.5f - (1.0f + HE_DUTY) * 0.25f;

  uint16_t *out = fb;
  for (int y = 0; y < SCREEN_H; y++) {
    float dyc = (float)y + 0.5f - 64.0f;
    float ady = fabsf(dyc);
    for (int x = 0; x < SCREEN_W; x++) {
      float s, invGrad, pupil = 1.0f;

      if (ady <= heLensH[x]) {
        float dx = (float)x + 0.5f - pupilX;
        float r  = sqrtf(dx * dx + dyc * dyc);
        if (r < 0.01f) r = 0.01f;
        float invR  = 1.0f / r;
        float invR3 = invR * invR * invR;
        // The squeeze is pinned to the pupil edge: zero there, growing
        // outward. The rings only move because the pupil does.
        float b  = r + k * dx * (1.0f - HE_R_PUPIL * invR);
        float gx = dx * invR + k * (1.0f - HE_R_PUPIL * dyc * dyc * invR3);
        float gy = dyc * invR + k * HE_R_PUPIL * dx * dyc * invR3;
        invGrad = 1.0f / sqrtf(gx * gx + gy * gy);
        s = (b - HE_R_PUPIL) * invPitch + phaseIn;
        pupil = (b - HE_R_PUPIL) * invGrad;     // pupil edge softened over a pixel
      } else {
        invGrad = heLensInvG[x];
        s = (ady - heLensH[x] + flow) * invPitch;
      }

      // A triangle wave rather than a square one: its slope hands us the
      // anti-aliasing for free, and where the bands crowd tighter than the
      // screen can hold it sags towards grey instead of breaking into moire.
      float fr  = s - floorf(s);
      float tri = 1.0f - 2.0f * fabsf(2.0f * fr - 1.0f);
      float v   = 0.5f + (tri + HE_DUTY) * edge * invGrad;
      if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;
      if (pupil < 1.0f) {
        if (pupil < 0.0f) pupil = 0.0f;
        v *= pupil;
      }

      // The one line that never moves: a white stroke kept legible by a black
      // shoulder each side, which is what the drifting bands die against.
      float dl = fabsf(ady - heLensH[x]) * heLensInvG[x];
      float shoulder = lidW + lidGap - dl;
      if (shoulder > 0.0f) {
        v *= 1.0f - (shoulder > 1.0f ? 1.0f : shoulder);
        float white = lidW - dl;
        if (white > 0.0f) v = fmaxf(v, white > 1.0f ? 1.0f : white);
      }
      *out++ = heGrey[(int)(v * 31.0f + 0.5f)];
    }
  }
  tft.pushImage(0, 0, SCREEN_W, SCREEN_H, fb);
}

// ======================================================================
// framework
// ======================================================================

static void initAnim(int idx) {
  animStart = millis();
  animTime = 0;
  switch (idx) {
    case 3: initBuddha(); break;
    case 4: initSwarm(); break;
    case 6: initSphereColor(); break;
    case 7: initCelestial(); break;
    case 8:
      eBlinking = false; eNextBlink = 1.5f;
      eBgRed = true; eBgT = BG_RED_TIME; eGlitching = false; eGCount = 0;
      eMoodTimer = 10.0f;
      eIntro = true; eIntroT = 0;        // slow wake-up each time the eye starts
      break;
    case 9: initTunnel(); break;
    case 10: initWorldRing(); break;
    case 11: initHypnoEye(); break;
    default: break;
  }
  Serial.printf("anim %d\n", idx);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== Cyber Cycle ===");

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true);
  tft.setAttribute(CP437_SWITCH, 1);   // so 0xF8 gives the degree ring

  randomSeed(analogRead(4) * millis());

  // Default variant per view (untouched-by-buttons look)
  variant[0] = 1;   // Morph : widest chroma bleed (the one that smears most)
  variant[2] = 1;   // Waves: the most zoomed-in (12-line) variant by default
  variant[6] = 0;   // SphereColor: flowing colour-wave variant by default
  variant[7] = 0;   // Scan: medium contour density
  variant[10] = 0;  // World: graticule and continents together
  variant[11] = 0;  // Hypno: medium band weight

  btnLeft.attachClick([]() {                 // next view in the schedule
    slot = (slot + 1) % N_VIEWS;
    initAnim(viewOrder[slot]);
  });
  btnRight.attachClick([]() {                 // variant of the current view
    int v = viewOrder[slot];
    variant[v] = (variant[v] + 1) % variantCount[v];
    Serial.printf("view %d variant %d\n", v, variant[v]);
  });

  initAnim(viewOrder[slot]);
  lastFrameTime = millis();
}

void loop() {
  unsigned long now = millis();
  float dt = (now - lastFrameTime) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;
  lastFrameTime = now;
  animTime += dt;

  btnLeft.tick();
  btnRight.tick();
  otaTick();

  switch (viewOrder[slot]) {
    case 0: animMorph(dt);         break;
    case 1: animGrid(animTime);    break;
    case 2: animWaves(animTime);   break;
    case 3: animBuddha(dt);        break;
    case 4: animSwarm(dt);         break;
    case 5: animMosaic(animTime);  break;
    case 6: animSphereColor(animTime); break;
    case 7: animCelestial(animTime); break;
    case 8: animEye(dt);           break;
    case 9: animTunnel(dt);        break;
    case 10: animWorldRing(animTime); break;
    case 11: animHypnoEye(animTime); break;
  }

  if (now - animStart >= ANIM_MS) {
    slot = (slot + 1) % N_VIEWS;
    initAnim(viewOrder[slot]);
  }

  delay(5);
}
