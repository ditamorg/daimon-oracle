#!/usr/bin/env python3
"""
Quick oracle text test — no audio, no hardware.
Type a question, get the oracle's answer.
Tests: ORACLE_SOUL.md + Claude API + ElevenLabs voice (optional).
"""

import os, sys, tempfile, subprocess
import anthropic
import requests

ANTHROPIC_KEY    = os.environ.get("ANTHROPIC_API_KEY", "")
ELEVENLABS_KEY   = os.environ.get("ELEVENLABS_API_KEY", "")
ELEVENLABS_VOICE = os.environ.get("ELEVENLABS_VOICE_ID", "eo7J8dOdyvEAOgI5xV33")
ELEVENLABS_MODEL = os.environ.get("ELEVENLABS_MODEL", "eleven_multilingual_v2")
CLAUDE_MODEL     = os.environ.get("CLAUDE_MODEL", "claude-sonnet-4-6")

def load_file(name):
    here = os.path.dirname(os.path.abspath(__file__))
    for path in [
        os.path.join(here, name),
        os.path.join(here, "..", name),
        os.path.expanduser(f"~/{name}"),
    ]:
        if os.path.exists(path):
            return open(path).read()
    return ""

ORACLE_SYSTEM = load_file("ORACLE_SOUL.md")
if not ORACLE_SYSTEM:
    print("✗ ORACLE_SOUL.md not found"); sys.exit(1)

if not ANTHROPIC_KEY:
    print("✗ ANTHROPIC_API_KEY not set"); sys.exit(1)

client = anthropic.Anthropic(api_key=ANTHROPIC_KEY)

def ask_oracle(question: str) -> str:
    msg = client.messages.create(
        model=CLAUDE_MODEL,
        max_tokens=300,
        system=ORACLE_SYSTEM,
        messages=[{"role": "user", "content": question}],
    )
    return msg.content[0].text.strip()

def speak(text: str):
    if not ELEVENLABS_KEY:
        print("  (no ElevenLabs key — text only)")
        return
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
            print(f"  ⚠ ElevenLabs {resp.status_code}: {resp.text[:80]}")
            return
        tmp = tempfile.NamedTemporaryFile(suffix=".mp3", delete=False)
        tmp.write(resp.content)
        tmp.close()
        subprocess.run(["afplay", tmp.name], check=False)
        os.unlink(tmp.name)
    except Exception as e:
        print(f"  ⚠ {e}")

print("\n" + "═"*50)
print("  ◈  D A I M O N  ·  O R A C L E  ◈")
print("  Text test — no hardware needed")
print("═"*50)
print(f"  Soul     : ORACLE_SOUL.md ({len(ORACLE_SYSTEM)} chars)")
print(f"  Claude   : {CLAUDE_MODEL}")
print(f"  Voice    : {'ElevenLabs ' + ELEVENLABS_VOICE if ELEVENLABS_KEY else 'text only'}")
print("═"*50)
print("  Type a question. Empty line to quit.\n")

while True:
    try:
        q = input("  QUESTION: ").strip()
    except (EOFError, KeyboardInterrupt):
        print("\n\nOracle sleeps.")
        break
    if not q:
        print("\nOracle sleeps.")
        break
    print("  …thinking…", flush=True)
    answer = ask_oracle(q)
    print(f"\n  ORACLE: {answer}\n")
    speak(answer)
