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

enum class Font : uint8_t { Small, Medium, Large, Loud };
enum class Align : uint8_t { Left, Center, Right };

// Values match the firmware's Input::BUTTON_* indices.
enum class Button : uint8_t { Down = 0, Center = 1, Up = 2 };

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
};

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    uint32_t timeoutMs = 10000;
    size_t maxBytes = 0;  ///< The largest answer to accept, or 0 for the host's usual limit
};

struct HttpResponse {
    int status = 0;       ///< HTTP status, or 0 when the request never got an answer
    std::string body;
    std::string error;    ///< Set when status is 0
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
