#pragma once
#include <stdint.h>

// =============================================================================
//  mood_led.h  —  Mood/animation LED strip (GPIO 48, separate from status LED)
//
//  Receives UDP packets on port 5007 from bridge.py.
//  Packet format:
//    1 byte  — command (0x30–0x3F)
//    3 bytes — R G B  (only for CTRL_MOOD_CUSTOM = 0x3F)
// =============================================================================

void mood_led_init(uint16_t udpPort);

// Call every 20ms from ui_task — handles incoming UDP + runs animation tick
void mood_led_tick();

// Direct mood control (used by touch.cpp)
void    mood_led_set(uint8_t mood);
uint8_t mood_led_get_current();
