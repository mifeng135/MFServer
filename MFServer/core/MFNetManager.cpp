#include "MFNetManager.hpp"
#include "MFApplication.hpp"
#include "MFTcpServer.hpp"
#include "MFTcpClient.hpp"
#include "MFWebServer.hpp"
#include "MFUtil.hpp"
#include "MFLuaMessage.hpp"
#include "MFLuaServiceManager.hpp"
#include "MFUdpServer.hpp"
#include "MFShareTable.hpp"

MFNetManager::MFNetManager(MFApplication* application)
: m_application(application) {
    m_webServer = std::make_shared<MFWebServer>(m_application);
}

MFNetManager::~MFNetManager() = default;


void MFNetManager::createTcpServer(uint32_t configId, MFServiceId_t serviceId, uint8_t type) {
    std::shared_ptr<MFTcpServer> server = std::make_shared<MFTcpServer>(m_application);
    std::string ip;
    int port = 0;
    int ioThread = 0;
    const char* name = type == MFNetTypeRPC ? MFRpcConfig : MFSocketConfig;
    if (!loadShareConfig(name, configId, ip, port, &ioThread)) {
        return;
    }
    server->init(ip, port, serviceId, ioThread, configId, type);
    {
        std::unique_lock ul(m_tcpServerMtx);
        m_tcpServerMap[configId] = server;
    }
}

void MFNetManager::createHttpServer(uint32_t configId, MFServiceId_t serviceId) {
    std::string ip;
    int port = 0;
    if (!loadShareConfig(MFSocketConfig, configId, ip, port, nullptr)) {
        return;
    }
    m_webServer->initHttp(ip, port, serviceId);
}

void MFNetManager::createWebSocketServer(uint32_t configId, MFServiceId_t serviceId) {
    std::string ip;
    int port = 0;
    if (!loadShareConfig(MFSocketConfig, configId, ip, port, nullptr)) {
        return;
    }
    m_webServer->initWebSocket(ip, port, serviceId);
}

void MFNetManager::createUdpServer(uint32_t configId, MFServiceId_t serviceId) {
    std::shared_ptr<MFUdpServer> server = std::make_shared<MFUdpServer>(m_application);
    std::string ip;
    int port = 0;
    int ioThread = 0;
    if (!loadShareConfig(MFSocketConfig, configId, ip, port, &ioThread)) {
        return;
    }
    server->init(ip, port, serviceId, ioThread, configId);
    {
        std::unique_lock ul(m_udpServerMtx);
        m_udpServerMap[configId] = server;
    }
}

size_t MFNetManager::createHttpClient(const std::string& host, const drogon::HttpRequestPtr& request, MFServiceId_t serviceId) {
    auto client = drogon::HttpClient::newHttpClient(host);
    size_t id = MFUtil::genSessionId();
    client->sendRequest(request, [client, serviceId, id, this](drogon::ReqResult result, const drogon::HttpResponsePtr& response) {
        auto pool = m_webServer->getClientMsgPool();
        MFHttpClientMessage* message = pool->pop();
        message->setMessageType(LuaMessageTypeHttpClientRsp);
        message->setDst(serviceId);
        message->setSessionId(id);
        message->setPool(pool);
        if (response) {
            uint8_t retResult = result == drogon::ReqResult::Ok && response->getStatusCode() == drogon::HttpStatusCode::k200OK ? 1 : 0;
            message->setResult(retResult);
            message->setViewData(response->getBody().data());
        } else {
            message->setResult(0);
			message->setViewData("");
        }
        MFLuaServiceManager::getInstance()->nativeDispatch(message);
    });
    return id;
}

void MFNetManager::stopTcpServer(uint32_t configId) {
    std::shared_ptr<MFTcpServer> server;
    {
        std::unique_lock ul(m_tcpServerMtx);
        auto it = m_tcpServerMap.find(configId);
        if (it == m_tcpServerMap.end()) {
            return;
        }
        server = std::move(it->second);
        m_tcpServerMap.erase(it);
    }
    server->stop();
}

void MFNetManager::setTcpServerIdleTime(uint32_t configId, size_t time) {
    std::shared_ptr<MFTcpServer> server;
    {
        std::shared_lock lk(m_tcpServerMtx);
        auto it = m_tcpServerMap.find(configId);
        if (it == m_tcpServerMap.end()) {
            return;
        }
        server = it->second;
    }
    server->setIdleTime(time);
}

void MFNetManager::rpcClientSend(uint32_t configId, const char *msg, int len, const sol::this_state& state) {
    std::shared_ptr<MFTcpClient> client = getClient(configId, state, true);
    if (client) {
        client->send(msg, len);
    }
}

std::shared_ptr<MFTcpClient> MFNetManager::getClient(uint32_t configId, const sol::this_state &state, bool isRpc) {
    {
        std::shared_lock lk(m_clientMtx);
        auto it = m_clientMap.find(configId);
        if (it != m_clientMap.end()) {
            return it->second;
        }
    }

    std::lock_guard<MFSpinLock> guard(m_clientCreateLock);
    {
        std::shared_lock lk(m_clientMtx);
        auto it = m_clientMap.find(configId);
        if (it != m_clientMap.end()) {
            return it->second;
        }
    }

    std::string ip;
    int port = 0;
    if (!loadShareConfig(MFRpcConfig, configId, ip, port, nullptr)) {
        return nullptr;
    }

    auto stateView = sol::state_view(state);
    MFServiceId_t serviceId = stateView["MF"]["core"]["serviceId"];
    
    std::shared_ptr<MFTcpClient> client = std::make_shared<MFTcpClient>(m_application);
    client->initClient(ip, port, serviceId, configId, MFNetTypeRPC);
    {
        //prevent concurrent read and write operations on the same unordered_map
        std::unique_lock ul(m_clientMtx);
        m_clientMap[configId] = client;
    }
    return client;
}

bool MFNetManager::removeClient(uint32_t configId) {
    std::shared_ptr<MFTcpClient> client;
    {
        std::unique_lock ul(m_clientMtx);
        auto it = m_clientMap.find(configId);
        if (it == m_clientMap.end()) {
            return false;
        }
        client = std::move(it->second);
        m_clientMap.erase(it);
    }
    client->disconnect();
    return true;
}

bool MFNetManager::loadShareConfig(const char* name, uint32_t configId, std::string& ip, int& port, int* io) {
    const bool found = MFShareTable::getInstance()->withEntry(name, static_cast<lua_Integer>(configId), [&](lua_State* L) {
        ip = MFShareTable::fieldString(L, "ip");
        port = MFShareTable::fieldInt(L, "port");
        if (io) {
            *io = MFShareTable::fieldInt(L, "io");
        }
    });
    if (!found) {
        MFApplication::getInstance()->logInfo("loadShareConfig not found {}.{}", name, configId);
        return false;
    }
    return true;
}

