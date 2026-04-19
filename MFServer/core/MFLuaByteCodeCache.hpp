#ifndef MFLuaByteCodeCache_hpp
#define MFLuaByteCodeCache_hpp

#include <string>
#include "MFStripedMap.hpp"
#include "sol/sol.hpp"

class MFLuaByteCodeCache {
public:
    MFLuaByteCodeCache();
public:
    static MFLuaByteCodeCache* getInstance();
    static void destroyInstance();
public:
    void addRequireSearcher(lua_State* L, const std::string& rootPath);
    void addRequireSearcher(sol::state& state, const std::string& rootPath);
    void addRequireSearcher(const sol::this_state& state, const std::string& rootPath);
    void clearRequireBytecodeCache();
    void removeRequireBytecodeCache(const std::string& path);
public:
    MFStripedMap<std::string, std::string, 16> m_requireBytecodeCache;
private:
    static MFLuaByteCodeCache* m_instance;
};

#endif /* MFLuaByteCodeCache_hpp */
