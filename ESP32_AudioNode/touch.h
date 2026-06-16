#pragma once
#include <stdint.h>

// =============================================================================
//  touch.h  —  Capacitive touch sensor (GPIO 1, touch_pad_t1)
//  Hold → record starts. Release → record stops.
// =============================================================================

#define CTRL_TOUCH_ON    0x03   // ESP32 → server: touch/record began
#define CTRL_TOUCH_OFF   0x04   // ESP32 → server: touch/record released

void touch_init();
void touch_tick();

// Update tuning params at runtime (called after config load / web UI save)
void touch_set_params(uint8_t thresholdPct, uint16_t debounceMs, uint16_t holdoffMs);
