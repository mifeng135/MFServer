/**
 *
 *  @file UdpServer.h
 *  UDP server with optional multi-threaded I/O via SO_REUSEPORT.
 *
 *  Copyright 2018, An Tao.  All rights reserved.
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the License file.
 *
 */

#pragma once

#include <trantor/exports.h>
#include <trantor/net/EventLoopThreadPool.h>
#include <trantor/net/InetAddress.h>
#include <trantor/utils/NonCopyable.h>
#include <trantor/utils/Logger.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>


namespace trantor
{
   
class UdpSocket;  
    /**
 * @brief UDP server. With setIoLoopNum(N) on platforms that support
 * SO_REUSEPORT (e.g. Linux, BSD), N sockets bind to the same address and
 * the kernel distributes incoming packets across them (one socket per
 * EventLoop thread). On Windows or when N==1, a single UdpSocket is used.
 *
 * @note The message callback may be invoked from multiple threads; it must
 * be thread-safe.
 */
class TRANTOR_EXPORT UdpServer : NonCopyable
{
  public:
    using MessageCallback =
        std::function<void(UdpSocket *socket, const char *buf, size_t len, InetAddress&& address, EventLoop* loop)>;

    using BalancingCallback = std::function<EventLoop*(const char *buf, size_t len, bool&sameLoop, EventLoop* loop)>;
    /**
     * @brief Construct a new UDP server instance.
     * @param loop The event loop for the server (and the only socket when not
     * using setIoLoopNum).
     * @param address The address to bind (e.g. 0.0.0.0:port).
     * @param name Server name (for logging).
     * @param reUseAddr SO_REUSEADDR.
     * @param reUsePort SO_REUSEPORT; when setIoLoopNum(n>1) is used,
     * SO_REUSEPORT is required and will be enabled for the N sockets.
     */
    UdpServer(EventLoop *loop,
              const InetAddress &address,
              std::string name,
              bool reUseAddr = true,
              bool reUsePort = false);
    ~UdpServer();

    void start();
    void stop();

    /**
     * @brief Set the number of I/O event loops. When > 1 and SO_REUSEPORT is
     * available, N UdpSockets are created (one per loop), all binding to the
     * same address; the kernel distributes packets. Call before start().
     */
    void setIoLoopNum(size_t num)
    {
        assert(!started_);
        loopPoolPtr_ = std::make_shared<EventLoopThreadPool>(num, name_ + "Udp");
        loopPoolPtr_->start();
        ioLoops_ = loopPoolPtr_->getLoops();
        numIoLoops_ = ioLoops_.size();
    }

    void setIoLoopThreadPool(const std::shared_ptr<EventLoopThreadPool> &pool)
    {
        assert(pool->size() > 0);
        assert(!started_);
        loopPoolPtr_ = pool;
        loopPoolPtr_->start();
        ioLoops_ = loopPoolPtr_->getLoops();
        numIoLoops_ = ioLoops_.size();
    }

    void setIoLoops(const std::vector<EventLoop *> &ioLoops)
    {
        assert(!ioLoops.empty());
        assert(!started_);
        ioLoops_ = ioLoops;
        numIoLoops_ = ioLoops_.size();
        loopPoolPtr_.reset();
    }

    void setMessageCallback(const MessageCallback &cb)
    {
        messageCallback_ = cb;
    }
    void setMessageCallback(MessageCallback &&cb)
    {
        messageCallback_ = std::move(cb);
    }

    void setBalancingCallback(const BalancingCallback &cb)
    {
        balancingCallback_ = cb;
    }

    void setBalancingCallback(BalancingCallback &&cb)
    {
        balancingCallback_ = std::move(cb);
    }

    const std::string &name() const
    {
        return name_;
    }
    
    std::string ipPort() const;

    const InetAddress &address() const
    {
        return addr_;
    }
    
    EventLoop *getLoop() const
    {
        return loop_;
    }
    
    const std::vector<EventLoop*>& getIoLoops() const
    {
        return ioLoops_;
    }

  private:
    void startInLoop();

    EventLoop *loop_;
    InetAddress addr_;
    std::string name_;
    bool reUseAddr_;
    bool reUsePort_;
    MessageCallback messageCallback_;
    BalancingCallback balancingCallback_;
    std::shared_ptr<EventLoopThreadPool> loopPoolPtr_;
    std::vector<EventLoop *> ioLoops_;
    size_t numIoLoops_{0};

    std::vector<std::shared_ptr<UdpSocket>> sockets_;
    bool started_{false};
};

}  // namespace trantor
