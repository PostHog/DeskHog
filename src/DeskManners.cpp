#include "DeskManners.h"

#include <time.h>

#include "ConfigManager.h"
#include "hardware/DisplayInterface.h"
#include "hardware/Input.h"
#include "hardware/NeoPixelController.h"
#include "plugins/PluginManager.h"

namespace {

// Before the clock is set from the network the device counts from 1970.
constexpr time_t kClockSet = 1577836800;  // 2020-01-01

int64_t epochNow() {
    const time_t now = time(nullptr);
    return now >= kClockSet ? static_cast<int64_t>(now) : 0;
}

// The time zone is looked up once a second rather than on every loop.
constexpr uint32_t kZoneEveryMs = 1000;

}  // namespace

DeskManners::DeskManners(ConfigManager& config, DisplayInterface& display, NeoPixelController& led,
                         PluginManager& plugins)
    : config_(config), display_(display), led_(led), plugins_(plugins) {}

void DeskManners::begin() {
    lock_ = xSemaphoreCreateMutex();
    settings_ = config_.getDeviceSettings();
    rules_.setSettings(settings_);

    const uint32_t now = millis();
    zone_ = plugins_.timeZone();
    zoneAtMs_ = now;
    lastTickMs_ = now;
    const deskhog::DeskRules::Levels levels = rules_.tick(epochNow(), zone_, now);
    night_ = levels.night;
    manual_ = levels.manual;
    screenMilli_ = levels.screen * 1000;
    ledMilli_ = levels.led * 1000;
    screenShown_ = levels.screen;
    ledShown_ = levels.led;
    display_.setBrightness(levels.screen);
    led_.setLevel(levels.led);
}

void DeskManners::tick() {
    const uint32_t now = millis();

    // Never wait for the lock here: whoever holds it may be writing to flash.
    if (dirty_ && xSemaphoreTake(lock_, 0) == pdTRUE) {
        const deskhog::DeviceSettings settings = settings_;
        dirty_ = false;
        xSemaphoreGive(lock_);
        rules_.setSettings(settings);
    }
    const int preview = previewWanted_.exchange(0);
    if (preview) rules_.preview(preview == 2, now);

    if (now - zoneAtMs_ >= kZoneEveryMs) {
        zone_ = plugins_.timeZone();
        zoneAtMs_ = now;
    }
    const deskhog::DeskRules::Levels levels = rules_.tick(epochNow(), zone_, now);
    night_ = levels.night;
    manual_ = levels.manual;

    for (uint8_t b = 0; b <= Input::BUTTON_UP; b++) {
        if (!buttons[b].isPressed()) swallowed_ &= ~(1u << b);
    }

    uint32_t elapsed = now - lastTickMs_;
    lastTickMs_ = now;
    if (elapsed > kFadeMs) elapsed = kFadeMs;
    const int step = static_cast<int>(elapsed * 100000u / kFadeMs);
    fadeToward(screenMilli_, levels.screen * 1000, step);
    fadeToward(ledMilli_, levels.led * 1000, step);

    const int screen = (screenMilli_ + 500) / 1000;
    if (screen != screenShown_) {
        screenShown_ = screen;
        display_.setBrightness(screen);
    }
    const int led = (ledMilli_ + 500) / 1000;
    if (led != ledShown_) {
        ledShown_ = led;
        led_.setLevel(led);
    }
}

void DeskManners::fadeToward(int& milli, int target, int step) {
    if (milli < target) milli = milli + step < target ? milli + step : target;
    else if (milli > target) milli = milli - step > target ? milli - step : target;
}

bool DeskManners::buttonPressed(uint8_t button) {
    const bool swallow = rules_.press(millis());
    if (button <= Input::BUTTON_UP) {
        if (swallow) swallowed_ |= 1u << button;
        else swallowed_ &= ~(1u << button);
    }
    return swallow;
}

bool DeskManners::swallowing(uint8_t button) const {
    return button <= Input::BUTTON_UP && (swallowed_ & (1u << button)) && buttons[button].isPressed();
}

void DeskManners::toggleNight() { rules_.toggleNight(); }

deskhog::DeviceSettings DeskManners::settings() {
    xSemaphoreTake(lock_, portMAX_DELAY);
    const deskhog::DeviceSettings settings = settings_;
    xSemaphoreGive(lock_);
    return settings;
}

bool DeskManners::change(const std::vector<std::pair<std::string, std::string>>& changes, std::string& error) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    deskhog::DeviceSettings next = settings_;
    const bool ok = deskhog::applyDeviceSettings(next, changes, error);
    if (ok) {
        config_.saveDeviceSettings(next);
        settings_ = next;
        dirty_ = true;
    }
    xSemaphoreGive(lock_);
    return ok;
}

void DeskManners::preview(bool night) { previewWanted_ = night ? 2 : 1; }

DeskManners::Status DeskManners::status() { return Status{night_, manual_}; }
