#include "ctrl.h"
#include "led.h"
#include "config.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

// =============================================================================
//  ctrl.cpp  —  Bidirectional UDP control channel
//
//  ESP32 → server:  CTRL_PING (heartbeat), CTRL_CALL, CTRL_HANGUP
//  Server → ESP32:  LED state commands, loopback on/off
//
//  Ping packet format (5 bytes):
//    byte 0     = 0xFF  (CTRL_PING)
//    bytes 1–4  = ESP32 IPv4 address (big-endian octets)
//  Bridge reads the IP from bytes 1–4 and auto-updates its target address.
//  Single-byte 0xFF pongs from the server are still handled correctly.
// =============================================================================

static WiFiUDP   udpCtrl;
static char      serverIp[32]  = DEFAULT_SERVER_IP;
static uint16_t  serverPort    = DEFAULT_PORT_CTRL;

static uint32_t  lastPingSent  = 0;
static uint32_t  lastPongRx    = 0;
static bool      serverAlive_  = false;

extern bool loopbackActive;

// ── Public API ────────────────────────────────────────────────────────────────

void ctrl_init(const char* ip, uint16_t port) {
    strncpy(serverIp, ip, sizeof(serverIp) - 1);
    serverPort = port;
    udpCtrl.begin(port);
}

void ctrl_set_server(const char* ip, uint16_t port) {
    strncpy(serverIp, ip, sizeof(serverIp) - 1);
    serverPort = port;
}

void ctrl_send(uint8_t byte) {
    udpCtrl.beginPacket(serverIp, serverPort);
    udpCtrl.write(&byte, 1);
    udpCtrl.endPacket();
}

// Send ping with embedded IP so bridge always knows our address
static void ctrl_send_ping() {
    IPAddress ip = WiFi.localIP();
    uint8_t buf[5] = {
        CTRL_PING,
        ip[0], ip[1], ip[2], ip[3]
    };
    udpCtrl.beginPacket(serverIp, serverPort);
    udpCtrl.write(buf, sizeof(buf));
    udpCtrl.endPacket();
    Serial.printf("Ping → %s  myIP=%d.%d.%d.%d\n",
                  serverIp, ip[0], ip[1], ip[2], ip[3]);
}

bool ctrl_server_alive() {
    return serverAlive_;
}

// ── Poll — call every 20ms from ui_task ───────────────────────────────────────

void ctrl_poll() {
    uint32_t now = millis();

    // ── Send heartbeat with embedded IP every PING_INTERVAL_MS ───────────────
    if (now - lastPingSent >= PING_INTERVAL_MS) {
        lastPingSent = now;
        ctrl_send_ping();
    }

    // ── Check server liveness (10 s timeout) ─────────────────────────────────
    bool wasAlive = serverAlive_;
    serverAlive_  = (now - lastPongRx) < 10000;
    if (wasAlive && !serverAlive_) {
        led_set(LED_NO_SERVER);
    } else if (!wasAlive && serverAlive_) {
        led_set(LED_IDLE);
    }

    // ── Receive incoming control bytes (non-blocking) ─────────────────────────
    int pktSize = udpCtrl.parsePacket();
    if (pktSize <= 0) return;

    uint8_t buf[4];
    int len = udpCtrl.read(buf, sizeof(buf));
    if (len <= 0) return;

    uint8_t cmd = buf[0];

    switch (cmd) {
        case CTRL_PING:
            lastPongRx = now;
            break;

        case CTRL_LED_IDLE:
            led_set(LED_IDLE);
            break;

        case CTRL_LED_CALLING:
            led_set(LED_CALLING);
            break;

        case CTRL_LED_RINGING:
            led_set(LED_RINGING);
            break;

        case CTRL_LED_IN_CALL:
            led_set(LED_IN_CALL);
            break;

        case CTRL_LED_FAILED:
            led_set(LED_FAILED);
            break;

        case CTRL_LED_ERROR:
            led_set(LED_ERROR);
            break;

        case CTRL_LOOPBACK_ON:
            loopbackActive = true;
            led_set(LED_LOOPBACK);
            break;

        case CTRL_LOOPBACK_OFF:
            loopbackActive = false;
            led_set(serverAlive_ ? LED_IDLE : LED_NO_SERVER);
            break;

        default:
            break;
    }
}
