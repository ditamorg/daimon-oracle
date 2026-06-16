#pragma once

// =============================================================================
//  config.h  —  Edit these before flashing
// =============================================================================

// ── WiFi fallback (compiled in, used if LittleFS config is absent/reset) ──────
#define WIFI_FALLBACK_SSID   "BillAir_IoT"
#define WIFI_FALLBACK_PASS   "Kocicky1337+"

// ── Server (bridge.py) ────────────────────────────────────────────────────────
#define DEFAULT_SERVER_IP    "192.168.8.242"

// ── UDP ports ─────────────────────────────────────────────────────────────────
#define DEFAULT_PORT_MIC_TX   5004   // ESP32 → server  (mic PCM)
#define DEFAULT_PORT_CTRL     5006   // bidirectional   (control bytes)

// ── Audio ─────────────────────────────────────────────────────────────────────
#define SAMPLE_RATE           8000
#define FRAME_SAMPLES         160    // 20 ms at 8 kHz
#define FRAME_BYTES           320    // 160 × 2 bytes s16le
#define DEFAULT_MIC_GAIN      16     // right-shift of 32-bit I2S word → s16
                                     // lower = louder (range 8–24)

// ── Pin assignments ───────────────────────────────────────────────────────────
// INMP441 (I2S microphone)
#define PIN_MIC_SCK    4
#define PIN_MIC_WS     5
#define PIN_MIC_SD     6

// WS2812B status LED
#define PIN_LED        38
#define NUM_LEDS       1

// Button (active LOW, internal pull-up)
#define PIN_BUTTON     0

// ── Heartbeat ─────────────────────────────────────────────────────────────────
#define PING_INTERVAL_MS   5000   // send CTRL_PING every 5 s

// ── Node identity ─────────────────────────────────────────────────────────────
#define DEFAULT_NODE_NAME  "Node-1"
#define AP_PASSWORD        "configure123"

// ── GPIO safety map (ESP32-S3 with built-in USB) ─────────────────────────────
//
//  AVOID these pins — held by peripherals at boot:
//    GPIO 0        — strapping pin (boot mode), use as input only (button OK)
//    GPIO 12–15    — USB-JTAG when using built-in USB (CDC/JTAG)
//    GPIO 19–20    — USB D- / D+ (built-in USB PHY)
//    GPIO 26–32    — SPI flash / PSRAM (do not touch)
//    GPIO 45–46    — strapping pins
//    GPIO 48       — onboard RGB LED on most DevKit boards
//
//  SAFE for general IO (no conflicts with built-in USB):
//    GPIO 1–11, 16–18, 21, 33–40, 47
//
// ── Mood LED (separate animation channel) ─────────────────────────────────────
#define PIN_MOOD_LED          14   // Mood LED strip
#define NUM_MOOD_LEDS         25   // adjust to your strip length
#define DEFAULT_PORT_MOOD_RX  5007 // bridge → ESP32 mood commands

// Mood LED control bytes (0x30–0x3F)
// Sent from bridge.py on UDP port 5007
#define CTRL_MOOD_OFF         0x30
#define CTRL_MOOD_CALM        0x31   // slow blue breathe
#define CTRL_MOOD_HAPPY       0x32   // warm amber pulse
#define CTRL_MOOD_TENSE       0x33   // fast red flicker
#define CTRL_MOOD_THINK       0x34   // slow purple fade
#define CTRL_MOOD_SPEAK       0x35   // cyan chase
#define CTRL_MOOD_LISTEN      0x36   // soft green throb
#define CTRL_MOOD_CUSTOM      0x3F   // next 3 bytes = R G B

// ── ILI9163C Display (SPI) ────────────────────────────────────────────────────
//  128×128 pixel TFT, write-only SPI (no MISO needed)
//  All pins in the safe zone for ESP32-S3 with built-in USB
#define PIN_TFT_SCK    36   // SPI clock
#define PIN_TFT_MOSI   35   // SPI data (SDA)
#define PIN_TFT_CS     39   // Chip select  (active LOW)
#define PIN_TFT_DC     40   // Data / Command
#define PIN_TFT_RST    47   // Reset        (active LOW)

#define TFT_WIDTH      128
#define TFT_HEIGHT     128

#define DEFAULT_PORT_DISPLAY_RX  5008  // bridge → ESP32 display commands

// Display command bytes (0x40–0x5F)
#define DISP_FILL          0x40   // 1 byte cmd + 2 bytes R5G6B5 colour → fill screen
#define DISP_MOOD          0x41   // mirror mood: fill with mood colour
#define DISP_TEXT          0x42   // 1 byte cmd + 1 byte len + N bytes UTF-8 text
#define DISP_CLEAR         0x43   // fill black
#define DISP_BRIGHTNESS    0x44   // 1 byte cmd + 1 byte 0-255 (future PWM backlight)

// ── Touch sensor ─────────────────────────────────────────────────────────────
#define PIN_TOUCH                1      // Capacitive touch input (ESP32-S3 touch_pad_t1)

// Touch tuning — adjustable via web UI config page
#define DEFAULT_TOUCH_THRESHOLD  20     // % deviation from baseline to trigger (lower = more sensitive)
#define DEFAULT_TOUCH_DEBOUNCE   80     // ms — ignore jitter shorter than this
#define DEFAULT_TOUCH_HOLDOFF    500    // ms — min gap between release and next trigger (anti-bounce)
#define TOUCH_SAMPLES            16     // baseline averaging samples at boot
#define TOUCH_RECAL_INTERVAL_MS  30000  // auto-recalibrate baseline every 30s when idle
