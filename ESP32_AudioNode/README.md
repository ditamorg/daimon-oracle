# ESP32 Audio Node — Arduino Sketch

Streams INMP441 microphone audio to bridge.py as raw s16le PCM over UDP.
WS2812B LED shows node state. Button sends control commands to bridge.
Web UI at `http://<node-ip>` for runtime configuration.

---

## Hardware

### INMP441 Microphone → ESP32-S3

| INMP441 | ESP32-S3 GPIO | Note |
|---------|---------------|------|
| SCK     | GPIO 4        | Bit clock |
| WS      | GPIO 5        | Word select |
| SD      | GPIO 6        | Data |
| L/R     | GND           | Left channel select |
| VDD     | 3.3V          |
| GND     | GND           |

### WS2812B LED

| WS2812B | ESP32-S3 |
|---------|----------|
| DIN     | GPIO 38  |
| VDD     | 3.3V or 5V |
| GND     | GND      |

### Button

| Button  | ESP32-S3 |
|---------|----------|
| One leg | GPIO 0   |
| Other   | GND      |

Internal pull-up enabled — active LOW.

---

## Arduino Setup

### Board
- **Package:** `esp32` by Espressif (≥ 3.0.0)
- **Board:** `ESP32S3 Dev Module` (or your specific variant)
- **Flash size:** 4MB minimum
- **Partition scheme:** Default (has LittleFS space)
- **Upload speed:** 921600

### Libraries (install via Library Manager)

| Library | Version |
|---------|---------|
| Adafruit NeoPixel | ≥ 1.12 |
| ArduinoJson | ≥ 7.0 |

LittleFS is bundled with the ESP32 Arduino core — no install needed.

---

## First Flash

1. Edit `config.h` — set `WIFI_FALLBACK_SSID`, `WIFI_FALLBACK_PASS`, `DEFAULT_SERVER_IP`
2. Open `ESP32_AudioNode.ino` in Arduino IDE
3. Select correct board and port
4. Flash

On first boot the node connects to your WiFi and prints its IP on Serial (115200 baud).
Open `http://<node-ip>` to adjust settings without reflashing.

---

## WiFi Recovery

If WiFi fails (wrong credentials or no config.json):

1. Node starts AP: `ESP32-SIP-Node-1` / password `configure123`
2. Connect your phone/laptop to that AP
3. Open `http://192.168.4.1`
4. Enter credentials → Save → node reboots

---

## LED States

| Pattern | Meaning |
|---------|---------|
| Slow blue pulse | Booting / WiFi connecting |
| Dim green steady | Idle, server reachable |
| Dim yellow steady | Idle, server not responding |
| Fast white blink | Outgoing call ringing |
| Fast cyan blink | Incoming call ringing |
| Solid bright green | Call / recording active |
| Solid blue | Loopback test active |
| 3× red flash → green | Call failed |
| Solid red | Error |

---

## Data Flow

```
INMP441
  → I2S_NUM_0 (32-bit MSB, 8kHz, left slot)
  → audioTxTask (Core 1, priority 5)
      raw[i] >> micGain  →  int16_t pcm[160]
      UDP sendto(serverIp, 5004)  →  bridge.py
```

```
bridge.py
  → UDP :5006  →  ctrl_poll() every 20ms
      0x10–0x15  →  led_set()
      0xFF       →  pong (server alive)
      0x20/0x21  →  loopbackActive flag
```

---

## File Structure

```
ESP32_AudioNode/
├── ESP32_AudioNode.ino   Main sketch: setup, WiFi, tasks, web UI
├── config.h              All compile-time defaults and pin assignments
├── audio.h / audio.cpp   INMP441 I2S driver + UDP TX task
├── ctrl.h  / ctrl.cpp    Control channel (UDP :5006)
├── led.h   / led.cpp     WS2812B state machine
└── README.md
```
