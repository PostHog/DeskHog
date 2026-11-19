// Desktop tests for the Lua plugin runtime. Run with test/native/run.sh.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "Attention.h"
#include "DeskRules.h"
#include "HttpRedirect.h"
#include "LuaPlugin.h"
#include "PixelRuns.h"
#include "SourceHash.h"
#include "TimeZone.h"
#include "MockHost.h"

using namespace deskhog;

namespace {

int g_checks = 0;
int g_failures = 0;
std::string g_pluginDir;

struct TestCase {
    const char* name;
    std::function<void()> body;
};

std::vector<TestCase>& tests() {
    static std::vector<TestCase> all;
    return all;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> body) { tests().push_back({name, std::move(body)}); }
};

#define TEST(name)                                   \
    static void name();                              \
    static Registrar registrar_##name(#name, name);  \
    static void name()

#define CHECK(condition)                                                        \
    do {                                                                        \
        g_checks++;                                                             \
        if (!(condition)) {                                                     \
            g_failures++;                                                       \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);       \
        }                                                                       \
    } while (0)

#define CHECK_EQ(actual, expected)                                                               \
    do {                                                                                         \
        g_checks++;                                                                              \
        const std::string a = toText(actual), e = toText(expected);                              \
        if (a != e) {                                                                            \
            g_failures++;                                                                        \
            printf("  FAIL %s:%d  %s\n    got:      %s\n    expected: %s\n", __FILE__, __LINE__, \
                   #actual, a.c_str(), e.c_str());                                               \
        }                                                                                        \
    } while (0)

std::string toText(const std::string& v) { return v; }
std::string toText(const char* v) { return v; }
std::string toText(bool v) { return v ? "true" : "false"; }
template <typename T>
std::string toText(T v) {
    return std::to_string(v);
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/** A plugin wired to the fake device, showing one card (id "1"). */
struct Fixture {
    RecordingGfx gfx;
    MockServices services;
    LuaPlugin plugin;

    Fixture() : plugin(gfx, services) {}
    explicit Fixture(const LuaPlugin::Limits& limits) : plugin(gfx, services, limits, LuaPlugin::Memory()) {}

    bool start(const std::string& source, const LuaPlugin::Settings& settings = {}) {
        return begin("test", source, settings);
    }
    bool startFile(const std::string& name, const LuaPlugin::Settings& settings = {},
                   const LuaPlugin::Settings& cardSettings = {}) {
        const std::string source = readFile(g_pluginDir + "/" + name + ".lua");
        if (source.empty()) printf("  could not read plugin %s\n", name.c_str());
        return begin(name, source, settings, cardSettings);
    }
    bool begin(const std::string& name, const std::string& source, const LuaPlugin::Settings& settings,
               const LuaPlugin::Settings& cardSettings = {}) {
        plugin.start(name, source.data(), source.size(), settings);
        // The first kind: "main" for a single-card plugin.
        const std::string kind = plugin.kinds().empty() ? LuaPlugin::kMainKind : plugin.kinds()[0].key;
        plugin.addCard("1", kind, cardSettings, gfx);
        return !plugin.failed();
    }
    void draw() {
        gfx.calls.clear();
        plugin.draw("1");
    }
    void tick(uint32_t now, bool visible) {
        plugin.setVisible("1", visible);
        plugin.tick(now);
    }
    bool button(Button which) { return plugin.button("1", which); }
    bool needsDraw() const { return plugin.needsDraw("1"); }
    /** Run `seconds` of frames while visible. */
    void runFrames(float seconds) {
        const int frames = (int)(seconds * 1000.0f / 33.0f);
        for (int i = 0; i < frames && !plugin.failed(); i++) {
            services.now += 33;
            tick(services.now, true);
            if (needsDraw()) draw();
        }
    }
    void respond(size_t index, int status, const std::string& body, const std::string& error = "") {
        HttpResponse response;
        response.status = status;
        response.body = body;
        response.error = error;
        plugin.httpResponse(services.requests.at(index).id, response);
    }
};

// ------------------------------------------------------------------ drawing

// What a redirect leads to: the method and address of the next request, or "" if it is not followed.
static std::string redirected(int status, std::string method, std::string url, const std::string& location) {
    if (!deskhog::followRedirect(status, method, url, location)) return "";
    return method + " " + url;
}

TEST(redirects_are_followed_to_where_they_point) {
    // A whole address, also to another host, and from http up to https.
    CHECK_EQ(redirected(307, "GET", "https://example.com", "https://www.example.com/"), "GET https://www.example.com/");
    CHECK_EQ(redirected(301, "GET", "http://example.com/a", "https://example.com/a"), "GET https://example.com/a");
    CHECK_EQ(redirected(308, "HEAD", "https://example.com/a", "HTTPS://Example.com/b"), "HEAD https://Example.com/b");
    // Only a path, or a path next to the one that answered.
    CHECK_EQ(redirected(302, "GET", "https://example.com/a/b?c=1", "/d?e=2"), "GET https://example.com/d?e=2");
    CHECK_EQ(redirected(302, "GET", "https://example.com:8443/a/b?c=/x", "d"), "GET https://example.com:8443/a/d");
    CHECK_EQ(redirected(302, "GET", "https://example.com", "d"), "GET https://example.com/d");
    CHECK_EQ(redirected(302, "GET", "https://example.com?q=1", "/d"), "GET https://example.com/d");
    CHECK_EQ(redirected(302, "GET", "http://example.com/a", "//other.example/b#top"), "GET http://other.example/b");
    // After a POST the browser way: 302 and 303 become a GET, the others are the plugin's answer.
    CHECK_EQ(redirected(303, "POST", "https://example.com/a", "/done"), "GET https://example.com/done");
    CHECK_EQ(redirected(302, "PUT", "https://example.com/a", "/done"), "GET https://example.com/done");
    CHECK_EQ(redirected(307, "POST", "https://example.com/a", "/b"), "");
    CHECK_EQ(redirected(301, "DELETE", "https://example.com/a", "/b"), "");
    CHECK_EQ(redirected(303, "HEAD", "https://example.com/a", "/b"), "HEAD https://example.com/b");
}

TEST(redirects_that_are_not_followed) {
    CHECK_EQ(redirected(200, "GET", "https://example.com/a", "/b"), "");
    CHECK_EQ(redirected(304, "GET", "https://example.com/a", "/b"), "");
    CHECK_EQ(redirected(302, "GET", "https://example.com/a", ""), "");
    // Not from https back to http, and not to something that is no web address.
    CHECK_EQ(redirected(302, "GET", "https://example.com/a", "http://example.com/a"), "");
    CHECK_EQ(redirected(302, "GET", "https://example.com/a", "ftp://example.com/a"), "");
    CHECK_EQ(redirected(302, "GET", "http://example.com/a", "/b?next=ftp://x"), "GET http://example.com/b?next=ftp://x");
}

TEST(credentials_stay_with_the_host_they_were_meant_for) {
    CHECK_EQ(deskhog::urlOrigin("https://Example.com:8443/a?b"), "https://example.com:8443");
    CHECK_EQ(deskhog::urlOrigin("http://example.com"), "http://example.com");
    CHECK_EQ(deskhog::urlOrigin("example.com"), "");
    CHECK(deskhog::urlOrigin("https://example.com/a") != deskhog::urlOrigin("https://www.example.com/a"));
    CHECK(deskhog::isCredentialHeader("Authorization"));
    CHECK(deskhog::isCredentialHeader("cookie"));
    CHECK(!deskhog::isCredentialHeader("Accept"));
}

TEST(draws_with_gfx) {
    Fixture f;
    CHECK(f.start(R"(
        function draw()
          gfx.clear("#102030")
          gfx.rect(10, 20, 30, 40, "red", 4)
          gfx.outline(1, 2, 3, 4, 0x00FF00, 2)
          gfx.line(0, 0, 10.4, 10.6, "#fff")
          gfx.circle(50, 50, 5, "blue")
          gfx.pixel(7, 8)
          gfx.text(116, 10, "Hi", "white", "large", "center")
        end
    )"));
    f.draw();
    CHECK(!f.plugin.failed());
    CHECK(f.gfx.drew("rect 0,0 233x135 #102030 r0"));
    CHECK(f.gfx.drew("rect 10,20 30x40 #e74c3c r4"));
    CHECK(f.gfx.drew("outline 1,2 3x4 #00ff00 w2"));
    CHECK(f.gfx.drew("line 0,0-10,11 #ffffff w1"));
    CHECK(f.gfx.drew("rect 45,45 11x11 #3498db r6"));
    CHECK(f.gfx.drew("rect 7,8 1x1 #ffffff r0"));
    CHECK(f.gfx.drew("text 116,10 #ffffff large center Hi"));
}

TEST(exposes_screen_size_and_text_metrics) {
    Fixture f;
    CHECK(f.start(R"(
        w, h = gfx.width, gfx.height
        tw = gfx.textWidth("abc", "small")
        th = gfx.textHeight("large")
        function draw() gfx.text(0, 0, w .. "x" .. h .. " " .. tw .. " " .. th) end
    )"));
    f.draw();
    CHECK(f.gfx.drew("233x135 24 44"));
}

TEST(ui_labels_wrap) {
    Fixture f;
    // The fake small font is 8 pixels a character: 27 characters fit the card.
    CHECK(f.start(R"(
        function draw()
          ui.title("Status")
          ui.label("Set the address and the token under Connections", "orange")
          ui.label("Short one")
        end
    )"));
    f.draw();
    CHECK(f.gfx.drew("small center Set the address and the"));
    CHECK(f.gfx.drew("small center token under Connections"));
    CHECK(f.gfx.drew("small center Short one"));
    CHECK(!f.plugin.failed());
}

TEST(wraps_text_to_a_width) {
    Fixture f;
    // The fake small font is 8 pixels a character, so 80 pixels holds ten.
    CHECK(f.start(R"(
        lines = gfx.wrap("the quick brown fox jumps", 80, "small")
        long = gfx.wrap("supercalifragilistic", 80, "small")
        split = gfx.wrap("one\ntwo", 80, "small")
        none = gfx.wrap("", 80, "small")
        function draw()
          gfx.text(0, 0, table.concat(lines, "|"))
          gfx.text(0, 20, table.concat(long, "|"))
          gfx.text(0, 40, table.concat(split, "|") .. " " .. #none)
        end
    )"));
    f.draw();
    CHECK(f.gfx.drew("the quick|brown fox|jumps"));
    CHECK(f.gfx.drew("supercalif|ragilistic"));  // a word wider than the line is cut
    CHECK(f.gfx.drew("one|two 0"));
}

TEST(clips_drawing_to_a_rectangle) {
    Fixture f;
    CHECK(f.start(R"(
        function draw()
          gfx.clip(10, 20, 100, 50)
          gfx.rect(0, 0, 200, 200, "red")
          gfx.clip(5, 5, 0, 10)
          gfx.clip()
        end
    )"));
    f.draw();
    CHECK(!f.plugin.failed());
    // calls[0] is the black background every frame starts with
    CHECK_EQ(f.gfx.calls.at(1), "clip 10,20 100x50");
    CHECK_EQ(f.gfx.calls.at(3), "clip -1,-1 1x1");  // an empty rectangle hides everything
    CHECK_EQ(f.gfx.calls.at(4), "clip off");
}

TEST(draws_images_made_of_text) {
    Fixture f;
    CHECK(f.start(R"(
        dot = gfx.image({ "ab", "b.", colors = { a = "#ff0000", b = "blue", ["."] = "none" } })
        size = dot.width .. "x" .. dot.height
        function draw()
          gfx.text(0, 0, size)
          gfx.draw(dot, 5, 6)
          gfx.draw(dot, 7, 8, 3)
          gfx.draw(dot, 0, 0, 99)
        end
    )"));
    f.draw();
    CHECK(!f.plugin.failed());
    CHECK(f.gfx.drew("2x2"));
    CHECK(f.gfx.drew("image 5,6 2x2 x1 ffff0000..00000000"));
    CHECK(f.gfx.drew("image 7,8 2x2 x3 ffff0000..00000000"));
    CHECK(f.gfx.drew("image 0,0 2x2 x8"));  // the scale is kept sensible
}

TEST(image_mistakes_are_explained) {
    Fixture a;
    CHECK(!a.start(R"(gfx.image({ "ab", "a", colors = { a = "red", b = "blue" } }))"));
    CHECK(contains(a.plugin.error(), "row 2 has 1 characters, the first has 2"));
    Fixture b;
    CHECK(!b.start(R"(gfx.image({ "ax", colors = { a = "red" } }))"));
    CHECK(contains(b.plugin.error(), "row 1 uses 'x', which is not in `colors`"));
    Fixture c;
    CHECK(!c.start(R"(gfx.image({ "a" }))"));
    CHECK(contains(c.plugin.error(), "give it a `colors` table"));
    Fixture d;
    CHECK(d.start(R"(function draw() gfx.draw({ id = 3 }, 0, 0) end)"));
    d.draw();
    CHECK(contains(d.plugin.error(), "not an image made with gfx.image()"));
}

TEST(images_are_limited_per_plugin) {
    LuaPlugin::Limits limits;
    limits.imageBytes = 4 * 1024;  // room for 1024 pixels
    Fixture f(limits);
    CHECK(!f.start(R"(
        local row = string.rep("a", 30)
        local rows = { colors = { a = "red" } }
        for i = 1, 30 do rows[i] = row end
        made = 0
        for i = 1, 5 do gfx.image(rows); made = made + 1 end
    )"));
    CHECK(contains(f.plugin.error(), "too many or too large images"));
}

TEST(update_can_say_nothing_changed) {
    Fixture f;
    CHECK(f.start(R"(
        plugin = { fps = 10 }
        moving = false
        function update(dt) if not moving then return false end end
        function draw() gfx.text(0, 0, "x") end
        function button() moving = true; return true end
    )"));
    f.services.now += 200;
    f.tick(f.services.now, true);
    f.draw();
    f.services.now += 200;
    f.tick(f.services.now, true);
    CHECK(!f.needsDraw());  // update() returned false: no repaint
    f.button(Button::Center);
    f.draw();
    f.services.now += 200;
    f.tick(f.services.now, true);
    CHECK(f.needsDraw());   // returning nothing means "repaint", as before
}

TEST(ui_lays_out_a_data_screen) {
    Fixture f;
    CHECK(f.start(R"(
        function draw()
          ui.title("Antwerp")
          ui.big("21°")
          ui.label("Cloudy")
          ui.chart({1, 2, 3})
        end
    )"));
    f.draw();
    CHECK(!f.plugin.failed());
    CHECK(f.gfx.drew("text 8,6 #aaaaaa small left Antwerp"));
    // With the fake font sizes: the title takes 6..24 and the chart 99..129, so
    // the big value (44px) and the label (18px) are centred between 24 and 95.
    CHECK(f.gfx.drew("text 116,27 #ffffff large center 21°"));
    CHECK(f.gfx.drew("text 116,73 #aaaaaa small center Cloudy"));
    CHECK(f.gfx.drew("line 8,128-116,113 #3498db w2"));
    CHECK(f.gfx.drew("line 116,113-224,99 #3498db w2"));
}

TEST(ui_big_steps_down_when_the_rows_do_not_fit) {
    Fixture f;
    CHECK(f.start(R"(
        function draw()
          ui.title("Busy")
          ui.big("42")
          ui.label("one")
          ui.label("two")
          ui.chart({ 3, 1, 2 })
        end
    )"));
    f.draw();
    CHECK(f.gfx.drew("medium center 42"));
    CHECK(f.gfx.drew("small center two"));
}

TEST(ui_big_shrinks_when_too_wide) {
    Fixture f;
    CHECK(f.start(R"(function draw() ui.big("123,456,789,000") end)"));
    f.draw();
    CHECK(f.gfx.drew("medium center 123,456,789,000"));
}

TEST(drawing_outside_draw_is_an_error) {
    Fixture f;
    CHECK(!f.start(R"(function load() gfx.rect(0, 0, 1, 1) end)"));
    CHECK(f.plugin.failed());
    CHECK(contains(f.plugin.error(), "gfx.rect() only works inside draw()"));
}

TEST(bad_color_is_reported) {
    Fixture f;
    CHECK(f.start(R"(function draw() gfx.rect(0, 0, 1, 1, "nope") end)"));
    f.draw();
    CHECK(f.plugin.failed());
    CHECK(contains(f.plugin.error(), "not a color"));
}

TEST(gfx_color_says_whether_something_is_a_colour) {
    Fixture f;
    CHECK(f.start(R"(
        print(gfx.color("red"), gfx.color("#fff"), gfx.color("#FF8800"), gfx.color(0x102030), gfx.color(0x1FFFFFF))
        print(gfx.color("nope"), gfx.color("#ff"), gfx.color("#ggg"), gfx.color("Red"), gfx.color(""))
        print(gfx.color(nil), gfx.color(), gfx.color({}), gfx.color(true), gfx.color(1.5), gfx.color(print))
        function draw() gfx.rect(0, 0, 1, 1, gfx.color("orange")) end   -- what comes back is a colour itself
    )"));
    CHECK(f.services.logged("15158332 16777215 16746496 1056816 16777215"));
    CHECK(f.services.logged("nil nil nil nil nil"));
    CHECK(f.services.logged("nil nil nil nil nil nil"));
    f.draw();
    CHECK(f.gfx.drew("#e67e22"));
    CHECK(!f.plugin.failed());
}

// ---------------------------------------------------------------- lifecycle

TEST(runs_load_once_at_start_and_again_on_refresh) {
    Fixture f;
    CHECK(f.start(R"(
        plugin = { name = "Counter", refresh = 60 }
        loads = 0
        function load() loads = loads + 1 end
        function draw() gfx.text(0, 0, "loads " .. loads) end
    )"));
    CHECK_EQ(f.plugin.title(), "Counter");
    f.draw();
    CHECK(f.gfx.drew("loads 1"));

    f.services.now += 59000;
    f.tick(f.services.now, false);
    CHECK(!f.needsDraw());

    f.services.now += 1000;
    f.tick(f.services.now, false);  // refresh also happens while the card is hidden
    CHECK(f.needsDraw());
    f.draw();
    CHECK(f.gfx.drew("loads 2"));
}

TEST(refresh_now_runs_load_on_the_next_tick) {
    Fixture f;
    CHECK(f.start(R"(
        loads = 0
        function load() loads = loads + 1 end
        function draw() gfx.text(0, 0, "loads " .. loads) end
    )"));
    f.draw();
    f.tick(f.services.now + 10, false);
    CHECK(!f.needsDraw());   // no refresh interval: load() ran once and stays that way

    f.plugin.refreshNow();          // e.g. WiFi just connected
    f.tick(f.services.now + 20, false);
    f.tick(f.services.now + 30, false);
    f.draw();
    CHECK(f.gfx.drew("loads 2"));   // once, not on every tick after
}

TEST(update_runs_only_while_visible_at_the_requested_rate) {
    Fixture f;
    CHECK(f.start(R"(
        plugin = { fps = 10 }
        frames, total = 0, 0
        function update(dt) frames = frames + 1; total = total + dt end
        function draw() gfx.text(0, 0, string.format("%d %.1f", frames, total)) end
    )"));
    for (int i = 0; i < 100; i++) {  // one second, hidden
        f.services.now += 10;
        f.tick(f.services.now, false);
    }
    f.draw();
    CHECK(f.gfx.drew("0 0.0"));

    for (int i = 0; i <= 100; i++) {  // one second, visible
        f.services.now += 10;
        f.tick(f.services.now, true);
    }
    f.draw();
    CHECK(f.gfx.drew("10 1.0"));
}

TEST(update_dt_is_capped_after_a_stall) {
    Fixture f;
    CHECK(f.start(R"(
        biggest = 0
        function update(dt) biggest = math.max(biggest, dt) end
        function draw() gfx.text(0, 0, string.format("%.2f", biggest)) end
    )"));
    f.tick(f.services.now, true);
    f.services.now += 5000;
    f.tick(f.services.now, true);
    f.draw();
    CHECK(f.gfx.drew("0.25"));
}

TEST(button_reports_whether_the_press_was_used) {
    Fixture f;
    CHECK(f.start(R"(
        last = "none"
        function button(name) last = name; return name == "center" end
        function draw() gfx.text(0, 0, last) end
    )"));
    CHECK_EQ(f.button(Button::Center), true);
    CHECK(f.needsDraw());
    CHECK_EQ(f.button(Button::Up), false);
    CHECK_EQ(f.button(Button::Down), false);
    f.draw();
    CHECK(f.gfx.drew("down"));
}

TEST(plugin_without_button_function_leaves_navigation_alone) {
    Fixture f;
    CHECK(f.start("function draw() end"));
    CHECK_EQ(f.button(Button::Center), false);
}

// ------------------------------------------------------------------- config

TEST(config_uses_defaults_and_saved_settings) {
    Fixture f;
    const char* source = R"(
        config = {
          city = { label = "City", default = "Antwerp" },
          days = { label = "Days", default = 3 },
          metric = { default = true },
          short = "plain",
        }
        function draw()
          gfx.text(0, 0, string.format("%s %s %s %s %s", config.city, config.days, math.type(config.days),
                                       tostring(config.metric), config.short))
        end
    )";
    CHECK(f.start(source));
    f.draw();
    CHECK(f.gfx.drew("Antwerp 3 integer true plain"));
    CHECK_EQ(f.plugin.configSchema().size(), (size_t)4);

    bool sawCity = false;
    for (const LuaPlugin::ConfigField& field : f.plugin.configSchema()) {
        if (field.key != "city") continue;
        sawCity = true;
        CHECK_EQ(field.label, "City");
        CHECK_EQ(field.type, "string");
        CHECK_EQ(field.defaultValue, "Antwerp");
    }
    CHECK(sawCity);

    Fixture g;
    CHECK(g.start(source, {{"city", "Antwerp"}, {"days", "7"}, {"metric", "false"}, {"short", "changed"}}));
    g.draw();
    CHECK(g.gfx.drew("Antwerp 7 integer false changed"));

    Fixture h;  // a saved value that is not a number keeps the default
    CHECK(h.start(source, {{"days", "lots"}}));
    h.draw();
    CHECK(h.gfx.drew("Antwerp 3 integer"));
}

// --------------------------------------------------------------------- http

TEST(http_get_delivers_decoded_json) {
    Fixture f;
    CHECK(f.start(R"(
        text = "waiting"
        function load()
          http.get("https://example.com/a?b=1", { headers = { Authorization = "Bearer x" }, timeout = 5 }, function(res)
            text = string.format("%s %d %s %d", tostring(res.ok), res.status, res.json.name, #res.json.list)
          end)
        end
        function draw() gfx.text(0, 0, text) end
    )"));
    CHECK_EQ(f.services.requests.size(), (size_t)1);
    const HttpRequest& request = f.services.requests[0].request;
    CHECK_EQ(request.method, "GET");
    CHECK_EQ(request.url, "https://example.com/a?b=1");
    CHECK_EQ(request.timeoutMs, (uint32_t)5000);
    CHECK_EQ(request.headers.size(), (size_t)1);
    CHECK_EQ(request.headers[0].first, "Authorization");

    f.draw();
    CHECK(!f.needsDraw());
    f.respond(0, 200, R"({"name":"hog","list":[1,2,3]})");
    CHECK(f.needsDraw());
    f.draw();
    CHECK(f.gfx.drew("true 200 hog 3"));
}

TEST(http_failure_reaches_the_callback) {
    Fixture f;
    CHECK(f.start(R"(
        text = "waiting"
        function load()
          http.get("http://example.com", function(res)
            text = tostring(res.ok) .. " " .. res.status .. " " .. tostring(res.error) .. " " .. tostring(res.json)
          end)
        end
        function draw() gfx.text(0, 0, text) end
    )"));
    f.respond(0, 0, "", "connection refused");
    f.draw();
    CHECK(f.gfx.drew("false 0 connection refused nil"));
}

TEST(http_post_encodes_tables_as_json) {
    Fixture f;
    CHECK(f.start(R"(
        function load()
          http.post("https://example.com/hook", { event = "press", n = 2 }, function() end)
          http.post("https://example.com/raw", "plain text", function() end)
        end
    )"));
    CHECK_EQ(f.services.requests.size(), (size_t)2);
    const HttpRequest& json = f.services.requests[0].request;
    CHECK_EQ(json.method, "POST");
    CHECK(json.body == R"({"event":"press","n":2})" || json.body == R"({"n":2,"event":"press"})");
    CHECK_EQ(json.headers.size(), (size_t)1);
    CHECK_EQ(json.headers[0].second, "application/json");
    CHECK_EQ(f.services.requests[1].request.body, "plain text");
    CHECK_EQ(f.services.requests[1].request.headers.size(), (size_t)0);
}

TEST(http_setup_errors_leave_nothing_behind) {
    Fixture f;  // a body that cannot be sent, and options that blow up when read
    CHECK(f.start(R"(
        local first, why1 = pcall(http.post, "http://example.com", { fn = print }, function() end)
        local trap = setmetatable({}, { __index = function() error("trap") end })
        local second, why2 = pcall(http.get, "http://example.com", trap, function() end)
        http.get("http://example.com/ok", function() end)
        function draw()
          gfx.text(0, 0, tostring(first) .. " " .. why1)
          gfx.text(0, 20, tostring(second) .. " " .. why2)
        end
    )"));
    f.draw();
    CHECK(f.gfx.drew("false http: cannot encode this type"));
    CHECK(f.gfx.drew("trap"));
    // Neither failed call took a request slot or reached the network.
    CHECK_EQ(f.services.requests.size(), (size_t)1);
    CHECK_EQ(f.services.requests[0].request.url, "http://example.com/ok");
}

TEST(http_rejects_bad_urls_and_floods) {
    Fixture f;
    CHECK(!f.start(R"(function load() http.get("ftp://example.com", function() end) end)"));
    CHECK(contains(f.plugin.error(), "must start with http"));

    Fixture g;
    CHECK(!g.start(R"(function load() for i = 1, 13 do http.get("http://example.com", function() end) end end)"));
    CHECK(contains(g.plugin.error(), "too many requests in flight (max 12)"));
}

TEST(http_answer_after_a_crash_is_ignored) {
    Fixture f;
    CHECK(f.start(R"(
        function load() http.get("http://example.com", function() end) end
        function draw() error("boom") end
    )"));
    f.draw();
    CHECK(f.plugin.failed());
    f.respond(0, 200, "{}");  // must not touch the closed Lua state
    CHECK(f.plugin.failed());
}

// --------------------------------------------------------------------- json

TEST(json_round_trip) {
    Fixture f;
    CHECK(f.start(R"(
        local v = json.decode('{"a":[1,2.5,"x\\u00e9\\ud83d\\ude00",true,null,{"b":-3}],"big":4000000000,"n":null}')
        out = {
          math.type(v.a[1]), v.a[2], v.a[3], tostring(v.a[4]), tostring(v.a[5] == json.null),
          v.a[6].b, math.type(v.big), tostring(v.n), #v.a,
        }
        encoded = json.encode({ list = { 1, "two", false, json.null }, })
        bad, why = json.decode("{oops}")
        function draw()
          gfx.text(0, 0, table.concat(out, "|"))
          gfx.text(0, 20, encoded)
          gfx.text(0, 40, tostring(bad) .. " " .. why)
        end
    )"));
    f.draw();
    CHECK(!f.plugin.failed());
    CHECK(f.gfx.drew("integer|2.5|x\xC3\xA9\xF0\x9F\x98\x80|true|true|-3|float|nil|6"));
    CHECK(f.gfx.drew(R"({"list":[1,"two",false,null]})"));
    CHECK(f.gfx.drew("nil bad object"));
}

TEST(json_numbers_and_size_limit) {
    Fixture f;
    CHECK(f.start(R"(
        short = json.encode({ 0.1, 2.5, 1/3, 100, -7 })
        back = json.decode(json.encode({ 1/3 }))[1] == 1/3
        ok, why = pcall(json.encode, { string.rep("x", 40000), string.rep("y", 40000) })
        function draw()
          gfx.text(0, 0, short)
          gfx.text(0, 20, tostring(back) .. " " .. tostring(ok) .. " " .. why)
        end
    )"));
    f.draw();
    CHECK(!f.plugin.failed());
    CHECK(f.gfx.drew("[0.1,2.5,0.33333334,100,-7]"));  // the shortest text that reads back as the same float
    CHECK(f.gfx.drew("true false json.encode: result is larger than 64 KB"));
}

TEST(json_rejects_garbage_without_crashing) {
    const char* inputs[] = {"", "[", "[1,", "{\"a\"", "{\"a\":}", "\"abc", "\"\\u12\"", "\"\\ud800x\"", "tru",
                            "01x", "1.e5", "[1]]", "nul", "{\"a\":1,}", "\x01"};
    for (const char* input : inputs) {
        Fixture f;
        f.services.stored["v"] = input;
        CHECK(f.start(R"(
            got = store.get("v", "fallback")
            function draw() gfx.text(0, 0, tostring(got)) end
        )"));
        f.draw();
        CHECK(f.gfx.drew("fallback"));
    }

    // 100 levels of nesting is refused instead of overflowing the C stack.
    Fixture f;
    f.services.stored["v"] = std::string(100, '[') + std::string(100, ']');
    CHECK(f.start(R"(got = store.get("v", "fallback") function draw() gfx.text(0, 0, tostring(got)) end)"));
    f.draw();
    CHECK(f.gfx.drew("fallback"));
}

// -------------------------------------------------------------------- store

TEST(store_time_does_not_count_against_the_budget) {
    // The fake clock moves 1 ms per reading. Pretend each flash write takes 400 ms.
    struct SlowStore : MockServices {
        bool storeSet(const std::string& key, const std::string& json) override {
            now += 400;
            return MockServices::storeSet(key, json);
        }
    };
    RecordingGfx gfx;
    SlowStore services;
    LuaPlugin plugin(gfx, services);
    const std::string source = R"(function button() store.set("a", 1) store.set("b", 2) return true end)";
    CHECK(plugin.start("test", source.data(), source.size()));
    plugin.addCard("1", LuaPlugin::kMainKind, {}, gfx);
    CHECK_EQ(plugin.button("1", Button::Center), true);  // 800 ms of flash time, 250 ms budget
    CHECK(!plugin.failed());
}

TEST(store_keeps_values) {
    Fixture f;
    CHECK(f.start(R"(
        store.set("best", 12)
        store.set("who", { name = "Max" })
        function draw()
          gfx.text(0, 0, store.get("best") .. " " .. store.get("who").name .. " " .. store.get("missing", "none"))
        end
    )"));
    CHECK_EQ(f.services.stored["best"], "12");
    f.draw();
    CHECK(f.gfx.drew("12 Max none"));
}

// ---------------------------------------------------------------- led, time

TEST(led_time_input_and_print) {
    Fixture f;
    f.services.held[(int)Button::Up] = true;
    CHECK(f.start(R"(
        led.set("#ff8800")
        print("hello", 42, true)
        function draw()
          gfx.text(0, 0, time.now() .. " " .. time.ms() .. " " .. tostring(input.held("up")) .. tostring(input.held("down")))
        end
    )"));
    CHECK_EQ(f.services.led, "#ff8800");
    CHECK(f.services.logged("hello 42 true"));
    f.draw();
    CHECK(f.gfx.drew("1763200000 1000 truefalse"));

    Fixture g;
    CHECK(g.start(R"(led.set(1, 2, 3, 1.5))"));
    CHECK_EQ(g.services.led, "#010203 for 1500ms");

    Fixture h;
    CHECK(h.start(R"(led.set("green", 3) led.off())"));
    CHECK_EQ(h.services.led, "default");
}

// ------------------------------------------------------------------- safety

TEST(syntax_error_names_the_line) {
    Fixture f;
    CHECK(!f.start("x = 1\nfunction draw(\n"));
    CHECK(f.plugin.failed());
    CHECK(contains(f.plugin.error(), "test:3:"));
    CHECK(f.services.logged("error: test:3:"));
}

TEST(runtime_error_paints_the_error_card) {
    Fixture f;
    CHECK(f.start(R"(
        plugin = { name = "Broken" }
        function draw()
          local t = nil
          gfx.text(0, 0, t.field)
        end
    )"));
    f.draw();
    CHECK(f.plugin.failed());
    CHECK(contains(f.plugin.error(), "test:5:"));
    CHECK(f.gfx.drew("#e74c3c small left Broken crashed"));
    CHECK(f.gfx.drew("attempt to index a"));
    CHECK(f.gfx.drew("nil value"));
    CHECK(!f.needsDraw());
    CHECK_EQ(f.plugin.memoryUsed(), (size_t)0);  // the Lua state is gone
    f.tick(f.services.now + 100, true);   // and later calls are harmless
    CHECK_EQ(f.button(Button::Center), false);
}

TEST(long_error_messages_wrap_inside_the_card) {
    Fixture f;
    CHECK(f.start(R"(function draw() error("one two three four five six seven eight nine ten eleven twelve thirteen") end)"));
    f.draw();
    int lines = 0;
    for (const std::string& call : f.gfx.calls) {
        if (!contains(call, "#cccccc")) continue;
        lines++;
        // 27 characters of 8px fit in 233 - 16.
        const std::string text = call.substr(call.find(" left ") + 6);
        CHECK(text.size() <= 27);
    }
    CHECK(lines >= 3);
}

TEST(infinite_loop_is_stopped) {
    Fixture f;
    f.services.advancePerCall = 1;
    CHECK(f.start("function update() while true do end end"));
    f.tick(f.services.now, true);
    f.tick(f.services.now + 100, true);
    CHECK(f.plugin.failed());
    CHECK(contains(f.plugin.error(), "took too long (limit 250 ms)"));
}

TEST(infinite_loop_cannot_hide_behind_pcall) {
    Fixture f;
    f.services.advancePerCall = 1;
    CHECK(!f.start(R"(
        while true do
          pcall(function() while true do end end)
        end
    )"));
    CHECK(contains(f.plugin.error(), "took too long (limit 2000 ms)"));

    Fixture g;  // same, inside a coroutine
    g.services.advancePerCall = 1;
    CHECK(!g.start(R"(
        local co = coroutine.wrap(function() while true do end end)
        while true do pcall(co) end
    )"));
    CHECK(contains(g.plugin.error(), "took too long"));
}

TEST(runaway_recursion_is_an_error_not_a_crash) {
    Fixture f;  // through pcall: hits Lua's cap on nested C calls
    CHECK(f.start(R"(
        local function dive() return pcall(dive) end
        ok, why = dive()
        function draw() gfx.text(0, 0, "survived") end
    )"));
    f.draw();
    CHECK(f.gfx.drew("survived"));

    Fixture g;  // plain Lua recursion: runs into the memory cap
    CHECK(!g.start(R"(
        local function dive(n) return 1 + dive(n + 1) end
        dive(1)
    )"));
    CHECK(contains(g.plugin.error(), "stack overflow") || contains(g.plugin.error(), "out of memory"));

    Fixture h;  // source nested deeper than the parser allows
    const std::string nested = "x = " + std::string(60, '(') + "1" + std::string(60, ')');
    CHECK(!h.start(nested));
    CHECK(contains(h.plugin.error(), "overflow"));
}

TEST(memory_cap_stops_a_greedy_plugin) {
    LuaPlugin::Limits limits;
    limits.memoryBytes = 128 * 1024;
    Fixture f(limits);
    CHECK(!f.start(R"(
        local hoard = {}
        for i = 1, 1000000 do hoard[i] = string.rep("x", 100) .. i end
    )"));
    CHECK(contains(f.plugin.error(), "out of memory (limit 128 KB)"));
    CHECK_EQ(f.plugin.memoryUsed(), (size_t)0);
}

TEST(huge_string_hits_the_memory_cap) {
    Fixture f;
    CHECK(!f.start(R"(local s = string.rep("x", 10000000))"));
    CHECK(contains(f.plugin.error(), "out of memory"));
}

TEST(sandbox_hides_the_dangerous_parts) {
    Fixture f;
    CHECK(f.start(R"(
        found = {}
        for _, name in ipairs({ "io", "os", "debug", "package", "require", "load", "loadfile", "dofile" }) do
          if _G[name] ~= nil then found[#found + 1] = name end
        end
        if string.dump then found[#found + 1] = "string.dump" end
        function draw() gfx.text(0, 0, "found:" .. table.concat(found, ",")) end
    )"));
    f.draw();
    CHECK(f.gfx.drew("found:"));
    CHECK(!f.gfx.drew("found:i") && !f.gfx.drew("found:o") && !f.gfx.drew("found:l") && !f.gfx.drew("found:s"));
}

TEST(bytecode_is_refused) {
    Fixture f;
    const std::string bytecode = std::string("\x1bLua", 4) + std::string(40, '\0');
    CHECK(!f.plugin.start("test", bytecode.data(), bytecode.size()));
    CHECK(contains(f.plugin.error(), "attempt to load a binary chunk"));
}

TEST(finalizers_are_refused) {
    // Lua runs __gc with the time-budget hook off, so a looping finalizer could
    // freeze the device. Registering one is an error instead.
    Fixture f;
    CHECK(!f.start(R"(keep = setmetatable({}, { __gc = function() while true do end end }))"));
    CHECK(contains(f.plugin.error(), "__gc finalizers are not available"));

    Fixture g;  // ordinary metatables still work
    CHECK(g.start(R"(
        local point = setmetatable({ x = 2 }, { __index = { double = function(self) return self.x * 2 end } })
        function draw() gfx.text(0, 0, "double " .. point:double() .. " " .. tostring(getmetatable(point) ~= nil)) end
    )"));
    g.draw();
    CHECK(g.gfx.drew("double 4 true"));
}

TEST(restart_replaces_the_running_plugin) {
    Fixture f;
    CHECK(f.start(R"(function draw() gfx.text(0, 0, "one") end)"));
    CHECK(f.start(R"(function draw() gfx.text(0, 0, "two") end)"));
    f.draw();
    CHECK(f.gfx.drew("two"));
    CHECK(!f.start("this is not lua"));
    CHECK(f.start(R"(function draw() gfx.text(0, 0, "three") end)"));
    CHECK(!f.plugin.failed());
    f.draw();
    CHECK(f.gfx.drew("three"));
}

// A counting allocator: everything the runtime takes it must give back.
size_t g_liveBytes = 0;
void* countingRealloc(void* ptr, size_t size) {
    size_t* header = ptr ? static_cast<size_t*>(ptr) - 2 : nullptr;
    const size_t oldSize = header ? header[0] : 0;
    size_t* fresh = static_cast<size_t*>(realloc(header, size + 2 * sizeof(size_t)));
    if (!fresh) return nullptr;
    fresh[0] = size;
    g_liveBytes = g_liveBytes - oldSize + size;
    return fresh + 2;
}
void countingFree(void* ptr) {
    if (!ptr) return;
    size_t* header = static_cast<size_t*>(ptr) - 2;
    g_liveBytes -= header[0];
    free(header);
}

TEST(lua_heap_is_fully_released) {
    g_liveBytes = 0;
    {
        RecordingGfx gfx;
        MockServices services;
        LuaPlugin::Memory memory;
        memory.realloc = countingRealloc;
        memory.free = countingFree;
        LuaPlugin plugin(gfx, services, LuaPlugin::Limits(), memory);
        const std::string source = readFile(g_pluginDir + "/flappy.lua");
        CHECK(plugin.start("flappy", source.data(), source.size()));
        plugin.addCard("1", LuaPlugin::kMainKind, {}, gfx);
        CHECK(g_liveBytes > 10000);
        CHECK_EQ(plugin.memoryUsed(), g_liveBytes);
        plugin.draw("1");
    }
    CHECK_EQ(g_liveBytes, (size_t)0);
}

// ------------------------------------------- keeping part of a large answer

TEST(http_only_keeps_just_what_was_asked_for) {
    Fixture f;
    CHECK(f.start(R"(
        text = "waiting"
        function load()
          local only = { results = { { name = true, result = { data = true, count = true } } }, total = true }
          http.get("https://example.com/big", { only = only }, function(res)
            local first = res.json.results[1]
            local kept = {}
            for key in pairs(first) do kept[#kept + 1] = key end
            table.sort(kept)
            text = table.concat(kept, ",") .. " | " .. first.name .. " | " .. #res.json.results .. " | "
              .. table.concat(first.result[1].data, "+") .. "=" .. first.result[1].count .. " | "
              .. tostring(first.result[1].labels) .. " | " .. tostring(res.json.unwanted) .. " | "
              .. res.json.total .. " | " .. tostring(res.body)
          end)
        end
        function draw() gfx.text(0, 0, text) end
    )"));
    CHECK_EQ(f.services.requests[0].request.maxBytes, (size_t)(256 * 1024));  // a filtered answer may be large
    f.respond(0, 200, R"({
        "unwanted": {"deep": [1, {"x": "y \" } ] tricky"}, [[[]]]], "more": null},
        "total": 2,
        "results": [
          {"name": "A", "id": 1, "owner": {"email": "a@example.com"},
           "result": [{"data": [1, 2, 3], "labels": ["a", "b", "c"], "count": 6, "action": {"id": 5}}]},
          {"name": "B", "result": []}
        ]
    })");
    f.draw();
    CHECK(!f.plugin.failed());
    CHECK(f.gfx.drew("name,result | A | 2 | 1+2+3=6 | nil | nil | 2 | nil"));
}

TEST(http_only_shapes) {
    // One filter covers an object, a list of objects, and lists of lists; `true` keeps a whole branch.
    struct Case {
        const char* filter;
        const char* body;
        const char* expected;
    };
    const Case cases[] = {
        {"{ a = true }", R"({"a": {"x": [1, {"y": 2}]}, "b": 1})", R"({"a":{"x":[1,{"y":2}]}})"},
        {"{ a = true }", R"([{"a": 1, "b": 2}, {"b": 3}, {"a": 4}])", R"([{"a":1},{},{"a":4}])"},
        {"{ a = true }", R"([[{"a": 1, "b": 2}], [{"a": 3, "c": 4}]])", R"([[{"a":1}],[{"a":3}]])"},
        {"{ rows = { count = true } }", R"({"rows": [[1, "x"], [2, "y"]]})", R"({"rows":[[1,"x"],[2,"y"]]})"},
        {"{ list = { { id = true } } }", R"({"list": [{"id": 1, "n": "a"}, {"id": 2}], "x": 0})", R"({"list":[{"id":1},{"id":2}]})"},
        {"{ a = { b = { c = true } } }", R"({"a": {"b": {"c": 1, "d": 2}, "e": 3}})", R"({"a":{"b":{"c":1}}})"},
        {"{ a = true }", R"({"b": "no \\\" } quote", "a": "kept"})", R"({"a":"kept"})"},
        // "*" is every key that is not named: for objects keyed by ids.
        {"{ ['*'] = { name = true } }", R"({"d1": {"name": "Lamp", "caps": {"x": [1, 2]}, "big": "..."}})", R"({"d1":{"name":"Lamp"}})"},
        {"{ ['*'] = true }", R"({"a": {"x": [1]}})", R"({"a":{"x":[1]}})"},
        {"{ ['*'] = { n = true }, total = true }", R"({"total": {"m": 2}})", R"({"total":{"m":2}})"},  // a name goes first
        {"{ ['*'] = { n = true }, total = true }", R"({"a": {"n": 3, "m": 4}})", R"({"a":{"n":3}})"},
        {"{ ['*'] = { n = true } }", R"({"b": [{"n": 1, "m": 2}], "c": null})", R"({"b":[{"n":1}]})"},
        {"{ ['*'] = { n = true } }", R"({"a": 5})", R"({"a":5})"},
        {"{ list = { { ['*'] = { id = true } } } }", R"({"list": [{"k": {"id": 1, "x": 2}}]})", R"({"list":[{"k":{"id":1}}]})"},
    };
    for (const Case& test : cases) {
        Fixture f;
        const std::string source = std::string("out = '?' function load() http.get('http://x', { only = ") + test.filter +
                                   " }, function(res) out = json.encode(res.json) end) end "
                                   "function draw() gfx.text(0, 0, out) end";
        CHECK(f.start(source));
        f.respond(0, 200, test.body);
        f.draw();
        CHECK_EQ(f.gfx.calls.back(), std::string("text 0,0 #ffffff small left ") + test.expected);
    }
}

TEST(http_only_survives_broken_and_deep_answers) {
    const std::string deep = "{\"skip\":" + std::string(60, '[') + std::string(60, ']') + ",\"a\":1}";
    const std::string tooDeep = "{\"skip\":" + std::string(80, '[') + std::string(80, ']') + ",\"a\":1}";
    const std::pair<std::string, const char*> cases[] = {
        {deep, R"({"a":1})"},     // skipped parts may nest deeper than kept ones
        {tooDeep, "null"},        // but not without limit
        {R"({"skip": [1, 2, "a": 1})", "null"},
        {R"({"skip": "unterminated)", "null"},
        {R"({"a": )", "null"},
        {R"({"skip" 1, "a": 2})", "null"},
    };
    for (const auto& test : cases) {
        Fixture f;
        CHECK(f.start(R"(
            out = "?"
            function load() http.get("http://x", { only = { a = true } }, function(res) out = json.encode(res.json) end) end
            function draw() gfx.text(0, 0, out) end
        )"));
        f.respond(0, 200, test.first);
        f.draw();
        CHECK(!f.plugin.failed());
        CHECK_EQ(f.gfx.calls.back(), std::string("text 0,0 #ffffff small left ") + test.second);
    }
}

TEST(reading_a_setting_at_the_top_of_the_file_is_explained) {
    const char* const sources[] = {
        "config = { list = { default = 'a;b' } } for item in config.list:gmatch('[^;]+') do end",
        "config = { city = { default = 'Antwerp' } } local url = 'https://x/' .. config.city",
        "config = { days = { default = 3 } } local week = config.days * 7",
        "config = { days = { default = 3 } } if config.days > 2 then end",
    };
    for (const char* source : sources) {
        Fixture f;
        CHECK(!f.start(source));
        if (!contains(f.plugin.error(), "read them in load() or later")) printf("  %s\n", f.plugin.error().c_str());
        CHECK(contains(f.plugin.error(), "test:1: attempt to"));
        CHECK(contains(f.plugin.error(), "read them in load() or later"));
    }
    // Other mistakes, and plugins without settings, get no such advice.
    Fixture other;
    CHECK(!other.start("config = { a = 1 } error('boom')"));
    CHECK(!contains(other.plugin.error(), "load()"));
    Fixture none;
    CHECK(!none.start("local t = {} local x = t.name:upper()"));
    CHECK(!contains(none.plugin.error(), "load()"));
    // In load() the same code is fine.
    Fixture fine;
    CHECK(fine.start("config = { days = { default = 3 } } function load() week = config.days * 7 end"));
}

TEST(settings_come_in_the_order_the_plugin_asks_for) {
    RecordingGfx gfx;
    MockServices services;
    LuaPlugin plugin(gfx, services);
    const std::string source = R"(config = {
        zebra = { label = "Z", order = 1 }, apple = { label = "A", order = 2 }, mango = "m", banana = "b" })";
    CHECK(plugin.inspect("p", source.data(), source.size()));
    std::string keys;
    for (const LuaPlugin::ConfigField& field : plugin.configSchema()) keys += field.key + " ";
    CHECK_EQ(keys, "zebra apple banana mango ");  // ordered ones first, the rest by name
}

// --------------------------------------------------- plugins with card kinds

const char* const kTwoKinds = R"(
    plugin = { name = "Shop" }
    config = {
      key  = { label = "API key", secret = true },
      zone = { label = "Zone", default = "eu", options = { "eu", "us" } },
    }
    shared = { loads = 0, total = 0 }
    function load() shared.loads = shared.loads + 1 end

    cards = {
      sales = {
        name = "Sales",
        config = { product = { label = "Product", default = "all" }, limit = 10 },
        load = function(card) card.loaded = (card.loaded or 0) + 1 end,
        update = function(card, dt) card.frames = (card.frames or 0) + 1 end,
        draw = function(card)
          gfx.text(0, 0, string.format("sales %s %s limit=%d zone=%s key=%s loads=%d/%d frames=%d presses=%d total=%d",
            card.id, card.config.product, card.config.limit, config.zone, config.key, shared.loads,
            card.loaded or 0, card.frames or 0, card.presses or 0, shared.total))
        end,
        button = function(card, name)
          card.presses = (card.presses or 0) + 1
          shared.total = shared.total + 1
          card.title = "Pressed " .. name
          return name == "center"
        end,
      },
      stock = {
        draw = function(card) gfx.text(0, 0, "stock " .. card.kind .. " total=" .. shared.total) end,
      },
    }
)";

TEST(inspect_reads_settings_and_card_kinds_without_running_anything) {
    RecordingGfx gfx;
    MockServices services;
    LuaPlugin plugin(gfx, services);
    const std::string source = std::string(kTwoKinds) + "\nfunction load() http.get('http://example.com', print) end";
    CHECK(plugin.inspect("shop", source.data(), source.size()));
    CHECK_EQ(services.requests.size(), (size_t)0);  // load() did not run
    CHECK_EQ(plugin.title(), "Shop");

    CHECK_EQ(plugin.configSchema().size(), (size_t)2);
    for (const LuaPlugin::ConfigField& field : plugin.configSchema()) {
        if (field.key == "key") {
            CHECK_EQ(field.label, "API key");
            CHECK(field.secret);
            CHECK_EQ(field.defaultValue, "");
            CHECK_EQ(field.type, "string");
        } else {
            CHECK_EQ(field.key, "zone");
            CHECK(!field.secret);
            CHECK_EQ(field.options.size(), (size_t)2);
            CHECK_EQ(field.options[1], "us");
        }
    }

    CHECK_EQ(plugin.kinds().size(), (size_t)2);
    CHECK_EQ(plugin.kinds()[0].key, "sales");  // sorted by key
    CHECK_EQ(plugin.kinds()[0].name, "Sales");
    CHECK_EQ(plugin.kinds()[0].config.size(), (size_t)2);
    CHECK_EQ(plugin.kinds()[1].key, "stock");
    CHECK_EQ(plugin.kinds()[1].name, "stock");  // no name given: the key
    CHECK_EQ(plugin.kinds()[1].config.size(), (size_t)0);
}

TEST(single_card_plugin_has_one_kind_called_main) {
    Fixture f;
    CHECK(f.start(R"(plugin = { name = "Solo" } function draw() end)"));
    CHECK_EQ(f.plugin.kinds().size(), (size_t)1);
    CHECK_EQ(f.plugin.kinds()[0].key, "main");
    CHECK_EQ(f.plugin.kinds()[0].name, "Solo");
}

TEST(cards_share_one_program_but_keep_their_own_state) {
    RecordingGfx a, b, c;
    MockServices services;
    LuaPlugin plugin(a, services);
    CHECK(plugin.start("shop", kTwoKinds, strlen(kTwoKinds), {{"key", "s3cret"}, {"zone", "us"}}));
    CHECK(plugin.addCard("a", "sales", {{"product", "hats"}}, a));
    CHECK(plugin.addCard("b", "sales", {{"limit", "25"}}, b));
    CHECK(plugin.addCard("c", "stock", {}, c));
    CHECK(!plugin.addCard("d", "nope", {}, c));  // no such kind: not added, plugin unharmed
    CHECK(!plugin.failed());
    CHECK_EQ(plugin.cardCount(), (size_t)3);

    plugin.draw("a");
    plugin.draw("b");
    plugin.draw("c");
    CHECK(a.drew("sales a hats limit=10 zone=us key=s3cret loads=1/1 frames=0 presses=0 total=0"));
    CHECK(b.drew("sales b all limit=25 zone=us key=s3cret loads=1/1 frames=0 presses=0 total=0"));
    CHECK(c.drew("stock stock total=0"));

    // A press goes to one card. What it changes in shared state, every card sees.
    CHECK_EQ(plugin.button("a", Button::Center), true);
    CHECK_EQ(plugin.button("a", Button::Up), false);
    CHECK_EQ(plugin.button("c", Button::Center), false);  // this kind has no button function
    CHECK(plugin.needsDraw("a"));
    a.calls.clear();
    b.calls.clear();
    c.calls.clear();
    plugin.draw("a");
    plugin.draw("b");
    plugin.draw("c");
    CHECK(a.drew("presses=2 total=2"));
    CHECK(b.drew("presses=0 total=2"));
    CHECK(c.drew("total=2"));
    CHECK_EQ(plugin.cardTitle("a"), "Pressed up");
    CHECK_EQ(plugin.cardTitle("b"), "");
}

TEST(only_visible_cards_get_update_and_refresh_reaches_every_card) {
    RecordingGfx a, b;
    MockServices services;
    LuaPlugin plugin(a, services);
    const std::string source = std::string(kTwoKinds) + "\nplugin.refresh = 60";
    CHECK(plugin.start("shop", source.data(), source.size()));
    plugin.addCard("a", "sales", {}, a);
    plugin.addCard("b", "sales", {}, b);
    plugin.setVisible("a", true);
    for (int i = 0; i < 10; i++) {
        services.now += 40;
        plugin.tick(services.now);
    }
    plugin.draw("a");
    plugin.draw("b");
    CHECK(a.drew("frames=9"));
    CHECK(b.drew("frames=0"));

    services.now += 60000;
    plugin.tick(services.now);
    a.calls.clear();
    b.calls.clear();
    CHECK(plugin.needsDraw("b"));
    plugin.draw("a");
    plugin.draw("b");
    CHECK(a.drew("loads=2/2"));  // global load(), then this card's load(card)
    CHECK(b.drew("loads=2/2"));
}

TEST(detached_card_keeps_its_state) {
    RecordingGfx first, second;
    MockServices services;
    LuaPlugin plugin(first, services);
    CHECK(plugin.start("shop", kTwoKinds, strlen(kTwoKinds)));
    plugin.addCard("a", "sales", {}, first);
    plugin.button("a", Button::Center);

    plugin.detachCard("a");  // the card stack is being rebuilt
    CHECK(plugin.hasCard("a"));
    CHECK(!plugin.needsDraw("a"));
    plugin.draw("a");  // no surface: nothing happens
    CHECK_EQ(first.calls.size(), (size_t)0);

    CHECK(plugin.attachCard("a", second));
    CHECK(plugin.needsDraw("a"));
    plugin.draw("a");
    CHECK(second.drew("presses=1"));
    CHECK(second.drew("loads=1/1"));  // attaching did not load again

    plugin.removeCard("a");
    CHECK(!plugin.hasCard("a"));
    CHECK(!plugin.attachCard("a", second));
    CHECK_EQ(plugin.button("a", Button::Center), false);
}

TEST(a_crash_shows_on_every_card_of_the_plugin) {
    RecordingGfx a, b;
    MockServices services;
    LuaPlugin plugin(a, services);
    const std::string source = R"(
        plugin = { name = "Fragile" }
        cards = {
          good = { draw = function(card) gfx.text(0, 0, "fine") end },
          bad = { draw = function(card) error("kaput") end },
        }
    )";
    CHECK(plugin.start("fragile", source.data(), source.size()));
    plugin.addCard("a", "good", {}, a);
    plugin.addCard("b", "bad", {}, b);
    plugin.draw("a");
    CHECK(a.drew("fine"));
    CHECK(!plugin.needsDraw("a"));

    plugin.draw("b");
    CHECK(plugin.failed());
    CHECK(b.drew("Fragile crashed"));
    CHECK(plugin.needsDraw("a"));  // the healthy card has to repaint, as the error
    a.calls.clear();
    plugin.draw("a");
    CHECK(a.drew("Fragile crashed"));
    CHECK(a.drew("kaput"));

    // A card added after the crash exists to show the error too.
    RecordingGfx c;
    CHECK(plugin.addCard("c", "good", {}, c));
    plugin.draw("c");
    CHECK(c.drew("kaput"));
}

TEST(card_load_errors_and_bad_kind_names_are_reported) {
    RecordingGfx gfx;
    MockServices services;
    LuaPlugin plugin(gfx, services);
    const std::string source = R"(cards = { main = { load = function(card) error("no data for " .. card.id) end } })";
    CHECK(plugin.start("p", source.data(), source.size()));
    plugin.addCard("7", "main", {}, gfx);
    CHECK(plugin.failed());
    CHECK(contains(plugin.error(), "no data for 7"));

    LuaPlugin other(gfx, services);
    const std::string bad = R"(cards = { ["Bad Name"] = { draw = function() end } })";
    CHECK(!other.start("p", bad.data(), bad.size()));
    CHECK(contains(other.error(), "card kind 'Bad Name'"));
}

TEST(http_answer_repaints_every_card) {
    RecordingGfx a, b;
    MockServices services;
    LuaPlugin plugin(a, services);
    const std::string source = R"(
        local price = "?"
        function load() http.get("http://example.com/price", function(res) price = res.body end) end
        cards = { tile = { draw = function(card) gfx.text(0, 0, card.id .. " " .. price) end } }
    )";
    CHECK(plugin.start("p", source.data(), source.size()));
    plugin.addCard("a", "tile", {}, a);
    plugin.addCard("b", "tile", {}, b);
    plugin.draw("a");
    plugin.draw("b");
    CHECK(!plugin.needsDraw("a") && !plugin.needsDraw("b"));

    HttpResponse response;
    response.status = 200;
    response.body = "42";
    plugin.httpResponse(services.requests.at(0).id, response);
    CHECK(plugin.needsDraw("a") && plugin.needsDraw("b"));
    plugin.draw("b");
    CHECK(b.drew("b 42"));
}

TEST(restart_drops_cards_and_the_host_adds_them_back) {
    RecordingGfx a;
    MockServices services;
    LuaPlugin plugin(a, services);
    CHECK(plugin.start("shop", kTwoKinds, strlen(kTwoKinds)));
    plugin.addCard("a", "sales", {}, a);
    plugin.button("a", Button::Center);
    CHECK(plugin.start("shop", kTwoKinds, strlen(kTwoKinds)));
    CHECK_EQ(plugin.cardCount(), (size_t)0);
    plugin.addCard("a", "sales", {}, a);
    plugin.draw("a");
    CHECK(a.drew("presses=0"));
}

// ----------------------------------------------------------- sample plugins

TEST(sample_hello) {
    Fixture f;
    CHECK(f.startFile("hello"));
    f.draw();
    CHECK(f.gfx.drew("large center Hi there"));
    CHECK(f.gfx.drew("0 presses"));
    CHECK_EQ(f.button(Button::Center), true);
    CHECK_EQ(f.button(Button::Down), false);
    f.draw();
    CHECK(f.gfx.drew("1 press"));
    CHECK(!f.plugin.failed());
}

TEST(sample_weather) {
    Fixture f;
    CHECK(f.startFile("weather", {}, {{"city", "Sint-Niklaas"}}));
    CHECK_EQ(f.plugin.title(), "Weather");
    f.draw();
    CHECK(f.gfx.drew("small left Sint-Niklaas"));
    CHECK(f.gfx.drew("Loading..."));

    CHECK_EQ(f.services.requests.size(), (size_t)1);
    CHECK_EQ(f.services.requests[0].request.url,
             "https://geocoding-api.open-meteo.com/v1/search?count=1&name=Sint-Niklaas");
    f.respond(0, 200, R"({"results":[{"name":"Sint-Niklaas","latitude":51.16509,"longitude":4.1437}]})");
    CHECK_EQ(f.plugin.cardTitle("1"), "Sint-Niklaas");

    CHECK_EQ(f.services.requests.size(), (size_t)2);
    CHECK_EQ(f.services.requests[1].request.url,
             "https://api.open-meteo.com/v1/forecast?latitude=51.1651&longitude=4.1437"
             "&current=temperature_2m,weather_code&hourly=temperature_2m&forecast_days=1");
    f.respond(1, 200,
              R"({"current":{"temperature_2m":8.6,"weather_code":61},"hourly":{"temperature_2m":[5,6,7.5,9,8,6]}})");
    f.draw();
    CHECK(!f.plugin.failed());
    CHECK(f.gfx.drew("large center 9°"));
    CHECK(f.gfx.drew("small center Rain"));
    CHECK(f.gfx.drew("line "));

    // Fifteen minutes later it asks again, without looking the city up twice.
    f.services.now += 900000;
    f.tick(f.services.now, false);
    CHECK_EQ(f.services.requests.size(), (size_t)3);
    CHECK(contains(f.services.requests[2].request.url, "api.open-meteo.com/v1/forecast"));
}

TEST(sample_weather_handles_bad_answers) {
    Fixture f;
    CHECK(f.startFile("weather"));
    f.respond(0, 200, R"({"generationtime_ms":0.5})");
    f.draw();
    CHECK(f.gfx.drew("City not found"));

    Fixture g;
    CHECK(g.startFile("weather", {{"unit", "F"}}, {{"city", "São Paulo"}}));
    CHECK(contains(g.services.requests[0].request.url, "name=S%C3%A3o%20Paulo"));
    g.respond(0, 0, "", "timeout");
    g.draw();
    CHECK(g.gfx.drew("No connection"));
    CHECK(!g.plugin.failed());
}

TEST(sample_weather_two_cities_share_the_unit) {
    RecordingGfx antwerp, lisbon;
    MockServices services;
    LuaPlugin plugin(antwerp, services);
    const std::string source = readFile(g_pluginDir + "/weather.lua");
    CHECK(plugin.start("weather", source.data(), source.size(), {{"unit", "F"}}));
    CHECK(plugin.addCard("a", "city", {{"city", "Antwerp"}}, antwerp));
    CHECK(plugin.addCard("b", "city", {{"city", "Lisbon"}}, lisbon));
    CHECK_EQ(services.requests.size(), (size_t)2);

    HttpResponse place;
    place.status = 200;
    place.body = R"({"results":[{"name":"Lisbon","latitude":38.7,"longitude":-9.1}]})";
    plugin.httpResponse(services.requests[1].id, place);
    CHECK_EQ(services.requests.size(), (size_t)3);
    CHECK(contains(services.requests[2].request.url, "temperature_unit=fahrenheit"));

    HttpResponse weather;
    weather.status = 200;
    weather.body = R"({"current":{"temperature_2m":64.2,"weather_code":0},"hourly":{"temperature_2m":[60,64,66]}})";
    plugin.httpResponse(services.requests[2].id, weather);

    plugin.draw("a");
    plugin.draw("b");
    CHECK(antwerp.drew("small left Antwerp"));
    CHECK(antwerp.drew("Loading..."));        // its own lookup has not come back yet
    CHECK(lisbon.drew("large center 64°"));
    CHECK(lisbon.drew("small center Clear"));
    CHECK_EQ(plugin.cardTitle("a"), "");
    CHECK_EQ(plugin.cardTitle("b"), "Lisbon");
}

TEST(sample_flappy_plays_and_crashes) {
    Fixture f;
    f.services.stored["best"] = "3";
    CHECK(f.startFile("flappy"));
    f.draw();
    CHECK(f.gfx.drew("Press center"));
    CHECK(f.gfx.drew("rect 30,68 10x10 #f1c40f r2"));  // the hog, mid screen

    CHECK_EQ(f.button(Button::Up), false);      // up/down still navigate
    CHECK_EQ(f.button(Button::Center), true);   // start
    f.runFrames(3.0f);                                 // never flap again: fall to the floor
    CHECK(!f.plugin.failed());
    CHECK(f.gfx.drew("Game over"));
    CHECK(f.gfx.drew("Best 3"));
    CHECK_EQ(f.services.led, "#e74c3c for 2000ms");

    CHECK_EQ(f.button(Button::Center), true);   // back to the start screen
    f.draw();
    CHECK(f.gfx.drew("Press center"));
}

TEST(sample_flappy_survives_a_long_session) {
    Fixture f;
    CHECK(f.startFile("flappy"));
    // Flap on a fixed rhythm for five minutes of game time, restarting after each crash.
    for (int frame = 0; frame < 9000 && !f.plugin.failed(); frame++) {
        if (frame % 12 == 0) f.button(Button::Center);
        f.services.now += 33;
        f.tick(f.services.now, true);
        if (f.needsDraw()) f.draw();
    }
    CHECK(!f.plugin.failed());
    CHECK(f.plugin.memoryUsed() < 64 * 1024);
}

TEST(sample_friend) {
    Fixture f;
    CHECK(f.startFile("friend"));
    f.draw();
    CHECK(!f.plugin.failed());
    CHECK(f.gfx.drew("rect 5,5 223x125 #0e7a00 r8"));
    CHECK(f.gfx.drew("image 4,"));
    CHECK(f.gfx.drew("loud center CHOICES"));
    CHECK_EQ(f.button(Button::Up), false);
    CHECK_EQ(f.button(Button::Center), true);
    f.draw();
    CHECK(f.gfx.drew("loud center APPROVE"));
    f.button(Button::Center);
    f.draw();
    CHECK(f.gfx.drew("medium center EXPECTATIONS"));  // a word too long for the loud font is set smaller, not cut
    f.runFrames(2.0f);  // Max walks
    CHECK(!f.plugin.failed());
    CHECK(f.plugin.memoryUsed() < 128 * 1024);

    Fixture own;
    CHECK(own.startFile("friend", {{"messages", " GOOD JOB | | NICE HAT "}}));
    own.draw();
    CHECK(own.gfx.drew("loud center GOOD JOB"));
    own.button(Button::Center);
    own.draw();
    CHECK(own.gfx.drew("loud center NICE HAT"));
    own.button(Button::Center);
    own.draw();
    CHECK(own.gfx.drew("loud center GOOD JOB"));
}

TEST(sample_question) {
    Fixture f;
    CHECK(f.startFile("question"));
    f.draw();
    CHECK(!f.plugin.failed());
    CHECK(f.gfx.drew("rect 5,5 223x125 #4a4a4a r8"));
    CHECK(f.gfx.drew("clip 12,12 209x111"));
    CHECK(f.gfx.drew("text 12,12 #ffffff loud left If you had to"));  // too long to fit: starts at the top
    CHECK_EQ(f.gfx.calls.back(), "clip off");

    f.runFrames(4.0f);  // rests, then scrolls
    CHECK(!f.plugin.failed());
    CHECK(!f.gfx.drew("text 12,12 #ffffff loud left If you had to"));
    CHECK(f.gfx.drew("loud left If you had to"));

    for (int i = 0; i < 6; i++) CHECK_EQ(f.button(Button::Center), true);
    f.draw();
    CHECK(f.gfx.drew("#c0392b"));
    CHECK(f.gfx.drew("loud left Pineapple on"));
    f.services.now += 5000;
    f.tick(f.services.now, true);
    CHECK(!f.needsDraw());  // a short question stays put and is not repainted
    f.button(Button::Center);
    f.draw();
    CHECK(f.gfx.drew("#4a4a4a"));  // round again
}

TEST(sample_paddle) {
    Fixture f;
    CHECK(f.startFile("paddle"));
    f.draw();
    CHECK(f.gfx.drew("loud center PADDLE"));
    CHECK_EQ(f.button(Button::Up), false);     // not playing: up and down switch cards
    CHECK_EQ(f.button(Button::Center), true);  // start
    CHECK_EQ(f.button(Button::Up), true);      // now they move the paddle
    f.draw();
    CHECK(f.gfx.drew("Ready?"));

    f.services.held[(int)Button::Up] = true;
    f.runFrames(1.0f);
    CHECK(f.gfx.drew("Ready?"));               // paddles wait for the serve
    f.runFrames(1.0f);
    CHECK(f.gfx.drew("rect 5,0 5x30 #ffffff r0"));  // held up: the paddle is at the top
    f.services.held[(int)Button::Up] = false;

    CHECK_EQ(f.button(Button::Center), true);  // pause
    f.draw();
    CHECK(f.gfx.drew("Paused"));
    CHECK_EQ(f.button(Button::Down), false);   // paused: you can leave
    f.draw();
    f.services.now += 5000;
    f.tick(f.services.now, true);
    CHECK(!f.needsDraw());
    CHECK_EQ(f.button(Button::Center), true);  // carry on

    f.runFrames(120.0f);  // never move again: the hedgehog wins
    CHECK(!f.plugin.failed());
    CHECK(f.gfx.drew("Game over"));
    CHECK(f.gfx.drew("medium left 5"));
    CHECK_EQ(f.services.led, "#e74c3c for 2000ms");
    CHECK_EQ(f.button(Button::Center), true);  // play again
    f.draw();
    CHECK(f.gfx.drew("Ready?"));
    CHECK(f.gfx.drew("medium left 0"));
}

TEST(sample_webhook) {
    Fixture f;
    CHECK(f.startFile("webhook"));
    CHECK_EQ(f.button(Button::Center), true);
    f.draw();
    CHECK(f.gfx.drew("No URL set"));
    CHECK_EQ(f.services.requests.size(), (size_t)0);

    Fixture g;
    CHECK(g.startFile("webhook", {{"url", "https://example.com/hook"}, {"label", "Deploy"}}));
    g.draw();
    CHECK(g.gfx.drew("large center Deploy"));
    CHECK_EQ(g.button(Button::Center), true);
    CHECK_EQ(g.services.led, "#f1c40f");
    CHECK_EQ(g.services.requests.size(), (size_t)1);
    CHECK_EQ(g.services.requests[0].request.method, "POST");
    CHECK(contains(g.services.requests[0].request.body, R"("label":"Deploy")"));
    g.respond(0, 204, "");
    g.draw();
    CHECK(g.gfx.drew("#2ecc71 small center Sent"));
    CHECK_EQ(g.services.led, "#2ecc71 for 3000ms");

    CHECK_EQ(g.button(Button::Center), true);
    g.respond(1, 500, "nope");
    g.draw();
    CHECK(g.gfx.drew("Failed: HTTP 500"));
    CHECK(!g.plugin.failed());
}

// The PostHog plugin, fed answers in the shapes PostHog's API gives (test/native/fixtures).
struct PostHog {
    Fixture f;
    std::string fixtureDir = g_pluginDir + "/../test/native/fixtures/";

    explicit PostHog(const std::string& insightId = "AbC123xy",
                     const LuaPlugin::Settings& settings = {{"project", "12345"}, {"key", "phx_secret"}, {"region", "eu"}}) {
        f.startFile("posthog", settings, {{"id", insightId}});
    }
    void answer(size_t request, const std::string& fixture) { f.respond(request, 200, readFile(fixtureDir + fixture + ".json")); }
    const HttpRequest& request(size_t index) { return f.services.requests.at(index).request; }
};

TEST(posthog_asks_the_right_question) {
    PostHog p;
    CHECK(!p.f.plugin.failed());
    CHECK_EQ(p.f.plugin.title(), "PostHog");
    CHECK_EQ(p.f.plugin.kinds().size(), (size_t)1);
    CHECK_EQ(p.f.plugin.kinds()[0].key, "insight");
    std::string fields;
    for (const LuaPlugin::ConfigField& field : p.f.plugin.configSchema()) fields += field.key + (field.secret ? "(secret) " : " ");
    CHECK_EQ(fields, "region project key(secret) host ");

    CHECK_EQ(p.f.services.requests.size(), (size_t)1);
    CHECK_EQ(p.request(0).url, "https://eu.posthog.com/api/projects/12345/insights/?short_id=AbC123xy&refresh=force_cache");
    CHECK_EQ(p.request(0).headers.size(), (size_t)1);
    CHECK_EQ(p.request(0).headers[0].first, "Authorization");
    CHECK_EQ(p.request(0).headers[0].second, "Bearer phx_secret");  // the key is not in the URL
    CHECK(p.request(0).maxBytes > 100000);
    p.f.draw();
    CHECK(p.f.gfx.drew("small left PostHog"));
    CHECK(p.f.gfx.drew("Loading..."));

    PostHog selfHosted("x y", {{"project", "7"}, {"key", "k"}, {"host", "https://hog.example.com/"}});
    CHECK_EQ(selfHosted.request(0).url, "https://hog.example.com/api/projects/7/insights/?short_id=x%20y&refresh=force_cache");
}

TEST(posthog_big_number) {
    PostHog p;
    p.answer(0, "posthog-number");
    p.f.draw();
    CHECK(!p.f.plugin.failed());
    CHECK(p.f.gfx.drew("small left Signups this week"));
    CHECK(p.f.gfx.drew("large center 1,234"));
    CHECK_EQ(p.f.plugin.cardTitle("1"), "Signups this week");

    PostHog money;
    money.answer(0, "posthog-money");
    money.f.draw();
    CHECK(money.f.gfx.drew("large center $1.23M MRR"));
}

TEST(posthog_line_chart) {
    PostHog p;
    p.answer(0, "posthog-line");
    p.f.draw();
    CHECK(!p.f.plugin.failed());
    CHECK(p.f.gfx.drew("small left Daily active users"));
    CHECK(p.f.gfx.drew("large center 784"));                // the latest point: 400 + 150*sin(29/4) + 29*9
    CHECK(p.f.gfx.drew("small center $pageview +1 more"));  // only the first series is drawn
    int lines = 0;
    for (const std::string& call : p.f.gfx.calls) lines += contains(call, "line ") && contains(call, "#e67e22");
    CHECK_EQ(lines, 29);
    // The 7.6 KB answer was whittled down before it reached the plugin's memory:
    // the plugin itself takes about 45 KB of the total.
    CHECK(p.f.plugin.memoryUsed() < 64 * 1024);
}

TEST(posthog_funnel) {
    PostHog p;
    p.answer(0, "posthog-funnel");
    p.f.draw();
    CHECK(!p.f.plugin.failed());
    CHECK(p.f.gfx.drew("small left Signup funnel"));
    CHECK(p.f.gfx.drew("right 12,840  100%"));
    CHECK(p.f.gfx.drew("left Visited the .."));             // the custom name wins, cut to fit beside the figures
    CHECK(p.f.gfx.drew("right 3,120  24%"));
    CHECK(p.f.gfx.drew("right 1,432  11%"));
    CHECK(p.f.gfx.drew("right 96  1%"));                    // five steps: the fourth row is the last step
    CHECK(!p.f.gfx.drew("610"));
    CHECK(p.f.gfx.drew("rect 8,45 217x5 #2a2a2a r2"));      // a track, and a bar as wide as the share
    CHECK(p.f.gfx.drew("rect 8,45 217x5 #e67e22 r2"));
    CHECK(p.f.gfx.drew("rect 8,71 53x5 #e67e22 r2"));

    PostHog breakdown;
    breakdown.answer(0, "posthog-funnel-breakdown");
    breakdown.f.draw();
    CHECK(breakdown.f.gfx.drew("right 500  100%"));         // the first breakdown
    CHECK(breakdown.f.gfx.drew("right 125  25%"));
    CHECK(!breakdown.f.gfx.drew("300"));
}

TEST(posthog_sql_insights) {
    PostHog p;
    p.answer(0, "posthog-sql-number");
    p.f.draw();
    CHECK(p.f.gfx.drew("large center 4,821"));

    PostHog chart;
    chart.answer(0, "posthog-sql-line");
    chart.f.draw();
    CHECK(chart.f.gfx.drew("center 221"));                  // 100 + 11*11
    CHECK(chart.f.gfx.drew("line "));
}

TEST(posthog_asks_again_when_nothing_was_calculated_yet) {
    PostHog p;
    p.answer(0, "posthog-uncalculated");
    CHECK_EQ(p.f.services.requests.size(), (size_t)2);
    CHECK(contains(p.request(1).url, "refresh=blocking"));
    CHECK_EQ(p.request(1).timeoutMs, (uint32_t)30000);
    p.answer(1, "posthog-number");
    p.f.draw();
    CHECK(p.f.gfx.drew("large center 1,234"));

    PostHog still;  // still nothing after the blocking request: say so, do not loop
    still.answer(0, "posthog-uncalculated");
    still.answer(1, "posthog-uncalculated");
    CHECK_EQ(still.f.services.requests.size(), (size_t)2);
    still.f.draw();
    CHECK(still.f.gfx.drew("No data yet"));
}

TEST(posthog_refreshes_and_recalculates_on_a_press) {
    PostHog p;
    p.answer(0, "posthog-number");
    p.f.services.now += 600000;
    p.f.tick(p.f.services.now, false);
    CHECK_EQ(p.f.services.requests.size(), (size_t)2);
    CHECK(contains(p.request(1).url, "refresh=blocking"));  // after the first answer it asks for fresh numbers

    CHECK_EQ(p.f.button(Button::Center), true);
    CHECK_EQ(p.f.services.requests.size(), (size_t)3);
    CHECK_EQ(p.f.button(Button::Up), false);                // up and down still switch cards

    // A network hiccup keeps the last numbers on screen.
    p.f.respond(1, 0, "", "timeout");
    p.f.draw();
    CHECK(p.f.gfx.drew("large center 1,234"));
}

TEST(posthog_explains_what_is_wrong) {
    PostHog noKey("AbC123xy", {{"project", "12345"}});
    CHECK_EQ(noKey.f.services.requests.size(), (size_t)0);
    noKey.f.draw();
    CHECK(noKey.f.gfx.drew("Add your project ID"));

    PostHog noId("");
    CHECK_EQ(noId.f.services.requests.size(), (size_t)0);
    noId.f.draw();
    CHECK(noId.f.gfx.drew("Set this card's insight ID"));

    PostHog refused;
    refused.f.respond(0, 401, R"({"type":"authentication_error","code":"authentication_failed","detail":"Invalid key"})");
    refused.f.draw();
    CHECK(refused.f.gfx.drew("PostHog refused the API key"));

    PostHog missing;
    missing.answer(0, "posthog-missing");
    missing.f.draw();
    CHECK(missing.f.gfx.drew("No insight with the ID"));

    PostHog offline;
    offline.f.respond(0, 0, "", "no WiFi");
    offline.f.draw();
    CHECK(offline.f.gfx.drew("No connection"));

    PostHog other;
    other.answer(0, "posthog-retention");
    other.f.draw();
    CHECK(other.f.gfx.drew("is not supported yet"));
    CHECK(!other.f.plugin.failed());

    PostHog garbage;
    garbage.f.respond(0, 200, "<html>proxy login page</html>");
    garbage.f.draw();
    CHECK(garbage.f.gfx.drew("No insight with the ID"));
    CHECK(!garbage.f.plugin.failed());
}

TEST(a_plugin_describes_itself) {
    Fixture f;
    CHECK(f.plugin.inspect("clock", R"(
        plugin = { name = "Clock", description = "The time in large digits", author = "Someone", version = "1.2" }
    )", 120));
    CHECK_EQ(f.plugin.title(), "Clock");
    CHECK_EQ(f.plugin.description(), "The time in large digits");
    CHECK_EQ(f.plugin.author(), "Someone");
    CHECK_EQ(f.plugin.version(), "1.2");

    // A version written as a number is fine; what is left out is empty; long text is cut.
    Fixture g;
    const std::string source = "plugin = { version = 2, description = string.rep('x', 500) }";
    CHECK(g.plugin.inspect("plain", source.data(), source.size()));
    CHECK_EQ(g.plugin.title(), "plain");
    CHECK_EQ(g.plugin.version(), "2");
    CHECK_EQ(g.plugin.author(), "");
    CHECK_EQ(g.plugin.description().size(), (size_t)200);

    // Starting another plugin forgets the previous one's description.
    CHECK(g.plugin.inspect("other", "plugin = {}", 11));
    CHECK_EQ(g.plugin.version(), "");
}

TEST(source_fingerprints) {
    // The web page (portal.js) and tools/deskhog compute the same values.
    CHECK_EQ(sourceHash("", 0), 2166136261u);
    CHECK_EQ(sourceHash("a", 1), 0xe40c292cu);
    CHECK_EQ(sourceHash("foobar", 6), 0xbf9cf968u);
}

TEST(every_plugin_in_the_directory_loads) {
    // plugins/index.json lists them; whatever is published there has to at least load.
    const std::string index = readFile(g_pluginDir + "/index.json");
    CHECK(!index.empty());
    size_t count = 0, at = 0;
    const std::string key = "\"file\": \"";
    while ((at = index.find(key, at)) != std::string::npos) {
        at += key.size();
        const std::string file = index.substr(at, index.find('"', at) - at);
        const std::string name = file.substr(0, file.size() - 4);
        const std::string source = readFile(g_pluginDir + "/" + file);
        Fixture f;
        const bool loads = !source.empty() && f.plugin.inspect(name, source.data(), source.size());
        if (!loads) printf("  %s: %s\n", file.c_str(), source.empty() ? "missing" : f.plugin.error().c_str());
        CHECK(loads);
        CHECK(!f.plugin.description().empty() && !f.plugin.version().empty());
        // The index says what the plugin says.
        CHECK(contains(index, "\"title\": \"" + f.plugin.title() + "\""));
        count++;
    }
    CHECK(count >= 9);
}

TEST(sample_clock) {
    Fixture f;
    f.services.epoch = 1763236140;  // Saturday 15 November 2025, 19:49 UTC
    f.services.zone = "CET-1CEST,M3.5.0,M10.5.0/3";
    CHECK(f.startFile("clock", {{"offset", "5"}}));  // a setting from before version 2: ignored
    f.draw();
    CHECK(f.gfx.drew("Saturday"));
    CHECK(f.gfx.drew("20:49"));
    CHECK(f.gfx.drew("15 November 2025"));
    for (int i = 0; i < 3; i++) {
        f.services.now += 1000;
        f.tick(f.services.now, true);
        f.draw();
    }
    f.services.now += 1000;
    f.tick(f.services.now, true);
    CHECK(!f.needsDraw());  // the same minute: nothing to repaint
    f.services.epoch += 60;
    f.services.now += 1000;
    f.tick(f.services.now, true);
    CHECK(f.needsDraw());
    f.draw();
    CHECK(f.gfx.drew("20:50"));

    Fixture twelve;
    twelve.services.epoch = 1763236140 + 5 * 3600;  // 00:49 UTC the next day
    twelve.services.zone = "<-0530>5:30";
    CHECK(twelve.startFile("clock", {{"hours12", "true"}, {"label", "Home"}}));
    twelve.draw();
    CHECK(twelve.gfx.drew("7:19 pm"));
    CHECK(twelve.gfx.drew("Home"));
    CHECK(twelve.gfx.drew("Saturday 15 November 2025"));

    Fixture unset;
    unset.services.epoch = 0;
    CHECK(unset.startFile("clock"));
    unset.draw();
    CHECK(unset.gfx.drew("Waiting for the time"));

    // Summer time comes with the zone: the same clock, in July.
    Fixture summer;
    summer.services.zone = "CET-1CEST,M3.5.0,M10.5.0/3";
    summer.services.epoch = 1752522540;  // Monday 14 July 2025, 19:49 UTC
    CHECK(summer.startFile("clock"));
    summer.draw();
    CHECK(summer.gfx.drew("21:49"));
    CHECK(summer.gfx.drew("Monday"));
    CHECK_EQ(summer.plugin.configSchema().size(), (size_t)2);  // no offset to fill in any more
}

// ------------------------------------------------- more plugins in plugins/

bool hasHeader(const HttpRequest& request, const std::string& name, const std::string& value) {
    for (const auto& header : request.headers) {
        if (header.first == name && header.second == value) return true;
    }
    return false;
}

/** Let `seconds` pass in steps, with the card on screen or not. */
void pass(Fixture& f, float seconds, bool visible = true) {
    for (int i = 0; i < (int)(seconds * 10); i++) {
        f.services.now += 100;
        f.tick(f.services.now, visible);
    }
}

/** Start a plugin with one card of a given kind (instead of its first kind). */
bool startKind(Fixture& f, const std::string& name, const std::string& kind, const LuaPlugin::Settings& settings = {},
               const LuaPlugin::Settings& cardSettings = {}) {
    const std::string source = readFile(g_pluginDir + "/" + name + ".lua");
    f.plugin.start(name, source.data(), source.size(), settings);
    f.plugin.addCard("1", kind, cardSettings, f.gfx);
    return !f.plugin.failed();
}

TEST(sample_pomodoro) {
    Fixture f;
    CHECK(f.startFile("pomodoro", {{"focus", "1"}, {"rest", "2"}}));
    f.draw();
    CHECK(f.gfx.drew("large center 1:00"));
    CHECK_EQ(f.button(Button::Up), false);      // nothing to start over: up switches cards
    CHECK_EQ(f.button(Button::Center), true);   // start
    CHECK_EQ(f.services.led, "#e74c3c");        // red: focusing
    pass(f, 30);
    f.draw();
    CHECK(f.gfx.drew("Focus, round 1 of 4"));
    CHECK(f.gfx.drew("large center 0:30"));

    CHECK_EQ(f.button(Button::Center), true);   // pause
    CHECK_EQ(f.services.led, "default");
    pass(f, 20);
    f.draw();
    CHECK(f.gfx.drew("large center 0:30"));     // time stands still
    CHECK_EQ(f.button(Button::Center), true);   // carry on

    // The break starts by itself, also while another card is on screen.
    pass(f, 40, false);
    CHECK_EQ(f.services.led, "#2ecc71");
    f.draw();
    CHECK(f.gfx.drew("small left Break"));
    pass(f, 125, false);
    CHECK_EQ(f.services.led, "default");
    f.draw();
    CHECK(f.gfx.drew("Break is over. Centre for"));

    CHECK_EQ(f.button(Button::Center), true);
    CHECK_EQ(f.button(Button::Center), true);   // paused
    CHECK_EQ(f.button(Button::Up), true);       // start over
    f.draw();
    CHECK(f.gfx.drew("Centre to start"));
    CHECK(!f.plugin.failed());
}

TEST(sample_pet) {
    Fixture f;  // the fake clock says 09:46 UTC: he is awake
    CHECK(f.startFile("pet"));
    f.draw();
    CHECK(f.gfx.drew("medium left Max"));
    CHECK(f.gfx.drew("Feeling fine"));
    CHECK(f.services.stored.count("pet") == 1);

    // Five hours later he is hungry, and says so with the LED.
    f.services.epoch += 5 * 3600;
    pass(f, 61, false);
    CHECK_EQ(f.services.led, "#e67e22");
    f.draw();
    CHECK(f.gfx.drew("I'm hungry"));
    CHECK_EQ(f.button(Button::Center), true);
    f.draw();
    CHECK(f.gfx.drew("Nom nom nom"));
    pass(f, 3);
    f.draw();
    CHECK(f.gfx.drew("Play with me"));          // fed, but still bored
    CHECK_EQ(f.button(Button::Center), true);
    pass(f, 3);
    f.draw();
    CHECK(f.gfx.drew("Feeling fine"));
    CHECK_EQ(f.button(Button::Up), false);

    // What he was like is remembered across a restart, and he sleeps at night.
    Fixture later;
    later.services.stored = f.services.stored;
    later.services.epoch = f.services.epoch + 8 * 3600;  // 22:46
    CHECK(later.startFile("pet", {{"name", "Spike"}}));
    later.draw();
    CHECK(later.gfx.drew("medium left Spike"));
    CHECK(later.gfx.drew("Zzz..."));
    CHECK_EQ(later.button(Button::Center), true);
    later.draw();
    CHECK(later.gfx.drew("Shh. He's asleep"));
    CHECK(!later.plugin.failed());

    Fixture garbage;  // saved data that is not what we expect is started afresh
    garbage.services.stored["pet"] = "\"oops\"";
    CHECK(garbage.startFile("pet"));
    garbage.draw();
    CHECK(garbage.gfx.drew("Feeling fine"));
}

TEST(sample_snake) {
    Fixture f;
    f.services.stored["best"] = "7";
    CHECK(f.startFile("snake"));
    f.draw();
    CHECK(f.gfx.drew("loud center SNAKE"));
    CHECK_EQ(f.button(Button::Up), false);      // no game on: up and down switch cards
    CHECK_EQ(f.button(Button::Center), true);
    CHECK_EQ(f.button(Button::Up), true);       // now they steer
    f.runFrames(1.0f);
    CHECK_EQ(f.button(Button::Center), true);   // pause
    f.draw();
    CHECK(f.gfx.drew("Paused"));
    CHECK_EQ(f.button(Button::Down), false);

    // Ten minutes of steering at random, starting again after every bite.
    for (int frame = 0; frame < 18000 && !f.plugin.failed(); frame++) {
        if (frame % 23 == 0) f.button(frame % 3 == 0 ? Button::Up : Button::Down);
        if (frame % 301 == 0) f.button(Button::Center);
        f.services.now += 33;
        f.tick(f.services.now, true);
        if (f.needsDraw()) f.draw();
    }
    CHECK(!f.plugin.failed());
    CHECK(f.plugin.memoryUsed() < 96 * 1024);
}

TEST(sample_breakout) {
    Fixture f;
    CHECK(f.startFile("breakout"));
    f.draw();
    CHECK(f.gfx.drew("loud center BREAKOUT"));
    CHECK_EQ(f.button(Button::Down), false);
    CHECK_EQ(f.button(Button::Center), true);   // a new game: the ball waits on the bat
    f.draw();
    CHECK(f.gfx.drew("Centre to serve"));
    CHECK(f.gfx.drew("rect 4,18 225x7 #e74c3c r0"));  // a whole row of bricks is one rectangle
    f.services.held[(int)Button::Up] = true;
    f.runFrames(1.5f);
    f.services.held[(int)Button::Up] = false;
    CHECK(f.gfx.drew("rect 0,127 38x4 #ffffff r2"));  // held up: the bat is at the left edge
    CHECK_EQ(f.button(Button::Center), true);   // serve

    for (int frame = 0; frame < 18000 && !f.plugin.failed(); frame++) {
        f.services.held[(int)Button::Up] = (frame / 40) % 3 == 0;
        f.services.held[(int)Button::Down] = (frame / 40) % 3 == 1;
        if (frame % 211 == 0) f.button(Button::Center);
        f.services.now += 33;
        f.tick(f.services.now, true);
        if (f.needsDraw()) f.draw();
    }
    CHECK(!f.plugin.failed());
    CHECK(f.plugin.memoryUsed() < 96 * 1024);
}

TEST(sample_reaction) {
    Fixture f;
    CHECK(f.startFile("reaction"));
    f.draw();
    CHECK(f.gfx.drew("Ready?"));
    CHECK_EQ(f.button(Button::Center), true);
    CHECK_EQ(f.services.led, "#e74c3c");
    pass(f, 0.3f);
    f.draw();
    CHECK(f.gfx.drew("Wait for green"));
    for (int i = 0; i < 60 && f.services.led != "#2ecc71"; i++) pass(f, 0.1f);  // green within five seconds
    CHECK_EQ(f.services.led, "#2ecc71");
    f.draw();
    CHECK(f.gfx.drew("NOW!"));
    f.services.now += 250;
    CHECK_EQ(f.button(Button::Center), true);
    f.draw();
    CHECK(f.gfx.drew("large center 250 ms"));
    CHECK_EQ(f.services.stored["best"], "250");
    CHECK_EQ(f.services.led, "default");

    CHECK_EQ(f.button(Button::Center), true);   // again, but too eager this time
    CHECK_EQ(f.button(Button::Center), true);
    f.draw();
    CHECK(f.gfx.drew("Too soon"));
    CHECK_EQ(f.services.stored["best"], "250");
    CHECK_EQ(f.button(Button::Down), false);
}

TEST(sample_simon) {
    Fixture f;
    CHECK(f.startFile("simon"));
    f.draw();
    CHECK(f.gfx.drew("loud center SIMON"));
    CHECK_EQ(f.button(Button::Down), false);
    CHECK_EQ(f.button(Button::Center), true);
    CHECK_EQ(f.button(Button::Down), true);     // while it shows the pattern, presses are swallowed
    f.runFrames(3.0f);
    f.draw();
    CHECK(f.gfx.drew("Your turn"));
    // Play for a while by guessing. Right or wrong, it must carry on.
    for (int i = 0; i < 400 && !f.plugin.failed(); i++) {
        f.button((Button)(i % 3));
        f.runFrames(0.5f);
    }
    CHECK(!f.plugin.failed());
    CHECK(f.services.stored.count("best") == 1 || f.gfx.drew("Round"));
}

TEST(sample_countdown) {
    // The fake clock says Saturday 15 November 2025, 09:46:40 UTC.
    struct Case { const char* date; const char* time; const char* zone; const char* expect; };
    const Case cases[] = {
        {"2025-12-25", "", "UTC0", "large center 39 days"},
        {"2025-11-15", "", "UTC0", "Today!"},
        {"2025-11-16", "", "<+15>-15", "Today!"},           // fifteen hours ahead it is already tomorrow
        {"2025-11-16", "9:00", "UTC0", "23h 13m"},
        {"2025-11-16", "9:00", "CET-1CEST,M3.5.0,M10.5.0/3", "22h 13m"},  // the device's zone, an hour ahead
        {"2025-11-15", "10:00", "UTC0", "13:20"},
        {"2025-11-15", "9:00", "UTC0", "Today!"},           // earlier today
        {"2025-11-10", "", "UTC0", "5 days ago"},
        {"2025-11-14", "", "UTC0", "1 day ago"},
        {"2100-01-01", "", "UTC0", " days"},                // far beyond what a 32-bit clock counts in seconds
        {"next week", "", "UTC0", "Write the date as"},
        {"2025-13-01", "", "UTC0", "Write the date as"},
    };
    for (const Case& c : cases) {
        Fixture f;
        f.services.zone = c.zone;
        CHECK(f.startFile("countdown", {}, {{"what", "Launch"}, {"date", c.date}, {"time", c.time}}));
        f.draw();
        if (!f.gfx.drew(c.expect)) printf("  %s %s in %s: expected %s, drew\n%s", c.date, c.time, c.zone, c.expect, f.gfx.dump().c_str());
        CHECK(f.gfx.drew(c.expect));
        CHECK(f.gfx.drew("small left Launch"));
        CHECK(!f.plugin.failed());
    }
    Fixture f;
    CHECK(f.startFile("countdown", {}, {{"what", "Christmas"}, {"date", "2025-12-25"}}));
    CHECK_EQ(f.plugin.cardTitle("1"), "Christmas");
    f.draw();
    CHECK(f.gfx.drew("25 December 2025"));
    pass(f, 3);
    f.draw();
    pass(f, 3);
    CHECK(!f.needsDraw());  // nothing changed: no repaint
}

TEST(sample_starfield) {
    Fixture f;
    CHECK(f.startFile("starfield"));
    f.runFrames(5.0f);
    CHECK(f.gfx.calls.size() > 10);
    CHECK_EQ(f.button(Button::Center), true);
    CHECK_EQ(f.button(Button::Up), false);
    f.runFrames(20.0f);
    CHECK(!f.plugin.failed());
}

TEST(sample_github) {
    Fixture none;
    CHECK(none.startFile("github"));
    none.draw();
    CHECK(none.gfx.drew("Give the repository as"));
    CHECK_EQ(none.services.requests.size(), (size_t)0);

    Fixture f;
    CHECK(f.startFile("github", {{"token", "ghp_x"}}, {{"repo", "acme/app"}}));
    CHECK_EQ(f.plugin.cardTitle("1"), "app");
    CHECK_EQ(f.services.requests.size(), (size_t)2);
    const HttpRequest& runs = f.services.requests[0].request;
    CHECK(contains(runs.url, "https://api.github.com/repos/acme/app/actions/runs?"));
    CHECK(contains(runs.url, "branch=main"));
    CHECK(hasHeader(runs, "Authorization", "Bearer ghp_x"));
    CHECK(contains(f.services.requests[1].request.url, "q=repo:acme/app+type:pr+state:open"));

    // The newest commit ("a") has a failed run; an older commit's success does not count.
    f.respond(0, 200, R"({"total_count":3,"workflow_runs":[
        {"name":"Lint","status":"completed","conclusion":"success","head_sha":"a","id":1},
        {"name":"CI","status":"completed","conclusion":"failure","head_sha":"a"},
        {"name":"CI","status":"completed","conclusion":"success","head_sha":"b"}]})");
    f.respond(1, 200, R"({"total_count":7,"items":[{"title":"not kept"}]})");
    CHECK_EQ(f.services.led, "#e74c3c");
    f.draw();
    CHECK(f.gfx.drew("large center 7"));
    CHECK(f.gfx.drew("open pull requests"));
    CHECK(f.gfx.drew("#e74c3c small center main is failing: CI"));

    // Five minutes later it is fixed and building again.
    pass(f, 301, false);
    CHECK_EQ(f.services.requests.size(), (size_t)4);
    f.respond(2, 200, R"({"workflow_runs":[{"name":"CI","status":"in_progress","conclusion":null,"head_sha":"c"}]})");
    f.respond(3, 200, R"({"total_count":1})");
    CHECK_EQ(f.services.led, "default");
    f.draw();
    CHECK(f.gfx.drew("main is building"));
    CHECK(f.gfx.drew("open pull request"));
    CHECK(!f.gfx.drew("open pull requests"));

    Fixture missing;
    CHECK(missing.startFile("github", {}, {{"repo", "acme/gone"}, {"branch", "trunk"}}));
    CHECK(missing.services.requests[0].request.headers.size() == 1);  // no token: no Authorization
    CHECK(contains(missing.services.requests[0].request.url, "branch=trunk"));
    missing.respond(0, 404, R"({"message":"Not Found"})");
    missing.draw();
    CHECK(missing.gfx.drew("No such repository."));
    CHECK(!missing.plugin.failed());
}

TEST(sample_uptime) {
    Fixture none;
    CHECK(none.startFile("uptime"));
    none.draw();
    CHECK(none.gfx.drew("Add the addresses"));

    Fixture f;
    CHECK(f.startFile("uptime", {{"sites", "a.example  http://b.example/health, c.example"}}));
    CHECK_EQ(f.services.requests.size(), (size_t)3);
    CHECK_EQ(f.services.requests[0].request.url, "https://a.example");
    CHECK_EQ(f.services.requests[1].request.url, "http://b.example/health");
    f.respond(0, 200, "<html>");
    f.respond(1, 503, "down for maintenance");
    f.respond(2, 0, "", "response too large (max 48 KB)");  // a big page is still an answer
    CHECK_EQ(f.services.led, "#e74c3c");
    f.draw();
    CHECK(f.gfx.drew("#e74c3c small left 1 down"));
    CHECK(f.gfx.drew("small right up"));
    CHECK(f.gfx.drew("#e74c3c small right answered 503"));
    CHECK(f.gfx.drew("small left b.example"));
    CHECK(f.services.logged("b.example: HTTP 503"));

    CHECK_EQ(f.button(Button::Center), true);   // check again now
    CHECK_EQ(f.services.requests.size(), (size_t)6);
    f.respond(3, 200, "");
    f.respond(4, 204, "");
    f.respond(5, 0, "", "connection refused");
    CHECK_EQ(f.services.led, "#e74c3c");
    f.draw();
    CHECK(f.gfx.drew("small right no answer"));
    pass(f, 121, false);
    f.respond(6, 200, "");
    f.respond(7, 200, "");
    f.respond(8, 301, "");
    CHECK_EQ(f.services.led, "default");
    f.draw();
    CHECK(f.gfx.drew("small left All up"));
}

TEST(sample_deploy) {
    Fixture none;
    CHECK(none.startFile("deploy"));
    CHECK_EQ(none.button(Button::Center), true);
    none.draw();
    CHECK(none.gfx.drew("Set the web address"));
    CHECK_EQ(none.services.requests.size(), (size_t)0);

    Fixture f;
    CHECK(f.startFile("deploy", {{"url", "https://example.com/deploy"}, {"label", "Ship it"}, {"secret", "Bearer abc"}}));
    f.draw();
    CHECK(f.gfx.drew("large center Ship it"));
    CHECK_EQ(f.button(Button::Center), true);   // the first press only asks
    CHECK_EQ(f.services.requests.size(), (size_t)0);
    CHECK_EQ(f.services.led, "#e67e22");
    f.draw();
    CHECK(f.gfx.drew("Sure?"));
    pass(f, 5);                                  // not confirmed in time
    f.draw();
    CHECK(f.gfx.drew("large center Ship it"));
    CHECK_EQ(f.services.led, "default");

    CHECK_EQ(f.button(Button::Center), true);
    CHECK_EQ(f.button(Button::Center), true);   // confirmed
    CHECK_EQ(f.services.requests.size(), (size_t)1);
    const HttpRequest& request = f.services.requests[0].request;
    CHECK_EQ(request.method, "POST");
    CHECK(hasHeader(request, "Authorization", "Bearer abc"));
    CHECK(contains(request.body, R"("label":"Ship it")"));
    CHECK_EQ(f.button(Button::Center), true);   // while it is on its way, presses do nothing
    CHECK_EQ(f.services.requests.size(), (size_t)1);
    f.respond(0, 201, "{}");
    f.draw();
    CHECK(f.gfx.drew("large center Done"));
    CHECK(f.services.stored.count("last") == 1);
    pass(f, 9);
    f.draw();
    CHECK(f.gfx.drew("Last one just now"));

    CHECK_EQ(f.button(Button::Center), true);
    CHECK_EQ(f.button(Button::Center), true);
    f.respond(1, 500, "boom");
    f.draw();
    CHECK(f.gfx.drew("large center Failed"));
    CHECK(f.gfx.drew("The other side answered"));
    CHECK(!f.plugin.failed());
}

TEST(sample_homeassistant) {
    Fixture none;
    CHECK(none.startFile("homeassistant", {}, {{"entity", "light.desk"}}));
    none.draw();
    CHECK(none.gfx.drew("Set the address and the"));
    CHECK_EQ(none.services.requests.size(), (size_t)0);

    Fixture f;
    CHECK(f.startFile("homeassistant", {{"url", "http://10.0.0.5:8123/"}, {"token", "tok"}},
                      {{"entity", "sensor.office_temperature"}}));
    CHECK_EQ(f.services.requests[0].request.url, "http://10.0.0.5:8123/api/states/sensor.office_temperature");
    CHECK(hasHeader(f.services.requests[0].request, "Authorization", "Bearer tok"));
    f.respond(0, 200, R"({"entity_id":"sensor.office_temperature","state":"21.456","last_changed":"x",
        "attributes":{"friendly_name":"Office","unit_of_measurement":"°C","device_class":"temperature"}})");
    CHECK_EQ(f.plugin.cardTitle("1"), "Office");
    f.draw();
    CHECK(f.gfx.drew("small left Office"));
    CHECK(f.gfx.drew("large center 21.5 °C"));
    CHECK_EQ(f.button(Button::Center), false);  // this card's button does nothing: it switches cards

    Fixture light;
    CHECK(light.startFile("homeassistant", {{"url", "http://10.0.0.5:8123"}, {"token", "tok"}},
                          {{"entity", "light.desk"}, {"press", "toggle"}}));
    light.respond(0, 200, R"({"state":"off","attributes":{"friendly_name":"Desk lamp"}})");
    light.draw();
    CHECK(light.gfx.drew("large center Off"));
    CHECK(light.gfx.drew("Centre to toggle"));
    CHECK_EQ(light.button(Button::Center), true);
    const HttpRequest& call = light.services.requests[1].request;
    CHECK_EQ(call.method, "POST");
    CHECK_EQ(call.url, "http://10.0.0.5:8123/api/services/homeassistant/toggle");
    CHECK_EQ(call.body, R"({"entity_id":"light.desk"})");
    light.respond(1, 200, "[]");
    CHECK_EQ(light.services.requests.size(), (size_t)3);  // and it looks at the new state
    light.respond(2, 200, R"({"state":"on","attributes":{"friendly_name":"Desk lamp"}})");
    light.draw();
    CHECK(light.gfx.drew("large center On"));
    CHECK(light.gfx.drew("#2ecc71 small center Done"));

    Fixture refused;
    CHECK(refused.startFile("homeassistant", {{"url", "http://10.0.0.5:8123"}, {"token", "old"}}, {{"entity", "light.desk"}}));
    refused.respond(0, 401, "401: Unauthorized");
    refused.draw();
    CHECK(refused.gfx.drew("Home Assistant refused the"));
    CHECK_EQ(refused.button(Button::Center), false);
}

TEST(sample_homey) {
    Fixture f;
    CHECK(f.startFile("homey", {{"url", "http://10.0.0.9"}, {"key", "k"}}, {{"device", "abc-123"}, {"flow", "f-1"}}));
    CHECK_EQ(f.services.requests[0].request.url, "http://10.0.0.9/api/manager/devices/device/abc-123");
    CHECK(hasHeader(f.services.requests[0].request, "Authorization", "Bearer k"));
    f.respond(0, 200, R"({"id":"abc-123","name":"Thermostat","zone":"z","capabilitiesObj":{
        "measure_temperature":{"value":20.5,"units":"°C","title":"Temperature"},"onoff":{"value":true,"title":"On"}}})");
    f.draw();
    CHECK(f.gfx.drew("small left Thermostat"));
    CHECK(f.gfx.drew("large center 20.5 °C"));
    CHECK_EQ(f.button(Button::Center), true);
    CHECK_EQ(f.services.requests[1].request.method, "POST");
    CHECK_EQ(f.services.requests[1].request.url, "http://10.0.0.9/api/manager/flow/flow/f-1/trigger");
    f.respond(1, 200, "{}");
    f.draw();
    CHECK(f.gfx.drew("Flow started"));

    Fixture other;
    CHECK(other.startFile("homey", {{"url", "http://10.0.0.9"}, {"key", "k"}},
                          {{"device", "abc-123"}, {"value", "onoff"}, {"flow", "f-2"}, {"advanced", "true"}}));
    other.respond(0, 200, R"({"name":"Lamp","capabilitiesObj":{"onoff":{"value":false,"title":"On"}}})");
    other.draw();
    CHECK(other.gfx.drew("large center Off"));
    other.button(Button::Center);
    CHECK_EQ(other.services.requests[1].request.url, "http://10.0.0.9/api/manager/flow/advancedflow/f-2/trigger");

    Fixture wrong;
    CHECK(wrong.startFile("homey", {{"url", "http://10.0.0.9"}, {"key", "k"}}, {{"device", "abc-123"}, {"value", "measure_power"}}));
    wrong.respond(0, 200, R"({"name":"Lamp","capabilitiesObj":{"onoff":{"value":false},"dim":{"value":0.5}}})");
    wrong.draw();
    CHECK(wrong.gfx.drew("It has no measure_power."));
    CHECK(wrong.services.logged("Lamp has: dim, onoff"));
    CHECK_EQ(wrong.button(Button::Center), false);

    Fixture none;
    CHECK(none.startFile("homey"));
    none.draw();
    CHECK(none.gfx.drew("Set the address and the"));

    // The lists behind the Device and Flow settings. Homey says a lot about each
    // device; only names and rooms are kept.
    Fixture lists;
    CHECK(lists.startFile("homey", {{"url", "http://10.0.0.9"}, {"key", "k"}}, {{"device", "abc-123"}}));
    lists.plugin.choices("listDevices", 1, {"device", {}});
    CHECK_EQ(lists.services.requests[1].request.url, "http://10.0.0.9/api/manager/devices/device/");
    CHECK(lists.services.requests[1].request.maxBytes > 48 * 1024);  // `only` allows a large answer
    lists.respond(1, 200, R"({"d-2":{"id":"d-2","name":"Thermostat","zoneName":"Hall","capabilitiesObj":{"a":{"value":1}}},
        "d-1":{"id":"d-1","name":"Lamp","capabilitiesObj":{}}})");
    CHECK_EQ(lists.services.choiceAnswers[0].choices.size(), (size_t)2);
    CHECK_EQ(lists.services.choiceAnswers[0].choices[0].value, "d-1");
    CHECK_EQ(lists.services.choiceAnswers[0].choices[1].label, "Thermostat (Hall)");
    // Which flows: the kind the form's box says, ticked or not, saved or not.
    lists.plugin.choices("listFlows", 2, {"device", {}});
    CHECK_EQ(lists.services.requests[2].request.url, "http://10.0.0.9/api/manager/flow/flow/");
    lists.respond(2, 200, R"({"f-1":{"name":"Good night"}})");
    CHECK_EQ(lists.services.choiceAnswers[1].choices[0].label, "Good night");
    lists.plugin.choices("listFlows", 3, {"device", {{"advanced", "true"}}});
    CHECK_EQ(lists.services.requests[3].request.url, "http://10.0.0.9/api/manager/flow/advancedflow/");
    lists.respond(3, 401, "");
    CHECK(contains(lists.services.choiceAnswers[2].error, "refused the API key"));
}

TEST(sample_reminders_count_down_over_a_clock_change) {
    Fixture f;
    f.services.zone = "CET-1CEST,M3.5.0,M10.5.0/3";
    f.services.epoch = 1774735200;  // Saturday 28 March 2026, 23:00. That night 02:00 becomes 03:00
    CHECK(f.startFile("reminders", {{"list", "10:30 Stand-up; 23:00 Bed"}}));
    f.draw();
    CHECK(f.gfx.drew("large center 10h 30m"));  // not 11h 30m: the night is an hour short
    CHECK(f.gfx.drew("Tomorrow 10:30  Stand-up"));
    f.services.epoch -= 3600;  // 22:00: bed time is today, an hour away
    f.draw();
    CHECK(f.gfx.drew("large center 1h 00m"));
    CHECK(f.gfx.drew("center 23:00  Bed"));

    Fixture doorbell;
    CHECK(doorbell.startFile("doorbell"));
    CHECK(doorbell.plugin.message("1", R"({"text":"One","color":"GREEN"})"));
    CHECK(doorbell.plugin.message("1", R"({"text":"Two","color":"not-a-colour"})"));
    CHECK(doorbell.plugin.message("1", R"({"text":"Three","color":["red"]})"));
    doorbell.draw();
    CHECK(!doorbell.plugin.failed());
    CHECK(doorbell.gfx.drew("#2ecc71 tiny left One"));   // a colour gfx knows is used
    CHECK(doorbell.gfx.drew("#ffffff tiny left Two"));   // one it does not know is not
    CHECK(doorbell.gfx.drew("center Three"));
}

TEST(sample_trains) {
    Fixture f;
    const long long now = f.services.epoch;
    CHECK(f.startFile("trains", {}, {{"station", "Gent Sint Pieters"}}));
    CHECK_EQ(f.services.requests[0].request.url,
             "https://api.irail.be/v1/liveboard?format=json&lang=en&arrdep=departure&station=Gent%20Sint%20Pieters");
    const std::string board = R"({"version":"1.3","station":"Antwerp-Central","departures":{"number":"4","departure":[
        {"id":"0","station":"Brugge","time":")" + std::to_string(now + 420) + R"(","delay":"0","canceled":"0","platform":"5","vehicle":"x"},
        {"id":"1","station":"Antwerp-Central","time":")" + std::to_string(now + 900) + R"(","delay":"300","canceled":"0","platform":"8"},
        {"id":"2","station":"Oostende","time":")" + std::to_string(now + 1200) + R"(","delay":"0","canceled":"1","platform":"3"},
        {"id":"3","station":"Left already","time":")" + std::to_string(now - 600) + R"(","delay":"0","canceled":"0","platform":"1"}]}})";
    f.respond(0, 200, board);
    CHECK_EQ(f.plugin.cardTitle("1"), "Antwerp-Central");
    f.draw();
    CHECK(f.gfx.drew("small left Antwerp-Central"));
    CHECK(f.gfx.drew("medium left 7 min"));
    CHECK(f.gfx.drew("medium left Brugge"));
    CHECK(f.gfx.drew("medium right 5"));
    CHECK(f.gfx.drew("medium left 20 min"));     // fifteen minutes plus five of delay
    CHECK(f.gfx.drew("#e74c3c small left +5"));
    CHECK(f.gfx.drew("#e74c3c small left cancelled"));
    CHECK(!f.gfx.drew("Left already"));

    // Six minutes on, without a new answer, the minutes have counted down.
    f.services.epoch += 360;
    pass(f, 2);
    f.draw();
    CHECK(f.gfx.drew("medium left 1 min"));
    CHECK(f.gfx.drew("medium left 14 min"));

    Fixture filtered;
    CHECK(filtered.startFile("trains", {}, {{"towards", "ANTW"}}));
    filtered.respond(0, 200, board);
    filtered.draw();
    CHECK(filtered.gfx.drew("Antwerp-Central"));
    CHECK(!filtered.gfx.drew("Brugge"));

    Fixture unknown;
    CHECK(unknown.startFile("trains", {}, {{"station", "Nowhere"}}));
    unknown.respond(0, 404, R"({"error":404,"message":"Could not find station Nowhere"})");
    unknown.draw();
    CHECK(unknown.gfx.drew("iRail knows no station"));
    CHECK(!unknown.plugin.failed());
}

TEST(sample_rain) {
    struct Case { const char* amounts; const char* expect; };
    const Case cases[] = {
        {"[0,0,0,0,0,0,0,0]", "Dry for the next two"},
        {"[0,0,0.4,1.2,0,0,0,0]", "Rain in about 30"},
        {"[0,0,0,0,0,0.3,0,0]", "Rain in about 1.2"},   // an hour and a quarter
        {"[0.5,0.2,0,0,0,0,0,0]", "Raining. It stops in"},
        {"[0.5,0.2,0.3,1,2,3,1,1]", "Rain for the next two"},
        {"[0.05,0.02,0,0,0,0,0,0]", "Dry for the next two"},  // a few drops are not rain
    };
    for (const Case& c : cases) {
        Fixture f;
        CHECK(f.startFile("rain", {{"city", "Antwerpen"}}));
        f.respond(0, 200, R"({"results":[{"name":"Antwerp","latitude":51.22,"longitude":4.40}]})");
        CHECK(contains(f.services.requests[1].request.url, "minutely_15=precipitation&forecast_minutely_15=8"));
        f.respond(1, 200, std::string(R"({"minutely_15":{"time":[],"precipitation":)") + c.amounts + "}}");
        f.draw();
        if (!f.gfx.drew(c.expect)) printf("  %s: expected %s, drew\n%s", c.amounts, c.expect, f.gfx.dump().c_str());
        CHECK(f.gfx.drew(c.expect));
        CHECK(f.gfx.drew("small left Rain in Antwerp"));
        CHECK(!f.plugin.failed());
    }
    Fixture nowhere;
    CHECK(nowhere.startFile("rain", {{"city", "Xyzzy"}}));
    nowhere.respond(0, 200, "{}");
    nowhere.draw();
    CHECK(nowhere.gfx.drew("No city called Xyzzy"));
}

TEST(sample_queues) {
    const std::string park = R"({"lands":[{"id":1,"name":"Land","rides":[
        {"id":1,"name":"Baron 1898","is_open":true,"wait_time":45,"last_updated":"x"},
        {"id":2,"name":"Droomvlucht","is_open":true,"wait_time":70},
        {"id":3,"name":"Python","is_open":false,"wait_time":0}]}],
        "rides":[{"id":4,"name":"Carrousel","is_open":true,"wait_time":5}]})";
    Fixture f;
    CHECK(f.startFile("queues"));
    CHECK_EQ(f.services.requests[0].request.url, "https://queue-times.com/parks/160/queue_times.json");
    f.respond(0, 200, park);
    f.draw();
    CHECK(f.gfx.drew("small left Efteling"));
    CHECK(f.gfx.drew("Queue-Times.com"));        // the credit they ask for
    CHECK(f.gfx.drew("#e74c3c medium right 70 min"));
    CHECK(f.gfx.drew("#e67e22 medium right 45 min"));
    CHECK(f.gfx.drew("#2ecc71 medium right 5 min"));
    CHECK(!f.gfx.drew("Python"));                // closed rides are not among the longest waits
    // Longest first.
    CHECK(f.gfx.dump().find("Droomvlucht") < f.gfx.dump().find("Baron 1898"));

    Fixture chosen;
    CHECK(chosen.startFile("queues", {{"park", "51"}, {"title", "Europa-Park"}, {"rides", " python | baron "}}));
    CHECK(contains(chosen.services.requests[0].request.url, "/parks/51/"));
    chosen.respond(0, 200, park);
    chosen.draw();
    CHECK(chosen.gfx.drew("medium right closed"));
    CHECK(chosen.gfx.drew("45 min"));
    CHECK(!chosen.gfx.drew("Droomvlucht"));

    Fixture closed;
    CHECK(closed.startFile("queues"));
    closed.respond(0, 200, R"({"lands":[{"rides":[{"name":"Python","is_open":false,"wait_time":0}]}],"rides":[]})");
    closed.draw();
    CHECK(closed.gfx.drew("large center Closed"));

    Fixture gone;
    CHECK(gone.startFile("queues", {{"park", "99999"}}));
    gone.respond(0, 404, "Not found");
    gone.draw();
    CHECK(gone.gfx.drew("Queue-Times has no park"));
}

TEST(sample_joke) {
    Fixture f;
    CHECK(f.startFile("joke"));
    f.draw();
    CHECK(f.gfx.drew("Thinking of one..."));
    CHECK_EQ(f.services.requests[0].request.url, "https://icanhazdadjoke.com/");
    CHECK(hasHeader(f.services.requests[0].request, "Accept", "application/json"));
    f.respond(0, 200, R"({"id":"x","joke":"Why did the scarecrow win an award? He was outstanding in his field.","status":200})");
    f.draw();
    CHECK(f.gfx.drew("medium center Why did the scarecrow"));
    pass(f, 3);
    f.draw();
    pass(f, 3);
    CHECK(!f.needsDraw());                       // a joke that fits stays put

    CHECK_EQ(f.button(Button::Center), true);   // another one
    CHECK_EQ(f.services.requests.size(), (size_t)2);
    std::string longJoke;
    for (int i = 0; i < 40; i++) longJoke += "and then the hedgehog said ";
    f.respond(1, 200, "{\"joke\":\"" + longJoke + "\"}");
    f.draw();
    CHECK(f.gfx.drew("small center and then the hedgehog"));   // too long for the bigger font
    pass(f, 4);
    CHECK(f.needsDraw());                        // and it scrolls
    f.draw();

    Fixture offline;
    CHECK(offline.startFile("joke"));
    offline.respond(0, 0, "", "no WiFi");
    offline.draw();
    CHECK(offline.gfx.drew("no WiFi"));
}

TEST(sample_crypto) {
    Fixture f;
    CHECK(f.startFile("crypto", {{"currency", "usd"}}, {{"coin", " Bitcoin "}}));
    CHECK_EQ(f.plugin.cardTitle("1"), "Bitcoin");
    CHECK_EQ(f.services.requests[0].request.url,
             "https://api.coingecko.com/api/v3/coins/bitcoin/market_chart?days=1&vs_currency=usd");
    f.respond(0, 200, R"({"prices":[[1794685800000,50000.25],[1794686100000,50500],[1794686400000,51000.4]],
        "market_caps":[[1,2]],"total_volumes":[[1,2]]})");
    f.draw();
    CHECK(f.gfx.drew("large center 51,000"));
    CHECK(f.gfx.drew("#2ecc71 small center USD, +2.0% in a day"));
    CHECK(f.gfx.drew("line "));

    pass(f, 301, false);
    f.respond(1, 200, R"({"prices":[[1,0.5],[2,0.25]]})");
    f.draw();
    CHECK(f.gfx.drew("center 0.2500"));
    CHECK(f.gfx.drew("#e74c3c small center USD, -50.0% in a day"));

    Fixture unknown;
    CHECK(unknown.startFile("crypto", {}, {{"coin", "notacoin"}}));
    CHECK(contains(unknown.services.requests[0].request.url, "vs_currency=eur"));
    unknown.respond(0, 404, R"({"error":"coin not found"})");
    unknown.draw();
    CHECK(unknown.gfx.drew("CoinGecko has no coin"));
    CHECK(!unknown.plugin.failed());
}

TEST(sample_space) {
    Fixture f;
    CHECK(startKind(f, "space", "station", {}, {{"city", "Antwerpen"}}));
    CHECK(contains(f.services.requests[0].request.url, "geocoding-api.open-meteo.com/v1/search?count=1&name=Antwerpen"));
    f.respond(0, 200, R"({"results":[{"name":"Antwerp","latitude":51.22,"longitude":4.40}]})");
    CHECK_EQ(f.services.requests[1].request.url, "https://api.wheretheiss.at/v1/satellites/25544");
    // Over Western Australia: a long way to the east.
    f.respond(1, 200, R"({"name":"iss","latitude":-29.17,"longitude":123.61,"visibility":"daylight","velocity":27538.3})");
    f.draw();
    CHECK(f.gfx.drew(" km"));
    CHECK(f.gfx.drew("to the east of Antwerp"));
    CHECK(f.gfx.drew("Over 29°S 124°E, in"));
    CHECK_EQ(f.services.led, "default");

    // Half a minute later it is over the Netherlands: look up.
    pass(f, 31, false);
    CHECK_EQ(f.services.requests.size(), (size_t)3);   // the city is not looked up again
    f.respond(2, 200, R"({"latitude":52.4,"longitude":4.9,"visibility":"eclipsed"})");
    CHECK_EQ(f.services.led, "#3498db for 20000ms");
    f.draw();
    CHECK(f.gfx.drew("large center 136 km"));
    CHECK(f.gfx.drew("above your horizon"));
    CHECK(f.gfx.drew("in the dark"));

    Fixture launch;
    const long long now = launch.services.epoch;
    CHECK(startKind(launch, "space", "launch"));
    CHECK_EQ(launch.services.requests[0].request.url, "https://fdo.rocketlaunch.live/json/launches/next/1");
    launch.respond(0, 200, R"({"valid_auth":false,"count":1,"result":[{"id":1,"name":"Starlink 12-3","sort_date":")" +
        std::to_string(now + 7260) + R"(","provider":{"id":1,"name":"SpaceX"},"vehicle":{"name":"Falcon 9"},
        "pad":{"name":"SLC-40","location":{"name":"Cape Canaveral SFS","country":"United States"}},"weather_summary":"x"}]})");
    launch.draw();
    CHECK(launch.gfx.drew("RocketLaunch.Live"));
    CHECK(launch.gfx.drew("large center 2h 01m"));
    CHECK(launch.gfx.drew("small center Falcon 9, Starlink 12-3"));
    CHECK(launch.gfx.drew("small center SpaceX, Cape Canaveral"));
    pass(launch, 65);
    CHECK_EQ(launch.services.requests.size(), (size_t)1);   // launches are not asked for every half minute
    launch.services.epoch += 7000;
    pass(launch, 2);
    launch.draw();
    CHECK(launch.gfx.drew("large center 4:"));             // minutes and seconds in the last hour
    launch.services.epoch += 1000;                          // it has gone: ask what is next
    pass(launch, 31);
    CHECK_EQ(launch.services.requests.size(), (size_t)2);
    CHECK(!launch.plugin.failed());
}

TEST(pictures_pack_into_runs) {
    // 600 black pixels, then one white, then 3 red: (255,0)(255,0)(90,0)(1,white)(3,red).
    std::vector<uint8_t> raw(600 * 2, 0);
    raw.insert(raw.end(), {0xFF, 0xFF});
    for (int i = 0; i < 3; i++) raw.insert(raw.end(), {0x00, 0xF8});
    std::vector<uint8_t> packed;
    CHECK(packPixelRuns(raw, packed));
    const std::vector<uint8_t> expected = {255, 0, 0, 255, 0, 0, 90, 0, 0, 1, 0xFF, 0xFF, 3, 0x00, 0xF8};
    CHECK(packed == expected);

    // Unpacking gives the same picture back.
    std::vector<uint8_t> back;
    for (size_t i = 0; i + 2 < packed.size(); i += 3) {
        for (int n = 0; n < packed[i]; n++) back.insert(back.end(), {packed[i + 1], packed[i + 2]});
    }
    CHECK(back == raw);

    // Noise does not get smaller: the caller sends it as it is.
    std::vector<uint8_t> noise;
    for (int i = 0; i < 2000; i++) noise.push_back(static_cast<uint8_t>(i * 37 + (i >> 3)));
    CHECK(!packPixelRuns(noise, packed));
    std::vector<uint8_t> nothing;
    CHECK(packPixelRuns(nothing, packed) && packed.empty());
}

TEST(plugin_names) {
    CHECK(isValidPluginName("weather"));
    CHECK(isValidPluginName("my-plugin_2"));
    CHECK(!isValidPluginName(""));
    CHECK(!isValidPluginName("Weather"));
    CHECK(!isValidPluginName("../etc"));
    CHECK(!isValidPluginName("a.lua"));
    CHECK(!isValidPluginName(std::string(33, 'a')));
}

// ------------------------------------------------------- three-button games

TEST(sample_rps) {
    Fixture f;
    CHECK(f.startFile("rps"));
    f.draw();
    CHECK(f.gfx.drew("Centre to start"));
    CHECK_EQ(f.button(Button::Down), false);    // no match on: up and down switch cards
    CHECK_EQ(f.button(Button::Center), true);
    f.draw();
    CHECK(f.gfx.drew("Pick a hand"));
    CHECK(f.gfx.drew("small left Scissors"));
    CHECK_EQ(f.button(Button::Up), true);       // rock
    CHECK_EQ(f.button(Button::Down), true);     // too late to change your mind
    f.runFrames(0.5f);
    f.draw();
    CHECK(f.gfx.drew("loud center Paper..."));  // the count: rock, paper, scissors
    f.runFrames(0.6f);
    f.draw();
    CHECK(f.gfx.drew("You win") || f.gfx.drew("Max wins") || f.gfx.drew("Draw"));
    CHECK(f.gfx.drew("small center You"));

    // Whole matches, one after the other: every one ends with a winner.
    for (int i = 0; i < 300 && !f.plugin.failed(); i++) {
        f.button((Button)(i % 3));
        f.runFrames(0.7f);
    }
    CHECK(!f.plugin.failed());
    const int matches = atoi(f.services.stored["won"].c_str()) + atoi(f.services.stored["lost"].c_str());
    CHECK(matches >= 5);

    Fixture later;  // the tally is kept
    later.services.stored["won"] = "4";
    later.services.stored["lost"] = "2";
    CHECK(later.startFile("rps"));
    later.draw();
    CHECK(later.gfx.drew("Matches 4 - 2"));
}

TEST(sample_whack) {
    Fixture f;
    f.services.stored["best"] = "4";
    CHECK(f.startFile("whack"));
    f.draw();
    CHECK(f.gfx.drew("Best 4"));
    CHECK_EQ(f.button(Button::Up), false);
    CHECK_EQ(f.button(Button::Center), true);

    // Wait for the first hedgehog. Which burrow it is in shows from where the drawing is clipped.
    const Button buttons[] = {Button::Up, Button::Center, Button::Down};
    int burrow = -1;
    for (int i = 0; i < 40 && burrow < 0; i++) {
        f.runFrames(0.1f);
        f.draw();
        for (int b = 0; b < 3; b++) {
            if (f.gfx.drew("clip 0," + std::to_string(20 + b * 42 - 22) + " ")) burrow = b;
        }
    }
    CHECK(burrow >= 0);
    CHECK_EQ(f.button(buttons[burrow]), true);
    CHECK(contains(f.services.led, "#2ecc71"));
    f.draw();
    CHECK(f.gfx.drew("large center 1"));
    CHECK(f.gfx.drew("Bonk!"));
    CHECK_EQ(f.button(buttons[burrow]), true);  // a second tap on the same one is forgiven
    CHECK(contains(f.services.led, "#2ecc71"));
    // Straight away, before the next hedgehog can be up: a tap on an empty burrow costs a life.
    CHECK_EQ(f.button(buttons[(burrow + 1) % 3]), true);
    CHECK(contains(f.services.led, "#e74c3c"));

    f.runFrames(12.0f);                         // the rest get away
    f.draw();
    CHECK(f.gfx.drew("Game over"));
    CHECK(f.gfx.drew("Best 4"));
    CHECK_EQ(f.services.stored["best"], "4");
    CHECK_EQ(f.button(Button::Down), false);

    for (int i = 0; i < 600 && !f.plugin.failed(); i++) {
        f.button((Button)(i % 3));
        f.runFrames(0.3f);
    }
    CHECK(!f.plugin.failed());
}

std::string triviaQuestion(const std::string& question, int number) {
    const std::string n = std::to_string(number);
    return "{\"type\":\"multiple\",\"difficulty\":\"hard\",\"category\":\"Science%3A%20Computers\",\"question\":\"" +
           question + "\",\"correct_answer\":\"Right%20" + n +
           "\",\"incorrect_answers\":[\"Wrong%20A\",\"Wrong%20B\",\"Caf%C3%A9%20C\"]}";
}

TEST(sample_trivia) {
    Fixture f;
    CHECK(f.startFile("trivia", {{"category", "Computers"}, {"difficulty", "Hard"}}));
    CHECK_EQ(f.services.requests[0].request.url,
             "https://opentdb.com/api.php?amount=12&type=multiple&encode=url3986&category=18&difficulty=hard");
    f.draw();
    CHECK(f.gfx.drew("Fetching questions..."));
    CHECK_EQ(f.button(Button::Down), false);

    // Twelve questions, one of them far too long for the card.
    std::string body = "{\"response_code\":0,\"results\":[";
    for (int i = 1; i <= 11; i++) body += triviaQuestion("Is%20this%20%22question%22%20" + std::to_string(i) + "%3F", i) + ",";
    std::string longQuestion;
    for (int i = 0; i < 30; i++) longQuestion += "and%20then%20some%20more%20";
    body += triviaQuestion(longQuestion, 12) + "]}";
    f.respond(0, 200, body);
    f.draw();
    CHECK(f.gfx.drew("Centre to start"));
    CHECK_EQ(f.button(Button::Center), true);

    const Button buttons[] = {Button::Up, Button::Center, Button::Down};
    for (int asked = 1; asked <= 10; asked++) {
        f.draw();
        CHECK(f.gfx.drew("small center Is this \"question\" "));
        CHECK(!f.gfx.drew("and then some more"));
        int right = -1;
        for (int row = 0; row < 3; row++) {
            if (f.gfx.drew("text 28," + std::to_string(71 + row * 22) + " #ffffff small left Right ")) right = row;
        }
        CHECK(right >= 0);
        const bool answerRight = asked <= 7;
        CHECK_EQ(f.button(buttons[answerRight ? right : (right + 1) % 3]), true);
        CHECK_EQ(f.services.led, answerRight ? "#2ecc71 for 1000ms" : "#e74c3c for 1000ms");
        CHECK_EQ(f.button(Button::Up), true);   // while the answer shows, presses are swallowed
        f.draw();
        CHECK(f.gfx.drew("#1e6b3a"));           // the right answer lights up either way
        pass(f, 2);
    }
    f.draw();
    CHECK(f.gfx.drew("large center 7 / 10"));
    CHECK(f.gfx.drew("Best 7."));
    CHECK_EQ(f.services.stored["best"], "7");
    CHECK_EQ(f.services.requests.size(), (size_t)2);   // one question left: it fetched more for the next round
    CHECK_EQ(f.button(Button::Up), false);

    // Told to slow down: it asks again by itself, a few times.
    Fixture busy;
    CHECK(busy.startFile("trivia"));
    CHECK_EQ(busy.services.requests[0].request.url, "https://opentdb.com/api.php?amount=30&type=multiple&encode=url3986");
    busy.respond(0, 429, "{\"response_code\":5,\"results\":[]}");
    busy.draw();
    CHECK(busy.gfx.drew("Fetching questions..."));
    pass(busy, 7);
    CHECK_EQ(busy.services.requests.size(), (size_t)2);
    busy.respond(1, 200, "{\"response_code\":1,\"results\":[]}");
    busy.draw();
    CHECK(busy.gfx.drew("Too few questions"));

    Fixture offline;
    CHECK(offline.startFile("trivia"));
    offline.respond(0, 0, "", "no WiFi");
    offline.draw();
    CHECK(offline.gfx.drew("no WiFi"));
    CHECK(!offline.plugin.failed());
}

TEST(sample_runner) {
    Fixture f;
    f.services.stored["best"] = "3";
    CHECK(f.startFile("runner"));
    f.draw();
    CHECK(f.gfx.drew("loud center HOG RUN"));
    CHECK_EQ(f.button(Button::Up), false);      // no run on: up and down switch cards
    CHECK_EQ(f.button(Button::Center), true);
    CHECK_EQ(f.button(Button::Up), true);       // a jump
    CHECK_EQ(f.button(Button::Down), true);     // a roll
    f.runFrames(20.0f);                         // nobody steering: it runs into something
    f.draw();
    CHECK(f.gfx.drew("Ouch"));
    CHECK_EQ(f.services.led, "#e74c3c for 1500ms");
    CHECK(atoi(f.services.stored["best"].c_str()) >= 3);
    CHECK_EQ(f.button(Button::Down), false);

    // Ten minutes of jumping and rolling at random, starting again after every fall.
    for (int frame = 0; frame < 18000 && !f.plugin.failed(); frame++) {
        if (frame % 17 == 0) f.button(frame % 5 == 0 ? Button::Down : Button::Up);
        if (frame % 101 == 0) f.button(Button::Center);
        f.services.held[(int)Button::Down] = (frame / 50) % 7 == 0;
        f.services.now += 33;
        f.tick(f.services.now, true);
        if (f.needsDraw()) f.draw();
    }
    CHECK(!f.plugin.failed());
    CHECK(atoi(f.services.stored["best"].c_str()) > 3);   // somewhere in there it got further than 3
    CHECK(f.plugin.memoryUsed() < 96 * 1024);
}

TEST(sample_lander) {
    Fixture f;
    CHECK(f.startFile("lander"));
    f.draw();
    CHECK(f.gfx.drew("loud center LANDER"));
    CHECK_EQ(f.button(Button::Down), false);
    CHECK_EQ(f.button(Button::Center), true);
    CHECK_EQ(f.button(Button::Up), true);       // in flight all three belong to the game
    f.draw();
    CHECK(f.gfx.drew("Level 1"));
    f.runFrames(15.0f);                         // no engine: it comes down hard, or beside the pad
    f.draw();
    CHECK(f.gfx.drew("Too fast") || f.gfx.drew("Missed the pad") || f.gfx.drew("Too much sideways"));
    CHECK(f.gfx.drew("Centre to fly again"));
    CHECK_EQ(f.button(Button::Up), false);

    // Flown by a small autopilot, to see landings count and levels follow. It reads the
    // craft's position, which the plugin keeps to itself, so those are made global here.
    std::string source = readFile(g_pluginDir + "/lander.lua");
    const std::string keep = "local x, y, vx, vy, fuel, gravity\n";
    const std::string keepToo = "local ground, pad, stars ";
    CHECK(source.find(keep) != std::string::npos);
    CHECK(source.find(keepToo) != std::string::npos);
    source.replace(source.find(keep), keep.size(), "\n");
    source.replace(source.find(keepToo), keepToo.size(), "ground, pad, stars = nil, nil, nil ");
    source += R"(
        function input.held(name)
          if not pad or not x then return false end
          local target = (pad.from + pad.to) / 2
          local want = math.max(-10, math.min(10, (target - x) * 0.4))
          if name == "up" then return vx > want + 1.5 end
          if name == "down" then return vx < want - 1.5 end
          local above = pad.y - y
          local lined = math.abs(target - x) < 6
          if not lined and above < 45 then return vy > -4 end   -- wait above the hills until over the pad
          return vy > (above > 40 and 24 or 10)
        end
    )";
    Fixture pilot;
    CHECK(pilot.begin("lander", source, {}));
    CHECK_EQ(pilot.button(Button::Center), true);
    int landings = 0;
    for (int frame = 0; frame < 30000 && !pilot.plugin.failed() && landings < 3; frame++) {
        pilot.services.now += 33;
        pilot.tick(pilot.services.now, true);
        if (!pilot.needsDraw()) continue;
        pilot.draw();
        if (pilot.gfx.drew("Landed!")) {
            landings++;
            CHECK(pilot.gfx.drew("Centre for the next one"));
            CHECK_EQ(pilot.button(Button::Center), true);
            pilot.draw();
            CHECK(pilot.gfx.drew("Level " + std::to_string(landings + 1)));
        } else if (pilot.gfx.drew("Centre to fly again")) {
            CHECK_EQ(pilot.button(Button::Center), true);
            landings = 0;
        }
    }
    CHECK(!pilot.plugin.failed());
    CHECK_EQ(landings, 3);
    CHECK(atoi(pilot.services.stored["best"].c_str()) >= 150);   // three landings at 50 each, plus fuel
}

TEST(sample_blocks) {
    Fixture f;
    CHECK(f.startFile("blocks"));
    f.draw();
    CHECK(f.gfx.drew("loud center BLOCKS"));
    CHECK_EQ(f.button(Button::Up), false);
    CHECK_EQ(f.button(Button::Center), true);
    f.draw();
    CHECK(f.gfx.drew("Next"));
    CHECK(f.gfx.drew("rect 37,5 12x12"));       // the pair starts at the top of the third column
    CHECK_EQ(f.button(Button::Up), true);       // one column along
    f.draw();
    CHECK(f.gfx.drew("rect 51,5 12x12"));
    for (int i = 0; i < 5; i++) f.button(Button::Up);  // and all the way round to where it started
    f.draw();
    CHECK(f.gfx.drew("rect 37,5 12x12"));
    CHECK_EQ(f.button(Button::Center), true);   // swap the two colours
    CHECK_EQ(f.button(Button::Down), true);     // dropped: it lies on the floor of the well
    f.draw();
    CHECK(f.gfx.drew("rect 37,117 12x12"));
    CHECK(f.gfx.drew("rect 37,103 12x12"));

    // With one colour only, the second pair makes four in a column: they light up, then go.
    Fixture red;
    CHECK(red.begin("blocks", "math.random = function() return 1 end\n" + readFile(g_pluginDir + "/blocks.lua"), {}));
    CHECK_EQ(red.button(Button::Center), true);
    CHECK_EQ(red.button(Button::Down), true);
    CHECK_EQ(red.button(Button::Down), true);
    red.draw();
    CHECK(red.gfx.drew("rect 37,117 12x12 #ffffff"));
    CHECK_EQ(red.button(Button::Down), true);   // swallowed while they go
    red.runFrames(0.4f);
    red.draw();
    CHECK(red.gfx.drew("large left 40"));
    CHECK(!red.gfx.drew("rect 37,117 12x12"));
    CHECK(!red.plugin.failed());

    // Ten minutes of pressing at random: chains, full wells and new games.
    for (int frame = 0; frame < 18000 && !f.plugin.failed(); frame++) {
        if (frame % 7 == 0) f.button((Button)((frame / 7) % 3));
        if (frame % 53 == 0) f.button(Button::Down);
        f.services.now += 33;
        f.tick(f.services.now, true);
        if (f.needsDraw()) f.draw();
    }
    CHECK(!f.plugin.failed());
    CHECK(atoi(f.services.stored["best"].c_str()) > 0);
    CHECK(f.plugin.memoryUsed() < 96 * 1024);
}

// ------------------------------------------------------------ plugin API v2

TEST(time_zones_parse) {
    TimeZone zone;
    CHECK(zone.parse("UTC0"));
    CHECK_EQ(zone.offsetAt(1763236140), 0);
    CHECK(zone.parse("CET-1CEST,M3.5.0,M10.5.0/3"));
    CHECK(zone.parse("EST5EDT,M3.2.0,M11.1.0"));
    CHECK(zone.parse("EST5EDT"));                       // no rules: the American ones
    CHECK(zone.parse("<+0545>-5:45"));                  // Kathmandu
    CHECK_EQ(zone.offsetAt(0), 5 * 3600 + 45 * 60);
    CHECK(zone.parse("<-03>3"));
    CHECK_EQ(zone.offsetAt(0), -3 * 3600);
    CHECK(zone.parse("NZST-12NZDT,M9.5.0,M4.1.0/3"));
    CHECK(zone.parse("IST-2IDT,M3.4.4/26,M10.5.0"));    // Jerusalem: a time past midnight
    CHECK(zone.parse("<-02>2<-01>,M3.5.0/-1,M10.5.0/0"));  // Nuuk: a negative time
    CHECK(zone.parse("WGT3WGST,J60/2,J300/2"));
    CHECK(zone.parse("AAA3BBB,59,300"));

    const char* const bad[] = {"", "Europe/Brussels", "CET", "C-1", "CET-1CEST,M3.5.0", "CET-1CEST,M13.5.0,M10.5.0",
                               "CET-1CEST,M3.6.0,M10.5.0", "CET-1CEST,M3.5.7,M10.5.0", "CET-99", "CET-1 junk",
                               "<+01-1", "CET-1CEST,J0,J5", "CET-1:99"};
    for (const char* text : bad) {
        if (zone.parse(text)) printf("  parsed \"%s\"\n", text);
        CHECK(!zone.parse(text));
        CHECK_EQ(zone.offsetAt(1763236140), 0);  // a zone that did not parse is UTC
    }
    CHECK(!zone.parse(nullptr));
}

TEST(every_time_zone_the_web_page_offers_is_understood) {
    // html/portal.js holds the list people pick from. The device gets the POSIX string.
    const std::string page = readFile(g_pluginDir + "/../html/portal.js");
    const size_t start = page.find("// <timezones>");
    const size_t end = page.find("// </timezones>");
    CHECK(start != std::string::npos && end != std::string::npos);
    if (start == std::string::npos || end == std::string::npos) return;
    int zones = 0, withSummerTime = 0;
    size_t at = start;
    for (;;) {
        // Lines look like "Europe/Brussels":"CET-1CEST,M3.5.0,M10.5.0/3",
        const size_t separator = page.find("\":\"", at);
        if (separator == std::string::npos || separator > end) break;
        const size_t nameStart = page.rfind('"', separator - 1) + 1;
        const size_t valueEnd = page.find('"', separator + 3);
        const std::string name = page.substr(nameStart, separator - nameStart);
        const std::string posix = page.substr(separator + 3, valueEnd - separator - 3);
        at = valueEnd;
        TimeZone zone;
        if (!zone.parse(posix.c_str())) printf("  %s: \"%s\" does not parse\n", name.c_str(), posix.c_str());
        CHECK(zone.parse(posix.c_str()));
        CHECK(posix.size() <= 64);  // what the device accepts
        const int32_t winter = zone.offsetAt(1736899200), summer = zone.offsetAt(1752537600);
        CHECK(winter >= -12 * 3600 && winter <= 14 * 3600);
        if (winter != summer) withSummerTime++;
        zones++;
        if (name == "Europe/Brussels") CHECK_EQ(summer, 7200);
        if (name == "Asia/Kolkata") CHECK_EQ(summer, 19800);
        if (name == "America/Los_Angeles") CHECK_EQ(winter, -8 * 3600);
        if (name == "Pacific/Chatham") CHECK_EQ(winter, 13 * 3600 + 45 * 60);
    }
    CHECK(zones > 300);
    CHECK(withSummerTime > 50);
}

TEST(summer_time_starts_and_ends_on_the_right_second) {
    TimeZone brussels;
    CHECK(brussels.parse("CET-1CEST,M3.5.0,M10.5.0/3"));
    bool dst = true;
    // 2025: summer time from 30 March 01:00 UTC to 26 October 01:00 UTC.
    CHECK_EQ(brussels.offsetAt(1743296399, &dst), 3600);
    CHECK(!dst);
    CHECK_EQ(brussels.offsetAt(1743296400, &dst), 7200);
    CHECK(dst);
    CHECK_EQ(brussels.offsetAt(1761440399, &dst), 7200);
    CHECK_EQ(brussels.offsetAt(1761440400, &dst), 3600);
    CHECK(!dst);
    // 2024 was a leap year; the last Sundays fell on 31 March and 27 October.
    CHECK_EQ(brussels.offsetAt(1711846799), 3600);
    CHECK_EQ(brussels.offsetAt(1711846800), 7200);
    CHECK_EQ(brussels.offsetAt(1729990799), 7200);
    CHECK_EQ(brussels.offsetAt(1729990800), 3600);

    TimeZone newYork;
    CHECK(newYork.parse("EST5EDT,M3.2.0,M11.1.0"));
    // 2025: from 9 March 07:00 UTC to 2 November 06:00 UTC.
    CHECK_EQ(newYork.offsetAt(1741503599), -5 * 3600);
    CHECK_EQ(newYork.offsetAt(1741503600), -4 * 3600);
    CHECK_EQ(newYork.offsetAt(1762063199), -4 * 3600);
    CHECK_EQ(newYork.offsetAt(1762063200), -5 * 3600);
    TimeZone newYorkShort;
    CHECK(newYorkShort.parse("EST5EDT"));
    CHECK_EQ(newYorkShort.offsetAt(1741503600), -4 * 3600);

    // South of the equator summer runs over new year.
    TimeZone sydney;
    CHECK(sydney.parse("AEST-10AEDT,M10.1.0,M4.1.0/3"));
    CHECK_EQ(sydney.offsetAt(1736899200, &dst), 11 * 3600);  // 15 January 2025
    CHECK(dst);
    CHECK_EQ(sydney.offsetAt(1752537600, &dst), 10 * 3600);  // 15 July 2025
    CHECK(!dst);
    // Back to standard time on 6 April 2025 at 03:00 summer time = 5 April 16:00 UTC.
    CHECK_EQ(sydney.offsetAt(1743868799), 11 * 3600);
    CHECK_EQ(sydney.offsetAt(1743868800), 10 * 3600);
    // And forward again on 5 October 2025 at 02:00 standard time = 4 October 16:00 UTC.
    CHECK_EQ(sydney.offsetAt(1759593599), 10 * 3600);
    CHECK_EQ(sydney.offsetAt(1759593600), 11 * 3600);

    // Day-of-year rules. J60 is 1 March in every year; 59 (counted from 0) is
    // 29 February in a leap year.
    TimeZone julian;
    CHECK(julian.parse("AAA0BBB,J60/0,J300/0"));
    CHECK_EQ(julian.offsetAt(1709251199), 0);     // 29 February 2024, 23:59:59
    CHECK_EQ(julian.offsetAt(1709251200), 3600);  // 1 March 2024
    TimeZone counted;
    CHECK(counted.parse("AAA0BBB,59/0,300/0"));
    CHECK_EQ(counted.offsetAt(1709164799), 0);     // 28 February 2024, 23:59:59
    CHECK_EQ(counted.offsetAt(1709164800), 3600);  // 29 February 2024
}

TEST(civil_time_knows_the_calendar) {
    TimeZone utc;
    CivilTime t = utc.toCivil(0);
    CHECK_EQ(t.year, 1970);
    CHECK_EQ(t.weekday, 4);  // a Thursday
    CHECK_EQ(t.yearDay, 0);
    t = utc.toCivil(1709208000);  // 29 February 2024, 12:00
    CHECK_EQ(t.month, 2);
    CHECK_EQ(t.day, 29);
    CHECK_EQ(t.yearDay, 59);
    CHECK_EQ(t.weekday, 4);
    t = utc.toCivil(-1);  // the second before 1970
    CHECK_EQ(t.year, 1969);
    CHECK_EQ(t.month, 12);
    CHECK_EQ(t.day, 31);
    CHECK_EQ(t.hour, 23);
    CHECK_EQ(t.second, 59);
    CHECK_EQ(t.weekday, 3);
    t = utc.toCivil(4102444800LL);  // 1 January 2100: past what 32 bits hold
    CHECK_EQ(t.year, 2100);
    CHECK_EQ(t.weekday, 5);
    CHECK_EQ(TimeZone::daysFromCivil(2000, 3, 1), (int64_t)11017);

    TimeZone brussels;
    brussels.parse("CET-1CEST,M3.5.0,M10.5.0/3");
    t = brussels.toCivil(1752522540);  // 14 July 2025, 19:49 UTC
    CHECK_EQ(formatTime("%Y-%m-%d %H:%M:%S %Z %z", t), "2025-07-14 21:49:00 CEST +0200");
    CHECK_EQ(formatTime("%a %A %b %B %j %e|%-d|%I %p|%-I|%y|%F %T|%R|100%%|%Q|%", t),
             "Mon Monday Jul July 195 14|14|09 PM|9|25|2025-07-14 21:49:00|21:49|100%|%Q|%");
    t = brussels.toCivil(1763164800);  // 15 November 2025, 00:00 UTC
    CHECK_EQ(formatTime("%I:%M %p %Z", t), "01:00 AM CET");
    TimeZone newfoundland;
    newfoundland.parse("NST3:30NDT,M3.2.0,M11.1.0");
    CHECK_EQ(formatTime("%z", newfoundland.toCivil(1763164800)), "-0330");
    CHECK_EQ(formatTime(std::string(600, 'x').c_str(), t).size(), (size_t)256);
}

TEST(plugins_tell_local_time) {
    Fixture f;
    f.services.zone = "CET-1CEST,M3.5.0,M10.5.0/3";
    f.services.epoch = 1752522540;  // Monday 14 July 2025, 19:49 UTC
    CHECK(f.start(R"(
        local d = time.date()
        print(d.year, d.month, d.day, d.hour, d.min, d.sec, d.wday, d.yday, d.isdst, d.offset, d.zone)
        local w = time.date(1763236140)                      -- a moment in winter
        print(w.hour, w.isdst, w.offset, w.wday)
        local ny = time.date(1763236140, "EST5EDT,M3.2.0,M11.1.0")
        print(ny.hour, ny.offset, ny.zone)
        print(time.date(nil, "").hour)                       -- "" is the device's zone, as an empty setting is
        print(time.format("%H:%M %A"), time.format("%H:%M", 1763236140), time.format("%H:%M", 0, "UTC0"))
        print(time.zone(), time.offset(), time.offset(1763236140), time.offset(nil, "<+0545>-5:45"))
        print(time.date(4102444800.0, "UTC0").year)          -- past 2038, as a float
    )"));
    CHECK(f.services.logged("2025 7 14 21 49 0 2 195 true 7200 CEST"));
    CHECK(f.services.logged("20 false 3600 7"));
    CHECK(f.services.logged("14 -18000 EST"));
    CHECK(f.services.logs.at(3) == "21");
    CHECK(f.services.logged("21:49 Monday 20:49 00:00"));
    CHECK(f.services.logged("CET-1CEST,M3.5.0,M10.5.0/3 7200 3600 20700"));
    CHECK(f.services.logged("2100"));

    // Before the clock is set there is no "now" to describe.
    Fixture unset;
    unset.services.epoch = 0;
    CHECK(unset.start("print(time.date(), time.format('%H:%M'), time.offset(), time.date(60).min)"));
    CHECK(unset.services.logged("nil nil 0 1"));

    // A device zone that makes no sense reads as UTC; one a plugin passes is its mistake.
    Fixture odd;
    odd.services.zone = "Europe/Brussels";
    CHECK(odd.start("print(time.date(3600).hour, time.zone())"));
    CHECK(odd.services.logged("1 Europe/Brussels"));
    Fixture empty;
    empty.services.zone = "";
    CHECK(empty.start("print(time.zone(), time.offset())"));
    CHECK(empty.services.logged("UTC0 0"));
    Fixture wrong;
    CHECK(!wrong.start("time.date(0, 'Europe/Brussels')"));
    CHECK(contains(wrong.plugin.error(), "not a POSIX time zone"));
    Fixture huge;
    CHECK(!huge.start("time.date(1e18)"));
    CHECK(contains(huge.plugin.error(), "not a time"));
}

TEST(a_clock_reading_becomes_a_moment) {
    TimeZone brussels;
    brussels.parse("CET-1CEST,M3.5.0,M10.5.0/3");
    CHECK_EQ(brussels.fromCivil(2025, 7, 14, 21, 49, 0), (int64_t)1752522540);   // summer
    CHECK_EQ(brussels.fromCivil(2025, 11, 15, 20, 49, 0), (int64_t)1763236140);  // winter
    // 29 March 2026: 02:00 becomes 03:00. Half past two never happens; it is read as half past three.
    CHECK_EQ(brussels.fromCivil(2026, 3, 29, 1, 59, 59), (int64_t)1774747800 - 1801);
    CHECK_EQ(brussels.fromCivil(2026, 3, 29, 2, 30, 0), (int64_t)1774747800);
    CHECK_EQ(brussels.fromCivil(2026, 3, 29, 3, 30, 0), (int64_t)1774747800);
    // 25 October 2026: 03:00 becomes 02:00 again. Half past two happens twice; the first one, unless told otherwise.
    CHECK_EQ(brussels.fromCivil(2026, 10, 25, 2, 30, 0), (int64_t)1792888200);
    CHECK_EQ(brussels.fromCivil(2026, 10, 25, 2, 30, 0, 1), (int64_t)1792888200);
    CHECK_EQ(brussels.fromCivil(2026, 10, 25, 2, 30, 0, 0), (int64_t)1792891800);
    CHECK_EQ(brussels.fromCivil(2026, 10, 25, 3, 0, 0), (int64_t)1792891800 + 1800);
    // Fields past their range carry over.
    CHECK_EQ(brussels.fromCivil(2025, 12, 32, 0, 0, 0), (int64_t)1767222000);  // 1 January 2026, 00:00 CET
    CHECK_EQ(brussels.fromCivil(2025, 13, 1, 0, 0, 0), (int64_t)1767222000);
    CHECK_EQ(brussels.fromCivil(2026, 1, 1, -1, 60, 0), (int64_t)1767222000);
    CHECK_EQ(brussels.fromCivil(2027, 0, 1, 0, 0, 0), (int64_t)1796079600);    // month 0: December the year before
    // South of the equator, where summer runs over new year, and a zone without summer time.
    TimeZone sydney;
    sydney.parse("AEST-10AEDT,M10.1.0,M4.1.0/3");
    CHECK_EQ(sydney.fromCivil(2026, 1, 15, 0, 0, 0), (int64_t)1768395600);
    TimeZone utc;
    CHECK_EQ(utc.fromCivil(2100, 1, 1, 0, 0, 0), (int64_t)4102444800LL);
    // There and back, an hour at a time over a whole year, both changes included.
    for (int64_t t = 1767222000; t < 1767222000 + 366 * 86400; t += 3600) {
        const CivilTime c = brussels.toCivil(t);
        CHECK_EQ(brussels.fromCivil(c.year, c.month, c.day, c.hour, c.minute, c.second, c.dst ? 1 : 0), t);
    }

    Fixture f;
    f.services.zone = "CET-1CEST,M3.5.0,M10.5.0/3";
    CHECK(f.start(R"(
        print(time.make({ year = 2025, month = 7, day = 14, hour = 21, min = 49 }))
        print(time.make({ year = 2025, month = 7, day = 14 }))                       -- midnight when no time is given
        print(time.make({ year = 2025, month = 7, day = 14, hour = 15, min = 49 }, "EST5EDT,M3.2.0,M11.1.0"))
        print(time.make({ year = 2026, month = 10, day = 25, hour = 2, min = 30 }),
              time.make({ year = 2026, month = 10, day = 25, hour = 2, min = 30, isdst = false }))
        print(time.make({ year = 2026, month = 3, day = 29, hour = 2, min = 30 }))
        local d = time.date(1763236140)
        print(time.make(d) == 1763236140, math.type(time.make(d)))                   -- what time.date gives goes back in
        local far = time.make({ year = 2100, month = 1, day = 1 }, "UTC0")
        print(math.type(far), time.date(far, "UTC0").year)                           -- past 2038: a float, as time.date takes
        print(time.make({ year = 2025, month = 12, day = 31 + 1, hour = 9.9 }))      -- carried over; fractions are dropped
    )"));
    CHECK(f.services.logged("1752522540"));
    CHECK(f.services.logged("1752444000"));
    CHECK_EQ(f.services.logs.at(2), "1752522540");
    CHECK(f.services.logged("1792888200 1792891800"));
    CHECK(f.services.logged("1774747800"));
    CHECK(f.services.logged("true integer"));
    CHECK(f.services.logged("float 2100"));
    CHECK(f.services.logged("1767254400"));

    const std::pair<const char*, const char*> wrong[] = {
        {"time.make({ year = 2025, month = 7 })", "`day` is missing"},
        {"time.make({ year = 2025, month = 7, day = 'x' })", "`day` is not a number"},
        {"time.make({ year = 2025, month = 7, day = 1e9 })", "`day` is out of range"},
        {"time.make({ year = 99999, month = 1, day = 1 })", "outside the years"},
        {"time.make({ year = 2025, month = 7, day = 1 }, 'Europe/Brussels')", "not a POSIX time zone"},
        {"time.make(1752522540)", "table expected"},
    };
    for (const auto& test : wrong) {
        Fixture bad;
        CHECK(!bad.start(test.first));
        if (!contains(bad.plugin.error(), test.second)) printf("  %s: %s\n", test.first, bad.plugin.error().c_str());
        CHECK(contains(bad.plugin.error(), test.second));
    }
}

TEST(a_setting_can_be_a_time_zone) {
    Fixture f;
    CHECK(f.start(R"(
        config = { zone = { label = "Time zone", type = "timezone" }, city = "Antwerp",
                   count = { type = "timezone", default = 3 } }
        cards = { city = { config = { tz = { label = "Zone", type = "timezone", default = "" } },
                           draw = function(card) ui.big(time.format("%H:%M", 1763236140, card.config.tz)) end } }
    )", {}));
    const auto& schema = f.plugin.configSchema();
    CHECK_EQ(schema.size(), (size_t)3);
    for (const auto& field : schema) {
        if (field.key == "zone") CHECK_EQ(field.type, "timezone");
        if (field.key == "city") CHECK_EQ(field.type, "string");
        if (field.key == "count") CHECK_EQ(field.type, "number");  // a time zone is text
    }
    CHECK_EQ(f.plugin.kinds()[0].config[0].type, "timezone");
    f.services.zone = "CET-1CEST,M3.5.0,M10.5.0/3";
    f.draw();
    CHECK(f.gfx.drew("20:49"));  // empty: the device's zone
    f.plugin.addCard("2", "city", {{"tz", "JST-9"}}, f.gfx);
    f.gfx.calls.clear();
    f.plugin.draw("2");
    CHECK(f.gfx.drew("04:49"));
}

TEST(http_request_takes_every_method) {
    Fixture f;
    CHECK(f.start(R"(
        local function cb(res) print("answer", res.status, res.body) end
        http.put("https://a.example/1", { on = true }, cb)
        http.patch("https://a.example/2", "raw", { headers = { ["X-Key"] = "k" }, insecure = true }, cb)
        http.delete("https://a.example/3", cb)
        http.delete("https://a.example/4", { timeout = 5 }, cb)
        http.request({ url = "https://a.example/5", method = "head" }, cb)
        http.request({ url = "http://a.example/6", method = "POST", body = { a = 1 }, timeout = 3,
                       headers = { Accept = "text/plain" }, insecure = true }, cb)
        http.request({ url = "https://a.example/7" }, cb)
        http.request({ url = "https://a.example/8", method = "DELETE", body = "gone" }, cb)
        http.request({ url = "https://a.example/9", method = "GET", body = "ignored", only = { "a" } }, cb)
    )"));
    const auto& r = f.services.requests;
    CHECK_EQ(r.size(), (size_t)9);
    CHECK_EQ(r[0].request.method, "PUT");
    CHECK_EQ(r[0].request.body, "{\"on\":true}");
    CHECK(hasHeader(r[0].request, "Content-Type", "application/json"));
    CHECK(!r[0].request.insecure);  // certificates are checked unless the plugin says otherwise
    CHECK_EQ(r[1].request.method, "PATCH");
    CHECK_EQ(r[1].request.body, "raw");
    CHECK(hasHeader(r[1].request, "X-Key", "k"));
    CHECK(r[1].request.insecure);
    CHECK_EQ(r[2].request.method, "DELETE");
    CHECK_EQ(r[2].request.body, "");
    CHECK_EQ(r[3].request.timeoutMs, (uint32_t)5000);
    CHECK_EQ(r[4].request.method, "HEAD");
    CHECK_EQ(r[5].request.method, "POST");
    CHECK_EQ(r[5].request.body, "{\"a\":1}");
    CHECK_EQ(r[5].request.timeoutMs, (uint32_t)3000);
    CHECK(hasHeader(r[5].request, "Accept", "text/plain"));
    CHECK(r[5].request.insecure);
    CHECK_EQ(r[6].request.method, "GET");
    CHECK_EQ(r[7].request.body, "gone");
    CHECK_EQ(r[8].request.body, "");
    CHECK_EQ(r[8].request.maxBytes, (size_t)(256 * 1024));
    CHECK_EQ(r[0].request.imageMaxWidth, 0);
    f.respond(4, 200, "");
    CHECK(f.services.logged("answer 200"));
    CHECK(!f.plugin.failed());

    const char* const bad[] = {
        "http.request({ method = 'GET' }, function() end)",                         // no url
        "http.request({ url = 'https://a.example', method = 'TRACE' }, print)",
        "http.request({ url = 'https://a.example', method = 5 }, print)",
        "http.request({ url = 'ftp://a.example' }, print)",
        "http.request({ url = 'https://a.example' })",                              // no callback
        "http.request('https://a.example', print)",
        "http.request({ url = 'https://a.example', method = 'PUT', body = 5 }, print)",
        "http.put('https://a.example', print)",                                     // no body
    };
    for (const char* source : bad) {
        Fixture broken;
        if (broken.start(source)) printf("  accepted: %s\n", source);
        CHECK(broken.plugin.failed());
        CHECK_EQ(broken.services.requests.size(), (size_t)0);
    }
}

TEST(http_answers_come_with_their_headers) {
    Fixture f;
    CHECK(f.start(R"(
        http.get("https://a.example", function(res)
          local names = {}
          for name in pairs(res.headers) do names[#names + 1] = name end
          table.sort(names)
          print(table.concat(names, " "))
          print(res.headers.etag, res.headers["x-ratelimit-remaining"], res.headers["set-cookie"], res.headers.ETag)
        end)
        http.get("https://b.example", function(res) print("none", type(res.headers), next(res.headers)) end)
        http.get("https://c.example", function(res)
          local total = 0
          for name, value in pairs(res.headers) do total = total + #name + #value end
          print("kept", total, res.headers.last)
        end)
    )"));
    HttpResponse response;
    response.status = 200;
    response.headers = {{"ETag", "\"abc\""}, {"X-RateLimit-Remaining", "59"}, {"Set-Cookie", "a=1"}, {"set-cookie", "b=2"},
                        {"", "nameless"}};
    f.plugin.httpResponse(f.services.requests[0].id, response);
    CHECK(f.services.logged("etag set-cookie x-ratelimit-remaining"));
    CHECK(f.services.logged("\"abc\" 59 a=1, b=2 nil"));

    f.respond(1, 0, "", "no WiFi");
    CHECK(f.services.logged("none table nil"));

    // Far too many headers: about 2 KB of them are kept.
    HttpResponse flood;
    flood.status = 200;
    for (int i = 0; i < 200; i++) flood.headers.emplace_back("X-H" + std::to_string(i), std::string(40, 'v'));
    flood.headers.emplace_back("Last", std::string(5000, 'z'));
    f.plugin.httpResponse(f.services.requests[2].id, flood);
    const std::string& line = f.services.logs.back();
    CHECK(contains(line, "kept "));
    const int kept = atoi(line.c_str() + 5);
    CHECK(kept > 1900 && kept <= 2048);
    CHECK(contains(line, " nil"));
    CHECK(!f.plugin.failed());
}

TEST(net_reports_the_connection) {
    Fixture offline;
    CHECK(offline.start("print(net.online(), net.rssi(), net.ssid(), net.ip())"));
    CHECK(offline.services.logged("false nil nil nil"));

    Fixture f;
    f.services.net.online = true;
    f.services.net.rssi = -61;
    f.services.net.ssid = "Hedgehog";
    f.services.net.ip = "192.168.1.20";
    CHECK(f.start("print(net.online(), net.rssi(), net.ssid(), net.ip())"));
    CHECK(f.services.logged("true -61 Hedgehog 192.168.1.20"));
}

TEST(device_battery_is_nil_without_one) {
    Fixture f;
    CHECK(f.start("print(device.battery())"));
    CHECK(f.services.logged("nil"));
}

TEST(device_battery_reports_the_reading) {
    Fixture f;
    f.services.hasBattery = true;
    f.services.batteryStatus.percent = 82;
    f.services.batteryStatus.volts = 3.98f;
    f.services.batteryStatus.charging = true;
    CHECK(f.start("local b = device.battery(); print(b.percent, string.format('%.2f', b.volts), b.charging)"));
    CHECK(f.services.logged("82 3.98 true"));
}

TEST(device_name_is_the_devices_name) {
    Fixture f;
    f.services.name = "kitchen";
    CHECK(f.start("print(device.name())"));
    CHECK(f.services.logged("kitchen"));
}

TEST(random_numbers_start_from_the_device) {
    const char* source = "print(math.random(1000000), math.random(1000000), math.random(1000000))";
    Fixture one, same, other;
    other.services.entropyCalls = 7;  // another device, or another boot
    CHECK(one.start(source));
    CHECK(same.start(source));
    CHECK(other.start(source));
    CHECK_EQ(one.services.logs.at(0), same.services.logs.at(0));
    CHECK(one.services.logs.at(0) != other.services.logs.at(0));
    CHECK(one.services.entropyCalls >= 2);
}

TEST(draws_rings_arcs_and_shapes) {
    Fixture f;
    CHECK(f.start(R"(
        function draw()
          gfx.circle(50, 50, 20, "red")              -- filled, as always
          gfx.circle(50, 50, 20, "red", 3)           -- a ring
          gfx.circle(50, 50, 20, nil, 99)            -- never thicker than the radius
          gfx.circle(50, 50, 20, "red", 0)           -- nothing
          gfx.arc(100, 60, 40, 0, 90, "#00ff00", 6)
          gfx.arc(100, 60, 40, 350, 10)              -- over the top, a filled slice
          gfx.arc(100, 60, 40, -90, 45.4, "blue", 2)
          gfx.arc(100, 60, 40, 720, 730, "blue", 2)
          gfx.arc(100, 60, 40, 0, 360, "blue", 2)
          gfx.arc(100, 60, 40, 10, 900, "blue", 2)   -- more than once round: the whole ring
          gfx.arc(100, 60, 40, 30, 30, "blue", 2)    -- nothing
          gfx.arc(100, 60, 0, 0, 90)                 -- nothing
          gfx.arc(100, 60, 40, 180, 90, "white", 1)  -- three quarters, the long way
          gfx.poly({ 10, 10, 60, 20.4, 30, 70.6, 5, 40 }, "yellow")
          gfx.triangle(1, 2, 30, 4, 15, 60, "#abc")
          gfx.triangle(1, 2, 30, 4, 15, 60)
          gfx.text(4, 4, "tiny text", "white", "tiny", "right")
        end
        print(gfx.textHeight("tiny"), gfx.textWidth("abc", "tiny"))
    )"));
    f.draw();
    CHECK(f.gfx.drew("rect 30,30 41x41 #e74c3c r21"));
    CHECK(f.gfx.drew("arc 50,50 r20 0-360 #e74c3c w3"));
    CHECK(f.gfx.drew("arc 50,50 r20 0-360 #ffffff w20"));
    CHECK(f.gfx.drew("arc 100,60 r40 0-90 #00ff00 w6"));
    CHECK(f.gfx.drew("arc 100,60 r40 350-370 #ffffff w40"));
    CHECK(f.gfx.drew("arc 100,60 r40 270-405 #3498db w2"));
    CHECK(f.gfx.drew("arc 100,60 r40 0-10 #3498db w2"));
    CHECK(f.gfx.drew("arc 100,60 r40 0-360 #3498db w2"));
    CHECK(f.gfx.drew("arc 100,60 r40 10-370 #3498db w2"));
    CHECK(f.gfx.drew("arc 100,60 r40 180-450 #ffffff w1"));
    int arcs = 0;
    for (const std::string& call : f.gfx.calls) arcs += call.compare(0, 4, "arc ") == 0;
    CHECK_EQ(arcs, 9);
    CHECK(f.gfx.drew("poly 10,10 60,20 30,71 5,40 #f1c40f"));
    CHECK(f.gfx.drew("poly 1,2 30,4 15,60 #aabbcc"));
    CHECK(f.gfx.drew("poly 1,2 30,4 15,60 #ffffff"));
    CHECK(f.gfx.drew("text 4,4 #ffffff tiny right tiny text"));
    CHECK(f.services.logged("12 15"));
    CHECK(!f.plugin.failed());

    const char* const bad[] = {
        "function draw() gfx.poly({ 1, 2, 3, 4 }) end",             // two points
        "function draw() gfx.poly({ 1, 2, 3, 4, 5, 6, 7 }) end",    // half a point
        "function draw() gfx.poly({ 1, 2, 3, 4, 5, 'x' }) end",
        "function draw() gfx.poly(5) end",
        "function draw() local p = {} for i = 1, 130 do p[i] = i end gfx.poly(p) end",
        "function draw() gfx.triangle(1, 2, 3, 4, 5) end",
        "function draw() gfx.arc(1, 2, 3, 0, 1 / 0) end",
        "function draw() gfx.arc(1, 2, 3) end",
        "function draw() gfx.text(1, 1, 'x', 'white', 'huge') end",
        "gfx.arc(1, 2, 3, 0, 90)",                                  // outside draw()
        "gfx.poly({ 1, 2, 3, 4, 5, 6 })",
    };
    for (const char* source : bad) {
        Fixture broken;
        broken.start(source);
        broken.draw();
        if (!broken.plugin.failed()) printf("  accepted: %s\n", source);
        CHECK(broken.plugin.failed());
    }
}

HttpResponse picture(int width, int height, uint32_t first = 0xFF112233, uint32_t last = 0xFF445566) {
    HttpResponse response;
    response.status = 200;
    response.imageWidth = width;
    response.imageHeight = height;
    response.image.assign((size_t)width * height, 0xFF000000);
    response.image.front() = first;
    response.image.back() = last;
    return response;
}

TEST(fetches_pictures_from_the_network) {
    Fixture f;
    CHECK(f.start(R"(
        local art
        gfx.fetchImage("https://img.example/cover.jpg", { width = 64, height = 48, insecure = true,
                                                          headers = { Accept = "image/*" }, timeout = 4 },
          function(image, err)
            art = image
            print("got", image and image.width, image and image.height, err)
          end)
        gfx.fetchImage("https://img.example/missing.png", function(image, err) print("second", image, err) end)
        gfx.fetchImage("https://img.example/text", { width = 9999, height = 0 }, function(image, err) print("third", image, err) end)
        gfx.fetchImage("https://img.example/down", function(image, err) print("fourth", image, err) end)
        function draw()
          if art then gfx.draw(art, 10, 20) end
        end
        local stale
        function button(name)
          if name == "center" then
            stale = art
            gfx.release(art)
            gfx.release(art)   -- twice is fine
            art = nil
          else
            art = stale
          end
          return true
        end
    )"));
    const auto& r = f.services.requests;
    CHECK_EQ(r.size(), (size_t)4);
    CHECK_EQ(r[0].request.method, "GET");
    CHECK_EQ(r[0].request.imageMaxWidth, 64);
    CHECK_EQ(r[0].request.imageMaxHeight, 48);
    CHECK(r[0].request.insecure);
    CHECK(hasHeader(r[0].request, "Accept", "image/*"));
    CHECK_EQ(r[0].request.timeoutMs, (uint32_t)4000);
    CHECK_EQ(r[0].request.maxBytes, (size_t)(256 * 1024));
    CHECK_EQ(r[1].request.imageMaxWidth, 240);  // no size asked for: whatever fits the largest picture
    CHECK_EQ(r[1].request.imageMaxHeight, 240);
    CHECK_EQ(r[2].request.imageMaxWidth, 240);
    CHECK_EQ(r[2].request.imageMaxHeight, 1);

    f.plugin.httpResponse(r[0].id, picture(64, 48));
    CHECK(f.services.logged("got 64 48 nil"));
    CHECK_EQ(f.plugin.imageBytesUsed(), (size_t)(64 * 48 * 4));
    CHECK(f.needsDraw());
    f.draw();
    CHECK(f.gfx.drew("image 10,20 64x48 x1 ff112233..ff445566"));

    f.respond(1, 404, "not here");
    CHECK(f.services.logged("second nil HTTP 404"));
    HttpResponse notAPicture;
    notAPicture.status = 200;
    notAPicture.error = "not a PNG or JPEG";
    f.plugin.httpResponse(r[2].id, notAPicture);
    CHECK(f.services.logged("third nil not a PNG or JPEG"));
    f.respond(3, 0, "", "no WiFi");
    CHECK(f.services.logged("fourth nil no WiFi"));
    CHECK_EQ(f.plugin.imageBytesUsed(), (size_t)(64 * 48 * 4));

    // Releasing gives the memory back; drawing it afterwards is the plugin's mistake.
    CHECK_EQ(f.button(Button::Center), true);
    CHECK_EQ(f.plugin.imageBytesUsed(), (size_t)0);
    f.draw();
    CHECK(!f.plugin.failed());
    f.button(Button::Up);
    f.draw();
    CHECK(f.plugin.failed());
    CHECK(contains(f.plugin.error(), "released"));
}

TEST(pictures_count_against_the_image_budget) {
    LuaPlugin::Limits limits;
    limits.imageBytes = 100 * 100 * 4 + 16;
    Fixture f(limits);
    CHECK(f.start(R"(
        local held = {}
        function fetch()
          gfx.fetchImage("https://img.example/a.png", function(image, err)
            held[#held + 1] = image
            print(image and "kept" or err)
          end)
        end
        fetch() fetch()
        function button(name)
          if name == "up" then gfx.release(held[1]) fetch() end
          if name == "down" then gfx.image({ "ab", colors = { a = "red", b = "blue" } }) end
          return true
        end
    )"));
    HttpResponse first = picture(100, 100);
    f.plugin.httpResponse(f.services.requests[0].id, std::move(first));
    CHECK(first.image.empty());  // handed over, not copied
    CHECK(f.services.logged("kept"));
    f.plugin.httpResponse(f.services.requests[1].id, picture(100, 100));
    CHECK(f.services.logged("no room for this picture"));
    CHECK(!f.plugin.failed());  // the plugin hears about it; it is not a crash
    CHECK_EQ(f.plugin.imageBytesUsed(), (size_t)(100 * 100 * 4));

    f.button(Button::Down);  // gfx.image shares the budget: 8 bytes still fit
    CHECK(!f.plugin.failed());
    f.button(Button::Up);    // let go of the first, fetch again
    f.services.logs.clear();
    f.plugin.httpResponse(f.services.requests[2].id, picture(100, 100));
    CHECK(f.services.logged("kept"));

    // A host that answers with nonsense cannot get it past the runtime.
    Fixture g;
    CHECK(g.start("for i = 1, 3 do gfx.fetchImage('https://img.example', function(image, err) print(image, err) end) end"));
    HttpResponse tooLarge = picture(241, 10);
    g.plugin.httpResponse(g.services.requests[0].id, tooLarge);
    HttpResponse lying = picture(10, 10);
    lying.imageWidth = 20;
    g.plugin.httpResponse(g.services.requests[1].id, lying);
    HttpResponse negative = picture(10, 10);
    negative.imageHeight = -10;
    g.plugin.httpResponse(g.services.requests[2].id, negative);
    CHECK_EQ(g.services.logs.size(), (size_t)3);
    for (const std::string& line : g.services.logs) CHECK(contains(line, "nil not a PNG or JPEG picture"));
    CHECK_EQ(g.plugin.imageBytesUsed(), (size_t)0);

    // Image requests share the limit on requests in flight, and a crash in the callback is contained.
    Fixture h;
    CHECK(!h.start("for i = 1, 13 do gfx.fetchImage('https://img.example', print) end"));
    CHECK(contains(h.plugin.error(), "too many requests"));
    Fixture crash;
    CHECK(crash.start("gfx.fetchImage('https://img.example', function(image) error('boom') end)"));
    crash.plugin.httpResponse(crash.services.requests[0].id, picture(4, 4));
    CHECK(crash.plugin.failed());
    CHECK(contains(crash.plugin.error(), "boom"));
    CHECK_EQ(crash.plugin.imageBytesUsed(), (size_t)0);
}

TEST(old_plugins_only_hear_about_presses) {
    Fixture f;
    CHECK(f.start(R"(
        local presses = 0
        function button(name)
          presses = presses + 1
          print(name, presses)
          return name == "center"
        end
    )"));
    f.services.held[(int)Button::Center] = true;
    CHECK_EQ(f.button(Button::Center), true);
    CHECK_EQ(f.button(Button::Down), false);
    for (int i = 0; i < 10; i++) {
        f.services.now += 100;
        f.tick(f.services.now, true);
    }
    f.services.held[(int)Button::Center] = false;
    f.services.now += 100;
    f.tick(f.services.now, true);
    CHECK_EQ(f.services.logs.size(), (size_t)2);  // no "long", no "release": exactly what it saw before
    CHECK(f.services.logged("down 2"));
}

TEST(buttons_report_long_and_release) {
    Fixture f;
    CHECK(f.start(R"(
        function button(name, event)
          print(name, event)
          return event ~= "press" or name == "center"
        end
    )"));
    // A tap: down and up again within one tick.
    CHECK_EQ(f.button(Button::Center), true);
    CHECK(f.services.logged("center press"));
    f.services.now += 33;
    f.tick(f.services.now, true);
    CHECK_EQ(f.services.logs.back(), "center release");
    CHECK_EQ(f.services.logs.size(), (size_t)2);

    // A hold: "long" once, after 600 ms, then "release".
    f.services.logs.clear();
    f.services.held[(int)Button::Center] = true;
    CHECK_EQ(f.button(Button::Center), true);
    for (int i = 0; i < 5; i++) {
        f.services.now += 100;
        f.tick(f.services.now, true);
    }
    CHECK_EQ(f.services.logs.size(), (size_t)1);  // 500 ms: not long yet
    f.services.now += 100;
    f.tick(f.services.now, true);
    CHECK_EQ(f.services.logs.back(), "center long");
    CHECK(f.needsDraw());
    for (int i = 0; i < 20; i++) {
        f.services.now += 100;
        f.tick(f.services.now, true);
    }
    CHECK_EQ(f.services.logs.size(), (size_t)2);  // once per hold
    f.services.held[(int)Button::Center] = false;
    f.services.now += 100;
    f.tick(f.services.now, true);
    CHECK_EQ(f.services.logs.back(), "center release");
    f.services.now += 100;
    f.tick(f.services.now, true);
    CHECK_EQ(f.services.logs.size(), (size_t)3);

    // Navigation goes by the press alone: what "long" and "release" return changes nothing,
    // and a press the plugin did not use still gets its release.
    f.services.logs.clear();
    CHECK_EQ(f.button(Button::Down), false);
    f.services.now += 33;
    f.tick(f.services.now, false);  // the stack moved on: the card is hidden
    CHECK_EQ(f.services.logs.back(), "down release");

    // A hold on a card that left the screen is not "long", but is released.
    f.services.logs.clear();
    f.services.held[(int)Button::Up] = true;
    f.button(Button::Up);
    for (int i = 0; i < 10; i++) {
        f.services.now += 100;
        f.tick(f.services.now, false);
    }
    CHECK_EQ(f.services.logs.size(), (size_t)1);
    f.services.held[(int)Button::Up] = false;
    f.services.now += 100;
    f.tick(f.services.now, false);
    CHECK_EQ(f.services.logs.back(), "up release");
    CHECK(!f.plugin.failed());

    // A crash in an event is contained like any other.
    Fixture crash;
    CHECK(crash.start("function button(name, event) if event == 'release' then error('let go') end end"));
    crash.button(Button::Center);
    CHECK(!crash.plugin.failed());
    crash.services.now += 33;
    crash.tick(crash.services.now, true);
    CHECK(crash.plugin.failed());
    CHECK(contains(crash.plugin.error(), "let go"));
    crash.services.now += 33;
    crash.tick(crash.services.now, true);  // and nothing more happens
}

TEST(cards_of_a_kind_get_button_events_too) {
    Fixture f;
    CHECK(f.start(R"(
        cards = {
          new = { button = function(card, name, event) print(card.id, name, event) return true end },
          old = { button = function(card, name) print(card.id, name) return true end },
        }
    )"));
    f.plugin.addCard("2", "old", {}, f.gfx);
    f.services.held[(int)Button::Center] = true;
    CHECK_EQ(f.plugin.button("1", Button::Center), true);
    CHECK_EQ(f.plugin.button("2", Button::Center), true);
    f.plugin.setVisible("1", true);
    f.services.now += 700;
    f.plugin.tick(f.services.now);
    f.services.held[(int)Button::Center] = false;
    f.services.now += 33;
    f.plugin.tick(f.services.now);
    CHECK_EQ(f.services.logs.size(), (size_t)4);
    CHECK_EQ(f.services.logs[0], "1 center press");
    CHECK_EQ(f.services.logs[1], "2 center");
    CHECK_EQ(f.services.logs[2], "1 center long");
    CHECK_EQ(f.services.logs[3], "1 center release");
}

TEST(timers_run_once_or_again_and_again) {
    Fixture f;
    CHECK(f.start(R"(
        local ticks = 0
        once = timer.after(0.5, function() print("once") end)
        again = timer.every(0.2, function()
          ticks = ticks + 1
          print("tick", ticks)
          if ticks == 3 then again:cancel() end
        end)
        quiet = timer.every(1, function() return false end)   -- nothing it draws changed
        gone = timer.after(0.1, function() print("never") end)
        gone:cancel()
        gone:cancel()                                           -- twice is fine
        timer.cancel(gone)
        print(type(once), once.cancel == timer.cancel, getmetatable(once))
    )"));
    CHECK(f.services.logged("table true false"));
    CHECK_EQ(f.plugin.timerCount(), (size_t)3);
    f.draw();
    CHECK(!f.needsDraw());
    pass(f, 0.1f);
    CHECK_EQ(f.services.logs.size(), (size_t)1);
    pass(f, 0.1f);
    CHECK_EQ(f.services.logs.back(), "tick 1");
    CHECK(f.needsDraw());  // a timer's callback repaints the card
    f.draw();
    pass(f, 0.3f);
    CHECK(f.services.logged("once"));
    CHECK(f.services.logged("tick 2"));
    CHECK_EQ(f.plugin.timerCount(), (size_t)2);  // the one-shot is gone
    pass(f, 0.2f);
    CHECK(f.services.logged("tick 3"));
    CHECK_EQ(f.plugin.timerCount(), (size_t)1);
    f.draw();
    f.services.logs.clear();
    pass(f, 3);
    CHECK_EQ(f.services.logs.size(), (size_t)0);
    CHECK(!f.services.logged("never"));
    CHECK(!f.needsDraw());  // the quiet one returned false every time
    CHECK(!f.plugin.failed());
}

TEST(timers_keep_running_while_hidden_but_slower) {
    Fixture f;
    CHECK(f.start(R"(
        count = 0
        timer.every(0.1, function() count = count + 1 end)
        timer.every(0, function() end)                 -- never faster than ten times a second
        timer.after(2.5, function() print("after", count) end)
        function button() print("count", count) count = 0 return true end
    )"));
    pass(f, 2, true);
    f.button(Button::Center);
    CHECK(f.services.logged("count 20"));
    pass(f, 5, false);  // hidden: once a second
    CHECK(f.services.logged("after 1"));  // the one-shot came on time, half a second in
    f.button(Button::Center);
    CHECK(f.services.logged("count 5"));
    pass(f, 1, true);   // and back to full speed
    f.button(Button::Center);
    const int count = atoi(f.services.logs.back().c_str() + 6);
    CHECK(count >= 9 && count <= 10);

    // After a stall the missed rounds are not made up for.
    f.services.now += 60000;
    f.tick(f.services.now, true);
    f.services.now += 33;
    f.tick(f.services.now, true);
    f.button(Button::Center);
    CHECK(f.services.logged("count 1"));

    // The millisecond clock wrapping round does not stop them.
    Fixture wrap;
    wrap.services.now = 0xFFFFFF00u;
    CHECK(wrap.start("timer.every(1, function() print('still here') end)"));
    pass(wrap, 3);
    CHECK(wrap.services.now < 0x80000000u);
    CHECK_EQ(wrap.services.logs.size(), (size_t)3);
}

TEST(timers_have_limits) {
    Fixture f;
    CHECK(f.start(R"(
        handles = {}
        for i = 1, 16 do handles[i] = timer.every(60, function() end) end
        print(pcall(timer.after, 1, function() end))
        handles[1]:cancel()
        print(pcall(timer.after, 1, function() end))
    )"));
    CHECK(f.services.logged("too many timers (max 16)"));
    CHECK(contains(f.services.logs.back(), "true "));  // room again after a cancel
    CHECK_EQ(f.plugin.timerCount(), (size_t)16);

    const char* const bad[] = {"timer.after(1)", "timer.every('soon', print)", "timer.after(1, 'x')", "timer.cancel(5)"};
    for (const char* source : bad) {
        Fixture broken;
        CHECK(!broken.start(source));
    }
    Fixture forgiving;
    CHECK(forgiving.start("timer.after(-5, print) timer.after(0/0, print) timer.after(1e12, print) timer.cancel({})"));
    CHECK_EQ(forgiving.plugin.timerCount(), (size_t)3);

    // A timer that crashes or never returns takes the plugin down, not the device.
    Fixture crash;
    CHECK(crash.start("timer.after(0.1, function() error('timer broke') end) function draw() ui.big('fine') end"));
    pass(crash, 0.3f);
    CHECK(crash.plugin.failed());
    CHECK(contains(crash.plugin.error(), "timer broke"));
    crash.draw();
    CHECK(crash.gfx.drew("crashed"));

    Fixture spin;
    spin.services.advancePerCall = 1;
    CHECK(spin.start("timer.after(0.1, function() while true do end end)"));
    pass(spin, 0.3f);
    CHECK(spin.plugin.failed());
    CHECK(contains(spin.plugin.error(), "took too long"));

    // Timers end with the program that made them.
    Fixture restart;
    CHECK(restart.start("timer.every(0.1, function() print('old') end)"));
    CHECK(restart.start("x = 1"));
    CHECK_EQ(restart.plugin.timerCount(), (size_t)0);
    pass(restart, 1);
    CHECK(!restart.services.logged("old"));

    // A callback can start and cancel timers, its own included.
    Fixture chain;
    CHECK(chain.start(R"(
        local n = 0
        local function step()
          n = n + 1
          print("step", n)
          if n < 3 then timer.after(0.1, step) end
        end
        timer.after(0.1, step)
        local self
        self = timer.every(0.1, function() self:cancel() print("self") end)
    )"));
    pass(chain, 1);
    CHECK(chain.services.logged("step 3"));
    CHECK(!chain.services.logged("step 4"));
    CHECK_EQ(chain.plugin.timerCount(), (size_t)0);
    int selfs = 0;
    for (const std::string& line : chain.services.logs) selfs += line == "self";
    CHECK_EQ(selfs, 1);
    CHECK(!chain.plugin.failed());
}

TEST(timers_reach_cards_through_closures) {
    Fixture f;
    CHECK(f.start(R"(
        cards = { beat = {
          load = function(card)
            card.beats = 0
            if not card.timer then card.timer = timer.every(0.5, function() card.beats = card.beats + 1 end) end
          end,
          draw = function(card) ui.big(tostring(card.beats)) end,
        } }
    )"));
    f.plugin.addCard("2", "beat", {}, f.gfx);
    pass(f, 1.1f, false);  // neither card is on screen: timers still run, once a second
    f.draw();
    CHECK(f.gfx.drew("large center 1"));
    CHECK(f.plugin.needsDraw("2"));
    CHECK_EQ(f.plugin.timerCount(), (size_t)2);
}

// ------------------------------------------------------------------- notify

TEST(notify_asks_the_host_for_attention) {
    Fixture f;
    CHECK(f.start(R"(
        notify("Tea is ready")
        notify("Build failed", { led = "red" })
        notify{ led = "#00ff00" }
        notify()
        notify(42)
        notify(string.rep("é", 300))
    )"));
    CHECK_EQ(f.services.notices.size(), (size_t)6);
    CHECK_EQ(f.services.notices[0].card, "");  // a single-card plugin: its only card
    CHECK_EQ(f.services.notices[0].notice.text, "Tea is ready");
    CHECK_EQ(f.services.notices[0].notice.hasLed, false);
    CHECK_EQ(f.services.notices[1].notice.text, "Build failed");
    CHECK_EQ(f.services.notices[1].notice.hasLed, true);
    CHECK_EQ((int)f.services.notices[1].notice.ledRgb, 0xE74C3C);
    CHECK_EQ(f.services.notices[2].notice.text, "");
    CHECK_EQ((int)f.services.notices[2].notice.ledRgb, 0x00FF00);
    CHECK_EQ(f.services.notices[3].notice.text, "");
    CHECK_EQ(f.services.notices[3].notice.hasLed, false);
    CHECK_EQ(f.services.notices[4].notice.text, "42");
    CHECK_EQ(f.services.notices[5].notice.text.size(), (size_t)200);  // cut between characters, not inside one

    const char* const bad[] = {"notify('x', 'red')", "notify('x', { led = 'nocolor' })", "notify(print)"};
    for (const char* source : bad) {
        Fixture wrong;
        CHECK(!wrong.start(source));
        CHECK(wrong.services.notices.empty());
    }
}

TEST(notify_of_a_card_names_the_card) {
    Fixture f;
    CHECK(f.start(R"(
        cards = { alarm = {
          load = function(card) card.notify("from " .. card.id, { led = "blue" }) end,
          button = function(card, name) card:notify("pressed " .. name) return true end,
        } }
    )"));
    f.plugin.addCard("7", "alarm", {}, f.gfx);
    CHECK_EQ(f.services.notices.size(), (size_t)2);
    CHECK_EQ(f.services.notices[0].card, "1");
    CHECK_EQ(f.services.notices[0].notice.text, "from 1");
    CHECK_EQ(f.services.notices[1].card, "7");
    CHECK(f.plugin.button("7", Button::Center));
    CHECK_EQ(f.services.notices[2].card, "7");
    CHECK_EQ(f.services.notices[2].notice.text, "pressed center");  // card:notify works like card.notify

    // The plain notify() cannot know which card is meant.
    Fixture wrong;
    CHECK(!wrong.start("cards = { a = { load = function(card) notify('hi') end } }"));
    CHECK(contains(wrong.plugin.error(), "card.notify"));
}

// ---------------------------------------------------------------- attention

struct AttentionRig {
    Attention attention;
    uint32_t now = 100000;
    std::string onScreen = "game:1";
    std::vector<std::string> blocked;  ///< Cards whose "May come to the front" is off
    std::vector<std::string> shows;
    int flashes = 0;
    uint32_t lastLed = 0;

    bool allowed(const std::string& key) const {
        for (const std::string& card : blocked) {
            if (card == key) return false;
        }
        return true;
    }
    void ask(const std::string& key, const std::string& text, bool led = false, uint32_t rgb = 0) {
        Notice notice;
        notice.text = text;
        notice.hasLed = led;
        notice.ledRgb = rgb;
        attention.request(key, notice, allowed(key), now);
    }
    /** Run for a while. A card that is asked to the front gets there, like on the device. */
    void run(uint32_t ms) {
        for (uint32_t passed = 0; passed < ms; passed += 50) {
            now += 50;
            const Attention::Actions actions =
                attention.tick(onScreen, now, [this](const std::string& key) { return allowed(key); });
            if (!actions.show.empty()) {
                shows.push_back(actions.show);
                onScreen = actions.show;
            }
            if (actions.led) {
                flashes++;
                lastLed = actions.ledRgb;
            }
        }
    }
    std::string banner(const std::string& key) const {
        const std::string* text = attention.banner(key);
        return text ? *text : "(none)";
    }
};

TEST(attention_brings_a_card_to_the_front_with_a_banner) {
    AttentionRig rig;
    rig.ask("timer:1", "Tea is ready");
    CHECK_EQ(rig.attention.pending().size(), (size_t)1);
    rig.run(200);
    CHECK_EQ(rig.shows.size(), (size_t)1);
    CHECK_EQ(rig.shows[0], "timer:1");
    CHECK_EQ(rig.banner("timer:1"), "Tea is ready");
    CHECK_EQ(rig.banner("game:1"), "(none)");
    CHECK(rig.attention.pending().empty());
    CHECK_EQ(rig.flashes, 0);  // it came to the front, and gave no colour: the LED stays as it is
    rig.run(3500);
    CHECK_EQ(rig.banner("timer:1"), "Tea is ready");
    rig.run(600);
    CHECK_EQ(rig.banner("timer:1"), "(none)");  // about four seconds
    CHECK_EQ(rig.shows.size(), (size_t)1);

    // With a colour the LED flashes three times, even when the card comes up at once.
    AttentionRig led;
    led.ask("timer:1", "Done", true, 0x00FF00);
    led.run(3000);
    CHECK_EQ(led.flashes, 3);
    CHECK_EQ((int)led.lastLed, 0x00FF00);

    // A card that is on screen already just gets its banner.
    AttentionRig here;
    here.run(100);
    here.ask("game:1", "New high score");
    here.run(100);
    CHECK(here.shows.empty());
    CHECK_EQ(here.banner("game:1"), "New high score");
    CHECK_EQ(here.flashes, 0);

    // No text: the card comes to the front, and that is all.
    AttentionRig quiet;
    quiet.ask("timer:1", "");
    quiet.run(200);
    CHECK_EQ(quiet.shows.size(), (size_t)1);
    CHECK_EQ(quiet.banner("timer:1"), "(none)");
}

TEST(attention_waits_while_a_card_is_being_played) {
    AttentionRig rig;
    rig.attention.busy(rig.now);  // the game on screen just used a press
    rig.ask("timer:1", "Tea is ready");
    rig.run(3000);
    CHECK(rig.shows.empty());
    CHECK_EQ(rig.flashes, 3);  // it has to wait: the LED says something is up
    CHECK_EQ((int)rig.lastLed, 0xFFFFFF);
    rig.attention.busy(rig.now);  // still playing: the five seconds start again
    rig.run(4500);
    CHECK(rig.shows.empty());
    CHECK_EQ(rig.attention.pending().size(), (size_t)1);
    rig.run(700);
    CHECK_EQ(rig.shows.size(), (size_t)1);  // idle for five seconds
    CHECK_EQ(rig.banner("timer:1"), "Tea is ready");

    // A press that the card uses takes its banner away.
    rig.attention.busy(rig.now);
    rig.run(50);
    CHECK_EQ(rig.banner("timer:1"), "(none)");
}

TEST(attention_respects_the_card_setting) {
    AttentionRig rig;
    rig.blocked.push_back("timer:1");
    rig.ask("timer:1", "Tea is ready");
    rig.run(60000);
    CHECK(rig.shows.empty());  // it never comes to the front by itself
    CHECK_EQ(rig.flashes, 3);
    CHECK_EQ(rig.attention.pending().size(), (size_t)1);
    CHECK_EQ(rig.attention.pending()[0].text, "Tea is ready");
    CHECK_EQ(rig.banner("timer:1"), "(none)");

    // More requests collapse into the one that waits. The latest text wins.
    rig.ask("timer:1", "Tea is cold");
    rig.ask("timer:1", "");
    CHECK_EQ(rig.attention.pending().size(), (size_t)1);
    CHECK_EQ(rig.attention.pending()[0].text, "Tea is cold");
    CHECK_EQ(rig.attention.pending()[0].count, 3);

    // Someone walks to the card: now it shows what it had to say.
    rig.onScreen = "timer:1";
    rig.run(100);
    CHECK_EQ(rig.banner("timer:1"), "Tea is cold");
    CHECK(rig.attention.pending().empty());
    CHECK(rig.shows.empty());

    // Walking away ends the banner.
    rig.onScreen = "game:1";
    rig.run(100);
    CHECK_EQ(rig.banner("timer:1"), "(none)");
}

TEST(attention_is_rate_limited) {
    AttentionRig rig;
    rig.ask("timer:1", "one");
    rig.run(200);
    CHECK_EQ(rig.shows.size(), (size_t)1);
    rig.onScreen = "game:1";  // someone goes back to what they were doing
    rig.run(1000);

    // The card asks again and again: nothing happens for 30 seconds.
    for (int i = 0; i < 20; i++) {
        rig.ask("timer:1", "again " + std::to_string(i));
        rig.run(1000);
    }
    CHECK_EQ(rig.shows.size(), (size_t)1);
    CHECK_EQ(rig.attention.pending().size(), (size_t)1);
    CHECK_EQ(rig.attention.pending()[0].count, 20);
    rig.run(10000);
    CHECK_EQ(rig.shows.size(), (size_t)2);  // once, with the latest text
    CHECK_EQ(rig.banner("timer:1"), "again 19");

    // Two cards at once take turns, a few seconds apart.
    AttentionRig two;
    two.ask("a:1", "from a");
    two.ask("b:1", "from b");
    two.run(1000);
    CHECK_EQ(two.shows.size(), (size_t)1);
    CHECK_EQ(two.banner("a:1"), "from a");
    two.run(5000);
    CHECK_EQ(two.shows.size(), (size_t)2);
    CHECK_EQ(two.banner("b:1"), "from b");

    // A card that went away takes its requests with it.
    AttentionRig gone;
    gone.blocked.push_back("a:1");
    gone.ask("a:1", "x");
    gone.ask("ab:1", "y");
    gone.attention.forget("a:");
    CHECK_EQ(gone.attention.pending().size(), (size_t)1);
    CHECK_EQ(gone.attention.pending()[0].key, "ab:1");

    // Long text is cut, and the list of waiting cards cannot grow without end.
    AttentionRig many;
    for (int i = 0; i < 40; i++) {
        many.blocked.push_back("c:" + std::to_string(i));
        many.ask("c:" + std::to_string(i), std::string(500, 'x'));
    }
    CHECK_EQ(many.attention.pending().size(), Attention::kMaxPending);
    CHECK_EQ(many.attention.pending()[0].text.size(), Attention::kMaxText);
}

TEST(banner_is_drawn_over_the_card) {
    RecordingGfx gfx;
    drawBanner(gfx, "Tea is ready");
    CHECK(gfx.drew("clip off"));
    CHECK(gfx.drew("rect 4,4 225x30"));
    CHECK(gfx.drew("small left Tea is ready"));

    // Long text wraps to two lines and ends in dots.
    RecordingGfx tall;
    drawBanner(tall, "The quick brown fox jumps over the lazy dog and then keeps running for a very long time");
    CHECK(tall.drew("rect 4,4 225x48"));
    CHECK(tall.drew("small left The quick brown fox jumps"));
    CHECK(tall.drew("..."));
    int lines = 0;
    for (const std::string& call : tall.calls) lines += contains(call, "text ");
    CHECK_EQ(lines, 2);

    RecordingGfx none;
    drawBanner(none, "");
    CHECK(none.calls.empty());

    CHECK_EQ(clipUtf8("héllo", 2), "h");  // never half a character
    CHECK_EQ(clipUtf8("héllo", 3), "hé");
    CHECK_EQ(clipUtf8("hi", 10), "hi");
}

// ------------------------------------------------------------- desk rules

namespace {

typedef std::vector<std::pair<std::string, std::string>> Changes;

/** The moment a UTC clock shows this date and time. */
int64_t utcAt(int year, int month, int day, int hour, int minute) {
    return TimeZone().fromCivil(year, month, day, hour, minute, 0);
}

/** Night hours 22:00-07:00 on, in UTC. */
DeskRules nightRules() {
    DeviceSettings settings;
    settings.nightEnabled = true;
    DeskRules rules;
    rules.setSettings(settings);
    return rules;
}

}  // namespace

TEST(device_settings_defaults_change_nothing) {
    DeskRules rules;
    for (int hour : {0, 3, 12, 23}) {
        const DeskRules::Levels levels = rules.tick(utcAt(2026, 3, 1, hour, 0), "UTC0", 1000);
        CHECK_EQ(levels.screen, 80);
        CHECK_EQ(levels.led, 100);
        CHECK(!levels.night);
        CHECK(!levels.manual);
    }
}

TEST(device_settings_are_all_or_nothing) {
    DeviceSettings settings;
    std::string error;
    CHECK(!applyDeviceSettings(settings, Changes{{"screen.day", "60"}, {"led.night", "300"}}, error));
    CHECK(contains(error, "led.night"));
    CHECK_EQ(settings.screenDay, 80);

    CHECK(!applyDeviceSettings(settings, Changes{{"screen.day", "4"}}, error));
    CHECK(applyDeviceSettings(settings, Changes{{"screen.day", "5"}}, error));
    CHECK_EQ(settings.screenDay, 5);

    CHECK(!applyDeviceSettings(settings, Changes{{"rotate.seconds", "5"}}, error));
    for (const char* ok : {"0", "10", "600"}) CHECK(applyDeviceSettings(settings, Changes{{"rotate.seconds", ok}}, error));
    CHECK_EQ(settings.rotateSeconds, 600);
    CHECK(!applyDeviceSettings(settings, Changes{{"rotate.seconds", "601"}}, error));
    CHECK(!applyDeviceSettings(settings, Changes{{"bogus", "1"}}, error));
    CHECK(contains(error, "bogus"));

    CHECK(applyDeviceSettings(settings, Changes{{"name", "kitchen"}, {"night.enabled", "yes"}, {"night.from", "21:30"},
                                                {"night.until", "360"}, {"battery", "1"}, {"led.day", "0"}}, error));
    CHECK_EQ(settings.name, "kitchen");
    CHECK(settings.nightEnabled);
    CHECK_EQ(settings.nightFrom, 1290);
    CHECK_EQ(settings.nightUntil, 360);
    CHECK(settings.battery);
    CHECK_EQ(settings.ledDay, 0);
    CHECK(!applyDeviceSettings(settings, Changes{{"night.enabled", "maybe"}}, error));
    CHECK(!applyDeviceSettings(settings, Changes{{"name", "Desk"}}, error));
}

TEST(device_names) {
    for (const char* ok : {"kitchen", "desk-2", "a"}) CHECK(isValidDeviceName(ok));
    for (const char* bad : {"", "-a", "a-", "Desk", "a_b"}) CHECK(!isValidDeviceName(bad));
    CHECK(isValidDeviceName(std::string(24, 'a')));
    CHECK(!isValidDeviceName(std::string(25, 'a')));
}

TEST(clock_text) {
    int minutes = 0;
    CHECK(parseClock("22:30", minutes));
    CHECK_EQ(minutes, 1350);
    CHECK(parseClock("7:05", minutes));
    CHECK_EQ(minutes, 425);
    CHECK(parseClock("1350", minutes));
    CHECK_EQ(minutes, 1350);
    CHECK(!parseClock("24:00", minutes));
    CHECK(!parseClock("12:60", minutes));
    CHECK(!parseClock("x", minutes));
    CHECK(!parseClock("", minutes));
    CHECK(!parseClock("1440", minutes));
    CHECK_EQ(formatClock(425), "07:05");
}

TEST(stored_settings_are_sanitized) {
    DeviceSettings stored;
    stored.screenDay = 0;
    stored.ledNight = 999;
    stored.name = "Bad Name";
    stored.nightFrom = 5000;
    stored.rotateSeconds = 3;
    stored.screenNight = 25;
    const DeviceSettings fixed = sanitized(stored);
    CHECK_EQ(fixed.screenDay, 80);
    CHECK_EQ(fixed.ledNight, 0);
    CHECK_EQ(fixed.name, "deskhog");
    CHECK_EQ(fixed.nightFrom, 1320);
    CHECK_EQ(fixed.rotateSeconds, 0);
    CHECK_EQ(fixed.screenNight, 25);  // in range: kept
}

TEST(night_crosses_midnight) {
    DeskRules rules = nightRules();
    CHECK(!rules.tick(utcAt(2026, 3, 1, 21, 59), "UTC0", 0).night);
    CHECK(rules.tick(utcAt(2026, 3, 1, 22, 0), "UTC0", 0).night);
    const DeskRules::Levels deep = rules.tick(utcAt(2026, 3, 2, 3, 0), "UTC0", 0);
    CHECK(deep.night);
    CHECK_EQ(deep.screen, 10);
    CHECK_EQ(deep.led, 0);
    CHECK(rules.tick(utcAt(2026, 3, 2, 6, 59), "UTC0", 0).night);
    CHECK(!rules.tick(utcAt(2026, 3, 2, 7, 0), "UTC0", 0).night);

    DeviceSettings same;
    same.nightEnabled = true;
    same.nightFrom = same.nightUntil = 600;
    DeskRules never;
    never.setSettings(same);
    CHECK(!never.tick(utcAt(2026, 3, 1, 10, 0), "UTC0", 0).night);
    CHECK(!never.tick(utcAt(2026, 3, 1, 3, 0), "UTC0", 0).night);
}

TEST(night_follows_the_time_zone_and_summer_time) {
    DeskRules rules = nightRules();
    const char* cet = "CET-1CEST,M3.5.0,M10.5.0/3";
    CHECK(rules.tick(utcAt(2026, 7, 1, 20, 30), cet, 0).night);   // 22:30 local
    CHECK(!rules.tick(utcAt(2026, 1, 1, 20, 30), cet, 0).night);  // 21:30 local
    CHECK(!rules.tick(utcAt(2026, 7, 1, 20, 30), "", 0).night);   // empty is UTC: 20:30
}

TEST(no_night_until_the_clock_is_set) {
    DeskRules rules = nightRules();
    const DeskRules::Levels levels = rules.tick(0, "UTC0", 0);
    CHECK(!levels.night);
    CHECK_EQ(levels.screen, 80);
    CHECK_EQ(levels.led, 100);
}

TEST(a_press_at_night_wakes_the_screen) {
    DeskRules rules = nightRules();
    const int64_t late = utcAt(2026, 3, 1, 23, 0);
    rules.tick(late, "UTC0", 1000);
    CHECK(!rules.press(1000));
    DeskRules::Levels levels = rules.tick(late, "UTC0", 1000 + 29900);
    CHECK_EQ(levels.screen, 80);
    CHECK_EQ(levels.led, 0);
    CHECK(levels.night);
    levels = rules.tick(late, "UTC0", 1000 + 30000);
    CHECK_EQ(levels.screen, 10);
    CHECK_EQ(levels.led, 0);

    DeviceSettings dark;
    dark.nightEnabled = true;
    dark.screenNight = 0;
    DeskRules black;
    black.setSettings(dark);
    black.tick(late, "UTC0", 5000);
    CHECK(black.press(5000));
    CHECK_EQ(black.tick(late, "UTC0", 5100).screen, 80);
    CHECK(!black.press(6000));
    CHECK_EQ(black.tick(late, "UTC0", 6000 + DeskRules::kWakeMs).screen, 0);
}

TEST(press_by_day_is_just_a_press) {
    DeskRules rules = nightRules();
    const int64_t noon = utcAt(2026, 3, 1, 12, 0);
    rules.tick(noon, "UTC0", 0);
    CHECK(!rules.press(0));
    const DeskRules::Levels levels = rules.tick(noon, "UTC0", 10);
    CHECK_EQ(levels.screen, 80);
    CHECK_EQ(levels.led, 100);
    CHECK(!levels.night);
}

TEST(a_wake_raises_only_the_screen) {
    DeskRules rules = nightRules();
    const int64_t late = utcAt(2026, 3, 1, 23, 0);
    rules.tick(late, "UTC0", 0);
    rules.wake(0, 5000);
    DeskRules::Levels levels = rules.tick(late, "UTC0", 4999);
    CHECK_EQ(levels.screen, 80);
    CHECK_EQ(levels.led, 0);
    CHECK_EQ(rules.tick(late, "UTC0", 5000).screen, 10);
    // A shorter wake does not cut a longer one short.
    rules.wake(10000, 20000);
    rules.wake(11000, 1000);
    CHECK_EQ(rules.tick(late, "UTC0", 29000).screen, 80);
}

TEST(night_by_hand_holds_until_the_schedule_changes) {
    DeskRules rules = nightRules();
    rules.tick(utcAt(2026, 3, 1, 20, 0), "UTC0", 0);
    rules.toggleNight();
    DeskRules::Levels levels = rules.tick(utcAt(2026, 3, 1, 20, 0), "UTC0", 10);
    CHECK(levels.night);
    CHECK(levels.manual);
    CHECK_EQ(levels.screen, 10);
    levels = rules.tick(utcAt(2026, 3, 1, 21, 59), "UTC0", 20);
    CHECK(levels.night);
    CHECK(levels.manual);
    levels = rules.tick(utcAt(2026, 3, 1, 22, 0), "UTC0", 30);
    CHECK(levels.night);
    CHECK(!levels.manual);
    levels = rules.tick(utcAt(2026, 3, 2, 7, 0), "UTC0", 40);
    CHECK(!levels.night);
    CHECK(!levels.manual);

    // Day by hand in the middle of the night: until 07:00, then the schedule again.
    DeskRules other = nightRules();
    other.tick(utcAt(2026, 3, 1, 23, 0), "UTC0", 0);
    other.toggleNight();
    levels = other.tick(utcAt(2026, 3, 1, 23, 0), "UTC0", 10);
    CHECK(!levels.night);
    CHECK(levels.manual);
    CHECK_EQ(levels.led, 100);
    CHECK(!other.tick(utcAt(2026, 3, 2, 6, 59), "UTC0", 20).night);
    levels = other.tick(utcAt(2026, 3, 2, 7, 0), "UTC0", 30);
    CHECK(!levels.night);
    CHECK(!levels.manual);
    CHECK(!other.tick(utcAt(2026, 3, 2, 21, 59), "UTC0", 40).night);
    CHECK(other.tick(utcAt(2026, 3, 2, 22, 0), "UTC0", 50).night);
}

TEST(night_by_hand_without_night_hours) {
    DeskRules rules;
    const int64_t noon = utcAt(2026, 3, 1, 12, 0);
    rules.tick(noon, "UTC0", 0);
    rules.toggleNight();
    CHECK(rules.tick(noon, "UTC0", 10).night);
    CHECK(rules.tick(utcAt(2026, 3, 2, 3, 0), "UTC0", 20).night);
    rules.toggleNight();
    const DeskRules::Levels levels = rules.tick(utcAt(2026, 3, 2, 4, 0), "UTC0", 30);
    CHECK(!levels.night);
    CHECK(!levels.manual);
}

TEST(clock_set_or_zone_changed_mid_override) {
    DeskRules rules = nightRules();
    const int64_t when = utcAt(2026, 3, 1, 20, 0);  // day in UTC
    rules.tick(when, "UTC0", 0);
    rules.toggleNight();
    CHECK(rules.tick(when, "UTC0", 10).night);
    // In Tokyo it is 05:00 the next morning: scheduled night, so the schedule takes over.
    DeskRules::Levels levels = rules.tick(when, "JST-9", 20);
    CHECK(levels.night);
    CHECK(!levels.manual);
    // Back to a zone where it is day: day, and the old override is gone.
    levels = rules.tick(when, "UTC0", 30);
    CHECK(!levels.night);
    CHECK(!levels.manual);
}

TEST(preview_shows_a_level_briefly) {
    DeskRules rules = nightRules();
    const int64_t noon = utcAt(2026, 3, 1, 12, 0);
    rules.tick(noon, "UTC0", 1000);
    rules.preview(true, 1000);
    DeskRules::Levels levels = rules.tick(noon, "UTC0", 3999);
    CHECK(levels.night);
    CHECK_EQ(levels.screen, 10);
    CHECK_EQ(levels.led, 0);
    levels = rules.tick(noon, "UTC0", 4000);
    CHECK(!levels.night);
    CHECK_EQ(levels.screen, 80);
}

TEST(wake_timer_survives_millis_wrapping) {
    DeskRules rules = nightRules();
    const int64_t late = utcAt(2026, 3, 1, 23, 0);
    rules.tick(late, "UTC0", 0xFFFFFFF0u);
    rules.press(0xFFFFFFF0u);
    CHECK_EQ(rules.tick(late, "UTC0", 0x00000010u).screen, 80);
    CHECK_EQ(rules.tick(late, "UTC0", 0xFFFFFFF0u + DeskRules::kWakeMs).screen, 10);

    rules.preview(true, 0xFFFFFFF0u);
    CHECK_EQ(rules.tick(utcAt(2026, 3, 1, 12, 0), "UTC0", 0x00000010u).night, true);
}


namespace {

Rotation::Input rotationInput(std::vector<bool> rotates, int onScreen) {
    Rotation::Input input;
    input.rotates = rotates;
    input.onScreen = onScreen;
    return input;
}

Rotation rotationEvery(int seconds) {
    Rotation rotation;
    rotation.setSeconds(seconds);
    return rotation;
}

}  // namespace

TEST(rotation_is_off_by_default) {
    Rotation rotation;
    const Rotation::Input input = rotationInput({false, true, true}, 1);
    for (uint32_t t = 0; t < 1000000; t += 5000) CHECK_EQ(rotation.tick(input, t), -1);
}

TEST(rotation_moves_on_and_wraps) {
    Rotation rotation = rotationEvery(30);
    Rotation::Input input = rotationInput({false, true, true, true}, 1);
    CHECK_EQ(rotation.tick(input, 0), -1);
    CHECK_EQ(rotation.tick(input, 29900), -1);
    CHECK_EQ(rotation.tick(input, 30000), 2);
    input.onScreen = 3;
    CHECK_EQ(rotation.tick(input, 30100), -1);
    CHECK_EQ(rotation.tick(input, 60100), 1);
}

TEST(rotation_skips_cards) {
    Rotation rotation = rotationEvery(10);
    Rotation::Input input = rotationInput({false, true, false, true}, 1);
    rotation.tick(input, 0);
    CHECK_EQ(rotation.tick(input, 10000), 3);
    input.onScreen = 3;
    rotation.tick(input, 10000);
    CHECK_EQ(rotation.tick(input, 20000), 1);
}

TEST(rotation_never_leaves_a_skipped_card) {
    Rotation rotation = rotationEvery(10);
    for (int onScreen : {0, 2}) {
        const Rotation::Input input = rotationInput({false, true, false, true}, onScreen);
        for (uint32_t t = 0; t < 200000; t += 7000) CHECK_EQ(rotation.tick(input, t), -1);
    }
}

TEST(rotation_needs_two_cards) {
    Rotation rotation = rotationEvery(10);
    const Rotation::Input one = rotationInput({false, true}, 1);
    const Rotation::Input alone = rotationInput({false, true, false}, 1);
    for (uint32_t t = 0; t < 100000; t += 5000) {
        CHECK_EQ(rotation.tick(one, t), -1);
        CHECK_EQ(rotation.tick(alone, t), -1);
    }
}

TEST(rotation_waits_while_playing_a_banner_or_dark) {
    for (int flag = 0; flag < 3; flag++) {
        Rotation rotation = rotationEvery(30);
        Rotation::Input input = rotationInput({false, true, true}, 1);
        rotation.tick(input, 0);
        input.playing = flag == 0;
        input.banner = flag == 1;
        input.screenOff = flag == 2;
        CHECK_EQ(rotation.tick(input, 31000), -1);
        input.playing = input.banner = input.screenOff = false;
        CHECK_EQ(rotation.tick(input, 31100), 2);
    }
}

TEST(rotation_pauses_after_a_move_by_hand) {
    Rotation rotation = rotationEvery(10);
    Rotation::Input input = rotationInput({false, true, true}, 1);
    rotation.tick(input, 0);
    input.onScreen = 2;
    rotation.movedByHand(0);
    CHECK_EQ(rotation.tick(input, 0), -1);
    CHECK_EQ(rotation.tick(input, 20000), -1);
    CHECK_EQ(rotation.tick(input, Rotation::kHandPauseMs - 1), -1);
    CHECK_EQ(rotation.tick(input, Rotation::kHandPauseMs), 1);
}

TEST(a_card_that_came_to_the_front_gets_a_full_interval) {
    Rotation rotation = rotationEvery(30);
    Rotation::Input input = rotationInput({false, true, true}, 1);
    rotation.tick(input, 0);
    input.onScreen = 2;
    CHECK_EQ(rotation.tick(input, 25000), -1);
    CHECK_EQ(rotation.tick(input, 54900), -1);
    CHECK_EQ(rotation.tick(input, 55000), 1);
}

TEST(rotation_survives_a_changing_stack) {
    Rotation rotation = rotationEvery(10);
    const std::vector<std::vector<bool>> lists = {
        {}, {true}, {false, true, true, true, true}, {false, true, true}, {true, false}, {}, {false, true, true, true}};
    uint32_t now = 0;
    for (int onScreen : {-3, -1, 0, 1, 2, 3, 4, 9, 1000}) {
        for (const auto& rotates : lists) {
            now += 20000;
            Rotation::Input input = rotationInput(rotates, onScreen);
            const int next = rotation.tick(input, now);
            CHECK(next == -1 || (next >= 0 && next < static_cast<int>(rotates.size()) && rotates[next]));
        }
    }
}

TEST(rotation_timer_survives_millis_wrapping) {
    Rotation rotation = rotationEvery(30);
    Rotation::Input input = rotationInput({false, true, true}, 1);
    rotation.tick(input, 0xFFFFFFF0u);
    CHECK_EQ(rotation.tick(input, 0x00000010u), -1);
    CHECK_EQ(rotation.tick(input, 0xFFFFFFF0u + 30000u), 2);

    rotation.movedByHand(0xFFFFFFF0u);
    CHECK_EQ(rotation.tick(input, 0x00000010u), -1);
    CHECK_EQ(rotation.tick(input, 0xFFFFFFF0u + Rotation::kHandPauseMs), 2);
}


// ----------------------------------------------------------------- messages

TEST(message_reaches_a_single_card_plugin) {
    Fixture f;
    CHECK(f.start(R"(
        local last = "nothing yet"
        function message(msg)
          last = msg.body
          if msg.json then print("json", msg.json.who, msg.json.n) else print("text", msg.body) end
          notify(msg.body)
        end
        function draw() ui.big(last) end
    )"));
    CHECK(f.plugin.takesMessages());
    CHECK(f.plugin.kinds()[0].message);
    f.draw();
    CHECK(!f.needsDraw());

    CHECK(f.plugin.message("", "Pizza's here"));
    CHECK(f.services.logged("text Pizza's here"));
    CHECK(f.needsDraw());  // a message repaints the card
    f.draw();
    CHECK(f.gfx.drew("Pizza's here"));
    CHECK_EQ(f.services.notices.size(), (size_t)1);

    // To the card or to the plugin: a single-card plugin has one message().
    CHECK(f.plugin.message("1", R"(  {"who":"door","n":2})"));
    CHECK(f.services.logged("json door 2"));
    CHECK(f.plugin.message("", "{not json"));
    CHECK(f.services.logged("text {not json"));
    CHECK(f.plugin.message("", ""));
    CHECK(!f.plugin.message("9", "no such card"));
    CHECK(!f.plugin.failed());

    // No message(): nothing to deliver to.
    Fixture deaf;
    CHECK(deaf.start("function draw() end"));
    CHECK(!deaf.plugin.takesMessages());
    CHECK(!deaf.plugin.kinds()[0].message);
    CHECK(!deaf.plugin.message("", "hello?"));
    CHECK(!deaf.plugin.message("1", "hello?"));

    // A message handler that crashes or hangs takes the plugin down, not the device.
    Fixture crash;
    CHECK(crash.start("function message(msg) error('cannot read ' .. msg.body) end"));
    CHECK(!crash.plugin.message("", "this"));
    CHECK(crash.plugin.failed());
    CHECK(contains(crash.plugin.error(), "cannot read this"));
    CHECK(!crash.plugin.message("", "again"));

    Fixture spin;
    spin.services.advancePerCall = 1;
    CHECK(spin.start("function message() while true do end end"));
    CHECK(!spin.plugin.message("", "x"));
    CHECK(contains(spin.plugin.error(), "took too long"));

    // A message is held against the plugin's memory like everything else.
    LuaPlugin::Limits small;
    small.memoryBytes = 64 * 1024;
    Fixture tight(small);
    CHECK(tight.start("kept = {} function message(msg) kept[#kept + 1] = msg.body end"));
    const std::string big(4096, 'x');
    bool survived = true;
    for (int i = 0; i < 40 && survived; i++) survived = tight.plugin.message("", big + std::to_string(i));
    CHECK(!survived);
    CHECK(contains(tight.plugin.error(), "out of memory"));
}

TEST(message_reaches_a_card_kind) {
    Fixture f;
    CHECK(f.start(R"(
        function message(msg) print("plugin got", msg.body) end
        cards = {
          door = {
            message = function(card, msg) card.last = msg.body print("card", card.id, "got", msg.body) end,
            draw = function(card) ui.big(card.last or "quiet") end,
          },
          plain = { draw = function(card) ui.big("plain") end },
        }
    )"));
    f.plugin.addCard("2", "plain", {}, f.gfx);
    CHECK(f.plugin.takesMessages());
    CHECK_EQ(f.plugin.kinds()[0].key, "door");
    CHECK(f.plugin.kinds()[0].message);
    CHECK(!f.plugin.kinds()[1].message);

    CHECK(f.plugin.message("1", "ding"));
    CHECK(f.services.logged("card 1 got ding"));
    CHECK(!f.plugin.message("2", "dong"));  // that kind of card takes no messages
    CHECK(f.plugin.message("", "for everyone"));
    CHECK(f.services.logged("plugin got for everyone"));
    f.draw();
    CHECK(f.gfx.drew("ding"));

    // Inspecting a plugin tells which kinds take messages, without running load().
    Fixture probe;
    const std::string source = "cards = { a = { message = function() end }, b = {} }";
    CHECK(probe.plugin.inspect("probe", source.data(), source.size()));
    CHECK(!probe.plugin.takesMessages());
    CHECK(probe.plugin.kinds()[0].message);
    CHECK(!probe.plugin.kinds()[1].message);
}

// ------------------------------------------------------------------ choices

TEST(choices_are_listed_by_the_plugin) {
    Fixture f;
    CHECK(f.start(R"(
        config = {
          device = { label = "Device", choices = "listDevices" },
          room = { label = "Room", choices = "not a name!" },
          plain = "x",
        }
        function listDevices(done)
          done({ { value = "abc", label = "Kitchen lamp" }, { value = 7 }, "plain", 12, { value = "x", label = 5 } })
          done({ { value = "ignored" } })   -- only the first answer counts
        end
        function failing(done) done(nil, "log in first") end
        function empty(done) done({}) end
        function silent(done) end
        function wrong(done) done({ { label = "no value" } }) end
        function many(done)
          local list = {}
          for i = 1, 150 do list[i] = "item " .. i end
          done(list)
        end
    )"));
    // The settings say which function fills them in. Only a plain name counts.
    const auto& schema = f.plugin.configSchema();
    CHECK_EQ(schema[0].key, "device");
    CHECK_EQ(schema[0].choices, "listDevices");
    CHECK_EQ(schema[2].key, "room");
    CHECK_EQ(schema[2].choices, "");
    CHECK_EQ(schema[1].choices, "");

    f.plugin.choices("listDevices", 1);
    CHECK_EQ(f.services.choiceAnswers.size(), (size_t)1);
    const auto& first = f.services.choiceAnswers[0];
    CHECK_EQ(first.id, 1);
    CHECK_EQ(first.error, "");
    CHECK_EQ(first.choices.size(), (size_t)5);
    CHECK_EQ(first.choices[0].value, "abc");
    CHECK_EQ(first.choices[0].label, "Kitchen lamp");
    CHECK_EQ(first.choices[1].value, "7");
    CHECK_EQ(first.choices[1].label, "7");  // no label: the value
    CHECK_EQ(first.choices[2].value, "plain");
    CHECK_EQ(first.choices[3].value, "12");
    CHECK_EQ(first.choices[4].label, "5");

    f.plugin.choices("failing", 2);
    CHECK_EQ(f.services.choiceAnswers[1].error, "log in first");
    f.plugin.choices("empty", 3);
    CHECK_EQ(f.services.choiceAnswers[2].error, "");
    CHECK(f.services.choiceAnswers[2].choices.empty());
    f.plugin.choices("nowhere", 4);
    CHECK(contains(f.services.choiceAnswers[3].error, "no function nowhere()"));
    f.plugin.choices("wrong", 5);
    CHECK(contains(f.services.choiceAnswers[4].error, "needs a value"));
    f.plugin.choices("many", 6);
    CHECK_EQ(f.services.choiceAnswers[5].choices.size(), (size_t)100);
    // Long entries: the list stops at 8 KB in total.
    Fixture fat;
    CHECK(fat.start("function list(done) local l = {} for i = 1, 100 do l[i] = { value = i, label = string.rep('x', 190) } end done(l) end"));
    fat.plugin.choices("list", 1);
    CHECK_EQ(fat.services.choiceAnswers[0].error, "");
    CHECK_EQ(fat.services.choiceAnswers[0].choices.size(), (size_t)42);

    f.plugin.choices("plain", 7);  // a global, but not a function
    CHECK(contains(f.services.choiceAnswers[6].error, "no function"));

    // A function that never answers: the host hears so after ten seconds.
    f.plugin.choices("silent", 8);
    CHECK_EQ(f.services.choiceAnswers.size(), (size_t)7);
    pass(f, 9);
    CHECK_EQ(f.services.choiceAnswers.size(), (size_t)7);
    pass(f, 1.5f);
    CHECK_EQ(f.services.choiceAnswers.size(), (size_t)8);
    CHECK_EQ(f.services.choiceAnswers[7].id, 8);
    CHECK(contains(f.services.choiceAnswers[7].error, "in time"));
    CHECK(!f.plugin.failed());
}

TEST(choices_are_told_about_the_form) {
    Fixture f;
    CHECK(f.start(R"(
        config = { url = "http://x", key = { secret = true }, home = { choices = "list" } }
        cards = {
          device = { config = {
            flow = { choices = "list", default = "" },
            advanced = { default = false },
            count = 3,
            room = "hall",
          } },
        }
        function list(done, ctx)
          local keys = {}
          for key, value in pairs(ctx.settings) do keys[#keys + 1] = key .. "=" .. tostring(value) .. ":" .. type(value) end
          table.sort(keys)
          print(tostring(ctx.kind) .. " " .. table.concat(keys, " "))
          done({})
        end
        function old(done) done({ "still works" }) end
    )", {{"url", "http://homey"}, {"key", "s3cret"}}));
    // Nothing sent: the kind's defaults, typed as `config` is.
    f.plugin.choices("list", 1, {"device", {}});
    CHECK_EQ(f.services.logs.back(), "device advanced=false:boolean count=3:number flow=:string room=hall:string");
    // What the form holds wins, typed by the field it is for. Text that is no number leaves the default.
    f.plugin.choices("list", 2, {"device", {{"advanced", "true"}, {"count", "7"}, {"flow", "f-1"}, {"extra", "5"}}});
    CHECK_EQ(f.services.logs.back(), "device advanced=true:boolean count=7:number extra=5:string flow=f-1:string room=hall:string");
    f.plugin.choices("list", 3, {"device", {{"count", "many"}}});
    CHECK(contains(f.services.logs.back(), "count=3:number"));
    // A setting of the plugin itself: no kind, and what is in use where the form says nothing.
    f.plugin.choices("list", 4, {"", {{"url", "http://typed"}}});
    CHECK_EQ(f.services.logs.back(), "nil home=:string key=s3cret:string url=http://typed:string");
    f.plugin.choices("list", 5);
    CHECK_EQ(f.services.logs.back(), "nil home=:string key=s3cret:string url=http://homey:string");
    // A kind the plugin does not have: the form as text, and no crash.
    f.plugin.choices("list", 6, {"nope", {{"a", "1"}}});
    CHECK_EQ(f.services.logs.back(), "nope a=1:string");
    // A function written before there was a second argument is called as before.
    f.plugin.choices("old", 7);
    CHECK_EQ(f.services.choiceAnswers.size(), (size_t)7);
    CHECK_EQ(f.services.choiceAnswers[6].choices[0].value, "still works");
    CHECK(!f.plugin.failed());
}

TEST(choices_may_fetch_first) {
    Fixture f;
    CHECK(f.start(R"(
        function listFlows(done)
          http.get("https://example.com/flows", function(res)
            if not res.ok then return done(nil, "HTTP " .. res.status) end
            local list = {}
            for _, flow in ipairs(res.json) do list[#list + 1] = { value = flow.id, label = flow.name } end
            done(list)
          end)
        end
    )"));
    f.plugin.choices("listFlows", 11);
    CHECK(f.services.choiceAnswers.empty());
    CHECK_EQ(f.services.requests.size(), (size_t)1);
    f.respond(0, 200, R"([{"id":"f1","name":"Morning"},{"id":"f2","name":"Away"}])");
    CHECK_EQ(f.services.choiceAnswers.size(), (size_t)1);
    CHECK_EQ(f.services.choiceAnswers[0].id, 11);
    CHECK_EQ(f.services.choiceAnswers[0].choices.size(), (size_t)2);
    CHECK_EQ(f.services.choiceAnswers[0].choices[1].label, "Away");

    f.plugin.choices("listFlows", 12);
    f.respond(1, 401, "");
    CHECK_EQ(f.services.choiceAnswers[1].error, "HTTP 401");

    // Answering after the ten seconds are over is ignored.
    f.plugin.choices("listFlows", 13);
    pass(f, 11);
    CHECK_EQ(f.services.choiceAnswers.size(), (size_t)3);
    f.respond(2, 200, "[]");
    CHECK_EQ(f.services.choiceAnswers.size(), (size_t)3);

    // At most four lists at once.
    for (int i = 0; i < 5; i++) f.plugin.choices("listFlows", 20 + i);
    CHECK_EQ(f.services.choiceAnswers.size(), (size_t)4);
    CHECK_EQ(f.services.choiceAnswers[3].id, 24);
    CHECK(contains(f.services.choiceAnswers[3].error, "busy"));

    // A restart answers whoever is still waiting.
    CHECK(f.start("x = 1"));
    CHECK_EQ(f.services.choiceAnswers.size(), (size_t)8);
    CHECK(contains(f.services.choiceAnswers[7].error, "restarted"));
}

TEST(choices_cannot_take_the_device_down) {
    Fixture crash;
    CHECK(crash.start("function list(done) error('list broke') end"));
    crash.plugin.choices("list", 1);
    CHECK(crash.plugin.failed());
    CHECK_EQ(crash.services.choiceAnswers.size(), (size_t)1);
    CHECK(contains(crash.services.choiceAnswers[0].error, "list broke"));
    crash.plugin.choices("list", 2);  // asking a crashed plugin answers at once
    CHECK(contains(crash.services.choiceAnswers[1].error, "crashed"));

    Fixture spin;
    spin.services.advancePerCall = 1;
    CHECK(spin.start("function list(done) while true do end end"));
    spin.plugin.choices("list", 1);
    CHECK(contains(spin.plugin.error(), "took too long"));
    CHECK_EQ(spin.services.choiceAnswers.size(), (size_t)1);

    // A plugin that keeps `done` and calls it much later does no harm.
    Fixture late;
    CHECK(late.start(R"(
        function list(done) kept = done end
        function button() kept({ "late" }) return true end
    )"));
    late.plugin.choices("list", 1);
    pass(late, 11);
    CHECK_EQ(late.services.choiceAnswers.size(), (size_t)1);
    CHECK(late.button(Button::Center));
    CHECK_EQ(late.services.choiceAnswers.size(), (size_t)1);
    CHECK(!late.plugin.failed());

    // Going away with a list still wanted tells nobody: the host is going too.
    MockServices services;
    {
        RecordingGfx gfx;
        LuaPlugin plugin(gfx, services);
        const std::string source = "function list(done) end";
        plugin.start("gone", source.data(), source.size());
        plugin.choices("list", 1);
    }
    CHECK(services.choiceAnswers.empty());
}

}  // namespace

int main(int argc, char** argv) {
    g_pluginDir = argc > 1 ? argv[1] : "plugins";
    const char* only = argc > 2 ? argv[2] : nullptr;
    int ran = 0;
    for (const TestCase& test : tests()) {
        if (only && !strstr(test.name, only)) continue;
        const int before = g_failures;
        test.body();
        ran++;
        printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", test.name);
    }
    printf("\n%d tests, %d checks, %d failed\n", ran, g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
