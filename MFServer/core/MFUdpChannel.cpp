#include "MFUdpChannel.hpp"
#include "MFUtil.hpp"

static inline uint32_t iclock() {
    return static_cast<uint32_t>(MFUtil::getMilliseconds() & 0xfffffffful);
}

static int udpOutPutFn(const char *buf, int len, ikcpcb *kcp, void *user) {
    MFUdpChannel *channel = static_cast<MFUdpChannel*>(user);
    if (!channel) {
        return 0;
    }
    return channel->realSend(buf, len);
}

MFUdpChannel::MFUdpChannel(uint32_t conv, trantor::EventLoop* loop, std::function<void(uint32_t conv)> connectFn, uint32_t timeoutMs)
: m_kcp(nullptr)
, m_receiveBuffer(nullptr)
, m_udpSocket(nullptr)
, m_eventLoop(loop)
, m_conv(conv)
, m_timeoutMs(timeoutMs)
, m_lastRecvTs(0)
, m_receiveCallback(nullptr)
, m_disconnectCallback(nullptr)
, m_openCallback(std::move(connectFn))
, m_isRemove(false) {

}

MFUdpChannel::~MFUdpChannel() {
    if (m_kcp) {
        ikcp_release(m_kcp);
        m_kcp = nullptr;
    }
    delete m_receiveBuffer;
}

void MFUdpChannel::init() {
    m_receiveBuffer = new trantor::MsgBuffer();
    initKcp();
    if (m_openCallback) {
        m_openCallback(m_conv);
	}
}

void MFUdpChannel::initKcp() {
    m_kcp = ikcp_create(m_conv, this);
    m_kcp->output = udpOutPutFn;
    ikcp_wndsize(m_kcp, 128, 128);
    ikcp_nodelay(m_kcp, 1, 10, 2, 1);
    m_kcp->rx_minrto = 10;
    m_kcp->fastresend = 1;
}

void MFUdpChannel::send(const char* buf, size_t len) {
    std::shared_ptr<MFUdpChannel> self = shared_from_this();
    m_eventLoop->runInLoop([self, payload = std::string(buf, len)] {
        ikcp_send(self->m_kcp, payload.data(), static_cast<int>(payload.size()));
    });
}

int MFUdpChannel::realSend(const char *buf, size_t len) {
    if (m_udpSocket) {
        return static_cast<int>(m_udpSocket->send(buf, len, m_address));
    }
    return 0;
}

void MFUdpChannel::onReceive(trantor::InetAddress&& address, trantor::UdpSocket* socket, const char* buf, size_t len, trantor::EventLoop* loop) {
    m_address = std::move(address);
    m_udpSocket = socket;
    processBuffer(buf, len);
}

void MFUdpChannel::processBuffer(const char* buf, size_t len) {
    m_lastRecvTs = iclock();
    if (ikcp_input(m_kcp, buf, static_cast<long>(len)) != 0) {
        return;
    }
    while (true) {
        int psz = ikcp_peeksize(m_kcp);
        if (psz < 0) {
            break;
        }
        m_receiveBuffer->ensureWritableBytes(static_cast<size_t>(psz));
        int r = ikcp_recv(m_kcp, m_receiveBuffer->beginWrite(), psz);
        if (r < 0) {
            break;
        }
        m_receiveBuffer->hasWritten(static_cast<size_t>(r));
		if (m_receiveCallback) {
            m_receiveCallback(m_conv, m_receiveBuffer->peek(), m_receiveBuffer->readableBytes());
        }
        m_receiveBuffer->retrieveAll();
    }
    ikcp_update(m_kcp, m_lastRecvTs);
}

void MFUdpChannel::updateKcp() {
    uint32_t currentTime = iclock();
    uint32_t nextUpdate = ikcp_check(m_kcp, currentTime);
    if (currentTime >= nextUpdate) {
        ikcp_update(m_kcp, currentTime);
    }
}

void MFUdpChannel::onDisconnect() {
    if (m_disconnectCallback) {
        m_disconnectCallback(m_conv);
    }
}

void MFUdpChannel::removeChannel() {
    std::shared_ptr<MFUdpChannel> self = shared_from_this();
    m_eventLoop->runInLoop([self] {
        self->m_isRemove = true;
    });
}

uint32_t MFUdpChannel::getConv() const {
    return m_conv;
}	

bool MFUdpChannel::isDisconnected() const {
    if (m_lastRecvTs == 0) {
        return false;
    }
    if (m_isRemove) {
        return true;
    }
    uint32_t now = iclock();
    return now - m_lastRecvTs > m_timeoutMs;
}
