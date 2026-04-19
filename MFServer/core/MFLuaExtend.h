#ifndef lua_extend_h
#define lua_extend_h


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
    int luaopen_extend(lua_State *L);
#ifdef __cplusplus
}
#endif

#endif /* lua_extend_h */
