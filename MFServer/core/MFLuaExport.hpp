#ifndef MFLuaExport_hpp
#define MFLuaExport_hpp

#include "sol/sol.hpp"

class MFLuaProfiler;

class MFLuaExport
{
public:
    static void exportLua(sol::state& lua);
public:
	static void exportMFUtil(sol::state& lua);
	static void exportMFApplication(sol::state& lua);
	static void exportMFNetManager(sol::state& lua);
	static void exportMFShareData(sol::state& lua);
	static void exportMFLuaServiceManager(sol::state& lua);
	static void exportMFRedisPoolManager(sol::state& lua);
	static void exportMFMysqlPoolManager(sol::state& lua);
	static void exportMFConnectManager(sol::state& lua);
	static void exportMFProfiler(sol::state& lua, MFLuaProfiler* luaProfiler);
	static void ensureExportHttpRequest(sol::state_view lua);
};


#endif /* MFLuaExport_hpp */
