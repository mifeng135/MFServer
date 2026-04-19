#include "MFTcpServer.hpp"
#include "MFLuaServiceManager.hpp"
#include "MFApplication.hpp"
#include "MFUtil.hpp"
#include "MFLuaMessage.hpp"
#include "MFConnectionManager.hpp"

MFTcpServer::MFTcpServer(MFApplication* application)
: m_server(nullptr)
, m_serviceId(0)
, m_application(application)
, m_eventLoopThread(nullptr)
, m_unique(0)
, m_type(MFNetTypeNormal)
{
    m_tcpServerMsgPool = new MFObjectPool<MFSocketMessage>();
}

MFTcpServer::~MFTcpServer() {
    if (!m_eventLoopThread) {
        return;
    }
    m_eventLoopThread->getLoop()->runInLoop([this]()->void {
        m_server->stop();
    });
    delete m_tcpServerMsgPool;
    delete m_eventLoopThread;
    delete m_server;
}

void MFTcpServer::init(const std::string &ip, uint16_t port, MFServiceId_t serviceId, int ioThread, uint32_t unique, uint8_t type)
{
    m_serviceId = serviceId;
    m_unique = unique;
    m_type = type;
    trantor::InetAddress addr(ip, port);
    m_eventLoopThread = new trantor::EventLoopThread("MFTcpServerEventLoop");
    m_eventLoopThread->run();

    m_server = new trantor::TcpServer(m_eventLoopThread->getLoop(), addr, std::to_string(serviceId));
    m_server->setConnectionCallback([this](const trantor::TcpConnectionPtr &connPtr) {
        if (connPtr->connected()) {
            onActive(connPtr);
        } else if (connPtr->disconnected()) {
            onInActive(connPtr);
        }
    });
    
    m_server->setRecvMessageCallback([this](const trantor::TcpConnectionPtr &connectionPtr, trantor::MsgBuffer *buffer) {
        onRead(connectionPtr, buffer);
    });
    m_server->setIoLoopNum(ioThread); //io请求
    m_server->start();
    if (MFNetTypeRPC == m_type) {
        m_application->logInfo("start rpcServer ip = {}, port = {}, ioThreadNumber = {}", ip, port, ioThread);
    } else {
        m_application->logInfo("start tcpServer ip = {}, port = {}, ioThreadNumber = {}", ip, port, ioThread);
    }
}

void MFTcpServer::stop()
{
    m_server->stop();
}

void MFTcpServer::setIdleTime(size_t timeSeconds)
{
    m_server->kickoffIdleConnections(timeSeconds);
}

void MFTcpServer::onActive(const trantor::TcpConnectionPtr &connPtr)
{
    size_t connectId = MFUtil::genConnectFd();
    std::shared_ptr<ConnectContext> context = std::make_shared<ConnectContext>();
    context->setConnectId(connectId);
    connPtr->setContext(context);

    int msgType = m_type == MFNetTypeRPC ? LuaMessageTypeRpcReq : LuaMessageTypeSocket;
    MFSocketMessage* message = m_tcpServerMsgPool->pop();
    message->setPool(m_tcpServerMsgPool);
    message->setMessageType(msgType);
    message->setFd(connectId);
    message->setCmd(MFSocketOpen);
    message->setUnique(m_unique);
    message->setDst(m_serviceId);
    message->setSrc(MFNetTcpServer);
    MFConnectionManager::getInstance()->addConnection(connectId, connPtr);
    MFLuaServiceManager::getInstance()->nativeDispatch(message);
}

void MFTcpServer::onInActive(const trantor::TcpConnectionPtr &connPtr)
{
    int msgType = m_type == MFNetTypeRPC ? LuaMessageTypeRpcReq : LuaMessageTypeSocket;
    size_t fd = connPtr->getContext<ConnectContext>()->getConnectId();
    MFSocketMessage* message = m_tcpServerMsgPool->pop();
    message->setPool(m_tcpServerMsgPool);
    message->setMessageType(msgType);
    message->setFd(fd);
    message->setCmd(MFSocketClose);
    message->setUnique(m_unique);
    message->setDst(m_serviceId);
    message->setSrc(MFNetTcpServer);

    MFConnectionManager::getInstance()->removeConnection(fd);
    MFLuaServiceManager::getInstance()->nativeDispatch(message);
}

void MFTcpServer::onRead(const trantor::TcpConnectionPtr &connectionPtr, trantor::MsgBuffer *buffer) {
    do {
        size_t readableBytes = buffer->readableBytes();
        if (readableBytes < 4) {
            break;
        }
        uint32_t msgLen = buffer->peekInt32();
        if (msgLen > readableBytes) {
            break;
        }

        int msgType = m_type == MFNetTypeRPC ? LuaMessageTypeRpcReq : LuaMessageTypeSocket;
        MFSocketMessage* message = m_tcpServerMsgPool->pop();
        message->setPool(m_tcpServerMsgPool);
        message->setMessageType(msgType);
        message->setViewData(buffer->peek(), msgLen);
        message->setCmd(MFSocketRead);
        message->setUnique(m_unique);
        message->setDst(m_serviceId);
        message->setFd(connectionPtr->getContext<ConnectContext>()->getConnectId());
        message->setSrc(MFNetTcpServer);
        MFLuaServiceManager::getInstance()->nativeDispatch(message);
        buffer->retrieve(msgLen);
    } while (true);
}