#include "MFUdpChannelManager.hpp"
#include "MFUdpChannel.hpp"

MFUdpChannelManager* MFUdpChannelManager::m_instance = nullptr;

MFUdpChannelManager::MFUdpChannelManager() {

}

MFUdpChannelManager::~MFUdpChannelManager() {
}

MFUdpChannelManager* MFUdpChannelManager::getInstance() {
	if (!m_instance) {
		m_instance = new MFUdpChannelManager();
	}
	return m_instance;
}

void MFUdpChannelManager::destroyInstance()
{
	if (m_instance) {
		delete m_instance;
		m_instance = nullptr;
	}
}

std::shared_ptr<MFUdpChannel> MFUdpChannelManager::getOrCreateChannel(unsigned int conv, trantor::EventLoop* loop, std::function<void(int conv)> connectCallback) {
	std::shared_ptr<MFUdpChannel> channel;
	if (m_udpChannelMap.find(conv, channel)) {
		return channel;
	}

	std::lock_guard<MFSpinLock> guard(m_createLock);
	if (m_udpChannelMap.find(conv, channel)) {
		return channel;
	}

	channel = std::make_shared<MFUdpChannel>(conv, loop, connectCallback);
	channel->init();
	m_udpChannelMap.insert(conv, channel);
	return channel;
}

void MFUdpChannelManager::eraseChannel(unsigned int conv) {
	m_udpChannelMap.erase(conv);
}

void MFUdpChannelManager::updateChannels() {
	std::vector<unsigned int> toRemove;
	m_udpChannelMap.for_each([&toRemove](const unsigned int&, std::shared_ptr<MFUdpChannel>& ch) {
		if (ch->isDisconnected()) {
			toRemove.push_back(ch->getConv());
			ch->onDisconnect();
		} else {
			ch->updateKcp();
		}
	});

	for (auto conv : toRemove) {
		m_udpChannelMap.erase(conv);
	}
}

void MFUdpChannelManager::udpSendAll(const char* buf, size_t len) {
	m_udpChannelMap.for_each([buf, len](const unsigned int&, std::shared_ptr<MFUdpChannel>& ch) {
		ch->send(buf, len);
	});
}

void MFUdpChannelManager::udpSend(unsigned int conv, const char* buf, size_t len) {
	m_udpChannelMap.find_fn(conv, [buf, len](const std::shared_ptr<MFUdpChannel>& ch) {
		ch->send(buf, len);
	});
}
