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

bool MFSqlConnection::isVaild() {
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
        m_connectPool.enqueue(createConnect());
    }
    m_timerId = MFApplication::getInstance()->submitMainTimer([this] {
        MFApplication::getInstance()->submitIo([this]() ->int {
            clearExpiredConnect();
            return 1;
        });
    });
}


size_t MFMysqlConnectPool::queryAsync(const std::string &sql, MFServiceId_t serviceId) {
    MFSqlConnection* connect = getConnection();
    size_t sessionId = MFUtil::genSessionId();
    connect->getClient()->execute(sql, serviceId, sessionId, [connect, this, sql](MFMysqlResult&& result) {
        releaseConnect(connect);
        dispatchQuery(std::move(result), false, sql);
    });
    return sessionId;
}

size_t MFMysqlConnectPool::queryOneAsync(const std::string &sql, MFServiceId_t serviceId) {
    MFSqlConnection* connect = getConnection();
    size_t sessionId = MFUtil::genSessionId();
    connect->getClient()->execute(sql, serviceId, sessionId, [connect, this, sql](MFMysqlResult&& result) {
        releaseConnect(connect);
        dispatchQuery(std::move(result), true, sql);
    });
    return sessionId;
}

size_t MFMysqlConnectPool::executeAsync(const std::string &sql, MFServiceId_t serviceId) {
    MFSqlConnection* connect = getConnection();
    size_t sessionId = MFUtil::genSessionId();
    connect->getClient()->execute(sql, serviceId, sessionId, [connect, this, sql](MFMysqlResult&& result) {
        releaseConnect(connect);
        dispatchExecute(std::move(result), sql);
    });
    return sessionId;
}

MFSqlConnection* MFMysqlConnectPool::getConnection() {
    MFSqlConnection* connect = nullptr;
    m_connectPool.try_dequeue(connect);
    if (connect) {
        connect->setLastUsedTime(std::chrono::steady_clock::now());
        return connect;
    }
    if (m_currentPoolSize >= m_maxPoolSize) {
        m_connectPool.wait_dequeue(connect);
        connect->setLastUsedTime(std::chrono::steady_clock::now());
        return connect;
    }
    return createConnect();
}

MFSqlConnection* MFMysqlConnectPool::createConnect() {
    MFMysqlClient* client = new MFMysqlClient();
    client->init(m_url, m_port, m_user, m_password, m_database);
    ++m_currentPoolSize;
    return new MFSqlConnection(client, m_maxIdleTime);
}

void MFMysqlConnectPool::releaseConnect(MFSqlConnection *connection) {
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
        if (conn->isVaild()) {
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
        MFApplication::getInstance()->logInfo("execute faile sql = {}, error = {}", sql, result.error);
    }
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
    MFSqlConnection* connect = getConnection();
    size_t transactionId = MFUtil::genSessionId();
    m_transactionConnections.insert(transactionId, connect);

    size_t sessionId = MFUtil::genSessionId();
    connect->getClient()->execute("START TRANSACTION", serviceId, sessionId, [this, transactionId, sql = std::string("START TRANSACTION")](MFMysqlResult&& result) {
        if (!result.success) {
            MFSqlConnection* conn = nullptr;
            if (m_transactionConnections.extract(transactionId, conn)) {
                releaseConnect(conn);
            }
        }
        dispatchExecute(std::move(result), sql);
    });
    
    return transactionId;
}

size_t MFMysqlConnectPool::executeInTransaction(size_t transactionId, const std::string& sql, MFServiceId_t serviceId) {
    MFSqlConnection* connect = checkTransactionError(transactionId, serviceId, ActionType::ExecuteInTransaction);
    if (!connect) {
        return 0;
    }
    size_t sessionId = MFUtil::genSessionId();
    connect->getClient()->execute(sql, serviceId, sessionId, [this, sql](MFMysqlResult&& result) {
        dispatchExecute(std::move(result), sql);
    });
    
    return sessionId;
}

size_t MFMysqlConnectPool::commitTransaction(size_t transactionId, MFServiceId_t serviceId) {
    MFSqlConnection* connect = checkTransactionError(transactionId, serviceId, ActionType::CommitTransaction);
    if (!connect) {
        return 0;
    }
    
    size_t sessionId = MFUtil::genSessionId();
    connect->getClient()->execute("COMMIT", serviceId, sessionId, [this, transactionId, connect, sql = std::string("COMMIT")](MFMysqlResult&& result) {
        m_transactionConnections.erase(transactionId);
        releaseConnect(connect);
        dispatchExecute(std::move(result), sql);
    });
    
    return sessionId;
}

size_t MFMysqlConnectPool::rollbackTransaction(size_t transactionId, MFServiceId_t serviceId) {
    MFSqlConnection* connect = checkTransactionError(transactionId, serviceId, ActionType::RollbackTransaction);
    if (!connect) {
        return 0;
    }
    
    size_t sessionId = MFUtil::genSessionId();
    connect->getClient()->execute("ROLLBACK", serviceId, sessionId, [this, transactionId, connect, sql = std::string("ROLLBACK")](MFMysqlResult&& result) {
        m_transactionConnections.erase(transactionId);
        releaseConnect(connect);
        dispatchExecute(std::move(result), sql);
    });
    
    return sessionId;
}

MFSqlConnection* MFMysqlConnectPool::checkTransactionError(size_t transactionId, MFServiceId_t serviceId, ActionType actionType) {
    MFSqlConnection* connect = nullptr;
    if (m_transactionConnections.find(transactionId, connect)) {
        return connect;
    }
    size_t sessionId = MFUtil::genSessionId();
    MFMysqlResult result(sessionId, serviceId);
    result.success = false;
    result.error = "Transaction not found";
    if (actionType == ActionType::CommitTransaction) {
        dispatchExecute(std::move(result), "COMMIT");
    } else if (actionType == ActionType::RollbackTransaction) {
        dispatchExecute(std::move(result), "ROLLBACK");
    }
    return nullptr;
}


MFMysqlPoolMananger* MFMysqlPoolMananger::m_instance = nullptr;

MFMysqlPoolMananger::MFMysqlPoolMananger() {

}

MFMysqlPoolMananger::~MFMysqlPoolMananger() {
    std::unique_lock ul(m_poolMtx);
    for (auto& [k, v] : m_sqlPoolMap) {
        delete v;
    }
    m_sqlPoolMap.clear();
}

MFMysqlPoolMananger* MFMysqlPoolMananger::getInstance() {
    if (!m_instance) {
        m_instance = new MFMysqlPoolMananger();
    }
    return m_instance;
}

void MFMysqlPoolMananger::destroyInstance() {
    if (m_instance) {
        delete m_instance;
        m_instance = nullptr;
    }
}

MFMysqlConnectPool* MFMysqlPoolMananger::getConnectPool(int key, const sol::this_state& state) {
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

size_t MFMysqlPoolMananger::queryAsync(const std::string& sql, MFServiceId_t service, int key, const sol::this_state& state)
{
	MFMysqlConnectPool* pool = getConnectPool(key, state);
	return pool->queryAsync(sql, service);
}

size_t MFMysqlPoolMananger::queryOneAsync(const std::string& sql, MFServiceId_t serviceId, int key, const sol::this_state& state)
{
    MFMysqlConnectPool* pool = getConnectPool(key, state);
	return pool->queryOneAsync(sql, serviceId);
}

size_t MFMysqlPoolMananger::executeAsync(const std::string& sql, MFServiceId_t serviceId, int key, const sol::this_state& state)
{
    MFMysqlConnectPool* pool = getConnectPool(key, state);
	return pool->executeAsync(sql, serviceId);
}

size_t MFMysqlPoolMananger::beginTransaction(MFServiceId_t serviceId, int key, const sol::this_state& state)
{
    MFMysqlConnectPool* pool = getConnectPool(key, state);
	return pool->beginTransaction(serviceId);
}

size_t MFMysqlPoolMananger::executeInTransaction(size_t transactionId, const std::string& sql, MFServiceId_t service, int key, const sol::this_state& state)
{
    MFMysqlConnectPool* pool = getConnectPool(key, state);
	return pool->executeInTransaction(transactionId, sql, service);
}

size_t MFMysqlPoolMananger::commitTransaction(size_t transactionId, MFServiceId_t service, int key, const sol::this_state& state)
{
    MFMysqlConnectPool* pool = getConnectPool(key, state);
	return pool->commitTransaction(transactionId, service);
}

size_t MFMysqlPoolMananger::rollbackTransaction(size_t transactionId, MFServiceId_t service, int key, const sol::this_state& state)
{
    MFMysqlConnectPool* pool = getConnectPool(key, state);
	return pool->rollbackTransaction(transactionId, service);
}
