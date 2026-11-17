#include "drivers/hub75_panel.h"

#include <cstring>

#include "config.h"
#include "hardware/clocks.h"
#include "hub75.pio.h"

using namespace tw::hub75;

void Hub75Panel::init() {
    gamma_ = makeGammaTable(cfg::PANEL_GAMMA);

    const uint32_t sysHz = clock_get_hz(clk_sys);
    // Two PIO instructions per column clock.
    const float clkdiv = static_cast<float>(sysHz) / (2.0f * cfg::PANEL_PIXEL_CLOCK_HZ);

    const uint rowOffset = pio_add_program(pio_, &hub75_row_program);
    const uint dataOffset = pio_add_program(pio_, &hub75_data_program);
    smRow_ = static_cast<uint>(pio_claim_unused_sm(pio_, true));
    smData_ = static_cast<uint>(pio_claim_unused_sm(pio_, true));
    hub75_row_program_init(pio_, smRow_, rowOffset, cfg::PIN_ROW_BASE, cfg::PIN_LATCH);
    hub75_data_program_init(pio_, smData_, dataOffset, cfg::PIN_RGB_BASE, cfg::PIN_CLK, clkdiv);

    // Size the BCM time unit so a full-brightness frame is lit for ~75% of the
    // frame period; the remainder covers shifting/latching overhead and keeps
    // the refresh locked to 100 Hz.
    const uint64_t frameCycles = uint64_t{sysHz} * cfg::FRAME_PERIOD_US / 1'000'000;
    const uint32_t unitsPerFrame = kScanRows * ((1u << kColorBits) - 1);
    baseCycles_ = static_cast<uint32_t>(frameCycles * 3 / 4 / unitsPerFrame);
}

void Hub75Panel::load(const tw::Framebuffer& fb) { packBitplanes(fb, gamma_, planes_); }

void Hub75Panel::scanFrame(uint8_t brightness) {
    const uint32_t unit = (baseCycles_ * brightness) / 255;

    for (uint32_t row = 0; row < kScanRows; ++row) {
        for (uint32_t plane = 0; plane < kColorBits; ++plane) {
            const uint8_t* bytes = planes_.data[plane][row];
            for (int i = 0; i < kWidth; i += 4) {
                uint32_t word;
                std::memcpy(&word, bytes + i, sizeof(word));
                pio_sm_put_blocking(pio_, smData_, word);
            }
            // Wait for the column data to finish shifting and the previous
            // row's on-time to elapse, then latch and light this one.
            hub75_wait_tx_stall(pio_, smData_);
            hub75_wait_tx_stall(pio_, smRow_);

            uint32_t onCycles = unit << plane;
            if (onCycles == 0) onCycles = 1;
            pio_sm_put_blocking(pio_, smRow_, row | ((onCycles - 1) << 4));
        }
    }
}
