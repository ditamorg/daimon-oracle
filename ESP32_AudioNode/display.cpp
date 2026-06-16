#include "display.h"
#include "config.h"

#include <Arduino.h>
#include <WiFiUdp.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>   // ILI9163C is register-compatible with ST7735S

// =============================================================================
//  display.cpp  —  ILI9163C 128×128 TFT via Adafruit ST7735 library
//
//  Library: Adafruit ST7735 and ST7789 Library  (install via Library Manager)
//  Dependency: Adafruit GFX Library             (install via Library Manager)
//
//  The ILI9163C is register-compatible with the ST7735S.
//  Use initR(INITR_144GREENTAB) for the 128×128 green-tab variant.
//
//  SPI pins (hardware SPI on ESP32-S3):
//    SCK  → GPIO 36
//    MOSI → GPIO 35
//    CS   → GPIO 34
//    DC   → GPIO 33
//    RST  → GPIO 47
// =============================================================================

static Adafruit_ST7735 tft = Adafruit_ST7735(
    PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_MOSI, PIN_TFT_SCK, PIN_TFT_RST
);

static WiFiUDP udpDisp;
static bool    dispReady = false;

// ── Colour helpers ────────────────────────────────────────────────────────────

// Convert 24-bit RGB888 to 16-bit RGB565
static uint16_t rgb888to565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// ── Public API ────────────────────────────────────────────────────────────────

void display_fill(uint16_t colour565) {
    if (!dispReady) return;
    tft.fillScreen(colour565);
}

void display_clear() {
    if (!dispReady) return;
    tft.fillScreen(ST77XX_BLACK);
}

void display_print(const char* text, uint16_t colour565, uint16_t bg565) {
    if (!dispReady) return;
    tft.fillScreen(bg565);
    tft.setTextColor(colour565);
    tft.setTextSize(2);
    tft.setTextWrap(true);
    // Centre text block vertically — estimate line height = 16px at size 2
    int lines   = 1;
    for (const char* p = text; *p; p++) if (*p == '\n') lines++;
    int16_t y   = (TFT_HEIGHT - lines * 16) / 2;
    tft.setCursor(4, max((int16_t)4, y));
    tft.print(text);
}

// ── Init ──────────────────────────────────────────────────────────────────────

void display_init(uint16_t udpPort) {
    // initR with INITR_144GREENTAB = 128×128 display
    tft.initR(INITR_144GREENTAB);
    tft.setRotation(0);
    tft.fillScreen(ST77XX_BLACK);

    // Boot splash — dim white so you can confirm wiring
    tft.setTextColor(tft.color565(40, 40, 40));
    tft.setTextSize(1);
    tft.setCursor(8, 56);
    tft.print("AUDIO NODE");
    tft.setCursor(8, 68);
    tft.print("connecting...");

    dispReady = true;
    udpDisp.begin(udpPort);
    Serial.printf("Display: ILI9163C 128x128, UDP :%d\n", udpPort);
}

// ── Tick — call every 20ms from ui_task ──────────────────────────────────────

void display_tick() {
    if (!dispReady) return;

    int pktSize = udpDisp.parsePacket();
    if (pktSize < 1) return;

    uint8_t buf[130] = {0};   // max: cmd(1) + len(1) + 128 bytes text
    int got = udpDisp.read(buf, sizeof(buf) - 1);
    if (got < 1) return;

    uint8_t cmd = buf[0];
    Serial.printf("Display UDP rx: cmd=0x%02X len=%d\n", cmd, got);

    switch (cmd) {

        // DISP_FILL: fill entire screen with one colour
        // Packet: 0x40  colHi  colLo
        case DISP_FILL: {
            if (got < 3) break;
            uint16_t col = ((uint16_t)buf[1] << 8) | buf[2];
            display_fill(col);
            break;
        }

        // DISP_MOOD: same as fill — bridge sends the mood colour pre-converted
        // Packet: 0x41  colHi  colLo
        case DISP_MOOD: {
            if (got < 3) break;
            uint16_t col = ((uint16_t)buf[1] << 8) | buf[2];
            // Dim the colour to ~25% for a subtle background wash
            uint8_t r = ((col >> 11) & 0x1F) << 3;
            uint8_t g = ((col >>  5) & 0x3F) << 2;
            uint8_t b = ((col      ) & 0x1F) << 3;
            uint16_t dimCol = rgb888to565(r / 4, g / 4, b / 4);
            display_fill(dimCol);
            break;
        }

        // DISP_TEXT: fill bg then print text
        // Packet: 0x42  textLen  [text bytes]  colHi colLo  bgHi bgLo
        // Simplified: text in white on black background
        case DISP_TEXT: {
            if (got < 3) break;
            uint8_t textLen = buf[1];
            if (textLen == 0 || got < 2 + textLen) break;
            buf[2 + textLen] = '\0';   // null-terminate
            uint16_t fg = ST77XX_WHITE;
            uint16_t bg = ST77XX_BLACK;
            // Optional colour bytes after text
            if (got >= 2 + textLen + 4) {
                int o = 2 + textLen;
                fg = ((uint16_t)buf[o]   << 8) | buf[o+1];
                bg = ((uint16_t)buf[o+2] << 8) | buf[o+3];
            }
            display_print((char*)&buf[2], fg, bg);
            break;
        }

        // DISP_CLEAR: black screen
        case DISP_CLEAR:
            display_clear();
            break;

        // DISP_BRIGHTNESS: future PWM backlight control (placeholder)
        case DISP_BRIGHTNESS:
            // TODO: wire backlight pin to PWM channel
            break;

        default:
            break;
    }
}
