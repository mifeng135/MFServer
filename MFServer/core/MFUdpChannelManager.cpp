#include "MFUdpChannelManager.hpp"
#include "MFUdpChannel.hpp"
#include "MFUtil.hpp"

MFUdpChannelManager* MFUdpChannelManager::m_instance = nullptr;

MFEventLoopTimer::MFEventLoopTimer(trantor::EventLoop *eventLoop)
: m_eventLoop(eventLoop)
, m_timerId(0) {

}


void MFEventLoopTimer::start() {
	m_timerId = m_eventLoop->runEvery(0.01, [this] {
		uint32_t now = MFUtil::iclock();
		auto& channels = MFUdpChannelManager::getInstance()->getChannels();
		if (channels.empty()) {
			return;
		}
		auto& slot = channels[m_eventLoop->index()];
		if (!slot) {
			return;
		}
		auto& loopChannel = *slot;
        for (auto it = loopChannel.begin(); it != loopChannel.end(); ++it) {
            std::shared_ptr<MFUdpChannel>& ch = it->second;
            if (ch->isDisconnected(now)) {
                m_waitRemove.push_back(ch->getConv());
                ch->onDisconnect();
            } else {
                ch->updateKcp(now);
            }
        }

		for (auto conv : m_waitRemove) {
			loopChannel.erase(conv);
			MFUdpChannelManager::getInstance()->remove(conv);
		}
        m_waitRemove.clear();
	});
}

void MFEventLoopTimer::stop() {
	if (m_timerId <= 0) {
		return;
	}
	m_eventLoop->invalidateTimer(m_timerId);
	m_timerId = 0;
}

MFUdpChannelManager::MFUdpChannelManager() {

}

MFUdpChannelManager::~MFUdpChannelManager() {
	for (size_t i = 0; i < m_eventLoopTimerVec.size(); i++) {
		m_eventLoopTimerVec[i]->stop();
	}
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

void MFUdpChannelManager::init(const std::vector<trantor::EventLoop *>& loops) {
	m_udpChannelLoopVec.reserve(loops.size());
	for (size_t i = 0; i < loops.size(); ++i) {
		trantor::EventLoop* loop = loops[i];
		loop->setIndex(i);
		m_udpChannelLoopVec.push_back(std::make_unique<MFUdpLoopChannelMap>());
		m_eventLoopTimerVec.emplace_back(std::make_unique<MFEventLoopTimer>(loop));
		m_eventLoopTimerVec.back()->start();
	}
}

std::shared_ptr<MFUdpChannel> MFUdpChannelManager::getOrCreateChannel(uint32_t conv, trantor::EventLoop* loop, std::function<void(uint32_t conv)> connectCallback) {
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
	const size_t idx = loop->index();
	m_udpChannelLoopVec[idx]->insert(std::make_pair(conv, channel));
	return channel;
}

std::shared_ptr<MFUdpChannel> MFUdpChannelManager::findChannel(uint32_t conv) {
	std::shared_ptr<MFUdpChannel> channel;
	if (m_udpChannelMap.find(conv, channel)) {
		return channel;
	}
	return nullptr;
}


void MFUdpChannelManager::udpSend(uint32_t conv, const char* buf, size_t len) {
	m_udpChannelMap.find_fn(conv, [buf, len](const std::shared_ptr<MFUdpChannel>& ch) {
		ch->send(buf, len);
	});
}

void MFUdpChannelManager::removeChannel(uint32_t conv) {
    m_udpChannelMap.find_fn(conv, [](const std::shared_ptr<MFUdpChannel>& ch) {
        ch->removeChannel();
    });
}

void MFUdpChannelManager::remove(uint32_t conv) {
    m_udpChannelMap.erase(conv);
}

