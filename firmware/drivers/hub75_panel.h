#pragma once
// PIO-driven HUB75 panel. Owned and called exclusively by core 1.

#include <cstdint>

#include "hardware/pio.h"
#include "tw/graphics.h"
#include "tw/hub75_pack.h"

class Hub75Panel {
public:
    void init();
    // Converts a rendered framebuffer to bitplanes (only needed when it changes).
    void load(const tw::Framebuffer& fb);
    // Shifts out one complete frame: 16 scan rows x kColorBits planes.
    void scanFrame(uint8_t brightness);

private:
    PIO pio_ = pio0;
    uint smData_ = 0;
    uint smRow_ = 0;
    uint32_t baseCycles_ = 1;  // on-time of the least-significant bitplane
    tw::hub75::GammaTable gamma_{};
    tw::hub75::Bitplanes planes_{};
};
