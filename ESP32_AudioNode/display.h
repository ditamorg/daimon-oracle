#pragma once
#include <stdint.h>

// =============================================================================
//  display.h  —  ILI9163C 128×128 TFT display driver
//
//  Receives UDP packets on port 5008 from bridge.py.
//
//  Packet formats:
//    DISP_FILL    (0x40)  3 bytes: cmd  colHi  colLo    (R5G6B5 colour)
//    DISP_MOOD    (0x41)  3 bytes: cmd  colHi  colLo    (mirror mood colour)
//    DISP_TEXT    (0x42)  3+N bytes: cmd len [N bytes UTF-8]
//    DISP_CLEAR   (0x43)  1 byte:  cmd                  (fill black)
//    DISP_BRIGHTNESS (0x44) 2 bytes: cmd level 0-255
// =============================================================================

void display_init(uint16_t udpPort);

// Call every 20ms from ui_task — handles incoming UDP commands
void display_tick();

// Direct API (called internally or from other modules)
void display_fill(uint16_t colour565);
void display_clear();
void display_print(const char* text, uint16_t colour565, uint16_t bg565);
