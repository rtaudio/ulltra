#include "NetworkManager.h"

#include <rtt.h>
#include<time.h>
#include<iostream>

#include"UlltraProto.h"


NetworkManager::NetworkManager()
{
#if _WIN32
	static bool needWSInit = true;

	if (needWSInit) {
		WSADATA wsa;
		WSAStartup(MAKEWORD(2, 0), &wsa);
		needWSInit = false;
	}
#endif

	m_isRunning = true;
	RttThread::Routine updateThread(std::bind(&NetworkManager::updateThreadMain, this, std::placeholders::_1));
	m_updateThread = new RttThread(updateThread);
}


NetworkManager::~NetworkManager()
{
}


void NetworkManager::updateThreadMain(void *arg)
{
	m_discovery.onNodeDiscovered = [this](const Discovery::NodeDevice &node) {
        if (node.getId() > m_discovery.getHwAddress()) {
			LOG(logINFO) << "initiating link evaluation to node " << node;

			// request a latency test start
			m_discovery.send(UlltraProto::LatencyTestStartToken, node);
			m_linkEval.latencyTestMaster(node);
		}
	};

	
	m_discovery.onNodeLost = [](const Discovery::NodeDevice &node) {

	};

	m_discovery.on(UlltraProto::LatencyTestStartToken, [this](const Discovery::NodeDevice &node) {
		m_linkEval.latencyTestSlave(node);
	});

	if (!m_discovery.start(UlltraProto::DiscoveryPort)) {
		m_isRunning = false;
		LOG(logERROR) << "Discovery init failed!";
		return;
	}

	if (!m_linkEval.init(this)) {
		m_isRunning = false;
		LOG(logERROR) << "Link evaluation init failed!";
		return;
	}


	if (!m_contr.init()) {
		m_isRunning = false;
		LOG(logERROR) << "Controller init failed!";
		return;
	}

	time_t now;

	while (m_isRunning) {
		now = time(NULL);

		m_discovery.update(now);
		//m_linkEval.update(now);
		m_contr.update(now);

#ifdef _WIN32
		Sleep(200);
#else
		usleep(1000*200);
#endif
	}

}

const Discovery::NodeDevice &NetworkManager::getDiscoveredNode(const sockaddr_storage &addr) const
{
	return m_discovery.getNode(addr);
}

