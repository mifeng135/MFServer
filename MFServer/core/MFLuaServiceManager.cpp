#include "MFLuaServiceManager.hpp"
#include "MFUtil.hpp"
#include "MFApplication.hpp"
#include "MFLuaMessage.hpp"
#include "MFLuaService.hpp"

MFLuaServiceManager* MFLuaServiceManager::m_instance = nullptr;

MFLuaServiceManager::MFLuaServiceManager()
: m_threadCount(0)
, m_shutdown(false) {
    m_serviceMsgPool = new MFObjectPool<MFForwardMessage>();
    m_serviceMultiMsgPool = new MFObjectPool<MFMultiForwardMessage>();
    m_reloadMsgPool = new MFObjectPool<MFHotReloadMessage>();
}

MFLuaServiceManager::~MFLuaServiceManager() {
    for (auto it : m_threadVec) {
        if (it->joinable()) {
            it->join();
        }
        delete it;
    }
    m_serviceMap.for_each([](const MFServiceId_t&, MFLuaService* v) {
        delete v;
    });
    m_serviceMap.clear();
    delete m_serviceMsgPool;
    delete m_serviceMultiMsgPool;
    delete m_reloadMsgPool;
}

MFLuaServiceManager* MFLuaServiceManager::getInstance() {
    if (!m_instance) {
        m_instance = new MFLuaServiceManager();
    }
    return m_instance;
}

void MFLuaServiceManager::destroyInstance() {
    if (m_instance) {
        m_instance->shutdown();
        delete m_instance;
        m_instance = nullptr;
    }
}

void MFLuaServiceManager::init(int threadCount) {
    m_threadCount = threadCount;
    startWorkThread();
    MFApplication::getInstance()->logInfo("serviceWork threadCount = {}", threadCount);
}


MFServiceId_t MFLuaServiceManager::addService(const std::string& serviceName) {
    MFServiceId_t serviceId = MFUtil::genServiceId();
    MFLuaService* service = new MFLuaService(serviceId, serviceName.c_str());
    service->init();
    m_serviceMap.insert(serviceId, service);
    return serviceId;
}

MFServiceId_t MFLuaServiceManager::addUniqueService(const std::string &serviceName) {
    MFServiceId_t serviceId = MFUtil::genServiceId();
    MFLuaService* service = new MFLuaService(serviceId, serviceName.c_str());
    service->init();
    m_serviceMap.insert(serviceId, service);
    m_serviceNameMap.insert(serviceName, serviceId);
    return serviceId;
}

MFServiceId_t MFLuaServiceManager::queryUniqueService(const std::string& serviceName) {
    MFServiceId_t serviceId = 0;
    m_serviceNameMap.find(serviceName, serviceId);
    return serviceId;
}

void MFLuaServiceManager::closeService(MFServiceId_t removeServiceId)
{
    MFLuaService* service = getService(removeServiceId);
    if (!service) {
        return;
    }
    MFForwardMessage* message = m_serviceMsgPool->pop();
    message->setPool(m_serviceMsgPool);
    message->setSrc(0);
    message->setData(nullptr);
    message->setLen(0);
    message->setSessionId(0);
    message->setMessageType(LuaMessageTypeCloseService);
    service->pushMessage(message, false);
    enqueueService(service);
    std::string serviceName = service->getServiceName();
    m_serviceMap.erase(removeServiceId);
    m_serviceNameMap.erase(serviceName);
}

void MFLuaServiceManager::nativeDispatch(MFMessage *message, bool isPriorityQueue) {
    MFLuaService* service = getService(message->getDst());
    if (!service) {
        MFApplication::getInstance()->logInfo("MFLuaServiceManager nativeDispatch not find serviceId = {}", message->getDst());
        message->reset();
        return;
    }
    service->pushMessage(message, isPriorityQueue);
    enqueueService(service);
}

void MFLuaServiceManager::nativeDispatchHotReload(const std::string& moduleName, const std::string& fullName) {
    std::vector<MFServiceId_t> serviceIds = m_serviceNameMap.values();

    MFHotReloadMessage* message = m_reloadMsgPool->pop();
    message->setPool(m_reloadMsgPool);
    message->setRefCount(static_cast<int>(serviceIds.size()));
    message->setMessageType(LuaMessageTypeHotReload);
    message->setModule(moduleName);
    message->setFullPath(fullName);
    for (MFServiceId_t sid : serviceIds) {
        MFLuaService* service = getService(sid);
        if (!service) {
            message->reset();
            continue;
        }
        service->pushMessage(message, false);
        enqueueService(service);
    }
}

void MFLuaServiceManager::nativeDispatchReloadConfig() {
    std::vector<MFServiceId_t> serviceIds = m_serviceNameMap.values();

    MFHotReloadMessage* message = m_reloadMsgPool->pop();
    message->setPool(m_reloadMsgPool);
    message->setRefCount(static_cast<int>(serviceIds.size()));
    message->setMessageType(LuaMessageTypeReloadConfig);
    for (MFServiceId_t sid : serviceIds) {
        MFLuaService* service = getService(sid);
        if (!service) {
            message->reset();
            continue;
        }
        service->pushMessage(message, false);
        enqueueService(service);
    }
}

void MFLuaServiceManager::sendMulti(MFServiceId_t src, void *data, size_t len, const sol::table &dstList) {
    auto size = dstList.size();
    MFMultiForwardMessage* message = m_serviceMultiMsgPool->pop();
    message->setPool(m_serviceMultiMsgPool);
    message->setSrc(src);
    message->setData(data);
    message->setLen(len);
    message->setSessionId(0);
    message->setMessageType(LuaMessageTypeMultiSend);
    message->setRefCount(static_cast<int>(size));
    for (auto& pair : dstList) {
        MFServiceId_t dst = pair.second.as<MFServiceId_t>();
        MFLuaService* service = getService(dst);
        if (!service) {
            message->reset();
            continue;
        }
        service->pushMessage(message, false);
        enqueueService(service);
    }
}

size_t MFLuaServiceManager::send(MFServiceId_t src, MFServiceId_t dst, void *data, size_t len, int msgType, bool genSessionId, bool usePriority) {
    MFLuaService* service = getService(dst);
    if (!service) {
        MFApplication::getInstance()->logInfo("MFLuaServiceManager send not find serviceId = {}", dst);
        return 0;
    }
    size_t sessionId = 0;
    if (genSessionId) {
        sessionId = MFUtil::genSessionId();
    }
    MFForwardMessage* message = m_serviceMsgPool->pop();
    message->setPool(m_serviceMsgPool);
    message->setSrc(src);
    message->setDst(dst);
    message->setData(data);
    message->setLen(len);
    message->setSessionId(sessionId);
    message->setMessageType(msgType);
    service->pushMessage(message, usePriority);
    enqueueService(service);
    return sessionId;
}

void MFLuaServiceManager::ret(MFServiceId_t src, MFServiceId_t dst, void *data, size_t len, size_t sessionId, int messageType, bool usePriority) {
    MFLuaService* service = getService(dst);
    if (!service) {
        return;
    }
    MFForwardMessage* message = m_serviceMsgPool->pop();
    message->setPool(m_serviceMsgPool);
    message->setSrc(src);
    message->setDst(dst);
    message->setData(data);
    message->setLen(len);
    message->setMessageType(messageType);
    message->setSessionId(sessionId);
    service->pushMessage(message, usePriority);
    enqueueService(service);
}

void MFLuaServiceManager::startWorkThread() {
    for (int i = 0; i < m_threadCount; i++) {
        std::thread* thread = new (std::nothrow) std::thread(&MFLuaServiceManager::serviceWorkThread, this);
        m_threadVec.emplace_back(thread);
    }
}

void MFLuaServiceManager::serviceWorkThread()
{
#if defined(__APPLE__)
    pthread_setname_np("luaService");
#endif
    while (true) {
        MFLuaService* svc = nullptr;
        m_readyServiceQueue.wait_dequeue(svc);
        if (svc) {
            svc->processMsg();
            svc->m_inReadyQueue.store(false);
        } else {
            if (m_shutdown) {
                break;
            }
        }
    }
}

void MFLuaServiceManager::shutdown() {
    m_shutdown = true;
    for (int i = 0; i < m_threadCount; i++) {
        m_readyServiceQueue.enqueue(nullptr);
    }
}

void MFLuaServiceManager::enqueueService(MFLuaService *service) {
    if (!service->m_inReadyQueue.exchange(true, std::memory_order_acq_rel)) {
        m_readyServiceQueue.enqueue(service);
    }
}

MFLuaService* MFLuaServiceManager::getService(MFServiceId_t serviceId) {
    MFLuaService* service = nullptr;
    m_serviceMap.find(serviceId, service);
    return service;
}
