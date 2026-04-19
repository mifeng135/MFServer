#ifndef MFTcpServer_hpp
#define MFTcpServer_hpp

#include "trantor/net/TcpServer.h"
#include "trantor/net/EventLoopThread.h"
#include "MFObjectPool.hpp"


class MFApplication;
class MFSocketMessage;


class ConnectContext
{
public:
    ConnectContext()
    : m_connectId(0)
    , m_uid(0) {

    }
public:
    void setConnectId(size_t id) { m_connectId = id;}
    [[nodiscard]] size_t getConnectId() const { return m_connectId; }
    
    void setUid(uint64_t id) { m_uid = id; }
    [[nodiscard]] uint64_t getUid() const { return m_uid; }
private:
    size_t      m_connectId;
    uint64_t    m_uid;
};

class MFTcpServer
{
public:
    explicit MFTcpServer(MFApplication* application);
    ~MFTcpServer();
public:
    void init(const std::string& ip, uint16_t port, MFServiceId_t serviceId, int ioThread, uint32_t unique, uint8_t type);
    void stop();
    void setIdleTime(size_t timeSeconds);
private:
    void onActive(const trantor::TcpConnectionPtr &connPtr);
    void onInActive(const trantor::TcpConnectionPtr &connPtr);
    void onRead(const trantor::TcpConnectionPtr &connectionPtr, trantor::MsgBuffer *buffer);
private:
    trantor::TcpServer*                                     m_server;
    MFServiceId_t                                             m_serviceId;
    MFApplication*                                          m_application;
    trantor::EventLoopThread*                               m_eventLoopThread;
private:
    MFObjectPool<MFSocketMessage>*                          m_tcpServerMsgPool;
    uint32_t                                                m_unique;
    uint8_t                                                 m_type;
};
#endif /* MFTcpServer_hpp */
