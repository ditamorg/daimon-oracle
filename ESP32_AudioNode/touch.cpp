#include "touch.h"
#include "config.h"
#include "mood_led.h"
#include "ctrl.h"

#include <Arduino.h>
#include <driver/touch_pad.h>

// =============================================================================
//  touch.cpp  —  Reliable capacitive touch for hold-to-record
//
//  Behaviour:
//    Touch down  → CTRL_TOUCH_ON  → bridge starts recording, mood → TENSE
//    Touch up    → CTRL_TOUCH_OFF → bridge stops  recording, mood restored
//
//  Anti-bounce:
//    DEBOUNCE_MS  — consecutive samples must all agree before state change
//    HOLDOFF_MS   — minimum gap after release before next trigger accepted
//
//  Auto-recalibration:
//    Baseline drifts with temperature. We recalibrate every
//    TOUCH_RECAL_INTERVAL_MS while the pad is idle (not being touched).
//
//  Tunable at runtime via web config page (no reflash needed):
//    touchThresholdPct, touchDebouncMs, touchHoldoffMs
// =============================================================================

// Runtime-tunable params (set from NodeConfig)
static uint8_t  thresholdPct = DEFAULT_TOUCH_THRESHOLD;
static uint16_t debounceMs   = DEFAULT_TOUCH_DEBOUNCE;
static uint16_t holdoffMs    = DEFAULT_TOUCH_HOLDOFF;

// Internal state
static uint32_t baseline      = 0;
static uint32_t threshold_lo  = 0;
static uint32_t threshold_hi  = 0;
static bool     touching      = false;
static bool     initOk        = false;
static uint8_t  preTouchMood  = CTRL_MOOD_OFF;

// Debounce: require N consecutive ticks in same state before committing
#define DEBOUNCE_TICKS  4   // 4 × 20ms = 80ms confirmation window
static uint8_t  sameStateCount = 0;
static bool     candidateState = false;

// Timing
static uint32_t touchStartMs   = 0;
static uint32_t releaseMs      = 0;
static uint32_t lastRecalMs    = 0;
static uint32_t lastDebugMs    = 0;
#define DEBUG_INTERVAL_MS  1000

// ── Helpers ───────────────────────────────────────────────────────────────────

static uint32_t readPad() {
    uint32_t val = 0;
    touch_pad_read_raw_data(TOUCH_PAD_NUM1, &val);
    return val;
}

static void computeThresholds() {
    uint32_t margin  = baseline * thresholdPct / 100;
    threshold_lo = (baseline > margin) ? baseline - margin : 0;
    threshold_hi = baseline + margin;
}

static void calibrate(const char* reason) {
    uint64_t sum = 0;
    uint32_t mn = UINT32_MAX, mx = 0;
    for (int i = 0; i < TOUCH_SAMPLES; i++) {
        uint32_t v = readPad();
        sum += v; mn = min(mn, v); mx = max(mx, v);
        delay(30);
    }
    baseline = (uint32_t)(sum / TOUCH_SAMPLES);
    computeThresholds();
    Serial.printf("[TOUCH] %s: baseline=%lu  thr=[%lu,%lu]  noise=%lu\n",
                  reason, baseline, threshold_lo, threshold_hi, mx - mn);
}

// ── Public: set tuning params from NodeConfig (called after config load) ──────

void touch_set_params(uint8_t pct, uint16_t dbnMs, uint16_t holdMs) {
    thresholdPct = pct;
    debounceMs   = dbnMs;
    holdoffMs    = holdMs;
    if (initOk) {
        computeThresholds();
        Serial.printf("[TOUCH] params updated: pct=%d debounce=%dms holdoff=%dms\n",
                      pct, dbnMs, holdMs);
    }
}

// ── Init ──────────────────────────────────────────────────────────────────────

void touch_init() {
    Serial.println("[TOUCH] init start");

    touch_pad_init();
    touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V);
    touch_pad_config(TOUCH_PAD_NUM1);
    touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
    touch_pad_fsm_start();

    Serial.printf("[TOUCH] GPIO=%d  threshold=%d%%  debounce=%dms  holdoff=%dms\n",
                  PIN_TOUCH, thresholdPct, debounceMs, holdoffMs);
    Serial.println("[TOUCH] settling 1000ms — keep finger away...");
    delay(1000);

    calibrate("boot");
    initOk = (baseline > 0);

    if (!initOk) {
        Serial.println("[TOUCH] ERROR: baseline=0 — pad not responding");
    } else {
        Serial.println("[TOUCH] ready");
    }

    lastRecalMs = millis();
}

// ── Tick — call every 20ms from ui_task ──────────────────────────────────────

void touch_tick() {
    if (!initOk) return;

    uint32_t val = readPad();
    uint32_t now = millis();

    // ── Debug print ───────────────────────────────────────────────────────────
    if (now - lastDebugMs >= DEBUG_INTERVAL_MS) {
        lastDebugMs = now;
        int32_t delta = (int32_t)val - (int32_t)baseline;
        Serial.printf("[TOUCH] raw=%-6lu  base=%-6lu  delta=%+6d  thr=[%lu,%lu]  %s\n",
                      val, baseline, delta, threshold_lo, threshold_hi,
                      touching ? "RECORDING" : "idle");
    }

    // ── Auto-recalibrate baseline when idle ───────────────────────────────────
    if (!touching && (now - lastRecalMs) >= TOUCH_RECAL_INTERVAL_MS) {
        lastRecalMs = now;
        calibrate("auto-recal");
    }

    // ── Raw touch detection ───────────────────────────────────────────────────
    bool rawTouched = (val > 0) && ((val < threshold_lo) || (val > threshold_hi));

    // ── Multi-sample debounce ─────────────────────────────────────────────────
    // Require DEBOUNCE_TICKS consecutive readings in same state
    if (rawTouched == candidateState) {
        if (sameStateCount < DEBOUNCE_TICKS) sameStateCount++;
    } else {
        candidateState  = rawTouched;
        sameStateCount  = 1;
    }

    if (sameStateCount < DEBOUNCE_TICKS) return;  // not confirmed yet
    bool confirmed = candidateState;

    // ── Holdoff after release — prevents double-trigger ───────────────────────
    if (confirmed && !touching) {
        if ((now - releaseMs) < holdoffMs) return;  // too soon after last release
    }

    // ── State transitions ─────────────────────────────────────────────────────
    if (confirmed && !touching) {
        touching      = true;
        touchStartMs  = now;
        preTouchMood  = mood_led_get_current();
        mood_led_set(CTRL_MOOD_THINK);   // thinking/recording
        ctrl_send(CTRL_TOUCH_ON);
        Serial.printf("[TOUCH] ON  raw=%lu  delta=%+d\n",
                      val, (int32_t)val - (int32_t)baseline);

    } else if (!confirmed && touching) {
        uint32_t held = now - touchStartMs;
        touching   = false;
        releaseMs  = now;
        mood_led_set(CTRL_MOOD_SPEAK);   // processing/STT
        ctrl_send(CTRL_TOUCH_OFF);
        Serial.printf("[TOUCH] OFF  held=%lums  raw=%lu\n", held, val);
    }
}
