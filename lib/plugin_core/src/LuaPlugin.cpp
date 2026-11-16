#include "LuaPlugin.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <utility>

#include "LuaJson.h"
#include "lua.hpp"

// A note on error handling. Lua raises errors with longjmp, which skips C++
// destructors. So a function here never holds a std::string or other owning
// object across a Lua call that can raise. It does its argument checks first;
// where it then needs both an owning object and such a call, the call goes
// through lua_pcall (see pushStringProtected and friends), so an error comes
// back as a status and is re-raised once the object is gone. Every entry from
// the host goes through run(), which wraps the work in lua_pcall, so an error
// (including out of memory) can never escape into the firmware.

namespace deskhog {

namespace {

const uint32_t kBlack = 0x000000;
const uint32_t kWhite = 0xFFFFFF;
const uint32_t kGray = 0xAAAAAA;
const uint32_t kRed = 0xE74C3C;
const uint32_t kChartBlue = 0x3498DB;
const int kPad = 8;

struct NamedColor {
    const char* name;
    uint32_t rgb;
};

const NamedColor kNamedColors[] = {
    {"white", 0xFFFFFF}, {"black", 0x000000}, {"red", 0xE74C3C},    {"green", 0x2ECC71},
    {"blue", 0x3498DB},  {"yellow", 0xF1C40F}, {"orange", 0xE67E22}, {"purple", 0x9B59B6},
    {"gray", 0x888888},  {"grey", 0x888888},
};

const char* const kFontNames[] = {"small", "medium", "large", "loud", nullptr};
const char* const kAlignNames[] = {"left", "center", "right", nullptr};
const char* const kButtonNames[] = {"down", "center", "up", nullptr};

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parseColorText(const char* text, uint32_t& out) {
    if (text[0] == '#') {
        const size_t len = strlen(text + 1);
        if (len != 3 && len != 6) return false;
        uint32_t value = 0;
        for (size_t i = 0; i < len; i++) {
            const int d = hexValue(text[1 + i]);
            if (d < 0) return false;
            // "#abc" is shorthand for "#aabbcc".
            value = (len == 3) ? ((value << 8) | (uint32_t)(d * 17)) : ((value << 4) | (uint32_t)d);
        }
        out = value;
        return true;
    }
    for (const NamedColor& c : kNamedColors) {
        if (strcmp(c.name, text) == 0) {
            out = c.rgb;
            return true;
        }
    }
    return false;
}

uint32_t optColor(lua_State* L, int index, uint32_t fallback) {
    const int type = lua_type(L, index);
    if (type == LUA_TNONE || type == LUA_TNIL) return fallback;
    if (type == LUA_TNUMBER) return (uint32_t)luaL_checkinteger(L, index) & 0xFFFFFF;
    if (type == LUA_TSTRING) {
        uint32_t rgb = 0;
        if (parseColorText(lua_tostring(L, index), rgb)) return rgb;
        return (uint32_t)luaL_argerror(L, index, "not a color (use \"#rrggbb\", 0xRRGGBB or a name like \"red\")");
    }
    return (uint32_t)luaL_argerror(L, index, "color expected");
}

int checkPx(lua_State* L, int index) {
    float v = (float)luaL_checknumber(L, index);
    if (!(v > -10000.0f)) v = -10000.0f;  // also catches NaN
    if (v > 10000.0f) v = 10000.0f;
    return (int)floorf(v + 0.5f);
}

int optPx(lua_State* L, int index, int fallback) {
    return lua_isnoneornil(L, index) ? fallback : checkPx(L, index);
}

Font optFont(lua_State* L, int index) { return (Font)luaL_checkoption(L, index, "small", kFontNames); }

Align optAlign(lua_State* L, int index) { return (Align)luaL_checkoption(L, index, "left", kAlignNames); }

struct TextArgs {
    const char* data;
    size_t length;
    bool ok;
};

int pushStringBody(lua_State* L) {
    const TextArgs* args = static_cast<const TextArgs*>(lua_touserdata(L, 1));
    lua_pushlstring(L, args->data, args->length);
    return 1;
}

/**
 * Push a string without letting a Lua error (out of memory) leave this frame.
 * @return the lua_pcall status. On failure the error value is on the stack.
 */
int pushStringProtected(lua_State* L, const char* data, size_t length) {
    TextArgs args{data, length, false};
    lua_pushcfunction(L, pushStringBody);
    lua_pushlightuserdata(L, &args);
    return lua_pcall(L, 1, 1, 0);
}

int decodeJsonBody(lua_State* L) {
    TextArgs* args = static_cast<TextArgs*>(lua_touserdata(L, 1));
    const char* error = nullptr;
    args->ok = jsonDecode(L, args->data, args->length, &error);
    return args->ok ? 1 : 0;
}

/**
 * Decode JSON without letting a Lua error leave this frame.
 * @return the lua_pcall status. On LUA_OK one value is on the stack: the
 *         decoded value if *decoded is true, otherwise nil.
 */
int decodeJsonProtected(lua_State* L, const char* data, size_t length, bool* decoded) {
    TextArgs args{data, length, false};
    lua_pushcfunction(L, decodeJsonBody);
    lua_pushlightuserdata(L, &args);
    const int status = lua_pcall(L, 1, 1, 0);
    *decoded = status == LUA_OK && args.ok;
    return status;
}

// With an `only` filter the plugin keeps a small part of the answer, so the
// answer itself may be much larger than usual.
const size_t kFilteredMaxBytes = 256 * 1024;

struct HttpBuild {
    HttpRequest* request;
    int callbackRef;
    int filterRef;
    const char* problem;
};

// Fill in an HttpRequest from Lua values. Runs under lua_pcall, with the
// arguments: the HttpBuild, the body (string, table or nil), the options table
// (or nil) and the callback.
int buildHttpRequest(lua_State* L) {
    HttpBuild* build = static_cast<HttpBuild*>(lua_touserdata(L, 1));
    HttpRequest& request = *build->request;
    bool hasContentType = false;

    if (lua_istable(L, 3)) {
        if (lua_getfield(L, 3, "timeout") == LUA_TNUMBER) {
            double seconds = (double)lua_tonumber(L, -1);
            if (seconds < 1) seconds = 1;
            if (seconds > 30) seconds = 30;
            request.timeoutMs = (uint32_t)(seconds * 1000.0);
        }
        lua_pop(L, 1);
        if (lua_getfield(L, 3, "only") == LUA_TTABLE) {
            build->filterRef = luaL_ref(L, LUA_REGISTRYINDEX);  // pops it
            request.maxBytes = kFilteredMaxBytes;
        } else {
            lua_pop(L, 1);
        }
        if (lua_getfield(L, 3, "headers") == LUA_TTABLE) {
            const int headers = lua_gettop(L);
            lua_pushnil(L);
            while (lua_next(L, headers) != 0) {
                // Only string keys and values; anything else is skipped.
                if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TSTRING && request.headers.size() < 16) {
                    const char* headerName = lua_tostring(L, -2);
                    const char* headerValue = lua_tostring(L, -1);
                    if (strcasecmp(headerName, "content-type") == 0) hasContentType = true;
                    request.headers.emplace_back(headerName, headerValue);
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }

    if (lua_type(L, 2) == LUA_TSTRING) {
        size_t length = 0;
        const char* body = lua_tolstring(L, 2, &length);
        request.body.assign(body, length);
    } else if (lua_type(L, 2) == LUA_TTABLE) {
        if (!jsonEncode(L, 2, request.body, &build->problem)) return 0;
        if (!hasContentType) request.headers.emplace_back("Content-Type", "application/json");
    }

    // Last, so nothing above can fail and leave the callback referenced.
    lua_pushvalue(L, 4);
    build->callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

// Break text into lines no wider than maxWidth, at spaces where it can,
// measured with the real font. A newline in the text always starts a new line.
std::vector<std::string> wrapText(Gfx& gfx, const std::string& text, Font font, int maxWidth) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size() && lines.size() < 200) {
        size_t end = start;
        size_t lastSpace = std::string::npos;
        while (end < text.size() && text[end] != '\n') {
            // Never measure half a character: step over a whole UTF-8 sequence.
            size_t next = end + 1;
            while (next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80) next++;
            if (gfx.textWidth(text.substr(start, next - start).c_str(), font) > maxWidth) break;
            if (text[end] == ' ') lastSpace = end;
            end = next;
        }
        size_t next = end;
        if (end < text.size() && text[end] == '\n') {
            next = end + 1;
        } else if (end < text.size()) {
            if (lastSpace != std::string::npos && lastSpace > start) {
                end = lastSpace;
                next = lastSpace + 1;
            } else if (end == start) {
                // A single character wider than the line: emit it anyway.
                end = start + 1;
                while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) end++;
                next = end;
            }
        }
        lines.push_back(text.substr(start, end - start));
        start = next;
    }
    return lines;
}

void setFunctions(lua_State* L, const char* global, const luaL_Reg* functions) {
    lua_newtable(L);
    luaL_setfuncs(L, functions, 0);
    lua_setglobal(L, global);
}

}  // namespace

// ---------------------------------------------------------------- lifecycle

const char* const LuaPlugin::kMainKind = "main";

LuaPlugin::LuaPlugin(Gfx& metrics, PluginServices& services) : LuaPlugin(metrics, services, Limits(), Memory()) {}

LuaPlugin::LuaPlugin(Gfx& metrics, PluginServices& services, const Limits& limits, const Memory& memory)
    : metrics_(metrics), services_(services), limits_(limits), memory_(memory) {
    if (!memory_.realloc || !memory_.free) {
        memory_.realloc = ::realloc;
        memory_.free = ::free;
    }
}

LuaPlugin::~LuaPlugin() { close(); }

void LuaPlugin::close() {
    if (L_) {
        // Closing can run plugin code (__close handlers). With the hook firing
        // on every instruction it cannot stall the shutdown.
        killed_ = true;
        lua_sethook(L_, hook, LUA_MASKCOUNT, 1);
        lua_close(L_);
        L_ = nullptr;
    }
    pendingHttp_.clear();
    drawing_ = nullptr;
    for (Card& card : cards_) card.ref = LUA_NOREF;
    // Images belong to the program that made them.
    std::vector<Image>().swap(images_);
    imageBytes_ = 0;
}

LuaPlugin* LuaPlugin::from(lua_State* L) { return *static_cast<LuaPlugin**>(lua_getextraspace(L)); }

void* LuaPlugin::alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    LuaPlugin* self = static_cast<LuaPlugin*>(ud);
    const size_t oldSize = ptr ? osize : 0;  // for new blocks osize is a type tag, not a size
    if (nsize == 0) {
        if (ptr) {
            self->memUsed_ -= oldSize;
            self->memory_.free(ptr);
        }
        return nullptr;
    }
    if (nsize > oldSize && self->memUsed_ + (nsize - oldSize) > self->limits_.memoryBytes) {
        return nullptr;  // Lua turns this into a "not enough memory" error
    }
    void* block = self->memory_.realloc(ptr, nsize);
    if (block) self->memUsed_ = self->memUsed_ - oldSize + nsize;
    return block;
}

void LuaPlugin::hook(lua_State* L, lua_Debug*) {
    LuaPlugin* self = from(L);
    if (!self->killed_ && self->services_.millis() - self->callStartMs_ <= self->callBudgetMs_) return;
    if (!self->killed_) {
        self->killed_ = true;
        // Fire on every instruction from now on, so a plugin that catches the
        // error with pcall still gets stopped right after.
        lua_sethook(L, hook, LUA_MASKCOUNT, 1);
        if (self->L_ && self->L_ != L) lua_sethook(self->L_, hook, LUA_MASKCOUNT, 1);
    }
    luaL_error(L, "took too long (limit %d ms)", (int)self->callBudgetMs_);
}

void LuaPlugin::fail(const std::string& message) {
    failed_ = true;
    error_ = message;
    markAllDirty();  // every card of this plugin now shows the error
    services_.log("error: " + message);
    close();
}

void LuaPlugin::markAllDirty() {
    for (Card& card : cards_) card.dirty = true;
}

LuaPlugin::Card* LuaPlugin::findCard(const std::string& id) {
    for (Card& card : cards_) {
        if (card.id == id) return &card;
    }
    return nullptr;
}

const LuaPlugin::Card* LuaPlugin::findCard(const std::string& id) const {
    for (const Card& card : cards_) {
        if (card.id == id) return &card;
    }
    return nullptr;
}

bool LuaPlugin::run(Call& call, uint32_t budgetMs) {
    if (!L_ || failed_) return false;

    killed_ = false;
    callBudgetMs_ = budgetMs;
    callStartMs_ = services_.millis();
    lua_sethook(L_, hook, LUA_MASKCOUNT, 1000);

    lua_settop(L_, 0);
    lua_pushcfunction(L_, trampoline);
    lua_pushlightuserdata(L_, &call);
    const int status = lua_pcall(L_, 1, 0, 0);
    if (status == LUA_OK) return true;

    std::string message;
    if (status == LUA_ERRMEM) {
        char buf[64];
        snprintf(buf, sizeof(buf), "out of memory (limit %d KB)", (int)(limits_.memoryBytes / 1024));
        message = buf;
    } else {
        const char* text = lua_tostring(L_, -1);
        message = text ? text : "error (not a string)";
    }
    lua_settop(L_, 0);
    fail(message);
    return false;
}

// Leaves the function and, for plugins with card kinds, the card table on the
// stack. Single-card plugins use plain global functions that take no card.
int LuaPlugin::pushCardFunction(lua_State* L, const Card& card, const char* function) {
    if (card.ref == LUA_NOREF) {
        if (lua_getglobal(L, function) == LUA_TFUNCTION) return 0;
        lua_pop(L, 1);
        return -1;
    }
    if (lua_getglobal(L, "cards") != LUA_TTABLE) {
        lua_pop(L, 1);
        return -1;
    }
    lua_pushstring(L, card.kind.c_str());
    if (lua_rawget(L, -2) != LUA_TTABLE) {
        lua_pop(L, 2);
        return -1;
    }
    lua_pushstring(L, function);
    if (lua_rawget(L, -2) != LUA_TFUNCTION) {
        lua_pop(L, 3);
        return -1;
    }
    lua_remove(L, -2);  // the kind table
    lua_remove(L, -2);  // the cards table
    lua_rawgeti(L, LUA_REGISTRYINDEX, card.ref);
    return 1;
}

int LuaPlugin::trampoline(lua_State* L) {
    Call* call = static_cast<Call*>(lua_touserdata(L, 1));
    LuaPlugin* self = call->self;
    switch (call->kind) {
        case CallKind::Start:
            self->doStart(L, *call);
            break;
        case CallKind::Load:
            if (lua_getglobal(L, "load") == LUA_TFUNCTION) lua_call(L, 0, 0);
            break;
        case CallKind::AddCard:
            self->doAddCard(L, *call);
            break;
        case CallKind::CardLoad: {
            const int args = self->pushCardFunction(L, *call->card, "load");
            if (args >= 0) lua_call(L, args, 0);
            break;
        }
        case CallKind::CardUpdate: {
            const int args = self->pushCardFunction(L, *call->card, "update");
            if (args >= 0) {
                lua_pushnumber(L, (lua_Number)call->dt);
                lua_call(L, args + 1, 1);
                // update() may return false to say nothing it draws has changed.
                call->result = !(lua_isboolean(L, -1) && !lua_toboolean(L, -1));
            }
            break;
        }
        case CallKind::CardDraw: {
            const int args = self->pushCardFunction(L, *call->card, "draw");
            if (args >= 0) lua_call(L, args, 0);
            break;
        }
        case CallKind::CardButton: {
            const int args = self->pushCardFunction(L, *call->card, "button");
            if (args >= 0) {
                lua_pushstring(L, call->buttonName);
                lua_call(L, args + 1, 1);
                call->result = lua_toboolean(L, -1) != 0;
            }
            break;
        }
        case CallKind::CardTitle:
            if (call->card->ref != LUA_NOREF) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, call->card->ref);
                lua_pushliteral(L, "title");
                if (lua_rawget(L, -2) == LUA_TSTRING) *call->text = lua_tostring(L, -1);
            }
            break;
        case CallKind::Http:
            self->doHttp(L, *call);
            break;
    }
    return 0;
}

bool LuaPlugin::start(const std::string& name, const char* source, size_t length, const Settings& settings) {
    return begin(name, source, length, settings, true);
}

bool LuaPlugin::inspect(const std::string& name, const char* source, size_t length) {
    return begin(name, source, length, Settings(), false);
}

bool LuaPlugin::begin(const std::string& name, const char* source, size_t length, const Settings& settings,
                      bool runLoad) {
    close();
    cards_.clear();
    name_ = name;
    title_ = name;
    description_.clear();
    version_.clear();
    author_.clear();
    chunkName_ = "=" + name;
    settings_ = settings;
    schema_.clear();
    kinds_.clear();
    ui_.clear();
    error_.clear();
    failed_ = false;
    runLoad_ = runLoad;
    multiCard_ = false;
    hasLoad_ = false;
    memUsed_ = 0;
    refreshMs_ = 0;
    loadDue_ = false;
    frameIntervalMs_ = 33;

    L_ = lua_newstate(alloc, this);
    if (!L_) {
        failed_ = true;
        error_ = "not enough memory to start";
        services_.log("error: " + error_);
        return false;
    }
    *static_cast<LuaPlugin**>(lua_getextraspace(L_)) = this;

    Call startCall{this, CallKind::Start};
    startCall.source = source;
    startCall.length = length;
    if (!run(startCall, limits_.loadBudgetMs)) return false;

    lastLoadMs_ = services_.millis();
    if (runLoad && hasLoad_) {
        Call loadCall{this, CallKind::Load};
        if (!run(loadCall, limits_.loadBudgetMs)) return false;
    }
    return true;
}

void LuaPlugin::doStart(lua_State* L, Call& call) {
    openSandbox(L);
    if (luaL_loadbufferx(L, call.source, call.length, chunkName_.c_str(), "t") != LUA_OK) lua_error(L);
    lua_call(L, 0, 0);
    readPluginTable(L);

    // The shared settings. Each entry of `config` is replaced by its value, so
    // the plugin just reads `config.city`.
    if (lua_getglobal(L, "config") == LUA_TTABLE) {
        const int table = lua_gettop(L);
        resolveSpec(L, table, &settings_, &schema_, table);
    }
    lua_pop(L, 1);

    readKinds(L);
    hasLoad_ = lua_getglobal(L, "load") == LUA_TFUNCTION;
    lua_pop(L, 1);
}

void LuaPlugin::openSandbox(lua_State* L) {
    static const luaL_Reg libs[] = {
        {LUA_GNAME, luaopen_base},      {LUA_TABLIBNAME, luaopen_table}, {LUA_STRLIBNAME, luaopen_string},
        {LUA_MATHLIBNAME, luaopen_math}, {LUA_COLIBNAME, luaopen_coroutine}, {LUA_UTF8LIBNAME, luaopen_utf8},
        {nullptr, nullptr},
    };
    for (const luaL_Reg* lib = libs; lib->func; lib++) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }

    // Plugins get one file of source text and nothing else: no loading code at
    // runtime, no bytecode.
    static const char* const removed[] = {"dofile", "loadfile", "load", "require"};
    for (const char* name : removed) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
    lua_getglobal(L, LUA_STRLIBNAME);
    lua_pushnil(L);
    lua_setfield(L, -2, "dump");
    lua_pop(L, 1);

    // Lua runs __gc finalizers with the time-budget hook switched off, so one
    // that loops would freeze the device. Plugins cannot register them.
    lua_getglobal(L, "setmetatable");
    lua_pushcclosure(L, l_setmetatable, 1);
    lua_setglobal(L, "setmetatable");

    lua_pushcfunction(L, l_print);
    lua_setglobal(L, "print");

    static const luaL_Reg gfx[] = {
        {"clear", l_gfx_clear},   {"rect", l_gfx_rect},   {"outline", l_gfx_outline},
        {"line", l_gfx_line},     {"circle", l_gfx_circle}, {"pixel", l_gfx_pixel},
        {"text", l_gfx_text},     {"textWidth", l_gfx_textWidth}, {"textHeight", l_gfx_textHeight},
        {"wrap", l_gfx_wrap},     {"clip", l_gfx_clip},   {"image", l_gfx_image},
        {"draw", l_gfx_draw},
        {nullptr, nullptr},
    };
    setFunctions(L, "gfx", gfx);
    lua_getglobal(L, "gfx");
    lua_pushinteger(L, metrics_.width());
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, metrics_.height());
    lua_setfield(L, -2, "height");
    lua_pop(L, 1);

    static const luaL_Reg ui[] = {
        {"title", l_ui_title}, {"big", l_ui_big}, {"label", l_ui_label}, {"chart", l_ui_chart}, {nullptr, nullptr},
    };
    setFunctions(L, "ui", ui);

    static const luaL_Reg http[] = {{"get", l_http_get}, {"post", l_http_post}, {nullptr, nullptr}};
    setFunctions(L, "http", http);

    static const luaL_Reg json[] = {{"decode", l_json_decode}, {"encode", l_json_encode}, {nullptr, nullptr}};
    setFunctions(L, "json", json);
    lua_getglobal(L, "json");
    jsonPushNull(L);
    lua_setfield(L, -2, "null");
    lua_pop(L, 1);

    static const luaL_Reg led[] = {{"set", l_led_set}, {"off", l_led_off}, {nullptr, nullptr}};
    setFunctions(L, "led", led);

    static const luaL_Reg store[] = {{"get", l_store_get}, {"set", l_store_set}, {nullptr, nullptr}};
    setFunctions(L, "store", store);

    static const luaL_Reg time[] = {{"ms", l_time_ms}, {"now", l_time_now}, {nullptr, nullptr}};
    setFunctions(L, "time", time);

    static const luaL_Reg input[] = {{"held", l_input_held}, {nullptr, nullptr}};
    setFunctions(L, "input", input);
}

void LuaPlugin::readPluginTable(lua_State* L) {
    if (lua_getglobal(L, "plugin") != LUA_TTABLE) {
        lua_pop(L, 1);
        return;
    }
    if (lua_getfield(L, -1, "name") == LUA_TSTRING) title_ = lua_tostring(L, -1);
    lua_pop(L, 1);

    // For plugin directories: a line about what it does, who wrote it, which version.
    const struct {
        const char* key;
        std::string* into;
        size_t most;
    } about[] = {{"description", &description_, 200}, {"version", &version_, 24}, {"author", &author_, 60}};
    for (const auto& field : about) {
        // A version may be written as a number: version = 1.2
        const int type = lua_getfield(L, -1, field.key);
        if (type == LUA_TSTRING || type == LUA_TNUMBER) {
            size_t length = 0;
            const char* text = lua_tolstring(L, -1, &length);
            field.into->assign(text, length < field.most ? length : field.most);
        }
        lua_pop(L, 1);
    }

    if (lua_getfield(L, -1, "refresh") == LUA_TNUMBER) {
        const double seconds = (double)lua_tonumber(L, -1);
        // Below one second the device would do little else.
        if (seconds > 0) refreshMs_ = (uint32_t)(fmax(seconds, 1.0) * 1000.0);
    }
    lua_pop(L, 1);

    if (lua_getfield(L, -1, "fps") == LUA_TNUMBER) {
        double fps = (double)lua_tonumber(L, -1);
        if (fps < 1) fps = 1;
        if (fps > 60) fps = 60;
        frameIntervalMs_ = (uint32_t)(1000.0 / fps);
    }
    lua_pop(L, 2);
}

// The card kinds a plugin offers: the entries of its global `cards` table. A
// plugin without one has a single kind, "main", made of its global functions.
void LuaPlugin::readKinds(lua_State* L) {
    kinds_.clear();
    if (lua_getglobal(L, "cards") == LUA_TTABLE) {
        const int table = lua_gettop(L);
        lua_pushnil(L);
        while (lua_next(L, table) != 0) {
            if (lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1)) {
                const int kindTable = lua_gettop(L);
                CardKind kind;
                kind.key = lua_tostring(L, -2);
                if (!isValidPluginName(kind.key)) {
                    luaL_error(L, "card kind '%s': names are 1-32 characters of a-z, 0-9, - and _", lua_tostring(L, -2));
                }
                kind.name = kind.key;
                lua_pushliteral(L, "name");
                if (lua_rawget(L, kindTable) == LUA_TSTRING) kind.name = lua_tostring(L, -1);
                lua_pop(L, 1);
                lua_pushliteral(L, "config");
                if (lua_rawget(L, kindTable) == LUA_TTABLE) resolveSpec(L, lua_gettop(L), nullptr, &kind.config, 0);
                lua_pop(L, 1);
                kinds_.push_back(kind);
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    multiCard_ = !kinds_.empty();
    if (!multiCard_) {
        CardKind main;
        main.key = kMainKind;
        main.name = title_;
        kinds_.push_back(main);
        return;
    }
    // Lua does not keep table order. Sort, so the list looks the same every time.
    for (size_t i = 1; i < kinds_.size(); i++) {
        for (size_t j = i; j > 0 && kinds_[j].key < kinds_[j - 1].key; j--) std::swap(kinds_[j], kinds_[j - 1]);
    }
}

// Reads a settings table such as
//   { city = { label = "City", default = "Antwerp" }, days = 3 }
// (the second form is short for "just a default").
//  - With `schema`, describes each entry.
//  - With `target` (a stack index), stores each entry's value there: the saved
//    setting if `settings` has one, otherwise the default. `target` may be the
//    table itself, which replaces every description by its value.
void LuaPlugin::resolveSpec(lua_State* L, int spec, const Settings* settings, std::vector<ConfigField>* schema,
                            int target) {
    spec = lua_absindex(L, spec);
    if (target != 0) target = lua_absindex(L, target);
    lua_pushnil(L);
    while (lua_next(L, spec) != 0) {
        const int key = lua_gettop(L) - 1;
        const int value = key + 1;
        if (lua_type(L, key) == LUA_TSTRING) {
            ConfigField field;
            field.key = lua_tostring(L, key);
            field.label = field.key;
            if (lua_istable(L, value)) {
                lua_pushliteral(L, "label");
                if (lua_rawget(L, value) == LUA_TSTRING) field.label = lua_tostring(L, -1);
                lua_pop(L, 1);
                lua_pushliteral(L, "secret");
                lua_rawget(L, value);
                field.secret = lua_toboolean(L, -1) != 0;
                lua_pop(L, 1);
                lua_pushliteral(L, "order");
                if (lua_rawget(L, value) == LUA_TNUMBER) field.order = (int)lua_tointeger(L, -1);
                lua_pop(L, 1);
                lua_pushliteral(L, "options");
                if (lua_rawget(L, value) == LUA_TTABLE) {
                    const lua_Unsigned count = lua_rawlen(L, -1);
                    for (lua_Unsigned i = 1; i <= count && i <= 24; i++) {
                        if (lua_rawgeti(L, -1, (lua_Integer)i) == LUA_TSTRING) field.options.push_back(lua_tostring(L, -1));
                        lua_pop(L, 1);
                    }
                }
                lua_pop(L, 1);
                lua_pushliteral(L, "default");
                if (lua_rawget(L, value) == LUA_TNIL) {
                    // No default means an empty text field, not a missing one.
                    lua_pop(L, 1);
                    lua_pushliteral(L, "");
                }
            } else {
                lua_pushvalue(L, value);
            }
            // The default is now on top of the stack.
            const int defaultType = lua_type(L, -1);
            field.type = defaultType == LUA_TNUMBER ? "number" : defaultType == LUA_TBOOLEAN ? "boolean" : "string";
            if (defaultType == LUA_TBOOLEAN) {
                field.defaultValue = lua_toboolean(L, -1) ? "true" : "false";
            } else if (defaultType == LUA_TNUMBER || defaultType == LUA_TSTRING) {
                lua_pushvalue(L, -1);
                field.defaultValue = lua_tostring(L, -1);
                lua_pop(L, 1);
            }

            if (settings) {
                const auto saved = settings->find(field.key);
                if (saved != settings->end()) {
                    const std::string& text = saved->second;
                    if (defaultType == LUA_TNUMBER) {
                        // Keep the default when the saved text is not a number.
                        if (lua_stringtonumber(L, text.c_str()) != 0) lua_remove(L, -2);
                    } else if (defaultType == LUA_TBOOLEAN) {
                        lua_pop(L, 1);
                        lua_pushboolean(L, text == "true" || text == "1" || text == "on" || text == "yes");
                    } else {
                        lua_pop(L, 1);
                        lua_pushlstring(L, text.data(), text.size());
                    }
                }
            }
            if (schema) schema->push_back(field);

            if (target != 0) {
                lua_pushvalue(L, key);
                lua_insert(L, -2);
                lua_rawset(L, target);
            }
        }
        lua_settop(L, key);  // keep the key for lua_next
    }

    // Lua tables have no order of their own. Fields go by their `order` if the
    // plugin gave one, then by name, so a form looks the same every time.
    if (schema) {
        for (size_t i = 1; i < schema->size(); i++) {
            for (size_t j = i; j > 0; j--) {
                const ConfigField& a = (*schema)[j - 1];
                const ConfigField& b = (*schema)[j];
                if (a.order < b.order || (a.order == b.order && a.key <= b.key)) break;
                std::swap((*schema)[j - 1], (*schema)[j]);
            }
        }
    }
}

// -------------------------------------------------------------------- cards

bool LuaPlugin::addCard(const std::string& id, const std::string& kind, const Settings& settings, Gfx& surface) {
    removeCard(id);
    if (!failed_) {
        bool known = false;
        for (const CardKind& candidate : kinds_) known = known || candidate.key == kind;
        if (!known) return false;
    }

    Card card;
    card.id = id;
    card.kind = kind;
    card.ref = LUA_NOREF;
    card.surface = &surface;
    cards_.push_back(card);
    if (failed_ || !L_) return true;  // the card exists to show the error

    Call call{this, CallKind::AddCard};
    call.card = &cards_.back();
    call.settings = &settings;
    run(call, limits_.loadBudgetMs);
    return true;
}

void LuaPlugin::doAddCard(lua_State* L, Call& call) {
    Card& card = *call.card;
    if (!multiCard_) {
        // The global load() already ran when the plugin started.
        card.hasUpdate = lua_getglobal(L, "update") == LUA_TFUNCTION;
        card.hasDraw = lua_getglobal(L, "draw") == LUA_TFUNCTION;
        card.hasButton = lua_getglobal(L, "button") == LUA_TFUNCTION;
        lua_pop(L, 3);
        return;
    }

    if (lua_getglobal(L, "cards") != LUA_TTABLE) luaL_error(L, "the plugin's `cards` table is gone");
    lua_pushstring(L, card.kind.c_str());
    if (lua_rawget(L, -2) != LUA_TTABLE) luaL_error(L, "the plugin has no card kind '%s'", card.kind.c_str());
    const int kindTable = lua_gettop(L);

    // The table the plugin gets as `card`: { id, kind, config = {...} }. The
    // plugin is free to keep its own state in it.
    lua_createtable(L, 0, 4);
    const int cardTable = lua_gettop(L);
    lua_pushstring(L, card.id.c_str());
    lua_setfield(L, cardTable, "id");
    lua_pushstring(L, card.kind.c_str());
    lua_setfield(L, cardTable, "kind");
    lua_createtable(L, 0, 2);
    const int configTable = lua_gettop(L);
    lua_pushliteral(L, "config");
    if (lua_rawget(L, kindTable) == LUA_TTABLE) resolveSpec(L, lua_gettop(L), call.settings, nullptr, configTable);
    lua_pop(L, 1);
    lua_setfield(L, cardTable, "config");

    lua_pushvalue(L, cardTable);
    card.ref = luaL_ref(L, LUA_REGISTRYINDEX);

    static const char* const names[] = {"load", "update", "draw", "button"};
    bool* const flags[] = {&card.hasLoad, &card.hasUpdate, &card.hasDraw, &card.hasButton};
    for (int i = 0; i < 4; i++) {
        lua_pushstring(L, names[i]);
        *flags[i] = lua_rawget(L, kindTable) == LUA_TFUNCTION;
        lua_pop(L, 1);
    }

    if (card.hasLoad && runLoad_) {
        lua_pushliteral(L, "load");
        lua_rawget(L, kindTable);
        lua_pushvalue(L, cardTable);
        lua_call(L, 1, 0);
    }
}

void LuaPlugin::detachCard(const std::string& id) {
    Card* card = findCard(id);
    if (!card) return;
    card->surface = nullptr;
    card->visible = false;
    card->frameClockValid = false;
}

bool LuaPlugin::attachCard(const std::string& id, Gfx& surface) {
    Card* card = findCard(id);
    if (!card) return false;
    card->surface = &surface;
    card->dirty = true;
    return true;
}

void LuaPlugin::removeCard(const std::string& id) {
    for (auto it = cards_.begin(); it != cards_.end(); ++it) {
        if (it->id != id) continue;
        if (L_ && it->ref != LUA_NOREF) luaL_unref(L_, LUA_REGISTRYINDEX, it->ref);
        cards_.erase(it);
        return;
    }
}

void LuaPlugin::setVisible(const std::string& id, bool visible) {
    Card* card = findCard(id);
    if (card) card->visible = visible && card->surface;
}

bool LuaPlugin::needsDraw(const std::string& id) const {
    const Card* card = findCard(id);
    return card && card->surface && card->dirty;
}

std::string LuaPlugin::cardTitle(const std::string& id) {
    std::string title;
    Card* card = findCard(id);
    if (!card || failed_ || !L_ || card->ref == LUA_NOREF) return title;
    Call call{this, CallKind::CardTitle};
    call.card = card;
    call.text = &title;
    run(call, limits_.callBudgetMs);
    return title;
}

// ------------------------------------------------------------------ runtime

void LuaPlugin::tick(uint32_t nowMs) {
    if (failed_ || !L_) return;

    const bool refreshDue = refreshMs_ != 0 && nowMs - lastLoadMs_ >= refreshMs_;
    if (refreshDue || loadDue_) {
        loadDue_ = false;
        lastLoadMs_ = nowMs;
        bool ran = false;
        if (hasLoad_) {
            Call call{this, CallKind::Load};
            if (!run(call, limits_.loadBudgetMs)) return;
            ran = true;
        }
        for (size_t i = 0; i < cards_.size(); i++) {
            if (!cards_[i].hasLoad) continue;
            Call call{this, CallKind::CardLoad};
            call.card = &cards_[i];
            if (!run(call, limits_.loadBudgetMs)) return;
            ran = true;
        }
        if (ran) markAllDirty();
    }

    for (size_t i = 0; i < cards_.size(); i++) {
        Card& card = cards_[i];
        if (!card.visible || !card.hasUpdate) {
            card.frameClockValid = false;
            continue;
        }
        if (!card.frameClockValid) {
            card.frameClockValid = true;
            card.lastFrameMs = nowMs;
            continue;
        }
        const uint32_t elapsed = nowMs - card.lastFrameMs;
        if (elapsed < frameIntervalMs_) continue;
        card.lastFrameMs = nowMs;

        Call call{this, CallKind::CardUpdate};
        call.card = &card;
        // A long stall (card hidden, slow request) should not teleport the game.
        call.dt = fminf((float)elapsed / 1000.0f, 0.25f);
        if (!run(call, limits_.callBudgetMs)) return;
        if (call.result) card.dirty = true;
    }
}

void LuaPlugin::draw(const std::string& id) {
    Card* card = findCard(id);
    if (!card || !card->surface) return;
    Gfx& gfx = *card->surface;
    card->dirty = false;
    gfx.fillRect(0, 0, gfx.width(), gfx.height(), kBlack, 0);

    if (!failed_ && L_ && card->hasDraw) {
        ui_.clear();
        drawing_ = &gfx;
        Call call{this, CallKind::CardDraw};
        call.card = card;
        const bool ok = run(call, limits_.callBudgetMs);
        drawing_ = nullptr;
        if (ok) flushUi(gfx);
        ui_.clear();
    }

    if (failed_) {
        card->dirty = false;  // failing marked every card dirty; this one is being painted now
        gfx.fillRect(0, 0, gfx.width(), gfx.height(), kBlack, 0);
        drawError(gfx);
    }
}

bool LuaPlugin::button(const std::string& id, Button button) {
    Card* card = findCard(id);
    if (!card || failed_ || !L_ || !card->hasButton) return false;
    Call call{this, CallKind::CardButton};
    call.card = card;
    call.buttonName = kButtonNames[(int)button];
    if (!run(call, limits_.callBudgetMs)) return false;
    card->dirty = true;
    return call.result;
}

void LuaPlugin::httpResponse(int requestId, const HttpResponse& response) {
    if (failed_ || !L_) return;
    const auto pending = pendingHttp_.find(requestId);
    if (pending == pendingHttp_.end()) return;
    const PendingHttp refs = pending->second;
    pendingHttp_.erase(pending);

    Call call{this, CallKind::Http};
    call.requestId = refs.callbackRef;
    call.filterRef = refs.filterRef;
    call.response = &response;
    // The callback may have changed data any of the cards shows.
    if (run(call, limits_.callBudgetMs)) markAllDirty();
}

void LuaPlugin::doHttp(lua_State* L, Call& call) {
    const HttpResponse& response = *call.response;
    lua_rawgeti(L, LUA_REGISTRYINDEX, call.requestId);
    luaL_unref(L, LUA_REGISTRYINDEX, call.requestId);
    // The plugin asked to keep only part of the answer (the `only` option).
    const bool filtered = call.filterRef != LUA_NOREF;
    int filter = 0;
    if (filtered) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, call.filterRef);
        luaL_unref(L, LUA_REGISTRYINDEX, call.filterRef);
        filter = lua_gettop(L);
    }

    lua_createtable(L, 0, 5);
    lua_pushboolean(L, response.status >= 200 && response.status < 300);
    lua_setfield(L, -2, "ok");
    lua_pushinteger(L, response.status);
    lua_setfield(L, -2, "status");
    if (!filtered) {
        // A filtered answer can be far larger than the plugin's memory: it
        // gets res.json and no res.body.
        lua_pushlstring(L, response.body.data(), response.body.size());
        lua_setfield(L, -2, "body");
    }
    if (!response.error.empty()) {
        lua_pushlstring(L, response.error.data(), response.error.size());
        lua_setfield(L, -2, "error");
    }

    // Decode the body up front when it looks like JSON, so plugins can go
    // straight to res.json.
    size_t i = 0;
    while (i < response.body.size() && (unsigned char)response.body[i] <= ' ') i++;
    if (i < response.body.size() && (response.body[i] == '{' || response.body[i] == '[')) {
        const char* error = nullptr;
        if (jsonDecodeFiltered(L, response.body.data(), response.body.size(), filter, &error)) {
            lua_setfield(L, -2, "json");
        }
    }
    if (filtered) lua_remove(L, filter);
    lua_call(L, 1, 0);
}

// ------------------------------------------------------------- host drawing

void LuaPlugin::flushUi(Gfx& gfx) {
    if (ui_.empty()) return;
    const int width = gfx.width();
    const int height = gfx.height();
    int top = 6;
    int bottom = height - 6;

    for (const UiItem& item : ui_) {
        if (item.kind != UiItem::Title) continue;
        gfx.text(kPad, top, item.text.c_str(), item.color, Font::Small, Align::Left);
        top += gfx.textHeight(Font::Small);
        break;
    }

    for (const UiItem& item : ui_) {
        if (item.kind != UiItem::Chart) continue;
        const int chartHeight = 30;
        const int chartTop = bottom - chartHeight;
        const int chartWidth = width - 2 * kPad;
        const size_t count = item.values.size();
        bottom = chartTop - 4;
        if (count == 0) break;

        float low = item.values[0];
        float high = item.values[0];
        for (float v : item.values) {
            if (v < low) low = v;
            if (v > high) high = v;
        }
        const float range = high - low;
        int previousX = 0;
        int previousY = 0;
        for (size_t i = 0; i < count; i++) {
            const int x = kPad + (count > 1 ? (int)((long)i * (chartWidth - 1) / (long)(count - 1)) : chartWidth / 2);
            const float level = range > 0 ? (item.values[i] - low) / range : 0.5f;
            const int y = chartTop + (chartHeight - 1) - (int)(level * (float)(chartHeight - 1) + 0.5f);
            if (count == 1) {
                gfx.fillRect(x - 2, y - 2, 5, 5, item.color, 3);
            } else if (i > 0) {
                gfx.line(previousX, previousY, x, y, item.color, 2);
            }
            previousX = x;
            previousY = y;
        }
        break;
    }

    // Everything else is stacked and centred in the space that is left.
    struct Row {
        std::string text;
        uint32_t color;
        Font font;
        int height;
        int gap;  ///< Space above it
    };
    std::vector<Row> rows;
    int total = 0;
    // First try the large font for big values. If the rows then do not fit
    // between the title and the chart, lay them out again one size down.
    for (int attempt = 0; attempt < 2; attempt++) {
        rows.clear();
        total = 0;
        for (const UiItem& item : ui_) {
            if (item.kind != UiItem::Big && item.kind != UiItem::Label) continue;
            Font font = Font::Small;
            if (item.kind == UiItem::Big) {
                font = attempt == 0 ? Font::Large : Font::Medium;
                if (gfx.textWidth(item.text.c_str(), font) > width - 2 * kPad) font = Font::Medium;
            }
            const int rowHeight = gfx.textHeight(font);
            if (item.kind == UiItem::Label) {
                // A label that is too wide continues on the next line (four at most).
                const std::vector<std::string> lines = wrapText(gfx, item.text, font, width - 2 * kPad);
                for (size_t line = 0; line < lines.size() && line < 4; line++) {
                    const int gap = rows.empty() ? 0 : line == 0 ? 2 : 0;
                    total += gap + rowHeight;
                    rows.push_back({lines[line], item.color, font, rowHeight, gap});
                }
                continue;
            }
            const int gap = rows.empty() ? 0 : 2;
            total += gap + rowHeight;
            rows.push_back({item.text, item.color, font, rowHeight, gap});
        }
        if (total <= bottom - top) break;
    }
    int y = top + (bottom - top - total) / 2;
    if (y < top) y = top;
    for (const Row& row : rows) {
        y += row.gap;
        gfx.text(width / 2, y, row.text.c_str(), row.color, row.font, Align::Center);
        y += row.height;
    }
}

void LuaPlugin::drawError(Gfx& gfx) {
    const int width = gfx.width();
    const int height = gfx.height();
    const int lineHeight = gfx.textHeight(Font::Small);
    int y = kPad - 2;

    const std::string heading = title_ + " crashed";
    gfx.text(kPad, y, heading.c_str(), kRed, Font::Small, Align::Left);
    y += lineHeight + 4;

    for (const std::string& lineText : wrapText(gfx, error_.substr(0, 240), Font::Small, width - 2 * kPad)) {
        if (y + lineHeight > height) break;
        gfx.text(kPad, y, lineText.c_str(), 0xCCCCCC, Font::Small, Align::Left);
        y += lineHeight;
    }
}

// ------------------------------------------------------------ Lua-facing API

int LuaPlugin::l_setmetatable(lua_State* L) {
    if (lua_istable(L, 2)) {
        lua_pushstring(L, "__gc");
        const bool hasFinalizer = lua_rawget(L, 2) != LUA_TNIL;
        lua_pop(L, 1);
        if (hasFinalizer) return luaL_error(L, "__gc finalizers are not available to plugins");
    }
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    return lua_gettop(L);
}

int LuaPlugin::l_print(lua_State* L) {
    LuaPlugin* self = from(L);
    const int count = lua_gettop(L);
    luaL_Buffer buffer;
    luaL_buffinit(L, &buffer);
    for (int i = 1; i <= count; i++) {
        if (i > 1) luaL_addchar(&buffer, ' ');
        luaL_tolstring(L, i, nullptr);
        luaL_addvalue(&buffer);
    }
    luaL_pushresult(&buffer);
    size_t length = 0;
    const char* text = lua_tolstring(L, -1, &length);
    if (length > 240) length = 240;
    self->services_.log(std::string(text, length));
    return 0;
}

Gfx& LuaPlugin::requireFrame(lua_State* L, const char* what) {
    if (!drawing_) luaL_error(L, "%s() only works inside draw()", what);
    return *drawing_;
}

int LuaPlugin::l_gfx_clear(lua_State* L) {
    Gfx& gfx = from(L)->requireFrame(L, "gfx.clear");
    gfx.fillRect(0, 0, gfx.width(), gfx.height(), optColor(L, 1, kBlack), 0);
    return 0;
}

int LuaPlugin::l_gfx_rect(lua_State* L) {
    Gfx& gfx = from(L)->requireFrame(L, "gfx.rect");
    const int x = checkPx(L, 1), y = checkPx(L, 2), w = checkPx(L, 3), h = checkPx(L, 4);
    const uint32_t color = optColor(L, 5, kWhite);
    const int radius = optPx(L, 6, 0);
    if (w > 0 && h > 0) gfx.fillRect(x, y, w, h, color, radius < 0 ? 0 : radius);
    return 0;
}

int LuaPlugin::l_gfx_outline(lua_State* L) {
    Gfx& gfx = from(L)->requireFrame(L, "gfx.outline");
    const int x = checkPx(L, 1), y = checkPx(L, 2), w = checkPx(L, 3), h = checkPx(L, 4);
    const uint32_t color = optColor(L, 5, kWhite);
    const int lineWidth = optPx(L, 6, 1);
    if (w > 0 && h > 0 && lineWidth > 0) gfx.strokeRect(x, y, w, h, color, lineWidth);
    return 0;
}

int LuaPlugin::l_gfx_line(lua_State* L) {
    Gfx& gfx = from(L)->requireFrame(L, "gfx.line");
    const int x1 = checkPx(L, 1), y1 = checkPx(L, 2), x2 = checkPx(L, 3), y2 = checkPx(L, 4);
    const uint32_t color = optColor(L, 5, kWhite);
    const int lineWidth = optPx(L, 6, 1);
    if (lineWidth > 0) gfx.line(x1, y1, x2, y2, color, lineWidth);
    return 0;
}

int LuaPlugin::l_gfx_circle(lua_State* L) {
    Gfx& gfx = from(L)->requireFrame(L, "gfx.circle");
    const int x = checkPx(L, 1), y = checkPx(L, 2), radius = checkPx(L, 3);
    const uint32_t color = optColor(L, 4, kWhite);
    if (radius > 0) gfx.fillRect(x - radius, y - radius, 2 * radius + 1, 2 * radius + 1, color, radius + 1);
    return 0;
}

int LuaPlugin::l_gfx_pixel(lua_State* L) {
    Gfx& gfx = from(L)->requireFrame(L, "gfx.pixel");
    const int x = checkPx(L, 1), y = checkPx(L, 2);
    gfx.fillRect(x, y, 1, 1, optColor(L, 3, kWhite), 0);
    return 0;
}

int LuaPlugin::l_gfx_text(lua_State* L) {
    Gfx& gfx = from(L)->requireFrame(L, "gfx.text");
    const int x = checkPx(L, 1), y = checkPx(L, 2);
    const char* text = luaL_checkstring(L, 3);
    const uint32_t color = optColor(L, 4, kWhite);
    const Font font = optFont(L, 5);
    const Align align = optAlign(L, 6);
    gfx.text(x, y, text, color, font, align);
    return 0;
}

int LuaPlugin::l_gfx_textWidth(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    const Font font = optFont(L, 2);
    lua_pushinteger(L, from(L)->metrics_.textWidth(text, font));
    return 1;
}

int LuaPlugin::l_gfx_textHeight(lua_State* L) {
    lua_pushinteger(L, from(L)->metrics_.textHeight(optFont(L, 1)));
    return 1;
}

// gfx.wrap(text, width, [font]) -> list of lines
int LuaPlugin::l_gfx_wrap(lua_State* L) {
    LuaPlugin* self = from(L);
    size_t length = 0;
    const char* text = luaL_checklstring(L, 1, &length);
    const int width = checkPx(L, 2);
    const Font font = optFont(L, 3);
    if (length > 2000) length = 2000;
    lua_createtable(L, 4, 0);
    int status = LUA_OK;
    {
        const std::vector<std::string> lines = wrapText(self->metrics_, std::string(text, length), font, width);
        for (size_t i = 0; i < lines.size() && status == LUA_OK; i++) {
            status = pushStringProtected(L, lines[i].data(), lines[i].size());
            if (status == LUA_OK) lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
        }
    }
    if (status != LUA_OK) return lua_error(L);
    return 1;
}

// gfx.clip(x, y, w, h) limits drawing to a rectangle; gfx.clip() lifts the limit.
int LuaPlugin::l_gfx_clip(lua_State* L) {
    Gfx& gfx = from(L)->requireFrame(L, "gfx.clip");
    if (lua_gettop(L) == 0) {
        gfx.clip(0, 0, 0, 0);
        return 0;
    }
    const int x = checkPx(L, 1), y = checkPx(L, 2), w = checkPx(L, 3), h = checkPx(L, 4);
    // An empty rectangle hides everything; it must not read as "no limit".
    if (w <= 0 || h <= 0) {
        gfx.clip(-1, -1, 1, 1);
    } else {
        gfx.clip(x, y, w, h);
    }
    return 0;
}

// gfx.image({ "rows of characters", ..., colors = { a = "#ff0000", ["."] = "none" } })
// Each character is one pixel; `colors` says what each character means.
int LuaPlugin::l_gfx_image(lua_State* L) {
    LuaPlugin* self = from(L);
    luaL_checktype(L, 1, LUA_TTABLE);

    // The palette, by character. Everything that can raise a Lua error happens
    // here, before any memory is taken for the pixels.
    uint32_t palette[256];
    bool known[256] = {false};
    if (lua_getfield(L, 1, "colors") != LUA_TTABLE) return luaL_error(L, "gfx.image: give it a `colors` table");
    const int colors = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, colors) != 0) {
        size_t keyLength = 0;
        const char* key = lua_type(L, -2) == LUA_TSTRING ? lua_tolstring(L, -2, &keyLength) : nullptr;
        if (!key || keyLength != 1) return luaL_error(L, "gfx.image: each key in `colors` is one character");
        const unsigned char character = static_cast<unsigned char>(key[0]);
        if (lua_type(L, -1) == LUA_TSTRING && strcmp(lua_tostring(L, -1), "none") == 0) {
            palette[character] = 0;  // fully transparent
        } else {
            palette[character] = 0xFF000000u | optColor(L, lua_gettop(L), kWhite);
        }
        known[character] = true;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    const lua_Unsigned rowCount = lua_rawlen(L, 1);
    if (rowCount == 0 || rowCount > 240) return luaL_error(L, "gfx.image: it needs 1 to 240 rows");
    size_t width = 0;
    for (lua_Unsigned row = 1; row <= rowCount; row++) {
        size_t length = 0;
        const bool isText = lua_rawgeti(L, 1, static_cast<lua_Integer>(row)) == LUA_TSTRING;
        if (isText) lua_tolstring(L, -1, &length);
        lua_pop(L, 1);
        if (!isText) return luaL_error(L, "gfx.image: row %d is not text", (int)row);
        if (row == 1) width = length;
        if (length != width) return luaL_error(L, "gfx.image: row %d has %d characters, the first has %d", (int)row, (int)length, (int)width);
    }
    if (width == 0 || width > 240) return luaL_error(L, "gfx.image: rows need 1 to 240 characters");
    const size_t bytes = width * rowCount * sizeof(uint32_t);
    if (self->imageBytes_ + bytes > self->limits_.imageBytes || self->images_.size() >= 256) {
        return luaL_error(L, "gfx.image: too many or too large images (limit %d KB per plugin)", (int)(self->limits_.imageBytes / 1024));
    }

    int badRow = 0;
    int badCharacter = 0;
    {
        Image image;
        image.width = static_cast<int>(width);
        image.height = static_cast<int>(rowCount);
        image.pixels.resize(width * rowCount);
        for (lua_Unsigned row = 1; row <= rowCount && badRow == 0; row++) {
            lua_rawgeti(L, 1, static_cast<lua_Integer>(row));
            const char* text = lua_tostring(L, -1);
            for (size_t x = 0; x < width; x++) {
                const unsigned char character = static_cast<unsigned char>(text[x]);
                if (!known[character]) {
                    badRow = static_cast<int>(row);
                    badCharacter = character;
                    break;
                }
                image.pixels[(row - 1) * width + x] = palette[character];
            }
            lua_pop(L, 1);
        }
        if (badRow == 0) {
            self->images_.push_back(std::move(image));
            self->imageBytes_ += bytes;
        }
    }
    if (badRow != 0) return luaL_error(L, "gfx.image: row %d uses '%c', which is not in `colors`", badRow, badCharacter);

    // What the plugin holds on to: a small table naming the image.
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, static_cast<lua_Integer>(self->images_.size()));
    lua_setfield(L, -2, "id");
    lua_pushinteger(L, static_cast<lua_Integer>(width));
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, static_cast<lua_Integer>(rowCount));
    lua_setfield(L, -2, "height");
    return 1;
}

// gfx.draw(image, x, y, [scale])
int LuaPlugin::l_gfx_draw(lua_State* L) {
    LuaPlugin* self = from(L);
    Gfx& gfx = self->requireFrame(L, "gfx.draw");
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushliteral(L, "id");
    lua_rawget(L, 1);
    const lua_Integer id = lua_tointeger(L, -1);
    lua_pop(L, 1);
    if (id < 1 || static_cast<size_t>(id) > self->images_.size()) {
        return luaL_argerror(L, 1, "not an image made with gfx.image()");
    }
    const int x = checkPx(L, 2), y = checkPx(L, 3);
    int scale = optPx(L, 4, 1);
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    const Image& image = self->images_[static_cast<size_t>(id - 1)];
    gfx.image(x, y, image.pixels.data(), image.width, image.height, scale);
    return 0;
}

int LuaPlugin::l_ui_title(lua_State* L) {
    LuaPlugin* self = from(L);
    self->requireFrame(L, "ui.title");
    const char* text = luaL_checkstring(L, 1);
    const uint32_t color = optColor(L, 2, kGray);
    self->ui_.push_back({UiItem::Title, text, color, {}});
    return 0;
}

int LuaPlugin::l_ui_big(lua_State* L) {
    LuaPlugin* self = from(L);
    self->requireFrame(L, "ui.big");
    const char* text = luaL_checkstring(L, 1);
    const uint32_t color = optColor(L, 2, kWhite);
    self->ui_.push_back({UiItem::Big, text, color, {}});
    return 0;
}

int LuaPlugin::l_ui_label(lua_State* L) {
    LuaPlugin* self = from(L);
    self->requireFrame(L, "ui.label");
    const char* text = luaL_checkstring(L, 1);
    const uint32_t color = optColor(L, 2, kGray);
    self->ui_.push_back({UiItem::Label, text, color, {}});
    return 0;
}

int LuaPlugin::l_ui_chart(lua_State* L) {
    LuaPlugin* self = from(L);
    self->requireFrame(L, "ui.chart");
    luaL_checktype(L, 1, LUA_TTABLE);
    const uint32_t color = optColor(L, 2, kChartBlue);
    lua_Unsigned count = lua_rawlen(L, 1);
    if (count > 240) count = 240;

    UiItem item{UiItem::Chart, "", color, {}};
    item.values.reserve((size_t)count);
    for (lua_Unsigned i = 1; i <= count; i++) {
        lua_rawgeti(L, 1, (lua_Integer)i);
        int isNumber = 0;
        const lua_Number value = lua_tonumberx(L, -1, &isNumber);
        lua_pop(L, 1);
        if (isNumber) item.values.push_back((float)value);
    }
    self->ui_.push_back(std::move(item));
    return 0;
}

int LuaPlugin::startHttp(lua_State* L, const char* method, int bodyIndex, int optsIndex, int callbackIndex) {
    const char* url = luaL_checkstring(L, 1);
    luaL_checktype(L, callbackIndex, LUA_TFUNCTION);
    if (optsIndex != 0) luaL_checktype(L, optsIndex, LUA_TTABLE);
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        return luaL_error(L, "http: the url must start with http:// or https://");
    }
    if ((int)pendingHttp_.size() >= limits_.maxPendingHttp) {
        return luaL_error(L, "http: too many requests in flight (max %d)", limits_.maxPendingHttp);
    }
    if (bodyIndex != 0) {
        const int type = lua_type(L, bodyIndex);
        if (type != LUA_TSTRING && type != LUA_TTABLE && type != LUA_TNIL) {
            return luaL_argerror(L, bodyIndex, "body must be a string, a table or nil");
        }
    }

    const char* problem = nullptr;
    int status = LUA_OK;
    {
        HttpRequest request;
        request.method = method;
        request.url = url;
        HttpBuild build{&request, LUA_NOREF, LUA_NOREF, nullptr};

        lua_pushcfunction(L, buildHttpRequest);
        lua_pushlightuserdata(L, &build);
        if (bodyIndex != 0) {
            lua_pushvalue(L, bodyIndex);
        } else {
            lua_pushnil(L);
        }
        if (optsIndex != 0) {
            lua_pushvalue(L, optsIndex);
        } else {
            lua_pushnil(L);
        }
        lua_pushvalue(L, callbackIndex);
        status = lua_pcall(L, 4, 0, 0);
        problem = build.problem;

        if (status == LUA_OK && !problem) {
            const int requestId = nextRequestId_++;
            pendingHttp_[requestId] = PendingHttp{build.callbackRef, build.filterRef};
            if (!services_.httpStart(requestId, request)) {
                pendingHttp_.erase(requestId);
                luaL_unref(L, LUA_REGISTRYINDEX, build.callbackRef);
                luaL_unref(L, LUA_REGISTRYINDEX, build.filterRef);
                problem = "the network queue is full";
            }
        } else {
            // The request never started; let go of what was set aside for it.
            luaL_unref(L, LUA_REGISTRYINDEX, build.callbackRef);
            luaL_unref(L, LUA_REGISTRYINDEX, build.filterRef);
        }
    }
    if (status != LUA_OK) return lua_error(L);
    if (problem) return luaL_error(L, "http: %s", problem);
    return 0;
}

// http.get(url, [options], callback)
int LuaPlugin::l_http_get(lua_State* L) {
    const bool hasOptions = lua_gettop(L) >= 3;
    return from(L)->startHttp(L, "GET", 0, hasOptions ? 2 : 0, hasOptions ? 3 : 2);
}

// http.post(url, body, [options], callback)
int LuaPlugin::l_http_post(lua_State* L) {
    const bool hasOptions = lua_gettop(L) >= 4;
    return from(L)->startHttp(L, "POST", 2, hasOptions ? 3 : 0, hasOptions ? 4 : 3);
}

int LuaPlugin::l_json_decode(lua_State* L) {
    size_t length = 0;
    const char* text = luaL_checklstring(L, 1, &length);
    const char* error = nullptr;
    if (jsonDecode(L, text, length, &error)) return 1;
    lua_pushnil(L);
    lua_pushstring(L, error);
    return 2;
}

int LuaPlugin::l_json_encode(lua_State* L) {
    luaL_checkany(L, 1);
    const char* error = nullptr;
    bool ok = false;
    int status = LUA_OK;
    {
        std::string out;
        ok = jsonEncode(L, 1, out, &error);
        if (ok) status = pushStringProtected(L, out.data(), out.size());
    }
    if (!ok) return luaL_error(L, "json.encode: %s", error);
    if (status != LUA_OK) return lua_error(L);
    return 1;
}

// led.set(color, [seconds]) or led.set(r, g, b, [seconds])
int LuaPlugin::l_led_set(lua_State* L) {
    LuaPlugin* self = from(L);
    uint8_t r = 0, g = 0, b = 0;
    int secondsIndex = 2;
    if (lua_gettop(L) >= 3) {
        r = (uint8_t)(luaL_checkinteger(L, 1) & 0xFF);
        g = (uint8_t)(luaL_checkinteger(L, 2) & 0xFF);
        b = (uint8_t)(luaL_checkinteger(L, 3) & 0xFF);
        secondsIndex = 4;
    } else {
        luaL_checkany(L, 1);
        const uint32_t rgb = optColor(L, 1, kBlack);
        r = (uint8_t)(rgb >> 16);
        g = (uint8_t)(rgb >> 8);
        b = (uint8_t)rgb;
    }
    double seconds = (double)luaL_optnumber(L, secondsIndex, 0);
    if (!(seconds > 0)) seconds = 0;
    if (seconds > 86400) seconds = 86400;
    self->services_.ledSet(r, g, b, (uint32_t)(seconds * 1000.0));
    return 0;
}

int LuaPlugin::l_led_off(lua_State* L) {
    from(L)->services_.ledOff();
    return 0;
}

// store.get(key, [default])
int LuaPlugin::l_store_get(lua_State* L) {
    LuaPlugin* self = from(L);
    const char* key = luaL_checkstring(L, 1);
    bool decoded = false;
    int status = LUA_OK;
    {
        std::string json;
        if (self->services_.storeGet(key, json)) {
            status = decodeJsonProtected(L, json.data(), json.size(), &decoded);
            if (status == LUA_OK && !decoded) lua_pop(L, 1);
        }
    }
    if (status != LUA_OK) return lua_error(L);
    if (!decoded) {
        if (lua_gettop(L) >= 2) {
            lua_pushvalue(L, 2);
        } else {
            lua_pushnil(L);
        }
    }
    return 1;
}

// store.set(key, value)
int LuaPlugin::l_store_set(lua_State* L) {
    LuaPlugin* self = from(L);
    const char* key = luaL_checkstring(L, 1);
    luaL_checkany(L, 2);
    const char* error = nullptr;
    bool ok = false;
    {
        std::string json;
        ok = jsonEncode(L, 2, json, &error);
        if (ok) {
            // Writing to flash can take a while. That is the device's time,
            // not the plugin's, so it does not count against the call budget.
            const uint32_t before = self->services_.millis();
            if (!self->services_.storeSet(key, json)) {
                ok = false;
                error = "could not save (too much data?)";
            }
            self->callStartMs_ += self->services_.millis() - before;
        }
    }
    if (!ok) return luaL_error(L, "store.set: %s", error);
    return 0;
}

int LuaPlugin::l_time_ms(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)(from(L)->services_.millis() & 0x7FFFFFFF));
    return 1;
}

int LuaPlugin::l_time_now(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)from(L)->services_.epochSeconds());
    return 1;
}

int LuaPlugin::l_input_held(lua_State* L) {
    const int which = luaL_checkoption(L, 1, nullptr, kButtonNames);
    lua_pushboolean(L, from(L)->services_.buttonHeld((Button)which));
    return 1;
}

}  // namespace deskhog
