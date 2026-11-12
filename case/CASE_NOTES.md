# ISS Monitor — Case (OpenSCAD)

`ISS_console.scad` is the whole case, fully parametric. Open it in OpenSCAD (`brew install --cask openscad`), pick a
`part` at the top of the file (or use the Customizer), F6, export STL. Ready-made STLs are in `stl/`, previews in
`previews/`, and `panel_layout.png` / `panel_layout_1to1.pdf` are the dimensioned front-panel drawing (the PDF prints
1:1 on Letter — check the 100 mm bar). `panel_drawing.py` regenerates the drawing; its numbers mirror the top of the .scad.

**Overall: 144 W × 85 D × 96 H mm.** Front panel tilted back 35°, top corners chamfered 28 mm at 45°.

```
                [ TOP ]            <- live position / globe
      [ L ]           [ R ]        <- two secondary screens
 (knob)  | | | | | |  (knob)       <- 6 toggles at 13 mm pitch, one encoder per lower screen
```
Green PASS LED top-left of the top screen. Light sensor looks up through the shell's top plate.

## Parts to print

| Part | What it is | Print orientation | Notes |
|---|---|---|---|
| `shell_print` | Rear box: floor, sides, chamfered corners, back, top, front lip, 6 screw posts, USB-C pad, perfboard pegs, LDR window | **Back wall on the bed** (already oriented) | No supports. Chamfer walls are vertical in this orientation. |
| `panel_print` | Front plate: 3 screen bezels, 6 toggles, 2 encoders, LED, engraved labels, 5 clamp bosses | **Face down** (already oriented) | Textured plate = nice face. Labels are 0.6 mm engravings (fill with a paint marker). |
| `clamp_print` | 3 bars that hold the display modules against the panel back (two 58 mm for the lower screens, one 56 mm for the top) | Flat, feet up | |
| `bezel_print` | Optional contrasting rings, glue over the screen openings | Flat | Cosmetic. |

PLA or PETG, 0.2 mm layers, 3 walls, 20% infill. Print the **panel first** and test-fit every part on it before printing the shell.

## Hardware (case side)

| Qty | Item | Where |
|---|---|---|
| 11 | M3 heat-set inserts (OD ≈ 4.5, hole is 4.0 — `insert_d`) | 6 panel posts, 5 clamp bosses |
| 6 | M3 × 8 flat-head (countersunk) screws | panel → shell |
| 6 | M3 × 8 screws (any head) | clamp bars → bosses |
| 4 | M3 × 6 self-tapping | perfboard → floor pegs (2.5 mm pilot holes) |
| — | hot glue | USB-C connector → back-wall cutout. The model also has two 2.5 mm holes in a 5.5 mm pad for an optional M3 flange mount; the built unit does not use them |
| 4 | 10 mm stick-on rubber feet | recesses on the bottom |

## How it goes together

1. **Displays**: the round glass drops into a 36.4 mm pocket in the panel back; the PCB sits flat against the panel. A relief pocket takes the header tab / FPC connector. One clamp bar per screen: the two lower bars are horizontal (50 mm boss pitch, sharing the middle boss), the top bar is vertical (48 mm pitch) so it clears the top screen's sideways tab. The module's mounting holes are not used, so their spacing doesn't matter.
2. **Tabs point**: top screen → right (0°), lower-left → up-left (135°), lower-right → up-right (45°). Rotate the image in software to match.
3. **Toggles** (Taiss MTS-101, 6 mm bushing, 13 × 10 body): 6.4 mm holes, 12 mm rear counterbore leaving 2.5 mm so the nut threads on. Positions ±6.5, ±19.5, ±32.5 from centre, 11 mm up.
4. **Encoders** (KY-040, M7 bushing, 8 mm thread): 7.2 mm holes at ±50 from centre, 15 mm up, 15 mm rear counterbore leaving 2.5 mm. Mount the board with its long axis pointing **up** the panel and the 5-pin header facing **outward** (toward the side wall) — inward, the pins would touch the AUX 2 / NIGHT switch body. Power the module from **3V3**, not 5 V. Taiss 20 × 15.5 knobs, set screw on the D-flat.
5. **LED**: 5.1 mm press-fit hole top-left of the top screen ("PASS"); the flange stops on the back.
6. **LDR** (GL5539): 5.2 mm hole in the shell's top plate (right-rear); glue in from inside, face flush, ~10 cm leads down to the perfboard.
7. **Panel → shell**: 6 × M3 flat-head from the front into inserts in the posts (2 bottom corners, 2 on the chamfers, 2 along the top edge).
8. **Perfboard** (4 × 6 cm) on 4 floor pegs, 18 mm left of centre. The ESP32's USB-C port sits at the board's right edge, facing the right side wall. Set `pcb_hole_dx/dy` to your board's real corner-hole spacing.
9. **USB-C panel mount** in the back wall, 40 mm right of centre, so its body and cable stay clear of the perfboard. 15 × 8.5 cutout for a body about 14.4 wide, screw holes 22.4 apart, measured on the real connector. The 0.3 m cable sweeps round inside and plugs into the ESP32. See `previews/floor_layout.png`. The built unit holds the connector in the cutout with hot glue; the flange screw holes are an option.

## ⚠️ One change on the displays

The Hosyond modules come with a **right-angle header lying in the plane of the PCB**. With Dupont plugs on it that's ~22 mm sticking out sideways, which doesn't fit anywhere on this panel. Either desolder it and fit a **straight header on the back** (pins point rearward, plenty of room), or **solder wires directly** to the pads. The tab pockets assume one of those.

## Pin plan (everything used — no spare GPIOs after this)

This table follows `firmware/include/config.h`, which matches the unit as built. The encoders and the mode switches differ from the first plan in `WIRING_DIAGRAM.svg`.

Assumes the screens are the 7-pin type with no backlight pin (matches the photo), which frees D4.

| Signal | ESP32 pin | Notes |
|---|---|---|
| Encoder L: CLK / DT / SW | **GPIO 35** / **D4** / **D19** | 35 is input-only; the KY-040's on-board 10 kΩ pull-ups cover CLK/DT. SW uses `INPUT_PULLUP`. |
| Encoder R: CLK / DT / SW | **GPIO 36 (VP)** / **GPIO 39 (VN)** / **D27** | same |
| AUX 1 toggle | **D14** | `INPUT_PULLUP` |
| AUX 2 toggle | **D15** | `INPUT_PULLUP`. Strapping pin, but only affects boot-log output — harmless. |
| NIGHT / UNITS / ALERT / TRACK | D32 / D26 / D33 / D25 | panel slots 1–4, left to right |
| LDR, LED | D34, D13 | unchanged |

If you'd rather keep D15 free, drop AUX 2.

## Measurements to confirm (single-line edits at the top of the .scad, marked MEASURE)

| Param | What to measure | Current |
|---|---|---|
| `disp_glass_d` | outer diameter of the round glass | 35.6 |
| `disp_glass_h` | glass top above the PCB front face | 2.5 |
| `disp_pcb_d` | PCB diameter (round part) | 38 |
| `tab_w`, `tab_len` | header tab width, and how far it sticks past the round edge | 22, 16 |
| `usbc_cut`, `usbc_hole_sp` | USB-C flange: body cutout (w × h), screw spacing | 15 × 8.5, 22.4 (measured) |
| `pcb_hole_dx/dy` | perfboard corner-hole spacing | 54, 34 |
| `insert_d` | heat-set insert OD minus ~0.4 | 4.0 |

Toggle and encoder dimensions from your listings are already in.

## Knobs to turn

`tilt` (35°), `L`, `W`, `cham` (0 = plain box), `disp_dx / disp_zb / disp_zt` (screen triangle), `sw_pitch`, `enc_xz`, `sw_labels` (keep to 5 letters at 13 mm pitch), `labels_on`.
