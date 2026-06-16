#include "led.h"
#include "config.h"
#include <Adafruit_NeoPixel.h>

// =============================================================================
//  led.cpp  —  WS2812B single-pixel state machine
//  Library: Adafruit NeoPixel
// =============================================================================

static Adafruit_NeoPixel strip(NUM_LEDS, PIN_LED, NEO_GRB + NEO_KHZ800);
static LedState currentState = LED_BOOT;
static uint32_t tickCount    = 0;   // incremented every led_tick() call (20ms steps)

// ── Colour helpers ────────────────────────────────────────────────────────────

static inline uint32_t dim(uint32_t c, uint8_t brightness) {
    uint8_t r = ((c >> 16) & 0xFF) * brightness / 255;
    uint8_t g = ((c >>  8) & 0xFF) * brightness / 255;
    uint8_t b = ((c >>  0) & 0xFF) * brightness / 255;
    return strip.Color(r, g, b);
}

static const uint32_t C_GREEN  = 0x00FF00;
static const uint32_t C_CYAN   = 0x00FFFF;
static const uint32_t C_WHITE  = 0xFFFFFF;
static const uint32_t C_BLUE   = 0x0000FF;
static const uint32_t C_RED    = 0xFF0000;
static const uint32_t C_YELLOW = 0xFFAA00;
static const uint32_t C_OFF    = 0x000000;

static void setPixel(uint32_t colour) {
    strip.setPixelColor(0, colour);
    strip.show();
}

// ── Public API ────────────────────────────────────────────────────────────────

void led_init() {
    strip.begin();
    strip.setBrightness(255);
    strip.clear();
    strip.show();
}

void led_set(LedState s) {
    currentState = s;
    tickCount    = 0;
}

LedState led_get() {
    return currentState;
}

// Called every 20 ms from ui_task
void led_tick() {
    tickCount++;

    switch (currentState) {

        // Slow blue sine-ish pulse (boot / WiFi connecting)
        // Period ~2 s = 100 ticks. Use triangle wave.
        case LED_BOOT: {
            uint32_t phase = tickCount % 100;
            uint8_t  bri   = (phase < 50) ? (phase * 5) : ((100 - phase) * 5);
            setPixel(dim(C_BLUE, bri));
            break;
        }

        // Dim steady green — idle, server OK
        case LED_IDLE:
            setPixel(dim(C_GREEN, 30));
            break;

        // Dim steady yellow — idle, no server ping
        case LED_NO_SERVER:
            setPixel(dim(C_YELLOW, 25));
            break;

        // Fast white blink 200ms (10 ticks ON, 10 OFF)
        case LED_CALLING:
            setPixel((tickCount % 10 < 5) ? dim(C_WHITE, 200) : C_OFF);
            break;

        // Fast cyan blink 160ms (8 ticks ON, 8 OFF)
        case LED_RINGING:
            setPixel((tickCount % 8 < 4) ? dim(C_CYAN, 220) : C_OFF);
            break;

        // Solid bright green — in call
        case LED_IN_CALL:
            setPixel(dim(C_GREEN, 200));
            break;

        // 3× red flash then settle to dim green
        // Each flash: 5 ticks ON (100ms), 5 ticks OFF (100ms) → 30 ticks = 3 flashes
        case LED_FAILED: {
            if (tickCount <= 30) {
                setPixel((tickCount % 10 < 5) ? dim(C_RED, 200) : C_OFF);
            } else {
                currentState = LED_IDLE;   // auto-transition after animation
                setPixel(dim(C_GREEN, 30));
            }
            break;
        }

        // Solid red — error
        case LED_ERROR:
            setPixel(dim(C_RED, 180));
            break;

        // Solid blue — loopback test active
        case LED_LOOPBACK:
            setPixel(dim(C_BLUE, 180));
            break;

        default:
            setPixel(C_OFF);
            break;
    }
}
