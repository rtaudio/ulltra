#include "NetworkManager.h"

#include <rtt.h>

NetworkManager::NetworkManager()
{
#if WIN32
	static bool needWSInit = true;

	if (needWSInit) {
		WSADATA wsa;
		WSAStartup(MAKEWORD(2, 0), &wsa);
		needWSInit = false;
	}
#endif

	RttThread::Routine updateThread(std::bind(&NetworkManager::updateThreadMain, this, std::placeholders::_1));
	m_updateThread = new RttThread(updateThread);
}


NetworkManager::~NetworkManager()
{
}






void NetworkManager::updateThreadMain(void *arg)
{
	if (!m_discovery.start(26025)) {
		m_isRunning = false;
		return;
	}

	while (m_isRunning) {
		m_discovery.update();

#ifdef WIN32
		Sleep(200);
#else
		usleep(1000*200);
#endif
	}

}
