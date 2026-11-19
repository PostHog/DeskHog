#pragma once

// What the device settings mean: the limits each one has, and what the screen
// and the LED should show right now. Nothing here touches hardware, Arduino or
// LVGL, so the firmware, the simulator and the tests share it. Not thread safe:
// the UI task owns a DeskRules; other tasks change DeviceSettings under a lock
// and hand the result to setSettings().
//
// All timers compare uint32_t differences, so they survive millis() wrapping.

#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include "TimeZone.h"

namespace deskhog {

struct DeviceSettings {
    std::string name = "deskhog";
    int screenDay = 80, screenNight = 10;
    int ledDay = 100, ledNight = 0;
    bool nightEnabled = false;
    int nightFrom = 22 * 60, nightUntil = 7 * 60;  // minutes after midnight
    int rotateSeconds = 0;
    bool battery = false;
};

/** 1 to 24 of a-z 0-9 and '-', not starting or ending with '-'. */
bool isValidDeviceName(const std::string& name);
/** "22:30" or "1350" to minutes; false if neither. */
bool parseClock(const std::string& text, int& minutes);
std::string formatClock(int minutes);  // "07:00"
/** Keys: name, screen.day, screen.night, led.day, led.night, night.enabled, night.from,
 *  night.until, rotate.seconds, battery. Booleans are "true"/"false"/"yes"/"no"/"1"/"0".
 *  All or nothing: on false, `settings` is untouched and `error` names the key and the rule. */
bool applyDeviceSettings(DeviceSettings& settings,
                         const std::vector<std::pair<std::string, std::string>>& changes, std::string& error);
/** Whatever was stored: every field out of range goes back to its default. */
DeviceSettings sanitized(const DeviceSettings& stored);

class DeskRules {
public:
    static const uint32_t kWakeMs = 30000;     // screen stays at the day level after a press at night
    static const uint32_t kPreviewMs = 3000;   // a slider on the web page shows its level this long
    struct Levels { int screen; int led; bool night; bool manual; };
    void setSettings(const DeviceSettings& settings);
    /** @param epochSeconds 0 while the clock is not set: then it is never night by schedule. */
    Levels tick(int64_t epochSeconds, const std::string& posixZone, uint32_t nowMs);
    /** A button went down. True if the press only woke a dark screen and must go no further. */
    bool press(uint32_t nowMs);
    /** Something is worth seeing (a notify banner): day level for this long. */
    void wake(uint32_t nowMs, uint32_t forMs);
    /** ● on the status card. */
    void toggleNight();
    /** The web page is dragging a slider. */
    void preview(bool night, uint32_t nowMs);

private:
    bool scheduledNight(int64_t epochSeconds, const std::string& posixZone);
    bool woken(uint32_t nowMs) const { return wakeActive_ && nowMs - wakeStart_ < wakeSpan_; }
    Levels levelsFor(bool night, bool manual, uint32_t nowMs) const;

    DeviceSettings settings_;
    TimeZone zone_;
    std::string zoneText_;

    // The hand-made mode, and what the schedule said when it was chosen. It
    // ends on the first tick where the schedule says something else.
    bool manual_ = false;
    bool manualNight_ = false;
    bool scheduleAtManual_ = false;

    bool scheduled_ = false;  ///< What the last tick found
    bool night_ = false;      ///< What the last tick showed, before any preview

    bool wakeActive_ = false;
    uint32_t wakeStart_ = 0, wakeSpan_ = 0;
    bool previewActive_ = false, previewNight_ = false;
    uint32_t previewStart_ = 0;
};

}  // namespace deskhog
