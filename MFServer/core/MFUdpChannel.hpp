#ifndef MF_KCP_HANDLE_H
#define MF_KCP_HANDLE_H

#include "kcp/ikcp.h"
#include "trantor/net/InetAddress.h"
#include "trantor/net/UdpSocket.h"
#include "trantor/utils/MsgBuffer.h"

class MFUdpChannel : public std::enable_shared_from_this<MFUdpChannel> {
public:
	explicit MFUdpChannel(uint32_t conv, trantor::EventLoop* loop, std::function<void(uint32_t conv)> connectFn = nullptr, uint32_t timeoutMs = 30000);
	~MFUdpChannel();
public:
    void init();
    void send(const char* buf, size_t len);
    int realSend(const char* buf, size_t len);
    void onReceive(trantor::InetAddress&& address, trantor::UdpSocket* socket, const char* buf, size_t len, trantor::EventLoop* loop);
	void updateKcp(uint32_t currentTime);
	void onDisconnect();
    void removeChannel();
public:
    uint32_t getConv() const;
    bool isDisconnected(uint32_t now) const;
    trantor::EventLoop* getEventLoop() { return m_eventLoop; }

    void setReceiveCallback(std::function<void(uint32_t conv, const char* buf, size_t len)> callback) {
        m_receiveCallback = std::move(callback);
	}

    void setDisconnectCallback(std::function<void(uint32_t conv)> callback) {
        m_disconnectCallback = std::move(callback);
    }
private:
    void initKcp();
    void processBuffer(const char* buf, size_t len);
private:
    ikcpcb*                 m_kcp;
    trantor::InetAddress    m_address;
    trantor::MsgBuffer*     m_receiveBuffer;
    trantor::UdpSocket*     m_udpSocket;
    trantor::EventLoop*     m_eventLoop;
    uint32_t                m_conv;
    uint32_t                m_timeoutMs;
    uint32_t                m_lastRecvTs;
	std::function<void(uint32_t, const char*, size_t)>      m_receiveCallback;
	std::function<void(uint32_t conv)>                      m_disconnectCallback;
	std::function<void(uint32_t conv)>                      m_openCallback;
    bool                    m_isRemove;
};

#endif //MF_KCP_HANDLE_H
