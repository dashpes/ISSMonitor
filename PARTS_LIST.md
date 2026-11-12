# ISS Monitor — Parts List & Setup

A 3-screen desk "spacecraft console" that tracks the International Space Station in real time.

| Screen | Position | Shows |
|---|---|---|
| **Globe** | Top | Spinning cartoon Earth with the ISS orbiting along its real ground track |
| **Lower-left** | Below left | Selectable via encoder L — e.g. telemetry (speed, altitude, lat/long) |
| **Lower-right** | Below right | Selectable via encoder R — e.g. next visible pass over Tustin + compass |

Screens sit in a triangle (one on top, two below). Each lower screen has a click rotary encoder next to it; six toggles along the bottom change modes. A light sensor in the top of the case auto-dims the console, and a green LED flashes before a visible pass.

See **`WIRING_DIAGRAM.svg`** (or `.png`) for the full picture.

---

## 1. Parts

### Already have

| Qty | Part | Notes |
|---|---|---|
| 1 | DOIT ESP32 dev board (FCC ID 2BB77-ESP32-32X), USB-C | Original ESP32, Wi-Fi + BT, no PSRAM |
| 2 | 100 kΩ resistors (1 used, 1 spare) | Light-sensor divider (R1). Bands: **brown-black-yellow-gold** (4-band) or **brown-black-black-orange-brown** (5-band, 1%) |
| 1 | 5 mm green LED (2.8–3.6 V, 20 mA) | "ISS OVERHEAD" indicator (LED1) |
| 1–2 | **150 Ω resistor** (brown-green-brown-gold) | LED resistor (R2). Test brightness first. Too dim → two 150 Ω in parallel (= 75 Ω) |
| — | 10 kΩ resistor (brown-black-orange-gold) | Not used. **Don't mix it up with R1 (100 kΩ = brown-black-YELLOW-gold)** |
| 1+ | 100 nF ceramic capacitor ("104") | Optional decoupling: one across VCC/GND at a display, or at the ESP32 3V3 pin. Not polarized |
| — | M2 + M3 screws and heat-set inserts | M2 for the display modules, M3 for the case shell / perfboard. Check hole sizes on your parts first |
| — | USB-C **data** cable + 5 V USB power brick | Powers the whole console |
| — | Jumper wires, breadboard, hookup wire | For prototyping and wiring |
| — | Female headers | ESP32 socket on the perfboard |
| 1 | **4 × 6 cm perfboard** | Main board (see §6a) |

### Ordering

| Qty | Part | Notes |
|---|---|---|
| 3 | **Hosyond 1.28" GC9A01 round TFT** (3-pack) | 240×240, SPI |
| 6 (+spares) | **Taiss MTS-101 mini toggle, 2-pin ON/OFF** (10-pack) | 6 mm bushing, 13×10 mm body, 33 mm overall. Logic-level use only — the 6 A rating is way more than needed. 4 wired + 2 AUX |
| 1 | **Panel-mount USB-C 3.1 extension, 0.3 m** (male → female with screw flange) | Bought. Mounts on the back wall; cable coils inside to the ESP32 |
| 2 | **KY-040 rotary encoder module** (20 detents, push button, M7 bushing) + 2 × **Taiss 20 × 15.5 mm knobs** (6 mm D-shaft, set screw) | One per lower screen: turn = change what it shows, click = cycle its mode. Ordered |
| 1 | **GL5539 photoresistor** (Chanzon 20-pack, option F) | Ordered — mounts in the shell's top plate |

### Still need (cheap / probably in a drawer)

| Qty | Part | Notes |
|---|---|---|
| 1 kit | **Electrolytic capacitor assortment** containing 100–470 µF at ≥10 V (C1) | Bulk cap for Wi-Fi spikes (16 V/25 V parts are fine too). OK to breadboard without it meanwhile — see §4 |
| ~40 pins | **Male pin headers** (2.54 mm, breakable strips) | Plug-in connectors in rows 1–2 / cols 1 & 14. Skip if your header kit has male strips |
| ~30 | **Female-to-female Dupont jumpers, 20 cm** | Board ↔ displays (7 each), switches, LDR, LED. Skip if you have enough long ones |

### Optional console upgrades

| Part | Why |
|---|---|
| **MTS-103 3-pin ON-OFF-ON toggle** (the "3 Pin ON/Off/On" option in the same listing) | 3-position mode selector for the left screen (e.g., SPEED / ALT / CREW). Uses GPIO 27 + 19 — see §3. |
| Flip-up toggle safety covers ("missile switch" covers) | Put one over the ALERT switch for maximum spaceship |
| ~~Panel-mount USB-C extension~~ | Bought — now the plan (see Case notes). It carries data, so programming through it works |

> **Which Taiss switch to buy:** all the MTS-10x variants use the same metal body and look identical from the front. Only the solder lugs on the back differ.
> - **2-pin ON/OFF (MTS-101):** best for the four toggles, since each has exactly two positions.
> - **3-pin ON-OFF-ON (MTS-103):** add one or two for the optional 3-position selector (§3).
> - Avoid using ON-OFF-ON switches for the simple toggles. The center position makes on/off confusing.
> - **3-pin ON-ON (MTS-102)** also works as a plain toggle (use the middle lug + one outer lug), but it has no advantage here.
> - **Mounting:** drill or print a ~6.3 mm hole (measure your switch's threaded bushing first), and keep the front panel ≤ 3 mm thick so the nut can thread on.

### Case notes
The case is designed: **`case/ISS_console.scad`** (parametric OpenSCAD), STLs in `case/stl/`, previews in `case/previews/`, and everything about printing, assembly and the measurements still needed in **`case/CASE_NOTES.md`**.
- Sloped cockpit panel tilted 35° back from vertical, 144 × 85 × 96.5 mm, two main prints (front panel + rear shell) plus 3 small clamp bars.
- **USB-C:** the panel-mount extension sits in the back wall, 40 mm right of centre; the ESP32 stays on the perfboard with its port facing the right side wall.
- **Heat-set inserts:** M3 only — 6 for the panel screws, 4 for the display clamp bars. M3 self-tap into the perfboard pegs and the USB-C flange. No M2 needed: the displays are held by clamp bars, not by their mounting holes.
- **Displays:** swap the in-plane right-angle header for a straight one pointing backwards (or solder wires) — see CASE_NOTES.

### Tools
Soldering iron, multimeter, flush cutters, wire strippers, Bambu P1S for the case.

---

## 2. Pin map

Match wires by the **label printed on the board** (D18, D23, 3V3…), not by physical position — pin order varies between board and module versions.

> **The pin map below follows `firmware/include/config.h`, which describes the unit as built.** `WIRING_DIAGRAM.svg/.png` shows the first plan. Two things differ: the four mode switches went into the panel in a different order, and the two encoders swapped pins. Where the diagram and this table disagree, this table and `config.h` are right.

### Displays (shared SPI bus)

All three displays share every line **except CS**.

| Display pin | ESP32 pin | Wire color in diagram |
|---|---|---|
| VCC | 3V3 | red |
| GND | GND | gray |
| SCL (clock) | **D18** | yellow |
| SDA (data) | **D23** | blue |
| DC | **TX2 / GPIO 17** | green |
| RST | **RX2 / GPIO 16** | purple |
| BLK / BL (only if your module has an 8th pin) | **D4** | cyan (dashed) |

| Screen | CS pin |
|---|---|
| Top — live position / globe | **D21** |
| Lower-left (encoder L) | **D5** |
| Lower-right (encoder R) | **D22** |

> "SDA/SCL" on these modules is SPI, not I2C — the labels are just misleading.
>
> **7-pin modules (no BLK):** the backlight is always on. "Night mode" then dims the UI by switching to a dark/red color palette instead of dimming the backlight.

### Inputs & outputs

| Part | ESP32 pin | Wiring |
|---|---|---|
| SW1 — **NIGHT** (dim / red UI), panel slot 1 | **D32** | one leg → D32, other leg → GND |
| SW2 — **UNITS** (km ↔ mi), panel slot 2 | **D26** | one leg → D26, other leg → GND (diagram: D33) |
| SW3 — **ALERT** (arm the pass LED), panel slot 3 | **D33** | one leg → D33, other leg → GND (diagram: D25) |
| SW4 — **TRACK** (orbit trail on/off), panel slot 4 | **D25** | one leg → D25, other leg → GND (diagram: D26) |
| Light sensor (LDR + 100 kΩ) | **D34** | 3V3 → LDR → **D34** → 100 kΩ → GND. LDR sits in the shell's top plate, ~10 cm leads |
| "ISS OVERHEAD" LED (green) | **D13** | D13 → 150 Ω → LED long leg (+); short leg (−) → GND |
| C1 100–470 µF electrolytic | — | + leg → 3V3, − leg (stripe) → GND, close to the board |
| SW5 — **AUX 1** | **D14** | one leg → D14, other leg → GND |
| SW6 — **AUX 2** | **D15** | one leg → D15, other leg → GND (strapping pin — harmless for a toggle) |
| Encoder L (lower-left screen) CLK / DT / SW | **D35 / D4 / D19** | KY-040 `+` → **3V3**, GND → GND. 35 is input-only; the module's own pull-ups cover CLK/DT. SW uses `INPUT_PULLUP`. (Diagram: these are Encoder R) |
| Encoder R (lower-right screen) CLK / DT / SW | **VP 36 / VN 39 / D27** | same. 36/39 are input-only. (Diagram: these are Encoder L) |

**Switches don't need resistors:** the code enables the ESP32's internal pull-ups (`INPUT_PULLUP`). Switch **ON = pin reads LOW**.

### Pins to leave alone

| Pin(s) | Why |
|---|---|
| GPIO 6–11 | Wired to the board's flash chip |
| GPIO 0, 2, 12, 15 | Boot-mode "strapping" pins — can stop the board from booting/flashing |
| GPIO 1, 3 (TX0/RX0) | USB serial for uploading and Serial Monitor |
| GPIO 34–39 | Input-only, no internal pull-ups (fine for the LDR on 34 and the encoders' CLK/DT, which have pull-ups on the KY-040 board; not for bare switches) |

---

## 3. Optional 3-position mode selector (MTS-103 ON-OFF-ON)

> **Not supported by the current build.** D27 and D19 are now the encoder push switches, and the firmware has no code for this selector. Kept as a record of the idea.

| Switch pin | Connect to |
|---|---|
| Middle (common) | GND |
| Outer A | **D27** |
| Outer B | **D19** |

| Lever | D27 | D19 | Left screen shows |
|---|---|---|---|
| Up | LOW | HIGH | SPEED |
| Center | HIGH | HIGH | ALTITUDE |
| Down | HIGH | LOW | CREW |

---

## 4. Small circuits

### Light sensor (auto-dim)

```
3V3 ──[ LDR GL5539 ]──┬── D34
                      │
                   [ 100 kΩ ]
                      │
                     GND
```
Brighter room → LDR resistance drops → higher reading on D34 (0–4095). D34 uses ADC1, which keeps working while Wi-Fi is on.

### Pass-alert LED

```
D13 ──[ 150 Ω ]──►|── GND
                 green LED (long leg toward the resistor)
```
**Why 150 Ω and not 330 Ω:** this green LED needs 2.8–3.6 V, and the ESP32 pin only puts out 3.3 V. That leaves very little voltage for the resistor. With 330 Ω the LED gets about 1 mA or less, so it would be dim or dark. 150 Ω gives roughly 2–4 mA, which is enough for a modern green LED as an indicator.

- **Test first on the breadboard:** 3V3 → 150 Ω → LED → GND.
- **Too dim:** put **two 150 Ω in parallel** (side by side, both ends joined) = 75 Ω. Don't go below ~47 Ω, to protect the GPIO pin.
- **Too bright:** dim it in code with PWM (`analogWrite` / `ledcWrite`).

### Capacitors (what you have vs. what's listed)

| Marking | Value | Type | Use here |
|---|---|---|---|
| **104** | 100 nF = 0.1 µF | Ceramic | ✅ Optional: one across **VCC/GND at each display**. Short legs, as close to the display pins as possible |
| **474** | 470 nF = 0.47 µF | Ceramic/film | Works in the same spot, but not needed |
| 470 pF / 1 nF / 220 nF (kit ceramics) | tiny | Ceramic | Not needed. 220 nF could stand in for a 100 nF at a display |
| **100–470 µF** (to buy) | 100,000–470,000 nF | Electrolytic (can-shaped, has a stripe) | **C1**, the bulk cap. Hundreds of times bigger than the 474 |

**C1 is included as insurance** against brownout resets when Wi-Fi turns on. It's polarized: the stripe (−) leg goes to GND, and the long leg goes to 3V3. Place it close to the board's 3V3/GND pins. To confirm it's working, print `esp_reset_reason()` at boot. `ESP_RST_BROWNOUT` means a power dip.

### Power

Everything runs from the ESP32's **USB-C port**. Total draw is well under 500 mA (three screens + Wi-Fi), and the board's 3.3 V regulator feeds the screens.

---

## 5. Software setup

> **Superseded.** The firmware builds with PlatformIO, not the Arduino IDE, and the libraries are fetched by `scripts/vendor_libs.sh`. See `docs/firmware.md` for the current procedure. The data sources below are still correct.

1. **Arduino IDE** → Boards Manager → install **esp32 by Espressif**.
2. Board: **ESP32 Dev Module** (or **DOIT ESP32 DEVKIT V1**). Upload speed 921600.
3. Libraries (Library Manager):
   - **LovyanGFX**: recommended for 3 screens; each panel gets its own CS cleanly. TFT_eSPI also works but needs manual CS juggling and has had problems with ESP32 core 3.x.
   - **Sgp4** by Hopperpop: calculates ISS position and passes on the ESP32 itself.
   - **ArduinoJson**
4. Data:
   - Orbit data (TLE), refreshed once a day: `https://celestrak.org/NORAD/elements/gp.php?CATNR=25544&FORMAT=TLE`
   - Time: NTP via `configTime()` (set timezone to `PST8PDT`)
   - Observer location: Tustin, CA ≈ 33.746° N, 117.826° W
5. If the board doesn't show up as a port: try a different USB-C cable (many are charge-only), then install the **CH340** or **CP2102** driver. Match it to the small chip next to the USB port.

---

## 6. Build order

1. **One screen on a breadboard.** Wire display #1 (center), flash a "hello world," and confirm it draws.
2. **Add screens #2 and #3.** Same bus, each with its own CS. Confirm all three draw different things.
3. **Toggles:** print their states to Serial Monitor, then to a screen.
4. **LDR:** print raw readings in light and dark, and pick your dim threshold.
5. **LED:** test blinking from code.
6. **Capacitors:** add C1 across 3V3/GND, plus the 100 nF caps at each display.
7. **Globe + ISS math:** orbit data → live position → pass predictions.
8. **Move to perfboard:** solder, socket the ESP32, and keep wires short.
9. **Print the console case** (`case/`): measure the parts listed in `case/CASE_NOTES.md` first, update the params, print the panel, test-fit, then the shell.

---

## 6a. Perfboard layout (4 × 6 cm)

> **The case changed after this drawing.** The latest shell (`case/ISS_console.scad`) puts the ESP32's USB-C port at the board's right edge, facing the right side wall, and the panel-mount USB-C 40 mm right of centre. See `case/previews/floor_layout.png`. The column and row plan below still applies.

See **`PERFBOARD_LAYOUT.png`**: headers in columns 3 and 13 (your board's pin rows are 10 holes / 1.0" apart), rows 6–20. The USB-C end overhangs the short edge that faces the case's back wall. Connectors go in rows 1–2, columns 1–2 and column 14, because the ESP32's antenna end hovers over rows 3–5.

- **Why 4 × 6 instead of 3 × 7:** the ESP32's two pin rows sit ~9–10 holes apart. A 3 × 7 cm board is only ~10 holes wide, so there's no room beside the rows for wiring, and at 10-hole spacing it doesn't fit at all. The 4 × 6 cm board (~14 holes wide) leaves spare columns beside each row for solder connections.
- **Breadboard testing:** this ESP32 is too wide to leave free holes on both sides of one breadboard. Snap two breadboards together (remove the power rail between them) so the ESP32 straddles both. The +/− rails work normally: jumper 3V3 → + and GND → −.
- **Getting the header spacing exactly right:** push both female header strips onto the ESP32's pins, set the whole assembly on the perfboard, and solder the headers in place. Then pull the ESP32 out.
- **USB-C:** put the headers at the very end of the board, with the USB end of the ESP32 overhanging the edge by a few mm. Mount that board edge right against the case wall's USB-C cutout.
- **Connectors, not direct wires:** add male pin headers (or JST-XH) in rows 1–2, columns 1–2 and column 14 for the 3 displays (7 pins each), the 4 switches, the light sensor and the LED. Then everything unplugs for case assembly.
- **Mounting:** M3 screws through the perfboard corner holes into heat-set inserts. Brace the USB end well, because plugging in the cable pushes on it.

## 7. Pre-power checklist

- [ ] All grounds connected together
- [ ] Nothing on GPIO 0, 2, 12, 15, or 6–11
- [ ] Displays on **3V3**, not VIN (5 V)
- [ ] C1 stripe (−) to GND
- [ ] LED long leg toward the resistor/GPIO
- [ ] LED resistor is 47 Ω or higher, never wired straight to the pin
- [ ] Multimeter: no short between 3V3 and GND before plugging in
