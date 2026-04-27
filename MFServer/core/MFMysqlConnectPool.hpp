#ifndef MFMysqlConnectPool_hpp
#define MFMysqlConnectPool_hpp

#include <string>
#include <shared_mutex>
#include "MFMacro.h"
#include "MFObjectPool.hpp"
#include "MFSpinLock.hpp"
#include "MFStripedMap.hpp"
#include "sol/sol.hpp"

struct MFMysqlResult;
class MFMysqlClient;
class MFMysqlMessage;

struct MFSqlConnection {
public:
    explicit MFSqlConnection(MFMysqlClient* client, int maxIdleTime);
    ~MFSqlConnection();
public:
    bool isValid();
public:
    MFProperty(MFMysqlClient*, m_client, Client);
    MFProperty(std::chrono::steady_clock::time_point, m_lastUsedTime, LastUsedTime);
	int m_maxIdleTime;
};

class MFMysqlConnectPool {
public:
    MFMysqlConnectPool();
    ~MFMysqlConnectPool();
public:
    void init(const std::string& url, int port, const std::string& user, const std::string& password, const std::string& database, int minPoolSize, int maxPoolSize, int maxIdleTime);
public:
    size_t queryAsync(const std::string& sql, MFServiceId_t serviceId);
    size_t queryOneAsync(const std::string& sql, MFServiceId_t serviceId);
    size_t executeAsync(const std::string& sql, MFServiceId_t serviceId);
public:
    size_t beginTransaction(MFServiceId_t serviceId);
    size_t executeInTransaction(size_t transactionId, const std::string& sql, MFServiceId_t serviceId);
    size_t commitTransaction(size_t transactionId, MFServiceId_t serviceId);
    size_t rollbackTransaction(size_t transactionId, MFServiceId_t serviceId);
private:
    enum class TxRelease : uint8_t {
        Never,
        OnFailure,
        Always,
    };
    size_t executeOnTransaction(size_t transactionId, MFServiceId_t serviceId, std::string sql, TxRelease policy);
private:
    MFSqlConnection* getConnection();
    void createConnect();
    void releaseConnect(MFSqlConnection* connection);
    void clearExpiredConnect();
    void dispatchExecute(MFMysqlResult&& result, const std::string& sql);
    void dispatchQuery(MFMysqlResult&& result, bool queryOne, const std::string& sql);
    void dispatchAcquireError(MFServiceId_t serviceId, size_t sessionId, bool query, bool queryOne, const std::string& sql);
	void deleteSqlConnection(MFSqlConnection* connection);
	void deleteSqlClient(MFMysqlClient* client);
private:
    MFProperty(int, m_port, Port);
    MFPropertyRef(std::string, m_url, Url)
    MFPropertyRef(std::string, m_user, User);
    MFPropertyRef(std::string, m_password, Password);
    MFPropertyRef(std::string, m_database, Database);
    MFProperty(int, m_maxPoolSize, MaxPoolSize);
    MFProperty(int, m_maxIdleTime, MaxIdleTime);//milliseconds
    MFProperty(int, m_minPoolSize, MinPoolSize);
private:
    MFBlockingQueue<MFSqlConnection*>                   m_connectPool;
    std::atomic<int>                                    m_currentPoolSize;
    MFObjectPool<MFMysqlMessage>*                       m_sqlMsgPool;
    std::mutex                                          m_clearMutex;
    uint32_t                                            m_timerId;
    MFStripedMap<size_t, MFSqlConnection*>              m_transactionConnections;
};

class MFMysqlPoolManager {
public:
    MFMysqlPoolManager();
    ~MFMysqlPoolManager();
public:
    static MFMysqlPoolManager* getInstance();
    static void destroyInstance();
private:
	MFMysqlConnectPool* getConnectPool(int key, const sol::this_state& state);
public:
    size_t queryAsync(const std::string& sql, MFServiceId_t serviceId, int key, const sol::this_state& state);
    size_t queryOneAsync(const std::string& sql, MFServiceId_t serviceId, int key, const sol::this_state& state);
    size_t executeAsync(const std::string& sql, MFServiceId_t serviceId, int key, const sol::this_state& state);
public:
    size_t beginTransaction(MFServiceId_t serviceId, int key, const sol::this_state& state);
    size_t executeInTransaction(size_t transactionId, const std::string& sql, MFServiceId_t serviceId, int key, const sol::this_state& state);
    size_t commitTransaction(size_t transactionId, MFServiceId_t serviceId, int key, const sol::this_state& state);
    size_t rollbackTransaction(size_t transactionId, MFServiceId_t serviceId, int key, const sol::this_state& state);
private:
    static MFMysqlPoolManager* m_instance;
private:
    MFFastMap<int, MFMysqlConnectPool*>             m_sqlPoolMap;
    mutable std::shared_mutex                       m_poolMtx;
    MFSpinLock                                      m_createLock;
};

#endif
