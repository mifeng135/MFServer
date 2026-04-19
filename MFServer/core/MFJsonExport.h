#ifndef lua_json_export_h
#define lua_json_export_h


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
    int luaopen_cjson(lua_State *L);
#ifdef __cplusplus
}
#endif

#endif /* lua_json_export_h */