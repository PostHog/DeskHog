// DeskHog plugin simulator.
//
// Runs a plugin on your computer through the same Lua runtime, the same
// drawing code and the same graphics library and fonts as the device, then
// saves what the card would show as a PNG. See tools/sim/run.sh for usage.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <lvgl.h>

#include "LuaPlugin.h"
#include "PluginHost.h"
#include "plugins/LvglGfx.h"

using namespace deskhog;

namespace {

const int kCardWidth = 233;  // the 240px screen minus the card stack's indicator
const int kCardHeight = 135;
const int kFrameMs = 33;

// ---------------------------------------------------------------- PNG output

uint32_t crc32(const uint8_t* data, size_t length, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < length; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(v >> 24);
    out.push_back(v >> 16);
    out.push_back(v >> 8);
    out.push_back(v);
}

void chunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& data) {
    put32(png, (uint32_t)data.size());
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    png.insert(png.end(), body.begin(), body.end());
    put32(png, crc32(body.data(), body.size()));
}

// An uncompressed PNG: small enough here, and no zlib needed to build.
bool writePng(const std::string& path, const std::vector<uint8_t>& rgb, int width, int height) {
    std::vector<uint8_t> raw;
    for (int y = 0; y < height; y++) {
        raw.push_back(0);  // filter: none
        raw.insert(raw.end(), rgb.begin() + (size_t)y * width * 3, rgb.begin() + (size_t)(y + 1) * width * 3);
    }
    std::vector<uint8_t> zlib = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t offset = 0; offset < raw.size();) {
        const size_t size = raw.size() - offset < 65535 ? raw.size() - offset : 65535;
        zlib.push_back(offset + size == raw.size() ? 1 : 0);
        zlib.push_back(size & 0xFF);
        zlib.push_back(size >> 8);
        zlib.push_back(~size & 0xFF);
        zlib.push_back((~size >> 8) & 0xFF);
        zlib.insert(zlib.end(), raw.begin() + offset, raw.begin() + offset + size);
        offset += size;
    }
    put32(zlib, (b << 16) | a);

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> header;
    put32(header, (uint32_t)width);
    put32(header, (uint32_t)height);
    header.insert(header.end(), {8, 2, 0, 0, 0});  // 8-bit RGB
    chunk(png, "IHDR", header);
    chunk(png, "IDAT", zlib);
    chunk(png, "IEND", {});

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(png.data()), (std::streamsize)png.size());
    return out.good();
}

// ------------------------------------------------------------- fake services

std::string shellQuote(const std::string& text) {
    std::string out = "'";
    for (char c : text) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    return out + "'";
}

class SimServices : public PluginServices {
public:
    struct Pending {
        int id;
        HttpResponse response;
    };

    uint32_t now = 0;
    bool liveHttp = true;
    std::vector<std::pair<std::string, std::string>> replies;  ///< part of a URL -> file to answer with
    bool held[3] = {false, false, false};
    std::string led = "normal";
    std::vector<Pending> answers;
    std::map<std::string, std::string> stored;

    uint32_t millis() override { return now; }
    int64_t epochSeconds() override { return (int64_t)time(nullptr); }
    bool buttonHeld(Button button) override { return held[(int)button]; }
    void ledSet(uint8_t r, uint8_t g, uint8_t b, uint32_t durationMs) override {
        char buf[48];
        snprintf(buf, sizeof(buf), "#%02x%02x%02x%s", r, g, b, durationMs ? " (timed)" : "");
        led = buf;
        printf("[led] %s\n", buf);
    }
    void ledOff() override {
        led = "normal";
        printf("[led] back to normal\n");
    }
    bool storeGet(const std::string& key, std::string& json) override {
        const auto it = stored.find(key);
        if (it == stored.end()) return false;
        json = it->second;
        return true;
    }
    bool storeSet(const std::string& key, const std::string& json) override {
        stored[key] = json;
        printf("[store] %s = %s\n", key.c_str(), json.c_str());
        return true;
    }
    void log(const std::string& line) override { printf("[plugin] %s\n", line.c_str()); }

    // Requests go out through curl, so plugins show real data in the simulator.
    bool httpStart(int requestId, const HttpRequest& request) override {
        HttpResponse response;
        printf("[http] %s %s\n", request.method.c_str(), request.url.c_str());
        const size_t limit = request.maxBytes ? request.maxBytes : 48 * 1024;
        for (const auto& reply : replies) {
            if (request.url.find(reply.first) == std::string::npos) continue;
            std::ifstream file(reply.second, std::ios::binary);
            std::stringstream content;
            content << file.rdbuf();
            printf("[http] answered from %s\n", reply.second.c_str());
            response.status = 200;
            response.body = content.str();
            answers.push_back({requestId, response});
            return true;
        }
        if (!liveHttp) {
            response.error = "network is off (--no-http)";
            answers.push_back({requestId, response});
            return true;
        }
        std::string command = "curl -sS -L --max-time " + std::to_string(request.timeoutMs / 1000 + 1) +
                              " -A DeskHog -w '\\n%{http_code}' -X " + request.method;
        for (const auto& header : request.headers) command += " -H " + shellQuote(header.first + ": " + header.second);
        if (request.method == "POST") command += " --data-binary " + shellQuote(request.body);
        command += " " + shellQuote(request.url) + " 2>&1";

        std::string output;
        FILE* pipe = popen(command.c_str(), "r");
        if (pipe) {
            char buffer[4096];
            size_t got;
            while ((got = fread(buffer, 1, sizeof(buffer), pipe)) > 0) output.append(buffer, got);
            const int exitCode = pclose(pipe);
            const size_t lastLine = output.rfind('\n');
            if (exitCode == 0 && lastLine != std::string::npos) {
                response.status = atoi(output.c_str() + lastLine + 1);
                response.body = output.substr(0, lastLine);
            } else {
                response.error = output.empty() ? "request failed" : output.substr(0, 120);
            }
        } else {
            response.error = "could not run curl";
        }
        if (response.body.size() > limit) {
            response = HttpResponse();
            response.error = "response too large";
        }
        answers.push_back({requestId, response});
        return true;
    }
};

// ------------------------------------------------------------------- helpers

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void flushNothing(lv_display_t* display, const lv_area_t*, uint8_t*) { lv_display_flush_ready(display); }

struct Press {
    Button button;
    uint32_t atMs;
};

bool parseButton(const std::string& name, Button& out) {
    if (name == "down") out = Button::Down;
    else if (name == "center") out = Button::Center;
    else if (name == "up") out = Button::Up;
    else return false;
    return true;
}

void usage() {
    printf("usage: sim <plugin.lua> [options]\n"
           "  --out FILE        where to save the PNG (default: <plugin>.png)\n"
           "  --seconds N       how long to run before the screenshot (default: 2)\n"
           "  --press LIST      button presses, e.g. center@0.5,center@1.2,up@3\n"
           "  --card KIND       which kind of card to show, for plugins with several (default: the first)\n"
           "  --set KEY=VALUE   a setting, of the plugin or of the card; repeat for more\n"
           "  --scale N         enlarge the PNG N times (default: 3)\n"
           "  --reply PART=FILE answer requests whose URL contains PART with the file, instead of the network\n"
           "  --no-http         answer every request with a network error\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    const std::string pluginPath = argv[1];
    std::string outPath;
    double seconds = 2;
    int scale = 3;
    std::vector<Press> presses;
    std::string kind;
    std::map<std::string, std::string> settings;
    SimServices services;

    for (int i = 2; i < argc; i++) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--out" && hasValue) {
            outPath = argv[++i];
        } else if (arg == "--seconds" && hasValue) {
            seconds = atof(argv[++i]);
        } else if (arg == "--scale" && hasValue) {
            scale = atoi(argv[++i]);
            if (scale < 1) scale = 1;
        } else if (arg == "--card" && hasValue) {
            kind = argv[++i];
        } else if (arg == "--reply" && hasValue) {
            const std::string pair = argv[++i];
            const size_t equals = pair.find('=');
            if (equals == std::string::npos) {
                usage();
                return 2;
            }
            services.replies.emplace_back(pair.substr(0, equals), pair.substr(equals + 1));
        } else if (arg == "--no-http") {
            services.liveHttp = false;
        } else if (arg == "--set" && hasValue) {
            const std::string pair = argv[++i];
            const size_t equals = pair.find('=');
            if (equals == std::string::npos) {
                usage();
                return 2;
            }
            settings[pair.substr(0, equals)] = pair.substr(equals + 1);
        } else if (arg == "--press" && hasValue) {
            std::stringstream list(argv[++i]);
            std::string item;
            while (std::getline(list, item, ',')) {
                const size_t at = item.find('@');
                Press press{Button::Center, 0};
                if (at == std::string::npos || !parseButton(item.substr(0, at), press.button)) {
                    usage();
                    return 2;
                }
                press.atMs = (uint32_t)(atof(item.c_str() + at + 1) * 1000);
                presses.push_back(press);
            }
        } else {
            usage();
            return 2;
        }
    }

    const std::string source = readFile(pluginPath);
    if (source.empty()) {
        fprintf(stderr, "could not read %s\n", pluginPath.c_str());
        return 1;
    }
    std::string name = pluginPath;
    const size_t slash = name.find_last_of('/');
    if (slash != std::string::npos) name = name.substr(slash + 1);
    if (name.size() > 4 && name.substr(name.size() - 4) == ".lua") name = name.substr(0, name.size() - 4);
    if (outPath.empty()) outPath = name + ".png";
    if (!isValidPluginName(name)) {
        fprintf(stderr, "\"%s\" is not a valid plugin name: use 1-32 characters of a-z, 0-9, - and _\n", name.c_str());
        return 1;
    }

    // A display nobody looks at: the canvas keeps its own pixels.
    lv_init();
    static uint8_t displayBuffer[240 * 135 * 2];
    lv_display_t* display = lv_display_create(240, 135);
    lv_display_set_flush_cb(display, flushNothing);
    lv_display_set_buffers(display, displayBuffer, nullptr, sizeof(displayBuffer), LV_DISPLAY_RENDER_MODE_PARTIAL);

    int exitCode = 0;
    {
        LvglGfx gfx(lv_screen_active(), kCardWidth, kCardHeight);
        if (!gfx.ok()) {
            fprintf(stderr, "could not create the canvas\n");
            return 1;
        }
        LuaPlugin plugin(gfx, services);
        plugin.start(name, source.data(), source.size(), settings);

        // One card, like one screen on the device. A setting given with --set
        // goes to whichever of the two has a field by that name.
        const std::string card = "1";
        if (kind.empty() && !plugin.kinds().empty()) kind = plugin.kinds()[0].key;
        if (!plugin.addCard(card, kind, settings, gfx)) {
            fprintf(stderr, "this plugin has no card kind \"%s\". It has:", kind.c_str());
            for (const LuaPlugin::CardKind& known : plugin.kinds()) fprintf(stderr, " %s", known.key.c_str());
            fprintf(stderr, "\n");
            return 1;
        }
        plugin.setVisible(card, true);

        const uint32_t endMs = (uint32_t)(seconds * 1000);
        size_t nextPress = 0;
        int frames = 0;
        for (;;) {
            // Network answers arrive on the frame after the request, as on the device.
            std::vector<SimServices::Pending> answers;
            answers.swap(services.answers);
            for (const SimServices::Pending& answer : answers) plugin.httpResponse(answer.id, answer.response);

            while (nextPress < presses.size() && presses[nextPress].atMs <= services.now) {
                const Press& press = presses[nextPress++];
                static const char* const names[] = {"down", "center", "up"};
                const bool used = plugin.button(card, press.button);
                printf("[button] %s at %.2fs%s\n", names[(int)press.button], services.now / 1000.0,
                       used ? "" : " (not used by the plugin: the card stack would move)");
            }

            plugin.tick(services.now);
            if (plugin.needsDraw(card)) {
                gfx.beginFrame();
                plugin.draw(card);
                gfx.endFrame();
                frames++;
            }
            lv_tick_inc(kFrameMs);
            lv_timer_handler();

            if (services.now >= endMs) break;
            services.now += kFrameMs;
        }

        printf("%s: %s after %.1fs, %d frames drawn, %zu KB of Lua memory\n", plugin.title().c_str(),
               plugin.failed() ? "FAILED" : "running", services.now / 1000.0, frames, plugin.memoryUsed() / 1024);
        if (plugin.failed()) {
            printf("error: %s\n", plugin.error().c_str());
            exitCode = 1;
        }

        // RGB565 canvas -> RGB888, enlarged.
        lv_draw_buf_t* buffer = lv_canvas_get_draw_buf(gfx.canvas());
        const uint32_t stride = buffer->header.stride;
        std::vector<uint8_t> rgb((size_t)kCardWidth * scale * kCardHeight * scale * 3);
        for (int y = 0; y < kCardHeight * scale; y++) {
            const uint16_t* row = reinterpret_cast<const uint16_t*>(buffer->data + (size_t)(y / scale) * stride);
            for (int x = 0; x < kCardWidth * scale; x++) {
                const uint16_t pixel = row[x / scale];
                uint8_t* out = &rgb[((size_t)y * kCardWidth * scale + x) * 3];
                out[0] = (uint8_t)(((pixel >> 11) & 0x1F) * 255 / 31);
                out[1] = (uint8_t)(((pixel >> 5) & 0x3F) * 255 / 63);
                out[2] = (uint8_t)((pixel & 0x1F) * 255 / 31);
            }
        }
        if (!writePng(outPath, rgb, kCardWidth * scale, kCardHeight * scale)) {
            fprintf(stderr, "could not write %s\n", outPath.c_str());
            return 1;
        }
        printf("saved %s\n", outPath.c_str());
    }
    return exitCode;
}
