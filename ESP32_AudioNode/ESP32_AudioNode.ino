// =============================================================================
//  ESP32_AudioNode.ino
//  ESP32-S3 Art Project — Audio Node
//
//  Captures INMP441 mic audio via I2S and streams raw s16le PCM
//  to bridge.py over UDP port 5004.
//  Control channel (port 5006) handles LED state from server.
//  Web UI served on port 80 for configuration.
//
//  Board:    ESP32-S3 (any variant)
//  Arduino:  ESP32 Arduino core ≥ 3.0 (ESP-IDF v5 I2S driver)
//
//  Libraries (install via Library Manager):
//    - Adafruit NeoPixel
//    - ArduinoJson  (≥ 7.x)
//    - LittleFS     (bundled with ESP32 Arduino core)
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

#include "config.h"
#include "led.h"
#include "audio.h"
#include "ctrl.h"
#include "mood_led.h"
#include "display.h"
#include "touch.h"

// ── Global state ──────────────────────────────────────────────────────────────

bool loopbackActive = false;   // referenced by ctrl.cpp

// Runtime config (loaded from LittleFS /config.json)
struct NodeConfig {
    char     nodeName[32]  = DEFAULT_NODE_NAME;
    char     wifiSsid[64]  = WIFI_FALLBACK_SSID;
    char     wifiPass[64]  = WIFI_FALLBACK_PASS;
    char     serverIp[32]  = DEFAULT_SERVER_IP;
    uint16_t portMicTx     = DEFAULT_PORT_MIC_TX;
    uint16_t portCtrl      = DEFAULT_PORT_CTRL;
    uint8_t  micGain       = DEFAULT_MIC_GAIN;
    uint16_t portMoodRx    = DEFAULT_PORT_MOOD_RX;
    uint16_t portDisplayRx     = DEFAULT_PORT_DISPLAY_RX;
    uint8_t  touchThresholdPct = DEFAULT_TOUCH_THRESHOLD;
    uint16_t touchDebounceMs   = DEFAULT_TOUCH_DEBOUNCE;
    uint16_t touchHoldoffMs    = DEFAULT_TOUCH_HOLDOFF;
} cfg;

WebServer server(80);

// ── Config persistence ────────────────────────────────────────────────────────

bool configLoad() {
    if (!LittleFS.exists("/config.json")) return false;
    File f = LittleFS.open("/config.json", "r");
    if (!f) return false;

    JsonDocument doc;
    if (deserializeJson(doc, f)) { f.close(); return false; }
    f.close();

    strlcpy(cfg.nodeName, doc["nodeName"] | DEFAULT_NODE_NAME, sizeof(cfg.nodeName));
    strlcpy(cfg.wifiSsid, doc["wifiSsid"] | WIFI_FALLBACK_SSID, sizeof(cfg.wifiSsid));
    strlcpy(cfg.wifiPass, doc["wifiPass"] | WIFI_FALLBACK_PASS, sizeof(cfg.wifiPass));
    strlcpy(cfg.serverIp, doc["serverIp"] | DEFAULT_SERVER_IP, sizeof(cfg.serverIp));
    cfg.portMicTx = doc["portMicTx"] | DEFAULT_PORT_MIC_TX;
    cfg.portCtrl  = doc["portCtrl"]  | DEFAULT_PORT_CTRL;
    cfg.micGain   = doc["micGain"]   | DEFAULT_MIC_GAIN;
    cfg.portMoodRx    = doc["portMoodRx"]    | DEFAULT_PORT_MOOD_RX;
    cfg.portDisplayRx     = doc["portDisplayRx"]     | DEFAULT_PORT_DISPLAY_RX;
    cfg.touchThresholdPct = doc["touchThresholdPct"] | DEFAULT_TOUCH_THRESHOLD;
    cfg.touchDebounceMs   = doc["touchDebounceMs"]   | DEFAULT_TOUCH_DEBOUNCE;
    cfg.touchHoldoffMs    = doc["touchHoldoffMs"]    | DEFAULT_TOUCH_HOLDOFF;
    return true;
}

void configSave() {
    File f = LittleFS.open("/config.json", "w");
    if (!f) return;
    JsonDocument doc;
    doc["nodeName"]  = cfg.nodeName;
    doc["wifiSsid"]  = cfg.wifiSsid;
    doc["wifiPass"]  = cfg.wifiPass;
    doc["serverIp"]  = cfg.serverIp;
    doc["portMicTx"] = cfg.portMicTx;
    doc["portCtrl"]  = cfg.portCtrl;
    doc["micGain"]   = cfg.micGain;
    doc["portMoodRx"]    = cfg.portMoodRx;
    doc["portDisplayRx"]     = cfg.portDisplayRx;
    doc["touchThresholdPct"] = cfg.touchThresholdPct;
    doc["touchDebounceMs"]   = cfg.touchDebounceMs;
    doc["touchHoldoffMs"]    = cfg.touchHoldoffMs;
    serializeJson(doc, f);
    f.close();
}

// ── WiFi — station mode with AP fallback ─────────────────────────────────────

bool wifiConnect() {
    if (strlen(cfg.wifiSsid) == 0) return false;
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifiSsid, cfg.wifiPass);

    uint32_t t = millis();
    while (WiFi.status() != WL_CONNECTED) {
        led_tick();
        delay(20);
        if (millis() - t > 15000) return false;
    }
    return true;
}

void startAP() {
    char apName[48];
    snprintf(apName, sizeof(apName), "ESP32-SIP-%s", cfg.nodeName);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apName, AP_PASSWORD);
    Serial.printf("AP mode: %s  IP: %s\n", apName, WiFi.softAPIP().toString().c_str());
    led_set(LED_ERROR);
}

// ── Web UI (config page) ──────────────────────────────────────────────────────
//  Minimal HTML served from flash — no LittleFS HTML file needed.

static const char INDEX_HTML[] PROGMEM = R"html(
<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Audio Node</title>
<style>
  body{font-family:monospace;background:#0d0e10;color:#c8cad0;max-width:480px;margin:40px auto;padding:0 20px}
  h2{color:#4af0a0;letter-spacing:2px;font-size:14px;text-transform:uppercase}
  label{display:block;color:#7a7e88;font-size:11px;margin:14px 0 4px;letter-spacing:1px}
  input{width:100%;background:#1e2023;border:1px solid #3a3d42;color:#c8cad0;
        font-family:monospace;font-size:13px;padding:7px 10px;box-sizing:border-box;border-radius:2px}
  input:focus{outline:none;border-color:#00c97a}
  button{margin-top:20px;width:100%;padding:10px;background:transparent;
         border:1px solid #3a3d42;color:#7a7e88;font-family:monospace;font-size:12px;
         letter-spacing:1px;border-radius:2px;cursor:pointer}
  button:hover{border-color:#4af0a0;color:#4af0a0}
  .ok{color:#4af0a0;margin-top:12px;display:none}
  hr{border:none;border-top:1px solid #2a2c30;margin:20px 0}
  .info{font-size:11px;color:#4a4d55}
</style></head><body>
<h2>Audio Node Config</h2>
<form id="f">
  <label>Node Name</label><input name="nodeName" value="%NODE_NAME%">
  <label>WiFi SSID</label><input name="wifiSsid" value="%WIFI_SSID%">
  <label>WiFi Password</label><input name="wifiPass" type="password" value="">
  <label>Server IP (bridge.py)</label><input name="serverIp" value="%SERVER_IP%">
  <label>Mic UDP Port (TX)</label><input name="portMicTx" type="number" value="%PORT_MIC_TX%">
  <label>Control UDP Port</label><input name="portCtrl" type="number" value="%PORT_CTRL%">
  <label>Mic Gain (8–24, lower=louder)</label><input name="micGain" type="number" min="8" max="24" value="%MIC_GAIN%">
  <hr style="border:none;border-top:1px solid #2a2c30;margin:20px 0">
  <h2 style="margin-bottom:4px">Touch Sensor</h2>
  <label>Sensitivity % (5–50, lower=more sensitive)</label>
  <input name="touchThresholdPct" type="number" min="5" max="50" value="%TOUCH_THRESHOLD_PCT%">
  <small style="color:#4a4d55;font-size:10px">How much the reading must deviate from baseline to register. Reduce if touch is missed, increase if triggering by itself.</small>
  <label>Debounce ms (20–200)</label>
  <input name="touchDebounceMs" type="number" min="20" max="200" value="%TOUCH_DEBOUNCE_MS%">
  <small style="color:#4a4d55;font-size:10px">How long the signal must be stable before state changes. Increase to reject noise.</small>
  <label>Holdoff ms (100–2000)</label>
  <input name="touchHoldoffMs" type="number" min="100" max="2000" value="%TOUCH_HOLDOFF_MS%">
  <small style="color:#4a4d55;font-size:10px">Minimum gap after release before next trigger. Prevents accidental double-triggers.</small>
  <button type="submit">SAVE &amp; REBOOT</button>
</form>
<p class="ok" id="ok">Saved — rebooting…</p>
<hr>
<div class="info">IP: %LOCAL_IP% &nbsp;|&nbsp; Node: %NODE_NAME% &nbsp;|&nbsp; Uptime: %UPTIME%s</div>
<hr>
<button onclick="fetch('/reset').then(()=>{document.getElementById('ok').style.display='block'})">
  RESET CONFIG &amp; REBOOT
</button>
<script>
document.getElementById('f').addEventListener('submit', function(e){
  e.preventDefault();
  const d = Object.fromEntries(new FormData(this));
  fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)})
    .then(()=>{document.getElementById('ok').style.display='block'});
});
</script>
</body></html>
)html";

void handleRoot() {
    String html(INDEX_HTML);
    html.replace("%NODE_NAME%",  cfg.nodeName);
    html.replace("%WIFI_SSID%",  cfg.wifiSsid);
    html.replace("%SERVER_IP%",  cfg.serverIp);
    html.replace("%PORT_MIC_TX%", String(cfg.portMicTx));
    html.replace("%PORT_CTRL%",   String(cfg.portCtrl));
    html.replace("%MIC_GAIN%",    String(cfg.micGain));
    html.replace("%LOCAL_IP%",   WiFi.localIP().toString());
    html.replace("%UPTIME%",               String(millis() / 1000));
    html.replace("%TOUCH_THRESHOLD_PCT%",  String(cfg.touchThresholdPct));
    html.replace("%TOUCH_DEBOUNCE_MS%",    String(cfg.touchDebounceMs));
    html.replace("%TOUCH_HOLDOFF_MS%",     String(cfg.touchHoldoffMs));
    server.send(200, "text/html", html);
}

void handleSave() {
    if (!server.hasArg("plain")) { server.send(400); return; }
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) { server.send(400); return; }

    strlcpy(cfg.nodeName, doc["nodeName"] | cfg.nodeName, sizeof(cfg.nodeName));
    strlcpy(cfg.wifiSsid, doc["wifiSsid"] | cfg.wifiSsid, sizeof(cfg.wifiSsid));
    // Only update password if not blank
    const char* pw = doc["wifiPass"] | "";
    if (strlen(pw) > 0) strlcpy(cfg.wifiPass, pw, sizeof(cfg.wifiPass));
    strlcpy(cfg.serverIp, doc["serverIp"] | cfg.serverIp, sizeof(cfg.serverIp));
    cfg.portMicTx = doc["portMicTx"] | cfg.portMicTx;
    cfg.portCtrl  = doc["portCtrl"]  | cfg.portCtrl;
    cfg.micGain           = doc["micGain"]           | cfg.micGain;
    cfg.touchThresholdPct = doc["touchThresholdPct"] | cfg.touchThresholdPct;
    cfg.touchDebounceMs   = doc["touchDebounceMs"]   | cfg.touchDebounceMs;
    cfg.touchHoldoffMs    = doc["touchHoldoffMs"]    | cfg.touchHoldoffMs;

    configSave();
    server.send(200, "text/plain", "ok");
    delay(500);
    ESP.restart();
}

void handleReset() {
    LittleFS.remove("/config.json");
    server.send(200, "text/plain", "reset");
    delay(500);
    ESP.restart();
}

void handleStatus() {
    JsonDocument doc;
    doc["nodeName"]   = cfg.nodeName;
    doc["serverIp"]   = cfg.serverIp;
    doc["ip"]         = WiFi.localIP().toString();
    doc["rssi"]       = WiFi.RSSI();
    doc["uptime"]     = millis() / 1000;
    doc["ledState"]   = (uint8_t)led_get();
    doc["serverAlive"]= ctrl_server_alive();
    doc["loopback"]   = loopbackActive;
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

// ── Button handling ───────────────────────────────────────────────────────────

static bool     btnLastState  = HIGH;
static uint32_t btnPressTime  = 0;
static bool     btnHandled    = false;

void buttonPoll() {
    bool state = digitalRead(PIN_BUTTON);
    uint32_t now = millis();

    if (state == LOW && btnLastState == HIGH) {
        // Falling edge — button pressed
        btnPressTime = now;
        btnHandled   = false;
    }

    if (state == LOW && !btnHandled && (now - btnPressTime) > 50) {
        // Debounced press
        btnHandled = true;
        LedState ls = led_get();
        if (ls == LED_RINGING) {
            ctrl_send(CTRL_CALL);    // answer incoming
        } else if (ls == LED_IN_CALL || ls == LED_CALLING) {
            ctrl_send(CTRL_HANGUP);  // hang up
        } else {
            ctrl_send(CTRL_CALL);    // originate call
        }
    }

    btnLastState = state;
}

// ── FreeRTOS UI task ─────────────────────────────────────────────────────────
//  Runs on Core 0, handles: LED animation, button, ctrl poll, web server

void uiTask(void* pvParams) {
    for (;;) {
        led_tick();
        buttonPoll();
        ctrl_poll();
        mood_led_tick();
        display_tick();
        touch_tick();
        server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ── setup() ──────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println("\n\nESP32 Audio Node — booting");

    // LED first so we get visual feedback immediately
    led_init();
    led_set(LED_BOOT);

    // Button
    pinMode(PIN_BUTTON, INPUT_PULLUP);

    // Filesystem
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed — formatting");
    }
    configLoad();

    Serial.printf("Node: %s  Server: %s  MicPort: %d\n",
                  cfg.nodeName, cfg.serverIp, cfg.portMicTx);

    // WiFi
    Serial.printf("Connecting to WiFi: %s\n", cfg.wifiSsid);
    if (!wifiConnect()) {
        Serial.println("WiFi failed — starting AP");
        startAP();
    } else {
        Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
        led_set(LED_IDLE);
    }

    // Web server routes
    server.on("/",       HTTP_GET,  handleRoot);
    server.on("/save",   HTTP_POST, handleSave);
    server.on("/reset",  HTTP_GET,  handleReset);
    server.on("/status", HTTP_GET,  handleStatus);
    server.begin();
    Serial.printf("Web UI: http://%s\n",
                  (WiFi.getMode() == WIFI_AP)
                      ? WiFi.softAPIP().toString().c_str()
                      : WiFi.localIP().toString().c_str());

    // Audio init (only meaningful in station mode with WiFi up)
    if (WiFi.status() == WL_CONNECTED) {
        audio_init(cfg.serverIp, cfg.portMicTx);
        ctrl_init(cfg.serverIp, cfg.portCtrl);
        mood_led_init(cfg.portMoodRx);
        display_init(cfg.portDisplayRx);
        touch_init();
        touch_set_params(cfg.touchThresholdPct, cfg.touchDebounceMs, cfg.touchHoldoffMs);

        // Audio TX task — Core 1, priority 5 (real-time, I2S-driven)
        xTaskCreatePinnedToCore(
            audioTxTask,   // function
            "audioTX",     // name
            4096,          // stack bytes
            nullptr,       // params
            5,             // priority
            nullptr,       // handle
            1              // core 1
        );
    }

    // UI task — Core 0, priority 3
    xTaskCreatePinnedToCore(
        uiTask,
        "uiTask",
        4096,
        nullptr,
        3,
        nullptr,
        0
    );
}

// ── loop() ───────────────────────────────────────────────────────────────────
//  All work is done in FreeRTOS tasks. Keep loop() empty.

void loop() {
    vTaskDelay(portMAX_DELAY);
}
