#ifndef MF_Lua_MsgPack_h
#define MF_Lua_MsgPack_h


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
    int luaopen_cmsgpack_safe(lua_State *L);
#ifdef __cplusplus
}
#endif

#endif /* MF_Lua_MsgPack_h */
