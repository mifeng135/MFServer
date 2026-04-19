#include "MFLuaExtend.h"

#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

int
lua_table_clear(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        lua_pop(L, 1);
        lua_pushvalue(L, -1);
        lua_pushnil(L);
        lua_rawset(L, 1);
    }
    return 0;
}

int
lua_string_split(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    const char *sep = luaL_checkstring(L, 2);
    lua_newtable(L);
    const char *e;
    int i = 1;
    while ((e = strchr(s, *sep))) {
        lua_pushlstring(L, s, e - s);
        lua_rawseti(L, -2, i++);
        s = e + 1;
    }
    lua_pushstring(L, s);
    lua_rawseti(L, -2, i);
    return 1;
}

int
lua_uint64_to_unique_string(lua_State *L) {
    uint64_t value = (uint64_t)luaL_checkinteger(L, 1);
    static const char CHARS[] = "kL9mN0pQ2rS4tU6vW8xY0zA1bC3dE5fG7hI9jK1lM3nO5pQ7rS9tU1vW3xY5za";
    static const size_t CHARS_LEN = sizeof(CHARS) - 1;
    static const uint64_t SCRAMBLE_KEY = 0x9E3779B97F4A7C15ULL;

    value ^= SCRAMBLE_KEY;
    value = value << 32 | value >> 32;
    char result[13] = {0};
    for (int i = 0; i < 12; i++) {
        result[i] = CHARS[0];
    }
    result[12] = '\0';
    
    int pos = 11;
    uint64_t val = value;
    if (val == 0) {
        result[pos] = CHARS[0];
    } else {
        while (val > 0 && pos >= 0) {
            size_t idx = val % CHARS_LEN;
            result[pos] = CHARS[idx];
            val /= CHARS_LEN;
            pos--;
        }
    }
    lua_pushlstring(L, result, 12);
    return 1;
}

int hashString(lua_State* L) {
    const char *str = luaL_checkstring(L, 1);
    uint32_t hash = 0;
    size_t len = strlen(str);
    for (size_t i = 0; i < len; i++) {
        hash = (hash * 31 + (unsigned char)str[i]) % 2147483648U;
    }
    lua_pushnumber(L, hash);
    return 1;
}

int
lua_gen_cache_key(lua_State *L) {
    int n = lua_gettop(L);
    if (n < 1) {
        return luaL_error(L, "genCacheKey: expected configName[, ...]");
    }
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (int i = 1; i <= n; i++) {
        lua_pushvalue(L, i);
        luaL_addvalue(&b);
    }
    luaL_pushresult(&b);
    return 1;
}

int
registerExtendFunc(lua_State *L) {
    luaL_Reg l[] = {
        { "clear", lua_table_clear },
        { "split", lua_string_split },
        { "uint64Encode", lua_uint64_to_unique_string },
        { "strHash", hashString },
        { "genCacheKey", lua_gen_cache_key },
        { NULL, NULL },
    };
    luaL_newlib(L, l);
    return 1;
}

int luaopen_extend(lua_State *L) {
    luaL_requiref(L, "MF.extend", registerExtendFunc, 1);
    lua_pop(L, 1);
    return 1;
}




