#pragma once
// Converts a framebuffer into the bitplanes the HUB75 PIO program shifts out.
//
// A 64x32 1/16-scan panel drives two rows at once: row r (top half) and
// row r + 16 (bottom half). Each byte is one column clock:
//   bit 0..2 = R0 G0 B0 (top), bit 3..5 = R1 G1 B1 (bottom)
// Colour depth comes from binary-coded modulation: plane p is displayed for
// 2^p time units, giving 2^kColorBits levels per channel.

#include <array>
#include <cstdint>

#include "tw/graphics.h"

namespace tw::hub75 {

constexpr int kWidth = Framebuffer::kWidth;
constexpr int kScanRows = Framebuffer::kHeight / 2;
constexpr int kColorBits = 4;

struct alignas(4) Bitplanes {
    // [plane][scan row][column]; each row is 64 bytes = 16 PIO FIFO words.
    uint8_t data[kColorBits][kScanRows][kWidth];
};

using GammaTable = std::array<uint8_t, 256>;  // 8-bit value -> 0..(2^kColorBits - 1)

GammaTable makeGammaTable(float gamma);
void packBitplanes(const Framebuffer& fb, const GammaTable& gamma, Bitplanes& out);

}  // namespace tw::hub75
