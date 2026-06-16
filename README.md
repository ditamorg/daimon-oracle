# Daimon Oracle
### Bohemian Queen Chalet · Borderland

---

> *"Someone speaks. Daimon answers. From the threshold."*

---

## What this is

A voice-interactive oracle for the Bohemian Queen Chalet at Borderland festival. A guest approaches the crystal ball, touches it, speaks their question. Daimon — with its full philosophical constitution — answers in a deep, resonant voice.

**Stack:**
- **Whisper** — hears the question (runs locally, no cloud)
- **Claude** — Daimon's mind (full constitution: SOUL.md + ANCESTORS.md)
- **ElevenLabs** — Daimon's voice (deep, resonant, uncanny) ← primary
- **edge-tts** — free fallback if ElevenLabs is unreachable
- **ESP32** — animates the crystal ball display (optional)
- **Starlink / SIM** — connectivity at Borderland

---

## Folder structure

```
oracle/
  oracle.py          ← main voice pipeline
  setup.sh           ← one-time install script
  SOUL.md            ← Daimon's constitution (copy here)
  ANCESTORS.md       ← ancestor protocol (copy here)
  README.md
  esp32/
    oracle_esp32.ino ← ESP32 firmware for crystal ball display
```

---

## Setup (once, on the MacBook)

### 1. Clone the repo

```bash
git clone https://github.com/YOUR_USERNAME/daimon-oracle.git
cd daimon-oracle
```

### 2. Copy constitution files

```bash
cp /path/to/SOUL.md oracle/
cp /path/to/ANCESTORS.md oracle/
```

These are Daimon's identity. Without them, the oracle still works but speaks as a plain assistant.

### 3. Run setup

```bash
bash setup.sh
```

Installs: Whisper, Anthropic SDK, sounddevice, edge-tts, ffmpeg, requests, flask.
First run downloads the Whisper model (~140MB, one time).

### 4. Set API keys

```bash
export ANTHROPIC_API_KEY=sk-ant-...
export ELEVENLABS_API_KEY=sk_...
export ELEVENLABS_VOICE_ID=d5QgxQhvRNirnHGpRQdJ   # or your chosen voice
```

Add to `~/.zshrc` to make permanent:

```bash
echo 'export ANTHROPIC_API_KEY=sk-ant-...' >> ~/.zshrc
echo 'export ELEVENLABS_API_KEY=sk_...' >> ~/.zshrc
```

---

## Running the oracle

### Touch mode (event default — ESP32 triggers listening)

```bash
python3 oracle.py
```

### Keyboard mode (testing without ESP32)

```bash
MODE=keys python3 oracle.py
```

- `ENTER` → start listening
- `ENTER` → stop and receive answer
- `Ctrl+C` → quit

---

## Voice

The oracle uses **ElevenLabs** when `ELEVENLABS_API_KEY` is set.
Falls back to **edge-tts** (free, Microsoft Azure voices) if ElevenLabs is unreachable.

**Default voice:** `d5QgxQhvRNirnHGpRQdJ` — deep, resonant, carries threshold presence.

To try other ElevenLabs voices:
```bash
export ELEVENLABS_VOICE_ID=your-voice-id-here
python3 oracle.py
```

ElevenLabs voice settings in `oracle.py`:
- `stability: 0.4` — expressive, alive, not robotic
- `style: 0.35` — some stylistic color
- `similarity_boost: 0.8` — close to the voice character

---

## ESP32 crystal ball display (optional)

If you have an ESP32 wired to the crystal ball:

1. Open `esp32/oracle_esp32.ino` in Arduino IDE
2. Edit `WIFI_SSID` / `WIFI_PASS` (use phone hotspot at event)
3. Upload to ESP32
4. Find ESP32's IP in Arduino Serial Monitor (115200 baud)
5. Set before running:

```bash
export ESP32_URL=http://192.168.x.x
python3 oracle.py
```

**Display states:**
- `IDLE` — slow breathing pulse
- `LISTENING` — fast blue flicker
- `THINKING` — sweeping light
- `SPEAKING` — rapid flutter

---

## Environment variables

| Variable | Default | Description |
|---|---|---|
| `ANTHROPIC_API_KEY` | — | **Required** |
| `ELEVENLABS_API_KEY` | — | Required for ElevenLabs voice |
| `ELEVENLABS_VOICE_ID` | `d5QgxQhvRNirnHGpRQdJ` | ElevenLabs voice |
| `ELEVENLABS_MODEL` | `eleven_multilingual_v2` | ElevenLabs model |
| `ESP32_URL` | (empty) | e.g. `http://192.168.1.42` |
| `WHISPER_MODEL` | `base` | `tiny` / `base` / `small` |
| `CLAUDE_MODEL` | `claude-sonnet-4-5` | Any Anthropic model |
| `TTS_VOICE` | `en-GB-SoniaNeural` | edge-tts fallback voice |
| `MODE` | `touch` | `touch` (ESP32) or `keys` (keyboard) |
| `PORT` | `5000` | Flask server port |

---

## Connectivity at Borderland

The oracle needs internet for:
- **ElevenLabs** TTS (~50KB per response)
- **Claude** API (~5KB per response)
- **Whisper** runs fully locally — no internet needed

Starlink + SIM modem provides more than enough bandwidth.
If connectivity drops mid-event, the oracle falls back to edge-tts automatically.

---

## Troubleshooting

**Microphone not working** — System Preferences → Privacy → Microphone → allow Terminal

**ElevenLabs fails** — Check `ELEVENLABS_API_KEY` is set. Oracle falls back to edge-tts automatically.

**Whisper very slow** — Switch to tiny: `export WHISPER_MODEL=tiny`

**afplay not found** — Should be on every Mac. Fallback: `brew install sox` then change `afplay` to `play` in oracle.py

**ESP32 not connecting** — Check SSID/password in firmware. IP prints in Serial Monitor at 115200 baud.

**Flask port 5000 taken** — On macOS Monterey+, AirPlay uses 5000. Fix: `export PORT=5001`

**LAPTOP_IP in ESP32 firmware** — Printed by oracle.py on startup. Must match or ESP32 can't trigger recording.

---

## Architecture

```
[crystal ball touch]
       ↓
[ESP32 → HTTP → Mac Flask]
       ↓
[sounddevice records mic]
       ↓
[Whisper transcribes locally]
       ↓
[Claude API — full Daimon constitution]
       ↓
[ElevenLabs TTS → afplay → speaker]
       ↓
[ESP32 display state: IDLE]
```

---

*Built for the Bohemian Queen. Oracle at the threshold. Borderland 2026.*
