#include "MFWebServer.hpp"
#include "MFApplication.hpp"
#include "MFNetManager.hpp"
#include "MFLuaService.hpp"
#include "MFLuaServiceManager.hpp"
#include "MFUtil.hpp"
#include "MFLuaMessage.hpp"
#include "MFConnectionManager.hpp"

/*************************************MFWebSocketServer*******************************************/
MFWebSocketServer::MFWebSocketServer(MFServiceId_t serviceId)
: m_serviceId(serviceId) {
    m_messagePool = new MFObjectPool<MFSocketMessage>;
}

MFWebSocketServer::~MFWebSocketServer() {
    delete m_messagePool;
}

void MFWebSocketServer::handleNewMessage(const drogon::WebSocketConnectionPtr& ptr, std::string&& msg, const drogon::WebSocketMessageType& type)
{
    size_t fd = ptr->getContext<WebContext>()->getConnectId();
    MFSocketMessage* message = m_messagePool->pop();
    message->setPool(m_messagePool);
    message->setDst(m_serviceId);
    message->setMessageType(LuaMessageTypeWebSocket);
    message->setViewData(msg.data(), static_cast<uint32_t>(msg.length()));
    message->setFd(fd);
    message->setCmd(MFSocketRead);
    message->setSrc(static_cast<MFServiceId_t>(type));
    MFLuaServiceManager::getInstance()->nativeDispatch(message);
}

void MFWebSocketServer::handleConnectionClosed(const drogon::WebSocketConnectionPtr& connectPtr) {
    size_t fd = connectPtr->getContext<WebContext>()->getConnectId();
    MFSocketMessage* message = m_messagePool->pop();
    message->setPool(m_messagePool);
    message->setDst(m_serviceId);
    message->setMessageType(LuaMessageTypeWebSocket);
    message->setFd(fd);
    message->setCmd(MFSocketClose);
    MFLuaServiceManager::getInstance()->nativeDispatch(message);
    MFConnectionManager::getInstance()->removeWebSocketConnection(fd);
}

void MFWebSocketServer::handleNewConnection(const drogon::HttpRequestPtr& req, const drogon::WebSocketConnectionPtr& connectPtr) {
    size_t fd = MFUtil::genConnectFd();
    WebContext webContext;
    webContext.setConnectId(fd);
    connectPtr->setContext(std::make_shared<WebContext>(webContext));
    MFSocketMessage* message = m_messagePool->pop();
    message->setPool(m_messagePool);
    message->setDst(m_serviceId);
    message->setMessageType(LuaMessageTypeWebSocket);
    message->setFd(fd);
    message->setCmd(MFSocketOpen);
    MFLuaServiceManager::getInstance()->nativeDispatch(message);
    MFConnectionManager::getInstance()->addWebSocketConnection(fd, connectPtr);
}

/*************************************MFHttpServer*****************************************************/
MFHttpServer::MFHttpServer(MFServiceId_t serviceId)
: m_serviceId(serviceId) {
    m_httpServerPool = new MFObjectPool<MFHttpMessage>;
}

MFHttpServer::~MFHttpServer() {
    delete m_httpServerPool;
}

void MFHttpServer::postHandle(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr &)>&& callback)
{
	size_t fd = MFUtil::genConnectFd();
    MFHttpMessage* message = m_httpServerPool->pop();
    message->setPool(m_httpServerPool);
    message->setMessageType(LuaMessageTypeHttpServerReq);
    message->setDst(m_serviceId);
    message->setFd(fd);
    std::string_view body = req->getBody();
	message->setViewData(body.data(), body.length());
	message->setIp(req->getPeerAddr().toIp());
	MFConnectionManager::getInstance()->addHttpCallback(fd, std::move(callback));
    MFLuaServiceManager::getInstance()->nativeDispatch(message);
}

void MFHttpServer::getHandle(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr &)>&& callback)
{
    size_t fd = MFUtil::genConnectFd();
    MFHttpMessage* message = m_httpServerPool->pop();
    message->setPool(m_httpServerPool);
    message->setMessageType(LuaMessageTypeHttpServerReq);
    message->setDst(m_serviceId);
    message->setFd(fd);
    std::string_view body = req->getBody();
    message->setViewData(body.data(), body.length());
    message->setIp(req->getPeerAddr().toIp());
    MFConnectionManager::getInstance()->addHttpCallback(fd, std::move(callback));
    MFLuaServiceManager::getInstance()->nativeDispatch(message);
}


MFWebServer::MFWebServer(MFApplication* application)
: m_application(application) {
    m_httpClientMsgPool = new MFObjectPool<MFHttpClientMessage>;
}

MFWebServer::~MFWebServer() {
    delete m_httpClientMsgPool;
}

void MFWebServer::initHttp(const std::string& ip, uint16_t port, MFServiceId_t serviceId) {
    auto ctrl = std::make_shared<MFHttpServer>(serviceId);
	drogon::app().registerController(ctrl);
    drogon::app().addListener(ip, port);
    m_application->logInfo("start httpServer ip = {}, port = {}", ip, port);
}

void MFWebServer::initWebSocket(const std::string &ip, uint16_t port, MFServiceId_t serviceId) {
    auto ctrl = std::make_shared<MFWebSocketServer>(serviceId);
    drogon::app().registerController(ctrl);
    drogon::app().addListener(ip, port);
    m_application->logInfo("start webSocketServer ip = {}, port = {}", ip, port);
}
