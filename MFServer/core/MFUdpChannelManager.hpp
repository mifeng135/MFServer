#ifndef MF_UDP_CHANNEL_MANAGER_H
#define MF_UDP_CHANNEL_MANAGER_H

#include "MFStripedMap.hpp"
#include "MFSpinLock.hpp"

class MFUdpChannel;

namespace trantor {
class EventLoop;
}

class MFUdpChannelManager {
public:
	MFUdpChannelManager();
	~MFUdpChannelManager();
public:
	static MFUdpChannelManager* getInstance();
	static void destroyInstance();
public:
	std::shared_ptr<MFUdpChannel> getOrCreateChannel(unsigned int conv, trantor::EventLoop* loop, std::function<void(int conv)> connectCallback = nullptr);
	void eraseChannel(unsigned int conv);
	void updateChannels();
	void udpSendAll(const char* buf, size_t len);
	void udpSend(unsigned int conv, const char* buf, size_t len);
private:
	MFStripedMap<unsigned int, std::shared_ptr<MFUdpChannel>, 16>	m_udpChannelMap;
	MFSpinLock							m_createLock;
private:
	static MFUdpChannelManager*				m_instance;
};


#endif //MF_UDP_CHANNEL_MANAGER_H
