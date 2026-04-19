#ifndef MFLuaServiceManager_hpp
#define MFLuaServiceManager_hpp

#include "MFObjectPool.hpp"
#include "MFStripedMap.hpp"
#include "sol/sol.hpp"


class MFMessage;
class MFForwardMessage;
class MFMultiForwardMessage;
class MFLuaService;
class MFHotReloadMessage;

class MFLuaServiceManager
{
public:
    MFLuaServiceManager();
    ~MFLuaServiceManager();
public:
    static MFLuaServiceManager* getInstance();
    static void destroyInstance();
public:
    void init(int threadCount);

    MFServiceId_t addService(const std::string& serviceName);
    MFServiceId_t addUniqueService(const std::string& serviceName);
    MFServiceId_t queryUniqueService(const std::string& serviceName);
    void closeService(MFServiceId_t removeServiceId);
public:
    void nativeDispatch(MFMessage* message, bool isPriorityQueue = false);
    void nativeDispatchHotReload(const std::string& moduleName, const std::string& fullName);
    void nativeDispatchReloadConfig();
    void sendMulti(MFServiceId_t src, void *data, size_t len, const sol::table &dstList);
    size_t send(MFServiceId_t src, MFServiceId_t dst, void *data, size_t len, int msgType, bool genSessionId, bool usePriority = false);
    void ret(MFServiceId_t src, MFServiceId_t dst, void *data, size_t len, size_t sessionId, int messageType, bool usePriority = false);
private:
    void startWorkThread();
    void serviceWorkThread();
    void shutdown();
    void enqueueService(MFLuaService* service);
public:
    MFLuaService* getService(MFServiceId_t serviceId);
public:
    MFStripedMap<MFServiceId_t, MFLuaService*>        m_serviceMap;
    MFStripedMap<std::string, MFServiceId_t, 8>       m_serviceNameMap;
    std::vector<std::thread*>                       m_threadVec;
private:
    static MFLuaServiceManager*                     m_instance;
    int                                             m_threadCount;  
    bool                                            m_shutdown;
    MFObjectPool<MFForwardMessage>*                 m_serviceMsgPool;
    MFObjectPool<MFMultiForwardMessage>*            m_serviceMultiMsgPool;
    MFObjectPool<MFHotReloadMessage>*               m_reloadMsgPool;
    MFBlockingQueue<MFLuaService*>                  m_readyServiceQueue{ 2048 };
};

#endif /* MFLuaServiceManager_hpp */
