#ifndef MF_UDP_CHANNEL_MANAGER_H
#define MF_UDP_CHANNEL_MANAGER_H

#include <memory>
#include <vector>

#include "MFStripedMap.hpp"
#include "MFSpinLock.hpp"

class MFUdpChannel;

namespace trantor {
class EventLoop;
}

using MFUdpLoopChannelMap = MFFastMap<uint32_t, std::shared_ptr<MFUdpChannel>>;

class MFEventLoopTimer {
public:
	explicit MFEventLoopTimer(trantor::EventLoop* eventLoop);
public:
	void start();
	void stop();
	trantor::EventLoop* getEventLoop() { return m_eventLoop; }
private:
	trantor::EventLoop* m_eventLoop;
	std::vector<int>	m_waitRemove;
	uint64_t			m_timerId;
};

class MFUdpChannelManager {
public:
	MFUdpChannelManager();
	~MFUdpChannelManager();
public:
	static MFUdpChannelManager* getInstance();
	static void destroyInstance();
public:
	void init(const std::vector<trantor::EventLoop *>& loops);
	std::shared_ptr<MFUdpChannel> getOrCreateChannel(uint32_t conv, trantor::EventLoop* loop, std::function<void(uint32_t conv)> connectCallback = nullptr);
	std::shared_ptr<MFUdpChannel> findChannel(uint32_t conv);
	void udpSend(uint32_t conv, const char* buf, size_t len);
	void removeChannel(uint32_t conv);
	std::vector<std::unique_ptr<MFUdpLoopChannelMap>>& getChannels() { return m_udpChannelLoopVec; }
    void remove(uint32_t conv);
private:
	std::vector<std::unique_ptr<MFUdpLoopChannelMap>>						m_udpChannelLoopVec;
	MFSpinLock																m_createLock;
	MFStripedMap<uint32_t, std::shared_ptr<MFUdpChannel>>					m_udpChannelMap;
	std::vector<std::unique_ptr<MFEventLoopTimer>>							m_eventLoopTimerVec;
private:
	static MFUdpChannelManager*				m_instance;
};


#endif //MF_UDP_CHANNEL_MANAGER_H
