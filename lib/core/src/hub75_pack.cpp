#include "tw/hub75_pack.h"

#include <cmath>

namespace tw::hub75 {

GammaTable makeGammaTable(float gamma) {
    GammaTable t{};
    constexpr float kMax = (1 << kColorBits) - 1;
    for (int i = 0; i < 256; ++i) {
        const float v = std::pow(static_cast<float>(i) / 255.0f, gamma) * kMax;
        t[i] = static_cast<uint8_t>(std::lround(v));
    }
    return t;
}

void packBitplanes(const Framebuffer& fb, const GammaTable& gamma, Bitplanes& out) {
    for (int row = 0; row < kScanRows; ++row) {
        for (int x = 0; x < kWidth; ++x) {
            const Rgb top = fb.get(x, row);
            const Rgb bot = fb.get(x, row + kScanRows);
            const uint8_t lv[6] = {gamma[top.r], gamma[top.g], gamma[top.b],
                                   gamma[bot.r], gamma[bot.g], gamma[bot.b]};
            for (int p = 0; p < kColorBits; ++p) {
                uint8_t bits = 0;
                for (int ch = 0; ch < 6; ++ch) bits |= static_cast<uint8_t>(((lv[ch] >> p) & 1u) << ch);
                out.data[p][row][x] = bits;
            }
        }
    }
}

}  // namespace tw::hub75
