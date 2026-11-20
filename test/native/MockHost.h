#pragma once

// A fake device for running plugins on a desktop: it records what a plugin
// draws and asks for instead of doing it.

#include <stdarg.h>
#include <stdio.h>
#include <map>
#include <string>
#include <vector>

#include "PluginHost.h"

namespace deskhog {

inline const char* fontName(Font font) {
    switch (font) {
        case Font::Small: return "small";
        case Font::Medium: return "medium";
        case Font::Large: return "large";
        case Font::Loud: return "loud";
        case Font::Tiny: return "tiny";
    }
    return "?";
}

/** Records every drawing call as a line of text, e.g. "text 116,49 #ffffff large center 21°". */
class RecordingGfx : public Gfx {
public:
    std::vector<std::string> calls;

    int width() const override { return 233; }
    int height() const override { return 135; }

    void fillRect(int x, int y, int w, int h, uint32_t rgb, int radius) override {
        add("rect %d,%d %dx%d #%06x r%d", x, y, w, h, (unsigned)rgb, radius);
    }
    void strokeRect(int x, int y, int w, int h, uint32_t rgb, int lineWidth) override {
        add("outline %d,%d %dx%d #%06x w%d", x, y, w, h, (unsigned)rgb, lineWidth);
    }
    void line(int x1, int y1, int x2, int y2, uint32_t rgb, int lineWidth) override {
        add("line %d,%d-%d,%d #%06x w%d", x1, y1, x2, y2, (unsigned)rgb, lineWidth);
    }
    void text(int x, int y, const char* text, uint32_t rgb, Font font, Align align) override {
        static const char* const aligns[] = {"left", "center", "right"};
        add("text %d,%d #%06x %s %s %s", x, y, (unsigned)rgb, fontName(font), aligns[(int)align], text);
    }
    void image(int x, int y, const uint32_t* argb, int width, int height, int scale) override {
        // First and last pixel, so a test can tell images apart.
        add("image %d,%d %dx%d x%d %08x..%08x", x, y, width, height, scale, (unsigned)argb[0],
            (unsigned)argb[(size_t)width * height - 1]);
    }
    void clip(int x, int y, int w, int h) override {
        if (w <= 0 || h <= 0) {
            add("clip off");
        } else {
            add("clip %d,%d %dx%d", x, y, w, h);
        }
    }
    void arc(int x, int y, int radius, int startDeg, int endDeg, uint32_t rgb, int lineWidth) override {
        add("arc %d,%d r%d %d-%d #%06x w%d", x, y, radius, startDeg, endDeg, (unsigned)rgb, lineWidth);
    }
    void polygon(const int* xy, int count, uint32_t rgb) override {
        std::string points;
        for (int i = 0; i < count; i++) {
            points += (i ? " " : "") + std::to_string(xy[2 * i]) + "," + std::to_string(xy[2 * i + 1]);
        }
        add("poly %s #%06x", points.c_str(), (unsigned)rgb);
    }
    // Fixed-width stand-ins for the real fonts.
    int textWidth(const char* text, Font font) override {
        int longest = 0, current = 0;
        for (const char* p = text; *p; p++) {
            if (*p == '\n') {
                current = 0;
            } else if (((unsigned char)*p & 0xC0) != 0x80) {  // count characters, not UTF-8 bytes
                current++;
                if (current > longest) longest = current;
            }
        }
        return longest * glyphWidth(font);
    }
    int textHeight(Font font) override {
        switch (font) {
            case Font::Small: return 18;
            case Font::Medium: return 20;
            case Font::Large: return 44;
            case Font::Loud: return 24;
            case Font::Tiny: return 12;
        }
        return 18;
    }

    /** True if any recorded call contains `needle`. */
    bool drew(const std::string& needle) const {
        for (const std::string& call : calls) {
            if (call.find(needle) != std::string::npos) return true;
        }
        return false;
    }

    std::string dump() const {
        std::string out;
        for (const std::string& call : calls) out += "    " + call + "\n";
        return out;
    }

private:
    static int glyphWidth(Font font) {
        switch (font) {
            case Font::Small: return 8;
            case Font::Medium: return 9;
            case Font::Large: return 20;
            case Font::Loud: return 12;
            case Font::Tiny: return 5;
        }
        return 8;
    }

    void add(const char* format, ...) __attribute__((format(printf, 2, 3))) {
        char buf[512];
        va_list args;
        va_start(args, format);
        vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        calls.push_back(buf);
    }
};

class MockServices : public PluginServices {
public:
    struct StartedRequest {
        int id;
        HttpRequest request;
    };

    uint32_t now = 1000;
    uint32_t advancePerCall = 0;  ///< Set to 1 to make time pass while a plugin spins
    int64_t epoch = 1763200000;
    bool held[3] = {false, false, false};
    bool httpAccepts = true;
    std::vector<StartedRequest> requests;
    std::map<std::string, std::string> stored;
    std::vector<std::string> logs;
    std::string led = "default";
    std::string zone = "UTC0";
    NetStatus net;
    bool hasBattery = false;
    BatteryStatus batteryStatus;
    std::string name = "deskhog";
    uint32_t entropyCalls = 0;

    std::string timeZone() override { return zone; }
    NetStatus netStatus() override { return net; }
    bool battery(BatteryStatus& status) override {
        if (!hasBattery) return false;
        status = batteryStatus;
        return true;
    }
    std::string deviceName() override { return name; }
    uint32_t entropy() override { return 0x9E3779B9u * ++entropyCalls; }

    uint32_t millis() override {
        now += advancePerCall;
        return now;
    }
    int64_t epochSeconds() override { return epoch; }
    bool buttonHeld(Button button) override { return held[(int)button]; }
    void ledSet(uint8_t r, uint8_t g, uint8_t b, uint32_t durationMs) override {
        char buf[32];
        if (durationMs) {
            snprintf(buf, sizeof(buf), "#%02x%02x%02x for %ums", r, g, b, (unsigned)durationMs);
        } else {
            snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
        }
        led = buf;
    }
    void ledOff() override { led = "default"; }
    bool httpStart(int requestId, const HttpRequest& request) override {
        if (!httpAccepts) return false;
        requests.push_back({requestId, request});
        return true;
    }
    bool storeGet(const std::string& key, std::string& json) override {
        const auto it = stored.find(key);
        if (it == stored.end()) return false;
        json = it->second;
        return true;
    }
    bool storeSet(const std::string& key, const std::string& json) override {
        stored[key] = json;
        return true;
    }
    void log(const std::string& line) override { logs.push_back(line); }

    struct Notified {
        std::string card;
        Notice notice;
    };
    struct ChoicesAnswer {
        int id;
        std::vector<Choice> choices;
        std::string error;
    };
    std::vector<Notified> notices;
    std::vector<ChoicesAnswer> choiceAnswers;

    void notify(const std::string& cardId, const Notice& notice) override { notices.push_back({cardId, notice}); }
    void choicesDone(int requestId, const std::vector<Choice>& choices, const std::string& error) override {
        choiceAnswers.push_back({requestId, choices, error});
    }

    bool logged(const std::string& needle) const {
        for (const std::string& line : logs) {
            if (line.find(needle) != std::string::npos) return true;
        }
        return false;
    }
};

}  // namespace deskhog
