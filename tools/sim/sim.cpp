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
#include <unistd.h>

#include <sys/wait.h>

#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <lvgl.h>

#include "Attention.h"
#include "LuaPlugin.h"
#include "PluginHost.h"
#include "TimeZone.h"
#include "plugins/ImageDecode.h"
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
    bool offline = false;  ///< --offline: no WiFi
    std::vector<std::pair<std::string, std::string>> replies;  ///< part of a URL -> file to answer with
    std::vector<std::pair<std::string, int>> replyStatuses;    ///< part of a URL -> the status to answer with
    std::vector<std::pair<std::string, std::string>> replyHeaders;  ///< part of a URL -> "Name: value" to add
    int64_t startEpoch = (int64_t)time(nullptr);  ///< What the clock says when the run starts (--time)
    std::vector<int> rssi = {-58};                ///< Signal strength, a value per second, over and over (--rssi)
    bool seeded = false;                          ///< --seed: math.random gives the same numbers every run
    std::mt19937 seededNumbers;
    bool held[3] = {false, false, false};
    std::string led = "normal";
    std::string zone = "UTC0";  ///< The device's time zone, as a POSIX TZ string (--tz)
    std::vector<Pending> answers;
    std::map<std::string, std::string> stored;

    uint32_t millis() override { return now; }
    // The clock runs on the simulator's time, not the computer's: ten seconds
    // of card take a moment to run, and the card should see ten seconds pass.
    int64_t epochSeconds() override { return startEpoch + now / 1000; }
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
    std::string timeZone() override { return zone; }
    // A made-up network, the same on every run so screenshots do not change.
    NetStatus netStatus() override {
        NetStatus status;
        status.online = liveHttp && !offline;
        status.rssi = rssi[(now / 1000) % rssi.size()];
        status.ssid = "Simulator";
        status.ip = "192.168.1.50";
        return status;
    }
    bool hasBattery = false;
    BatteryStatus batteryStatus;
    std::string name = "deskhog";
    bool battery(BatteryStatus& status) override {
        if (!hasBattery) return false;
        status = batteryStatus;
        return true;
    }
    std::string deviceName() override { return name; }
    uint32_t entropy() override { return seeded ? (uint32_t)seededNumbers() : std::random_device()(); }

    // notify(): the same rules as on the device decide what comes of it.
    Attention attention;
    bool mayFront = true;  ///< The card's "May come to the front" setting (--no-front turns it off)
    void notify(const std::string&, const Notice& notice) override {
        char led[24] = "";
        if (notice.hasLed) snprintf(led, sizeof(led), " (led #%06x)", (unsigned)notice.ledRgb);
        printf("[notify] at %.2fs: %s%s\n", now / 1000.0, notice.text.empty() ? "(no text)" : notice.text.c_str(), led);
        attention.request("1", notice, mayFront, now);
    }
    // Why curl gave up, in the words the device would use where it has them.
    static std::string curlTrouble(int exitCode) {
        switch (exitCode) {
            case 3: return "bad url";
            case 6: return "could not resolve host";
            case 7: return "connection refused";
            case 28: return "connection timed out";
            case 51: case 60: case 77: case 83: case 90: case 91: return "certificate not trusted";
            case 35: case 53: case 58: case 59: return "secure connection failed";
            case 47: return "too many redirects";  // the device hands over the 3xx answer instead
            case 52: case 56: return "connection lost";
            default: return "request failed (curl gave up with code " + std::to_string(exitCode) + ")";
        }
    }

    void choicesDone(int, const std::vector<Choice>& choices, const std::string& error) override {
        if (!error.empty()) {
            printf("[choices] failed: %s\n", error.c_str());
            return;
        }
        printf("[choices] %zu to pick from\n", choices.size());
        for (const Choice& choice : choices) printf("[choices]   %-24s %s\n", choice.value.c_str(), choice.label.c_str());
    }

    // gfx.fetchImage: the body is a picture file. Decode it the way the device does.
    static void finish(const HttpRequest& request, HttpResponse& response) {
        if (request.imageMaxWidth <= 0 || response.status < 200 || response.status >= 300) return;
        std::string file;
        file.swap(response.body);
        if (!decodeImage(reinterpret_cast<uint8_t*>(&file[0]), file.size(), request.imageMaxWidth,
                         request.imageMaxHeight, response.image, response.imageWidth, response.imageHeight,
                         response.error)) {
            printf("[http] the picture could not be read: %s\n", response.error.c_str());
        }
    }

    // The headers of the last answer in a curl header dump (a redirect leaves several).
    static void readHeaders(const std::string& path, HttpResponse& response) {
        std::ifstream file(path);
        std::string line;
        while (std::getline(file, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            if (line.compare(0, 5, "HTTP/") == 0) {
                response.headers.clear();
                continue;
            }
            const size_t colon = line.find(':');
            if (colon == std::string::npos || colon == 0) continue;
            size_t value = colon + 1;
            while (value < line.size() && line[value] == ' ') value++;
            response.headers.emplace_back(line.substr(0, colon), line.substr(value));
        }
    }

    // Requests go out through curl, so plugins show real data in the simulator.
    bool httpStart(int requestId, const HttpRequest& request) override {
        HttpResponse response;
        printf("[http] %s %s\n", request.method.c_str(), request.url.c_str());
        const bool carriesBody = request.method != "GET" && request.method != "HEAD";
        if (carriesBody && !request.body.empty()) {
            const bool cut = request.body.size() > 400;
            printf("[http]   body: %s%s\n", request.body.substr(0, 400).c_str(), cut ? "..." : "");
        }
        const size_t limit = request.maxBytes ? request.maxBytes : 48 * 1024;
        if (offline) {
            response.error = "no WiFi";
            answers.push_back({requestId, response});
            return true;
        }
        // --reply, --reply-status and --reply-header: any of them that names a
        // part of this URL answers it, and the network is left alone.
        const auto matches = [&request](const std::string& part) { return request.url.find(part) != std::string::npos; };
        bool answered = false;
        std::string from = "--reply-status";
        for (const auto& reply : replies) {
            if (!matches(reply.first)) continue;
            std::ifstream file(reply.second, std::ios::binary);
            std::stringstream content;
            content << file.rdbuf();
            if (!file) printf("[http] could not read %s\n", reply.second.c_str());
            response.body = content.str();
            from = reply.second;
            answered = true;
            break;
        }
        response.status = 200;
        for (const auto& status : replyStatuses) {
            if (!matches(status.first)) continue;
            response.status = status.second;
            answered = true;
            break;
        }
        for (const auto& header : replyHeaders) {
            if (!matches(header.first)) continue;
            const size_t colon = header.second.find(':');
            size_t value = colon + 1;
            while (value < header.second.size() && header.second[value] == ' ') value++;
            response.headers.emplace_back(header.second.substr(0, colon), header.second.substr(value));
            if (!answered) from = "--reply-header";
            answered = true;
        }
        if (answered) {
            printf("[http] answered %d from %s\n", response.status, from.c_str());
            if (request.method == "HEAD") response.body.clear();
            if (response.body.size() > limit) {
                response = HttpResponse();
                response.error = "response too large";
            }
            finish(request, response);
            answers.push_back({requestId, std::move(response)});
            return true;
        }
        response = HttpResponse();
        if (!liveHttp) {
            response.error = "network is off (--no-http)";
            answers.push_back({requestId, response});
            return true;
        }
        char headerPath[] = "/tmp/deskhog-sim-headers-XXXXXX";
        const int headerFile = mkstemp(headerPath);
        if (headerFile >= 0) close(headerFile);
        std::string command = "curl -sS -L --max-redirs 5 --max-time " + std::to_string(request.timeoutMs / 1000 + 1) +
                              " -A DeskHog -w '\\n%{http_code}'";
        // The device checks certificates too, unless the plugin asks it not to.
        if (request.insecure) command += " -k";
        command += request.method == "HEAD" ? std::string(" --head") : " -X " + request.method;
        if (headerFile >= 0) command += " -D " + shellQuote(headerPath);
        for (const auto& header : request.headers) command += " -H " + shellQuote(header.first + ": " + header.second);
        if (carriesBody && !request.body.empty()) command += " --data-binary " + shellQuote(request.body);
        // What curl has to say about a failure stays out of it: the plugin gets
        // a short reason, as on the device.
        command += " " + shellQuote(request.url) + " 2>/dev/null";

        std::string output;
        FILE* pipe = popen(command.c_str(), "r");
        if (pipe) {
            char buffer[4096];
            size_t got;
            while ((got = fread(buffer, 1, sizeof(buffer), pipe)) > 0) output.append(buffer, got);
            const int waited = pclose(pipe);
            const int exitCode = WIFEXITED(waited) ? WEXITSTATUS(waited) : -1;
            const size_t lastLine = output.rfind('\n');
            if (exitCode == 0 && lastLine != std::string::npos) {
                response.status = atoi(output.c_str() + lastLine + 1);
                // With --head curl prints the headers where the body would be.
                if (request.method != "HEAD") response.body = output.substr(0, lastLine);
            } else {
                response.error = curlTrouble(exitCode);
                printf("[http] failed: %s\n", response.error.c_str());
            }
        } else {
            response.error = "could not run curl";
        }
        if (headerFile >= 0) {
            if (response.status) readHeaders(headerPath, response);
            unlink(headerPath);
        }
        if (response.body.size() > limit) {
            response = HttpResponse();
            response.error = "response too large";
        }
        finish(request, response);
        answers.push_back({requestId, std::move(response)});
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

struct Hold {
    Button button;
    uint32_t fromMs;
    uint32_t toMs;
};

struct Message {
    uint32_t atMs;
    std::string body;
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
           "  --hold LIST       buttons held down for a while, e.g. center@1-2.5,up@3-4\n"
           "                    (for a long press give both: --press center@1 --hold center@1-2.5)\n"
           "  --tz ZONE         the device's time zone as a POSIX string, e.g. CET-1CEST,M3.5.0,M10.5.0/3 (default: UTC0)\n"
           "  --time WHEN       what the clock says when the run starts: 2026-11-17T14:05 or 2026-11-17T14:05:30 in\n"
           "                    the --tz zone, the same with a Z at the end for UTC, or Unix seconds (default: now).\n"
           "                    The clock then runs with the simulation: --seconds 70 crosses a minute\n"
           "  --seed N          math.random gives the same numbers on every run (default: different ones)\n"
           "  --rssi LIST       WiFi signal strength in dBm, e.g. -58 or -52,-55,-61,-70: one value per second,\n"
           "                    over and over (default: -58)\n"
           "  --battery N[+]    the device has a battery at N percent; a + means it is charging\n"
           "                    (default: no battery, device.battery() is nil)\n"
           "  --name TEXT       what device.name() says (default: deskhog)\n"
           "  --offline         no WiFi: net.online() is false and every request fails with \"no WiFi\"\n"
           "  --store KEY=JSON  a value the plugin saved earlier, e.g. best=42; repeat for more\n"
           "  --card KIND       which kind of card to show, for plugins with several (default: the first)\n"
           "  --set KEY=VALUE   a setting, of the plugin or of the card; repeat for more\n"
           "  --scale N         enlarge the PNG N times (default: 3)\n"
           "  --reply PART=FILE answer requests whose URL contains PART with the file, instead of the network\n"
           "  --reply-status PART=CODE   answer them with this status instead of 200, e.g. /runs=304 (no --reply\n"
           "                    needed: the body is then empty)\n"
           "  --reply-header PART='Name: value'   and with this header; repeat for more\n"
           "  --no-http         answer every request with a network error\n"
           "  --message AT=TEXT push a message to the plugin AT seconds in, e.g. 1=\"Pizza's here\" or 1=@order.json\n"
           "                    (@FILE sends the file's contents); repeat for more\n"
           "  --choices NAME    run the function a setting names with choices = \"NAME\", and print its list\n"
           "  --arrive N        the card is off screen until N seconds in, as if you were on another card\n"
           "  --no-front        switch the card's \"May come to the front\" setting off\n");
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
    std::vector<Hold> holds;
    std::string kind;
    std::map<std::string, std::string> settings;
    std::vector<Message> messages;
    std::vector<std::string> choiceFunctions;
    uint32_t arriveMs = 0;
    std::string startTime;
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
        } else if ((arg == "--reply-status" || arg == "--reply-header") && hasValue) {
            const std::string pair = argv[++i];
            const size_t equals = pair.find('=');
            const std::string value = equals == std::string::npos ? "" : pair.substr(equals + 1);
            if (arg == "--reply-status" && atoi(value.c_str()) >= 100 && atoi(value.c_str()) <= 599) {
                services.replyStatuses.emplace_back(pair.substr(0, equals), atoi(value.c_str()));
            } else if (arg == "--reply-header" && value.find(':') != std::string::npos && value[0] != ':') {
                services.replyHeaders.emplace_back(pair.substr(0, equals), value);
            } else {
                usage();
                return 2;
            }
        } else if (arg == "--time" && hasValue) {
            startTime = argv[++i];
        } else if (arg == "--seed" && hasValue) {
            services.seeded = true;
            services.seededNumbers.seed((uint32_t)strtoul(argv[++i], nullptr, 10));
        } else if (arg == "--rssi" && hasValue) {
            services.rssi.clear();
            std::stringstream list(argv[++i]);
            std::string item;
            while (std::getline(list, item, ',')) services.rssi.push_back(atoi(item.c_str()));
            if (services.rssi.empty()) {
                usage();
                return 2;
            }
        } else if (arg == "--battery" && hasValue) {
            const char* value = argv[++i];
            const int percent = atoi(value);
            services.hasBattery = true;
            services.batteryStatus.percent = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
            services.batteryStatus.volts = 3.7f + services.batteryStatus.percent / 200.0f;
            services.batteryStatus.charging = value[strlen(value) - 1] == '+';
        } else if (arg == "--name" && hasValue) {
            services.name = argv[++i];
        } else if (arg == "--offline") {
            services.offline = true;
        } else if (arg == "--tz" && hasValue) {
            services.zone = argv[++i];
        } else if (arg == "--no-http") {
            services.liveHttp = false;
        } else if (arg == "--no-front") {
            services.mayFront = false;
        } else if (arg == "--arrive" && hasValue) {
            arriveMs = (uint32_t)(atof(argv[++i]) * 1000);
        } else if (arg == "--choices" && hasValue) {
            choiceFunctions.push_back(argv[++i]);
        } else if (arg == "--message" && hasValue) {
            const std::string pair = argv[++i];
            const size_t equals = pair.find('=');
            if (equals == std::string::npos) {
                usage();
                return 2;
            }
            Message message{(uint32_t)(atof(pair.c_str()) * 1000), pair.substr(equals + 1)};
            if (!message.body.empty() && message.body[0] == '@') {
                const std::string path = message.body.substr(1);
                std::ifstream file(path, std::ios::binary);
                if (!file) {
                    fprintf(stderr, "could not read %s\n", path.c_str());
                    return 1;
                }
                std::stringstream content;
                content << file.rdbuf();
                message.body = content.str();
            }
            if (message.body.size() > 4096) {
                fprintf(stderr, "that message is larger than 4 KB, the most the device takes\n");
                return 1;
            }
            messages.push_back(message);
        } else if (arg == "--set" && hasValue) {
            const std::string pair = argv[++i];
            const size_t equals = pair.find('=');
            if (equals == std::string::npos) {
                usage();
                return 2;
            }
            settings[pair.substr(0, equals)] = pair.substr(equals + 1);
        } else if (arg == "--store" && hasValue) {
            const std::string pair = argv[++i];
            const size_t equals = pair.find('=');
            if (equals == std::string::npos) {
                usage();
                return 2;
            }
            services.stored[pair.substr(0, equals)] = pair.substr(equals + 1);
        } else if (arg == "--hold" && hasValue) {
            std::stringstream list(argv[++i]);
            std::string item;
            while (std::getline(list, item, ',')) {
                const size_t at = item.find('@');
                const size_t dash = item.find('-', at == std::string::npos ? 0 : at);
                Hold hold{Button::Center, 0, 0};
                if (at == std::string::npos || dash == std::string::npos || !parseButton(item.substr(0, at), hold.button)) {
                    usage();
                    return 2;
                }
                hold.fromMs = (uint32_t)(atof(item.c_str() + at + 1) * 1000);
                hold.toMs = (uint32_t)(atof(item.c_str() + dash + 1) * 1000);
                holds.push_back(hold);
            }
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

    // --time, now that --tz is known whichever came first.
    if (!startTime.empty()) {
        int year, month, day, hour, minute, second = 0, used = 0;
        const bool allDigits = startTime.find_first_not_of("0123456789") == std::string::npos;
        if (allDigits) {
            services.startEpoch = (int64_t)strtoll(startTime.c_str(), nullptr, 10);
        } else if (sscanf(startTime.c_str(), "%d-%d-%d%*1[T ]%d:%d%n:%d%n", &year, &month, &day, &hour, &minute, &used, &second, &used) >= 5 &&
                   (startTime.substr(used).empty() || startTime.substr(used) == "Z")) {
            TimeZone zone;  // UTC
            if (startTime.back() != 'Z' && !zone.parse(services.zone.c_str())) {
                fprintf(stderr, "--tz is not a POSIX time zone (they look like CET-1CEST,M3.5.0,M10.5.0/3)\n");
                return 2;
            }
            services.startEpoch = zone.fromCivil(year, month, day, hour, minute, second);
        } else {
            fprintf(stderr, "--time takes 2026-11-17T14:05, 2026-11-17T14:05:30, either with a Z for UTC, or Unix seconds\n");
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
        // The web page asks for these when someone opens a setting that has `choices`.
        int choiceRequest = 1;
        for (const std::string& function : choiceFunctions) {
            printf("[choices] asking %s()\n", function.c_str());
            // With the form as --set filled it in, as the web page sends what it holds.
            LuaPlugin::ChoiceContext context;
            if (!plugin.kinds().empty()) context.kind = kind;
            context.settings = settings;
            plugin.choices(function, choiceRequest++, context);
        }

        const uint32_t endMs = (uint32_t)(seconds * 1000);
        size_t nextPress = 0;
        int frames = 0;
        bool onScreen = false;
        for (;;) {
            // The card is the one on screen, unless --arrive keeps it away for a while.
            if (!onScreen && services.now >= arriveMs) {
                onScreen = true;
                if (arriveMs) printf("[screen] at %.2fs: you arrive at the card\n", services.now / 1000.0);
            }
            plugin.setVisible(card, onScreen);

            for (Message& message : messages) {
                if (message.atMs > services.now || message.atMs == UINT32_MAX) continue;
                message.atMs = UINT32_MAX;  // sent
                // To the card, as POST /api/cards/message does it. A plugin with card
                // kinds may only take messages for the connection: then that way.
                bool taken = plugin.message(card, message.body);
                if (!taken && !plugin.failed()) taken = plugin.message("", message.body);
                printf("[message] at %.2fs: %s\n", services.now / 1000.0,
                       taken ? "delivered" : plugin.failed() ? "the plugin crashed on it"
                                                             : "not delivered: the plugin has no message() function");
            }

            // Network answers arrive on the frame after the request, as on the device.
            std::vector<SimServices::Pending> answers;
            answers.swap(services.answers);
            for (SimServices::Pending& answer : answers) plugin.httpResponse(answer.id, std::move(answer.response));

            while (nextPress < presses.size() && presses[nextPress].atMs <= services.now) {
                const Press& press = presses[nextPress++];
                static const char* const names[] = {"down", "center", "up"};
                const bool used = plugin.button(card, press.button);
                if (used && onScreen) services.attention.busy(services.now);  // the card is being played
                printf("[button] %s at %.2fs%s\n", names[(int)press.button], services.now / 1000.0,
                       used ? "" : " (not used by the plugin: the card stack would move)");
            }

            for (bool& held : services.held) held = false;
            for (const Hold& hold : holds) {
                if (services.now >= hold.fromMs && services.now < hold.toMs) services.held[(int)hold.button] = true;
            }

            bool anyHeld = false;
            for (const bool held : services.held) anyHeld = anyHeld || held;
            if (anyHeld && onScreen) services.attention.busy(services.now);

            // What the device makes of notify(): see Attention.h.
            const Attention::Actions actions = services.attention.tick(
                onScreen ? card : std::string(), services.now, [&services](const std::string&) { return services.mayFront; });
            if (!actions.show.empty() && !onScreen) {
                onScreen = true;
                plugin.setVisible(card, true);
                printf("[screen] at %.2fs: the card comes to the front\n", services.now / 1000.0);
            }
            if (!actions.repaint.empty()) plugin.repaint(card);
            if (actions.led) printf("[led] flash #%06x\n", (unsigned)actions.ledRgb);

            plugin.tick(services.now);
            if (plugin.needsDraw(card)) {
                gfx.beginFrame();
                plugin.draw(card);
                if (const std::string* banner = services.attention.banner(card)) drawBanner(gfx, *banner);
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
