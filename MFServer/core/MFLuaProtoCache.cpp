#include "MFLuaProtoCache.hpp"
#include "MFApplication.hpp"

#include <algorithm>
#include <mutex>
#include <unordered_map>

#include "MFMacro.h"

MFLuaProtoCache* MFLuaProtoCache::m_instance = nullptr;

MFLuaProtoCache::MFLuaProtoCache() {}

MFLuaProtoCache* MFLuaProtoCache::getInstance() {
    if (!m_instance) {
        m_instance = new MFLuaProtoCache();
    }
    return m_instance;
}

void MFLuaProtoCache::destroyInstance() {
    if (m_instance) {
        delete m_instance;
        m_instance = nullptr;
    }
}


static std::mutex s_protoMutex;
static lua_State* s_protoState = nullptr;
static MFFastMap<std::string, const void*> s_protos;

static const void* shareProto(const std::string& modname, const std::string& path, std::string& err) {
    auto it = s_protos.find(modname);
    if (it != s_protos.end()) {
        return it->second;
    }
    if (!s_protoState) {
        s_protoState = lua_newstate(luaL_alloc, nullptr, SOL_LUA_SHARED_SEED);
        if (!s_protoState) {
            err = "customRequireSearcher: create proto state failed";
            return nullptr;
        }
    }
    lua_State* cs = s_protoState;
    if (luaL_loadfilex_(cs, path.c_str(), nullptr) != LUA_OK) {
        const char* msg = lua_tostring(cs, -1);
        err = msg ? msg : "customRequireSearcher: load failed";
        lua_pop(cs, 1);
        return nullptr;
    }
    lua_sharefunction(cs, -1);
    const void* proto = lua_topointer(cs, -1);
    luaL_ref(cs, LUA_REGISTRYINDEX);
    s_protos.emplace(modname, proto);
    return proto;
}

static int customRequireSearcher(lua_State* L) {
    const char* modname = lua_tostring(L, 1);
    if (!modname) {
        lua_pushnil(L);
        lua_pushstring(L, "customRequireSearcher: modname is nil");
        return 2;
    }
    const char* root = lua_tostring(L, lua_upvalueindex(1));
    if (!root) {
        lua_pushnil(L);
        lua_pushstring(L, "customRequireSearcher: root upvalue is nil");
        return 2;
    }

    try {
        std::string path = root;
        path += "/";
        std::replace(path.begin(), path.end(), '\\', '/');
        for (const char* p = modname; *p; ++p) {
            path += (*p == '.') ? '/' : *p;
        }
        path += ".lua";

        const void* proto = nullptr;
        std::string err;
        {
            std::lock_guard<std::mutex> lock(s_protoMutex);
            proto = shareProto(modname, path, err);
        }

        if (!proto) {
            lua_pushnil(L);
            lua_pushlstring(L, err.data(), err.size());
            return 2;
        }

        lua_clonefunction(L, proto);
        return 1;
    } catch (const std::exception& e) {
        lua_pushnil(L);
        lua_pushfstring(L, "customRequireSearcher: %s", e.what());
        return 2;
    } catch ( ... ) {
        lua_pushnil(L);
        lua_pushstring(L, "customRequireSearcher: unknown C++ exception");
        return 2;
    }
}

void MFLuaProtoCache::addRequireSearcher(lua_State* L, const std::string& rootPath) {
    lua_getglobal(L, "package");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    lua_getfield(L, -1, "searchers");
    bool useSearchers = lua_istable(L, -1);
    if (!useSearchers) {
        lua_pop(L, 1);
        lua_getfield(L, -1, "loaders");
        if (!lua_istable(L, -1)) {
            lua_pop(L, 2);
            return;
        }
    }
    int len = (int)lua_rawlen(L, -1);
    for (int i = len; i >= 2; i--) {
        lua_rawgeti(L, -1, i);
        lua_rawseti(L, -2, i + 1);
    }
    lua_pushstring(L, rootPath.c_str());
    lua_pushcclosure(L, customRequireSearcher, 1);
    lua_rawseti(L, -2, 2);
    lua_pop(L, 2);
}

void MFLuaProtoCache::addRequireSearcher(sol::state& state, const std::string& rootPath) {
    addRequireSearcher(state.lua_state(), rootPath);
}

void MFLuaProtoCache::addRequireSearcher(const sol::this_state& state, const std::string& rootPath) {
    addRequireSearcher(state.lua_state(), rootPath);
}

void MFLuaProtoCache::clearRequireCache() {
    std::lock_guard<std::mutex> lock(s_protoMutex);
    s_protos.clear();
}

void MFLuaProtoCache::removeRequireCache(const std::string& path) {
    std::lock_guard<std::mutex> lock(s_protoMutex);
    s_protos.erase(path);
}
