#include "MFLuaByteCodeCache.hpp"
#include "MFApplication.hpp"

MFLuaByteCodeCache* MFLuaByteCodeCache::m_instance = nullptr;

MFLuaByteCodeCache::MFLuaByteCodeCache() {}

MFLuaByteCodeCache* MFLuaByteCodeCache::getInstance() {
    if (!m_instance) {
        m_instance = new MFLuaByteCodeCache();
    }
    return m_instance;
}

void MFLuaByteCodeCache::destroyInstance() {
    if (m_instance) {
        delete m_instance;
        m_instance = nullptr;
    }
}


struct BytecodeReaderState {
    const char* ptr = nullptr;
    size_t size = 0;
};

static const char* bytecodeReader(lua_State* L, void* ud, size_t* sz) {
    (void)L;
    BytecodeReaderState* s = static_cast<BytecodeReaderState*>(ud);
    if (s->size == 0) {
        *sz = 0;
        return nullptr;
    }
    *sz = s->size;
    s->size = 0;
    return s->ptr;
}

static int bytecodeWriter(lua_State* L, const void* p, size_t sz, void* ud) {
    (void)L;
    std::string* out = static_cast<std::string*>(ud);
    out->append(static_cast<const char*>(p), sz);
    return 0;
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

        MFLuaByteCodeCache* inst = MFLuaByteCodeCache::getInstance();
        std::string cachedBytecode;
        inst->m_requireBytecodeCache.find(modname, cachedBytecode);

        if (!cachedBytecode.empty()) {
            BytecodeReaderState readerState;
            readerState.ptr = cachedBytecode.data();
            readerState.size = cachedBytecode.size();
            if (lua_load(L, bytecodeReader, &readerState, modname, "b") == LUA_OK) {
                return 1;
            }
        }

        std::string path = root;
        path += "/";
        std::replace(path.begin(), path.end(), '\\', '/');
        for (const char* p = modname; *p; ++p) {
            path += (*p == '.') ? '/' : *p;
        }
        path += ".lua";

        if (luaL_loadfile(L, path.c_str()) != LUA_OK) {
            lua_pushnil(L);
            lua_pushvalue(L, -2);
            lua_remove(L, -3);
            return 2;
        }

        std::string dumped;
        if (lua_dump(L, bytecodeWriter, &dumped, 0) == 0 && !dumped.empty()) {
            inst->m_requireBytecodeCache.insert(std::string(modname), std::move(dumped));
        }

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

void MFLuaByteCodeCache::addRequireSearcher(lua_State* L, const std::string& rootPath) {
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

void MFLuaByteCodeCache::addRequireSearcher(sol::state& state, const std::string& rootPath) {
    addRequireSearcher(state.lua_state(), rootPath);
}

void MFLuaByteCodeCache::addRequireSearcher(const sol::this_state& state, const std::string& rootPath) {
    addRequireSearcher(state.lua_state(), rootPath);
}

void MFLuaByteCodeCache::clearRequireBytecodeCache() {
    m_requireBytecodeCache.clear();
}

void MFLuaByteCodeCache::removeRequireBytecodeCache(const std::string& path) {
    m_requireBytecodeCache.erase(path);
}
