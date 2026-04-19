#ifndef MFLuaService_hpp
#define MFLuaService_hpp

#include "sol/sol.hpp"
#include "MFLuaProfiler.hpp"

#include "trantor/utils/MPSCQueue.hpp"

class MFMessage;
struct MFMysqlResult;
struct MFRedisResult;

// empty service memory use 100K
class MFLuaService
{
public:
    explicit MFLuaService(MFServiceId_t serviceId, const char* path);
    ~MFLuaService();
public:
    void init();
    void pushMessage(MFMessage* message, bool isPriorityQueue = false);
    void processMsg();
    const std::string& getServiceName() { return m_serviceName; }
private:
    void createObject(MFMessage* msg);
    void singleMessage();
private:
    void processMessage(MFMessage* message);
public:
    void sqlMessageQuery(const MFMysqlResult& result, bool queryOne, int msgType);
    void redisMessage(const MFRedisResult& result, int msgType);
private:
	void logTraceback(sol::error& e);
	void luaTraceback(lua_State* L, lua_State* L1, const char* msg, int level);
    std::string getFileFullPath();
public:
    std::atomic<bool>                                                   m_inReadyQueue;
private:
    MFServiceId_t                                                       m_serviceId;
    sol::state                                                          m_state;
    daking::MPSC_queue<MFMessage*>                                      m_luaMessageQueue;
    std::unique_ptr<MFLuaProfiler>                                      m_profiler;
    sol::table                                                          m_messageHandlers;
    std::string                                                         m_serviceName;
};

#endif /* MFLuaService_hpp */
