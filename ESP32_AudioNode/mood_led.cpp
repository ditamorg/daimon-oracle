#include "mood_led.h"
#include "config.h"

#include <Arduino.h>
#include <WiFiUdp.h>
#include <Adafruit_NeoPixel.h>

// =============================================================================
//  mood_led.cpp  —  Animated mood LED strip on GPIO 14
//
//  Strip type: SK6812 RGBW  →  NEO_GRBW, 4 channels per pixel.
//  White channel (W) is kept at 0 — colours come purely from RGB.
//  Raise W in any anim function for a warmer/whiter look.
//
//  Each mood is a looping animation updated every 20 ms (50 Hz).
//  Tick counter resets to 0 on every mood change.
//
//  Mood → animation:
//    OFF     (0x30) — all pixels off
//    CALM    (0x31) — slow blue sine breathe, period ~4 s
//    HAPPY   (0x32) — warm amber pulse, period ~1.5 s
//    TENSE   (0x33) — fast red flicker, random per-pixel brightness
//    THINK   (0x34) — slow purple fade, period ~6 s
//    SPEAK   (0x35) — cyan Larson scanner (bouncing chase)
//    LISTEN  (0x36) — soft green throb at ~50 bpm
//    CUSTOM  (0x3F) — static R G B from UDP payload bytes 1-3
// =============================================================================

// GRBW pixel order, 800 kHz — correct for SK6812 RGBW
static Adafruit_NeoPixel strip(NUM_MOOD_LEDS, PIN_MOOD_LED, NEO_GRBW + NEO_KHZ800);

static WiFiUDP  udpMood;
static uint8_t  currentMood = CTRL_MOOD_OFF;
static uint32_t moodTick    = 0;
static uint8_t  customR = 255, customG = 255, customB = 255;

// ── Utilities ─────────────────────────────────────────────────────────────────

static uint8_t sinByte(uint8_t t) {
    return (uint8_t)(127.5f + 127.5f * sinf(t * (2.0f * M_PI / 256.0f)));
}

// Scale a byte value by brightness 0-255
static uint8_t sc(uint8_t value, uint8_t brightness) {
    return (uint16_t)value * brightness / 255;
}

static void clearStrip() {
    strip.clear();
    strip.show();
}

// ── Animations ────────────────────────────────────────────────────────────────

static void animCalm() {
    // Default idle colour: warm orange (255, 115, 0) slow breathe
    uint8_t phase = (uint8_t)((moodTick % 200) * 255 / 200);
    uint8_t bri   = (sinByte(phase) >> 1) + 20;   // 20-148, never fully off
    uint32_t col  = strip.Color(sc(255, bri), sc(115, bri), 0, 0);
    for (int i = 0; i < NUM_MOOD_LEDS; i++) strip.setPixelColor(i, col);
    strip.show();
}

static void animHappy() {
    uint8_t phase = (uint8_t)((moodTick % 75) * 255 / 75);
    uint8_t bri   = (sinByte(phase) >> 1) + 80;   // 80-208, always bright
    uint32_t col  = strip.Color(sc(255, bri), sc(160, bri), 0, 0);
    for (int i = 0; i < NUM_MOOD_LEDS; i++) strip.setPixelColor(i, col);
    strip.show();
}

static void animTense() {
    for (int i = 0; i < NUM_MOOD_LEDS; i++) {
        uint8_t bri = (uint8_t)random(60, 255);
        strip.setPixelColor(i, strip.Color(bri, 0, 0, 0));
    }
    strip.show();
}

static void animThink() {
    uint8_t phase = (uint8_t)((moodTick % 300) * 255 / 300);
    uint8_t bri   = (sinByte(phase) >> 2) + 30;   // 30-94, dim and meditative
    uint32_t col  = strip.Color(sc(160, bri), 0, sc(255, bri), 0);
    for (int i = 0; i < NUM_MOOD_LEDS; i++) strip.setPixelColor(i, col);
    strip.show();
}

static void animSpeak() {
    int period = NUM_MOOD_LEDS * 2;
    int pos    = (int)(moodTick % (uint32_t)period);
    int pixel  = (pos < NUM_MOOD_LEDS) ? pos : (period - 1 - pos);
    strip.clear();
    for (int i = 0; i < NUM_MOOD_LEDS; i++) {
        int dist = abs(i - pixel);
        if      (dist == 0) strip.setPixelColor(i, strip.Color(0, 220, 255, 0));
        else if (dist == 1) strip.setPixelColor(i, strip.Color(0,  60,  80, 0));
        else if (dist == 2) strip.setPixelColor(i, strip.Color(0,  15,  20, 0));
    }
    strip.show();
}

static void animListen() {
    uint8_t phase = (uint8_t)((moodTick % 60) * 255 / 60);
    uint8_t bri   = (sinByte(phase) >> 1) + 30;   // 30-158
    uint32_t col  = strip.Color(0, sc(220, bri), sc(60, bri), 0);
    for (int i = 0; i < NUM_MOOD_LEDS; i++) strip.setPixelColor(i, col);
    strip.show();
}

static void animCustom() {
    uint32_t col = strip.Color(customR, customG, customB, 0);
    for (int i = 0; i < NUM_MOOD_LEDS; i++) strip.setPixelColor(i, col);
    strip.show();
}

// ── Public API ────────────────────────────────────────────────────────────────

void mood_led_set(uint8_t mood) {
    if (mood != currentMood) {
        currentMood = mood;
        moodTick    = 0;
        if (mood == CTRL_MOOD_OFF) clearStrip();
    }
}

uint8_t mood_led_get_current() {
    return currentMood;
}

void mood_led_init(uint16_t udpPort) {
    strip.begin();
    strip.setBrightness(255);

    // Init: all pixels white at 10% so you can confirm wiring on boot
    uint32_t initWhite = strip.Color(25, 25, 25, 25);
    for (int i = 0; i < NUM_MOOD_LEDS; i++) strip.setPixelColor(i, initWhite);
    strip.show();

    bool ok = udpMood.begin(udpPort);
    Serial.printf("Mood LED: GPIO %d, %d pixels, UDP :%d bind=%s\n",
                  PIN_MOOD_LED, NUM_MOOD_LEDS, udpPort, ok ? "OK" : "FAIL");

    // Boot into CALM (orange breathe) as default idle state
    currentMood = CTRL_MOOD_CALM;
    moodTick    = 0;
    randomSeed(analogRead(0));
}

void mood_led_tick() {
    int pktSize = udpMood.parsePacket();
    if (pktSize >= 1) {
        uint8_t buf[4] = {0, 0, 0, 0};
        int got = udpMood.read(buf, sizeof(buf));
        uint8_t cmd = buf[0];
        Serial.printf("Mood UDP rx: %d bytes, cmd=0x%02X from %s\n",
                      got, cmd, udpMood.remoteIP().toString().c_str());

        currentMood = cmd;   // always update, even if same (re-trigger animation)
        moodTick    = 0;
        if (cmd == CTRL_MOOD_OFF) clearStrip();

        if (cmd == CTRL_MOOD_CUSTOM && pktSize >= 4) {
            customR = buf[1];
            customG = buf[2];
            customB = buf[3];
            Serial.printf("Custom colour: R=%d G=%d B=%d\n", customR, customG, customB);
        }
    }

    switch (currentMood) {
        case CTRL_MOOD_OFF:                        break;
        case CTRL_MOOD_CALM:   animCalm();         break;
        case CTRL_MOOD_HAPPY:  animHappy();        break;
        case CTRL_MOOD_TENSE:  animTense();        break;
        case CTRL_MOOD_THINK:  animThink();        break;
        case CTRL_MOOD_SPEAK:  animSpeak();        break;
        case CTRL_MOOD_LISTEN: animListen();       break;
        case CTRL_MOOD_CUSTOM: animCustom();       break;
        default:                                   break;
    }

    moodTick++;
}
