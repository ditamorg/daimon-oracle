#!/bin/bash
# Daimon Oracle — one-time setup
# Run this once on the MacBook before the event

set -e

echo ""
echo "═══════════════════════════════════════════"
echo "  Daimon Oracle — Setup"
echo "═══════════════════════════════════════════"
echo ""

# ─── Homebrew dependencies ───────────────────────────────────────────────────
echo "→ Installing system dependencies via Homebrew…"
if ! command -v brew &>/dev/null; then
    echo "  ✗ Homebrew not found. Install from https://brew.sh and re-run."
    exit 1
fi

brew install ffmpeg portaudio 2>/dev/null || true

# ─── Python dependencies ─────────────────────────────────────────────────────
echo "→ Installing Python dependencies…"
pip3 install \
    openai-whisper \
    anthropic \
    sounddevice \
    scipy \
    numpy \
    requests \
    flask \
    edge-tts \
    --break-system-packages 2>/dev/null || \
pip3 install \
    openai-whisper \
    anthropic \
    sounddevice \
    scipy \
    numpy \
    requests \
    flask \
    edge-tts

# ─── Note on ElevenLabs ──────────────────────────────────────────────────────
echo ""
echo "  Note: ElevenLabs is used via the requests library (already installed)."
echo "  No separate package needed — just set ELEVENLABS_API_KEY."
echo ""

# ─── Whisper model pre-download ──────────────────────────────────────────────
echo "→ Pre-downloading Whisper 'base' model (~140MB)…"
python3 -c "import whisper; whisper.load_model('base')"
echo "  ✓ Whisper ready."

# ─── API key reminder ────────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════"
echo "  Setup complete."
echo ""
echo "  Set your API keys:"
echo ""
echo "    export ANTHROPIC_API_KEY=sk-ant-..."
echo "    export ELEVENLABS_API_KEY=sk_..."         # required for ElevenLabs voice
echo "    export ELEVENLABS_VOICE_ID=d5QgxQhvRNirnHGpRQdJ   # default voice"
echo ""
echo "  Add these to ~/.zshrc to make permanent."
echo ""
echo "  Run the oracle:"
echo "    python3 oracle.py                   # touch mode (default)"
echo "    MODE=keys python3 oracle.py         # keyboard mode (testing)"
echo "═══════════════════════════════════════════"
echo ""
