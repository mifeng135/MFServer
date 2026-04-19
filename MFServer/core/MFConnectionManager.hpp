#ifndef MFConnectionManager_hpp
#define MFConnectionManager_hpp

#include "trantor/net/TcpServer.h"
#include "drogon/HttpController.h"
#include "sol/sol.hpp"

#include "MFStripedMap.hpp"
#include "drogon/WebSocketConnection.h"

using httpCallback = std::function<void(const drogon::HttpResponsePtr&)>;

class MFConnectionManager {
public:
    static MFConnectionManager* getInstance();
    static void destroyInstance();
public:
    void addConnection(size_t fd, const trantor::TcpConnectionPtr& connPtr);
    void removeConnection(size_t fd);
    void forceCloseConnection(size_t fd);
    void send(size_t fd, const char* msg, int len);
public:
    void addWebSocketConnection(size_t fd, const drogon::WebSocketConnectionPtr& connPtr);
    void removeWebSocketConnection(size_t fd);
    void forceCloseWebSocketConnection(size_t fd);
    void sendWebSocketConnection(size_t fd, const char* msg, int len, drogon::WebSocketMessageType type);
public:
    void addHttpCallback(size_t fd, httpCallback&& callback);
    void httpResponse(size_t fd, const std::string& body, drogon::HttpStatusCode code);
    void httpResponseParams(size_t fd, const std::string& body, drogon::HttpStatusCode code, const sol::table &headers);
private:
    MFStripedMap<size_t, trantor::TcpConnectionPtr>         m_connectionMap;
    MFStripedMap<size_t, drogon::WebSocketConnectionPtr>    m_webConnectionMap;
    MFStripedMap<size_t, httpCallback, 8>                   m_httpCallbackMap;
    static MFConnectionManager*                             m_instance;
};

#endif /* MFConnectionManager_hpp */
