#ifndef MFSqlConnectPool_hpp
#define MFSqlConnectPool_hpp

#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>
#include "MFMacro.h"
#include "MFObjectPool.hpp"
#include "MFSpinLock.hpp"
#include "drogon/orm/DbClient.h"
#include "sol/sol.hpp"

class MFMysqlMessage;

struct MFSqlResult {
    MFSqlResult() = default;
    MFSqlResult(size_t sid, MFServiceId_t service)
        : sessionId(sid)
        , serviceId(service)
    {
    }

    size_t sessionId{0};
    MFServiceId_t serviceId{0};
    bool success{true};
    std::string error;
    std::optional<drogon::orm::Result> result;
};

class MFSqlConnectPool {
public:
    MFSqlConnectPool();
    ~MFSqlConnectPool();
public:
    void init(const std::string& connInfo, size_t connNum);
public:
    size_t queryAsync(const std::string& sql, MFServiceId_t serviceId);
    size_t queryOneAsync(const std::string& sql, MFServiceId_t serviceId);
    size_t executeAsync(const std::string& sql, MFServiceId_t serviceId);
    size_t executeAsyncTransaction(const std::vector<std::string>& sqls, MFServiceId_t serviceId);
private:
    void execSql(const std::string& sql, MFServiceId_t serviceId, size_t sessionId, bool query, bool queryOne);
    void runTx(const std::shared_ptr<drogon::orm::Transaction>& tx,
               std::shared_ptr<std::vector<std::string>> sqls,
               size_t index,
               size_t sessionId,
               MFServiceId_t serviceId);
    void dispatchExecute(MFSqlResult&& result);
    void dispatchQuery(MFSqlResult&& result, bool queryOne);
private:
    drogon::orm::DbClientPtr m_client;
    MFObjectPool<MFMysqlMessage>* m_sqlMsgPool;
};

class MFSqlPoolManager {
public:
    MFSqlPoolManager();
    ~MFSqlPoolManager();
public:
    static MFSqlPoolManager* getInstance();
    static void destroyInstance();
private:
    MFSqlConnectPool* getConnectPool(int key, const sol::this_state& state);
    MFSqlConnectPool* requirePool(int key, const sol::this_state& state, size_t sessionId, MFServiceId_t serviceId, bool query, bool queryOne, const std::string& sql);
public:
    size_t queryAsync(const std::string& sql, MFServiceId_t serviceId, int key, const sol::this_state& state);
    size_t queryOneAsync(const std::string& sql, MFServiceId_t serviceId, int key, const sol::this_state& state);
    size_t executeAsync(const std::string& sql, MFServiceId_t serviceId, int key, const sol::this_state& state);
    size_t executeAsyncTransaction(const std::vector<std::string>& sqls, MFServiceId_t serviceId, int key, const sol::this_state& state);
private:
    static MFSqlPoolManager*            m_instance;
private:
    MFFastMap<int, MFSqlConnectPool*>   m_sqlPoolMap;
    mutable std::shared_mutex           m_poolMtx;
    MFSpinLock                          m_createLock;
    MFObjectPool<MFMysqlMessage>        m_errMsgPool;
};

#endif
