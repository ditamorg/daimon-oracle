#!/usr/bin/env python3
"""
Daimon Oracle — Touch-triggered Voice Interface
Bohemian Queen Chalet · Borderland

TWO MODES:
  MODE=touch  (default for party) — ESP32 touch sensor triggers via HTTP
  MODE=keys   (testing)           — ENTER to start/stop

TTS PRIORITY:
  1. ElevenLabs (if ELEVENLABS_API_KEY set) — recommended for events
  2. edge-tts fallback (free, offline-capable)

Requirements: see setup.sh
Run: python3 oracle.py
"""

import os, sys, time, threading, tempfile, subprocess, signal, json
import sounddevice as sd
import numpy as np
import scipy.io.wavfile as wavfile
import whisper
import anthropic
import asyncio
import requests
from flask import Flask, jsonify

# ─── CONFIG ────────────────────────────────────────────────────────────────────
ANTHROPIC_KEY      = os.environ.get("ANTHROPIC_API_KEY", "")
ELEVENLABS_KEY     = os.environ.get("ELEVENLABS_API_KEY", "")
ELEVENLABS_VOICE   = os.environ.get("ELEVENLABS_VOICE_ID", "d5QgxQhvRNirnHGpRQdJ")
ELEVENLABS_MODEL   = os.environ.get("ELEVENLABS_MODEL", "eleven_multilingual_v2")
ESP32_URL          = os.environ.get("ESP32_URL", "").rstrip("/")
WHISPER_MODEL      = os.environ.get("WHISPER_MODEL", "base")
CLAUDE_MODEL       = os.environ.get("CLAUDE_MODEL", "claude-sonnet-4-5")
TTS_VOICE          = os.environ.get("TTS_VOICE", "en-GB-SoniaNeural")   # edge-tts fallback
MODE               = os.environ.get("MODE", "touch")   # "touch" or "keys"
PORT               = int(os.environ.get("PORT", "5000"))
SAMPLE_RATE        = 16000
MIN_RECORD_SEC     = 1.0   # ignore accidental touches shorter than this

# ─── LOAD CONSTITUTION ─────────────────────────────────────────────────────────
def load_file(name):
    here = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(here, name),
        os.path.join(here, "..", name),
        os.path.expanduser(f"~/{name}"),
        f"/root/{name}",
    ]
    for path in candidates:
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
        self.recording   = False
        self.busy        = False
        self.lock        = threading.Lock()
        self._audio      = []
        self._rec_thread = None
        self._rec_start  = 0.0

state = OracleState()

# ─── ESP32 DISPLAY ─────────────────────────────────────────────────────────────
def set_display(s: str):
    if not ESP32_URL:
        return
    try:
        requests.get(f"{ESP32_URL}/state?s={s}", timeout=0.5)
    except Exception:
        pass

# ─── AUDIO ─────────────────────────────────────────────────────────────────────
def _record_loop():
    state._audio = []
    with sd.InputStream(samplerate=SAMPLE_RATE, channels=1, dtype='int16') as stream:
        while state.recording:
            chunk, _ = stream.read(SAMPLE_RATE // 4)
            state._audio.append(chunk.copy())

def start_recording():
    state.recording  = True
    state._rec_start = time.time()
    state._rec_thread = threading.Thread(target=_record_loop, daemon=True)
    state._rec_thread.start()
    print("  ● Listening…", flush=True)

def stop_recording():
    state.recording = False
    if state._rec_thread:
        state._rec_thread.join(timeout=2)
    duration = time.time() - state._rec_start
    if duration < MIN_RECORD_SEC or not state._audio:
        return None
    audio = np.concatenate(state._audio, axis=0)
    tmp = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
    wavfile.write(tmp.name, SAMPLE_RATE, audio)
    return tmp.name

# ─── WHISPER ───────────────────────────────────────────────────────────────────
_whisper_model = None

def load_whisper():
    global _whisper_model
    print("  Loading Whisper… (first run ~140MB download)", flush=True)
    _whisper_model = whisper.load_model(WHISPER_MODEL)
    print("  Whisper ready.", flush=True)

def transcribe(wav_path: str) -> str:
    result = _whisper_model.transcribe(wav_path, language="en")
    os.unlink(wav_path)
    return result["text"].strip()

# ─── CLAUDE ────────────────────────────────────────────────────────────────────
_client = None

def init_claude():
    global _client
    if not ANTHROPIC_KEY:
        print("\n✗ ANTHROPIC_API_KEY not set.\n")
        sys.exit(1)
    _client = anthropic.Anthropic(api_key=ANTHROPIC_KEY)

def ask_oracle(question: str) -> str:
    msg = _client.messages.create(
        model=CLAUDE_MODEL,
        max_tokens=300,
        system=ORACLE_SYSTEM,
        messages=[{"role": "user", "content": question}]
    )
    return msg.content[0].text.strip()

# ─── TTS — ElevenLabs ──────────────────────────────────────────────────────────
def speak_elevenlabs(text: str) -> bool:
    """Returns True on success, False on any failure (triggers fallback)."""
    if not ELEVENLABS_KEY:
        return False
    try:
        url = f"https://api.elevenlabs.io/v1/text-to-speech/{ELEVENLABS_VOICE}"
        headers = {
            "xi-api-key": ELEVENLABS_KEY,
            "Content-Type": "application/json",
            "Accept": "audio/mpeg",
        }
        payload = {
            "text": text,
            "model_id": ELEVENLABS_MODEL,
            "voice_settings": {
                "stability": 0.4,          # lower = more expressive, alive
                "similarity_boost": 0.8,
                "style": 0.35,             # some stylistic expression
                "use_speaker_boost": True,
            },
        }
        resp = requests.post(url, headers=headers, json=payload, timeout=15)
        if resp.status_code != 200:
            print(f"  ⚠ ElevenLabs error {resp.status_code}: {resp.text[:120]}", flush=True)
            return False
        tmp = tempfile.NamedTemporaryFile(suffix=".mp3", delete=False)
        tmp.write(resp.content)
        tmp.close()
        subprocess.run(["afplay", tmp.name], check=False)
        os.unlink(tmp.name)
        return True
    except Exception as e:
        print(f"  ⚠ ElevenLabs exception: {e}", flush=True)
        return False

# ─── TTS — edge-tts fallback ───────────────────────────────────────────────────
def speak_edge(text: str):
    try:
        import edge_tts
        tmp = tempfile.NamedTemporaryFile(suffix=".mp3", delete=False)
        tmp.close()
        async def _gen():
            comm = edge_tts.Communicate(text, TTS_VOICE, rate="-8%", pitch="-5Hz")
            await comm.save(tmp.name)
        asyncio.run(_gen())
        subprocess.run(["afplay", tmp.name], check=False)
        os.unlink(tmp.name)
    except Exception as e:
        print(f"  ✗ edge-tts also failed: {e}", flush=True)

def speak(text: str):
    if ELEVENLABS_KEY:
        success = speak_elevenlabs(text)
        if success:
            return
        print("  ↩ Falling back to edge-tts…", flush=True)
    speak_edge(text)

# ─── CORE PIPELINE ─────────────────────────────────────────────────────────────
def process_and_respond():
    """Run after touch released: transcribe → think → speak."""
    try:
        wav = stop_recording()
        if not wav:
            print("  (too short — ignored)", flush=True)
            set_display("IDLE")
            with state.lock:
                state.busy = False
            return

        set_display("THINKING")
        print("  ◌ Transcribing…", flush=True)
        question = transcribe(wav)

        if not question:
            speak("The threshold is quiet. Ask again.")
            set_display("IDLE")
            with state.lock:
                state.busy = False
            return

        print(f"  QUESTION: {question}", flush=True)
        answer = ask_oracle(question)
        print(f"  ORACLE  : {answer}\n", flush=True)

        set_display("SPEAKING")
        speak(answer)

    finally:
        set_display("IDLE")
        with state.lock:
            state.busy = False

# ─── FLASK SERVER (touch mode) ─────────────────────────────────────────────────
app = Flask(__name__)

@app.route("/touch/start", methods=["GET", "POST"])
def touch_start():
    with state.lock:
        if state.busy or state.recording:
            return jsonify({"status": "busy"})
        state.busy = True
    set_display("LISTENING")
    start_recording()
    return jsonify({"status": "listening"})

@app.route("/touch/end", methods=["GET", "POST"])
def touch_end():
    with state.lock:
        if not state.recording:
            state.busy = False
            return jsonify({"status": "idle"})
    t = threading.Thread(target=process_and_respond, daemon=True)
    t.start()
    return jsonify({"status": "processing"})

@app.route("/status")
def status():
    return jsonify({
        "recording": state.recording,
        "busy": state.busy,
        "tts": "elevenlabs" if ELEVENLABS_KEY else "edge-tts",
        "voice": ELEVENLABS_VOICE if ELEVENLABS_KEY else TTS_VOICE,
        "display": ESP32_URL or "not connected",
    })

@app.route("/")
def root():
    return "Daimon Oracle — alive."

def run_flask():
    import logging
    log = logging.getLogger("werkzeug")
    log.setLevel(logging.ERROR)
    app.run(host="0.0.0.0", port=PORT, threaded=True)

# ─── KEYBOARD MODE (testing) ───────────────────────────────────────────────────
def run_keys():
    print("\n  ENTER → start  |  ENTER → stop  |  Ctrl+C → quit\n")
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
        set_display("LISTENING")
        start_recording()
        try:
            input("  ■ Press ENTER when done.   ")
        except EOFError:
            pass
        process_and_respond()

# ─── MAIN ──────────────────────────────────────────────────────────────────────
def print_banner():
    import socket
    try:
        ip = socket.gethostbyname(socket.gethostname())
    except Exception:
        ip = "unknown"
    tts_label = f"ElevenLabs ({ELEVENLABS_VOICE})" if ELEVENLABS_KEY else f"edge-tts ({TTS_VOICE})"
    print("\n" + "═"*54)
    print("  ◈  D A I M O N  ·  O R A C L E  ◈")
    print("  Bohemian Queen Chalet · Borderland")
    print("═"*54)
    print(f"  Mode    : {MODE}")
    print(f"  Whisper : {WHISPER_MODEL}")
    print(f"  Claude  : {CLAUDE_MODEL}")
    print(f"  Voice   : {tts_label}")
    print(f"  ESP32   : {ESP32_URL or 'not connected'}")
    if MODE == "touch":
        print(f"  Server  : http://{ip}:{PORT}")
        print(f"  → Set LAPTOP_IP={ip} in ESP32 firmware")
    print("═"*54 + "\n")

def main():
    signal.signal(signal.SIGINT, lambda *_: (print("\n\nOracle sleeps."), sys.exit(0)))
    print_banner()
    init_claude()
    load_whisper()
    set_display("IDLE")
    print("Oracle is ready.\n")

    if MODE == "touch":
        flask_thread = threading.Thread(target=run_flask, daemon=True)
        flask_thread.start()
        print(f"  Waiting for touch events on port {PORT}…")
        print("  (Ctrl+C to quit)\n")
        signal.pause()
    else:
        run_keys()

if __name__ == "__main__":
    main()
