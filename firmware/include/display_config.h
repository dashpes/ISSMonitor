#pragma once
//
// display_config.h -- LovyanGFX setup for three GC9A01 panels on one SPI bus.
//
// Pure boilerplate, but three things here are load-bearing:
//
//  1. All three panels share SCK/MOSI/DC/RST and differ ONLY in chip select.
//     SPI works because exactly one CS is low at a time; LovyanGFX drives CS
//     around every transaction for you.
//  2. Each panel gets its own Bus_SPI instance pointed at the SAME
//     VSPI_HOST. LovyanGFX reference-counts the host, so this is not three
//     buses -- it is three views of one.
//  3. `panel_cfg.offset_rotation` and the 240x240 GC9A01 quirks are already
//     set. If your panels show a shifted image, that is the knob.
//
// C++ note: this is one of the few places a macro-free template would be
// over-engineering. A small class with a constructor parameter is clearer.
//
// LGFX_USE_V1 is set in platformio.ini build_flags -- defining it here too
// just produces a "redefined" warning on every translation unit.
#include <LovyanGFX.hpp>

#include "config.h"

class RoundPanel : public lgfx::LGFX_Device {
  public:
    explicit RoundPanel(uint8_t cs_pin) {
        {
            auto c = _bus.config();
            c.spi_host   = VSPI_HOST;
            c.spi_mode   = 0;
            c.freq_write = cfg::SPI_HZ;
            c.freq_read  = 16'000'000;
            c.spi_3wire  = true;
            c.use_lock   = true;          // required: three devices, one bus
            c.dma_channel = SPI_DMA_CH_AUTO;
            c.pin_sclk   = cfg::PIN_SCK;
            c.pin_mosi   = cfg::PIN_MOSI;
            c.pin_miso   = -1;            // these panels are write-only
            c.pin_dc     = cfg::PIN_DC;
            _bus.config(c);
            _panel.setBus(&_bus);
        }
        {
            auto c = _panel.config();
            c.pin_cs   = cs_pin;
            // -1 on purpose. RST is SHARED by all three panels, so if each
            // LGFX_Device owned it, every init() would reset the panels that
            // were already brought up -- leaving only the last one alive.
            // The reset is pulsed ONCE, by hand, before any init(). See
            // prepareDisplayBus() below.
            c.pin_rst  = -1;
            c.pin_busy = -1;
            c.panel_width   = cfg::SCREEN_W;
            c.panel_height  = cfg::SCREEN_H;
            c.memory_width  = cfg::SCREEN_W;
            c.memory_height = cfg::SCREEN_H;
            c.offset_x = 0;
            c.offset_y = 0;
            c.offset_rotation = 0;
            c.dummy_read_pixel = 8;
            c.dummy_read_bits  = 1;
            c.readable   = false;
            c.invert     = true;          // GC9A01 is an inverting panel
            c.rgb_order  = false;
            c.dlen_16bit = false;
            c.bus_shared = true;          // <-- the important one
            _panel.config(c);
        }
        setPanel(&_panel);
    }

  private:
    lgfx::Bus_SPI       _bus;
    lgfx::Panel_GC9A01  _panel;
};

// Prepare the shared bus. MUST run before the first RoundPanel::init().
//
// Two things, and both are load-bearing:
//
//  1. Drive EVERY chip select HIGH first. LovyanGFX only configures a panel's
//     CS pin inside that panel's init(), so until then the ESP32 pin is a
//     floating input -- and CS is active-LOW, so a floating CS means that
//     panel is SELECTED and listening to traffic meant for its neighbours.
//     The visible symptom is unmistakable: bring up three panels in a loop
//     and the first draw appears on all three, the second on two, the third
//     on one. Deselect them all up front and it goes away.
//
//  2. Pulse the shared RST once, here, for all three at once -- which is why
//     every panel is configured with pin_rst = -1 above. If each panel owned
//     the shared reset line, init() on panel N would reset panels 1..N-1 and
//     you would finish setup with only the last panel alive.
//
inline void prepareDisplayBus() {
    const uint8_t cs_pins[] = {
        cfg::PIN_CS_TELEMETRY, cfg::PIN_CS_GLOBE, cfg::PIN_CS_SKY};
    for (uint8_t pin : cs_pins) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);   // HIGH == deselected
    }

    pinMode(cfg::PIN_RST, OUTPUT);
    digitalWrite(cfg::PIN_RST, LOW);
    delay(20);
    digitalWrite(cfg::PIN_RST, HIGH);
    delay(150);   // GC9A01 needs ~120 ms after reset before it accepts commands
}

// Defined in src/main.cpp.
extern RoundPanel tft_telemetry;
extern RoundPanel tft_globe;
extern RoundPanel tft_sky;
