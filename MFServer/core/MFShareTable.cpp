#include "MFShareTable.hpp"

#include <cstdio>
#include <mutex>

extern "C" {
int luaopen_skynet_sharetable_core(lua_State* L);
}

static const char* kCoreKey = "MFShareTable.core";

MFShareTable* MFShareTable::s_instance = nullptr;

MFShareTable::MFShareTable()
: m_manager(nullptr) {
    m_manager = lua_newstate(luaL_alloc, nullptr, SOL_LUA_SHARED_SEED);
    if (!m_manager) {
        return;
    }
    luaL_openlibs(m_manager);
    luaL_requiref(m_manager, "skynet.sharetable.core", luaopen_skynet_sharetable_core, 0);
    lua_setfield(m_manager, LUA_REGISTRYINDEX, kCoreKey);
}

MFShareTable::~MFShareTable() {
    if (!m_manager) {
        return;
    }
    for (auto& kv : m_entries) {
        for (int ref : kv.second.oldBoxRefs) {
            closeBox(ref);
        }
        if (kv.second.boxRef != LUA_NOREF) {
            closeBox(kv.second.boxRef);
        }
    }
    m_entries.clear();
    lua_close(m_manager);
    m_manager = nullptr;
}

MFShareTable* MFShareTable::getInstance() {
    static std::once_flag once;
    std::call_once(once, []() { s_instance = new MFShareTable(); });
    return s_instance;
}

void MFShareTable::destroyInstance() {
    delete s_instance;
    s_instance = nullptr;
}

int MFShareTable::loadMatrix(const std::string& filename, const void** outPtr, std::string& err) {
    lua_State* L = m_manager;
    int top = lua_gettop(L);

    lua_getfield(L, LUA_REGISTRYINDEX, kCoreKey);
    lua_getfield(L, -1, "matrix");
    std::string source = "@" + filename;
    lua_pushstring(L, source.c_str());
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        err = msg ? msg : "sharetable matrix failed";
        lua_settop(L, top);
        return LUA_NOREF;
    }

    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_getfield(L, -1, "getptr");
    lua_pushvalue(L, -2);
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        err = msg ? msg : "sharetable getptr failed";
        lua_settop(L, top);
        closeBox(ref);
        return LUA_NOREF;
    }
    *outPtr = lua_touserdata(L, -1);
    lua_settop(L, top);

    if (*outPtr == nullptr) {
        err = "sharetable returned a null table";
        closeBox(ref);
        return LUA_NOREF;
    }
    return ref;
}

void MFShareTable::closeBox(int ref) {
    if (ref == LUA_NOREF || !m_manager) {
        return;
    }
    lua_State* L = m_manager;
    int top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (lua_type(L, -1) == LUA_TUSERDATA) {
        lua_getfield(L, -1, "close");
        lua_pushvalue(L, -2);
        lua_pcall(L, 1, 0, 0);
    }
    lua_settop(L, top);
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
}

int MFShareTable::share(const std::string& name, const std::string& filename) {
    if (!m_manager) {
        return -1;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_entries.find(name) != m_entries.end()) {
        return 0;
    }
    const void* ptr = nullptr;
    std::string err;
    int ref = loadMatrix(filename, &ptr, err);
    if (ref == LUA_NOREF) {
        fprintf(stderr, "[sharetable] share '%s': %s\n", name.c_str(), err.c_str());
        return -2;
    }
    Entry e;
    e.ptr = ptr;
    e.boxRef = ref;
    e.generation = 1;
    m_entries.emplace(name, std::move(e));
    return 0;
}

int MFShareTable::update(const std::string& name, const std::string& filename) {
    if (!m_manager) {
        return -1;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(name);
    if (it == m_entries.end()) {
        return -1;
    }
    const void* ptr = nullptr;
    std::string err;
    int ref = loadMatrix(filename, &ptr, err);
    if (ref == LUA_NOREF) {
        fprintf(stderr, "[sharetable] update '%s': %s\n", name.c_str(), err.c_str());
        return -2;
    }
    it->second.oldBoxRefs.push_back(it->second.boxRef);
    it->second.boxRef = ref;
    it->second.ptr = ptr;
    it->second.generation++;
    return 0;
}

bool MFShareTable::query(lua_State* L, const std::string& name) {
    const void* ptr = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(name);
        if (it == m_entries.end()) {
            return false;
        }
        ptr = it->second.ptr;
    }
    if (!ptr) {
        return false;
    }
    lua_clonetable(L, ptr);
    return true;
}

unsigned int MFShareTable::generation(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(name);
    return it == m_entries.end() ? 0 : it->second.generation;
}

int MFShareTable::releaseEntryLocked(Entry& entry) {
    int n = static_cast<int>(entry.oldBoxRefs.size());
    for (int ref : entry.oldBoxRefs) {
        closeBox(ref);
    }
    entry.oldBoxRefs.clear();
    return n;
}

void MFShareTable::releaseOld() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& kv : m_entries) {
        releaseEntryLocked(kv.second);
    }
}


int MFShareTable::releaseOld(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(name);
    if (it == m_entries.end()) {
        return -1;
    }
    return releaseEntryLocked(it->second);
}

int MFShareTable::count() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_entries.size());
}
