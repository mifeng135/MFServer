#include "MFApplication.hpp"

#include "MFLuaExport.hpp"
#include "MFUtil.hpp"
#include "MFNetManager.hpp"
#include "MFLuaServiceManager.hpp"
#include "MFRedisConnectPool.hpp"
#include "MFIoPool.hpp"
#include "MFMysqlConnectPool.hpp"
#include "spdlog/fmt/bundled/args.h"
#include "MFLuaMessage.hpp"
#include "MFSnowflake.hpp"
#include "MFConnectionManager.hpp"
#include "MFAnnotationHandle.hpp"
#include "MFLuaByteCodeCache.hpp"
#include "MFJsonConfig.hpp"
#include "MFUdpChannelManager.hpp"

MFApplication* MFApplication::m_instance = nullptr;

MFApplication::MFApplication()
: m_netManager(nullptr)
, m_shareData(nullptr)
, m_fileWatch(nullptr)
, m_ioPool(nullptr)
, m_shutdown(false)
, m_snowflake(nullptr) {
    m_schedulePool = new MFObjectPool<MFScheduleMessage>();
}

MFApplication::~MFApplication() {
    delete m_schedulePool;
    m_schedulePool = nullptr;

    if (m_snowflake) {
        delete m_snowflake;
        m_snowflake = nullptr;
    }
}

MFApplication *MFApplication::getInstance() {
    if (!m_instance) {
        m_instance = new MFApplication();
		m_instance->init();
    }
    return m_instance;
}

void MFApplication::destroyInstance() {
    if (m_instance) {
        delete m_instance;
        m_instance = nullptr;
    }
}

void MFApplication::init() {
    initLog();
    initInstance();
    m_netManager = std::make_shared<MFNetManager>(this);
    m_shareData = std::make_shared<MFLuaShareData>();
	m_ioPool = std::make_unique<MFIoPool>();
    m_ioPool->init(1);
    initLua();
    startMainTimer();
    initFileWatch(getScriptRoot());
}

void MFApplication::setScriptRoot(const std::string& path)
{
    m_scriptRoot = MFUtil::getCurrentPath() + "/" + path;
}

void MFApplication::setSqlMapperRoot(const std::string &path) {
    m_sqlMapperRoot = path;
}

void MFApplication::setServiceFindPaths(const sol::table &dstList) {
    size_t size = dstList.size();
    m_serviceFindPaths.reserve(size);
    for (auto& pair : dstList) {
        m_serviceFindPaths.emplace_back(pair.second.as<std::string>());
    }
}

void MFApplication::preloadProto(const std::string& path)
{
    m_protoContentVec.clear();
    std::string protoPath = MFUtil::getCurrentPath() + "/" + path;
    std::replace(protoPath.begin(), protoPath.end(), '\\', '/');

    std::vector<std::string> fileFullPathVec;
    MFUtil::getFileRecursive(protoPath, fileFullPathVec);
    for (std::string& fullPath : fileFullPathVec) {
		std::string prefix = MFUtil::getPathPrefix(fullPath);
        if (prefix != ".pb") {
            continue;
        }
        m_protoContentVec.emplace_back(MFUtil::readFile(fullPath));
    }
}

sol::object MFApplication::getProtoContent(const sol::this_state& ts)
{
    sol::state_view lua(ts);
    sol::table result = lua.create_table(m_protoContentVec.size());
    int index = 1;
    for (const std::string& content : m_protoContentVec) {
        result[index] = content;
        index++;
    }
    return result;
}

void MFApplication::initSnowflake(uint64_t timestamp, int workerId, int datacenterId)
{
	m_snowflake = new MFSnowflake(timestamp);
	m_snowflake->init(workerId, datacenterId);
}

int64_t MFApplication::nextId()
{
    if (!m_snowflake) {
        logInfo("---------------------not init snowflake---------------------");
        return 0;
    }
	return m_snowflake->nextId();
}

void MFApplication::submitIo(std::function<int()>&& fn)
{
	m_ioPool->submit(std::move(fn));
}

uint32_t MFApplication::submitMainTimer(std::function<void()>&& fn) {
    uint32_t timerId = MFUtil::genTimerId();
    drogon::app().getLoop()->runInLoop([this, timerId, func = std::move(fn)]() mutable {
        m_timerFnMap[timerId] = func;
    });
    return timerId;
}
//drogon::app().getLoop()->runInLoop and drogon::app().getLoop()->runEvery is same thread
void MFApplication::startMainTimer() {
    drogon::app().getLoop()->runEvery(30, [this] {
        if (!m_waitingRemoveTimerIds.empty()) {
            for (int timerId : m_waitingRemoveTimerIds) {
                auto it = m_timerFnMap.find(timerId);
                if (it != m_timerFnMap.end()) {
                    m_timerFnMap.erase(it);
                }
            }
        }

        for (auto& pair : m_timerFnMap) {
            pair.second();
        }
    });
}

void MFApplication::stopMainTimer(uint32_t timerId) {
    drogon::app().getLoop()->runInLoop([this, timerId]() mutable {
        m_waitingRemoveTimerIds.push_back(timerId);
    });
}

void MFApplication::closeState() {
	if (m_state.has_value()) {
        m_state->collect_gc();
        m_state.reset();
    }
}

void MFApplication::initInstance() {
    MFLuaServiceManager::getInstance();
    MFRedisPoolManager::getInstance();
    MFMysqlPoolManager::getInstance();
    MFConnectionManager::getInstance();
    MFAnnotationHandle::getInstance();
	MFUdpChannelManager::getInstance();
}

void MFApplication::initLog()
{
    const auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>("log/log.log", 1024 * 1024 * 5, 10);
    fileSink->set_level(spdlog::level::debug);
    fileSink->set_pattern("[%Y-%m-%d %H:%M:%S] [%l] [%t] %v");
    std::vector<spdlog::sink_ptr> sinks;
    if (MFUtil::isDebug()) {
        const auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_level(spdlog::level::debug);
        consoleSink->set_pattern("[%Y-%m-%d %H:%M:%S] [%l] [%t] %v");
        sinks.push_back(consoleSink);
    }
    sinks.push_back(fileSink);
    m_logger = std::make_shared<spdlog::logger>("multiSink", begin(sinks), end(sinks));
    m_logger->flush_on(spdlog::level::info);
    trantor::Logger::setLogLevel(trantor::Logger::kWarn);
}

void MFApplication::luaLogServiceName(std::string_view serviceName, std::string_view msg, const sol::variadic_args& args) {
    size_t size = args.size();
    if (size == 0) {
        m_logger->info("<{}> {}", serviceName, msg);
        return;
    }
    
    fmt::dynamic_format_arg_store<fmt::format_context> store;
    for (std::size_t i = 0; i < size; ++i) {
        switch (args.get_type(static_cast<int>(i))) {
            case sol::type::string:
                store.push_back(args.get<std::string>(static_cast<int>(i)));
                break;
            case sol::type::number:
                store.push_back(args.get<double>(static_cast<int>(i)));
                break;
            case sol::type::boolean:
                store.push_back(args.get<bool>(static_cast<int>(i)));
                break;
            case sol::type::lua_nil:
                store.push_back("nil");
                break;
            default:
                store.push_back("Unknown");
                break;
        }
    }
    try {
        std::string format = fmt::vformat(msg, store);
        m_logger->info("<{}> {} ", serviceName, fmt::vformat(msg, store));
    } catch (std::exception& e) {
        logInfo("luaLog = {}", e.what());
    }
}

void MFApplication::initLua() {
    luaP_init();
	m_state = sol::state();
    auto& state = m_state.value();
    MFLuaExport::exportLua(state);

	std::string currentPath = MFUtil::getCurrentPath();

    MFUtil::addSearchPath(currentPath, state);
    MFUtil::addSearchPath(currentPath + "/coreScript", state);

    MFLuaByteCodeCache::getInstance()->addRequireSearcher(state, currentPath);
    MFLuaByteCodeCache::getInstance()->addRequireSearcher(state, currentPath + "/coreScript");

    try {
        //preload 
        std::string doString = R"(
            require "Core.MF" 
        )";
        state.do_string(doString);
        state.script_file(currentPath + "/main.lua");
        closeState();
    } catch (sol::error& e) {
        logInfo("lua exception = {}", e.what());
    }
}

void MFApplication::shutDown() {
    if (m_shutdown) {
        return;
    }
    m_shutdown = true;
	m_ioPool->shutdown();

    drogon::app().getLoop()->runInLoop([]()->void {
        MFLuaServiceManager::destroyInstance();
        MFRedisPoolManager::destroyInstance();
        MFMysqlPoolManager::destroyInstance();
        MFAnnotationHandle::destroyInstance();
        MFConnectionManager::destroyInstance();
		MFUdpChannelManager::destroyInstance();
        drogon::app().quit();
        luaP_shutdown();
        MFApplication::destroyInstance();
    });
}

void MFApplication::run() {
    drogon::app().setIntSignalHandler([]() ->void {
        MFApplication::getInstance()->shutDown();
    });

    drogon::app().setTermSignalHandler([]() ->void {
        MFApplication::getInstance()->shutDown();
    });
    drogon::app().setIdleConnectionTimeout(60);
    drogon::app().run();
}

uint64_t MFApplication::scheduleOnce(double second, MFServiceId_t serviceId)
{
    auto timerIdPtr = std::make_shared<trantor::TimerId>(0);
    *timerIdPtr = drogon::app().getLoop()->runAfter(second, [this, timerIdPtr, serviceId]() {
        MFScheduleMessage* message = m_schedulePool->pop();
        message->setPool(m_schedulePool);
        message->setMessageType(LuaMessageTypeTimerOnce);
        message->setSessionId(*timerIdPtr);
        message->setDst(serviceId);
        MFLuaServiceManager::getInstance()->nativeDispatch(message);
    });
    return *timerIdPtr;
}

uint64_t MFApplication::schedule(double second, MFServiceId_t serviceId)
{
    auto timerIdPtr = std::make_shared<trantor::TimerId>(0);
    *timerIdPtr = drogon::app().getLoop()->runEvery(second, [this, timerIdPtr, serviceId]() {
        MFScheduleMessage* message = m_schedulePool->pop();
        message->setPool(m_schedulePool);
        message->setMessageType(LuaMessageTypeTimer);
        message->setSessionId(*timerIdPtr);
        message->setDst(serviceId);
        MFLuaServiceManager::getInstance()->nativeDispatch(message);
    });
    return *timerIdPtr;
}

void MFApplication::stopSchedule(uint64_t timerId) {
    drogon::app().getLoop()->invalidateTimer(timerId);
}

void MFApplication::setWorkThread(size_t ioTheadCount) {
    logInfo("native IO event loops work count = {}", ioTheadCount);
    drogon::app().setThreadNum(ioTheadCount);
}

void MFApplication::initFileWatch(const std::string& path) {
    m_fileWatch = std::make_unique<MFFileWatchar>();
    logInfo("initFileWatch = {}", path);
    m_fileWatch->start(path, [](dmon_action action, const std::string& rootDir, std::string filePath) {
        bool isHotReload = false;
        if (MFUtil::isLinux()) {
            isHotReload = action == dmon_action::DMON_ACTION_CREATE;
        } else {
            isHotReload = action == dmon_action::DMON_ACTION_MODIFY;
        }
        if (isHotReload) {
            size_t pathSize = filePath.length();
            if (pathSize < 5) {
                return;
            }
            if (filePath.compare(pathSize - 4, 4, ".lua") == 0) {
                filePath.erase(pathSize - 4, 4);
                std::replace(filePath.begin(), filePath.end(), '\\', '/');
                std::replace(filePath.begin(), filePath.end(), '/', '.');
				MFApplication::getInstance()->logInfo("file module change = {}", filePath);
                MFLuaByteCodeCache::getInstance()->removeRequireBytecodeCache(filePath);
                MFLuaServiceManager::getInstance()->nativeDispatchHotReload(filePath, "");
            }
        }
    });
}
