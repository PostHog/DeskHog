#pragma once

#include <Arduino.h>

/**
 * @class CpuMonitor
 * @brief Self-contained estimator for the DeskHog's own CPU load (0-100%).
 *
 * Spawns one low-priority "idle observer" task per core. Each task spins a
 * volatile counter at idle priority, so it only advances when nothing more
 * important is running on that core. The faster the counter climbs, the more
 * idle the core is; the slower it climbs, the busier the core is.
 *
 * The estimator is self-calibrating: it tracks the fastest count rate it has
 * ever observed as "fully idle" (100% free), so it does not depend on a clean
 * boot-time measurement. Load is reported as the average across both cores.
 *
 * Reading the load is cheap and lock-free (a plain read of volatile counters),
 * so it is safe to call from the UI task inside a card's update() loop.
 *
 * NOTE: this drives a fun desk-ornament animation, not a precise profiler.
 * The idle-observer approach is a well-known ESP32 approximation, good to
 * within a few percent - plenty for making a hedgehog run faster.
 */
class CpuMonitor {
public:
    CpuMonitor();

    /**
     * @brief Start the per-core idle-observer tasks. Call once from setup().
     */
    void begin();

    /**
     * @brief Current estimated CPU load, 0-100 (smoothed).
     *
     * Computes the count rate since the previous call, updates the observed
     * per-core maximum, and returns 100 * (1 - rate / max) averaged across
     * cores. Intended to be polled a few times per second.
     */
    uint8_t getLoadPercent();

private:
    static constexpr int kCores = 2;

    static void observerTask(void* arg);

    volatile uint32_t _counters[kCores];  ///< Per-core spin counters
    uint32_t _lastCounts[kCores];         ///< Counter snapshot at last sample
    float _maxRate[kCores];               ///< Fastest observed counts/ms per core
    uint32_t _lastSampleMs;               ///< millis() at last sample
    float _smoothed;                      ///< Exponentially smoothed load %
    bool _started;
};
