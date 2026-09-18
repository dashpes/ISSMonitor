#include "io/inputs.h"

#include <Arduino.h>

#include "config.h"

namespace inputs {
namespace {

// ===========================================================================
// PART A -- the toggles.
// ===========================================================================

// One row per wired switch. Adding AUX1/AUX2 later is two more rows here and
// two more cases in commit() -- no new logic. That is what the table buys you
// over six copies of the same if-block.
enum SwitchId : uint8_t {
    SW_NIGHT = 0,
    SW_UNITS,
    SW_ALERT,
    SW_TRACK,
#if HAVE_AUX_SWITCHES
    SW_AUX1,
    SW_AUX2,
#endif
    SW_COUNT
};

struct Debounced {
    uint8_t  pin;
    bool     stable;       // the value we have committed to, already inverted
    bool     candidate;    // what the pin has been reading lately
    uint32_t since_ms;     // millis() when it started reading that
};

Debounced g_sw[SW_COUNT] = {
    {cfg::PIN_SW_NIGHT, false, false, 0},
    {cfg::PIN_SW_UNITS, false, false, 0},
    {cfg::PIN_SW_ALERT, false, false, 0},
    {cfg::PIN_SW_TRACK, false, false, 0},
#if HAVE_AUX_SWITCHES
    {cfg::PIN_SW_AUX1,  false, false, 0},
    {cfg::PIN_SW_AUX2,  false, false, 0},
#endif
};

// Copy the committed values out into AppState. Done here so the debounce loop
// stays generic and knows nothing about what each switch means.
void commit(AppState& s) {
    s.sw.night          = g_sw[SW_NIGHT].stable;
    s.sw.units_imperial = g_sw[SW_UNITS].stable;
    s.sw.alert_armed    = g_sw[SW_ALERT].stable;
    s.sw.track_trail    = g_sw[SW_TRACK].stable;
#if HAVE_AUX_SWITCHES
    s.sw.aux1           = g_sw[SW_AUX1].stable;
    s.sw.aux2           = g_sw[SW_AUX2].stable;
#endif
}

}  // namespace

// ===========================================================================
// PART B -- rotary encoders. Compiled out until HAVE_ENCODER is 1.
// io/console.h gives you 1/2 and 3/4 over serial meanwhile.
// ===========================================================================
//
// Why does this half use an interrupt when the toggles above get by on a
// 5 ms poll? Because the two are different KINDS of input, and that is the
// single idea worth taking away from this file.
//
// A toggle is a LEVEL. It is up or it is down, it stays that way for hours,
// and a read that arrives 5 ms late is still the right answer. Miss one and
// you catch it on the next pass.
//
// A detent is an EVENT. Turning the knob one click sends CLK and DT through
// a four-step cycle in a few hundred microseconds. Sample that at 5 ms and
// you do not get the answer late, you do not get it at all -- and a knob that
// drops clicks when you spin it feels broken in a way nothing else on this
// console does. So the edges get an interrupt, and the button, which is a
// level like any other, is debounced down in poll() with the toggles.

#if HAVE_ENCODER

namespace {

constexpr int kEncoderCount = 2;         // 0 = left screen, 1 = right screen

// Four quarter-steps to one detent on a KY-040. Counting quarter-steps and
// dividing at the end -- rather than trying to spot detents in the ISR --
// means a knob left resting between two detents contributes nothing and does
// not lose its place either: the remainder stays in the accumulator.
constexpr int32_t kQuartersPerDetent = 4;

// Transitions of a Gray-coded pair, indexed by (previous << 2) | current.
// +1 one way round the cycle, -1 the other. The zeros on the diagonal are
// "nothing changed"; the zeros at 0b0011 and friends are the transitions
// where BOTH pins appear to have moved at once, which is physically
// impossible and therefore a bounce or a missed edge. Scoring those as zero
// is the whole of the debouncing here -- noise contributes nothing instead
// of a phantom click.
//
// DRAM_ATTR because an ISR may run while the flash cache is disabled, and a
// table living in flash would not be readable at that moment.
const DRAM_ATTR int8_t kQuadrature[16] = {
     0, -1, +1,  0,
    +1,  0,  0, -1,
    -1,  0,  0, +1,
     0, +1, -1,  0,
};

struct Encoder {
    uint8_t pin_clk;
    uint8_t pin_dt;
    uint8_t pin_sw;

    // Written by the ISR, read by the main loop. `volatile` stops the
    // compiler caching them in a register across the loop that reads them;
    // it does NOT make the read-modify-write atomic, which is what the
    // critical sections below are for.
    volatile int8_t  state;      // last (clk << 1) | dt
    volatile int32_t quarters;

    // The push switch. Main loop only, no ISR anywhere near it.
    bool     sw_stable;
    bool     sw_candidate;
    uint32_t sw_since_ms;
    bool     click_pending;
};

Encoder g_enc[kEncoderCount] = {
    {cfg::PIN_ENC_L_CLK, cfg::PIN_ENC_L_DT, cfg::PIN_ENC_L_SW, 0, 0, false, false, 0, false},
    {cfg::PIN_ENC_R_CLK, cfg::PIN_ENC_R_DT, cfg::PIN_ENC_R_SW, 0, 0, false, false, 0, false},
};

// `quarters` is read-modify-written from two contexts at once: the ISR adds
// to it, takeEncoderDelta() drains it. On a 32-bit machine that is one
// instruction each way and it still races -- the drain reads, the interrupt
// fires and adds, the drain writes back its own stale value, and the click
// you just turned is gone. The spinlock is four instructions and removes the
// question.
portMUX_TYPE g_enc_mux = portMUX_INITIALIZER_UNLOCKED;

// Everything reachable from an ISR has to be in IRAM: the handler can run
// during a flash operation, when code in flash is simply not there.
//
// Note what is NOT in here. No Serial.print -- it takes a mutex and can
// block. No malloc, no String, no floating point. Read two pins, look up a
// number, add it on, return. Anything you are tempted to do with the value
// belongs in poll(), where it is allowed to take as long as it likes.
void IRAM_ATTR handleEncoder(int which) {
    Encoder& e = g_enc[which];
    const uint8_t now = static_cast<uint8_t>((digitalRead(e.pin_clk) << 1) |
                                              digitalRead(e.pin_dt));
    portENTER_CRITICAL_ISR(&g_enc_mux);
    e.quarters += kQuadrature[(static_cast<uint8_t>(e.state) << 2) | now];
    e.state = static_cast<int8_t>(now);
    portEXIT_CRITICAL_ISR(&g_enc_mux);
}

// attachInterrupt wants a plain void(*)(), so the index is baked in here
// rather than passed. Two lines each; a lambda or an argument would cost
// more than it saves.
void IRAM_ATTR onEncoderLeft()  { handleEncoder(0); }
void IRAM_ATTR onEncoderRight() { handleEncoder(1); }

// The click. Same debounce as the toggles, and for the same reason -- but
// the toggles commit a level, where this commits an EDGE: the flag is raised
// on the press, not the release, and stays up until somebody takes it.
void pollEncoderButtons(uint32_t now) {
    for (int i = 0; i < kEncoderCount; ++i) {
        Encoder& e = g_enc[i];
        const bool reading = (digitalRead(e.pin_sw) == LOW);   // PULLUP

        if (reading != e.sw_candidate) {
            e.sw_candidate = reading;
            e.sw_since_ms  = now;
        } else if (now - e.sw_since_ms >= cfg::MS_DEBOUNCE &&
                   e.sw_candidate != e.sw_stable) {
            e.sw_stable = e.sw_candidate;
            if (e.sw_stable) e.click_pending = true;
        }
    }
}

}  // namespace

#endif  // HAVE_ENCODER

// ===========================================================================
// The two public entry points. Both parts come through here: begin() sets up
// pins for the toggles and the encoders alike, and poll() debounces every
// LEVEL on the board -- four toggles and two push switches -- while the
// encoders' rotation has already been counted by the ISR above.
// ===========================================================================

void begin() {
    for (int i = 0; i < SW_COUNT; ++i) {
        pinMode(g_sw[i].pin, INPUT_PULLUP);
        // Seed from the actual pin so the console does not spend the first
        // 25 ms believing every switch is off.
        const bool on = (digitalRead(g_sw[i].pin) == LOW);
        g_sw[i].stable    = on;
        g_sw[i].candidate = on;
        g_sw[i].since_ms  = millis();
    }

#if HAVE_ENCODER
    for (int i = 0; i < kEncoderCount; ++i) {
        Encoder& e = g_enc[i];
        // CLK and DT are on 36/39/35, which are input-only and have NO
        // internal pull-up. That is survivable only because the KY-040
        // breakout carries its own 10k pull-ups on those two lines -- wire a
        // bare encoder to these pins and both float. The push switch is on a
        // normal pin and uses the internal pull-up like every other switch.
        pinMode(e.pin_clk, INPUT);
        pinMode(e.pin_dt,  INPUT);
        pinMode(e.pin_sw,  INPUT_PULLUP);

        // Seed the quadrature state from where the knob actually is. Start
        // from a guessed 0 instead and the first edge decodes against the
        // wrong previous state, which is a spurious click at boot.
        e.state    = static_cast<int8_t>((digitalRead(e.pin_clk) << 1) |
                                          digitalRead(e.pin_dt));
        e.quarters = 0;

        const bool down  = (digitalRead(e.pin_sw) == LOW);
        e.sw_stable      = down;
        e.sw_candidate   = down;
        e.sw_since_ms    = millis();
        e.click_pending  = false;
    }

    // Both pins of both encoders, on CHANGE. Watching only CLK would halve
    // the resolution and, worse, make the decoder unable to tell a real
    // transition from a bounce -- the direction lives in which pin moved
    // first, so you have to be watching both.
    attachInterrupt(digitalPinToInterrupt(cfg::PIN_ENC_L_CLK), onEncoderLeft,  CHANGE);
    attachInterrupt(digitalPinToInterrupt(cfg::PIN_ENC_L_DT),  onEncoderLeft,  CHANGE);
    attachInterrupt(digitalPinToInterrupt(cfg::PIN_ENC_R_CLK), onEncoderRight, CHANGE);
    attachInterrupt(digitalPinToInterrupt(cfg::PIN_ENC_R_DT),  onEncoderRight, CHANGE);
#endif
}

void poll(AppState& s) {
    const uint32_t now = millis();

    for (int i = 0; i < SW_COUNT; ++i) {
        Debounced& d = g_sw[i];          // reference, so you edit the real row
        const bool reading = (digitalRead(d.pin) == LOW);   // PULLUP: ON == LOW

        if (reading != d.candidate){
            d.candidate = reading; // changes the candidate slot to the reading
            d.since_ms = now; // resets the time to when it was switched
        }
        else if (now - d.since_ms >= cfg::MS_DEBOUNCE && d.candidate != d.stable){
            d.stable = d.candidate; //changes the base line to current status after 25ms as defined by MS_DEBOUNCE
        }

    }

#if HAVE_ENCODER
    pollEncoderButtons(now);
#endif

    commit(s);
}
// --- The two public halves of Part B, on the main loop side. ---------------
int takeEncoderDelta(int which) {
#if HAVE_ENCODER
    if (which < 0 || which >= kEncoderCount) return 0;
    Encoder& e = g_enc[which];

    int32_t detents;
    portENTER_CRITICAL(&g_enc_mux);
    // Truncating division, deliberately: a knob sitting one or two
    // quarter-steps past a detent reports nothing and keeps the remainder
    // for when it finishes the turn.
    detents      = e.quarters / kQuartersPerDetent;
    e.quarters  -= detents * kQuartersPerDetent;
    portEXIT_CRITICAL(&g_enc_mux);

    // Turning it the wrong way round? Swap CLK and DT in config.h -- that is
    // a wiring fact, and it belongs with the other wiring facts rather than
    // as a minus sign hidden in here.
    return static_cast<int>(detents);
#else
    (void)which;
    return 0;
#endif
}

bool takeEncoderClick(int which) {
#if HAVE_ENCODER
    if (which < 0 || which >= kEncoderCount) return false;
    // Main-loop only on both ends, so no critical section: the ISR never
    // touches this flag.
    const bool had = g_enc[which].click_pending;
    g_enc[which].click_pending = false;
    return had;
#else
    (void)which;
    return false;
#endif
}

}  // namespace inputs
