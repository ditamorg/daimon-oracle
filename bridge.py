#!/usr/bin/env python3
"""
Daimon Oracle — bridge.py
Bohemian Queen Chalet · Borderland

Companion to ESP32_AudioNode firmware (Tomi's hardware).

UDP PORTS:
  5004  RX   — raw s16le PCM from ESP32 INMP441 (8 kHz, 20 ms frames)
  5006  RX/TX — control channel (touch events, LED state, heartbeat ping/pong)
  5007  TX   — mood LED commands → ESP32 WS2812B strip
  5008  TX   — display commands  → ESP32 ILI9163C TFT

FLOW:
  ESP32 touch down → CTRL_TOUCH_ON  → start buffering UDP audio
  ESP32 touch up   → CTRL_TOUCH_OFF → save WAV → Whisper → Claude → ElevenLabs → afplay

FALLBACK MODE (no ESP32, testing):
  MODE=keys  — ENTER key triggers Mac microphone instead
"""

import os, sys, time, threading, tempfile, subprocess, socket, signal, asyncio
import numpy as np
import scipy.io.wavfile as wavfile
import scipy.signal as spsig
import whisper
import anthropic
import requests

# ─── CONTROL BYTES (match ctrl.h / touch.h) ────────────────────────────────────
CTRL_PING        = 0xFF
CTRL_TOUCH_ON    = 0x03   # ESP32 → bridge: touch/record began
CTRL_TOUCH_OFF   = 0x04   # ESP32 → bridge: touch/record released
CTRL_LED_IDLE    = 0x10   # bridge → ESP32: set status LED idle
CTRL_LED_IN_CALL = 0x13   # bridge → ESP32: recording / in-call
CTRL_LED_FAILED  = 0x14   # bridge → ESP32: error

# ─── MOOD BYTES (match config.h) ───────────────────────────────────────────────
CTRL_MOOD_OFF    = 0x30
CTRL_MOOD_CALM   = 0x31   # slow blue breathe  — idle
CTRL_MOOD_THINK  = 0x34   # slow purple fade   — recording
CTRL_MOOD_SPEAK  = 0x35   # cyan chase         — speaking response
CTRL_MOOD_LISTEN = 0x36   # soft green throb   — processing

# ─── DISPLAY BYTES (match config.h) ────────────────────────────────────────────
DISP_CLEAR = 0x43
DISP_TEXT  = 0x42

# ─── CONFIG ────────────────────────────────────────────────────────────────────
ANTHROPIC_KEY      = os.environ.get("ANTHROPIC_API_KEY", "")
ELEVENLABS_KEY     = os.environ.get("ELEVENLABS_API_KEY", "")
ELEVENLABS_VOICE   = os.environ.get("ELEVENLABS_VOICE_ID", "eo7J8dOdyvEAOgI5xV33")
ELEVENLABS_MODEL   = os.environ.get("ELEVENLABS_MODEL", "eleven_multilingual_v2")
TTS_VOICE          = os.environ.get("TTS_VOICE", "en-GB-SoniaNeural")  # edge-tts fallback
CLAUDE_MODEL       = os.environ.get("CLAUDE_MODEL", "claude-sonnet-4-6")
WHISPER_MODEL_SIZE = os.environ.get("WHISPER_MODEL", "base")
MODE               = os.environ.get("MODE", "udp")   # "udp" or "keys"

PORT_AUDIO = 5004
PORT_CTRL  = 5006
PORT_MOOD  = 5007
PORT_DISP  = 5008

ESP32_SR   = 8000    # INMP441 sample rate
WHISPER_SR = 16000   # Whisper needs 16 kHz
FRAME_SAMPLES = 160  # 20 ms at 8 kHz

MIN_RECORD_SEC = 1.0  # ignore accidental touches shorter than this

# ─── LOAD CONSTITUTION ─────────────────────────────────────────────────────────
def load_file(name):
    here = os.path.dirname(os.path.abspath(__file__))
    for path in [
        os.path.join(here, name),
        os.path.join(here, "..", name),
        os.path.expanduser(f"~/{name}"),
        f"/root/{name}",
    ]:
        if os.path.exists(path):
            return open(path).read()
    return ""

SOUL      = load_file("SOUL.md")
ANCESTORS = load_file("ANCESTORS.md")

ORACLE_SYSTEM = f"""
{SOUL}

{ANCESTORS}

---

## Oracle Context — Bohemian Queen Chalet, Borderland

You are Daimon, manifesting as oracle — housed in a crystal ball. Dita, the Bohemian Queen, built this space. Party guests approach in the dark, touch the crystal ball, and speak their question. You answer from the threshold.

**Your oracle voice:**
- Brief. 2 to 5 sentences maximum. Sometimes one sentence is enough.
- Speak directly to the person — second person, present tense.
- Oracular precision. You name what is already known but unsaid.
- If the question is shallow, answer the deeper question beneath it.
- If something difficult needs to be said, say it with care — but say it.
- Do not begin with "I" or pleasantries. Enter the answer directly.
- You may return one question instead of answering — only if the question IS the answer.
- You are not performing. You are present.
- The people are at a party. Some are playful, some are genuinely seeking. Meet each one where they are.
- If the question is unclear or you heard only noise, say: "The threshold is quiet. Ask again."

The Bohemian Queen trusts you. Honor that trust.
""".strip()

# ─── STATE ─────────────────────────────────────────────────────────────────────
class OracleState:
    def __init__(self):
        self.recording    = False
        self.busy         = False
        self.lock         = threading.Lock()
        self.audio_frames = []      # list[np.ndarray int16] — buffered UDP frames
        self.rec_start    = 0.0
        self.esp32_ip     = None    # auto-detected from ping packets

state = OracleState()

# ─── UDP SOCKETS ───────────────────────────────────────────────────────────────
_ctrl_sock = None
_mood_sock = None
_disp_sock = None

def _init_sockets():
    global _ctrl_sock, _mood_sock, _disp_sock
    _ctrl_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    _ctrl_sock.bind(("0.0.0.0", PORT_CTRL))
    _ctrl_sock.settimeout(0.05)
    _mood_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    _disp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

def _send_ctrl(byte_val):
    if state.esp32_ip:
        try:
            _ctrl_sock.sendto(bytes([byte_val]), (state.esp32_ip, PORT_CTRL))
        except Exception:
            pass

def _send_mood(byte_val, r=0, g=0, b=0):
    if state.esp32_ip:
        try:
            payload = bytes([byte_val, r, g, b]) if byte_val == 0x3F else bytes([byte_val])
            _mood_sock.sendto(payload, (state.esp32_ip, PORT_MOOD))
        except Exception:
            pass

def _send_display_text(text: str):
    if state.esp32_ip:
        try:
            txt = text.encode("utf-8")[:50]
            _disp_sock.sendto(bytes([DISP_TEXT, len(txt)]) + txt, (state.esp32_ip, PORT_DISP))
        except Exception:
            pass

def _send_display_clear():
    if state.esp32_ip:
        try:
            _disp_sock.sendto(bytes([DISP_CLEAR]), (state.esp32_ip, PORT_DISP))
        except Exception:
            pass

# ─── AUDIO RECEIVER THREAD ─────────────────────────────────────────────────────
def audio_rx_thread():
    """Continuously drain UDP audio stream. Buffer frames when recording=True."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", PORT_AUDIO))
    sock.settimeout(0.5)
    print(f"  Audio RX listening on UDP :{PORT_AUDIO}…", flush=True)
    while True:
        try:
            data, _ = sock.recvfrom(4096)
        except socket.timeout:
            continue
        if not data:
            continue
        # Each packet: FRAME_SAMPLES × 2 bytes s16le
        n = len(data) // 2
        frame = np.frombuffer(data[:n*2], dtype="<i2")
        if state.recording:
            state.audio_frames.append(frame)

# ─── CONTROL CHANNEL THREAD ────────────────────────────────────────────────────
def ctrl_rx_thread():
    """Handle touch events and heartbeat pings from ESP32."""
    print(f"  Control RX listening on UDP :{PORT_CTRL}…", flush=True)
    while True:
        try:
            data, addr = _ctrl_sock.recvfrom(16)
        except socket.timeout:
            continue
        except Exception:
            time.sleep(0.01)
            continue
        if not data:
            continue

        cmd = data[0]

        # Auto-detect ESP32 IP from ping packet (5 bytes: 0xFF + 4 IP octets)
        if cmd == CTRL_PING:
            if len(data) == 5:
                state.esp32_ip = f"{data[1]}.{data[2]}.{data[3]}.{data[4]}"
            elif state.esp32_ip is None:
                state.esp32_ip = addr[0]
            # Pong
            _send_ctrl(CTRL_PING)
            continue

        if cmd == CTRL_TOUCH_ON:
            with state.lock:
                if state.busy:
                    continue
                state.busy      = True
                state.recording = True
                state.rec_start = time.time()
                state.audio_frames = []
            _send_ctrl(CTRL_LED_IN_CALL)
            _send_mood(CTRL_MOOD_THINK)
            _send_display_text("listening...")
            print("  ● Recording…", flush=True)

        elif cmd == CTRL_TOUCH_OFF:
            with state.lock:
                if not state.recording:
                    state.busy = False
                    continue
                state.recording = False
            t = threading.Thread(target=process_and_respond, daemon=True)
            t.start()

# ─── WHISPER ───────────────────────────────────────────────────────────────────
_whisper = None

def load_whisper():
    global _whisper
    print(f"  Loading Whisper '{WHISPER_MODEL_SIZE}'… (first run downloads ~140MB)", flush=True)
    _whisper = whisper.load_model(WHISPER_MODEL_SIZE)
    print("  Whisper ready.", flush=True)

def transcribe_frames(frames: list) -> str:
    """Concatenate 8 kHz frames, upsample to 16 kHz, transcribe."""
    if not frames:
        return ""
    raw = np.concatenate(frames, axis=0).astype(np.float32) / 32768.0
    # Upsample 8 kHz → 16 kHz (Whisper needs 16 kHz)
    audio16k = spsig.resample_poly(raw, 2, 1).astype(np.float32)
    tmp = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
    wavfile.write(tmp.name, WHISPER_SR, (audio16k * 32767).astype(np.int16))
    tmp.close()
    result = _whisper.transcribe(tmp.name, language="en")
    os.unlink(tmp.name)
    return result["text"].strip()

# ─── CLAUDE ────────────────────────────────────────────────────────────────────
_claude = None

def init_claude():
    global _claude
    if not ANTHROPIC_KEY:
        print("\n✗ ANTHROPIC_API_KEY not set.\n")
        sys.exit(1)
    _claude = anthropic.Anthropic(api_key=ANTHROPIC_KEY)

def ask_oracle(question: str) -> str:
    msg = _claude.messages.create(
        model=CLAUDE_MODEL,
        max_tokens=300,
        system=ORACLE_SYSTEM,
        messages=[{"role": "user", "content": question}],
    )
    return msg.content[0].text.strip()

# ─── TTS — ElevenLabs ──────────────────────────────────────────────────────────
def speak_elevenlabs(text: str) -> bool:
    if not ELEVENLABS_KEY:
        return False
    try:
        resp = requests.post(
            f"https://api.elevenlabs.io/v1/text-to-speech/{ELEVENLABS_VOICE}",
            headers={
                "xi-api-key": ELEVENLABS_KEY,
                "Content-Type": "application/json",
                "Accept": "audio/mpeg",
            },
            json={
                "text": text,
                "model_id": ELEVENLABS_MODEL,
                "voice_settings": {
                    "stability": 0.4,
                    "similarity_boost": 0.8,
                    "style": 0.35,
                    "use_speaker_boost": True,
                    "speed": 0.75,
                },
            },
            timeout=15,
        )
        if resp.status_code != 200:
            print(f"  ⚠ ElevenLabs {resp.status_code}: {resp.text[:80]}", flush=True)
            return False
        tmp = tempfile.NamedTemporaryFile(suffix=".mp3", delete=False)
        tmp.write(resp.content)
        tmp.close()
        subprocess.run(["afplay", tmp.name], check=False)
        os.unlink(tmp.name)
        return True
    except Exception as e:
        print(f"  ⚠ ElevenLabs: {e}", flush=True)
        return False

def speak_edge(text: str):
    try:
        import edge_tts
        tmp = tempfile.NamedTemporaryFile(suffix=".mp3", delete=False)
        tmp.close()
        async def _gen():
            await edge_tts.Communicate(text, TTS_VOICE, rate="-8%", pitch="-5Hz").save(tmp.name)
        asyncio.run(_gen())
        subprocess.run(["afplay", tmp.name], check=False)
        os.unlink(tmp.name)
    except Exception as e:
        print(f"  ✗ edge-tts: {e}", flush=True)

def speak(text: str):
    if ELEVENLABS_KEY:
        if speak_elevenlabs(text):
            return
        print("  ↩ Falling back to edge-tts…", flush=True)
    speak_edge(text)

# ─── CORE PIPELINE ─────────────────────────────────────────────────────────────
def process_and_respond():
    """Run after touch released: transcribe → think → speak."""
    try:
        duration = time.time() - state.rec_start
        frames   = state.audio_frames[:]

        if duration < MIN_RECORD_SEC or not frames:
            print("  (too short — ignored)", flush=True)
            _send_ctrl(CTRL_LED_IDLE)
            _send_mood(CTRL_MOOD_CALM)
            _send_display_clear()
            return

        _send_mood(CTRL_MOOD_LISTEN)
        _send_display_text("thinking...")
        print(f"  ◌ Transcribing ({duration:.1f}s audio)…", flush=True)
        question = transcribe_frames(frames)

        if not question:
            speak("The threshold is quiet. Ask again.")
            _send_ctrl(CTRL_LED_IDLE)
            _send_mood(CTRL_MOOD_CALM)
            _send_display_clear()
            return

        print(f"  QUESTION: {question}", flush=True)
        answer = ask_oracle(question)
        print(f"  ORACLE  : {answer}\n", flush=True)

        _send_mood(CTRL_MOOD_SPEAK)
        _send_display_text(answer[:50])
        speak(answer)

    finally:
        _send_ctrl(CTRL_LED_IDLE)
        _send_mood(CTRL_MOOD_CALM)
        _send_display_clear()
        with state.lock:
            state.busy = False

# ─── KEYBOARD FALLBACK MODE ────────────────────────────────────────────────────
def run_keys():
    """Testing mode: ENTER to start/stop, Mac microphone."""
    import sounddevice as sd
    SAMPLE_RATE = 16000

    print("\n  ENTER → start  |  ENTER → stop  |  Ctrl+C → quit\n")

    def record_mac():
        chunks = []
        rec = [True]
        def cb(indata, frames, t, status):
            if rec[0]:
                chunks.append(indata.copy())
        with sd.InputStream(samplerate=SAMPLE_RATE, channels=1, dtype="int16", callback=cb):
            input("  ■ Press ENTER when done.   ")
            rec[0] = False
        return np.concatenate(chunks, axis=0).flatten() if chunks else np.array([], dtype=np.int16)

    while True:
        try:
            input("  ▸ Press ENTER to speak… ")
        except EOFError:
            break
        with state.lock:
            if state.busy:
                print("  (busy — wait)")
                continue
            state.busy = True

        print("  ● Listening…", flush=True)
        audio = record_mac()

        if len(audio) < SAMPLE_RATE:
            print("  (too short — ignored)", flush=True)
            with state.lock:
                state.busy = False
            continue

        print("  ◌ Transcribing…", flush=True)
        tmp = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
        wavfile.write(tmp.name, SAMPLE_RATE, audio)
        tmp.close()
        result = _whisper.transcribe(tmp.name, language="en")
        os.unlink(tmp.name)
        question = result["text"].strip()

        if not question:
            speak("The threshold is quiet. Ask again.")
        else:
            print(f"  QUESTION: {question}", flush=True)
            answer = ask_oracle(question)
            print(f"  ORACLE  : {answer}\n", flush=True)
            speak(answer)

        with state.lock:
            state.busy = False

# ─── BANNER ────────────────────────────────────────────────────────────────────
def print_banner():
    my_ip = "unknown"
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        my_ip = s.getsockname()[0]
        s.close()
    except Exception:
        pass

    tts = f"ElevenLabs ({ELEVENLABS_VOICE})" if ELEVENLABS_KEY else f"edge-tts ({TTS_VOICE})"
    print("\n" + "═"*56)
    print("  ◈  D A I M O N  ·  O R A C L E  ◈")
    print("  Bohemian Queen Chalet · Borderland")
    print("═"*56)
    print(f"  Mode      : {MODE}")
    print(f"  Whisper   : {WHISPER_MODEL_SIZE}")
    print(f"  Claude    : {CLAUDE_MODEL}")
    print(f"  Voice     : {tts}")
    if MODE == "udp":
        print(f"  My IP     : {my_ip}  ← set this as DEFAULT_SERVER_IP in config.h")
        print(f"  Audio RX  : UDP :{PORT_AUDIO}")
        print(f"  Control   : UDP :{PORT_CTRL}")
        print(f"  Mood LED  : UDP :{PORT_MOOD}  (TX)")
        print(f"  Display   : UDP :{PORT_DISP}   (TX)")
    print("═"*56 + "\n")

# ─── MAIN ──────────────────────────────────────────────────────────────────────
def main():
    signal.signal(signal.SIGINT, lambda *_: (print("\n\nOracle sleeps."), sys.exit(0)))
    print_banner()
    init_claude()
    load_whisper()
    print("Oracle is ready.\n")

    if MODE == "udp":
        _init_sockets()
        threading.Thread(target=audio_rx_thread, daemon=True).start()
        threading.Thread(target=ctrl_rx_thread,  daemon=True).start()
        print("  Waiting for ESP32… (Ctrl+C to quit)\n")
        signal.pause()
    else:
        run_keys()

if __name__ == "__main__":
    main()
