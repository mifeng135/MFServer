#include "MFLuaSeri.h"
#include "MFpbcExport.h"
#include "MFJsonExport.h"
#include "MFLuaExport.hpp"
#include "MFLuaServiceManager.hpp"
#include "MFApplication.hpp"
#include "MFConnectionManager.hpp"
#include "MFUtil.hpp"
#include "MFNetManager.hpp"
#include "MFLuaShareData.hpp"
#include "MFMsgPack.h"
#include "MFMysqlConnectPool.hpp"
#include "MFLuaExtend.h"
#include "MFWebServer.hpp"
#include "MFRedisConnectPool.hpp"
#include "MFAnnotationHandle.hpp"
#include "MFJsonConfig.hpp"
#include "MFUdpChannelManager.hpp"
#include "MFLuaProfiler.hpp"
#include "MFLuaService.hpp"

#define LUA_FN(r, m, ...) static_cast<r(*)(__VA_ARGS__)>(&m)
#define LUA_MEMFN(r, cls, m, ...) static_cast<r(cls::*)(__VA_ARGS__)>(&cls::m)
#define LUA_MEMFN_CONST(r, cls, m, ...) static_cast<r(cls::*)(__VA_ARGS__)const>(&cls::m)
#define LUA_MEMFN_STATIC(r, cls, m, ...) static_cast<r(*)(__VA_ARGS__)>(&cls::m)


void MFLuaExport::exportLua(sol::state& lua)
{
    lua.open_libraries(
        sol::lib::base, sol::lib::package,
        sol::lib::string, sol::lib::table,
        sol::lib::math, sol::lib::coroutine,
        sol::lib::debug
    );
    luaopen_cmsgpack_safe(lua);
    luaopen_seri(lua);
    luaopen_pb(lua);
    luaopen_cjson(lua);
    luaopen_extend(lua);

    exportMFUtil(lua);
    exportMFApplication(lua);
    exportMFLuaServiceManager(lua);
    exportMFShareData(lua);
    exportMFNetManager(lua);
    exportMFRedisPoolManager(lua);
    exportMFMysqlPoolManager(lua);
    exportMFConnectManager(lua);
}

void MFLuaExport::exportMFUtil(sol::state& lua)
{
    auto table = lua.create_named_table("MFUtil");
    table["isDebug"] = []()-> bool {
        return MFUtil::isDebug();
    };

    table["getMilliseconds"] = []()->long long {
        return MFUtil::getMilliseconds();
    };

    table["getCurrentPath"] = []()-> std::string {
        return MFUtil::getCurrentPath();
    };

    table["readFile"] = [](const std::string& path)-> std::string {
        return MFUtil::readFile(path);
    };

    table["writeFile"] = [](const std::string& path, const std::string& content)-> bool {
        return MFUtil::writeFile(path, content);
    };

    table["getFileList"] = [](const std::string& path, const sol::this_state& ts)-> sol::object {
        return MFUtil::getFileList(path, ts);
    };

    table["isLinux"] = []()-> bool {
        return MFUtil::isLinux();
    };

    table["isWindows"] = []()-> bool {
        return MFUtil::isWindows();
    };

    table["genSessionId"] = []()-> size_t {
        return MFUtil::genSessionId();
    };

    table["genConvId"] = []()-> uint32_t {
        return MFUtil::genConvId();
    };

    table["addSearchPath"] = [](const std::string& path, const sol::this_state& ts) {
        MFUtil::addSearchPath(path, ts);
    };
    
    table["preloadSharedTable"] = [](const std::string& rootDir) {
		MFUtil::preloadSharedTable(rootDir);
    };

    table["luaShareTable"] = [](const std::string& name, const std::string& filename) -> int {
        return luaP_sharetable(name.c_str(), filename.c_str());
    };

    table["luaUpdateTable"] = [](const std::string& name, const std::string& filename) -> int {
        return luaP_updatetable(name.c_str(), filename.c_str());
    };

    table["luaQueryTable"] = [](const sol::this_state& ts, const std::string& name) -> sol::object {
        lua_State *L = ts;
        if (luaP_querytable(L, name.c_str())) {
            return sol::stack::pop<sol::object>(L);
        }
        return sol::lua_nil;
    };

    table["luaTableGeneration"] = [](const std::string& name) -> unsigned int {
        return luaP_generation(name.c_str());
    };

    table["jsonLoad"] = [](const std::string& fullPath)-> std::tuple<bool, std::string> {
        std::string err;
        bool ok = MFJsonConfig::instance().loadAllFromDirectory(fullPath, true, err);
        return std::make_tuple(ok, err);
    };
}

void MFLuaExport::exportMFApplication(sol::state& lua)
{
    auto table = lua.create_named_table("MFApplication");
    table["log"] = [](std::string_view msg) {
        MFApplication::getInstance()->luaLog(msg);
    };

    table["serviceLog"] = [](std::string_view serviceName, std::string_view msg, const sol::variadic_args& args) {
        MFApplication::getInstance()->luaLogServiceName(serviceName, msg, args);
    };

    table["scheduleOnce"] = [](double second, MFServiceId_t serviceId)-> uint64_t {
        return MFApplication::getInstance()->scheduleOnce(second, serviceId);
    };

    table["schedule"] = [](double second, MFServiceId_t serviceId)-> uint64_t {
        return MFApplication::getInstance()->schedule(second, serviceId);
    };

    table["stopSchedule"] = [](uint64_t timerId) {
        MFApplication::getInstance()->stopSchedule(timerId);
    };

    table["setWorkThread"] = [](size_t ioTheadCount) {
        MFApplication::getInstance()->setWorkThread(ioTheadCount);
    };
    
    table["shutDown"] = []() {
        MFApplication::getInstance()->shutDown();
    };

    table["getScriptRoot"] = []()-> const std::string& {
        return MFApplication::getInstance()->getScriptRoot();
    };

    table["setScriptRoot"] = [](const std::string& path) {
        MFApplication::getInstance()->setScriptRoot(path);
    };

    table["setSqlMapperRoot"] = [](const std::string& path) {
        MFApplication::getInstance()->setSqlMapperRoot(path);
    };

    table["getSqlMapperRoot"] = []()-> const std::string& {
        return MFApplication::getInstance()->getSqlMapperRoot();
    };

    table["initSnowflake"] = [](uint64_t timestamp, int workerId, int datacenterId) {
        MFApplication::getInstance()->initSnowflake(timestamp, workerId, datacenterId);
    };

    table["nextId"] = []()->int64_t {
		return MFApplication::getInstance()->nextId();
    };

    table["setServicePaths"] = [](const sol::table &dstList) {
        MFApplication::getInstance()->setServiceFindPaths(dstList);
    };

    table["preloadProto"] = [](const std::string& path) {
        MFApplication::getInstance()->preloadProto(path);
    };

    table["getProtoContent"] = [](const sol::this_state& ts)-> sol::object {
		return MFApplication::getInstance()->getProtoContent(ts);
    };

    table["scanRouter"] = [](const std::string& dir, const sol::this_state& luaState) {
        MFAnnotationHandle::getInstance()->scanRouter(dir, luaState);
    };

    table["getModulePath"] = [](const std::string& key, const sol::this_state& luaState)-> sol::object {
        return MFAnnotationHandle::getInstance()->getModulePath(key, luaState);
    };
}

void MFLuaExport::exportMFNetManager(sol::state& lua)
{
    auto table = lua.create_named_table("MFNativeNet");
    
    table["createTcpServer"] = [](uint32_t configId, MFServiceId_t serviceId, uint8_t type) {
		MFApplication::getInstance()->getNetManager()->createTcpServer(configId, serviceId, type);
    };

    table["createHttpServer"] = [](uint32_t configId, MFServiceId_t serviceId) {
        MFApplication::getInstance()->getNetManager()->createHttpServer(configId, serviceId);
    };

    table["createWebSocketServer"] = [](uint32_t configId, MFServiceId_t serviceId) {
        MFApplication::getInstance()->getNetManager()->createWebSocketServer(configId, serviceId);
	};

    table["createUdpServer"] = [](uint32_t configId, MFServiceId_t serviceId) {
        MFApplication::getInstance()->getNetManager()->createUdpServer(configId, serviceId);
    };
    
    table["stopTcpServer"] = [](uint32_t configId) {
        MFApplication::getInstance()->getNetManager()->stopTcpServer(configId);
    };

    table["setTcpServerIdleTime"] = [](uint32_t configId, size_t time) {
        MFApplication::getInstance()->getNetManager()->setTcpServerIdleTime(configId, time);
    };
}

void MFLuaExport::exportMFShareData(sol::state& lua)
{
    auto table = lua.create_named_table("MFShareData");
    table["update"] = [](size_t key, void* data, size_t len) {
		MFApplication::getInstance()->getShareData()->addOrUpdate(key, data, len);
    };

    table["get"] = [](size_t key, const sol::this_state& ts)-> std::tuple<sol::object, sol::object> {
        return MFApplication::getInstance()->getShareData()->get(key, ts);
    };

    table["remove"] = [](size_t key) {
        return MFApplication::getInstance()->getShareData()->remove(key);
    };
}

void MFLuaExport::exportMFLuaServiceManager(sol::state& lua)
{
    auto table = lua.create_named_table("MFLuaServiceManager");
    table["init"] = [](int threadCount) {
        MFLuaServiceManager::getInstance()->init(threadCount);
    };

    table["addService"] = [](const std::string& serviceName)-> MFServiceId_t {
        return MFLuaServiceManager::getInstance()->addService(serviceName);
    };

    table["addUniqueService"] = [](const std::string& serviceName)-> MFServiceId_t {
        return MFLuaServiceManager::getInstance()->addUniqueService(serviceName);
    };

    table["queryUniqueService"] = [](const std::string& serviceName) -> MFServiceId_t {
        return MFLuaServiceManager::getInstance()->queryUniqueService(serviceName);
    };

    table["closeService"] = [](MFServiceId_t serviceId) {
        MFLuaServiceManager::getInstance()->closeService(serviceId);
    };

    table["send"] = [](MFServiceId_t src, MFServiceId_t dst, void* data, size_t len, int msgType, bool genSessionId, bool usePriority)-> size_t {
        return MFLuaServiceManager::getInstance()->send(src, dst, data, len, msgType, genSessionId, usePriority);
    };

    table["ret"] = [](MFServiceId_t src, MFServiceId_t dst, void* data, size_t len, size_t sessionId, int messageType, bool usePriority) {
        MFLuaServiceManager::getInstance()->ret(src, dst, data, len, sessionId, messageType, usePriority);
    };

    table["sendMulti"] = [](MFServiceId_t src, void* data, size_t len, const sol::table& dstList) {
        MFLuaServiceManager::getInstance()->sendMulti(src, data, len, dstList);
    };
}

void MFLuaExport::exportMFRedisPoolManager(sol::state& lua)
{
    auto table = lua.create_named_table("MFNativeRedis");
    table["executeAsync"] = [](MFServiceId_t serviceId, int key, const sol::variadic_args& args, const sol::this_state& state)-> size_t {
        return MFRedisPoolManager::getInstance()->executeAsync(serviceId, key, args, state);
    };

    table["executeSync"] = [](int key, int timeOut, const sol::variadic_args& args, const sol::this_state& state)-> sol::object {
        return MFRedisPoolManager::getInstance()->executeSync(key, timeOut, args, state);
    };
}

void MFLuaExport::exportMFMysqlPoolManager(sol::state& lua)
{
    auto table = lua.create_named_table("MFNativeMysql");

    table["queryAsync"] = [](const std::string& sql, MFServiceId_t serviceId, int key, const sol::this_state& state)-> size_t {
        return MFMysqlPoolManager::getInstance()->queryAsync(sql, serviceId, key, state);
    };

    table["queryOneAsync"] = [](const std::string& sql, MFServiceId_t serviceId, int key, const sol::this_state& state)-> size_t {
        return MFMysqlPoolManager::getInstance()->queryOneAsync(sql, serviceId, key, state);
    };

    table["executeAsync"] = [](const std::string& sql, MFServiceId_t serviceId, int key, const sol::this_state& state)-> size_t {
        return MFMysqlPoolManager::getInstance()->executeAsync(sql, serviceId, key, state);
    };

    //
    table["beginTransaction"] = [](MFServiceId_t serviceId, int key, const sol::this_state& state)-> size_t {
        return MFMysqlPoolManager::getInstance()->beginTransaction(serviceId, key, state);
    };

    table["executeInTransaction"] = [](size_t transactionId, const std::string& sql, MFServiceId_t serviceId, int key, const sol::this_state& state)-> size_t {
        return MFMysqlPoolManager::getInstance()->executeInTransaction(transactionId, sql, serviceId, key, state);
    };

    table["commitTransaction"] = [](size_t transactionId, MFServiceId_t serviceId, int key, const sol::this_state& state)-> size_t {
        return MFMysqlPoolManager::getInstance()->commitTransaction(transactionId, serviceId, key, state);
    };

    table["rollbackTransaction"] = [](size_t transactionId, MFServiceId_t serviceId, int key, const sol::this_state& state)-> size_t {
        return MFMysqlPoolManager::getInstance()->rollbackTransaction(transactionId, serviceId, key, state);
    };
}

void MFLuaExport::exportMFConnectManager(sol::state &lua) {
    auto table = lua.create_named_table("MFSocket");

    table["send"] = [](size_t fd, const char* msg, int len) {
        MFConnectionManager::getInstance()->send(fd, msg, len);
    };

    table["forceCloseConnection"] = [](size_t fd) {
        MFConnectionManager::getInstance()->forceCloseConnection(fd);
    };

    table["httpResponse"] = [](size_t fd, const std::string& body, drogon::HttpStatusCode code) {
        MFConnectionManager::getInstance()->httpResponse(fd, body, code);
    };

    table["httpResponseParams"] = [](size_t fd, const std::string& body, drogon::HttpStatusCode code, const sol::table& headers) {
        MFConnectionManager::getInstance()->httpResponseParams(fd, body, code, headers);
    };

    table["udpSend"] = [](unsigned int conv, const char* msg, int len) {
        MFUdpChannelManager::getInstance()->udpSend(conv, msg, len);
    };
    
    table["webSocketSend"] = [](size_t fd, const char* msg, int len, drogon::WebSocketMessageType type) {
        MFConnectionManager::getInstance()->sendWebSocketConnection(fd, msg, len, type);
    };

    table["forceWebCloseConnection"] = [](size_t fd) {
        MFConnectionManager::getInstance()->forceCloseWebSocketConnection(fd);
    };

    table["rpcClientSend"] = [](uint32_t configId, const char *msg, int len, const sol::this_state& state) {
        MFApplication::getInstance()->getNetManager()->rpcClientSend(configId, msg, len, state);
    };

	table["httpRequest"] = [](MFServiceId_t serviceId, const std::string& host, int method,
        sol::optional<std::string> path,
        sol::optional<std::string> body,
        sol::optional<sol::table> headers) {

        drogon::HttpRequestPtr request = drogon::HttpRequest::newHttpRequest();

		request->setMethod(static_cast<drogon::HttpMethod>(method));

        if (body.has_value()) {
            request->setBody(body.value());
        }

        if (path.has_value()) {
            request->setPath(path.value());
        }

        if (headers.has_value()) {
            for (auto& pair : headers.value()) {
                request->addHeader(pair.first.as<std::string>(), pair.second.as<std::string>());
            }
        }
        return MFApplication::getInstance()->getNetManager()->createHttpClient(host, request, serviceId);
    };
}

static MFLuaProfiler* getProfilerFromState(lua_State* L) {
    lua_pushstring(L, "MFLuaProfiler_Ptr");
    lua_rawget(L, LUA_REGISTRYINDEX);
    auto* p = lua_islightuserdata(L, -1) ? static_cast<MFLuaProfiler*>(lua_touserdata(L, -1)) : nullptr;
    lua_pop(L, 1);
    return p;
}

void MFLuaExport::exportMFProfiler(sol::state& lua, MFLuaProfiler* luaProfiler)
{
    lua_pushstring(lua.lua_state(), "MFLuaProfiler_Ptr");
    lua_pushlightuserdata(lua.lua_state(), luaProfiler);
    lua_rawset(lua.lua_state(), LUA_REGISTRYINDEX);
    auto table = lua.create_named_table("MFProfiler");

    table["startProfiler"] = [](const sol::this_state& ts) {
        MFLuaProfiler* profiler = getProfilerFromState(ts);
        if (profiler) {
            profiler->start();
        }
    };

    table["stopProfiler"] = [](const sol::this_state& ts) {
        MFLuaProfiler* profiler = getProfilerFromState(ts);
        if (profiler) {
            profiler->stop();
        }
    };

    table["resetProfiler"] = [](const sol::this_state& ts) {
        MFLuaProfiler* profiler = getProfilerFromState(ts);
        if (profiler) {
            profiler->reset();
        }
    };

    table["reportProfiler"] = [](const sol::this_state& ts, sol::optional<int> maxDepth) {
        MFLuaProfiler* profiler = getProfilerFromState(ts);
        if (profiler) {
            MFApplication::getInstance()->logInfo(profiler->reportTree(maxDepth.value_or(20)));
        }
    };
}



