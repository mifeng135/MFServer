#include "MFUdpServer.hpp"

#include <trantor/net/UdpSocket.h>

#include "MFApplication.hpp"
#include "MFLuaMessage.hpp"
#include "MFUtil.hpp"
#include "MFUdpChannelManager.hpp"
#include "MFUdpChannel.hpp"
#include "MFLuaServiceManager.hpp"

MFUdpServer::MFUdpServer(MFApplication *application)
: m_server(nullptr)
, m_serviceId(0)
, m_application(application)
, m_eventLoopThread(nullptr)
, m_unique(0) {
    m_tcpServerMsgPool = new MFObjectPool<MFSocketMessage>();
}

MFUdpServer::~MFUdpServer() {
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

void MFUdpServer::init(const std::string &ip, uint16_t port, MFServiceId_t serviceId, int ioThread, uint32_t unique) {
    m_serviceId = serviceId;
    m_unique = unique;
    trantor::InetAddress addr(ip, port);
    m_eventLoopThread = new trantor::EventLoopThread("MFUdpServerEventLoop");
    m_eventLoopThread->run();

    m_server = new trantor::UdpServer(m_eventLoopThread->getLoop(), addr, std::to_string(serviceId));
    m_server->setMessageCallback([this](trantor::UdpSocket* udpSocket, const char* buf, size_t len, trantor::InetAddress&& address, trantor::EventLoop* loop) {
        if (len < 4) {
            return;
        }
        unsigned int conv = MFUtil::readUint32(buf);
        std::shared_ptr<MFUdpChannel> channel = MFUdpChannelManager::getInstance()->getOrCreateChannel(conv, loop, [this](int convId) {
            onActive(convId);
        });

        channel->setReceiveCallback([this](int convId, const char* buffer, size_t length) {
			onMessage(convId, buffer, length);
        });

        channel->setDisconnectCallback([this](int convId) {
            onInActive(convId);
        });

        channel->onReceive(std::move(address), udpSocket, buf, len);
    });

    m_server->setIoLoopNum(ioThread);
    m_server->start();
    m_application->logInfo("start udpServer ip = {}, port = {}, ioThreadNumber = {}", ip, port, ioThread);
    m_eventLoopThread->getLoop()->runEvery(0.01, []() {
        MFUdpChannelManager::getInstance()->updateChannels();
    });
}

void MFUdpServer::stop() {
    m_server->stop();
}

void MFUdpServer::onMessage(int conv, const char* buf, size_t len) {
    MFSocketMessage* message = m_tcpServerMsgPool->pop();
    message->setPool(m_tcpServerMsgPool);
    message->setDst(m_serviceId);
    message->setViewData(buf, static_cast<uint32_t>(len));
	message->setMessageType(LuaMessageTypeUdpServer);
	message->setFd(conv);
    message->setCmd(MFSocketRead);
    message->setUnique(m_unique);
    MFLuaServiceManager::getInstance()->nativeDispatch(message);
}

void MFUdpServer::onActive(uint32_t conv) {
    MFSocketMessage* message = m_tcpServerMsgPool->pop();
    message->setPool(m_tcpServerMsgPool);
    message->setDst(m_serviceId);
    message->setMessageType(LuaMessageTypeUdpServer);
    message->setFd(conv);
    message->setCmd(MFSocketOpen);
    message->setUnique(m_unique);
    MFLuaServiceManager::getInstance()->nativeDispatch(message);
}

void MFUdpServer::onInActive(uint32_t conv) {
    MFSocketMessage* message = m_tcpServerMsgPool->pop();
    message->setPool(m_tcpServerMsgPool);
    message->setDst(m_serviceId);
    message->setMessageType(LuaMessageTypeUdpServer);
    message->setFd(conv);
    message->setCmd(MFSocketClose);
    message->setUnique(m_unique);
    MFLuaServiceManager::getInstance()->nativeDispatch(message);
}
