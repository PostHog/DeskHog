#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <string>
#include <utility>
#include <vector>

#include "DeskRules.h"

class ConfigManager;
class DisplayInterface;
class NeoPixelController;
class PluginManager;

/**
 * How the device itself behaves: how bright the screen and the LED are, by day
 * and at night, and what a button press at night does. The rules are in
 * DeskRules.h (lib/plugin_core); this class keeps the settings and applies the
 * rules' answers to the hardware.
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

private:
    static void fadeToward(int& milli, int target, int step);

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

    // UI task only
    deskhog::DeskRules rules_;
    std::string zone_;
    uint32_t zoneAtMs_ = 0;
    uint32_t lastTickMs_ = 0;
    int screenMilli_ = 0, ledMilli_ = 0;  ///< Applied levels, in thousandths of a percent, mid-fade
    int screenShown_ = -1, ledShown_ = -1;
    uint8_t swallowed_ = 0;  ///< One bit per button
};
