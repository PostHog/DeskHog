#pragma once

#include <stddef.h>
#include <stdint.h>

namespace deskhog {

/**
 * A short fingerprint of a plugin's source (FNV-1a, 32 bits), to tell whether
 * two copies of a plugin are the same text: the one on the device and the one
 * in a plugin directory. It is not a security check. The web page, the
 * command line tool and the directory's index all compute it the same way.
 */
inline uint32_t sourceHash(const char* data, size_t length) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; i++) {
        hash ^= static_cast<uint8_t>(data[i]);
        hash *= 16777619u;
    }
    return hash;
}

}  // namespace deskhog
