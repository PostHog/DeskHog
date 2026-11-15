#pragma once

#include <stddef.h>
#include <string>

struct lua_State;

namespace deskhog {

/**
 * Parse JSON text and push the value on the Lua stack.
 *
 * Objects and arrays become tables (arrays are 1-based). `null` becomes nil,
 * except inside arrays where it becomes the `json.null` marker so the indices
 * of later items stay put. Whole numbers that fit become integers.
 *
 * May raise a Lua memory error, so call it from protected code.
 *
 * @return true with one value pushed, or false with nothing pushed and
 *         *error pointing at a static description.
 */
bool jsonDecode(lua_State* L, const char* text, size_t length, const char** error);

/**
 * Like jsonDecode, but keeps only what the filter at stack index `filter` asks
 * for, so a large answer costs little memory. The filter mirrors the shape of
 * the data:
 *
 *   true              keep everything from here down
 *   { name = true }   in an object, keep only these keys, each with its own filter
 *   { { ... } }       in a list, the first entry is the filter for every item
 *
 * A table filter that meets a list and has no first entry is applied to each
 * item as it is, which lets one filter cover "an object or a list of them".
 * Values that are not objects or lists are kept whenever their key was.
 */
bool jsonDecodeFiltered(lua_State* L, const char* text, size_t length, int filter, const char** error);

/**
 * Serialise the Lua value at `index` as JSON. Never raises.
 * @return false if the value holds something JSON cannot express.
 */
bool jsonEncode(lua_State* L, int index, std::string& out, const char** error);

/** Push the `json.null` marker. */
void jsonPushNull(lua_State* L);

}  // namespace deskhog
