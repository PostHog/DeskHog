#include "LuaJson.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.hpp"

namespace deskhog {

namespace {

const int kMaxDepth = 24;
const int kMaxSkipDepth = 64;  // parts that are skipped build nothing, so they may nest deeper

// The encoder writes into host memory, outside the plugin's Lua heap cap.
const size_t kMaxEncodedBytes = 64 * 1024;

struct Parser {
    lua_State* L;
    const char* p;
    const char* end;
    const char* error;
};

void skipSpace(Parser& s) {
    while (s.p < s.end && (*s.p == ' ' || *s.p == '\t' || *s.p == '\n' || *s.p == '\r')) s.p++;
}

bool fail(Parser& s, const char* message) {
    if (!s.error) s.error = message;
    return false;
}

// `filter` is 0 to keep everything, or the stack index of a filter value.
bool parseValue(Parser& s, int depth, bool inArray, int filter);
bool skipValue(Parser& s, int depth);

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parseHex4(Parser& s, unsigned& out) {
    if (s.end - s.p < 4) return false;
    out = 0;
    for (int i = 0; i < 4; i++) {
        const int d = hexDigit(s.p[i]);
        if (d < 0) return false;
        out = (out << 4) | (unsigned)d;
    }
    s.p += 4;
    return true;
}

void addUtf8(luaL_Buffer& b, unsigned cp) {
    if (cp < 0x80) {
        luaL_addchar(&b, (char)cp);
    } else if (cp < 0x800) {
        luaL_addchar(&b, (char)(0xC0 | (cp >> 6)));
        luaL_addchar(&b, (char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        luaL_addchar(&b, (char)(0xE0 | (cp >> 12)));
        luaL_addchar(&b, (char)(0x80 | ((cp >> 6) & 0x3F)));
        luaL_addchar(&b, (char)(0x80 | (cp & 0x3F)));
    } else {
        luaL_addchar(&b, (char)(0xF0 | (cp >> 18)));
        luaL_addchar(&b, (char)(0x80 | ((cp >> 12) & 0x3F)));
        luaL_addchar(&b, (char)(0x80 | ((cp >> 6) & 0x3F)));
        luaL_addchar(&b, (char)(0x80 | (cp & 0x3F)));
    }
}

// Expects s.p just past the opening quote. Pushes the string.
bool parseString(Parser& s) {
    // Fast path: no escapes, so the bytes can be pushed as they are.
    const char* start = s.p;
    while (s.p < s.end && *s.p != '"' && *s.p != '\\') {
        if ((unsigned char)*s.p < 0x20) return fail(s, "control character in string");
        s.p++;
    }
    if (s.p >= s.end) return fail(s, "unterminated string");
    if (*s.p == '"') {
        lua_pushlstring(s.L, start, (size_t)(s.p - start));
        s.p++;
        return true;
    }

    luaL_Buffer b;
    luaL_buffinit(s.L, &b);
    luaL_addlstring(&b, start, (size_t)(s.p - start));
    while (s.p < s.end) {
        const char c = *s.p++;
        if (c == '"') {
            luaL_pushresult(&b);
            return true;
        }
        if ((unsigned char)c < 0x20) break;
        if (c != '\\') {
            luaL_addchar(&b, c);
            continue;
        }
        if (s.p >= s.end) break;
        const char e = *s.p++;
        switch (e) {
            case '"': luaL_addchar(&b, '"'); break;
            case '\\': luaL_addchar(&b, '\\'); break;
            case '/': luaL_addchar(&b, '/'); break;
            case 'b': luaL_addchar(&b, '\b'); break;
            case 'f': luaL_addchar(&b, '\f'); break;
            case 'n': luaL_addchar(&b, '\n'); break;
            case 'r': luaL_addchar(&b, '\r'); break;
            case 't': luaL_addchar(&b, '\t'); break;
            case 'u': {
                unsigned cp = 0;
                if (!parseHex4(s, cp)) goto bad;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    // High surrogate: a low surrogate has to follow.
                    unsigned low = 0;
                    if (s.end - s.p < 6 || s.p[0] != '\\' || s.p[1] != 'u') goto bad;
                    s.p += 2;
                    if (!parseHex4(s, low) || low < 0xDC00 || low > 0xDFFF) goto bad;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    goto bad;
                }
                addUtf8(b, cp);
                break;
            }
            default:
                goto bad;
        }
    }
bad:
    // Leave the stack as we found it: the buffer may hold a temporary on it.
    luaL_pushresult(&b);
    lua_pop(s.L, 1);
    return fail(s, "bad string");
}

bool parseNumber(Parser& s) {
    const char* start = s.p;
    bool isInteger = true;
    if (s.p < s.end && *s.p == '-') s.p++;
    if (s.p >= s.end || *s.p < '0' || *s.p > '9') return fail(s, "bad number");
    while (s.p < s.end && *s.p >= '0' && *s.p <= '9') s.p++;
    if (s.p < s.end && *s.p == '.') {
        isInteger = false;
        s.p++;
        if (s.p >= s.end || *s.p < '0' || *s.p > '9') return fail(s, "bad number");
        while (s.p < s.end && *s.p >= '0' && *s.p <= '9') s.p++;
    }
    if (s.p < s.end && (*s.p == 'e' || *s.p == 'E')) {
        isInteger = false;
        s.p++;
        if (s.p < s.end && (*s.p == '+' || *s.p == '-')) s.p++;
        if (s.p >= s.end || *s.p < '0' || *s.p > '9') return fail(s, "bad number");
        while (s.p < s.end && *s.p >= '0' && *s.p <= '9') s.p++;
    }

    char buf[48];
    const size_t len = (size_t)(s.p - start);
    if (len >= sizeof(buf)) return fail(s, "number too long");
    memcpy(buf, start, len);
    buf[len] = '\0';

    if (isInteger) {
        const long long v = strtoll(buf, nullptr, 10);
        if (v >= (long long)LUA_MININTEGER && v <= (long long)LUA_MAXINTEGER) {
            lua_pushinteger(s.L, (lua_Integer)v);
            return true;
        }
    }
    lua_pushnumber(s.L, (lua_Number)strtod(buf, nullptr));
    return true;
}

bool parseLiteral(Parser& s, const char* word) {
    const size_t len = strlen(word);
    if ((size_t)(s.end - s.p) < len || memcmp(s.p, word, len) != 0) return fail(s, "unexpected token");
    s.p += len;
    return true;
}

bool parseArray(Parser& s, int depth, int filter) {
    // The filter for the items: the filter's first entry, or the filter itself.
    int itemFilter = 0;
    if (filter != 0) {
        if (lua_rawgeti(s.L, filter, 1) == LUA_TNIL) {
            lua_pop(s.L, 1);
            lua_pushvalue(s.L, filter);
        }
        itemFilter = lua_istable(s.L, -1) ? lua_gettop(s.L) : 0;  // `true` keeps everything
    }
    lua_createtable(s.L, 4, 0);
    const int table = lua_gettop(s.L);
    // On every way out: leave just the table (or nothing), not the item filter under it.
    const auto finish = [&s, filter, table](bool ok) {
        if (!ok) lua_settop(s.L, table - 1);
        if (filter != 0) lua_remove(s.L, ok ? table - 1 : lua_gettop(s.L));
        return ok;
    };
    skipSpace(s);
    if (s.p < s.end && *s.p == ']') {
        s.p++;
        return finish(true);
    }
    lua_Integer n = 0;
    for (;;) {
        if (!parseValue(s, depth + 1, true, itemFilter)) return finish(false);
        lua_rawseti(s.L, table, ++n);
        skipSpace(s);
        if (s.p >= s.end) break;
        if (*s.p == ',') {
            s.p++;
            continue;
        }
        if (*s.p == ']') {
            s.p++;
            return finish(true);
        }
        break;
    }
    finish(false);
    return fail(s, "expected ',' or ']'");
}

bool parseObject(Parser& s, int depth, int filter) {
    lua_createtable(s.L, 0, 4);
    const int table = lua_gettop(s.L);
    skipSpace(s);
    if (s.p < s.end && *s.p == '}') {
        s.p++;
        return true;
    }
    for (;;) {
        skipSpace(s);
        if (s.p >= s.end || *s.p != '"') break;
        s.p++;
        if (!parseString(s)) {
            lua_pop(s.L, 1);
            return false;
        }
        skipSpace(s);
        if (s.p >= s.end || *s.p != ':') {
            lua_pop(s.L, 1);
            break;
        }
        s.p++;
        if (filter != 0) {
            // Is this key wanted? Its entry in the filter is the filter for its value.
            lua_pushvalue(s.L, -1);
            if (lua_rawget(s.L, filter) == LUA_TNIL) {
                lua_pop(s.L, 2);  // the nil and the key
                if (!skipValue(s, depth + 1)) {
                    lua_settop(s.L, table - 1);
                    return false;
                }
            } else {
                const int valueFilter = lua_istable(s.L, -1) ? lua_gettop(s.L) : 0;
                if (!parseValue(s, depth + 1, false, valueFilter)) {
                    lua_settop(s.L, table - 1);
                    return false;
                }
                lua_remove(s.L, -2);  // the value's filter
                lua_rawset(s.L, table);
            }
        } else {
            if (!parseValue(s, depth + 1, false, 0)) {
                lua_pop(s.L, 2);
                return false;
            }
            lua_rawset(s.L, table);
        }
        skipSpace(s);
        if (s.p >= s.end) break;
        if (*s.p == ',') {
            s.p++;
            continue;
        }
        if (*s.p == '}') {
            s.p++;
            return true;
        }
        break;
    }
    lua_pop(s.L, 1);
    return fail(s, "bad object");
}

// Read past one value without building anything.
bool skipValue(Parser& s, int depth) {
    if (depth > kMaxSkipDepth) return fail(s, "nested too deeply");
    skipSpace(s);
    if (s.p >= s.end) return fail(s, "unexpected end");
    const char c = *s.p;
    if (c == '"') {
        s.p++;
        while (s.p < s.end && *s.p != '"') {
            if (*s.p == '\\') s.p++;  // whatever is escaped, it is not the closing quote
            s.p++;
        }
        if (s.p >= s.end) return fail(s, "unterminated string");
        s.p++;
        return true;
    }
    if (c == '{' || c == '[') {
        const char close = c == '{' ? '}' : ']';
        s.p++;
        skipSpace(s);
        if (s.p < s.end && *s.p == close) {
            s.p++;
            return true;
        }
        for (;;) {
            if (c == '{') {
                skipSpace(s);
                if (s.p >= s.end || *s.p != '"' || !skipValue(s, depth + 1)) return fail(s, "bad object");
                skipSpace(s);
                if (s.p >= s.end || *s.p != ':') return fail(s, "bad object");
                s.p++;
            }
            if (!skipValue(s, depth + 1)) return false;
            skipSpace(s);
            if (s.p >= s.end) return fail(s, "unexpected end");
            if (*s.p == ',') {
                s.p++;
                continue;
            }
            if (*s.p == close) {
                s.p++;
                return true;
            }
            return fail(s, c == '{' ? "bad object" : "expected ',' or ']'");
        }
    }
    // A number, true, false or null: everything up to the next delimiter.
    const char* start = s.p;
    while (s.p < s.end && *s.p != ',' && *s.p != '}' && *s.p != ']' && *s.p != ' ' && *s.p != '\n' && *s.p != '\r' &&
           *s.p != '\t') {
        s.p++;
    }
    return s.p > start ? true : fail(s, "unexpected character");
}

bool parseValue(Parser& s, int depth, bool inArray, int filter) {
    if (depth > kMaxDepth) return fail(s, "nested too deeply");
    if (!lua_checkstack(s.L, 10)) return fail(s, "out of stack");
    skipSpace(s);
    if (s.p >= s.end) return fail(s, "unexpected end");
    const char c = *s.p;
    if (c == '{') {
        s.p++;
        return parseObject(s, depth, filter);
    }
    if (c == '[') {
        s.p++;
        return parseArray(s, depth, filter);
    }
    if (c == '"') {
        s.p++;
        return parseString(s);
    }
    if (c == 't') {
        if (!parseLiteral(s, "true")) return false;
        lua_pushboolean(s.L, 1);
        return true;
    }
    if (c == 'f') {
        if (!parseLiteral(s, "false")) return false;
        lua_pushboolean(s.L, 0);
        return true;
    }
    if (c == 'n') {
        if (!parseLiteral(s, "null")) return false;
        if (inArray) {
            jsonPushNull(s.L);
        } else {
            lua_pushnil(s.L);
        }
        return true;
    }
    if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(s);
    return fail(s, "unexpected character");
}

void encodeString(const char* str, size_t len, std::string& out) {
    out.push_back('"');
    for (size_t i = 0; i < len; i++) {
        const unsigned char c = (unsigned char)str[i];
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back((char)c);
                }
        }
    }
    out.push_back('"');
}

bool encodeValue(lua_State* L, int index, std::string& out, int depth, const char** error) {
    if (depth > kMaxDepth) {
        *error = "nested too deeply";
        return false;
    }
    if (!lua_checkstack(L, 4)) {
        *error = "out of stack";
        return false;
    }
    if (out.size() > kMaxEncodedBytes) {
        *error = "result is larger than 64 KB";
        return false;
    }
    index = lua_absindex(L, index);
    switch (lua_type(L, index)) {
        case LUA_TNIL:
            out += "null";
            return true;
        case LUA_TBOOLEAN:
            out += lua_toboolean(L, index) ? "true" : "false";
            return true;
        case LUA_TLIGHTUSERDATA:
            if (lua_touserdata(L, index) == nullptr) {
                out += "null";
                return true;
            }
            *error = "cannot encode userdata";
            return false;
        case LUA_TNUMBER: {
            char buf[40];
            if (lua_isinteger(L, index)) {
                snprintf(buf, sizeof(buf), "%lld", (long long)lua_tointeger(L, index));
            } else {
                const double v = (double)lua_tonumber(L, index);
                if (isnan(v) || isinf(v)) {
                    out += "null";
                    return true;
                }
                // The shortest form that reads back as the same number: 0.1 stays
                // "0.1", and a 32-bit float never needs more than nine digits.
                for (int digits = 7; digits <= 9; digits++) {
                    snprintf(buf, sizeof(buf), "%.*g", digits, v);
                    if ((lua_Number)strtod(buf, nullptr) == lua_tonumber(L, index)) break;
                }
            }
            out += buf;
            return true;
        }
        case LUA_TSTRING: {
            size_t len = 0;
            const char* str = lua_tolstring(L, index, &len);
            if (out.size() + len > kMaxEncodedBytes) {
                *error = "result is larger than 64 KB";
                return false;
            }
            encodeString(str, len, out);
            return true;
        }
        case LUA_TTABLE: {
            const lua_Unsigned n = lua_rawlen(L, index);
            if (n > 0) {
                out.push_back('[');
                for (lua_Unsigned i = 1; i <= n; i++) {
                    if (i > 1) out.push_back(',');
                    lua_rawgeti(L, index, (lua_Integer)i);
                    const bool ok = encodeValue(L, -1, out, depth + 1, error);
                    lua_pop(L, 1);
                    if (!ok) return false;
                }
                out.push_back(']');
                return true;
            }
            out.push_back('{');
            bool first = true;
            lua_pushnil(L);
            while (lua_next(L, index) != 0) {
                // Only string keys make sense in a JSON object.
                if (lua_type(L, -2) == LUA_TSTRING) {
                    if (!first) out.push_back(',');
                    first = false;
                    size_t klen = 0;
                    const char* key = lua_tolstring(L, -2, &klen);
                    encodeString(key, klen, out);
                    out.push_back(':');
                    if (!encodeValue(L, -1, out, depth + 1, error)) {
                        lua_pop(L, 2);
                        return false;
                    }
                }
                lua_pop(L, 1);
            }
            out.push_back('}');
            return true;
        }
        default:
            *error = "cannot encode this type";
            return false;
    }
}

}  // namespace

void jsonPushNull(lua_State* L) { lua_pushlightuserdata(L, nullptr); }

bool jsonDecode(lua_State* L, const char* text, size_t length, const char** error) {
    return jsonDecodeFiltered(L, text, length, 0, error);
}

bool jsonDecodeFiltered(lua_State* L, const char* text, size_t length, int filter, const char** error) {
    Parser s{L, text, text + length, nullptr};
    const int top = lua_gettop(L);
    // Only a table narrows anything down; `true` (or nothing) keeps it all.
    filter = (filter != 0 && lua_istable(L, filter)) ? lua_absindex(L, filter) : 0;
    bool ok = parseValue(s, 0, false, filter);
    if (ok) {
        skipSpace(s);
        if (s.p != s.end) {
            ok = false;
            s.error = "trailing characters";
        }
    }
    if (!ok) {
        lua_settop(L, top);
        if (error) *error = s.error ? s.error : "invalid JSON";
        return false;
    }
    return true;
}

bool jsonEncode(lua_State* L, int index, std::string& out, const char** error) {
    const char* message = nullptr;
    const bool ok = encodeValue(L, index, out, 0, &message);
    if (!ok && error) *error = message ? message : "cannot encode";
    return ok;
}

}  // namespace deskhog
