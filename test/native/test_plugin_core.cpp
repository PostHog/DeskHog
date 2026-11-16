// Desktop tests for the Lua plugin runtime. Run with test/native/run.sh.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "LuaPlugin.h"
#include "PixelRuns.h"
#include "SourceHash.h"
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
          ui.title("Ghent")
          ui.big("21°")
          ui.label("Cloudy")
          ui.chart({1, 2, 3})
        end
    )"));
    f.draw();
    CHECK(!f.plugin.failed());
    CHECK(f.gfx.drew("text 8,6 #aaaaaa small left Ghent"));
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
          city = { label = "City", default = "Ghent" },
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
    CHECK(f.gfx.drew("Ghent 3 integer true plain"));
    CHECK_EQ(f.plugin.configSchema().size(), (size_t)4);

    bool sawCity = false;
    for (const LuaPlugin::ConfigField& field : f.plugin.configSchema()) {
        if (field.key != "city") continue;
        sawCity = true;
        CHECK_EQ(field.label, "City");
        CHECK_EQ(field.type, "string");
        CHECK_EQ(field.defaultValue, "Ghent");
    }
    CHECK(sawCity);

    Fixture g;
    CHECK(g.start(source, {{"city", "Antwerp"}, {"days", "7"}, {"metric", "false"}, {"short", "changed"}}));
    g.draw();
    CHECK(g.gfx.drew("Antwerp 7 integer false changed"));

    Fixture h;  // a saved value that is not a number keeps the default
    CHECK(h.start(source, {{"days", "lots"}}));
    h.draw();
    CHECK(h.gfx.drew("Ghent 3 integer"));
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
    RecordingGfx ghent, lisbon;
    MockServices services;
    LuaPlugin plugin(ghent, services);
    const std::string source = readFile(g_pluginDir + "/weather.lua");
    CHECK(plugin.start("weather", source.data(), source.size(), {{"unit", "F"}}));
    CHECK(plugin.addCard("a", "city", {{"city", "Ghent"}}, ghent));
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
    CHECK(ghent.drew("small left Ghent"));
    CHECK(ghent.drew("Loading..."));        // its own lookup has not come back yet
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
    CHECK(f.startFile("clock", {{"offset", "1"}}));
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
    CHECK(twelve.startFile("clock", {{"hours12", "true"}, {"label", "Home"}, {"offset", "-5.5"}}));
    twelve.draw();
    CHECK(twelve.gfx.drew("7:19 pm"));
    CHECK(twelve.gfx.drew("Home"));
    CHECK(twelve.gfx.drew("Saturday 15 November 2025"));

    Fixture unset;
    unset.services.epoch = 0;
    CHECK(unset.startFile("clock"));
    unset.draw();
    CHECK(unset.gfx.drew("Waiting for the time"));
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
    struct Case { const char* date; const char* time; const char* offset; const char* expect; };
    const Case cases[] = {
        {"2025-12-25", "", "0", "large center 39 days"},
        {"2025-11-15", "", "0", "Today!"},
        {"2025-11-16", "", "15", "Today!"},              // fifteen hours ahead it is already tomorrow
        {"2025-11-16", "9:00", "0", "23h 13m"},
        {"2025-11-15", "10:00", "0", "13:20"},
        {"2025-11-15", "9:00", "0", "Today!"},           // earlier today
        {"2025-11-10", "", "0", "5 days ago"},
        {"2025-11-14", "", "0", "1 day ago"},
        {"2100-01-01", "", "0", " days"},                // far beyond what a 32-bit clock counts in seconds
        {"next week", "", "0", "Write the date as"},
        {"2025-13-01", "", "0", "Write the date as"},
    };
    for (const Case& c : cases) {
        Fixture f;
        CHECK(f.startFile("countdown", {{"offset", c.offset}}, {{"what", "Launch"}, {"date", c.date}, {"time", c.time}}));
        f.draw();
        if (!f.gfx.drew(c.expect)) printf("  %s %s +%s: expected %s, drew\n%s", c.date, c.time, c.offset, c.expect, f.gfx.dump().c_str());
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
}

TEST(sample_trains) {
    Fixture f;
    const long long now = f.services.epoch;
    CHECK(f.startFile("trains", {}, {{"station", "Gent Sint Pieters"}}));
    CHECK_EQ(f.services.requests[0].request.url,
             "https://api.irail.be/v1/liveboard?format=json&lang=en&arrdep=departure&station=Gent%20Sint%20Pieters");
    const std::string board = R"({"version":"1.3","station":"Ghent-Sint-Pieters","departures":{"number":"4","departure":[
        {"id":"0","station":"Brugge","time":")" + std::to_string(now + 420) + R"(","delay":"0","canceled":"0","platform":"5","vehicle":"x"},
        {"id":"1","station":"Antwerpen-Centraal","time":")" + std::to_string(now + 900) + R"(","delay":"300","canceled":"0","platform":"8"},
        {"id":"2","station":"Oostende","time":")" + std::to_string(now + 1200) + R"(","delay":"0","canceled":"1","platform":"3"},
        {"id":"3","station":"Left already","time":")" + std::to_string(now - 600) + R"(","delay":"0","canceled":"0","platform":"1"}]}})";
    f.respond(0, 200, board);
    CHECK_EQ(f.plugin.cardTitle("1"), "Ghent-Sint-Pieters");
    f.draw();
    CHECK(f.gfx.drew("small left Ghent-Sint-Pieters"));
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
    CHECK(filtered.gfx.drew("Antwerpen-Centraal"));
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
        CHECK(f.startFile("rain", {{"city", "Gent"}}));
        f.respond(0, 200, R"({"results":[{"name":"Ghent","latitude":51.05,"longitude":3.72}]})");
        CHECK(contains(f.services.requests[1].request.url, "minutely_15=precipitation&forecast_minutely_15=8"));
        f.respond(1, 200, std::string(R"({"minutely_15":{"time":[],"precipitation":)") + c.amounts + "}}");
        f.draw();
        if (!f.gfx.drew(c.expect)) printf("  %s: expected %s, drew\n%s", c.amounts, c.expect, f.gfx.dump().c_str());
        CHECK(f.gfx.drew(c.expect));
        CHECK(f.gfx.drew("small left Rain in Ghent"));
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
    CHECK(startKind(f, "space", "station", {}, {{"city", "Gent"}}));
    CHECK(contains(f.services.requests[0].request.url, "geocoding-api.open-meteo.com/v1/search?count=1&name=Gent"));
    f.respond(0, 200, R"({"results":[{"name":"Ghent","latitude":51.05,"longitude":3.72}]})");
    CHECK_EQ(f.services.requests[1].request.url, "https://api.wheretheiss.at/v1/satellites/25544");
    // Over Western Australia: a long way to the east.
    f.respond(1, 200, R"({"name":"iss","latitude":-29.17,"longitude":123.61,"visibility":"daylight","velocity":27538.3})");
    f.draw();
    CHECK(f.gfx.drew(" km"));
    CHECK(f.gfx.drew("to the east of Ghent"));
    CHECK(f.gfx.drew("Over 29°S 124°E, in"));
    CHECK_EQ(f.services.led, "default");

    // Half a minute later it is over the Netherlands: look up.
    pass(f, 31, false);
    CHECK_EQ(f.services.requests.size(), (size_t)3);   // the city is not looked up again
    f.respond(2, 200, R"({"latitude":52.4,"longitude":4.9,"visibility":"eclipsed"})");
    CHECK_EQ(f.services.led, "#3498db for 20000ms");
    f.draw();
    CHECK(f.gfx.drew("large center 171 km"));
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
    f.runFrames(0.5f);                          // long enough for "Bonk!" to go, too short for the next one to get away
    CHECK_EQ(f.button(buttons[(burrow + 1) % 3]), true);  // an empty burrow, or the wrong one: a life
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
