#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <string>
#include <utility>
#include <vector>

#include "DeskRules.h"
#include "PluginHost.h"
#include "hardware/BatteryGauge.h"

class CardController;
class ConfigManager;
class DisplayInterface;
class NeoPixelController;
class PluginManager;

/**
 * How the device itself behaves: how bright the screen and the LED are, by day
 * and at night, what a button press at night does, and moving on to the next
 * card by itself. The rules are in DeskRules.h (lib/plugin_core); this class
 * keeps the settings and applies the rules' answers to the hardware and the
 * card stack.
 *
 * The UI task owns the rules and calls tick() every loop. Other tasks (the web
 * server) change the settings under a lock; tick() picks them up.
 */
class DeskManners {
public:
    /// A level change from 0 to 100 fades over this long; smaller ones are quicker.
    static constexpr uint32_t kFadeMs = 500;

    DeskManners(ConfigManager& config, DisplayInterface& display, NeoPixelController& led, PluginManager& plugins);

    /** Load the settings and light the screen and LED at their level at once. Call in setup(). */
    void begin();
    /** The card stack to rotate, and the status card's night switch. Call in setup(), after it is initialized. */
    void setCardController(CardController* cards);
    /** The battery gauge, for the status card and for plugins. Call in setup(). */
    void setBatteryGauge(BatteryGauge* gauge) { gauge_ = gauge; }

    // ---- UI task only -------------------------------------------------

    /** Pick up changed settings, run the rules and fade toward their levels. */
    void tick();
    /** A button went down. True if the press only woke a dark screen: it goes no further. */
    bool buttonPressed(uint8_t button);
    /** True while a press that buttonPressed() swallowed is still held: plugins must not see it. */
    bool swallowing(uint8_t button) const;
    /** Night mode on or off by hand. */
    void toggleNight();

    // ---- Safe from any task -------------------------------------------

    deskhog::DeviceSettings settings();
    /** Validate, save and apply. All or nothing: on false, `error` says why and nothing changed. */
    bool change(const std::vector<std::pair<std::string, std::string>>& changes, std::string& error);
    /** Show the night (or day) levels for a few seconds, while a slider on the web page moves. */
    void preview(bool night);
    struct Status { bool night; bool manual; };
    Status status();
    /** The battery for plugins. False unless this device has a battery and the gauge was found. Never waits. */
    bool battery(deskhog::BatteryStatus& status);
    /** What the gauge reads, whether or not this device is set to have a battery. */
    BatteryGauge::Reading gauge();

private:
    static void fadeToward(int& milli, int target, int step);
    void rotate(bool screenOff, uint32_t now);
    void showNightLine(const deskhog::DeskRules::Levels& levels, bool settingsChanged);
    void showBatteryLine(uint32_t now, bool settingsChanged);

    ConfigManager& config_;
    DisplayInterface& display_;
    NeoPixelController& led_;
    PluginManager& plugins_;

    // Shared with other tasks
    SemaphoreHandle_t lock_ = nullptr;
    deskhog::DeviceSettings settings_;      ///< Under lock_
    std::atomic<bool> dirty_{false};        ///< settings_ changed since the UI task took them
    std::atomic<int> previewWanted_{0};     ///< 0 none, 1 day, 2 night
    std::atomic<bool> night_{false};
    std::atomic<bool> manual_{false};
    std::atomic<bool> hasBattery_{false};   ///< settings_.battery, readable without the lock
    BatteryGauge* gauge_ = nullptr;

    // UI task only
    deskhog::DeskRules rules_;
    deskhog::Rotation rotation_;
    deskhog::DeviceSettings applied_;  ///< The settings the rules have
    CardController* cards_ = nullptr;
    int nightLineMode_ = 0;  ///< 0 no line, 1 day by hand, 2 night by hand
    std::string batteryText_;  ///< What the status card shows now; "" is hidden
    bool batteryShown_ = false;  ///< batteryText_ has been handed to the card at least once
    uint32_t batteryAtMs_ = 0;
    std::string zone_;
    uint32_t zoneAtMs_ = 0;
    uint32_t lastTickMs_ = 0;
    int screenMilli_ = 0, ledMilli_ = 0;  ///< Applied levels, in thousandths of a percent, mid-fade
    int screenShown_ = -1, ledShown_ = -1;
    uint8_t swallowed_ = 0;  ///< One bit per button
};
