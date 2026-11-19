#include "DeskManners.h"

#include <time.h>

#include "ConfigManager.h"
#include "ui/CardController.h"
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
    applied_ = settings_;
    rules_.setSettings(applied_);
    rotation_.setSeconds(applied_.rotateSeconds);

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

    // Both are called on the UI task, from PluginManager::tick().
    plugins_.setOnBanner([this](uint32_t forMs) { rules_.wake(millis(), forMs); });
    plugins_.setOnMovedByHand([this]() { rotation_.movedByHand(millis()); });
}

void DeskManners::setCardController(CardController* cards) {
    cards_ = cards;
    if (!cards_) return;
    if (cards_->getCardStack()) {
        cards_->getCardStack()->setOnMovedByHand([this]() { rotation_.movedByHand(millis()); });
    }
    if (cards_->getProvisioningCard()) {
        cards_->getProvisioningCard()->setOnCenter([this]() { toggleNight(); });
    }
}

void DeskManners::tick() {
    const uint32_t now = millis();

    // Never wait for the lock here: whoever holds it may be writing to flash.
    bool settingsChanged = false;
    if (dirty_ && xSemaphoreTake(lock_, 0) == pdTRUE) {
        applied_ = settings_;
        dirty_ = false;
        xSemaphoreGive(lock_);
        rules_.setSettings(applied_);
        rotation_.setSeconds(applied_.rotateSeconds);
        settingsChanged = true;
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
    showNightLine(levels, settingsChanged);
    rotate(levels.screen == 0, now);

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

void DeskManners::rotate(bool screenOff, uint32_t now) {
    if (!cards_) return;
    deskhog::Rotation::Input input;
    // Which cards take part only matters once the rotation is on; asking costs a lock per card.
    if (applied_.rotateSeconds > 0) input.rotates = cards_->rotationList();
    input.onScreen = cards_->currentIndex();
    input.playing = plugins_.attentionPlaying(now);
    input.banner = plugins_.attentionBanner();
    input.screenOff = screenOff;
    const int next = rotation_.tick(input, now);
    if (next >= 0) cards_->showIndex(next);
}

// The line on the status card while night or day mode is set by hand. It says
// until when: the next time the schedule changes the mode, which ends it.
void DeskManners::showNightLine(const deskhog::DeskRules::Levels& levels, bool settingsChanged) {
    const int mode = levels.manual ? (levels.night ? 2 : 1) : 0;
    if (!cards_ || !cards_->getProvisioningCard()) return;
    if (mode == nightLineMode_ && !(settingsChanged && mode != 0)) return;
    nightLineMode_ = mode;

    const bool schedule = applied_.nightEnabled && applied_.nightFrom != applied_.nightUntil;
    std::string text;
    if (mode == 2) {
        text = schedule ? "Night mode until " + deskhog::formatClock(applied_.nightUntil) : "Night mode on";
    } else if (mode == 1 && schedule) {
        text = "Day mode until " + deskhog::formatClock(applied_.nightFrom);
    }
    cards_->getProvisioningCard()->setNightLine(String(text.c_str()));
}

void DeskManners::fadeToward(int& milli, int target, int step) {
    if (milli < target) milli = milli + step < target ? milli + step : target;
    else if (milli > target) milli = milli - step > target ? milli - step : target;
}

bool DeskManners::buttonPressed(uint8_t button) {
    // The rules say whether the screen is meant to be dark; only a screen that
    // is dark already, not one still fading down, keeps the press for itself.
    const bool swallow = rules_.press(millis()) && screenShown_ == 0;
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
