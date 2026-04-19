#ifndef __PBC_EXPORT_LUA_H__
#define __PBC_EXPORT_LUA_H__

#ifdef __cplusplus
extern "C" {
#endif
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
	int luaopen_pb_conv(lua_State *L);
	int luaopen_pb_unsafe(lua_State *L);
	int luaopen_pb(lua_State *L);
	int luaopen_pb_slice(lua_State *L);
    int luaopen_pb_buffer(lua_State *L);
#ifdef __cplusplus
}
#endif

#endif
