#pragma once

#include <Arduino.h>

#include <atomic>

/**
 * The battery fuel gauge on the Feather: a MAX17048 on the I2C bus, read on a
 * task of its own so that a slow or missing chip never holds up the UI.
 *
 * A board without a battery still has the chip, but a board without the chip
 * (or with the bus dead) reads as "not found". Nothing here blocks for more
 * than the 50 ms bus timeout, and nothing here touches LVGL.
 */
class BatteryGauge {
public:
    struct Reading {
        bool found = false;
        int percent = 0;       ///< 0 to 100
        float volts = 0;
        bool charging = false;
    };

    /** Start the reading task (core 0). Call once in setup(), after the display. */
    void begin();
    /** The latest reading. Safe from any task. */
    Reading reading() const;

private:
    static void taskEntry(void* self);
    void run();
    bool readRegister(uint8_t reg, uint16_t& value);
    bool poll();
    void publish(const Reading& reading);

    // One word, so that a reader never sees half of one reading and half of the next.
    // Bit 0 found, bit 1 charging, bits 2-8 percent, bits 9-24 millivolts.
    std::atomic<uint32_t> packed_{0};
    uint8_t failures_ = 0;  ///< Reading task only
};
