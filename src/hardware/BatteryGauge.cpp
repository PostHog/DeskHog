#include "BatteryGauge.h"

#include <Wire.h>

namespace {

constexpr uint8_t kAddress = 0x36;
constexpr uint8_t kRegVcell = 0x02;   // 78.125 microvolts per unit
constexpr uint8_t kRegSoc = 0x04;     // 1/256 percent per unit
constexpr uint8_t kRegVersion = 0x08;
constexpr uint8_t kRegCrate = 0x16;   // Signed, 0.208 percent per hour per unit

constexpr uint32_t kBusTimeoutMs = 50;
constexpr uint32_t kEveryMs = 30000;
constexpr uint8_t kFailuresToLose = 3;
constexpr float kChargingAbovePerHour = 1.0f;

}  // namespace

void BatteryGauge::begin() {
    xTaskCreatePinnedToCore(taskEntry, "batteryTask", 3072, this, 1, nullptr, 0);
}

BatteryGauge::Reading BatteryGauge::reading() const {
    const uint32_t word = packed_.load();
    Reading r;
    r.found = word & 1u;
    r.charging = word & 2u;
    r.percent = (word >> 2) & 0x7F;
    r.volts = static_cast<float>((word >> 9) & 0xFFFF) / 1000.0f;
    return r;
}

void BatteryGauge::publish(const Reading& r) {
    const uint32_t millivolts = static_cast<uint32_t>(r.volts * 1000.0f + 0.5f) & 0xFFFF;
    packed_ = (r.found ? 1u : 0u) | (r.charging ? 2u : 0u) | (static_cast<uint32_t>(r.percent) << 2) |
              (millivolts << 9);
}

void BatteryGauge::taskEntry(void* self) { static_cast<BatteryGauge*>(self)->run(); }

void BatteryGauge::run() {
    // initVariant() of the board has already switched the power of the I2C bus
    // (and of the display) on. Setting the pin high again changes nothing; it
    // only keeps this working should that ever move.
    pinMode(TFT_I2C_POWER, OUTPUT);
    digitalWrite(TFT_I2C_POWER, HIGH);
    delay(10);  // The chip needs a moment after its power comes up

    Wire.begin();
    Wire.setTimeOut(kBusTimeoutMs);

    for (;;) {
        if (!poll()) {
            if (failures_ < kFailuresToLose) failures_++;
            if (failures_ >= kFailuresToLose) publish(Reading());
        }
        vTaskDelay(pdMS_TO_TICKS(kEveryMs));
    }
}

bool BatteryGauge::readRegister(uint8_t reg, uint16_t& value) {
    Wire.beginTransmission(kAddress);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(kAddress, static_cast<uint8_t>(2)) != 2) return false;
    const int high = Wire.read();
    const int low = Wire.read();
    if (high < 0 || low < 0) return false;
    value = static_cast<uint16_t>((high << 8) | low);
    return true;
}

// The version tells a MAX1704x from some other chip that answers at 0x36.
bool BatteryGauge::poll() {
    uint16_t version = 0, vcell = 0, soc = 0, crate = 0;
    if (!readRegister(kRegVersion, version) || (version & 0xFFF0) != 0x0010) return false;
    if (!readRegister(kRegVcell, vcell) || !readRegister(kRegSoc, soc) || !readRegister(kRegCrate, crate)) {
        return false;
    }

    Reading r;
    r.found = true;
    r.volts = vcell * 78.125e-6f;
    r.percent = soc >> 8;
    if (r.percent > 100) r.percent = 100;
    r.charging = static_cast<int16_t>(crate) * 0.208f > kChargingAbovePerHour;
    publish(r);
    failures_ = 0;
    return true;
}
