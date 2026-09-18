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

**2026-09-17 — the globe has land.**

The centre screen was a graticule on a blue disc, which reads as a wireframe
ball and not as the Earth. It now draws Natural Earth 1:110m coastlines.

- `scripts/gen_globe_data.py` → `src/ui/globe_data.h`. Coastlines simplified
  in 3D to ~1 px, the Antarctic and antimeridian seams removed, plus the
  graticule and equator. 1,128 coastline vertices, **12 kB of flash, 0 of RAM.**
- Vertices ship as unit vectors, not lat/lon. Lat/lon would cost ~12,000
  *software* double-precision trig calls per frame — the ESP32's FPU is
  single-precision only. `orbit::makeOrthoView()` hoists the six trig calls
  that depend on the camera out to once per frame; each vertex is then nine
  float multiplies. See the header comment in `lib/orbit/include/orbit/geo.h`.
- `orbit::projectUnit()` is cross-checked against the original
  `projectOrthographic()` over a sweep of the whole sphere. **29 tests green.**
- `src/ui/globe_preview.png` is regenerated alongside the header, through the
  same projection, so you can check the shape without flashing anything.

**2026-09-17 — the land is filled in.**

Outlines alone were not legible: a closed curve does not tell you which side of
itself is inside, so the coastline and the graticule read as two sets of lines
rather than as continents. The land is now filled.

- The fill runs the projection *backwards* — `orbit::unprojectUnit()`, pixel to
  direction — and asks a generated land/sea table what is underneath. That
  sidesteps the hard part of filling outlines: a continent straddling the limb
  projects to a shape bounded partly by coast and partly by an arc of the
  horizon, and LovyanGFX has no polygon fill anyway. Per-pixel has no special
  cases: limb clipping, inland seas and Antarctica all come out for free.
- `src/ui/land_mask.h` is a **cube map** of one bit per texel, 128² per face,
  12 kB of flash. A cube map and not a lat/lon grid because the lookup needs
  the dominant axis and one divide, where lat/lon would need an `asin` and an
  `atan2` per pixel. The generator emits the lookup function *next to* the
  table it fills, so the encoder and decoder cannot drift apart.
- Filling wants the map seams that stroking had to delete. Antarctica's ring
  only closes because of the artificial edges along lat −90 and the
  antimeridian; remove them and the even-odd rule fills the sea instead. So the
  mask is rasterised from the raw rings, not from the stroke table.
- **The mask is cached, and rebuilt a band at a time.** Deciding it costs
  ~34,000 inverse projections — 62 ms, measured — which is far too much for one
  frame. The view tracks the ISS and drifts at 0.075 px/s, so a rebuild is only
  triggered once the view has actually moved a pixel; in practice that fires
  every 6–8 s. The rebuild is then spread over 8 frames, a horizontal band per
  frame, with the old mask still being drawn meanwhile. 7.2 kB of RAM.
- `test/test_globe/test_landmask.cpp` checks the table against places you can
  point at on a globe — Sahara, Amazon, Tibet, Queen Maud Land, mid-Pacific —
  plus Antarctica all the way round, and that total land lands near Earth's
  29%. Six faces and three axes have plenty of self-consistent wrong wirings;
  a swapped pair would put Africa in the Pacific and nothing would warn you.
  **38 tests green.** `run_tests.sh` now picks up `test/test_*/`.

Also fixed while in there: the sprite was `setColorDepth(8)`, which in
LovyanGFX means **rgb332** — three bits of red, three of green, and *two* of
blue. Two bits of blue cannot hold `rgb565(10, 34, 58)`: the ocean's blue was
being discarded outright and the Atlantic was coming out dark green, which
against green coastlines is one flat smudge. It is now `palette_8bit` — same
57.6 kB, plus 768 bytes of lookup table, and every colour in `theme.h`
survives exactly. Draw calls on the globe sprite take a palette index now;
`GlobePens` in `globe_screen.h` keeps that from leaking into the drawing code.

To change the detail level or the viewpoint of the preview:

```bash
./scripts/gen_globe_data.py --tolerance-px 0.8 --view 33,-117
```

**2026-09-17 — globe frame cost, measured on the bench.**

`PLATFORMIO_BUILD_FLAGS=-DGLOBE_PROFILE=1 pio run -t upload -t monitor` turns on
a once-a-second frame report. Numbers below are from the real console, not an
estimate — and they were worth taking, because two of the estimates were wrong.

```
[globe] 10 fps | mask 0 bands | draw 11.2 = bg 0.5 + land 4.8 + grat 1.9
                 + coast 2.8 + over 1.3 | push 23.9 | worst 36.3 ms | heap 163428
```

| Stage | ms | |
|---|---|---|
| `pushSprite` | **23.9** | 240×240×2 B over SPI at 40 MHz. The floor, and it predates all of this |
| land runs | 4.8 | blitting the cached mask |
| coastline | 2.8 | ~1,200 vertices |
| graticule | 1.9 | ~830 vertices |
| overlay + bg | 1.8 | trail, ISS, observer, text, ocean disc |
| **quiet frame** | **36.3** | |
| **rebuild frame** | **44–47** | one of eight mask bands |

Two corrections to what was guessed beforehand:

1. A full mask rebuild is **62 ms**, not the ~17 ms predicted from instruction
   counts. Done in one go it made a **98 ms** frame — and since `main.cpp`
   renders all three screens in the same tick, that overran `MS_REDRAW` on its
   own. That is why the rebuild is banded across 8 frames; worst frame is now
   47.7 ms.
2. The redraw is dominated by `pushSprite`, not by any of the drawing. All the
   geometry together is 11.2 ms against 23.9 ms of SPI. If the globe ever needs
   to be faster, the thing to attack is the full-frame push — a dirty-rectangle
   push, or dropping the globe to 5 fps, which nothing would notice given the
   ISS moves 0.075 px/s.

Heap sits at 163 kB free with the 57.6 kB sprite and the 7.2 kB mask both live,
so the memory trade-off in `begin()` still has plenty of room.

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
| 11 | `src/ui/globe_screen.cpp` | the globe | ~150 | ●●●○○ |
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
| `src/ui/globe_data.h` | **generated** — coastline/graticule vertices. Do not hand-edit |
| `src/ui/land_mask.h` | **generated** — land/sea cube map + its lookup. Do not hand-edit |
| `scripts/gen_globe_data.py` | builds both of the above from Natural Earth, and the preview PNG |
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
