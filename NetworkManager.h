#pragma once

#include "Discovery.h"

class RttThread;

class NetworkManager
{
public:
	NetworkManager();
	~NetworkManager();

	inline bool isRunning() const { return m_isRunning;  } 

private:

	Discovery m_discovery;

	bool m_isRunning;
	RttThread *m_updateThread;

	void updateThreadMain(void *arg);
};

