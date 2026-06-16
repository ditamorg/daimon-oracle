#pragma once
#include <stdint.h>
#include <stddef.h>

// =============================================================================
//  audio.h  —  INMP441 I2S capture + UDP mic TX
// =============================================================================

// Call once after WiFi is up
void audio_init(const char* serverIp, uint16_t micPort);

// Update server IP/port at runtime (e.g. after web UI config change)
void audio_set_server(const char* serverIp, uint16_t micPort);

// FreeRTOS task — pin to Core 1, priority 5
void audioTxTask(void* pvParams);
