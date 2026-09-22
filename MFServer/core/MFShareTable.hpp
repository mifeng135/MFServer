#ifndef MF_SHARE_TABLE_H
#define MF_SHARE_TABLE_H

#include <mutex>
#include <string>
#include <vector>

#include "MFMacro.h"
#include "sol/sol.hpp"

class MFShareTable {
private:
    MFShareTable();
    ~MFShareTable();
public:
    static MFShareTable* getInstance();
    static void destroyInstance();
public:
    int share(const std::string& name, const std::string& filename);
    int update(const std::string& name, const std::string& filename);
    bool query(lua_State* L, const std::string& name);
    unsigned int generation(const std::string& name);
    void releaseOld();
    int releaseOld(const std::string& name);
    int count();

    template <typename Fn>
    bool withEntry(const std::string& name, lua_Integer key, Fn&& fn) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_manager) {
            return false;
        }
        auto it = m_entries.find(name);
        if (it == m_entries.end() || !it->second.ptr) {
            return false;
        }
        lua_State* L = m_manager;
        const int top = lua_gettop(L);
        struct Reset {
            lua_State* L;
            int top;
            ~Reset() {
                lua_settop(L, top);
            }
        } reset{L, top};

        lua_clonetable(L, it->second.ptr);
        lua_geti(L, -1, key);
        if (!lua_istable(L, -1)) {
            return false;
        }
        fn(L);
        return true;
    }

    static std::string fieldString(lua_State* L, const char* field) {
        lua_getfield(L, -1, field);
        std::string out;
        if (lua_isstring(L, -1)) {
            size_t n = 0;
            const char* s = lua_tolstring(L, -1, &n);
            out.assign(s, n);
        }
        lua_pop(L, 1);
        return out;
    }

    static int fieldInt(lua_State* L, const char* field, int fallback = 0) {
        lua_getfield(L, -1, field);
        int out = fallback;
        if (lua_isnumber(L, -1)) {
            out = static_cast<int>(lua_tointeger(L, -1));
        }
        lua_pop(L, 1);
        return out;
    }
private:
    struct Entry;
private:
    int loadMatrix(const std::string& filename, const void** outPtr, std::string& err);
    void closeBox(int ref);
    int releaseEntryLocked(Entry& entry);
private:
    struct Entry {
        const void*         ptr;        
        int                 boxRef;   
        unsigned int        generation;
        std::vector<int>    oldBoxRefs; 
    };
private:
    static MFShareTable*                    s_instance;
    std::mutex                              m_mutex;
    lua_State*                              m_manager;
    MFFastMap<std::string, Entry>           m_entries;
};

#endif //MF_SHARE_TABLE_H
