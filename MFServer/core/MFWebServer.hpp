#ifndef MFHttpServer_hpp
#define MFHttpServer_hpp

#include "drogon/drogon.h"
#include "drogon/WebSocketController.h"
#include "drogon/HttpController.h"
#include "MFMacro.h"
#include "MFObjectPool.hpp"



class MFApplication;
class MFWebServer;
class MFHttpMessage;
class MFHttpClientMessage;
class MFSocketMessage;


class WebContext {
public:
    WebContext()
    : m_connectId(0) {

    }
    ~WebContext() = default;
public:
    size_t getConnectId() const { return m_connectId; }
    void setConnectId(const size_t connectId) { m_connectId = connectId; }
private:
    size_t m_connectId;
};

//ws://127.0.0.1:8888/
class MFWebSocketServer final : public drogon::WebSocketController<MFWebSocketServer, false>
{
public:
    explicit MFWebSocketServer(MFServiceId_t serviceId);
    ~MFWebSocketServer() override;
public:
    void handleNewMessage(const drogon::WebSocketConnectionPtr &, std::string &&, const drogon::WebSocketMessageType &) override;
    void handleConnectionClosed(const drogon::WebSocketConnectionPtr &) override;
    void handleNewConnection(const drogon::HttpRequestPtr &, const drogon::WebSocketConnectionPtr &) override;
public:
    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/", drogon::Get);
    WS_PATH_LIST_END
private:
    MFServiceId_t                       m_serviceId;
    MFObjectPool<MFSocketMessage>*      m_messagePool;
};

//http://127.0.0.1:8000/xxx
class MFHttpServer : public drogon::HttpController<MFHttpServer, false>
{
public:
    explicit MFHttpServer(MFServiceId_t serviceId);
    ~MFHttpServer() override;
public:
    void postHandle(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    void getHandle(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr &)> &&callback);
public:
  METHOD_LIST_BEGIN
    ADD_METHOD_VIA_REGEX(MFHttpServer::getHandle, "^/.*$",  drogon::Get);
    ADD_METHOD_VIA_REGEX(MFHttpServer::postHandle, "^/.*$",  drogon::Post);
  METHOD_LIST_END
private:
    MFObjectPool<MFHttpMessage>*        m_httpServerPool;
    MFServiceId_t                       m_serviceId;
};


class MFWebServer
{
public:
    explicit MFWebServer(MFApplication* application);
    ~MFWebServer();
public:
    void initHttp(const std::string& ip, uint16_t port, MFServiceId_t serviceId);
    void initWebSocket(const std::string& ip, uint16_t port, MFServiceId_t serviceId);
public:
    MFObjectPool<MFHttpClientMessage>* getClientMsgPool() { return m_httpClientMsgPool; }
private:
    MFApplication*                                              m_application;
    MFObjectPool<MFHttpClientMessage>*                          m_httpClientMsgPool;
};


#endif /* MFHttpServer_hpp */
