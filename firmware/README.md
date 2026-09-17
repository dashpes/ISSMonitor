# ISS Monitor — firmware

Firmware for a three-screen desk console that tracks the ISS in real time.
ESP32 + 3x GC9A01 round displays. See `../PARTS_LIST.md` for the hardware and
`../case/CASE_NOTES.md` for the enclosure.

## Get building

```bash
brew install platformio          # or: pipx install platformio
cd firmware
cp src/secrets_example.h src/secrets.h   # fill in Wi-Fi
pio test -e native               # runs on the Mac, no hardware. 15 tests, all red.
pio run                          # compiles for the ESP32
pio run -t upload -t monitor     # flash + serial
```

### Running the tests

```bash
./scripts/run_tests.sh      # fast, no network, no PlatformIO  <-- use this
pio test -e native          # the PlatformIO way; same tests, needs the registry
```

`run_tests.sh` compiles the tests with plain `c++` and a vendored copy of
Unity (`test/unity_vendor/`, 3 files from ThrowTheSwitch/Unity). Sub-second,
works offline. Run it after every edit.

`pio test -e native` is the important one. It builds only `lib/orbit`, which
has no Arduino dependency at all, so the orbital math can be proven correct
on your laptop before a single wire is stripped.

## Status

**2026-09-17 — hardware bring-up PASSED.**

- `pio run -e led` — LED blinks and fades. Toolchain, board, data cable,
  upload, serial monitor all confirmed working.
- `pio run -e smoketest` — all three panels come up **in sequence**
  (red TELEMETRY → green GLOBE → blue SKY), then all three hold their own
  card. Rings centred, colours correct → `offset_x/offset_y` and `rgb_order`
  are right. Shared SPI bus with three independent chip selects: working.

Two bugs found and fixed during bring-up, both in `include/display_config.h`:

1. **Floating chip selects.** LovyanGFX configures a panel's CS pin inside
   that panel's `init()`. Before that the pin floats, and CS is active-LOW,
   so every uninitialised panel was listening to the bus. Symptom: first draw
   appeared on all three screens, second on two, third on one.
   Fix: `prepareDisplayBus()` drives every CS HIGH before any `init()`.
2. **Shared RST owned per-panel.** All three panels share one reset line, so
   `init()` on panel N reset panels 1..N-1 — setup ended with only the last
   panel alive. Fix: `pin_rst = -1` on every panel, one manual reset pulse in
   `prepareDisplayBus()`.

**2026-09-17 — `geo.cpp` complete. 15/15 tests green.**

- `lookAngle`, `projectOrthographic`

`./scripts/run_tests.sh` → `15 Tests 0 Failures`.

**2026-09-17 — inputs + data chain.**

- `src/io/inputs.cpp` toggle debounce. Watch it: `pio run -e inputs`
- `lib/orbit/src/tle.cpp` + 10 tests. **25 tests green total.**
- `src/net/netclock.cpp`, `src/net/tle_fetch.cpp`, `src/app/propagator.cpp`
- Pin map switched to the **two-encoder** plan from `case/CASE_NOTES.md`.
  AUX1→D14, AUX2→D15, Encoder L 36/39/D27, Encoder R 35/D4/D19. Assumes
  **7-pin displays with no BLK** — confirm by counting pins on a module.
- LovyanGFX and Sgp4 are vendored in `lib/` (the registry was crawling).

**2026-09-17 — console is live.** All three screens running against real
Celestrak data. Left telemetry, centre globe, right pass prediction.

Fixed during bring-up:
- Shared RST was owned per-panel, so `init()` on one reset the others.
- CS floats until a panel's `init()` configures it, so the first draw landed
  on all three screens. `prepareDisplayBus()` handles both.
- `Sgp4::init()` returns false both for a bad element set *and* for one it
  already holds. An unchanged refresh read as a failure and switched tracking
  off. `propagator::load()` now tracks the loaded line1 itself.

### Open

| Item | Notes |
|---|---|
| **Globe layout** | Needs work. Graticule only, no coastlines — that needs a generated polyline table in PROGMEM. |
| Encoder ISR | `src/io/inputs.cpp` Part B. Blocked on the KY-040s. Set `HAVE_ENCODER=1` when wired. |
| Light sensor | `src/io/lightsensor.cpp`. Blocked on the GL5539. Set `HAVE_LDR=1`. |
| AUX switches | `HAVE_AUX_SWITCHES=1` once D14/D15 are wired. |
| Confirm displays are 7-pin | The two-encoder pin map spends D4 on Encoder R. An 8th BLK pin means a conflict. |
| Register-based switch scan | `digitalRead` x4 → one 32-bit GPIO register read + bitmasks. |

## Parts on hand (Sept 2026)

Wired-up-able today: **ESP32, 3 GC9A01 screens, toggles, LED, resistors.**
On order: **GL5539 photoresistor, KY-040 encoder.**

That gates exactly two things — `lightsensor.cpp` (~50 lines) and the ISR half
of `inputs.cpp` (~60 lines). **110 of 1,900 lines.** Everything else is open.

`HAVE_ENCODER=0` and `HAVE_LDR=0` in `platformio.ini` compile the absent
hardware out, and `src/io/console.h` replaces it with serial keys:

```
[ ]     encoder left / right        n   night palette      d   force dim
ENTER   encoder click               u   units km/mi        j   +60 s (skip to a pass)
?       dump state                  a   arm alert          t   track trail
```

Flip the flags to 1 when the parts land.

## Start here, tonight

**In VS Code:** install the *PlatformIO IDE* extension, then open the
`firmware` folder itself — not `ISSMonitor` — or PlatformIO won't find
`platformio.ini`. First open downloads the ESP32 toolchain (a few minutes,
once).

### Step 0 — the LED, and nothing else

```bash
pio run -e led -t upload -t monitor
```

5 slow blinks, then a continuous fade, with a heartbeat on serial. Proves the
toolchain, your board setting, that your USB-C cable carries **data** (the
classic first wall), upload, the serial monitor, and the LED circuit.

Wiring: `GPIO 13 → 150 Ω → LED long leg`, short leg → `GND`.

- Text scrolling but no light → the circuit, not the chip. Check the long leg
  faces the resistor and that GND really lands on a GND pin.
- Lit but faint → expected at 150 Ω on a 3.3 V pin. Two in parallel (75 Ω)
  roughly doubles it. Never below ~47 Ω.
- No serial port at all → charge-only cable, or you need the CH340 / CP2102
  driver (match the small chip beside the USB port).

### Step 1 — everything you have

```bash
pio run -e smoketest -t upload -t monitor
```

`hw_check/smoketest.cpp` proves the wiring before any firmware exists: reset
reason, heap, then each display alone (red TELEMETRY / green GLOBE / blue SKY)
with a colour-bar card, then all three at once, then live switch states and an
LED brightness sweep. It exists so that when a screen stays black you know the
program is right and only the wiring is in question.

What it catches: swapped red/blue (`rgb_order`), a clipped or off-centre circle
(`offset_x/offset_y`), two panels lighting at once (shared CS), and brownouts
(`ESP_RST_BROWNOUT` — that is C1, or the USB cable).

## The order to write things

Each step is independently testable. Do not skip ahead — step 6 is miserable
to debug if step 2 is quietly wrong.

| # | File | What | Lines | Difficulty |
|---|---|---|---|---|
| 1 | `lib/orbit/src/geo.cpp` | coordinate math | ~150 | ●●○○○ |
| 2 | `lib/orbit/src/tle.cpp` | TLE parse + checksum | ~120 | ●●○○○ |
| 3 | `src/io/inputs.cpp` | six toggles, debounced | ~90 | ●●○○○ |
| — | `src/io/inputs.cpp` | encoder ISR (Part B) | ~60 | ⏸ **waiting on the KY-040** |
| — | `src/io/lightsensor.cpp` | LDR, EMA, hysteresis | ~50 | ⏸ **waiting on the GL5539** |
| 5 | `src/ui/telemetry_screen.cpp` | left screen | ~250 | ●●●○○ |
| 6 | `src/net/netclock.cpp` | Wi-Fi + NTP, non-blocking | ~120 | ●●●○○ |
| 7 | `src/net/tle_fetch.cpp` | HTTPS + NVS cache | ~150 | ●●●○○ |
| 8 | `lib/orbit/src/passes.cpp` | pass prediction | ~180 | ●●●●● |
| 9 | `src/ui/sky_screen.cpp` | right screen | ~250 | ●●●○○ |
| 10 | `src/app/alert.cpp` | LED state machine | ~60 | ●●○○○ |
| 11 | `src/ui/globe_screen.cpp` | the globe | ~350 | ●●●●● |
| 12 | `src/main.cpp` | fill in the TODOs | ~80 | ●●○○○ |

Roughly **1,900 lines you write**, on top of ~900 already here.

Steps 1–2 need no hardware at all. Step 3 needs one switch on a breadboard.
Step 5 needs one screen. You are not blocked on the case, the perfboard, or
the parts still in transit.

## Reference

| File | What it is |
|---|---|
| `include/config.h` | pin map, transcribed from PARTS_LIST.md |
| `include/display_config.h` | LovyanGFX 3-panel boilerplate. Nothing to learn, lots to get subtly wrong |
| `include/theme.h` | two palettes, RGB565 |
| `src/app/state.h` | the AppState struct — the one real architectural decision |
| `src/ui/screen.h` | abstract base class, heavily annotated |
| `test/test_orbit/test_geo.cpp` | 15 tests with independently-generated expected values |
| `hw_check/smoketest.cpp` | wiring diagnostic |
| `src/io/console.h/.cpp` | serial stand-in for unwired hardware; also a debug console |
| `platformio.ini` | two envs, C++17, warnings on |

## House rules

1. **No `delay()`**, anywhere, ever, after `setup()`. Everything is
   `if (now - last >= interval)`.
2. **No `String`** in anything that runs every frame. `char buf[N]` +
   `snprintf`. Heap fragmentation on a chip with no MMU is permanent.
3. **No dynamic allocation after boot.** Fixed arrays, known worst case.
4. **`lib/orbit` never includes `<Arduino.h>`.** If you are tempted, the
   logic belongs in `src/` instead. This boundary is what keeps the tests
   runnable.
5. Degrees at every public boundary, radians only inside a `.cpp`.
6. Every `virtual` override gets `override`.
