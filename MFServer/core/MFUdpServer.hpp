#ifndef MFUdpServer_hpp
#define MFUdpServer_hpp

#include "trantor/net/EventLoopThread.h"
#include "MFObjectPool.hpp"

#include "trantor/net/UdpServer.h"

class MFApplication;
class MFSocketMessage;

class MFUdpServer {
public:
    explicit MFUdpServer(MFApplication* application);
    ~MFUdpServer();
public:
    void init(const std::string& ip, uint16_t port, MFServiceId_t serviceId, int ioThread, uint32_t unique);
    void stop();
private:
    void onMessage(int conv, const char* buf, size_t len);
	void onActive(uint32_t conv);
	void onInActive(uint32_t conv);
private:
    trantor::UdpServer*                                     m_server;
    MFServiceId_t                                             m_serviceId;
    MFApplication*                                          m_application;
    trantor::EventLoopThread*                               m_eventLoopThread;
private:
    MFObjectPool<MFSocketMessage>*                          m_tcpServerMsgPool;
    uint32_t                                                m_unique;
};


#endif /* MFUdpServer_hpp */
