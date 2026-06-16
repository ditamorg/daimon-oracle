#include "audio.h"
#include "config.h"

#include <Arduino.h>
#include <WiFiUdp.h>
#include <driver/i2s_std.h>   // ESP-IDF v5 I2S driver (Arduino ESP32 ≥3.x)

// =============================================================================
//  audio.cpp  —  INMP441 I2S microphone + UDP TX to bridge.py
//
//  I2S configuration:
//    - Port:        I2S_NUM_0
//    - Mode:        standard (MSB / Philips), mono RX
//    - Word width:  32-bit frames (INMP441 outputs 24-bit left-aligned)
//    - Sample rate: 8000 Hz
//    - L/R pin:     GND → left channel selected
//
//  Sample extraction:
//    Raw 32-bit word from INMP441 has 24-bit audio data in bits [31:8].
//    Right-shift by micGain (default 16) to get a signed 16-bit sample.
//    Gain 16 → centre of useful range. Lower value = louder.
// =============================================================================

static i2s_chan_handle_t  rxHandle  = nullptr;
static WiFiUDP            udpMic;

static char     serverIp[32]  = DEFAULT_SERVER_IP;
static uint16_t serverPort    = DEFAULT_PORT_MIC_TX;
static uint8_t  micGain       = DEFAULT_MIC_GAIN;

// ── I2S init ──────────────────────────────────────────────────────────────────

static void i2s_mic_init() {
    // Channel config
    i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chanCfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chanCfg, nullptr, &rxHandle));

    // Standard (Philips/MSB) mode, 32-bit slot, 8 kHz
    i2s_std_config_t stdCfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)PIN_MIC_SCK,
            .ws   = (gpio_num_t)PIN_MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = (gpio_num_t)PIN_MIC_SD,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    // INMP441 with L/R=GND transmits on left slot
    stdCfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rxHandle, &stdCfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rxHandle));
}

// ── Public API ────────────────────────────────────────────────────────────────

void audio_init(const char* ip, uint16_t port) {
    strncpy(serverIp, ip, sizeof(serverIp) - 1);
    serverPort = port;
    i2s_mic_init();
    udpMic.begin(0);   // bind to any local port for TX
}

void audio_set_server(const char* ip, uint16_t port) {
    strncpy(serverIp, ip, sizeof(serverIp) - 1);
    serverPort = port;
}

void audio_set_gain(uint8_t gain) {
    micGain = gain;
}

// ── Audio TX task (Core 1, priority 5) ───────────────────────────────────────
//
//  This task is driven entirely by the I2S DMA hardware clock.
//  i2s_channel_read() blocks until exactly FRAME_SAMPLES 32-bit words
//  are available in the DMA buffer, which happens every 20 ms at 8 kHz.
//  No vTaskDelay needed — timing comes from hardware.

void audioTxTask(void* pvParams) {
    // Raw DMA buffer: FRAME_SAMPLES 32-bit words
    static int32_t  rawBuf[FRAME_SAMPLES];
    // Output: FRAME_SAMPLES 16-bit samples (320 bytes)
    static int16_t  pcmBuf[FRAME_SAMPLES];

    size_t bytesRead = 0;

    for (;;) {
        // Block until one full 20ms frame is in the DMA buffer
        esp_err_t err = i2s_channel_read(
            rxHandle,
            rawBuf,
            sizeof(rawBuf),
            &bytesRead,
            portMAX_DELAY
        );

        if (err != ESP_OK || bytesRead != sizeof(rawBuf)) {
            // DMA overrun or init issue — skip this frame
            continue;
        }

        // Convert 32-bit MSB-aligned I2S words → 16-bit signed PCM
        // INMP441: data is in bits [31:8], right-shift by micGain (default 16)
        for (int i = 0; i < FRAME_SAMPLES; i++) {
            pcmBuf[i] = (int16_t)(rawBuf[i] >> micGain);
        }

        // Send raw s16le PCM frame to bridge.py
        udpMic.beginPacket(serverIp, serverPort);
        udpMic.write((const uint8_t*)pcmBuf, FRAME_BYTES);
        udpMic.endPacket();
    }
}
