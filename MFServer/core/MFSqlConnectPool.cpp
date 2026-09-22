#include "MFSqlConnectPool.hpp"
#include <mutex>
#include "MFUtil.hpp"
#include "MFLuaServiceManager.hpp"
#include "MFApplication.hpp"
#include "MFLuaMessage.hpp"
#include "MFLuaService.hpp"
#include "MFShareTable.hpp"
#include "drogon/orm/Exception.h"

namespace {

std::string escapeConnValue(const std::string& value)
{
    if (value.find_first_of(" '\\") == std::string::npos) {
        return value;
    }
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('\'');
    for (char c : value) {
        if (c == '\\' || c == '\'') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

std::string buildPgConnInfo(const std::string& host,
                            int port,
                            const std::string& user,
                            const std::string& password,
                            const std::string& database)
{
    std::string info;
    info += "host=" + escapeConnValue(host);
    info += " port=" + std::to_string(port);
    info += " dbname=" + escapeConnValue(database);
    info += " user=" + escapeConnValue(user);
    info += " password=" + escapeConnValue(password);
    return info;
}

}  // namespace

MFSqlConnectPool::MFSqlConnectPool()
: m_sqlMsgPool(new MFObjectPool<MFMysqlMessage>())
{
    
}

MFSqlConnectPool::~MFSqlConnectPool()
{
    m_client.reset();
    delete m_sqlMsgPool;
}

void MFSqlConnectPool::init(const std::string& connInfo, size_t connNum)
{
    size_t num = connNum == 0 ? 1 : connNum;
    m_client = drogon::orm::DbClient::newPgClient(connInfo, num);
}

void MFSqlConnectPool::execSql(const std::string& sql, MFServiceId_t serviceId, size_t sessionId, bool query, bool queryOne)
{
    m_client->execSqlAsync(sql, 
        [this, sessionId, serviceId, query, queryOne](const drogon::orm::Result& rows) {
            MFSqlResult result(sessionId, serviceId);
            result.success = true;
            result.result = rows;
            if (query) {
                dispatchQuery(std::move(result), queryOne);
            } else {
                dispatchExecute(std::move(result));
            }
        },
        [this, sessionId, serviceId, query, queryOne, sql](const drogon::orm::DrogonDbException& e) {
            MFApplication::getInstance()->logInfo("sql fail = {}, error = {}", sql, e.base().what());
            MFSqlResult result(sessionId, serviceId);
            result.success = false;
            result.error = e.base().what();
            if (query) {
                dispatchQuery(std::move(result), queryOne);
            } else {
                dispatchExecute(std::move(result));
            }
        });
}

size_t MFSqlConnectPool::queryAsync(const std::string& sql, MFServiceId_t serviceId)
{
    size_t sessionId = MFUtil::genSessionId();
    execSql(sql, serviceId, sessionId, true, false);
    return sessionId;
}

size_t MFSqlConnectPool::queryOneAsync(const std::string& sql, MFServiceId_t serviceId)
{
    size_t sessionId = MFUtil::genSessionId();
    execSql(sql, serviceId, sessionId, true, true);
    return sessionId;
}

size_t MFSqlConnectPool::executeAsync(const std::string& sql, MFServiceId_t serviceId)
{
    size_t sessionId = MFUtil::genSessionId();
    execSql(sql, serviceId, sessionId, false, false);
    return sessionId;
}

size_t MFSqlConnectPool::executeAsyncTransaction(const std::vector<std::string>& sqls, MFServiceId_t serviceId)
{
    size_t sessionId = MFUtil::genSessionId();
    if (!m_client || sqls.empty()) {
        MFSqlResult result(sessionId, serviceId);
        result.success = false;
        result.error = sqls.empty() ? "empty transaction" : "PostgreSQL client is not ready";
        MFApplication::getInstance()->logInfo("sql fail error = {}", result.error);
        dispatchExecute(std::move(result));
        return sessionId;
    }
    auto sqlsPtr = std::make_shared<std::vector<std::string>>(sqls);
    m_client->newTransactionAsync([this, sessionId, serviceId, sqlsPtr](const std::shared_ptr<drogon::orm::Transaction>& tx) {
        if (!tx) {
            MFSqlResult result(sessionId, serviceId);
            result.success = false;
            result.error = "begin failed";
            MFApplication::getInstance()->logInfo("sql fail error = {}", result.error);
            dispatchExecute(std::move(result));
            return;
        }
        runTx(tx, sqlsPtr, 0, sessionId, serviceId);
    });
    return sessionId;
}

void MFSqlConnectPool::runTx(const std::shared_ptr<drogon::orm::Transaction>& tx,
                               std::shared_ptr<std::vector<std::string>> sqls,
                               size_t index,
                               size_t sessionId,
                               MFServiceId_t serviceId)
{
    if (index >= sqls->size()) {
        tx->setCommitCallback([this, sessionId, serviceId](bool ok) {
            MFSqlResult result(sessionId, serviceId);
            result.success = ok;
            if (!ok) {
                result.error = "commit failed";
                MFApplication::getInstance()->logInfo("sql fail error = {}", result.error);
            }
            dispatchExecute(std::move(result));
        });
        return;
    }
    tx->execSqlAsync(
        (*sqls)[index],
        [this, tx, sqls, index, sessionId, serviceId](const drogon::orm::Result&) {
            runTx(tx, sqls, index + 1, sessionId, serviceId);
        },
        [this, tx, sqls, index, sessionId, serviceId](const drogon::orm::DrogonDbException& e) {
            tx->rollback();
            MFApplication::getInstance()->logInfo("sql fail = {}, error = {}", (*sqls)[index], e.base().what());
            MFSqlResult result(sessionId, serviceId);
            result.success = false;
            result.error = e.base().what();
            dispatchExecute(std::move(result));
        });
}

void MFSqlConnectPool::dispatchExecute(MFSqlResult&& result)
{
    MFMysqlMessage* msg = m_sqlMsgPool->pop();
    msg->setPool(m_sqlMsgPool);
    msg->setDst(result.serviceId);
    msg->setMessageType(LuaMessageTypeMysqlExecute);
    msg->setSessionId(result.sessionId);
    msg->setQuery(false);
    msg->setSuccess(result.success);
    bool execRes = result.success;
    if (result.success && result.result.has_value()) {
        execRes = result.result->affectedRows() > 0 || result.result->size() > 0;
    }
    msg->setExecRes(execRes);
    MFLuaServiceManager::getInstance()->nativeDispatch(msg);
}


void MFSqlConnectPool::dispatchQuery(MFSqlResult&& result, bool queryOne)
{
    MFMysqlMessage* msg = m_sqlMsgPool->pop();
    msg->setPool(m_sqlMsgPool);
    msg->setSuccess(result.success);
    msg->setDst(result.serviceId);
    msg->setMessageType(LuaMessageTypeMysqlQuery);
    msg->setSessionId(result.sessionId);
    msg->setQueryOne(queryOne);
    msg->setCallback([result = std::move(result), queryOne](MFLuaService* service) mutable {
        service->sqlMessageQuery(result, queryOne, LuaMessageTypeMysqlQuery);
    });
    MFLuaServiceManager::getInstance()->nativeDispatch(msg);
}

MFSqlPoolManager* MFSqlPoolManager::m_instance = nullptr;

MFSqlPoolManager::MFSqlPoolManager() = default;

MFSqlPoolManager::~MFSqlPoolManager()
{
    std::unique_lock ul(m_poolMtx);
    for (auto& [k, v] : m_sqlPoolMap) {
        delete v;
    }
    m_sqlPoolMap.clear();
}

MFSqlPoolManager* MFSqlPoolManager::getInstance()
{
    if (!m_instance) {
        m_instance = new MFSqlPoolManager();
    }
    return m_instance;
}

void MFSqlPoolManager::destroyInstance()
{
    if (m_instance) {
        delete m_instance;
        m_instance = nullptr;
    }
}

MFSqlConnectPool* MFSqlPoolManager::getConnectPool(int key, const sol::this_state&)
{
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

    std::string ip;
    std::string userName;
    std::string password;
    std::string database;
    int port = 0;
    int maxPoolSize = 0;
    const bool found = MFShareTable::getInstance()->withEntry(MFSqlConfig, key, [&](lua_State* L) {
        ip = MFShareTable::fieldString(L, "ip");
        port = MFShareTable::fieldInt(L, "port");
        userName = MFShareTable::fieldString(L, "userName");
        password = MFShareTable::fieldString(L, "password");
        database = MFShareTable::fieldString(L, "database");
        maxPoolSize = MFShareTable::fieldInt(L, "maxPoolSize");
    });
    if (!found) {
        MFApplication::getInstance()->logInfo("MFSqlPoolManager::getConnectPool not found config for key = {}", key);
        return nullptr;
    }
    if (port == 0) {
        port = 5432;
    }
    if (maxPoolSize <= 0) {
        maxPoolSize = 1;
    }

    auto* pool = new MFSqlConnectPool();
    pool->init(buildPgConnInfo(ip, port, userName, password, database),
               static_cast<size_t>(maxPoolSize));
    {
        std::unique_lock ul(m_poolMtx);
        m_sqlPoolMap[key] = pool;
    }
    return pool;
}

MFSqlConnectPool* MFSqlPoolManager::requirePool(int key,
                                                    const sol::this_state& state,
                                                    size_t sessionId,
                                                    MFServiceId_t serviceId,
                                                    bool query,
                                                    bool queryOne,
                                                    const std::string& sql)
{
    MFSqlConnectPool* pool = getConnectPool(key, state);
    if (pool) {
        return pool;
    }
    return nullptr;
}

size_t MFSqlPoolManager::queryAsync(const std::string& sql, MFServiceId_t serviceId, int key, const sol::this_state& state)
{
    size_t sessionId = MFUtil::genSessionId();
    MFSqlConnectPool* pool = requirePool(key, state, sessionId, serviceId, true, false, sql);
    if (!pool) {
        return sessionId;
    }
    return pool->queryAsync(sql, serviceId);
}

size_t MFSqlPoolManager::queryOneAsync(const std::string& sql, MFServiceId_t serviceId, int key, const sol::this_state& state)
{
    size_t sessionId = MFUtil::genSessionId();
    MFSqlConnectPool* pool = requirePool(key, state, sessionId, serviceId, true, true, sql);
    if (!pool) {
        return sessionId;
    }
    return pool->queryOneAsync(sql, serviceId);
}

size_t MFSqlPoolManager::executeAsync(const std::string& sql, MFServiceId_t serviceId, int key, const sol::this_state& state)
{
    size_t sessionId = MFUtil::genSessionId();
    MFSqlConnectPool* pool = requirePool(key, state, sessionId, serviceId, false, false, sql);
    if (!pool) {
        return sessionId;
    }
    return pool->executeAsync(sql, serviceId);
}

size_t MFSqlPoolManager::executeAsyncTransaction(const std::vector<std::string>& sqls, MFServiceId_t serviceId, int key, const sol::this_state& state)
{
    size_t sessionId = MFUtil::genSessionId();
    auto* pool = requirePool(key, state, sessionId, serviceId, false, false, "TRANSACTION");
    return pool ? pool->executeAsyncTransaction(sqls, serviceId) : sessionId;
}
