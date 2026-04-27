#include "MFMysqlConnectPool.hpp"
#include "MFMysqlClient.hpp"
#include "MFUtil.hpp"
#include "MFLuaServiceManager.hpp"
#include "MFApplication.hpp"
#include "MFLuaMessage.hpp"
#include "MFLuaService.hpp"
#include "MFJsonConfig.hpp"

MFSqlConnection::MFSqlConnection(MFMysqlClient *client, int maxIdleTime)
: m_client(client)
, m_lastUsedTime(std::chrono::steady_clock::now())
, m_maxIdleTime(maxIdleTime) {

}

MFSqlConnection::~MFSqlConnection() {
    delete m_client;
}

bool MFSqlConnection::isValid() {
    auto now = std::chrono::steady_clock::now();
    auto idleTime = std::chrono::duration_cast<std::chrono::milliseconds>(now - getLastUsedTime()).count();
    if (idleTime > m_maxIdleTime || m_client->getState() == MFMysqlState::Disconnected) {
        return false;
    }
    return true;
}

MFMysqlConnectPool::MFMysqlConnectPool()
: m_currentPoolSize(0)
, m_port(0)
, m_maxPoolSize(0)
, m_maxIdleTime(0)
, m_minPoolSize(0)
, m_timerId(0) {
    m_sqlMsgPool = new MFObjectPool<MFMysqlMessage>();
}

MFMysqlConnectPool::~MFMysqlConnectPool() {
    MFApplication::getInstance()->stopMainTimer(m_timerId);
    MFSqlConnection* connect;
    while (m_connectPool.try_dequeue(connect)) {
        delete connect;
    }
    delete m_sqlMsgPool;
}

void MFMysqlConnectPool::init(const std::string &url, int port, const std::string& user, const std::string &password, const std::string& database,
    int minPoolSize, int maxPoolSize, int maxIdleTime) {
    m_url = url;
    m_port = port;
    m_user = user;
    m_password = password;
    m_database = database;
    m_maxPoolSize = maxPoolSize;
    m_maxIdleTime = maxIdleTime;
    m_minPoolSize = minPoolSize;
    for (int i = 0; i < m_minPoolSize; i++) {
        createConnect();
    }
    m_timerId = MFApplication::getInstance()->submitMainTimer([this] {
        MFApplication::getInstance()->submitIo([this]() ->int {
            clearExpiredConnect();
            return 1;
        });
    });
}


size_t MFMysqlConnectPool::queryAsync(const std::string &sql, MFServiceId_t serviceId) {
    size_t sessionId = MFUtil::genSessionId();
    MFSqlConnection* connect = getConnection();
    if (!connect) {
        dispatchAcquireError(serviceId, sessionId, true, false, sql);
        return sessionId;
    }
    connect->getClient()->execute(sql, serviceId, sessionId, [connect, this, sql](MFMysqlResult&& result) {
        releaseConnect(connect);
        dispatchQuery(std::move(result), false, sql);
    });
    return sessionId;
}

size_t MFMysqlConnectPool::queryOneAsync(const std::string &sql, MFServiceId_t serviceId) {
    size_t sessionId = MFUtil::genSessionId();
    MFSqlConnection* connect = getConnection();
    if (!connect) {
        dispatchAcquireError(serviceId, sessionId, true, true, sql);
        return sessionId;
    }
    connect->getClient()->execute(sql, serviceId, sessionId, [connect, this, sql](MFMysqlResult&& result) {
        releaseConnect(connect);
        dispatchQuery(std::move(result), true, sql);
    });
    return sessionId;
}

size_t MFMysqlConnectPool::executeAsync(const std::string &sql, MFServiceId_t serviceId) {
    size_t sessionId = MFUtil::genSessionId();
    MFSqlConnection* connect = getConnection();
    if (!connect) {
        dispatchAcquireError(serviceId, sessionId, false, false, sql);
        return sessionId;
    }
    connect->getClient()->execute(sql, serviceId, sessionId, [connect, this, sql](MFMysqlResult&& result) {
        releaseConnect(connect);
        dispatchExecute(std::move(result), sql);
    });
    return sessionId;
}

MFSqlConnection* MFMysqlConnectPool::getConnection() {
    MFSqlConnection* connect = nullptr;
    if (m_connectPool.try_dequeue(connect)) {
        connect->setLastUsedTime(std::chrono::steady_clock::now());
        return connect;
    }
    createConnect();
    m_connectPool.wait_dequeue(connect);
    connect->setLastUsedTime(std::chrono::steady_clock::now());
    return connect;
}

void MFMysqlConnectPool::createConnect() {
    if (m_currentPoolSize >= m_maxPoolSize) {
        return;
    }
    MFMysqlClient* client = new MFMysqlClient();
    ++m_currentPoolSize;
    client->init(m_url, m_port, m_user, m_password, m_database, [this](MFMysqlClient* clientInit, bool success) {
        if (!success) {
            MFApplication::getInstance()->logInfo("MySQL createConnect failed {}:{}:{}", m_url, m_port, m_database);
            --m_currentPoolSize;
            deleteSqlClient(clientInit);
            return;
        }
        MFSqlConnection* connection = new MFSqlConnection(clientInit, m_maxIdleTime);
        connection->setLastUsedTime(std::chrono::steady_clock::now());
        m_connectPool.enqueue(connection);
    });
}

void MFMysqlConnectPool::releaseConnect(MFSqlConnection *connection) {
    if (!connection->getClient()->isIdle()) {
        --m_currentPoolSize;
        createConnect();
        deleteSqlConnection(connection);
        return;
    }
    m_connectPool.enqueue(connection);
}

void MFMysqlConnectPool::clearExpiredConnect() {
    if (m_currentPoolSize <= m_minPoolSize) {
        return;
    }

    std::unique_lock lock(m_clearMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        return;
    }

    int checkedCount = 0;
    const int maxCheck = m_maxPoolSize;

    while (checkedCount < maxCheck && m_currentPoolSize > m_minPoolSize) {
        MFSqlConnection* conn = nullptr;
        if (!m_connectPool.try_dequeue(conn)) {
            break; 
        }
        checkedCount++;
        if (conn->isValid()) {
            m_connectPool.enqueue(conn);
            break;
        }
        delete conn;
        --m_currentPoolSize;
    }
}

void MFMysqlConnectPool::dispatchExecute(MFMysqlResult&& result, const std::string& sql) {
    MFMysqlMessage* msg = m_sqlMsgPool->pop();
    msg->setPool(m_sqlMsgPool);
    msg->setDst(result.serviceId);
    msg->setMessageType(LuaMessageTypeMysqlExecute);
    msg->setSessionId(result.sessionId);
    msg->setQuery(false);
    msg->setSuccess(result.success);
    msg->setExecRes(result.affectedRows > 0);
    MFLuaServiceManager::getInstance()->nativeDispatch(msg);
    if (!result.success) {
        MFApplication::getInstance()->logInfo("execute fail sql = {}, error = {}", sql, result.error);
    }
}

void MFMysqlConnectPool::dispatchAcquireError(MFServiceId_t serviceId, size_t sessionId, bool query, bool queryOne, const std::string& sql) {
    MFMysqlResult result(sessionId, serviceId);
    result.success = false;
    result.error = "Failed to acquire MySQL connection";
    if (query) {
        dispatchQuery(std::move(result), queryOne, sql);
    } else {
        dispatchExecute(std::move(result), sql);
    }
}

void MFMysqlConnectPool::deleteSqlConnection(MFSqlConnection* connection) {
    MFApplication::getInstance()->submitIo([connection]() -> int {
        delete connection;
        return 1;
    });
}

void MFMysqlConnectPool::deleteSqlClient(MFMysqlClient* client) {
    MFApplication::getInstance()->submitIo([client]() -> int {
        delete client;
        return 1;
    });
}

void MFMysqlConnectPool::dispatchQuery(MFMysqlResult&& result, bool queryOne, const std::string& sql) {
    if (!result.success) {
        MFApplication::getInstance()->logInfo("execute faile sql = {}, error = {}", sql, result.error);
    }
    MFMysqlMessage* msg = m_sqlMsgPool->pop();
    msg->setPool(m_sqlMsgPool);
    msg->setSuccess(result.success);
    msg->setDst(result.serviceId);
    msg->setMessageType(LuaMessageTypeMysqlQuery);
    msg->setSessionId(result.sessionId);
    msg->setQueryOne(queryOne);
    msg->setCallback([result = std::move(result), queryOne](MFLuaService* service) -> void {
		service->sqlMessageQuery(result, queryOne, LuaMessageTypeMysqlQuery);
    });
    MFLuaServiceManager::getInstance()->nativeDispatch(msg);
}

size_t MFMysqlConnectPool::beginTransaction(MFServiceId_t serviceId) {
    size_t transactionId = MFUtil::genSessionId();
    MFSqlConnection* connect = getConnection();
    if (!connect) {
        dispatchAcquireError(serviceId, transactionId, false, false, "START TRANSACTION");
        return transactionId;
    }
    m_transactionConnections.insert(transactionId, connect);
    executeOnTransaction(transactionId, serviceId, "START TRANSACTION", TxRelease::OnFailure);
    return transactionId;
}

size_t MFMysqlConnectPool::executeInTransaction(size_t transactionId, const std::string& sql, MFServiceId_t serviceId) {
    return executeOnTransaction(transactionId, serviceId, sql, TxRelease::Never);
}

size_t MFMysqlConnectPool::commitTransaction(size_t transactionId, MFServiceId_t serviceId) {
    return executeOnTransaction(transactionId, serviceId, "COMMIT", TxRelease::Always);
}

size_t MFMysqlConnectPool::rollbackTransaction(size_t transactionId, MFServiceId_t serviceId) {
    return executeOnTransaction(transactionId, serviceId, "ROLLBACK", TxRelease::Always);
}

size_t MFMysqlConnectPool::executeOnTransaction(size_t transactionId, MFServiceId_t serviceId, std::string sql, TxRelease policy) {
    size_t sessionId = MFUtil::genSessionId();

    MFSqlConnection* connect = nullptr;
    if (!m_transactionConnections.find(transactionId, connect)) {
        MFMysqlResult result(sessionId, serviceId);
        result.success = false;
        result.error = "Transaction not found";
        dispatchExecute(std::move(result), sql);
        return sessionId;
    }

    connect->getClient()->execute(sql, serviceId, sessionId, [this, transactionId, connect, policy, sql](MFMysqlResult&& result) {
        const bool lost = connect->getClient()->getState() == MFMysqlState::Disconnected;
        const bool shouldRelease = lost || policy == TxRelease::Always || (policy == TxRelease::OnFailure && !result.success);
        if (shouldRelease) {
            MFSqlConnection* conn = nullptr;
            if (m_transactionConnections.extract(transactionId, conn)) {
                releaseConnect(conn);
            }
        }
        dispatchExecute(std::move(result), sql);
    });
    return sessionId;
}


MFMysqlPoolManager* MFMysqlPoolManager::m_instance = nullptr;

MFMysqlPoolManager::MFMysqlPoolManager() {

}

MFMysqlPoolManager::~MFMysqlPoolManager() {
    std::unique_lock ul(m_poolMtx);
    for (auto& [k, v] : m_sqlPoolMap) {
        delete v;
    }
    m_sqlPoolMap.clear();
}

MFMysqlPoolManager* MFMysqlPoolManager::getInstance() {
    if (!m_instance) {
        m_instance = new MFMysqlPoolManager();
    }
    return m_instance;
}

void MFMysqlPoolManager::destroyInstance() {
    if (m_instance) {
        delete m_instance;
        m_instance = nullptr;
    }
}

MFMysqlConnectPool* MFMysqlPoolManager::getConnectPool(int key, const sol::this_state& state) {
    {
        std::shared_lock lk(m_poolMtx);
        auto it = m_sqlPoolMap.find(key);
        if (it != m_sqlPoolMap.end()) {
            return it->second;
        }
    }

    std::lock_guard<MFSpinLock> guard(m_createLock);
    {
        std::shared_lock lk(m_poolMtx);
        auto it = m_sqlPoolMap.find(key);
        if (it != m_sqlPoolMap.end()) {
            return it->second;
        }
    }

    auto config = MFJsonConfig::instance().get(MFSqlConfig, std::to_string(key));
    if (config == nullptr || !config->isObject()) {
        MFApplication::getInstance()->logInfo("MFMysqlPoolManager::getConnectPool not found config for key = {}", key);
        return nullptr;
    }

    const std::string& ip = (*config)["ip"].asString();
    int port = (*config)["port"].asInt();
    const std::string& userName = (*config)["userName"].asString();
    const std::string& password = (*config)["password"].asString();
    const std::string& database = (*config)["database"].asString();
    int minPoolSize = (*config)["minPoolSize"].asInt();
    int maxPoolSize = (*config)["maxPoolSize"].asInt();
    int maxIdleTime = (*config)["maxIdleTime"].asInt();
    
    MFMysqlConnectPool* pool = new MFMysqlConnectPool();
    pool->init(ip, port, userName, password, database, minPoolSize, maxPoolSize, maxIdleTime);
    {
        std::unique_lock ul(m_poolMtx);
        m_sqlPoolMap[key] = pool;
    }
    return pool;
}

size_t MFMysqlPoolManager::queryAsync(const std::string& sql, MFServiceId_t serviceId, int key, const sol::this_state& state)
{
	MFMysqlConnectPool* pool = getConnectPool(key, state);
	return pool->queryAsync(sql, serviceId);
}

size_t MFMysqlPoolManager::queryOneAsync(const std::string& sql, MFServiceId_t serviceId, int key, const sol::this_state& state)
{
    MFMysqlConnectPool* pool = getConnectPool(key, state);
	return pool->queryOneAsync(sql, serviceId);
}

size_t MFMysqlPoolManager::executeAsync(const std::string& sql, MFServiceId_t serviceId, int key, const sol::this_state& state)
{
    MFMysqlConnectPool* pool = getConnectPool(key, state);
	return pool->executeAsync(sql, serviceId);
}

size_t MFMysqlPoolManager::beginTransaction(MFServiceId_t serviceId, int key, const sol::this_state& state)
{
    MFMysqlConnectPool* pool = getConnectPool(key, state);
	return pool->beginTransaction(serviceId);
}

size_t MFMysqlPoolManager::executeInTransaction(size_t transactionId, const std::string& sql, MFServiceId_t serviceId, int key, const sol::this_state& state)
{
    MFMysqlConnectPool* pool = getConnectPool(key, state);
	return pool->executeInTransaction(transactionId, sql, serviceId);
}

size_t MFMysqlPoolManager::commitTransaction(size_t transactionId, MFServiceId_t serviceId, int key, const sol::this_state& state)
{
    MFMysqlConnectPool* pool = getConnectPool(key, state);
	return pool->commitTransaction(transactionId, serviceId);
}

size_t MFMysqlPoolManager::rollbackTransaction(size_t transactionId, MFServiceId_t serviceId, int key, const sol::this_state& state)
{
    MFMysqlConnectPool* pool = getConnectPool(key, state);
	return pool->rollbackTransaction(transactionId, serviceId);
}
