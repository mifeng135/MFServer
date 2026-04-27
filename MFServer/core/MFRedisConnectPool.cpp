#include "MFRedisConnectPool.hpp"

#include "drogon/drogon.h"

#include "MFUtil.hpp"
#include "MFApplication.hpp"
#include "MFRedisClient.hpp"
#include "MFLuaMessage.hpp"
#include "MFLuaService.hpp"
#include "MFLuaServiceManager.hpp"
#include "MFJsonConfig.hpp"


MFRedisConnectPool::MFRedisConnectPool()
: m_port(0)
, m_useDB(0)
, m_minPoolSize(0)
, m_maxPoolSize(0)
, m_maxIdleTime(0)
, m_currentPoolSize(0)
, m_timerId(0) {

}

MFRedisConnectPool::~MFRedisConnectPool() {
    MFApplication::getInstance()->stopMainTimer(m_timerId);
    MFRedisClient* client;
    while (m_redisContextPool.try_dequeue(client)) {
        delete client;
    }
}

void MFRedisConnectPool::createConnect() {
    if (m_currentPoolSize >= m_maxPoolSize) {
        return;
    }

    MFRedisClient* client = new MFRedisClient();
    ++m_currentPoolSize;
    client->init(m_url, m_port, [this](MFRedisClient* clientInit, bool success) {
        if (!success) {
            MFApplication::getInstance()->logInfo("Redis createConnect failed {}:{}", m_url, m_port);
            --m_currentPoolSize;
            deleteClient(clientInit);
            return;
        }
        clientInit->lastUsedTime = std::chrono::steady_clock::now();
        m_redisContextPool.enqueue(clientInit);
    }, m_password);
}

void MFRedisConnectPool::init(const std::string& url, int port, const std::string& password, int useDB, int minPoolSize, int maxPoolSize, int maxIdleTime) {
    m_url = url;
    m_port = port;
    m_password = password;
    m_useDB = useDB;
    m_minPoolSize = minPoolSize;
    m_maxPoolSize = maxPoolSize;
    m_maxIdleTime = maxIdleTime;

    for (int i = 0; i < m_minPoolSize; i++) {
        createConnect();
    }
    m_timerId = MFApplication::getInstance()->submitMainTimer([this] {
        MFApplication::getInstance()->submitIo([this]()-> int {
            clearExpiredConnect();
            return 1;
        });
    });
}

void MFRedisConnectPool::deleteClient(MFRedisClient* client) {
    MFApplication::getInstance()->submitIo([client]()-> int {
        delete client;
        return 1;
    });
}

MFRedisClient* MFRedisConnectPool::getConnect() {
    MFRedisClient* connect = nullptr;
    if (m_redisContextPool.try_dequeue(connect)) {
        connect->lastUsedTime = std::chrono::steady_clock::now();
        return connect;
    }
    createConnect();
    m_redisContextPool.wait_dequeue(connect);
    connect->lastUsedTime = std::chrono::steady_clock::now();
    return connect;
}

void MFRedisConnectPool::releaseConnect(MFRedisClient* connect) {
    if (!connect->isIdle()) {
		--m_currentPoolSize;
        createConnect();
        deleteClient(connect);
        return;
    }
    m_redisContextPool.enqueue(connect);
}

void MFRedisConnectPool::clearExpiredConnect() {
    if (m_currentPoolSize <= m_minPoolSize) {
        return;
    }

    std::unique_lock lock(m_clearMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    int checkedCount = 0;
    const int maxCheck = m_maxPoolSize;

    while (checkedCount < maxCheck && m_currentPoolSize > m_minPoolSize) {
        MFRedisClient* client = nullptr;
        if (!m_redisContextPool.try_dequeue(client)) {
            break;
        }
        checkedCount++;
        auto idleTime = std::chrono::duration_cast<std::chrono::milliseconds>(now - client->lastUsedTime).count();
        if (idleTime < m_maxIdleTime) {
            m_redisContextPool.enqueue(client);
            break;
        }
        delete client;
        --m_currentPoolSize;
    }
}

MFRedisPoolManager* MFRedisPoolManager::m_instance = nullptr;

MFRedisPoolManager::MFRedisPoolManager() = default;

MFRedisPoolManager::~MFRedisPoolManager() {
    std::unique_lock ul(m_poolMtx);
    for (auto& [k, v] : m_redisNativePools) {
        delete v;
    }
    m_redisNativePools.clear();
}

MFRedisPoolManager* MFRedisPoolManager::getInstance() {
    if (!m_instance) {
        m_instance = new MFRedisPoolManager();
    }
    return m_instance;
}

void MFRedisPoolManager::destroyInstance() {
    if (m_instance) {
        delete m_instance;
        m_instance = nullptr;
    }
}

MFNativeLuaRedis* MFRedisPoolManager::getNativeLuaRedis(int key, const sol::this_state& state) {
    {
        std::shared_lock lk(m_poolMtx);
        auto it = m_redisNativePools.find(key);
        if (it != m_redisNativePools.end()) {
            return it->second;
        }
    }

    std::lock_guard<MFSpinLock> guard(m_createLock);
    {
        std::shared_lock lk(m_poolMtx);
        auto it = m_redisNativePools.find(key);
        if (it != m_redisNativePools.end()) {
            return it->second;
        }
    }

    auto config = MFJsonConfig::instance().get(MFRedisConfig, std::to_string(key));
    if (config == nullptr || !config->isObject()) {
        MFApplication::getInstance()->logInfo("MFRedisPoolManager::getNativeLuaRedis not found config for key = {}", key);
        return nullptr;
    }

    MFRedisConnectPool* pool = new MFRedisConnectPool();
    const std::string& ip = (*config)["ip"].asString();
    int port = (*config)["port"].asInt();
    const std::string& password = (*config)["password"].asString();
    int db = (*config)["db"].asInt();
    int minPoolSize = (*config)["minPoolSize"].asInt();
    int maxPoolSize = (*config)["maxPoolSize"].asInt();
    int maxIdleTime = (*config)["maxIdleTime"].asInt();

    pool->init(ip, port, password, db, minPoolSize, maxPoolSize, maxIdleTime);
    MFNativeLuaRedis* nativeLuaRedis = new MFNativeLuaRedis(pool);
    {
        std::unique_lock ul(m_poolMtx);
        m_redisNativePools[key] = nativeLuaRedis;
    }
    return nativeLuaRedis;
}

size_t MFRedisPoolManager::executeAsync(MFServiceId_t serviceId, int key, const sol::variadic_args& args, const sol::this_state& state)
{
    MFNativeLuaRedis* nativeLua = getNativeLuaRedis(key, state);
    if (nativeLua) {
        return nativeLua->executeAsync(serviceId, args, state);
    }
    return 0;
}

sol::object MFRedisPoolManager::executeSync(int key, int timeOut, const sol::variadic_args& args, const sol::this_state& state)
{
    MFNativeLuaRedis* nativeLua = getNativeLuaRedis(key, state);
    if (nativeLua) {
        return nativeLua->executeSync(timeOut, args, state);
    }
    return nullptr;
}

/////////////////////////////MFNativeLuaRedis///////////////////////////////
MFNativeLuaRedis::MFNativeLuaRedis(MFRedisConnectPool* pool)
: m_pool(pool) {
	m_redisCmdPool = new MFObjectPool<MFRedisMessage>();
}

MFNativeLuaRedis::~MFNativeLuaRedis() {
    delete m_pool;
	delete m_redisCmdPool;
}

size_t MFNativeLuaRedis::executeAsync(MFServiceId_t serviceId, const sol::variadic_args& args, const sol::this_state& state)
{
    MFRedisClient* connection = m_pool->getConnect();
    if (!connection) {
        MFApplication::getInstance()->logInfo("Failed to get Redis connection from pool");
        return 0;
    }
	size_t size = args.size();
    std::vector<std::string> stringArgs;

    for (size_t i = 0; i < size; ++i) {
        stringArgs.push_back(args[i].as<std::string>());
    }

    size_t sessionId = MFUtil::genSessionId();
    connection->execute(stringArgs, serviceId, sessionId, [connection, this](MFRedisResult&& result) {
        m_pool->releaseConnect(connection);
        dispatchRedisCommand(std::move(result));
	});
    
    return sessionId;
}

sol::object MFNativeLuaRedis::executeSync(int timeOut, const sol::variadic_args& args, const sol::this_state& state)
{
    MFRedisClient* connection = m_pool->getConnect();
    if (!connection) {
        MFApplication::getInstance()->logInfo("Failed to get Redis connection from pool");
        return sol::lua_nil;
    }

    size_t size = args.size();
    std::vector<std::string> stringArgs;

    for (size_t i = 0; i < size; ++i) {
        stringArgs.push_back(args[i].as<std::string>());
    }

    auto redisPro = std::make_shared<std::promise<MFRedisResult>>();
    std::future<MFRedisResult> future = redisPro->get_future();
    connection->execute(stringArgs, 0, 0, [redisPro, this, connection](MFRedisResult&& result) {
        redisPro->set_value(std::move(result));
        m_pool->releaseConnect(connection);
    });

    auto status = future.wait_for(std::chrono::seconds(timeOut));//timeout 3 second
    if (status == std::future_status::ready) {
        MFRedisResult res = future.get();
        sol::state_view lua(state);
        return MFUtil::redisReplyToLuaObject(res.reply, lua);
    }
    std::string cmd;
    for (const auto& str : stringArgs) {
        cmd.append(str);
        cmd.push_back(' ');
    }
    MFApplication::getInstance()->logInfo("executeSync timeOut cmd = {}", cmd);
    return sol::lua_nil;
}

void MFNativeLuaRedis::dispatchRedisCommand(MFRedisResult&& result)
{
    MFRedisMessage* msg = m_redisCmdPool->pop();
    msg->setPool(m_redisCmdPool);
    msg->setDst(result.serviceId);
    msg->setSessionId(result.sessionId);
    msg->setMessageType(LuaMessageTypeRedisCmd);
    msg->setCallback([result = std::move(result)](MFLuaService* service) -> void {
        service->redisMessage(result, LuaMessageTypeRedisCmd);
    });
    MFLuaServiceManager::getInstance()->nativeDispatch(msg);
}
