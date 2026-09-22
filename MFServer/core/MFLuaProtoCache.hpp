#ifndef MFLuaProtoCache_hpp
#define MFLuaProtoCache_hpp

#include <string>
#include "sol/sol.hpp"

class MFLuaProtoCache {
public:
    MFLuaProtoCache();
public:
    static MFLuaProtoCache* getInstance();
    static void destroyInstance();
public:
    void addRequireSearcher(lua_State* L, const std::string& rootPath);
    void addRequireSearcher(sol::state& state, const std::string& rootPath);
    void addRequireSearcher(const sol::this_state& state, const std::string& rootPath);
    void clearRequireCache();
    void removeRequireCache(const std::string& path);
private:
    static MFLuaProtoCache* m_instance;
};

#endif /* MFLuaProtoCache_hpp */
