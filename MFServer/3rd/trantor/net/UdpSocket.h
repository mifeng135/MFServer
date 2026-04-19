/**
 *
 *  @file UdpSocket.h
 *  UDP socket with EventLoop integration.
 *
 *  Copyright 2018, An Tao.  All rights reserved.
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the License file.
 *
 */

#pragma once

#include <trantor/net/EventLoop.h>
#include <trantor/net/InetAddress.h>
#include <trantor/utils/NonCopyable.h>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace trantor
{
class Channel;

/**
 * @brief UDP socket bound to an EventLoop. Receives datagrams via a message
 * callback and sends with sendTo(). When the kernel
 * send buffer is full, outbound datagrams are queued and flushed on POLLOUT.
 */
class TRANTOR_EXPORT UdpSocket : NonCopyable
{
  public:
    using MessageCallback =
        std::function<void(UdpSocket *socket, const char *buf, size_t len, InetAddress&& address, EventLoop* loop)>;

    /**
     * @brief Create and bind a UDP socket on the given address.
     * @param loop Event loop that drives this socket.
     * @param bindAddr Local address to bind (e.g. 0.0.0.0:port).
     * @param reUseAddr SO_REUSEADDR
     * @param reUsePort SO_REUSEPORT (if supported)
     */
    UdpSocket(EventLoop *loop,
              const InetAddress &bindAddr,
              bool reUseAddr = true,
              bool reUsePort = false);
    ~UdpSocket();

    void setMessageCallback(const MessageCallback &cb)
    {
        messageCallback_ = cb;
    }
    void setMessageCallback(MessageCallback &&cb)
    {
        messageCallback_ = std::move(cb);
    }

    /**
     * @brief Send a datagram to the given peer address.
     * @return Bytes passed to the kernel immediately, or len if queued for send
     * when the socket is not writable (EAGAIN). Returns -1 on error. If called
     * from another thread, schedules the send on the loop thread and returns len.
     */
    long long send(const char *buf, size_t len, const InetAddress &peer);

    /**
     * @brief Return the underlying socket fd (e.g. for advanced options).
     */
    int fd() const
    {
        return sockFd_;
    }

    void setReuseAddr(bool on);
    void setReusePort(bool on);

  private:
    void handleRead();
    void handleWrite();
    long long sendToInLoop(const char *buf, size_t len, const InetAddress &peer);
    void bindAddress(const InetAddress &localaddr);

    static int createNonblockingUdpSocket(int family);

    int sockFd_;
    EventLoop *loop_;
    std::unique_ptr<Channel> channel_;
    MessageCallback messageCallback_;
    std::deque<std::pair<InetAddress, std::string>> sendQueue_;
};

}  // namespace trantor
