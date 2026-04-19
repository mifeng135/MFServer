// ReSharper disable All
#include "MFLuaService.hpp"

#include <utility>
#include "MFLuaExport.hpp"
#include "MFLuaByteCodeCache.hpp"
#include "MFUtil.hpp"
#include "MFApplication.hpp"
#include "MFLuaMessage.hpp"
#include "MFRedisClient.hpp"
#include "MFLuaServiceManager.hpp"

MFLuaService::MFLuaService(MFServiceId_t serviceId, const char* serviceName)
: m_inReadyQueue(false)
, m_serviceId(serviceId)
, m_serviceName(serviceName) {
}

MFLuaService::~MFLuaService() {
	MFMessage* message = nullptr;
    while (m_luaMessageQueue.try_dequeue(message)) {
        message->reset();
	}
    m_messageHandlers = sol::lua_nil;
	m_state.collect_gc();
    m_state.clear_package_loaders();
    m_state.collect_gc();
}

void MFLuaService::init() {
    m_profiler = std::make_unique<MFLuaProfiler>(m_state.lua_state(), m_serviceName);
    MFLuaExport::exportLua(m_state);
    MFLuaExport::exportMFProfiler(m_state, m_profiler.get());


	std::string rootPath = MFUtil::getCurrentPath();
    const std::string& scriptRootPath = MFApplication::getInstance()->getScriptRoot();

    MFUtil::addSearchPath(scriptRootPath, m_state);
    MFUtil::addSearchPath(rootPath + "/coreScript", m_state);

    MFLuaByteCodeCache::getInstance()->addRequireSearcher(m_state, rootPath + "/coreScript");
    MFLuaByteCodeCache::getInstance()->addRequireSearcher(m_state, scriptRootPath);


    std::string serviceFullPath = getFileFullPath();
	if (serviceFullPath.empty()) {
        MFApplication::getInstance()->logInfo("MFLuaService init failed, service file not found, serviceName = {}", m_serviceName);
		return;
    }

    try {
        std::string doString = R"(
            require "Core.MF"
        )";
        m_state.do_string(doString);
        m_state.script_file(serviceFullPath);
        m_state["main"](m_serviceId, m_serviceName);
        m_messageHandlers = m_state["MFMessageHandlers"];
    } catch (sol::error &e) {
        logTraceback(e);
    }
}

void MFLuaService::pushMessage(MFMessage *message, bool isPriorityQueue) {
    m_luaMessageQueue.enqueue(message);
}

void MFLuaService::processMsg() {
    singleMessage();
}

void MFLuaService::singleMessage() {
    bool isStop = false;
    MFMessage *msg = nullptr;
    while (m_luaMessageQueue.try_dequeue(msg)) {
        isStop = msg->getMessageType() == LuaMessageTypeCloseService;
        if (isStop) {
            break;
        }
        processMessage(msg);
    }
    if (isStop) {
        delete this;
    }
}

void MFLuaService::processMessage(MFMessage* message) {
    try {
        createObject(message);
    } catch (sol::error& e) {
        logTraceback(e);
    }
    message->reset();
}

void MFLuaService::createObject(MFMessage *msg) {
    int msgType = msg->getMessageType();
    sol::function handler = m_messageHandlers[msgType];
    switch (msgType) {
        case LuaMessageTypeLuaSend: {
            MFForwardMessage* m = dynamic_cast<MFForwardMessage*>(msg);
            handler(m->getData(), m->getLen());
            break;
        }
        case LuaMessageTypeLuaRequest: {
            MFForwardMessage* m = dynamic_cast<MFForwardMessage*>(msg);
            handler(m->getData(), m->getLen(), m->getSessionId(), m->getSrc());
            break;
        }
        case LuaMessageTypeLuaResponse: {
            MFForwardMessage* m = dynamic_cast<MFForwardMessage*>(msg);
            handler(m->getData(), m->getLen(), m->getSessionId());
            break;
        }
        case LuaMessageTypeSocket:
        case LuaMessageTypeRpcReq:
        case LuaMessageTypeUdpServer: {
            MFSocketMessage* m = dynamic_cast<MFSocketMessage*>(msg);
            if (m->getCmd() == MFSocketRead) {
                handler(m->getCmd(), m->getFd(), m->getViewData());
            } else {
                handler(m->getCmd(), m->getFd());
            }
            break;
        }
        case LuaMessageTypeWebSocket: {
            MFSocketMessage* m = dynamic_cast<MFSocketMessage*>(msg);
            if (m->getCmd() == MFSocketRead) {
                handler(m->getCmd(), m->getFd(), m->getViewData(), m->getSrc());
            } else {
                handler(m->getCmd(), m->getFd());
            }
            break;
        }
        case LuaMessageTypeSocketClient:
        case LuaMessageTypeRpcRsp: {
            MFSocketMessage* m = dynamic_cast<MFSocketMessage*>(msg);
            handler(m->getCmd(), m->getViewData());
            break;
        }
        case LuaMessageTypeHttpServerReq: {
            MFHttpMessage* m = dynamic_cast<MFHttpMessage*>(msg);
            handler(m->getViewData(), m->getIp(), m->getFd());
            break;
        }
        case LuaMessageTypeHttpClientRsp: {
            MFHttpClientMessage* m = dynamic_cast<MFHttpClientMessage*>(msg);
            handler(m->getSessionId(), m->getViewData(), m->getResult());
            break;
        }
        case LuaMessageTypeHotReload: {
            MFHotReloadMessage* m = dynamic_cast<MFHotReloadMessage*>(msg);
            handler(m->getModule(), m->getFullPath());
            break;
        }
        case LuaMessageTypeReloadConfig:
            handler();
            break;
        case LuaMessageTypeMysqlQuery: {
            MFMysqlMessage* m = dynamic_cast<MFMysqlMessage*>(msg);
            m->executeCallback(this);
            break;
        }
        case LuaMessageTypeMysqlExecute: {
            MFMysqlMessage* m = dynamic_cast<MFMysqlMessage*>(msg);
            handler(m->getSessionId(), m->getExecRes(), m->getSuccess());
            break;
        }
        case LuaMessageTypeTimerOnce:
        case LuaMessageTypeTimer:
            handler(msg->getSessionId());
            break;
        case LuaMessageTypeMultiSend: {
            MFMultiForwardMessage* m = dynamic_cast<MFMultiForwardMessage*>(msg);
            handler(m->getData(), m->getLen());
            break;
        }
        case LuaMessageTypeRedisCmd: {
            MFRedisMessage* m = dynamic_cast<MFRedisMessage*>(msg);
            m->executeCallback(this);
            break;
        }
        default:
            MFApplication::getInstance()->logInfo("MFLuaService Unknown message type = {}", msgType);
            break;
    }
}


void MFLuaService::sqlMessageQuery(const MFMysqlResult& result, bool queryOne, int msgType) {
	sol::function handler = m_messageHandlers[msgType];
    size_t rowSize = result.rows.size();
    if (rowSize == 0) {
        handler(result.sessionId, result.success);
        return;
    }
    const std::vector<std::vector<MFValue>>& rows = result.rows;
    if (queryOne) {
        sol::table rowTable = m_state.create_table(0, static_cast<int>(result.columns.size()));
        MFUtil::rowToTable(rows.at(0), rowTable, result.columns);
        handler(result.sessionId, result.success, rowTable);
        return;
    }
    sol::object sqlListTable = MFUtil::rowsToLuaTable(rows, result.columns, m_state.lua_state());
    handler(result.sessionId, result.success, sqlListTable);
}

void MFLuaService::redisMessage(const MFRedisResult& result, int msgType) {
    sol::state_view stateView(m_state.lua_state());
    m_messageHandlers[msgType](result.sessionId, MFUtil::redisReplyToLuaObject(result.reply, stateView));
}

void MFLuaService::logTraceback(sol::error& e) {
    std::string errorMsg = e.what();
    lua_State* L = m_state.lua_state();
    luaTraceback(L, L, e.what(), 1);
    const char* traceback = lua_tostring(L, -1);
    lua_settop(L, 0);
    MFApplication::getInstance()->logInfo("lua error = {}", traceback);
}

void MFLuaService::luaTraceback(lua_State* L, lua_State* L1, const char* msg, int level)
{
    lua_Debug ar;
    int top = lua_gettop(L);
    int last = 0; 
    if (msg != nullptr) {
        lua_pushfstring(L, "%s\n", msg);
    }
    luaL_checkstack(L, 10, nullptr);
    lua_pushliteral(L, "stack traceback:");

    while (lua_getstack(L1, level++, &ar)) {
        if (level > 10) {
            lua_pushliteral(L, "\n\t...");
            break;
        }
        if (lua_getinfo(L1, "Slnt", &ar) == 0) {
            break;
        }
        if (ar.name != nullptr) {
            lua_pushfstring(L, "\n\t%s:%d: in function '%s'", ar.short_src, ar.currentline, ar.name);
        } else if (ar.what && strcmp(ar.what, "main") == 0) {
            lua_pushfstring(L, "\n\t%s:%d: in main chunk", ar.short_src, ar.currentline);
        } else if (ar.what && strcmp(ar.what, "C") != 0) {
            lua_pushfstring(L, "\n\t%s:%d: in ?", ar.short_src, ar.currentline);
        } else {
            continue; 
        }
        last = level - 1;
    }
    if (last == 0) {
        lua_pushliteral(L, "\n\t(no stack traceback)");
    }
    lua_concat(L, lua_gettop(L) - top);
}

std::string MFLuaService::getFileFullPath() {
    const std::vector<std::string>& paths = MFApplication::getInstance()->getServiceFindPaths();
    const std::string& scriptRootPath = MFApplication::getInstance()->getScriptRoot();

	size_t pathCount = paths.size();
    for (size_t i = 0; i < pathCount; ++i) {
        std::string fullPath = scriptRootPath + "/" + paths[i] + "/" + m_serviceName + ".lua";
        if (MFUtil::checkFileExists(fullPath)) {
            return fullPath;
        }
	}
    return "";
}
