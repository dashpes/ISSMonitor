# Firmware guide

This guide tells you how to build, flash, configure and test the ISS Monitor firmware. For the hardware, see [build.md](build.md). The [glossary](../README.md#glossary) in the README defines the technical terms.

## Contents

- [Architecture](#architecture)
- [Prepare the computer](#prepare-the-computer)
- [Flash the firmware](#flash-the-firmware)
- [Configuration](#configuration)
- [Unit tests](#unit-tests)
- [Hardware check sketches](#hardware-check-sketches)
- [Serial console](#serial-console)
- [Make the globe data again](#make-the-globe-data-again)
- [Troubleshooting](#troubleshooting)

## Architecture

The firmware is one PlatformIO project in `firmware/`. It uses the Arduino framework on an ESP32. The `esp32dev` environment builds it.

| Module | File | Function |
|---|---|---|
| Scheduler | `src/main.cpp` | One loop. Each subsystem runs when its interval is complete. No `delay()` after `setup()`. |
| State | `src/app/state.h` | `AppState`, the one structure that all screens read. Only the application layer writes to it. |
| Propagator | `src/app/propagator.cpp` | Loads the TLE into SGP4, calculates the position each second and starts the pass search. |
| Alert | `src/app/alert.cpp` | Controls the PASS LED with PWM. |
| Network clock | `src/net/netclock.cpp` | Wi-Fi and NTP as a state machine that does not block. |
| TLE download | `src/net/tle_fetch.cpp` | HTTPS download from Celestrak, checksum check and NVS cache. |
| Inputs | `src/io/inputs.cpp` | Switch debounce, and an interrupt routine for the knobs. |
| Light sensor | `src/io/lightsensor.cpp` | Smoothed ADC reading with a hysteresis band. |
| Serial console | `src/io/console.cpp` | Debug commands over the USB serial port. |
| Screens | `src/ui/*_screen.cpp` | The telemetry, globe and sky screens. |
| Rotated frame | `src/ui/rotated_frame.cpp` | Off-screen buffer that the firmware turns by 45° when it sends it to a display. |
| Orbit library | `lib/orbit/` | TLE parser, coordinate math, pass search and sun position. No Arduino code. |
| Pin map and settings | `include/config.h` | All pin numbers and settings. |
| Display setup | `include/display_config.h` | LovyanGFX setup for three GC9A01 displays on one SPI bus. |
| Colors | `include/theme.h` | The day palette and the night palette. |

### Data flow

```
Celestrak ──HTTPS──> tle_fetch ──> NVS cache
                        │
                        v
NTP ──> netclock ──> AppState.unix_now
                        │
                        v
              propagator (SGP4, 1 s) ──> position, look angles, speed
                        │
                        v
              orbit::findPasses (36 h, 60 s steps) ──> up to 4 passes
                        │
                        v
              AppState ──> telemetry, globe, sky screens (100 ms)
                       └─> alert (PASS LED)
```

### Memory

The ESP32 in this project has no PSRAM. A full 240 × 240 image at 16 bits per pixel needs 115 kB. That is too much next to the Wi-Fi stack. Thus each of the three screens draws into a 4-bit palette buffer of 28.8 kB. The 4-bit palette also keeps enough memory free for the TLS connection to Celestrak. If a buffer allocation fails, the screen draws directly to the display and the serial monitor shows a message.

### Pass search

The pass search is in `lib/orbit/src/passes.cpp`. It does not use the pass function of the Sgp4 library. The firmware gives the search a function that returns the ISS position at a given time. On the ESP32 that function uses SGP4. In the unit tests it uses a satellite on a known circle. Thus the tests can compare the results with exact geometry.

AOS and LOS in this project are the times when the ISS crosses 10° elevation, not 0°. A good pass therefore shows as approximately 400 seconds. Other websites that use the horizon give a longer time for the same pass.

## Prepare the computer

1. Install PlatformIO Core (`brew install platformio` or `pipx install platformio`), or install the PlatformIO IDE extension for VS Code.
2. Clone the repository.
3. Open a terminal in the repository root.
4. Run `./scripts/vendor_libs.sh`.

The script copies LovyanGFX and Sgp4 from GitHub into `firmware/lib/`. These libraries are not in the repository because LovyanGFX includes approximately 126 MB of font tables. PlatformIO gets ArduinoJson from its registry on the first build.

If you use VS Code, open the `firmware` folder, not the repository root. PlatformIO looks for `platformio.ini` in the open folder. The first build downloads the ESP32 toolchain, which takes some minutes.

## Flash the firmware

> **CAUTION:** Do not put your Wi-Fi password in `secrets_example.h` or in any other file that git tracks. Only `secrets.h` is in `.gitignore`.

1. Go to the `firmware` folder.
2. Copy `src/secrets_example.h` to `src/secrets.h`.
3. Open `src/secrets.h`.
4. Set `WIFI_SSID` to the name of your Wi-Fi network.
5. Set `WIFI_PASS` to the password of your Wi-Fi network.
6. Set your location and time zone in `include/config.h`. See [Configuration](#configuration).
7. Connect the ESP32 to the computer with a USB-C data cable.
8. Run `pio run -t upload -t monitor`.
9. Make sure that the serial monitor shows the `=== ISS Monitor ===` line and the reset reason.
10. Wait for the `[net] wifi up`, `[net] time synced` and `[tle] fetched` lines.
11. Make sure that the STATUS mode on the left screen shows TRACKING.

The `secrets.h` file has two fields:

| Field | Type | Contents |
|---|---|---|
| `WIFI_SSID` | `constexpr char[]` | The name of the Wi-Fi network. The ESP32 uses 2.4 GHz only. |
| `WIFI_PASS` | `constexpr char[]` | The password of the Wi-Fi network. |

The start-up log contains lines of these types. The values change. On the first start, there is no cached TLE, so the first two lines do not show.

```
=== ISS Monitor ===  reset: POWERON  heap: <bytes>
[tle] loaded cached element set: ISS (ZARYA)
[prop] load ISS (ZARYA) -> ok (<n> rev/day)
[net] wifi up, ip <address>, rssi <dBm>
[net] time synced: <unix time>
[tle] fetched ISS (ZARYA) (epoch <year> day <day>)
[prop] <n> passes in the next 36 h, found in <ms> ms
```

The serial monitor runs at 115200 baud. The upload runs at 921600 baud.

## Configuration

All settings are in `firmware/include/config.h`. Each setting is a `constexpr` in the `cfg` namespace. For the pin numbers, see the [pin map](build.md#pin-map).

### Location and network

| Setting | Default | Function |
|---|---|---|
| `OBS_LAT_DEG` | 33.746 | Observer latitude in degrees, north positive. Set it to your location. |
| `OBS_LON_DEG` | −117.826 | Observer longitude in degrees, east positive. Set it to your location. |
| `OBS_ALT_M` | 35.0 | Observer height above sea level in meters. |
| `TZ_STRING` | `PST8PDT,M3.2.0,M11.1.0` | POSIX time zone string for the system clock. |
| `TLE_URL` | Celestrak, catalog number 25544 | The source of the TLE. 25544 is the ISS. |
| `NTP_1`, `NTP_2` | `pool.ntp.org`, `time.nist.gov` | Time servers. |

### Timing

| Setting | Default | Function |
|---|---|---|
| `MS_PROPAGATE` | 1000 ms | Interval between position calculations. |
| `MS_REDRAW` | 100 ms | Interval between screen updates. |
| `MS_INPUT_POLL` | 5 ms | Interval between switch reads. |
| `MS_DEBOUNCE` | 25 ms | Time that a switch must stay stable before the firmware accepts the change. |
| `MS_LDR_SAMPLE` | 100 ms | Interval between light sensor reads. |
| `MS_TLE_REFRESH` | 6 h | Interval between TLE downloads. |

### Pass prediction and alert

| Setting | Default | Function |
|---|---|---|
| `PASS_MIN_ELEVATION_DEG` | 10.0 | Elevation that defines AOS and LOS. |
| `PASS_SEARCH_HORIZON_S` | 36 h | How far ahead the pass search looks. |
| `PASS_COARSE_STEP_S` | 60 s | Step of the pass search. A pass lasts approximately six minutes. A step of 600 s misses passes, and a unit test shows this. |
| `ALERT_LEAD_SECONDS` | 300 s | Time before AOS when the PASS LED starts to pulse. |
| `MORSE_BEACON`, `MORSE_UNIT_MS` | `CQ DE NA1SS`, 120 ms | Not used yet. No code reads these values. |

### Light sensor

| Setting | Default | Function |
|---|---|---|
| `LDR_NIGHT_THRESHOLD` | 800 | ADC reading below which the console changes to the night palette. **TODO (owner):** record the calibrated value for the finished unit. |
| `LDR_HYSTERESIS` | 150 | The console changes back to the day palette only above threshold + hysteresis (950). This stops the palette changes on ADC noise. |
| `LDR_EMA_SHIFT` | 3 | Smoothing. Each new reading moves the average by 1/8 of the difference. The time constant is approximately 0.8 s. |

To calibrate the threshold:

1. Flash the `inputs` sketch.
2. Read the LDR value in normal room light.
3. Read the LDR value with the room light off.
4. Set `LDR_NIGHT_THRESHOLD` to a value between the two readings.

### Display

| Setting | Default | Function |
|---|---|---|
| `ROT_TELEMETRY_DEG` | −135 | Rotation of the left image, clockwise as seen from the front. |
| `ROT_GLOBE_DEG` | 90 | Rotation of the top image. |
| `ROT_SKY_DEG` | 135 | Rotation of the right image. |
| `SPI_HZ` | 40 MHz | SPI clock. Decrease it to 27 MHz if the screens show errors. |

A rotation in steps of 90° costs no time. Any other angle needs a full off-screen buffer and a software rotation on each update.

### Build flags

These flags are in `platformio.ini`. You can also set them for one build with the `PLATFORMIO_BUILD_FLAGS` environment variable.

| Flag | Default | Function |
|---|---|---|
| `HAVE_ENCODER` | 1 | 0 removes the knob code. The serial keys 1–4 then select the modes. |
| `HAVE_LDR` | 1 | 0 removes the light sensor code. The serial key `d` then sets the dim state. |
| `HAVE_AUX_SWITCHES` | 1 | 0 removes the AUX switch code. |
| `GLOBE_PROFILE` | 0 | 1 prints the frame time of the globe once each second. |

To measure the globe frame time, run `PLATFORMIO_BUILD_FLAGS=-DGLOBE_PROFILE=1 pio run -t upload -t monitor`.

### Colors

`include/theme.h` has the two palettes as RGB values. The day palette is amber on black. The night palette is dark red on black. You can change the values. Use the `rgb565()` function.

## Unit tests

The unit tests do a check of the orbit library and the land mask. They run on the computer and need no hardware.

| Suite | Directory | Tests | Subject |
|---|---|---|---|
| `test_geo` | `test/test_geo/` | 24 | Look angles, ground distance, globe projection and compass points. |
| `test_globe` | `test/test_globe/` | 5 | Land mask at known places, Antarctica, and the total land fraction. |
| `test_passes` | `test/test_passes/` | 25 | Pass search against a satellite on a known circle, and the sun position. |
| `test_tle` | `test/test_tle/` | 10 | TLE parser, checksum and epoch. |

To run the tests with PlatformIO:

1. Go to the `firmware` folder.
2. Run `pio test -e native`.
3. Make sure that the summary shows `64 test cases: 64 succeeded`.

To run the tests without PlatformIO:

1. Go to the `firmware` folder.
2. Run `./scripts/run_tests.sh`.
3. Make sure that the last line shows `ALL SUITES PASSED`.

`run_tests.sh` uses the system C++ compiler and the copy of Unity in `test/unity_vendor/`. It needs no network connection and finishes in approximately one second. To run one suite, give part of its name, for example `./scripts/run_tests.sh passes`.

> **CAUTION:** Put each new test suite in its own directory under `test/`. PlatformIO links all files in one directory into one program. Two files with a `main()` in one directory cause a link error.

To make sure that all firmware code compiles, run `pio run -e allhw`. This environment builds with all `HAVE_*` flags set to 1.

## Hardware check sketches

Each sketch in `firmware/hw_check/` has its own PlatformIO environment. Each sketch does a check of one part of the hardware. To flash a sketch, run `pio run -e <environment> -t upload -t monitor` in the `firmware` folder.

| Environment | File | What it does | Correct result |
|---|---|---|---|
| `led` | `led_only.cpp` | Blinks the LED five times, then fades it. Prints a heartbeat. | The LED blinks and fades. The serial monitor shows the heartbeat. |
| `smoketest` | `smoketest.cpp` | Prints the reset reason. Starts each display alone, then all three. Prints the switch states. Changes the LED brightness. | Left red TELEMETRY, center green GLOBE, right blue SKY, one at a time, then all three together. |
| `inputs` | `inputs_check.cpp` | Prints each debounced switch change, each knob step and each knob push. Prints the LDR value. | One line for each switch change. One count for each knob step. |
| `data` | `data_check.cpp` | Connects to Wi-Fi, gets the time and the TLE, and prints the ISS position each second. Uses no displays. Needs `secrets.h`. | A Wi-Fi line, a time line, a TLE line, then one position line each second. |
| `probe` | `pin_probe.cpp` | Drives each display signal pin with a different PWM duty cycle. | A multimeter on DC volts at each display pin shows the voltage that the serial monitor gives for that pin. |
| `leak` | `leak_check.cpp` | Sets D32 low and reads the left knob CLK and DT pins on the ADC. | CLK and DT stay near 3.3 V. A lower voltage shows a partial short to D32. |

Use the sketches in this sequence: `led`, `smoketest`, `inputs`, `data`. Use `probe` and `leak` only to find a fault.

## Serial console

The main firmware reads one-character commands from the serial monitor.

| Key | Function |
|---|---|
| `1`, `2` | Left screen: previous mode, next mode. |
| `3`, `4` | Right screen: previous mode, next mode. |
| `n`, `u`, `a`, `t` | Report the NIGHT, UNITS, ALERT or TRACK switch state. The switches control these values, so the keys cannot change them. |
| `d` | With `HAVE_LDR=0`: change the dim state. With the light sensor: report the LDR value. |
| `j` | Add 60 seconds to the time offset. Use this key to see a pass without a wait. |
| `J` | Set the time offset to zero. |
| `?` | Print the full state: modes, switches, light, position, passes, time and free memory. |

## Make the globe data again

The globe uses two generated files: `src/ui/globe_data.h` (coastlines, graticule, equator) and `src/ui/land_mask.h` (land and sea table). The script `scripts/gen_globe_data.py` makes both files and `src/ui/globe_preview.png` from Natural Earth 1:110m land data.

> **CAUTION:** Do not edit `globe_data.h` or `land_mask.h` by hand. Run the script again.

1. Make sure that Python 3 is installed.
2. Run `./scripts/gen_globe_data.py` from the repository root.
3. Run the unit tests.
4. Make sure that `globe_preview.png` shows the correct continents.

The first run downloads the Natural Earth data to `.cache/`. Later runs use the cache. Use `--refresh` to download it again. Use `--tolerance-px` to change the detail level and `--view LAT,LON` to change the preview position.

If you change `GLOBE_R` in `src/ui/globe_screen.cpp`, change `GLOBE_R_PX` in the script to the same value.

## Troubleshooting

These problems occurred during the development of the first unit.

### The computer does not find a serial port

1. Use a different USB-C cable. Many cables carry power only.
2. Install the CH340 or the CP2102 driver. Use the driver for the small chip near the USB port of the board.
3. Flash the `led` sketch to do a check of the toolchain and the cable.

### The board restarts and the log shows BROWNOUT

```
=== ISS Monitor ===  reset: BROWNOUT -- check C1 / the USB supply
```

The voltage decreased when Wi-Fi started.

1. Make sure that C1 is installed between 3V3 and GND.
2. Make sure that the stripe on C1 connects to GND.
3. Use a different USB cable or power supply.

### One drawing shows on more than one screen, or only the last screen operates

All three displays share the reset line, and each chip select floats until the display starts. The function `prepareDisplayBus()` in `display_config.h` sets all CS lines high and resets the displays one time. If the fault occurs again, do a check of the CS wires with the `smoketest` sketch.

### A screen stays blank

1. Flash the `smoketest` sketch.
2. If the screen stays blank, flash the `probe` sketch.
3. Measure the DC voltage at each pin of the display header.
4. Compare each voltage with the value on the serial monitor.

A voltage that agrees shows a good joint. A voltage for a different pin shows a wrong wire. 0 V or an unstable voltage shows an open joint. A voltage between two values shows a short to the adjacent pin.

### The colors are wrong or the image is not in the center

Red and blue change places if `rgb_order` is wrong in `display_config.h`. A cut or shifted circle shows that `offset_x` or `offset_y` is wrong. Use the `smoketest` sketch to find these faults.

### The NIGHT state changes when the left knob turns

A partial short between D32 and an adjacent pin causes this fault. A continuity test does not find a resistance of tens of kilohms.

1. Turn off the NIGHT switch.
2. Flash the `leak` sketch.
3. Do not touch the left knob during the test.
4. Read the CLK and DT voltages on the serial monitor.
5. If a voltage is less than 3.3 V, clean the flux and solder between D32 and the adjacent pins.

### A knob counts in the wrong direction

The CLK and DT wires are in the opposite order. Change the two pin numbers for that knob in `config.h`. Do not change the wires.

### A knob gives two counts or no count for each step

1. Flash the `inputs` sketch.
2. Turn the knob one step clockwise.
3. Make sure that the total increases by one.
4. If it does not, do a check of the CLK, DT and `+` wires.

### A lower screen is 45° from upright

The off-screen buffer for that screen did not get enough memory. The serial monitor shows this message:

```
[TELEMETRY] frame alloc FAILED -- drawing direct, 45 degrees off
```

Remove the change that uses more memory. Each buffer needs 28.8 kB in one block. Use the `?` key to see the largest free block.

### The console shows OFFLINE or old data

The STATUS mode on the left screen shows the link state and the TLE age. The console continues with the cached TLE when the network is not available.

- `[net] wifi association timed out`: make sure that `secrets.h` has the correct name and password, and that the network uses 2.4 GHz.
- `[tle] HTTP <code>`: Celestrak did not supply the TLE. The firmware tries again after 30 seconds.
- `[tle] parse/checksum failed; keeping cached set`: the download was not complete. The firmware keeps the old TLE.
- `[prop] element set unchanged, keeping current fit`: this is correct. Celestrak has no newer TLE.

A TLE that is more than approximately 72 hours old gives a less accurate position.

### The screens show errors or noise

Decrease `SPI_HZ` in `config.h` from 40 MHz to 27 MHz.

### The LED is too dim

Put a second 150 Ω resistor in parallel with R2. Do not use less than 47 Ω.

### The pass times are shorter than on other websites

This is correct. The firmware starts and stops a pass at 10° elevation, not at the horizon. Change `PASS_MIN_ELEVATION_DEG` to change this.

### The flash memory is almost full

The `esp32dev` build uses 87.9% of the 1.25 MB (1,310,720 bytes) application partition, and 19.3% of the RAM. Keep this limit in mind before you add a feature.
