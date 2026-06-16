/*
  Daimon Oracle — ESP32 Touch Controller
  Bohemian Queen Chalet · Borderland

  Touch the crystal ball → Oracle listens
  Release              → Oracle thinks and speaks

  WIRING:
  - Connect a wire/copper tape from GPIO4 (T0) to the base of the crystal ball
    (tape it under/around the PET-G dome, or wrap around the base ring)
  - That's it. ESP32 capacitive touch is built-in — no extra parts needed.

  SETUP:
  1. Set WIFI_SSID / WIFI_PASS to your hotspot
  2. Set LAPTOP_IP to your Mac's IP (shown when oracle.py starts)
  3. Upload to ESP32 via Arduino IDE (board: ESP32 Dev Module)
  4. Open Serial Monitor at 115200 to see IP and debug

  DISPLAY:
  - Uncomment TFT or MAX7219 section if you have one
  - Built-in LED (GPIO2) animates by default — always works
*/

#include <WiFi.h>
#include <HTTPClient.h>

// ── CONFIGURE THESE ───────────────────────────────────────────────────────────
const char* WIFI_SSID  = "DaimonOracle";      // your hotspot name
const char* WIFI_PASS  = "threshold";          // your hotspot password
const char* LAPTOP_IP  = "192.168.2.1";        // shown in oracle.py on startup
const int   LAPTOP_PORT = 5000;

// ── TOUCH ─────────────────────────────────────────────────────────────────────
#define TOUCH_PIN     T0          // GPIO4 — wire goes to crystal ball
#define TOUCH_THRESH  40          // lower = needs firmer touch (20-60 range)
#define DEBOUNCE_MS   80          // ms stable before registering state change

// ── LED ───────────────────────────────────────────────────────────────────────
#define LED_PIN       2           // built-in LED on most ESP32 boards

// ─────────────────────────────────────────────────────────────────────────────
// Display — uncomment ONE section
// ─────────────────────────────────────────────────────────────────────────────

// ── OPTION A: TFT (ILI9341 / ST7735) — Library: TFT_eSPI by Bodmer ───────────
// #include <TFT_eSPI.h>
// TFT_eSPI tft = TFT_eSPI();
// void initDisplay() { tft.init(); tft.setRotation(1); tft.fillScreen(TFT_BLACK); }
// void displayIdle()     { tft.fillScreen(TFT_BLACK); tft.drawCircle(120,160,60,0x2945); }
// void displayListen()   { tft.fillScreen(TFT_BLACK); tft.fillCircle(120,160,70,0x001F); }
// void displayThink()    { tft.fillScreen(TFT_BLACK); tft.fillCircle(120,160,70,0x780F); }
// void displaySpeak()    { tft.fillScreen(TFT_BLACK); tft.fillCircle(120,160,70,0xFFE0); }

// ── OPTION B: MAX7219 matrix — Library: LedControl ───────────────────────────
// #include <LedControl.h>
// LedControl lc = LedControl(23,18,5,1);
// void initDisplay() { lc.shutdown(0,false); lc.setIntensity(0,8); lc.clearDisplay(0); }
// void displayIdle()   { lc.clearDisplay(0); }
// void displayListen() { for(int r=0;r<8;r++) lc.setRow(0,r,0xFF); }
// void displayThink()  { for(int r=0;r<8;r++) lc.setRow(0,r,0xAA); }
// void displaySpeak()  { for(int r=0;r<8;r++) lc.setRow(0,r,0x55); }

// ── FALLBACK: LED pulse only (always enabled) ─────────────────────────────────
void initDisplay()   { pinMode(LED_PIN, OUTPUT); }
void displayIdle()   {}
void displayListen() {}
void displayThink()  {}
void displaySpeak()  {}

// ─────────────────────────────────────────────────────────────────────────────

enum DisplayState { IDLE, LISTENING, THINKING, SPEAKING };
DisplayState dispState = IDLE;
unsigned long stateStart = 0;
bool httpBusy = false;

// ─────────────────────────────────────────────────────────────────────────────

void setDisplayState(DisplayState s) {
  dispState  = s;
  stateStart = millis();
  switch(s) {
    case IDLE:      Serial.println("State: IDLE");      displayIdle();      break;
    case LISTENING: Serial.println("State: LISTENING"); displayListen();    break;
    case THINKING:  Serial.println("State: THINKING");  displayThink();     break;
    case SPEAKING:  Serial.println("State: SPEAKING");  displaySpeak();     break;
  }
}

// ─────────────────────────────────────────────────────────────────────────────

void notifyLaptop(const char* endpoint) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  char url[128];
  snprintf(url, sizeof(url), "http://%s:%d%s", LAPTOP_IP, LAPTOP_PORT, endpoint);
  http.begin(url);
  http.setTimeout(1000);
  int code = http.GET();
  Serial.printf("  → %s  [%d]\n", url, code);
  http.end();
}

// ─────────────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nDaimon Oracle — ESP32 starting…");

  initDisplay();
  setDisplayState(IDLE);

  // Connect WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Connecting to '%s'", WIFI_SSID);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500); Serial.print("."); tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nConnected. IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Laptop: http://%s:%d\n", LAPTOP_IP, LAPTOP_PORT);
  } else {
    Serial.println("\nWiFi failed — running in display-only mode.");
  }

  Serial.println("Touch the ball to begin.\n");
}

// ─────────────────────────────────────────────────────────────────────────────

bool      touched        = false;
bool      lastRaw        = false;
unsigned long debounceAt = 0;

void loop() {
  // ── Touch debounce ──────────────────────────────────────────────────────────
  bool rawTouch = (touchRead(TOUCH_PIN) < TOUCH_THRESH);

  if (rawTouch != lastRaw) {
    debounceAt = millis();
    lastRaw = rawTouch;
  }

  if ((millis() - debounceAt) > DEBOUNCE_MS && rawTouch != touched) {
    touched = rawTouch;

    if (touched) {
      // ── TOUCH START ──────────────────────────────────────────────────────────
      Serial.println("TOUCH detected");
      setDisplayState(LISTENING);
      notifyLaptop("/touch/start");

    } else {
      // ── TOUCH END ────────────────────────────────────────────────────────────
      Serial.println("TOUCH released");
      setDisplayState(THINKING);
      notifyLaptop("/touch/end");
      // Display will return to IDLE when oracle.py sends /state?s=IDLE
      // but we also set a fallback timer below
    }
  }

  // ── LED animation ───────────────────────────────────────────────────────────
  unsigned long t = millis() - stateStart;
  switch (dispState) {
    case IDLE: {
      // slow breathe 4s
      float phase = (t % 4000) / 4000.0;
      int b = (int)(127.5 * (1.0 + sin(phase * TWO_PI)));
      analogWrite(LED_PIN, b);
      break;
    }
    case LISTENING: {
      // fast pulse 400ms
      analogWrite(LED_PIN, (t % 400 < 200) ? 255 : 30);
      break;
    }
    case THINKING: {
      // slow sweep 2s
      float phase = (t % 2000) / 2000.0;
      analogWrite(LED_PIN, (int)(255.0 * phase));
      break;
    }
    case SPEAKING: {
      // flutter 120ms
      analogWrite(LED_PIN, (t % 120 < 60) ? 255 : 100);
      break;
    }
  }

  // ── Fallback: return to IDLE after 30s if no response from laptop ───────────
  if (dispState == THINKING && t > 30000) {
    setDisplayState(IDLE);
  }

  delay(10);
}
