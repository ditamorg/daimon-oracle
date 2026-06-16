#pragma once
#include <stdint.h>

// ── LED states (mirror the server control bytes) ──────────────────────────────
enum LedState : uint8_t {
    LED_BOOT        = 0x00,   // slow blue pulse — WiFi connecting
    LED_IDLE        = 0x10,   // dim green steady — idle, server reachable
    LED_CALLING     = 0x11,   // fast white blink 200ms — outgoing ring
    LED_RINGING     = 0x12,   // fast cyan blink 160ms  — incoming ring
    LED_IN_CALL     = 0x13,   // solid bright green — call active
    LED_FAILED      = 0x14,   // 3× red flash → green
    LED_ERROR       = 0x15,   // solid red
    LED_LOOPBACK    = 0x20,   // solid blue — loopback test
    LED_NO_SERVER   = 0xF0,   // dim yellow — no ping response
};

void     led_init();
void     led_set(LedState s);
LedState led_get();
void     led_tick();           // call from ui_task every 20 ms
