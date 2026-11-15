#pragma once

#include <stddef.h>
#include <stdint.h>
#include <map>
#include <string>
#include <vector>

#include "PluginHost.h"

struct lua_State;
struct lua_Debug;

namespace deskhog {

/**
 * One running copy of a plugin: a sandboxed Lua state, and the cards it shows.
 *
 * The host calls this a "connection": a plugin plus one set of shared
 * settings (say, a PostHog account). A connection can show several cards.
 * They all live in this one Lua state, so they share data, one login and one
 * set of network requests.
 *
 * A plugin file takes one of two shapes:
 *
 *  - Single card. It defines the global functions load(), update(dt), draw()
 *    and button(name). It has one card kind, called "main".
 *  - Several card kinds. It defines a global table `cards`, with one entry per
 *    kind. Each entry holds that kind's settings and its own load(card),
 *    update(card, dt), draw(card) and button(card, name).
 *
 * A plugin can never take the host down. Every call into Lua is protected, has
 * a time budget and runs against a memory cap. When any of those trips, the
 * connection is marked failed and each of its cards paints the error instead.
 *
 * Not thread safe: call everything from the UI task.
 */
class LuaPlugin {
public:
    struct Limits {
        size_t memoryBytes = 256 * 1024;  ///< Lua heap cap
        uint32_t callBudgetMs = 250;      ///< update, draw, button, http callbacks
        uint32_t loadBudgetMs = 2000;     ///< running the file, and load()
        size_t imageBytes = 512 * 1024;   ///< All of a plugin's images together
        int maxPendingHttp = 12;          ///< Shared by all of the plugin's cards
    };

    /** Where the Lua heap comes from. The firmware points this at PSRAM. */
    struct Memory {
        void* (*realloc)(void* ptr, size_t size) = nullptr;
        void (*free)(void* ptr) = nullptr;
    };

    struct ConfigField {
        std::string key;
        std::string label;
        std::string type;                  ///< "string", "number" or "boolean"
        std::string defaultValue;          ///< As text
        int order = 1000;                  ///< Where it goes in a form; lower comes first
        bool secret = false;               ///< An API key or similar: never shown back
        std::vector<std::string> options;  ///< If not empty, the value is one of these
    };

    /** A kind of card the plugin can show, e.g. "insight". */
    struct CardKind {
        std::string key;
        std::string name;                 ///< For people, e.g. "Insight"
        std::vector<ConfigField> config;  ///< Settings each card of this kind has
    };

    using Settings = std::map<std::string, std::string>;

    /** The key of the only card kind a single-card plugin has. */
    static const char* const kMainKind;

    /**
     * @param metrics Used for the card size and for measuring text when no card
     *                is being drawn. Its drawing calls are never used.
     */
    LuaPlugin(Gfx& metrics, PluginServices& services);
    LuaPlugin(Gfx& metrics, PluginServices& services, const Limits& limits, const Memory& memory);
    ~LuaPlugin();

    LuaPlugin(const LuaPlugin&) = delete;
    LuaPlugin& operator=(const LuaPlugin&) = delete;

    /**
     * Run the plugin file, then its global load() if it has one. Drops any
     * cards from an earlier run.
     * @param settings Saved values for the plugin's shared `config`.
     * @return false if the plugin failed; error() says why.
     */
    bool start(const std::string& name, const char* source, size_t length, const Settings& settings = {});

    /**
     * Run the plugin file only, to find out its title, shared settings and
     * card kinds. load() is not called.
     */
    bool inspect(const std::string& name, const char* source, size_t length);

    // ---- cards ----------------------------------------------------------

    /**
     * Add a card of the given kind and run that kind's load(card).
     * @param settings Saved values for the kind's `config`.
     * @return false if the kind does not exist or the plugin failed.
     */
    bool addCard(const std::string& id, const std::string& kind, const Settings& settings, Gfx& surface);

    /** The card's surface is going away. The card and its state stay, hidden. */
    void detachCard(const std::string& id);
    /** Give a detached card a surface again. @return false if there is no such card. */
    bool attachCard(const std::string& id, Gfx& surface);
    void removeCard(const std::string& id);
    bool hasCard(const std::string& id) const { return findCard(id) != nullptr; }

    /** Tell the plugin whether a card is on screen. Only visible cards get update(dt). */
    void setVisible(const std::string& id, bool visible);

    /** Advance timers: load() when a refresh is due, update(dt) for visible cards. */
    void tick(uint32_t nowMs);

    /** True when draw(id) would paint something new. */
    bool needsDraw(const std::string& id) const;

    /** Paint a card. The host wraps this in its begin/end frame. */
    void draw(const std::string& id);

    /** @return true if the plugin used the press, which stops card navigation. */
    bool button(const std::string& id, Button button);

    /** What the plugin calls this card (its `card.title`), or "" if it has not said. */
    std::string cardTitle(const std::string& id);

    // ---- everything else ------------------------------------------------

    /** Answer to a request started through PluginServices::httpStart. */
    void httpResponse(int requestId, const HttpResponse& response);

    /** Ask for load() to run again on the next tick, e.g. because the network just came up. */
    void refreshNow() { loadDue_ = true; }

    bool failed() const { return failed_; }
    const std::string& error() const { return error_; }
    const std::string& name() const { return name_; }
    /** plugin.name if the plugin set one, else the file name. */
    const std::string& title() const { return title_; }
    /** What the plugin says about itself in its `plugin` table. "" for what it left out. */
    const std::string& description() const { return description_; }
    const std::string& version() const { return version_; }
    const std::string& author() const { return author_; }
    size_t memoryUsed() const { return memUsed_; }
    /** The plugin's shared settings. */
    const std::vector<ConfigField>& configSchema() const { return schema_; }
    const std::vector<CardKind>& kinds() const { return kinds_; }
    size_t cardCount() const { return cards_.size(); }

private:
    enum class CallKind { Start, Load, AddCard, CardLoad, CardUpdate, CardDraw, CardButton, CardTitle, Http };

    struct Card {
        std::string id;
        std::string kind;
        int ref = -2;            ///< Registry reference to the card's Lua table (LUA_NOREF for "main")
        Gfx* surface = nullptr;  ///< Null while detached
        bool visible = false;
        bool dirty = true;
        bool hasLoad = false;
        bool hasUpdate = false;
        bool hasDraw = false;
        bool hasButton = false;
        bool frameClockValid = false;
        uint32_t lastFrameMs = 0;
    };

    struct Call {
        LuaPlugin* self;
        CallKind kind;
        const char* source = nullptr;
        size_t length = 0;
        Card* card = nullptr;
        const Settings* settings = nullptr;
        float dt = 0;
        const char* buttonName = nullptr;
        int requestId = 0;
        int filterRef = -2;  ///< LUA_NOREF
        const HttpResponse* response = nullptr;
        bool result = false;
        std::string* text = nullptr;
    };

    /** An image a plugin made with gfx.image(). Pixels are 0xAARRGGBB. */
    struct Image {
        int width;
        int height;
        std::vector<uint32_t> pixels;
    };

    struct UiItem {
        enum Kind { Title, Big, Label, Chart } kind;
        std::string text;
        uint32_t color;
        std::vector<float> values;
    };

    bool begin(const std::string& name, const char* source, size_t length, const Settings& settings, bool runLoad);
    bool run(Call& call, uint32_t budgetMs);
    void fail(const std::string& message);
    void close();
    void markAllDirty();
    Card* findCard(const std::string& id);
    const Card* findCard(const std::string& id) const;
    void drawError(Gfx& gfx);
    void flushUi(Gfx& gfx);

    // Protected bodies, run through trampoline().
    void doStart(lua_State* L, Call& call);
    void doAddCard(lua_State* L, Call& call);
    void doHttp(lua_State* L, Call& call);
    void openSandbox(lua_State* L);
    void readPluginTable(lua_State* L);
    void readKinds(lua_State* L);
    static void resolveSpec(lua_State* L, int spec, const Settings* settings, std::vector<ConfigField>* schema,
                            int target);
    /** Push a card callback and its `card` argument. @return the number of arguments pushed, or -1 if there is no such function. */
    int pushCardFunction(lua_State* L, const Card& card, const char* function);

    static int trampoline(lua_State* L);
    static void hook(lua_State* L, lua_Debug* ar);
    static void* alloc(void* ud, void* ptr, size_t osize, size_t nsize);
    static LuaPlugin* from(lua_State* L);

    // Lua-facing API.
    static int l_setmetatable(lua_State* L);
    static int l_print(lua_State* L);
    static int l_gfx_clear(lua_State* L);
    static int l_gfx_rect(lua_State* L);
    static int l_gfx_outline(lua_State* L);
    static int l_gfx_line(lua_State* L);
    static int l_gfx_circle(lua_State* L);
    static int l_gfx_pixel(lua_State* L);
    static int l_gfx_text(lua_State* L);
    static int l_gfx_textWidth(lua_State* L);
    static int l_gfx_textHeight(lua_State* L);
    static int l_gfx_wrap(lua_State* L);
    static int l_gfx_clip(lua_State* L);
    static int l_gfx_image(lua_State* L);
    static int l_gfx_draw(lua_State* L);
    static int l_ui_title(lua_State* L);
    static int l_ui_big(lua_State* L);
    static int l_ui_label(lua_State* L);
    static int l_ui_chart(lua_State* L);
    static int l_http_get(lua_State* L);
    static int l_http_post(lua_State* L);
    static int l_json_decode(lua_State* L);
    static int l_json_encode(lua_State* L);
    static int l_led_set(lua_State* L);
    static int l_led_off(lua_State* L);
    static int l_store_get(lua_State* L);
    static int l_store_set(lua_State* L);
    static int l_time_ms(lua_State* L);
    static int l_time_now(lua_State* L);
    static int l_input_held(lua_State* L);

    int startHttp(lua_State* L, const char* method, int bodyIndex, int optsIndex, int callbackIndex);
    Gfx& requireFrame(lua_State* L, const char* what);

    Gfx& metrics_;
    PluginServices& services_;
    Limits limits_;
    Memory memory_;

    lua_State* L_ = nullptr;
    std::string name_;
    std::string title_;
    std::string description_;
    std::string version_;
    std::string author_;
    std::string chunkName_;
    std::string error_;
    Settings settings_;
    std::vector<ConfigField> schema_;
    std::vector<CardKind> kinds_;
    std::vector<Card> cards_;

    bool failed_ = false;
    bool runLoad_ = true;     ///< False while inspecting
    bool multiCard_ = false;  ///< The plugin defines `cards`
    bool hasLoad_ = false;    ///< It has a global load()
    Gfx* drawing_ = nullptr;  ///< The surface of the card being drawn, during draw()

    uint32_t refreshMs_ = 0;
    uint32_t frameIntervalMs_ = 33;
    uint32_t lastLoadMs_ = 0;
    bool loadDue_ = false;

    // Time budget for the call in progress.
    uint32_t callStartMs_ = 0;
    uint32_t callBudgetMs_ = 0;
    bool killed_ = false;

    size_t memUsed_ = 0;
    int nextRequestId_ = 1;
    struct PendingHttp {
        int callbackRef;  ///< Registry reference to the callback
        int filterRef;    ///< Registry reference to the `only` filter, or LUA_NOREF
    };
    std::map<int, PendingHttp> pendingHttp_;  ///< By request id
    std::vector<UiItem> ui_;
    std::vector<Image> images_;
    size_t imageBytes_ = 0;
};

}  // namespace deskhog
