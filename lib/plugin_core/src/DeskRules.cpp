#include "DeskRules.h"

#include <stdio.h>

namespace deskhog {

namespace {

const DeviceSettings kDefaults;

bool parseNumber(const std::string& text, int& value) {
    if (text.empty() || text.size() > 6) return false;
    int result = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        result = result * 10 + (c - '0');
    }
    value = result;
    return true;
}

bool parseBool(const std::string& text, bool& value) {
    if (text == "true" || text == "yes" || text == "1") value = true;
    else if (text == "false" || text == "no" || text == "0") value = false;
    else return false;
    return true;
}

bool levelOk(int v, int low) { return v >= low && v <= 100; }
bool rotateOk(int v) { return v == 0 || (v >= 10 && v <= 600); }
bool minutesOk(int v) { return v >= 0 && v < 24 * 60; }

}  // namespace

bool isValidDeviceName(const std::string& name) {
    if (name.empty() || name.size() > 24 || name.front() == '-' || name.back() == '-') return false;
    for (char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return true;
}

bool parseClock(const std::string& text, int& minutes) {
    const size_t colon = text.find(':');
    if (colon == std::string::npos) {
        int total;
        if (!parseNumber(text, total) || !minutesOk(total)) return false;
        minutes = total;
        return true;
    }
    const std::string h = text.substr(0, colon), m = text.substr(colon + 1);
    int hours, mins;
    if (h.size() > 2 || m.size() != 2 || !parseNumber(h, hours) || !parseNumber(m, mins)) return false;
    if (hours > 23 || mins > 59) return false;
    minutes = hours * 60 + mins;
    return true;
}

std::string formatClock(int minutes) {
    char text[16];
    snprintf(text, sizeof(text), "%02d:%02d", minutes / 60, minutes % 60);
    return text;
}

bool applyDeviceSettings(DeviceSettings& settings,
                         const std::vector<std::pair<std::string, std::string>>& changes, std::string& error) {
    DeviceSettings next = settings;
    for (const auto& change : changes) {
        const std::string& key = change.first;
        const std::string& text = change.second;
        int number = 0;
        bool flag = false;
        int* level = nullptr;
        int low = 0;
        if (key == "name") {
            if (!isValidDeviceName(text)) {
                error = "name must be 1 to 24 of a-z, 0-9 and -, not starting or ending with -";
                return false;
            }
            next.name = text;
            continue;
        } else if (key == "screen.day") { level = &next.screenDay; low = 5; }
        else if (key == "screen.night") level = &next.screenNight;
        else if (key == "led.day") level = &next.ledDay;
        else if (key == "led.night") level = &next.ledNight;

        if (level) {
            if (!parseNumber(text, number) || !levelOk(number, low)) {
                error = key + " must be " + std::to_string(low) + " to 100";
                return false;
            }
            *level = number;
        } else if (key == "night.enabled" || key == "battery") {
            if (!parseBool(text, flag)) {
                error = key + " must be true or false";
                return false;
            }
            (key == "battery" ? next.battery : next.nightEnabled) = flag;
        } else if (key == "night.from" || key == "night.until") {
            if (!parseClock(text, number)) {
                error = key + " must be a time such as 22:30";
                return false;
            }
            (key == "night.from" ? next.nightFrom : next.nightUntil) = number;
        } else if (key == "rotate.seconds") {
            if (!parseNumber(text, number) || !rotateOk(number)) {
                error = "rotate.seconds must be 0 or 10 to 600";
                return false;
            }
            next.rotateSeconds = number;
        } else {
            error = "unknown setting " + key;
            return false;
        }
    }
    settings = next;
    return true;
}

DeviceSettings sanitized(const DeviceSettings& stored) {
    DeviceSettings out = stored;
    if (!isValidDeviceName(out.name)) out.name = kDefaults.name;
    if (!levelOk(out.screenDay, 5)) out.screenDay = kDefaults.screenDay;
    if (!levelOk(out.screenNight, 0)) out.screenNight = kDefaults.screenNight;
    if (!levelOk(out.ledDay, 0)) out.ledDay = kDefaults.ledDay;
    if (!levelOk(out.ledNight, 0)) out.ledNight = kDefaults.ledNight;
    if (!minutesOk(out.nightFrom)) out.nightFrom = kDefaults.nightFrom;
    if (!minutesOk(out.nightUntil)) out.nightUntil = kDefaults.nightUntil;
    if (!rotateOk(out.rotateSeconds)) out.rotateSeconds = kDefaults.rotateSeconds;
    return out;
}

void DeskRules::setSettings(const DeviceSettings& settings) { settings_ = settings; }

bool DeskRules::scheduledNight(int64_t epochSeconds, const std::string& posixZone) {
    if (!settings_.nightEnabled || epochSeconds == 0 || settings_.nightFrom == settings_.nightUntil) return false;
    if (posixZone != zoneText_) {
        zoneText_ = posixZone;
        zone_ = TimeZone();
        zone_.parse(posixZone.c_str());
    }
    const CivilTime t = zone_.toCivil(epochSeconds);
    const int now = t.hour * 60 + t.minute;
    const int from = settings_.nightFrom, until = settings_.nightUntil;
    return from < until ? (now >= from && now < until) : (now >= from || now < until);
}

DeskRules::Levels DeskRules::levelsFor(bool night, bool manual, uint32_t nowMs) const {
    Levels levels{settings_.screenDay, settings_.ledDay, night, manual};
    if (night) {
        levels.led = settings_.ledNight;
        levels.screen = woken(nowMs) ? settings_.screenDay : settings_.screenNight;
    }
    return levels;
}

DeskRules::Levels DeskRules::tick(int64_t epochSeconds, const std::string& posixZone, uint32_t nowMs) {
    scheduled_ = scheduledNight(epochSeconds, posixZone);
    if (manual_ && scheduled_ != scheduleAtManual_) manual_ = false;
    night_ = manual_ ? manualNight_ : scheduled_;

    if (previewActive_ && nowMs - previewStart_ < kPreviewMs) {
        Levels levels = levelsFor(previewNight_, manual_, nowMs);
        if (previewNight_) levels.screen = settings_.screenNight;
        levels.night = night_;  // only the levels follow the preview; the mode stays the real one
        return levels;
    }
    previewActive_ = false;
    return levelsFor(night_, manual_, nowMs);
}

bool DeskRules::press(uint32_t nowMs) {
    if (!night_) return false;
    const bool dark = settings_.screenNight == 0 && !woken(nowMs);
    wakeActive_ = true;
    wakeStart_ = nowMs;
    wakeSpan_ = kWakeMs;
    return dark;
}

void DeskRules::wake(uint32_t nowMs, uint32_t forMs) {
    if (woken(nowMs) && wakeSpan_ - (nowMs - wakeStart_) >= forMs) return;
    wakeActive_ = true;
    wakeStart_ = nowMs;
    wakeSpan_ = forMs;
}

void DeskRules::toggleNight() {
    const bool next = !night_;
    if (next == scheduled_) {
        manual_ = false;
    } else {
        manual_ = true;
        manualNight_ = next;
        scheduleAtManual_ = scheduled_;
    }
    night_ = next;
}

void DeskRules::preview(bool night, uint32_t nowMs) {
    previewActive_ = true;
    previewNight_ = night;
    previewStart_ = nowMs;
}

int Rotation::tick(const Input& input, uint32_t nowMs) {
    if (!seen_ || input.onScreen != lastOnScreen_) {
        seen_ = true;
        lastOnScreen_ = input.onScreen;
        since_ = nowMs;
    }
    if (seconds_ <= 0 || input.playing || input.banner || input.screenOff) return -1;
    if (handActive_ && nowMs - handAt_ < kHandPauseMs) return -1;
    if (nowMs - since_ < static_cast<uint32_t>(seconds_) * 1000u) return -1;

    const int count = static_cast<int>(input.rotates.size());
    const int here = input.onScreen;
    if (here < 0 || here >= count || !input.rotates[here]) return -1;
    // The next card that takes part, wrapping round; the card itself comes last.
    for (int step = 1; step < count; step++) {
        const int index = (here + step) % count;
        if (input.rotates[index]) return index;
    }
    return -1;
}

}  // namespace deskhog
