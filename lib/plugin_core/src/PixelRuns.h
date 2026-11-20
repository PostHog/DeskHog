#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace deskhog {

/**
 * Pack 16-bit pixels as runs of equal pixels: one byte for how many (1 to 255),
 * then the pixel's two bytes. A card's screen is mostly flat colour, so this
 * turns 63 KB into a few KB, which matters when it travels over the device's
 * WiFi every time a web page wants a picture of a card.
 *
 * @return false if packing would not make it smaller (a noisy picture). `packed`
 *         is then not usable.
 */
inline bool packPixelRuns(const std::vector<uint8_t>& raw, std::vector<uint8_t>& packed) {
    packed.clear();
    const size_t pixels = raw.size() / 2;
    size_t at = 0;
    while (at < pixels) {
        const uint8_t low = raw[at * 2], high = raw[at * 2 + 1];
        size_t run = 1;
        while (run < 255 && at + run < pixels && raw[(at + run) * 2] == low && raw[(at + run) * 2 + 1] == high) run++;
        if (packed.size() + 3 >= raw.size()) return false;
        packed.push_back(static_cast<uint8_t>(run));
        packed.push_back(low);
        packed.push_back(high);
        at += run;
    }
    return true;
}

}  // namespace deskhog
