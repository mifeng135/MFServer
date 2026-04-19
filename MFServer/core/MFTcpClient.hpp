#ifndef MFTcpClient_hpp
#define MFTcpClient_hpp

#include "trantor/net/TcpClient.h"
#include "trantor/net/EventLoopThread.h"
#include "MFObjectPool.hpp"



class MFApplication;
class MFSocketMessage;

struct MFWaitSend {
    std::string sendStr;
public:
    MFWaitSend(const char* src, size_t size) {
        sendStr = std::string(src, size);
    }
    MFWaitSend()
    : sendStr(""){

    }
};

class MFTcpClient
{
public:
    explicit MFTcpClient(MFApplication* application);
    ~MFTcpClient();
public:
    void initClient(const std::string &ip, uint16_t port, MFServiceId_t serviceId, uint32_t unique, uint8_t type);
    void disconnect();
    void send(const char* msg, size_t len);
private:
    void onActive(const trantor::TcpConnectionPtr &connPtr);
    void onInActive(const trantor::TcpConnectionPtr &connPtr);
    void onRead(const trantor::TcpConnectionPtr &connectionPtr, trantor::MsgBuffer *buffer);
    void sendWait();
private:
    MFApplication*                              m_application;
    std::shared_ptr<trantor::TcpClient>         m_client;
    MFServiceId_t                                 m_serviceId;
    trantor::TcpConnectionPtr                   m_connect;
    MFObjectPool<MFSocketMessage>*              m_tcpClientPool;
    uint32_t                                    m_unique;
    uint8_t                                     m_type;
    MFConcurrentQueue<MFWaitSend>               m_waitSendQueue{32};
    trantor::EventLoopThread*                   m_eventLoopThread;
};

#endif /* MFTcpClient_hpp */
