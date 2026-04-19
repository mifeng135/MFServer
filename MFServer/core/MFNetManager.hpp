#ifndef MFTcpServerManager_hpp
#define MFTcpServerManager_hpp

#include <shared_mutex>

#include "drogon/HttpController.h"
#include "sol/sol.hpp"

#include "MFMacro.h"
#include "MFSpinLock.hpp"

class MFApplication;
class MFTcpServer;
class MFTcpClient;
class MFWebServer;
class MFUdpServer;

class MFNetManager
{
public:
    explicit MFNetManager(MFApplication* application);
    ~MFNetManager();
public:
    void createTcpServer(uint32_t configId, MFServiceId_t serviceId, uint8_t type);
    void createHttpServer(uint32_t configId, MFServiceId_t serviceId);
    void createWebSocketServer(uint32_t configId, MFServiceId_t serviceId);
    void createUdpServer(uint32_t configId, MFServiceId_t serviceId);
    size_t createHttpClient(const std::string& host, const drogon::HttpRequestPtr& request, MFServiceId_t serviceId);
public:
	void stopTcpServer(uint32_t configId);
    void setTcpServerIdleTime(uint32_t configId, size_t time);
    std::shared_ptr<MFWebServer>& getWebServer() { return m_webServer; }
    void rpcClientSend(uint32_t configId, const char* msg, int len, const sol::this_state& state);
    bool removeClient(uint32_t configId);
private:
    std::shared_ptr<MFTcpClient> getClient(uint32_t configId, const sol::this_state &state, bool isRpc);
    std::shared_ptr<const Json::Value> getConfig(uint32_t configId, uint8_t type = MFNetTypeNormal);
private:
    std::shared_ptr<MFWebServer>            m_webServer;
    MFApplication*                          m_application;
private:
    MFFastMap<uint32_t, std::shared_ptr<MFTcpServer>>           m_tcpServerMap;
    mutable std::shared_mutex                                   m_tcpServerMtx;

    MFFastMap<uint32_t, std::shared_ptr<MFUdpServer>>           m_udpServerMap;
    mutable std::shared_mutex                                   m_udpServerMtx;

    MFFastMap<uint32_t, std::shared_ptr<MFTcpClient>>           m_clientMap;
    mutable std::shared_mutex                                   m_clientMtx;
    MFSpinLock                                                  m_clientCreateLock;
};

#endif /* MFTcpServerManager_hpp */
