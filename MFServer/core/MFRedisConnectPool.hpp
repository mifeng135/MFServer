#ifndef MFRedisConnectPool_hpp
#define MFRedisConnectPool_hpp

#include <mutex>
#include <shared_mutex>
#include <atomic>
#include "sol/sol.hpp"
#include "MFMacro.h"
#include "MFObjectPool.hpp"
#include "MFSpinLock.hpp"


class MFRedisClient;
struct MFRedisResult;
class MFRedisMessage;
class MFNativeLuaRedis;

class MFRedisConnectPool
{
public:
    MFRedisConnectPool();
    ~MFRedisConnectPool();
public:
    void createConnect();
    MFRedisClient* getConnect();
    void releaseConnect(MFRedisClient* connect);
    void clearExpiredConnect();
    void init(const std::string& url, int port, const std::string& password, int useDB, int minPoolSize, int maxPoolSize, int maxIdleTime);
    void deleteClient(MFRedisClient* client);
private:
    MFBlockingQueue<MFRedisClient*>         m_redisContextPool{32};
    std::string                             m_url;
    int                                     m_port;
    std::string                             m_password;
    int                                     m_useDB;
    int                                     m_minPoolSize;
    int                                     m_maxPoolSize;
    int                                     m_maxIdleTime;

    std::atomic<int>                        m_currentPoolSize;
    std::mutex                              m_clearMutex;
    uint32_t                                m_timerId;
};

class MFRedisPoolManager {
public:
    MFRedisPoolManager();
    ~MFRedisPoolManager();
public:
    static MFRedisPoolManager* getInstance();
    static void destroyInstance();
private:
    MFNativeLuaRedis* getNativeLuaRedis(int key, const sol::this_state& state);
public:
    size_t executeAsync(MFServiceId_t serviceId, int key, const sol::variadic_args& args, const sol::this_state& state);
    sol::object executeSync(int key, int timeOut, const sol::variadic_args& args, const sol::this_state& state);
private:
    MFFastMap<int, MFNativeLuaRedis*>               m_redisNativePools;
    mutable std::shared_mutex                       m_poolMtx;
    static MFRedisPoolManager*                      m_instance;
    MFSpinLock                                      m_createLock;
};

class MFNativeLuaRedis
{
public:
    explicit MFNativeLuaRedis(MFRedisConnectPool* pool);
    ~MFNativeLuaRedis();
public:
    size_t executeAsync(MFServiceId_t serviceId, const sol::variadic_args& args, const sol::this_state& state);
	sol::object executeSync(int timeOut, const sol::variadic_args& args, const sol::this_state& state);
private:
	void dispatchRedisCommand(MFRedisResult&& result);
private:
    MFRedisConnectPool*             m_pool;
    MFObjectPool<MFRedisMessage>*   m_redisCmdPool;
};

#endif /* MFRedisConnectPool_hpp */
