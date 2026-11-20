#include "LuaPlugin.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <utility>

#include "LuaJson.h"
#include "TimeZone.h"
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
const int kMaxPages = 99;     ///< More than this and the rest cannot be reached
const int kMaxPageDots = 10;  ///< Up to this many pages show as dots, more as "3/12"

struct NamedColor {
    const char* name;
    uint32_t rgb;
};

const NamedColor kNamedColors[] = {
    {"white", 0xFFFFFF}, {"black", 0x000000}, {"red", 0xE74C3C},    {"green", 0x2ECC71},
    {"blue", 0x3498DB},  {"yellow", 0xF1C40F}, {"orange", 0xE67E22}, {"purple", 0x9B59B6},
    {"gray", 0x888888},  {"grey", 0x888888},
};

const char* const kFontNames[] = {"small", "medium", "large", "loud", "tiny", nullptr};
const char* const kAlignNames[] = {"left", "center", "right", nullptr};
const char* const kButtonNames[] = {"down", "center", "up", nullptr};
const char* const kEventNames[] = {"press", "long", "release"};

// What res.headers keeps of an answer's headers, names and values together.
const size_t kMaxHeaderBytes = 2048;
// The picture behind gfx.fetchImage never enters Lua's memory, so the file may
// be larger than an ordinary answer.
const size_t kImageMaxBytes = 256 * 1024;
// Timers are counted in 32-bit milliseconds; this stays well inside half the range.
const double kMaxTimerSeconds = 20.0 * 86400.0;
const uint32_t kMinTimerMs = 100;
const uint32_t kMinHiddenTimerMs = 1000;
// time.date and friends: years 1 to 9999.
const int64_t kMinTime = -62135596800LL;
const int64_t kMaxTime = 253402300799LL;
const char kTimerMeta[] = "deskhog.timer";
// notify(): how much banner text is kept. The host may keep less.
const size_t kMaxNoticeBytes = 200;
// What done() of a `choices` function may hand over.
const size_t kMaxChoices = 100;
const size_t kMaxChoiceBytes = 200;       // one value, or one label
const size_t kMaxChoiceListBytes = 8192;  // all of them together; what comes after is left out

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
    bool image;
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
        lua_getfield(L, 3, "insecure");
        request.insecure = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        if (build->image) {
            // gfx.fetchImage: how large the picture may end up.
            static const char* const sides[] = {"width", "height"};
            int* const into[] = {&request.imageMaxWidth, &request.imageMaxHeight};
            for (int i = 0; i < 2; i++) {
                if (lua_getfield(L, 3, sides[i]) == LUA_TNUMBER) {
                    double px = (double)lua_tonumber(L, -1);
                    if (!(px >= 1)) px = 1;
                    if (px > kMaxImageSide) px = kMaxImageSide;
                    *into[i] = (int)px;
                }
                lua_pop(L, 1);
            }
        } else if (lua_getfield(L, 3, "only") == LUA_TTABLE) {
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

// Does the Lua function on top of the stack declare at least `count` parameters
// (or take any number)? Leaves the stack as it was.
bool takesArgument(lua_State* L, int count) {
    lua_Debug info;
    lua_pushvalue(L, -1);
    if (!lua_getinfo(L, ">u", &info)) return false;
    return info.isvararg || info.nparams >= count;
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

LuaPlugin::~LuaPlugin() {
    // Nobody is left to tell: the host that owns this plugin is going away too.
    pendingChoices_.clear();
    close();
}

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
    timers_.clear();
    drawing_ = nullptr;
    // Whoever waits for a list of choices gets an answer, not a timeout.
    if (!pendingChoices_.empty()) {
        std::map<int, uint32_t> waiting;
        waiting.swap(pendingChoices_);
        const std::string why = failed_ ? "the plugin crashed: " + error_ : "the plugin restarted";
        for (const auto& pair : waiting) services_.choicesDone(pair.first, std::vector<Choice>(), why);
    }
    for (Card& card : cards_) {
        card.ref = LUA_NOREF;
        for (bool& down : card.down) down = false;
    }
    // Images belong to the program that made them.
    images_.clear();
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

// Leaves what the plugin calls `name` on the stack: the global for a single-card
// plugin, the field of the card's kind otherwise. Nil when there is none.
void LuaPlugin::pushCardValue(lua_State* L, const Card& card, const char* name) {
    if (card.ref == LUA_NOREF) {
        lua_getglobal(L, name);
        return;
    }
    if (lua_getglobal(L, "cards") != LUA_TTABLE) {
        lua_pop(L, 1);
        lua_pushnil(L);
        return;
    }
    lua_pushstring(L, card.kind.c_str());
    if (lua_rawget(L, -2) != LUA_TTABLE) {
        lua_pop(L, 2);
        lua_pushnil(L);
        return;
    }
    lua_pushstring(L, name);
    lua_rawget(L, -2);
    lua_remove(L, -2);  // the kind table
    lua_remove(L, -2);  // the cards table
}

// How many pages the card has right now: the length of its `pages` list, or
// what its `pages` function answers. Keeps the page on show within that.
int LuaPlugin::countPages(lua_State* L, Card& card) {
    int count = 0;
    pushCardValue(L, card, "pages");
    if (lua_istable(L, -1)) {
        count = (int)lua_rawlen(L, -1);
    } else if (lua_isfunction(L, -1)) {
        int args = 0;
        if (card.ref != LUA_NOREF) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, card.ref);
            args = 1;
        }
        lua_call(L, args, 1);
        if (!lua_isnumber(L, -1)) luaL_error(L, "pages() must return how many pages there are, a number");
        const lua_Number answer = lua_tonumber(L, -1);
        count = answer < 1 ? 0 : answer > kMaxPages ? kMaxPages : (int)answer;
    }
    lua_pop(L, 1);
    if (count > kMaxPages) count = kMaxPages;
    card.pageCount = count;
    if (card.page > count) card.page = count;
    if (card.page < 1) card.page = 1;
    return count;
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
            Card& card = *call->card;
            if (card.pages == PagesKind::None) {
                const int args = self->pushCardFunction(L, card, "draw");
                if (args >= 0) lua_call(L, args, 0);
                break;
            }
            const int count = self->countPages(L, card);
            if (card.pages == PagesKind::List) {
                // Each entry of the list draws one page.
                if (count == 0) break;
                self->pushCardValue(L, card, "pages");
                if (lua_rawgeti(L, -1, card.page) != LUA_TFUNCTION) {
                    luaL_error(L, "pages[%d] is not a function", card.page);
                }
                int args = 0;
                if (card.ref != LUA_NOREF) {
                    lua_rawgeti(L, LUA_REGISTRYINDEX, card.ref);
                    args = 1;
                }
                lua_call(L, args, 0);
                break;
            }
            // pages() counts them and draw() is told which one to draw.
            const int args = self->pushCardFunction(L, card, "draw");
            if (args >= 0) {
                lua_pushinteger(L, card.page);
                lua_call(L, args + 1, 0);
            }
            break;
        }
        case CallKind::CardPages:
            self->countPages(L, *call->card);
            break;
        case CallKind::CardButton: {
            const int args = self->pushCardFunction(L, *call->card, "button");
            if (args >= 0) {
                lua_pushstring(L, call->buttonName);
                // Only functions that ask for the event get one, so a plugin
                // written before there were events sees what it always saw.
                if (call->eventName) lua_pushstring(L, call->eventName);
                lua_call(L, args + (call->eventName ? 2 : 1), 1);
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
        case CallKind::Image:
            self->doImage(L, *call);
            break;
        case CallKind::Timer:
            lua_rawgeti(L, LUA_REGISTRYINDEX, call->requestId);
            if (!call->keepRef) luaL_unref(L, LUA_REGISTRYINDEX, call->requestId);
            lua_call(L, 0, 1);
            // Like update(), a timer may return false to say nothing it draws has changed.
            call->result = !(lua_isboolean(L, -1) && !lua_toboolean(L, -1));
            break;
        case CallKind::Message:
            self->doMessage(L, *call);
            break;
        case CallKind::Choices:
            if (lua_getglobal(L, call->body->c_str()) != LUA_TFUNCTION) break;
            call->result = true;
            lua_pushinteger(L, call->requestId);
            lua_pushcclosure(L, l_choices_done, 1);
            self->pushChoiceContext(L, *call->context);
            lua_call(L, 2, 0);
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
    hasMessage_ = false;
    memUsed_ = 0;
    refreshMs_ = 0;
    loadDue_ = false;
    frameIntervalMs_ = 33;
    nextImageId_ = 1;
    nextTimerId_ = 1;

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
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        // The usual mistake at the top of a file: reading a setting there. While
        // the file runs, `config.city` is still its description, a table (or
        // nothing: it has no text or number to work with yet). Say so.
        const char* message = lua_tostring(L, -1);
        const bool looksLikeIt = message && (strstr(message, "a table value (field '") || strstr(message, "a nil value (method '") ||
                                             strstr(message, "a nil value (field '") || strstr(message, " with table") ||
                                             strstr(message, "table with "));
        if (looksLikeIt && lua_getglobal(L, "config") == LUA_TTABLE) {
            lua_pop(L, 1);
            lua_pushliteral(L, ". If that is a setting: `config` only holds the values once the file has run, so read them "
                               "in load() or later, not at the top of the file");
            lua_concat(L, 2);
        } else if (looksLikeIt) {
            lua_pop(L, 1);
        }
        lua_error(L);
    }
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
    hasMessage_ = lua_getglobal(L, "message") == LUA_TFUNCTION;
    lua_pop(L, 2);
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

    // Lua seeds math.random from the clock and an address. On the device both
    // are the same on every boot, so every game would play out the same way.
    {
        lua_getglobal(L, LUA_MATHLIBNAME);
        lua_getfield(L, -1, "randomseed");
        lua_pushinteger(L, (lua_Integer)services_.entropy());
        lua_pushinteger(L, (lua_Integer)(services_.entropy() ^ services_.millis()));
        lua_call(L, 2, 0);
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

    // notify() of a single-card plugin. A card of a plugin with card kinds has
    // its own, card.notify(), see doAddCard().
    lua_pushnil(L);
    lua_pushcclosure(L, l_notify, 1);
    lua_setglobal(L, "notify");

    static const luaL_Reg gfx[] = {
        {"clear", l_gfx_clear},   {"rect", l_gfx_rect},   {"outline", l_gfx_outline},
        {"line", l_gfx_line},     {"circle", l_gfx_circle}, {"pixel", l_gfx_pixel},
        {"text", l_gfx_text},     {"textWidth", l_gfx_textWidth}, {"textHeight", l_gfx_textHeight},
        {"wrap", l_gfx_wrap},     {"clip", l_gfx_clip},   {"image", l_gfx_image},
        {"draw", l_gfx_draw},     {"arc", l_gfx_arc},     {"poly", l_gfx_poly},
        {"triangle", l_gfx_triangle}, {"fetchImage", l_gfx_fetchImage}, {"release", l_gfx_release},
        {"color", l_gfx_color},
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

    static const luaL_Reg http[] = {
        {"get", l_http_get},       {"post", l_http_post},     {"put", l_http_put}, {"patch", l_http_patch},
        {"delete", l_http_delete}, {"request", l_http_request}, {nullptr, nullptr},
    };
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

    static const luaL_Reg time[] = {
        {"ms", l_time_ms},         {"now", l_time_now},   {"date", l_time_date},
        {"format", l_time_format}, {"zone", l_time_zone}, {"offset", l_time_offset},
        {"make", l_time_make},     {nullptr, nullptr},
    };
    setFunctions(L, "time", time);

    static const luaL_Reg net[] = {
        {"online", l_net_online}, {"rssi", l_net_rssi}, {"ssid", l_net_ssid}, {"ip", l_net_ip}, {nullptr, nullptr},
    };
    setFunctions(L, "net", net);

    static const luaL_Reg device[] = {
        {"battery", l_device_battery}, {"name", l_device_name}, {nullptr, nullptr},
    };
    setFunctions(L, "device", device);

    static const luaL_Reg timer[] = {
        {"after", l_timer_after}, {"every", l_timer_every}, {"cancel", l_timer_cancel}, {nullptr, nullptr},
    };
    setFunctions(L, "timer", timer);
    // What a timer handle is: a small table whose :cancel() is timer.cancel.
    luaL_newmetatable(L, kTimerMeta);
    lua_getglobal(L, "timer");
    lua_setfield(L, -2, "__index");
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "__metatable");  // getmetatable() shows nothing to tamper with
    lua_pop(L, 1);

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
                lua_pushliteral(L, "message");
                kind.message = lua_rawget(L, kindTable) == LUA_TFUNCTION;
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
        main.message = lua_getglobal(L, "message") == LUA_TFUNCTION;
        lua_pop(L, 1);
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
                lua_pushliteral(L, "type");
                if (lua_rawget(L, value) == LUA_TSTRING && strcmp(lua_tostring(L, -1), "timezone") == 0) {
                    field.type = "timezone";
                }
                lua_pop(L, 1);
                // The name of a function that lists what to pick from. Only a
                // plain name: it is looked up as a global, nothing more.
                lua_pushliteral(L, "choices");
                if (lua_rawget(L, value) == LUA_TSTRING) {
                    const char* name = lua_tostring(L, -1);
                    const size_t length = strlen(name);
                    bool plain = length > 0 && length <= 64;
                    for (size_t i = 0; i < length && plain; i++) {
                        const char c = name[i];
                        plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (i > 0 && c >= '0' && c <= '9');
                    }
                    if (plain) field.choices = name;
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
            // The value's type follows from the default. A string field may say
            // more about what it holds: type = "timezone".
            if (defaultType == LUA_TNUMBER) {
                field.type = "number";
            } else if (defaultType == LUA_TBOOLEAN) {
                field.type = "boolean";
            } else if (field.type != "timezone") {
                field.type = "string";
            }
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
        card.buttonEvents = card.hasButton && takesArgument(L, 2);
        card.hasMessage = lua_getglobal(L, "message") == LUA_TFUNCTION;
        const int pages = lua_getglobal(L, "pages");
        card.pages = pages == LUA_TTABLE ? PagesKind::List : pages == LUA_TFUNCTION ? PagesKind::Count : PagesKind::None;
        lua_pop(L, 5);
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
    // card.notify(text, opts): notify() for this card.
    lua_pushstring(L, card.id.c_str());
    lua_pushcclosure(L, l_notify, 1);
    lua_setfield(L, cardTable, "notify");

    lua_pushvalue(L, cardTable);
    card.ref = luaL_ref(L, LUA_REGISTRYINDEX);

    static const char* const names[] = {"load", "update", "draw", "button"};
    bool* const flags[] = {&card.hasLoad, &card.hasUpdate, &card.hasDraw, &card.hasButton};
    for (int i = 0; i < 4; i++) {
        lua_pushstring(L, names[i]);
        *flags[i] = lua_rawget(L, kindTable) == LUA_TFUNCTION;
        if (i == 3) card.buttonEvents = card.hasButton && takesArgument(L, 3);  // (card, name, event)
        lua_pop(L, 1);
    }
    lua_pushliteral(L, "message");
    card.hasMessage = lua_rawget(L, kindTable) == LUA_TFUNCTION;
    lua_pop(L, 1);
    lua_pushliteral(L, "pages");
    const int pages = lua_rawget(L, kindTable);
    card.pages = pages == LUA_TTABLE ? PagesKind::List : pages == LUA_TFUNCTION ? PagesKind::Count : PagesKind::None;
    lua_pop(L, 1);

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
    card->page = 1;
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
    if (!card) return;
    const bool now = visible && card->surface;
    // A card with pages starts on its first one whenever it leaves or comes back.
    if (now != card->visible && card->page != 1) {
        card->page = 1;
        card->dirty = true;
    }
    card->visible = now;
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

    runTimers(nowMs);
    if (failed_ || !L_) return;
    expireChoices(nowMs);
    runButtonEvents(nowMs);
    if (failed_ || !L_) return;

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

    if (!failed_ && L_ && (card->hasDraw || card->pages == PagesKind::List)) {
        ui_.clear();
        drawing_ = &gfx;
        Call call{this, CallKind::CardDraw};
        call.card = card;
        const bool ok = run(call, limits_.callBudgetMs);
        drawing_ = nullptr;
        if (ok) flushUi(gfx);
        ui_.clear();
        if (ok) drawPageDots(gfx, *card);
    }

    if (failed_) {
        card->dirty = false;  // failing marked every card dirty; this one is being painted now
        gfx.fillRect(0, 0, gfx.width(), gfx.height(), kBlack, 0);
        drawError(gfx);
    }
}

bool LuaPlugin::button(const std::string& id, Button button) {
    Card* card = findCard(id);
    if (!card || failed_ || !L_) return false;
    const int index = (int)button;
    bool used = false;
    if (card->hasButton) {
        used = sendButton(*card, button, ButtonEvent::Press);
        if (failed_ || !L_) return false;
        if (card->buttonEvents) {
            // tick() watches the button from here on, for "long" and "release".
            card->down[index] = true;
            card->longSent[index] = false;
            card->downAtMs[index] = services_.millis();
        }
    }
    // A centre press the plugin did not use goes to the next page, if there is one.
    if (!used && button == Button::Center && card->pages != PagesKind::None) {
        Call call{this, CallKind::CardPages};
        call.card = card;
        if (!run(call, limits_.callBudgetMs)) return false;
        if (card->pageCount > 1) {
            card->page = card->page % card->pageCount + 1;
            card->dirty = true;
            used = true;
        }
    }
    return used;
}

// Which page is on show, along the bottom edge: a dot per page, or "3/12" when
// the dots would not fit.
void LuaPlugin::drawPageDots(Gfx& gfx, const Card& card) {
    if (card.pages == PagesKind::None || card.pageCount < 2) return;
    const uint32_t on = 0xFFFFFF, off = 0x555555;
    if (card.pageCount > kMaxPageDots) {
        char text[16];
        snprintf(text, sizeof(text), "%d/%d", card.page, card.pageCount);
        const int w = gfx.textWidth(text, Font::Tiny), h = gfx.textHeight(Font::Tiny);
        gfx.fillRect(gfx.width() - kPad - w - 3, gfx.height() - h - 1, w + 6, h + 1, kBlack, 0);
        gfx.text(gfx.width() - kPad, gfx.height() - h - 1, text, 0x999999, Font::Tiny, Align::Right);
        return;
    }
    const int gap = 8;
    int x = (gfx.width() - (card.pageCount - 1) * gap) / 2;
    for (int i = 1; i <= card.pageCount; i++, x += gap) {
        gfx.fillRect(x - 2, gfx.height() - 6, 4, 4, i == card.page ? on : off, 2);
    }
}

bool LuaPlugin::sendButton(Card& card, Button button, ButtonEvent event) {
    Call call{this, CallKind::CardButton};
    call.card = &card;
    call.buttonName = kButtonNames[(int)button];
    if (card.buttonEvents) call.eventName = kEventNames[(int)event];
    if (!run(call, limits_.callBudgetMs)) return false;
    card.dirty = true;
    return call.result;
}

// "long" and "release" for the presses button() delivered. Whether a button is
// still down comes from the host, the same way input.held() learns it.
void LuaPlugin::runButtonEvents(uint32_t nowMs) {
    for (size_t i = 0; i < cards_.size(); i++) {
        for (int b = 0; b < 3; b++) {
            Card& card = cards_[i];
            if (!card.down[b]) continue;
            const bool held = services_.buttonHeld((Button)b);
            if (held && !card.longSent[b] && nowMs - card.downAtMs[b] >= kLongPressMs) {
                card.longSent[b] = true;
                // A hold only means something on the card that is being looked at.
                if (card.visible) sendButton(card, (Button)b, ButtonEvent::Long);
                if (failed_ || !L_) return;
            }
            if (!held) {
                card.down[b] = false;
                sendButton(card, (Button)b, ButtonEvent::Release);  // every press gets its release
                if (failed_ || !L_) return;
            }
        }
    }
}

void LuaPlugin::runTimers(uint32_t nowMs) {
    if (timers_.empty()) return;
    bool anyVisible = false;
    for (const Card& card : cards_) anyVisible = anyVisible || card.visible;

    // A callback may cancel timers or start new ones, so work from a list of
    // ids. One made during this round waits for the next.
    int due[32];
    int dueCount = 0;
    for (const Timer& timer : timers_) {
        if ((int32_t)(nowMs - timer.dueMs) >= 0 && dueCount < 32) due[dueCount++] = timer.id;
    }
    bool repaint = false;
    for (int i = 0; i < dueCount; i++) {
        Call call{this, CallKind::Timer};
        bool found = false;
        for (auto it = timers_.begin(); it != timers_.end(); ++it) {
            if (it->id != due[i]) continue;
            found = true;
            call.requestId = it->ref;
            call.keepRef = it->repeat;
            if (it->repeat) {
                // Counted from now, not from when it was due: after a stall the
                // missed rounds are skipped, not made up for. With no card on
                // screen nothing needs to move ten times a second.
                const uint32_t floorMs = anyVisible ? kMinTimerMs : kMinHiddenTimerMs;
                it->dueMs = nowMs + (it->intervalMs > floorMs ? it->intervalMs : floorMs);
            } else {
                timers_.erase(it);
            }
            break;
        }
        if (!found) continue;  // cancelled by a callback earlier in this round
        if (!run(call, limits_.callBudgetMs)) return;
        repaint = repaint || call.result;
    }
    if (repaint) markAllDirty();
}

// ------------------------------------------------------------------ messages

bool LuaPlugin::message(const std::string& id, const std::string& body) {
    if (failed_ || !L_) return false;
    Card* card = id.empty() ? nullptr : findCard(id);
    if (!id.empty() && !card) return false;
    if (card ? !card->hasMessage : !hasMessage_) return false;

    Call call{this, CallKind::Message};
    call.card = card;
    call.body = &body;
    if (!run(call, limits_.callBudgetMs)) return false;
    // Whatever arrived, some card probably shows it.
    markAllDirty();
    return call.result;
}

// message(msg) or, for a card of a plugin with card kinds, message(card, msg).
void LuaPlugin::doMessage(lua_State* L, Call& call) {
    int args = 0;
    if (call.card) {
        args = pushCardFunction(L, *call.card, "message");
    } else if (lua_getglobal(L, "message") != LUA_TFUNCTION) {
        args = -1;
    }
    if (args < 0) return;  // the plugin took its function away since it started

    const std::string& body = *call.body;
    lua_createtable(L, 0, 2);
    lua_pushlstring(L, body.data(), body.size());
    lua_setfield(L, -2, "body");
    // As with an http answer: decoded up front when it looks like JSON.
    size_t i = 0;
    while (i < body.size() && (unsigned char)body[i] <= ' ') i++;
    if (i < body.size() && (body[i] == '{' || body[i] == '[')) {
        const char* error = nullptr;
        if (jsonDecode(L, body.data(), body.size(), &error)) lua_setfield(L, -2, "json");
    }
    lua_call(L, args + 1, 0);
    call.result = true;
}

// ------------------------------------------------------------------ choices

void LuaPlugin::choices(const std::string& function, int requestId, const ChoiceContext& context) {
    if (failed_ || !L_) {
        services_.choicesDone(requestId, std::vector<Choice>(),
                              failed_ ? "the plugin crashed: " + error_ : "the plugin is not running");
        return;
    }
    if ((int)pendingChoices_.size() >= limits_.maxPendingChoices) {
        services_.choicesDone(requestId, std::vector<Choice>(), "the plugin is still busy with earlier lists");
        return;
    }
    pendingChoices_[requestId] = services_.millis();

    Call call{this, CallKind::Choices};
    call.body = &function;
    call.context = &context;
    call.requestId = requestId;
    // If this fails, close() tells whoever is waiting.
    if (!run(call, limits_.callBudgetMs)) return;
    if (!call.result && pendingChoices_.erase(requestId)) {
        services_.choicesDone(requestId, std::vector<Choice>(), "the plugin has no function " + function + "()");
    }
}

// A setting's text as the value a plugin sees in `config`: a number, true or
// false, or text, going by the field's type. Text that is no number gives nil.
static void pushSettingValue(lua_State* L, const std::string& type, const std::string& text) {
    if (type == "number") {
        if (lua_stringtonumber(L, text.c_str()) == 0) lua_pushnil(L);
    } else if (type == "boolean") {
        lua_pushboolean(L, text == "true" || text == "1" || text == "on" || text == "yes");
    } else {
        lua_pushlstring(L, text.data(), text.size());
    }
}

// The second argument of a `choices` function: { kind = , settings = { ... } }.
// Runs inside a protected call; holds no C++ objects of its own, so a Lua
// memory error passing through leaks nothing.
void LuaPlugin::pushChoiceContext(lua_State* L, const ChoiceContext& context) {
    lua_createtable(L, 0, 2);
    const std::vector<ConfigField>* fields = &schema_;
    if (!context.kind.empty()) {
        lua_pushlstring(L, context.kind.data(), context.kind.size());
        lua_setfield(L, -2, "kind");
        fields = nullptr;
        for (const CardKind& kind : kinds_) {
            if (kind.key == context.kind) fields = &kind.config;
        }
    }
    lua_createtable(L, 0, (int)context.settings.size());
    // What the page did not send (it never has a secret, for one): the value in use, or the default.
    if (fields) {
        for (const ConfigField& field : *fields) {
            const std::string* text = &field.defaultValue;
            if (context.kind.empty()) {
                const auto saved = settings_.find(field.key);
                if (saved != settings_.end()) text = &saved->second;
            }
            pushSettingValue(L, field.type, *text);
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                pushSettingValue(L, field.type, field.defaultValue);
            }
            lua_setfield(L, -2, field.key.c_str());
        }
    }
    for (const auto& pair : context.settings) {
        const ConfigField* known = nullptr;
        if (fields) {
            for (const ConfigField& field : *fields) {
                if (field.key == pair.first) known = &field;
            }
        }
        static const std::string asText = "string";  // a key the form does not have: kept as it came
        pushSettingValue(L, known ? known->type : asText, pair.second);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);  // not a number: what was there stays
            continue;
        }
        lua_setfield(L, -2, pair.first.c_str());
    }
    lua_setfield(L, -2, "settings");
}

void LuaPlugin::expireChoices(uint32_t nowMs) {
    for (auto it = pendingChoices_.begin(); it != pendingChoices_.end();) {
        if (nowMs - it->second < limits_.choicesTimeoutMs) {
            ++it;
            continue;
        }
        const int requestId = it->first;
        it = pendingChoices_.erase(it);
        services_.choicesDone(requestId, std::vector<Choice>(), "the plugin did not answer in time");
    }
}

void LuaPlugin::repaint(const std::string& id) {
    Card* card = findCard(id);
    if (card) card->dirty = true;
}

void LuaPlugin::httpResponse(int requestId, const HttpResponse& response) { deliverHttp(requestId, response, nullptr); }

void LuaPlugin::httpResponse(int requestId, HttpResponse&& response) {
    deliverHttp(requestId, response, &response.image);
}

void LuaPlugin::deliverHttp(int requestId, const HttpResponse& response, std::vector<uint32_t>* pixels) {
    if (failed_ || !L_) return;
    const auto pending = pendingHttp_.find(requestId);
    if (pending == pendingHttp_.end()) return;
    const PendingHttp refs = pending->second;
    pendingHttp_.erase(pending);

    Call call{this, refs.image ? CallKind::Image : CallKind::Http};
    call.requestId = refs.callbackRef;
    call.filterRef = refs.filterRef;
    call.response = &response;

    std::string problem;
    if (refs.image) {
        // Keep the picture before going into Lua: nothing that owns memory may
        // be alive across a call that can raise an error.
        const size_t count = (size_t)(response.imageWidth > 0 ? response.imageWidth : 0) *
                             (size_t)(response.imageHeight > 0 ? response.imageHeight : 0);
        const char* reason = nullptr;
        if (response.status == 0) {
            problem = response.error.empty() ? "no answer" : response.error;
        } else if (response.status < 200 || response.status >= 300) {
            problem = "HTTP " + std::to_string(response.status);
        } else if (count == 0 || response.image.size() != count || response.imageWidth > kMaxImageSide ||
                   response.imageHeight > kMaxImageSide) {
            problem = response.error.empty() ? "not a PNG or JPEG picture" : response.error;
        } else {
            if (pixels) {
                call.imageId = storeImage(response.imageWidth, response.imageHeight, std::move(*pixels), &reason);
            } else {
                call.imageId = storeImage(response.imageWidth, response.imageHeight,
                                          std::vector<uint32_t>(response.image), &reason);
            }
            if (reason) problem = reason;
        }
        call.text = &problem;
    }
    // The callback may have changed data any of the cards shows.
    if (run(call, limits_.callBudgetMs)) markAllDirty();
}

int LuaPlugin::storeImage(int width, int height, std::vector<uint32_t>&& pixels, const char** problem) {
    const size_t bytes = (size_t)width * (size_t)height * sizeof(uint32_t);
    if (imageBytes_ + bytes > limits_.imageBytes || images_.size() >= 256) {
        *problem = "no room for this picture: the plugin's images are at their limit. gfx.release() the ones it no longer shows";
        return 0;
    }
    const int id = nextImageId_++;
    Image& image = images_[id];
    image.width = width;
    image.height = height;
    image.pixels = std::move(pixels);
    imageBytes_ += bytes;
    return id;
}

// The callback of gfx.fetchImage: callback(image) or callback(nil, why).
void LuaPlugin::doImage(lua_State* L, Call& call) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, call.requestId);
    luaL_unref(L, LUA_REGISTRYINDEX, call.requestId);
    if (call.imageId == 0) {
        lua_pushnil(L);
        lua_pushlstring(L, call.text->data(), call.text->size());
        lua_call(L, 2, 0);
        return;
    }
    const Image& image = images_[call.imageId];
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, call.imageId);
    lua_setfield(L, -2, "id");
    lua_pushinteger(L, image.width);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, image.height);
    lua_setfield(L, -2, "height");
    lua_call(L, 1, 0);
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

    lua_createtable(L, 0, 6);
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

    // res.headers, with the names in lower case. A header that comes twice is
    // joined with ", ". Only the first couple of KB are kept.
    lua_createtable(L, 0, (int)(response.headers.size() < 32 ? response.headers.size() : 32));
    size_t kept = 0;
    for (const auto& header : response.headers) {
        const size_t size = header.first.size() + header.second.size();
        if (header.first.empty() || kept + size > kMaxHeaderBytes) continue;
        kept += size;
        luaL_Buffer name;
        char* lowered = luaL_buffinitsize(L, &name, header.first.size());
        for (size_t c = 0; c < header.first.size(); c++) {
            const char ch = header.first[c];
            lowered[c] = (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch;
        }
        luaL_pushresultsize(&name, header.first.size());
        lua_pushvalue(L, -1);
        if (lua_rawget(L, -3) == LUA_TSTRING) {
            lua_pushliteral(L, ", ");
            lua_pushlstring(L, header.second.data(), header.second.size());
            lua_concat(L, 3);
        } else {
            lua_pop(L, 1);
            lua_pushlstring(L, header.second.data(), header.second.size());
        }
        lua_rawset(L, -3);
    }
    lua_setfield(L, -2, "headers");

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

// gfx.color(value) -> the colour as a number, or nil if it is not one. Never raises.
int LuaPlugin::l_gfx_color(lua_State* L) {
    uint32_t rgb = 0;
    bool ok = false;
    if (lua_type(L, 1) == LUA_TNUMBER) {
        // Whole numbers only, as everywhere a colour is taken: 0xFF8800, not 1.5.
        int isInteger = 0;
        const lua_Integer value = lua_tointegerx(L, 1, &isInteger);
        ok = isInteger != 0;
        rgb = (uint32_t)value & 0xFFFFFF;
    } else if (lua_type(L, 1) == LUA_TSTRING) {
        ok = parseColorText(lua_tostring(L, 1), rgb);
    }
    if (ok) {
        lua_pushinteger(L, (lua_Integer)rgb);
    } else {
        lua_pushnil(L);
    }
    return 1;
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
    if (radius <= 0) return 0;
    if (lua_isnoneornil(L, 5)) {
        gfx.fillRect(x - radius, y - radius, 2 * radius + 1, 2 * radius + 1, color, radius + 1);
        return 0;
    }
    // With a width: a ring, drawn inwards from the radius.
    const int lineWidth = checkPx(L, 5);
    if (lineWidth > 0) gfx.arc(x, y, radius > 500 ? 500 : radius, 0, 360, color, lineWidth < radius ? lineWidth : radius);
    return 0;
}

// gfx.arc(x, y, r, startDeg, endDeg, [color], [width]): 0 is straight up, angles
// grow clockwise. Without a width it is a filled slice.
int LuaPlugin::l_gfx_arc(lua_State* L) {
    Gfx& gfx = from(L)->requireFrame(L, "gfx.arc");
    const int x = checkPx(L, 1), y = checkPx(L, 2);
    int radius = checkPx(L, 3);
    const double fromDeg = (double)luaL_checknumber(L, 4);
    const double toDeg = (double)luaL_checknumber(L, 5);
    const uint32_t color = optColor(L, 6, kWhite);
    int lineWidth = lua_isnoneornil(L, 7) ? radius : checkPx(L, 7);
    if (radius <= 0 || lineWidth <= 0) return 0;
    if (radius > 500) radius = 500;
    if (lineWidth > radius) lineWidth = radius;
    if (!(fabs(fromDeg) < 1e6) || !(fabs(toDeg) < 1e6)) return luaL_error(L, "gfx.arc: angles are in degrees");

    // Clockwise from the first angle to the second: 350 to 10 is the short way over the top.
    double sweep = toDeg - fromDeg;
    if (sweep >= 360.0) {
        sweep = 360.0;
    } else {
        sweep = fmod(sweep, 360.0);
        if (sweep < 0) sweep += 360.0;
    }
    double start = fmod(fromDeg, 360.0);
    if (start < 0) start += 360.0;
    const int startDeg = (int)floor(start + 0.5);
    const int endDeg = (int)floor(start + sweep + 0.5);
    if (endDeg > startDeg) gfx.arc(x, y, radius, startDeg, endDeg, color, lineWidth);
    return 0;
}

// gfx.poly({x1, y1, x2, y2, ...}, [color]): a filled shape.
int LuaPlugin::l_gfx_poly(lua_State* L) {
    Gfx& gfx = from(L)->requireFrame(L, "gfx.poly");
    luaL_checktype(L, 1, LUA_TTABLE);
    const uint32_t color = optColor(L, 2, kWhite);
    const lua_Unsigned count = lua_rawlen(L, 1);
    int xy[128];
    if (count < 6 || count % 2 != 0 || count > 128) {
        return luaL_error(L, "gfx.poly: give it 3 to 64 points as {x1, y1, x2, y2, ...}");
    }
    for (lua_Unsigned i = 0; i < count; i++) {
        lua_rawgeti(L, 1, (lua_Integer)(i + 1));
        xy[i] = checkPx(L, -1);
        lua_pop(L, 1);
    }
    gfx.polygon(xy, (int)(count / 2), color);
    return 0;
}

// gfx.triangle(x1, y1, x2, y2, x3, y3, [color])
int LuaPlugin::l_gfx_triangle(lua_State* L) {
    Gfx& gfx = from(L)->requireFrame(L, "gfx.triangle");
    int xy[6];
    for (int i = 0; i < 6; i++) xy[i] = checkPx(L, i + 1);
    gfx.polygon(xy, 3, optColor(L, 7, kWhite));
    return 0;
}

// gfx.fetchImage(url, [options], callback): a PNG or JPEG from the network.
// The callback gets (image), or (nil, why). Options: width and height to fit
// it in, and headers, timeout and insecure as for http.get.
int LuaPlugin::l_gfx_fetchImage(lua_State* L) {
    const bool hasOptions = lua_gettop(L) >= 3;
    return from(L)->startHttp(L, "GET", luaL_checkstring(L, 1), 0, hasOptions ? 2 : 0, hasOptions ? 3 : 2, true);
}

// gfx.release(image): forget an image and give its memory back.
int LuaPlugin::l_gfx_release(lua_State* L) {
    LuaPlugin* self = from(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushliteral(L, "id");
    lua_rawget(L, 1);
    const lua_Integer id = lua_tointeger(L, -1);
    lua_pop(L, 1);
    const auto found = self->images_.find((int)id);
    if (found == self->images_.end()) return 0;  // released before: nothing to do
    self->imageBytes_ -= (size_t)found->second.width * (size_t)found->second.height * sizeof(uint32_t);
    self->images_.erase(found);
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
    int imageId = 0;
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
            imageId = self->nextImageId_++;
            self->images_[imageId] = std::move(image);
            self->imageBytes_ += bytes;
        }
    }
    if (badRow != 0) return luaL_error(L, "gfx.image: row %d uses '%c', which is not in `colors`", badRow, badCharacter);

    // What the plugin holds on to: a small table naming the image.
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, imageId);
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
    const auto found = self->images_.find((int)id);
    if (id < 1 || found == self->images_.end()) {
        return luaL_argerror(L, 1, "not an image made with gfx.image() or gfx.fetchImage(), or one that was released");
    }
    const int x = checkPx(L, 2), y = checkPx(L, 3);
    int scale = optPx(L, 4, 1);
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    const Image& image = found->second;
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

int LuaPlugin::startHttp(lua_State* L, const char* method, const char* url, int bodyIndex, int optsIndex,
                         int callbackIndex, bool imageRequest) {
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
        if (imageRequest) {
            request.maxBytes = kImageMaxBytes;
            request.imageMaxWidth = kMaxImageSide;
            request.imageMaxHeight = kMaxImageSide;
        }
        HttpBuild build{&request, LUA_NOREF, LUA_NOREF, nullptr, imageRequest};

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
            pendingHttp_[requestId] = PendingHttp{build.callbackRef, build.filterRef, imageRequest};
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

// http.get(url, [options], callback); http.delete is the same shape.
static int httpWithoutBody(lua_State* L, int& optsIndex, int& callbackIndex) {
    const bool hasOptions = lua_gettop(L) >= 3;
    optsIndex = hasOptions ? 2 : 0;
    callbackIndex = hasOptions ? 3 : 2;
    return 0;
}

// http.post(url, body, [options], callback); http.put and http.patch are the same shape.
static int httpWithBody(lua_State* L, int& optsIndex, int& callbackIndex) {
    const bool hasOptions = lua_gettop(L) >= 4;
    optsIndex = hasOptions ? 3 : 0;
    callbackIndex = hasOptions ? 4 : 3;
    return 2;
}

int LuaPlugin::l_http_get(lua_State* L) {
    int opts, callback;
    const int body = httpWithoutBody(L, opts, callback);
    return from(L)->startHttp(L, "GET", luaL_checkstring(L, 1), body, opts, callback);
}

int LuaPlugin::l_http_delete(lua_State* L) {
    int opts, callback;
    const int body = httpWithoutBody(L, opts, callback);
    return from(L)->startHttp(L, "DELETE", luaL_checkstring(L, 1), body, opts, callback);
}

int LuaPlugin::l_http_post(lua_State* L) {
    int opts, callback;
    const int body = httpWithBody(L, opts, callback);
    return from(L)->startHttp(L, "POST", luaL_checkstring(L, 1), body, opts, callback);
}

int LuaPlugin::l_http_put(lua_State* L) {
    int opts, callback;
    const int body = httpWithBody(L, opts, callback);
    return from(L)->startHttp(L, "PUT", luaL_checkstring(L, 1), body, opts, callback);
}

int LuaPlugin::l_http_patch(lua_State* L) {
    int opts, callback;
    const int body = httpWithBody(L, opts, callback);
    return from(L)->startHttp(L, "PATCH", luaL_checkstring(L, 1), body, opts, callback);
}

// http.request({ method =, url =, body =, headers =, timeout =, only =, insecure = }, callback)
int LuaPlugin::l_http_request(lua_State* L) {
    static const char* const methods[] = {"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", nullptr};
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_settop(L, 2);
    if (lua_getfield(L, 1, "url") != LUA_TSTRING) return luaL_error(L, "http.request: give it a url");  // stays at 3
    int method = 0;
    if (lua_getfield(L, 1, "method") == LUA_TSTRING) {
        const char* given = lua_tostring(L, -1);
        method = -1;
        for (int i = 0; methods[i]; i++) {
            if (strcasecmp(given, methods[i]) == 0) method = i;
        }
        if (method < 0) return luaL_error(L, "http.request: method is GET, POST, PUT, PATCH, DELETE or HEAD");
    } else if (!lua_isnil(L, -1)) {
        return luaL_error(L, "http.request: method is GET, POST, PUT, PATCH, DELETE or HEAD");
    }
    lua_pop(L, 1);
    int bodyIndex = 0;
    // GET and HEAD carry no body.
    if (method != 0 && method != 5) {
        lua_getfield(L, 1, "body");  // stays at 4
        bodyIndex = 4;
    }
    return from(L)->startHttp(L, methods[method], lua_tostring(L, 3), bodyIndex, 1, 2);
}

// ---- net

int LuaPlugin::l_net_online(lua_State* L) {
    bool online = false;
    {
        const NetStatus status = from(L)->services_.netStatus();
        online = status.online;
    }
    lua_pushboolean(L, online);
    return 1;
}

// net.rssi(): signal strength in dBm (closer to 0 is stronger), or nil while offline.
int LuaPlugin::l_net_rssi(lua_State* L) {
    bool online = false;
    int rssi = 0;
    {
        const NetStatus status = from(L)->services_.netStatus();
        online = status.online;
        rssi = status.rssi;
    }
    if (online) {
        lua_pushinteger(L, rssi);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int pushNetText(lua_State* L, PluginServices& services, bool ip) {
    bool online = false;
    int status = LUA_OK;
    {
        const NetStatus net = services.netStatus();
        online = net.online;
        const std::string& text = ip ? net.ip : net.ssid;
        if (online) status = pushStringProtected(L, text.data(), text.size());
    }
    if (status != LUA_OK) return lua_error(L);
    if (!online) lua_pushnil(L);
    return 1;
}

int LuaPlugin::l_net_ssid(lua_State* L) { return pushNetText(L, from(L)->services_, false); }
int LuaPlugin::l_net_ip(lua_State* L) { return pushNetText(L, from(L)->services_, true); }

// ---- device

// device.battery(): { percent, volts, charging }, or nil when there is no battery to read.
int LuaPlugin::l_device_battery(lua_State* L) {
    BatteryStatus battery;
    bool present = false;
    {
        present = from(L)->services_.battery(battery);
    }
    if (!present) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, battery.percent);
    lua_setfield(L, -2, "percent");
    lua_pushnumber(L, (lua_Number)battery.volts);
    lua_setfield(L, -2, "volts");
    lua_pushboolean(L, battery.charging);
    lua_setfield(L, -2, "charging");
    return 1;
}

// device.name(): what the owner called this device.
int LuaPlugin::l_device_name(lua_State* L) {
    int status = LUA_OK;
    {
        const std::string name = from(L)->services_.deviceName();
        status = pushStringProtected(L, name.data(), name.size());
    }
    if (status != LUA_OK) return lua_error(L);
    return 1;
}

// ---- notify

// notify([text], [opts]) for a single-card plugin, card.notify([text], [opts])
// for a card of a plugin with card kinds. opts = { led = color }.
// The upvalue is the card's id, or nil for the global one.
int LuaPlugin::l_notify(lua_State* L) {
    LuaPlugin* self = from(L);
    const bool forCard = lua_type(L, lua_upvalueindex(1)) == LUA_TSTRING;
    if (!forCard && self->multiCard_) {
        return luaL_error(L, "notify: this plugin has several cards. Use card.notify(...) so the right one answers");
    }
    // card:notify("...") works too: drop the card itself.
    if (forCard && lua_istable(L, 1)) {
        lua_pushliteral(L, "notify");
        lua_rawget(L, 1);
        const bool isSelf = lua_tocfunction(L, -1) == l_notify;
        lua_pop(L, 1);
        if (isSelf) lua_remove(L, 1);
    }

    int optsIndex = 2;
    const char* text = "";
    size_t length = 0;
    if (lua_istable(L, 1)) {
        optsIndex = 1;  // notify{ led = "red" }: no text
    } else if (!lua_isnoneornil(L, 1)) {
        text = luaL_checklstring(L, 1, &length);
    }
    bool hasLed = false;
    uint32_t ledRgb = 0;
    if (!lua_isnoneornil(L, optsIndex)) {
        luaL_checktype(L, optsIndex, LUA_TTABLE);
        lua_getfield(L, optsIndex, "led");
        if (!lua_isnil(L, -1)) {
            hasLed = true;
            ledRgb = optColor(L, -1, kBlack);
        }
        lua_pop(L, 1);
    }
    if (length > kMaxNoticeBytes) {
        length = kMaxNoticeBytes;
        while (length > 0 && ((unsigned char)text[length] & 0xC0) == 0x80) length--;
    }

    // Nothing below raises a Lua error, so these may own memory.
    Notice notice;
    notice.text.assign(text, length);
    notice.hasLed = hasLed;
    notice.ledRgb = ledRgb;
    const std::string cardId = forCard ? lua_tostring(L, lua_upvalueindex(1)) : "";
    self->services_.notify(cardId, notice);
    return 0;
}

// ---- choices

namespace {

struct ChoiceList {
    std::vector<Choice>* out;
    const char* problem;
    size_t bytes;
};

// Read the list handed to done(). Runs under lua_pcall with the ChoiceList and
// the list as arguments, because it fills C++ containers while reading Lua values.
int readChoices(lua_State* L) {
    ChoiceList* list = static_cast<ChoiceList*>(lua_touserdata(L, 1));
    const lua_Unsigned count = lua_rawlen(L, 2);
    for (lua_Unsigned i = 1; i <= count && list->out->size() < kMaxChoices; i++) {
        lua_rawgeti(L, 2, (lua_Integer)i);
        const int item = lua_gettop(L);
        // An entry is { value =, label = }, or just the value.
        if (lua_istable(L, item)) {
            lua_getfield(L, item, "value");
            lua_getfield(L, item, "label");
        } else {
            lua_pushvalue(L, item);
            lua_pushnil(L);
        }
        const int valueType = lua_type(L, -2);
        const int labelType = lua_type(L, -1);
        if (valueType != LUA_TSTRING && valueType != LUA_TNUMBER) {
            list->problem = "every choice needs a value: { value = \"...\", label = \"...\" }";
            return 0;
        }
        size_t valueLength = 0, labelLength = 0;
        const char* value = lua_tolstring(L, -2, &valueLength);
        const char* label = value;
        labelLength = valueLength;
        if (labelType == LUA_TSTRING || labelType == LUA_TNUMBER) label = lua_tolstring(L, -1, &labelLength);
        if (valueLength > kMaxChoiceBytes) {
            list->problem = "a choice's value is too long (200 characters at most)";
            return 0;
        }
        if (labelLength > kMaxChoiceBytes) {
            labelLength = kMaxChoiceBytes;
            while (labelLength > 0 && ((unsigned char)label[labelLength] & 0xC0) == 0x80) labelLength--;
        }
        list->bytes += valueLength + labelLength;
        if (list->bytes > kMaxChoiceListBytes) return 0;  // the list ends here
        Choice choice;
        choice.value.assign(value, valueLength);
        choice.label.assign(label, labelLength);
        list->out->push_back(std::move(choice));
        lua_settop(L, 2);
    }
    return 0;
}

}  // namespace

// The `done` a choices function gets: done(list), or done(nil, "what went wrong").
int LuaPlugin::l_choices_done(lua_State* L) {
    LuaPlugin* self = from(L);
    const int requestId = (int)lua_tointeger(L, lua_upvalueindex(1));
    // Answered already, or too late: nothing waits for this any more.
    if (self->pendingChoices_.find(requestId) == self->pendingChoices_.end()) return 0;

    if (!lua_istable(L, 1)) {
        // No list. The second argument may say why.
        const char* why = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : "the plugin has no list to offer";
        self->pendingChoices_.erase(requestId);
        self->services_.choicesDone(requestId, std::vector<Choice>(), std::string(why).substr(0, 200));
        return 0;
    }

    // From here on nothing raises a Lua error: the reading runs under lua_pcall.
    std::vector<Choice> choices;
    ChoiceList list{&choices, nullptr, 0};
    lua_pushcfunction(L, readChoices);
    lua_pushlightuserdata(L, &list);
    lua_pushvalue(L, 1);
    const int status = lua_pcall(L, 2, 0, 0);
    if (status != LUA_OK) {
        lua_pop(L, 1);
        list.problem = "not enough memory for the list";
    }
    self->pendingChoices_.erase(requestId);
    if (list.problem) {
        self->services_.choicesDone(requestId, std::vector<Choice>(), list.problem);
    } else {
        self->services_.choicesDone(requestId, choices, "");
    }
    return 0;
}

// ---- timer

// timer.after(seconds, fn) and timer.every(seconds, fn) -> a handle with :cancel()
int LuaPlugin::startTimer(lua_State* L, bool repeat) {
    double seconds = (double)luaL_checknumber(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (!(seconds >= 0)) seconds = 0;  // also NaN
    if (seconds > kMaxTimerSeconds) seconds = kMaxTimerSeconds;
    uint32_t ms = (uint32_t)(seconds * 1000.0 + 0.5);
    if (repeat && ms < kMinTimerMs) ms = kMinTimerMs;
    if ((int)timers_.size() >= limits_.maxTimers) {
        return luaL_error(L, "timer: too many timers (max %d). Cancel the ones that are done with", limits_.maxTimers);
    }

    // Everything that can raise comes first; the bookkeeping after it cannot fail.
    const int id = nextTimerId_++;
    lua_createtable(L, 0, 1);
    luaL_setmetatable(L, kTimerMeta);
    lua_pushinteger(L, id);
    lua_setfield(L, -2, "id");
    lua_pushvalue(L, 2);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    timers_.push_back(Timer{id, ref, services_.millis() + ms, ms, repeat});
    return 1;
}

int LuaPlugin::l_timer_after(lua_State* L) { return from(L)->startTimer(L, false); }
int LuaPlugin::l_timer_every(lua_State* L) { return from(L)->startTimer(L, true); }

// handle:cancel() or timer.cancel(handle). Cancelling twice, or after it ran, is fine.
int LuaPlugin::l_timer_cancel(lua_State* L) {
    LuaPlugin* self = from(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushliteral(L, "id");
    lua_rawget(L, 1);
    const lua_Integer id = lua_tointeger(L, -1);
    lua_pop(L, 1);
    for (auto it = self->timers_.begin(); it != self->timers_.end(); ++it) {
        if (it->id != id) continue;
        luaL_unref(L, LUA_REGISTRYINDEX, it->ref);
        self->timers_.erase(it);
        break;
    }
    return 0;
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

// ---- local time

// The zone a time function works in: the one given as argument `index`, or the
// device's when that is missing or "".
static void zoneArgument(lua_State* L, PluginServices& services, int index, TimeZone& zone) {
    const char* given = luaL_optstring(L, index, "");
    if (*given) {
        if (!zone.parse(given)) {
            luaL_argerror(L, index, "not a POSIX time zone (they look like \"CET-1CEST,M3.5.0,M10.5.0/3\")");
        }
        return;
    }
    const std::string device = services.timeZone();
    zone.parse(device.c_str());  // UTC if the device's setting makes no sense
}

// The moment a time function is about: argument `index`, or now.
// @return false when it is "now" and the clock has not been set yet.
static bool timeArgument(lua_State* L, PluginServices& services, int index, int64_t& out) {
    if (lua_isnoneornil(L, index)) {
        out = services.epochSeconds();
        return out != 0;
    }
    // Whole numbers are 32 bits wide here; a time past 2038 arrives as a float.
    if (lua_isinteger(L, index)) {
        out = (int64_t)lua_tointeger(L, index);
        return true;
    }
    const double number = (double)luaL_checknumber(L, index);
    if (!(number >= (double)kMinTime && number <= (double)kMaxTime)) {
        luaL_argerror(L, index, "not a time (seconds since 1970)");
    }
    out = (int64_t)floor(number);
    return true;
}

// time.date([t], [tz]) -> { year, month, day, hour, min, sec, wday, yday, isdst, offset, zone }
int LuaPlugin::l_time_date(lua_State* L) {
    LuaPlugin* self = from(L);
    TimeZone zone;
    int64_t when = 0;
    const bool known = timeArgument(L, self->services_, 1, when);
    zoneArgument(L, self->services_, 2, zone);
    if (!known) {
        lua_pushnil(L);  // no clock yet
        return 1;
    }
    const CivilTime t = zone.toCivil(when);
    lua_createtable(L, 0, 11);
    const struct {
        const char* key;
        int value;
    } fields[] = {
        {"year", t.year}, {"month", t.month}, {"day", t.day},          {"hour", t.hour},        {"min", t.minute},
        {"sec", t.second}, {"wday", t.weekday + 1}, {"yday", t.yearDay + 1}, {"offset", (int)t.offset},
    };
    for (const auto& field : fields) {
        lua_pushinteger(L, field.value);
        lua_setfield(L, -2, field.key);
    }
    lua_pushboolean(L, t.dst);
    lua_setfield(L, -2, "isdst");
    lua_pushstring(L, t.zone);
    lua_setfield(L, -2, "zone");
    return 1;
}

// time.format(fmt, [t], [tz]) -> text, e.g. time.format("%H:%M") -> "20:49"
int LuaPlugin::l_time_format(lua_State* L) {
    LuaPlugin* self = from(L);
    const char* format = luaL_checkstring(L, 1);
    TimeZone zone;
    int64_t when = 0;
    const bool known = timeArgument(L, self->services_, 2, when);
    zoneArgument(L, self->services_, 3, zone);
    if (!known) {
        lua_pushnil(L);
        return 1;
    }
    int status = LUA_OK;
    {
        const std::string text = formatTime(format, zone.toCivil(when));
        status = pushStringProtected(L, text.data(), text.size());
    }
    if (status != LUA_OK) return lua_error(L);
    return 1;
}

// time.zone() -> the device's time zone as a POSIX TZ string
int LuaPlugin::l_time_zone(lua_State* L) {
    int status = LUA_OK;
    {
        std::string device = from(L)->services_.timeZone();
        if (device.empty()) device = "UTC0";
        status = pushStringProtected(L, device.data(), device.size());
    }
    if (status != LUA_OK) return lua_error(L);
    return 1;
}

// time.offset([t], [tz]) -> seconds east of UTC
int LuaPlugin::l_time_offset(lua_State* L) {
    LuaPlugin* self = from(L);
    TimeZone zone;
    int64_t when = 0;
    timeArgument(L, self->services_, 1, when);
    zoneArgument(L, self->services_, 2, zone);
    lua_pushinteger(L, zone.offsetAt(when));
    return 1;
}

// time.make({ year, month, day, [hour], [min], [sec], [isdst] }, [tz]) -> seconds since 1970.
// The other way round from time.date: what a clock in the zone shows, to the moment.
int LuaPlugin::l_time_make(lua_State* L) {
    LuaPlugin* self = from(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    TimeZone zone;
    zoneArgument(L, self->services_, 2, zone);
    const struct {
        const char* key;
        bool required;
    } fields[] = {{"year", true}, {"month", true}, {"day", true}, {"hour", false}, {"min", false}, {"sec", false}};
    int64_t values[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 6; i++) {
        const int type = lua_getfield(L, 1, fields[i].key);
        if (type == LUA_TNIL && !fields[i].required) {
            lua_pop(L, 1);
            continue;
        }
        const lua_Number number = lua_tonumber(L, -1);
        // Far enough either way for "40 days from now" and the like, and no further.
        if (type != LUA_TNUMBER || !(number >= -1.0e6f && number <= 1.0e6f)) {
            return luaL_error(L, "time.make: `%s` is %s", fields[i].key,
                              type == LUA_TNIL ? "missing" : type == LUA_TNUMBER ? "out of range" : "not a number");
        }
        values[i] = (int64_t)floor((double)number);
        lua_pop(L, 1);
    }
    int dst = -1;
    if (lua_getfield(L, 1, "isdst") == LUA_TBOOLEAN) dst = lua_toboolean(L, -1) ? 1 : 0;
    lua_pop(L, 1);

    const int64_t when = zone.fromCivil(values[0], (int)values[1], (int)values[2], values[3], values[4], values[5], dst);
    if (when < kMinTime || when > kMaxTime) return luaL_error(L, "time.make: that is outside the years 1 to 9999");
    // Whole numbers are 32 bits wide here; a time past 2038 goes back as a float, as time.date takes it.
    if (when >= INT32_MIN && when <= INT32_MAX) {
        lua_pushinteger(L, (lua_Integer)when);
    } else {
        lua_pushnumber(L, (lua_Number)when);
    }
    return 1;
}

int LuaPlugin::l_input_held(lua_State* L) {
    const int which = luaL_checkoption(L, 1, nullptr, kButtonNames);
    lua_pushboolean(L, from(L)->services_.buttonHeld((Button)which));
    return 1;
}

}  // namespace deskhog
