#pragma once

// What a plugin can reach on the device. The Lua runtime (LuaPlugin) only talks
// to these two interfaces, so it builds and runs on a desktop for tests and for
// the simulator, as well as in the firmware.

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

namespace deskhog {

enum class Font : uint8_t { Small, Medium, Large, Loud, Tiny };
enum class Align : uint8_t { Left, Center, Right };

// Values match the firmware's Input::BUTTON_* indices.
enum class Button : uint8_t { Down = 0, Center = 1, Up = 2 };

/** What a button did. Long: held for kLongPressMs, once per hold. */
enum class ButtonEvent : uint8_t { Press, Long, Release };
const uint32_t kLongPressMs = 600;

/** Drawing surface for one plugin card. Colors are 0xRRGGBB. */
class Gfx {
public:
    virtual ~Gfx() = default;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual void fillRect(int x, int y, int w, int h, uint32_t rgb, int radius) = 0;
    virtual void strokeRect(int x, int y, int w, int h, uint32_t rgb, int lineWidth) = 0;
    virtual void line(int x1, int y1, int x2, int y2, uint32_t rgb, int lineWidth) = 0;
    /** x is the left edge, the centre or the right edge, depending on align. */
    virtual void text(int x, int y, const char* text, uint32_t rgb, Font font, Align align) = 0;
    virtual int textWidth(const char* text, Font font) = 0;
    virtual int textHeight(Font font) = 0;
    /**
     * Draw an image with its top left corner at x, y. `argb` holds width * height
     * pixels as 0xAARRGGBB, row by row. `scale` enlarges it by a whole factor.
     */
    virtual void image(int x, int y, const uint32_t* argb, int width, int height, int scale) = 0;
    /** Limit drawing to a rectangle until the next call. A width or height of 0 lifts the limit. */
    virtual void clip(int x, int y, int w, int h) = 0;
    /**
     * Part of a ring around x, y: from startDeg clockwise to endDeg, where 0 is
     * straight up. `lineWidth` is how thick the ring is, measured inwards from
     * the radius; a width equal to the radius gives a pie slice. An end 360 or
     * more past the start draws the whole ring.
     */
    virtual void arc(int x, int y, int radius, int startDeg, int endDeg, uint32_t rgb, int lineWidth) = 0;
    /** A filled polygon. `xy` holds `count` points as x1, y1, x2, y2, ... */
    virtual void polygon(const int* xy, int count, uint32_t rgb) = 0;
};

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    uint32_t timeoutMs = 10000;
    size_t maxBytes = 0;  ///< The largest answer to accept, or 0 for the host's usual limit
    bool insecure = false;  ///< https only: do not check the server's certificate
    /**
     * Not 0: the answer is a picture. The host decodes it (PNG or JPEG), scales
     * it down to fit in this many pixels and answers with HttpResponse::image
     * instead of a body.
     */
    int imageMaxWidth = 0;
    int imageMaxHeight = 0;
};

/** The largest picture a host hands back, each way. */
const int kMaxImageSide = 240;

struct HttpResponse {
    int status = 0;       ///< HTTP status, or 0 when the request never got an answer
    std::string body;
    std::string error;    ///< Set when status is 0, or when a picture could not be decoded
    /** Response headers, in the order they came. The runtime lowercases the names. */
    std::vector<std::pair<std::string, std::string>> headers;
    /** For picture requests: width * height pixels as 0xAARRGGBB, row by row. */
    std::vector<uint32_t> image;
    int imageWidth = 0;
    int imageHeight = 0;
};

struct NetStatus {
    bool online = false;  ///< Connected, with an address
    int rssi = 0;         ///< Signal strength in dBm; only meaningful while online
    std::string ssid;
    std::string ip;
};

/** A plugin asking for attention with notify(). */
struct Notice {
    std::string text;      ///< For the banner over the card. May be "".
    bool hasLed = false;   ///< The plugin gave a colour to flash the LED in
    uint32_t ledRgb = 0;
};

/** One entry of a list a plugin fills in for a setting (`choices` in a config field). */
struct Choice {
    std::string value;  ///< What is saved
    std::string label;  ///< What people see
};

/** Everything else a plugin can touch. All calls happen on the UI task. */
class PluginServices {
public:
    virtual ~PluginServices() = default;
    virtual uint32_t millis() = 0;
    /** Unix time in seconds, or 0 while the clock has not been set. */
    virtual int64_t epochSeconds() = 0;
    virtual bool buttonHeld(Button button) = 0;
    /** @param durationMs How long to hold the color before the LED goes back to normal; 0 holds it until ledOff(). */
    virtual void ledSet(uint8_t r, uint8_t g, uint8_t b, uint32_t durationMs) = 0;
    virtual void ledOff() = 0;
    /**
     * Start a request. The host answers later by calling
     * LuaPlugin::httpResponse(requestId, ...) on the UI task.
     * @return false if the request could not be queued.
     */
    virtual bool httpStart(int requestId, const HttpRequest& request) = 0;
    /** Values are JSON text. */
    virtual bool storeGet(const std::string& key, std::string& json) = 0;
    virtual bool storeSet(const std::string& key, const std::string& json) = 0;
    virtual void log(const std::string& line) = 0;
    /** The device's time zone as a POSIX TZ string, e.g. "CET-1CEST,M3.5.0,M10.5.0/3". "" or "UTC0" for UTC. */
    virtual std::string timeZone() = 0;
    virtual NetStatus netStatus() = 0;
    /** A number nobody can guess, from the hardware if it can make one. Seeds math.random. */
    virtual uint32_t entropy() = 0;
    /**
     * The plugin called notify(). The host decides what comes of it: whether
     * the card may come to the front, and when (see Attention.h).
     * @param cardId The card that asks, or "" for a single-card plugin's only card.
     */
    virtual void notify(const std::string& cardId, const Notice& notice) = 0;
    /**
     * The answer to LuaPlugin::choices(). `error` is "" when the list is good.
     * May be called from inside choices() itself.
     */
    virtual void choicesDone(int requestId, const std::vector<Choice>& choices, const std::string& error) = 0;
};

/** Plugin names double as file names: 1-32 chars of a-z, 0-9, '-' and '_'. */
inline bool isValidPluginName(const std::string& name) {
    if (name.empty() || name.size() > 32) return false;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

}  // namespace deskhog
