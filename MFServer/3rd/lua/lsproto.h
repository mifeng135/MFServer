/*
** lsproto.h - Cross-VM Read-Only Shared Table Pool
**
** Executes a Lua script in an internal master lua_State; the returned
** table (and every reachable sub-table / string) is marked with SHAREDBIT
** so no state's GC will collect or traverse it.  Other lua_States push
** the *same* Table* via luaP_querytable() - zero-copy, one physical copy.
**
** The shared table is READ-ONLY.  Writing to it is undefined behavior.
*/

#ifndef lsproto_h
#define lsproto_h

#include "lua.h"

void         luaP_init(void);
void         luaP_shutdown(void);

int          luaP_sharetable(const char *name, const char *filename);
int          luaP_updatetable(const char *name, const char *filename);
int          luaP_querytable(lua_State *L, const char *name);
unsigned int luaP_generation(const char *name);
int          luaP_sharetablecount(void);

unsigned int luaP_masterseed(void);

#endif
