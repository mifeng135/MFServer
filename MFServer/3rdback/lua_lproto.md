# lsproto - Cross-VM Shared Table Patch for Lua 5.4.3

Base: Lua 5.4.3 official release
Patch: lsproto shared table pool - allows multiple independent lua_State to
       share the same read-only Table object in memory (zero-copy).

---

## Modified Lua Core Files (7 files)

### 1. `lgc.h` - GC shared bit + barrier skip

- Added `SHAREDBIT` (bit 7) macro, `isshared(o)`, `makeshared(o)`
- Modified `luaC_barrier`, `luaC_barrierback`, `luaC_objbarrier` to skip shared objects

```
SHAREDBIT = 7
isshared(o)   -> testbit(o->marked, SHAREDBIT)
makeshared(o) -> l_setbit(o->marked, SHAREDBIT)

luaC_barrier:     added `&& !isshared(gcvalue(v))`
luaC_barrierback: added `&& !isshared(gcvalue(v))`
luaC_objbarrier:  added `&& !isshared(o)`
```

### 2. `lgc.c` - GC mark/sweep/free skip shared objects

- `markvalue` macro: skip if `isshared`
- `markkey` macro: skip if `isshared`
- `markobject` macro: skip if `isshared`
- `freeobj()`: added `if (isshared(o)) return;` guard at top
- `sweeplist()`: added `if (isshared(curr)) { p = &curr->next; continue; }`

### 3. `lua.h` - New public API declaration

- Added `lua_pushsharedtable(L, void *tbl)` declaration

```c
LUA_API void (lua_pushsharedtable) (lua_State *L, void *tbl);
```

### 4. `lstate.c` - `lua_pushsharedtable` implementation

- Implements `lua_pushsharedtable`: sets a Table* TValue onto L's stack
  without linking it into L's allgc (GC skips it via SHAREDBIT)

```c
LUA_API void lua_pushsharedtable (lua_State *L, void *tbl) {
  Table *t = (Table *)tbl;
  lua_lock(L);
  sethvalue(L, s2v(L->top), t);
  api_incr_top(L);
  lua_unlock(L);
}
```

### 5. `ltable.c` - Cross-state string key lookup fix

Problem: Lua 5.4 short strings use per-state hash seed + pointer equality.
Different lua_States have different seeds and different TString* for the same
content, causing all string key lookups in shared tables to fail.

Fix:

- Added `#include "lsproto.h"`
- `mainposition()`: for shared tables, recompute string hash using
  `luaS_hash(content, len, luaP_masterseed())` instead of per-state `ts->hash`
  (both `LUA_VSHRSTR` and `LUA_VLNGSTR` cases)
- `equalkey()`: added `Table *t` parameter; for shared tables, short strings
  compare by `memcmp(content)` instead of pointer equality
- `luaH_getshortstr()`: added shared-table fast path with rehash +
  content comparison

### 6. `lsproto.c` - Shared table pool implementation (NEW FILE)

Core features:
- Internal master `lua_State` (`g_master`) for executing config scripts
- `SRWLOCK` / `pthread_rwlock` for thread-safe access
- `STEntry` hash table pool (128 buckets)
- `sp_mark_shared_obj()`: recursive SHAREDBIT marking on Table + all
  reachable sub-tables and strings
- `luaP_masterseed()`: exposes g_master's hash seed for ltable.c

Public API:

| Function                    | Description                                 |
|-----------------------------|---------------------------------------------|
| `luaP_init()`               | Create master state                         |
| `luaP_shutdown()`           | Clear SHAREDBIT on all objects, close master |
| `luaP_sharetable(name,file)`| Load script, mark returned table shared     |
| `luaP_updatetable(name,file)`| Hot-reload: load new version, old stays alive |
| `luaP_querytable(L, name)`  | Push shared Table* onto L's stack (zero-copy) |
| `luaP_generation(name)`     | Version counter (incremented on update)     |
| `luaP_sharetablecount()`    | Number of shared tables in pool             |
| `luaP_masterseed()`         | Master state's hash seed                    |

### 7. `lsproto.h` - Public API header (NEW FILE)

Declares all `luaP_*` functions above.

---

## Modified Build Files (2 files)

### 8. `CMakeLists.txt`

- Added `lsproto.c` to `LUA_CORE_SOURCES`

### 9. `makefile`

- Added `lsproto.o` to `CORE_O`
- Added dependency rule for `lsproto.o`

---

## Modified Project Files (outside Lua source)

### 10. `3rd/lua/lsproto.h`

- Copy of lsproto.h (without LUA_API prefix) for C++ callers linking lua.dll

### 11. `3rd/sol/sol.hpp`

- Added `#include <lsproto.h>` in the `extern "C"` Lua include block

### 12. `core/MFLuaExport.cpp`

- Added Lua bindings:
  - `MFUtil.luaShareTable(name, filename)` -> `luaP_sharetable`
  - `MFUtil.luaUpdateTable(name, filename)` -> `luaP_updatetable`
  - `MFUtil.luaQueryTable(name)` -> `luaP_querytable`
  - `MFUtil.luaTableGeneration(name)` -> `luaP_generation`

### 13. `core/MFApplication.cpp`

- Calls `luaP_init()` during startup
- Calls `luaP_shutdown()` during shutdown

### 14. `coreScript/api.lua`

- Added EmmyLua `@field` annotations for the 4 new MFUtil functions

---

## Lua Usage Example

```lua
-- main.lua (startup, runs once)
MFUtil.luaShareTable("Global", MFUtil.getCurrentPath() .. "/script/Config/Global.lua")

-- any VM / service
local cfg = MFUtil.luaQueryTable("Global")
print(cfg.data)  -- reads from shared memory, zero-copy

-- hot-reload
MFUtil.luaUpdateTable("Global", MFUtil.getCurrentPath() .. "/script/Config/Global.lua")
-- each VM re-queries to get the new version
cfg = MFUtil.luaQueryTable("Global")
```

## Risk Matrix

| Risk | Severity | Likelihood | Description |
|------|----------|------------|-------------|
| Shared table written to | **Critical** | Medium | No runtime write-protection; relies on convention. Writing triggers no GC barrier → dangling pointer / segfault. Add `__newindex` metamethod guard. |
| Shared table contains function/userdata | **Critical** | Medium | `sp_mark_shared_obj` only recurses into Table/String. Closures' upvalues and proto sub-objects are NOT marked shared → may be GC'd → dangling pointer. |
| Shutdown order violation | **Critical** | Low | If `luaP_shutdown` runs while consumer VMs still hold shared Table*, SHAREDBIT is cleared → consumer GC may double-free master-owned objects. |
| SHAREDBIT / TESTBIT collision (bit 7) | **High** | Low | Both use bit 7 of `marked`. Under `LUA_DEBUG` builds, GC test logic and shared marking interfere with each other → unpredictable GC behavior. |
| Hot-reload memory growth | **Medium** | High | Each `luaP_updatetable` pushes a new table onto `g_master` stack; old table + all sub-objects stay alive forever (SHAREDBIT prevents GC). Frequent reloads → OOM. |
| Missing `<string.h>` in `ltable.c` | **Medium** | Low | `memcmp` used without proper include. Works by luck on most toolchains; may fail on strict compilers. **(Fixed)** |
| String lookup performance regression | **Low** | Certain | Every string-key lookup on shared tables recomputes hash via `luaS_hash` + compares by `memcmp` instead of pointer equality. |
| Lua version upgrade difficulty | **High** | Certain | 6+ core files modified (`lgc.h/c`, `ltable.c`, `lstate.c`, `lua.h`). Any Lua upgrade (even 5.4.x patch) requires careful re-application and audit. |

## Important Constraints

1. Shared tables are READ-ONLY. Writing (rawset, etc.) is undefined behavior.
2. `luaP_shutdown` must be called AFTER all child lua_States are closed.
3. After `luaP_updatetable`, old table pointers remain valid (stale but safe).
   VMs should use `luaP_generation` to detect updates and re-query.
4. Shared tables must only contain: table, string, number, boolean, nil.
   Storing function, userdata, or thread values is unsafe.
