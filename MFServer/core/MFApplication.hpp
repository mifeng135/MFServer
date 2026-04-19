#ifndef MFApplication_h
#define MFApplication_h

#include "spdlog/spdlog.h"
#include "spdlog/sinks/daily_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/logger.h"
#include "sol/sol.hpp"
#include "trantor/net/EventLoopThread.h"
#include "MFLuaShareData.hpp"
#include "MFObjectPool.hpp"
#include "MFFileWatch.hpp"
#include "MFMacro.h"


class MFNetManager;
class MFScheduleMessage;
class MFIoPool;
class MFSnowflake;

class MFApplication
{
private:
    MFApplication();
    ~MFApplication();
public:
    static MFApplication* getInstance();
    static void destroyInstance();
public:
    template <typename... Args>
    void logInfo(spdlog::format_string_t<Args...> fmt, Args &&...args) {
        m_logger->info(fmt, std::forward<Args>(args)...);
    }
    template <typename T>
    void logInfo(const T &msg) {
        m_logger->info(msg);
    }

    void luaLog(std::string_view msg) {
        m_logger->info("Lua log: {}", msg);
    }
    void luaLogServiceName(std::string_view serviceName, std::string_view msg, const sol::variadic_args& args);
public:
    void run();
    std::shared_ptr<MFNetManager>& getNetManager() { return m_netManager; }
    std::shared_ptr<MFLuaShareData>& getShareData() { return m_shareData; }


    uint64_t scheduleOnce(double second, MFServiceId_t serviceId);
    uint64_t schedule(double second, MFServiceId_t serviceId);
    void stopSchedule(uint64_t timerId);
    void setWorkThread(size_t ioTheadCount);
    void initFileWatch(const std::string& path);
    void shutDown();
    void init();

    void setScriptRoot(const std::string& path);
	const std::string& getScriptRoot() const { return m_scriptRoot; }

    void setSqlMapperRoot(const std::string& path);
    const std::string& getSqlMapperRoot() const { return m_sqlMapperRoot; }

    void setServiceFindPaths(const sol::table &dstList);
    const std::vector<std::string>& getServiceFindPaths() { return m_serviceFindPaths; }

	void preloadProto(const std::string& path);
	sol::object getProtoContent(const sol::this_state& ts);
public:
    void initSnowflake(uint64_t timestamp, int workerId, int datacenterId);
    int64_t nextId();
public:
    void submitIo(std::function<int()>&& fn);
public:
    uint32_t submitMainTimer(std::function<void()>&& fn);
    void startMainTimer();
    void stopMainTimer(uint32_t timerId);
private:
    void closeState();
    void initInstance();
    void initLog();
    void initLua();
private:
    std::shared_ptr<spdlog::logger>                             m_logger;
    std::optional<sol::state>                                   m_state;
    std::shared_ptr<MFNetManager>                               m_netManager;
    std::shared_ptr<MFLuaShareData>                             m_shareData;
    std::unique_ptr<MFFileWatchar>                              m_fileWatch;
	std::unique_ptr<MFIoPool>                                   m_ioPool;
    std::vector<std::string>                                    m_serviceFindPaths;
private:
    static MFApplication*                                       m_instance;
    MFObjectPool<MFScheduleMessage>*                            m_schedulePool;
    bool                                                        m_shutdown;
	std::string 											    m_scriptRoot;
    std::string                                                 m_sqlMapperRoot;
	MFSnowflake*                                                m_snowflake;
private:
    MFFastMap<uint32_t, std::function<void()>>                  m_timerFnMap;
    std::vector<uint32_t>                                       m_waitingRemoveTimerIds;
    std::vector<std::string>                                    m_protoContentVec;
};


#endif /* MFApplication_h */
