#pragma once

#include "Discovery.h"
#include "LinkEval.h"
#include "Controller.h"

class RttThread;

class NetworkManager
{
public:
	NetworkManager();
	~NetworkManager();

	inline bool isRunning() const { return m_isRunning;  } 

    const Discovery::NodeDevice &getDiscoveredNode(const sockaddr_storage &addr) const;
private:

	Discovery m_discovery;
	LinkEval m_linkEval;
	Controller m_contr;

	bool m_isRunning;
	RttThread *m_updateThread;

	void updateThreadMain(void *arg);
};

