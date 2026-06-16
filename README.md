# Daimon Oracle
### Bohemian Queen Chalet · Borderland

---

> *"Someone speaks. Daimon answers. From the threshold."*

---

## Architecture

```
[crystal ball — ESP32-S3]                    [MacBook — bridge.py]
  INMP441 mic                                  Whisper STT (local)
  → I2S capture (8 kHz s16le)                 → Claude API (Daimon constitution)
  → UDP :5004 ──────────────────────────────► → ElevenLabs TTS
                                               → afplay (speaker)
  capacitive touch (GPIO 1)
  → CTRL_TOUCH_ON  (UDP :5006) ────────────►  start buffering audio
  → CTRL_TOUCH_OFF (UDP :5006) ────────────►  stop → transcribe → respond

  WS2812B mood LED strip      ◄────────────── UDP :5007 mood commands
  ILI9163C 128×128 TFT        ◄────────────── UDP :5008 display commands
  status LED (NeoPixel)       ◄────────────── UDP :5006 ctrl bytes
```

---

## Repo structure

```
oracle/
  bridge.py            ← Mac counterpart — runs on MacBook at Borderland
  oracle.py            ← standalone mode (no ESP32 — Mac mic + keyboard)
  setup.sh             ← one-time Mac install
  README.md
  ESP32_AudioNode/     ← Tomi's ESP32 firmware (Arduino sketch)
    ESP32_AudioNode.ino
    config.h           ← edit WIFI_FALLBACK_SSID + DEFAULT_SERVER_IP before flash
    audio.cpp/h        ← INMP441 I2S driver + UDP TX
    ctrl.cpp/h         ← control channel
    touch.cpp/h        ← capacitive touch → CTRL_TOUCH_ON/OFF
    led.cpp/h          ← WS2812B status LED state machine
    mood_led.cpp/h     ← mood LED strip
    display.cpp/h      ← ILI9163C TFT driver
  esp32/
    oracle_esp32.ino   ← legacy simple firmware (touch → HTTP, no mic)
```

---

## Setup

### MacBook (one time)

```bash
git clone https://github.com/ditamorg/daimon-oracle.git
cd daimon-oracle
bash setup.sh
```

Set API keys in `~/.zshrc`:

```bash
export ANTHROPIC_API_KEY=sk-ant-...
export ELEVENLABS_API_KEY=sk_...
export ELEVENLABS_VOICE_ID=d5QgxQhvRNirnHGpRQdJ   # or your chosen voice
```

### ESP32 (one time, Tomi)

1. Open `ESP32_AudioNode/ESP32_AudioNode.ino` in Arduino IDE
2. Edit `config.h`:
   - `WIFI_FALLBACK_SSID` / `WIFI_FALLBACK_PASS` — Borderland hotspot or router
   - `DEFAULT_SERVER_IP` — Mac's IP on the same network (printed by `bridge.py` on start)
3. Install libraries via Library Manager:
   - Adafruit NeoPixel ≥ 1.12
   - ArduinoJson ≥ 7.0
4. Flash to ESP32-S3 → watch Serial Monitor at 115200 baud for IP

After first flash: open `http://<esp32-ip>` in browser to configure without reflashing.

---

## Running

### Event mode (ESP32 connected)

On the MacBook:

```bash
python3 bridge.py
```

Banner prints the Mac's IP — set this as `DEFAULT_SERVER_IP` in `config.h` (or via web UI at `http://<esp32-ip>`).

### Testing without ESP32

```bash
MODE=keys python3 oracle.py
```

Uses Mac microphone, ENTER to start/stop.

---

## Voice

**ElevenLabs** (primary) — deep, resonant, uncanny. Requires `ELEVENLABS_API_KEY`.
**edge-tts** (fallback) — free, offline-capable. Used automatically if ElevenLabs fails.

Voice settings in `bridge.py`: `stability 0.4`, `style 0.35` — expressive, not robotic.

---

## Hardware (ESP32_AudioNode)

### INMP441 Microphone → ESP32-S3

| INMP441 | GPIO | Note |
|---------|------|------|
| SCK | 4 | Bit clock |
| WS | 5 | Word select |
| SD | 6 | Data |
| L/R | GND | Left channel |
| VDD | 3.3V | |

### WS2812B Status LED → GPIO 38
### WS2812B Mood Strip → GPIO 14 (25 LEDs default)
### ILI9163C TFT → SPI (SCK:36, MOSI:35, CS:39, DC:40, RST:47)
### Capacitive Touch → GPIO 1
### Button → GPIO 0 (active LOW)

---

## LED states (status LED)

| Pattern | Meaning |
|---------|---------|
| Slow blue pulse | Booting / WiFi connecting |
| Dim green | Idle, server reachable |
| Dim yellow | Idle, server not responding |
| Solid bright green | Recording / in call |
| Solid red | Error |

## Mood LED states

| Mood | Colour | When |
|------|--------|------|
| CALM | Slow blue breathe | Idle |
| THINK | Slow purple fade | Listening / recording |
| LISTEN | Soft green throb | Transcribing / processing |
| SPEAK | Cyan chase | Oracle speaking |

---

## Connectivity at Borderland

The oracle needs internet for:
- **ElevenLabs** TTS (~50KB per response)
- **Claude** API (~5KB per response)

**Whisper** runs fully locally — no internet needed for STT.

Starlink + SIM modem provides more than enough bandwidth.
If ElevenLabs is unreachable, bridge.py falls back to edge-tts automatically.

---

## WiFi recovery (ESP32)

If WiFi fails: ESP32 starts AP `ESP32-SIP-Node-1` / password `configure123`.
Connect your phone → open `http://192.168.4.1` → enter credentials → Save → reboots.

---

*Built for the Bohemian Queen. Oracle at the threshold. Borderland 2026.*
