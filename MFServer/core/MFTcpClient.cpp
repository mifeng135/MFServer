#include "MFTcpClient.hpp"
#include "MFLuaServiceManager.hpp"
#include "MFApplication.hpp"
#include "MFLuaMessage.hpp"


MFTcpClient::MFTcpClient(MFApplication* application)
: m_application(application)
, m_serviceId(0)
, m_connect(nullptr)
, m_unique(0)
, m_type(MFNetTypeNormal)
, m_eventLoopThread(nullptr) {
    m_tcpClientPool = new MFObjectPool<MFSocketMessage>();
}

MFTcpClient::~MFTcpClient() {
    m_client->disconnect();
    m_client.reset();
    delete m_tcpClientPool;
    delete m_eventLoopThread;
}

void MFTcpClient::initClient(const std::string &ip, uint16_t port, MFServiceId_t serviceId, uint32_t unique, uint8_t type) {
    m_serviceId = serviceId;
    m_unique = unique;
    m_type = type;
    trantor::InetAddress addr(ip, port);
    m_eventLoopThread = new trantor::EventLoopThread("MFTcpClientEventLoop");
    m_eventLoopThread->run();

    m_client = std::make_shared<trantor::TcpClient>(m_eventLoopThread->getLoop(), addr, std::to_string(serviceId));
    m_client->setConnectionCallback([this, ip, port](const trantor::TcpConnectionPtr &conn) {
        if (conn->connected()) {
            m_connect = conn;
            m_connect->setTcpNoDelay(true);
            sendWait();
            onActive(conn);
			if (m_type == MFNetTypeRPC) {
                m_application->logInfo("rpc connect success ip = {}, port = {}", ip, port);
            } else {
                m_application->logInfo("tcp connect success ip = {}, port = {}", ip, port);
            }
        } else {
            onInActive(conn);
			if (m_type == MFNetTypeRPC) {
                m_application->logInfo("rpc close ip = {}, port = {}", ip, port);
            } else {
                m_application->logInfo("tcp close ip = {}, port = {}", ip, port);
            }
        }
    });
    
    m_client->setMessageCallback([this](const trantor::TcpConnectionPtr &conn, trantor::MsgBuffer *buf) {
        onRead(conn, buf);
    });
    m_client->enableRetry();
    m_client->connect();
}

void MFTcpClient::onActive(const trantor::TcpConnectionPtr &connPtr) {
    int msgType = m_type == MFNetTypeRPC ? LuaMessageTypeRpcRsp : LuaMessageTypeSocketClient;
    MFSocketMessage* message = m_tcpClientPool->pop();
    message->setPool(m_tcpClientPool);
    message->setMessageType(msgType);
    message->setCmd(MFSocketOpen);
    message->setUnique(m_unique);
    message->setDst(m_serviceId);
    MFLuaServiceManager::getInstance()->nativeDispatch(message);
}

void MFTcpClient::onInActive(const trantor::TcpConnectionPtr &connPtr) {
    int msgType = m_type == MFNetTypeRPC ? LuaMessageTypeRpcRsp : LuaMessageTypeSocketClient;
    MFSocketMessage* message = m_tcpClientPool->pop();
    message->setPool(m_tcpClientPool);
    message->setMessageType(msgType);
    message->setCmd(MFSocketClose);
    message->setUnique(m_unique);
    message->setDst(m_serviceId);
    MFLuaServiceManager::getInstance()->nativeDispatch(message);
}

void MFTcpClient::onRead(const trantor::TcpConnectionPtr &connectionPtr, trantor::MsgBuffer *buffer) {
    do {
        size_t readableBytes = buffer->readableBytes();
        if (readableBytes < 4) {
            break;
        }
        uint32_t msgLen = buffer->peekInt32();
        if (msgLen > readableBytes) {
            break;
        }
        int msgType = m_type == MFNetTypeRPC ? LuaMessageTypeRpcRsp : LuaMessageTypeSocketClient;
        MFSocketMessage* message = m_tcpClientPool->pop();
        message->setPool(m_tcpClientPool);
        message->setMessageType(msgType);
        message->setViewData(buffer->peek(), msgLen);
        message->setCmd(MFSocketRead);
        message->setUnique(m_unique);
        message->setDst(m_serviceId);
        MFLuaServiceManager::getInstance()->nativeDispatch(message);
        buffer->retrieve(msgLen);
    } while (true);
}

void MFTcpClient::sendWait() {
    MFWaitSend send;
    while (m_waitSendQueue.try_dequeue(send)) {
        m_connect->send(send.sendStr);
    }
}

void MFTcpClient::disconnect() {
    m_client->disconnect();
}

void MFTcpClient::send(const char* msg, size_t len) {
    if (!m_connect) {
        m_waitSendQueue.enqueue(MFWaitSend(msg, len));
    } else {
        m_connect->send(msg, len);
    }
}
