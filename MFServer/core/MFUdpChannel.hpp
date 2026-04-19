#ifndef MF_KCP_HANDLE_H
#define MF_KCP_HANDLE_H

#include <memory>
#include <atomic>
#include "kcp/ikcp.h"
#include "trantor/net/InetAddress.h"
#include "trantor/net/UdpSocket.h"
#include "trantor/utils/MsgBuffer.h"

class MFUdpChannel : public std::enable_shared_from_this<MFUdpChannel> {
public:
	explicit MFUdpChannel(unsigned int conv, trantor::EventLoop* loop, std::function<void(int conv)> connectFn = nullptr, uint32_t timeoutMs = 30000);
	~MFUdpChannel();
public:
    void init();
    void send(const char* buf, size_t len);
    int realSend(const char* buf, size_t len);
    void onReceive(trantor::InetAddress&& address, trantor::UdpSocket* socket, const char* buf, size_t len);
	void updateKcp();
	void onDisconnect();
public:
    unsigned int getConv() const;
    bool isDisconnected() const;

    void setReceiveCallback(std::function<void(int conv, const char* buf, size_t len)> callback) {
        m_receiveCallback = std::move(callback);
	}

    void setDisconnectCallback(std::function<void(int conv)> callback) {
        m_disconnectCallback = std::move(callback);
    }
private:
    void initKcp();
    void updateChannelData(trantor::InetAddress&& address, trantor::UdpSocket* socket);
    void receiveInLoop(const char* buf, size_t len);
private:
    ikcpcb*                 m_kcp;
    trantor::InetAddress    m_address;
    trantor::MsgBuffer*     m_receiveBuffer;
    trantor::UdpSocket*     m_udpSocket;
    trantor::EventLoop*     m_eventLoop;
    unsigned int            m_conv;
    uint32_t                m_timeoutMs;
    uint32_t                m_lastRecvTs;
	std::function<void(int, const char*, size_t)>   m_receiveCallback;
	std::function<void(int conv)>                   m_disconnectCallback;
	std::function<void(int conv)>                   m_openCallback;
};

#endif //MF_KCP_HANDLE_H
