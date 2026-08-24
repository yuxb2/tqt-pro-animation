// LilyGO T-QT Pro (ESP32-S3, GC9A01 128x128)
// This local TFT_eSPI configuration makes Arduino IDE and GitHub Actions use
// the same display wiring without modifying the global TFT_eSPI library.
#define USER_SETUP_ID 211
#define USE_HSPI_PORT
#define GC9A01_DRIVER
#define TFT_WIDTH 128
#define TFT_HEIGHT 128
#define TFT_BACKLIGHT_ON 0
#define CGRAM_OFFSET
#define DISABLE_ALL_LIBRARY_WARNINGS
#define TFT_BL 10
#define TFT_MISO -1
#define TFT_MOSI 2
#define TFT_SCLK 3
#define TFT_CS 5
#define TFT_DC 6
#define TFT_RST 1
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 20000000
#define SPI_TOUCH_FREQUENCY 2500000
