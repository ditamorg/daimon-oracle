#pragma once
#include <stdint.h>

// =============================================================================
//  ctrl.h  —  UDP control channel (port 5006)
// =============================================================================

// ── Control bytes: ESP32 → server ─────────────────────────────────────────────
#define CTRL_PING       0xFF
#define CTRL_CALL       0x01
#define CTRL_HANGUP     0x02

// ── Control bytes: server → ESP32 ─────────────────────────────────────────────
#define CTRL_LED_IDLE       0x10
#define CTRL_LED_CALLING    0x11
#define CTRL_LED_RINGING    0x12
#define CTRL_LED_IN_CALL    0x13
#define CTRL_LED_FAILED     0x14
#define CTRL_LED_ERROR      0x15
#define CTRL_LOOPBACK_ON    0x20
#define CTRL_LOOPBACK_OFF   0x21

void ctrl_init(const char* serverIp, uint16_t ctrlPort);
void ctrl_set_server(const char* serverIp, uint16_t ctrlPort);
void ctrl_send(uint8_t byte);

// Call from ui_task every 20ms — handles incoming bytes + ping heartbeat
void ctrl_poll();

// Returns true if a ping response was received within the last 10 s
bool ctrl_server_alive();
