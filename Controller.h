#pragma once

#include <rtt.h>
#include "Discovery.h"
#include "LinkEval.h"
#include "JsonHttpServer.h"
#include "JsonHttpClient.h"


class Controller
{
public:
	Controller();
	~Controller();
	bool init();

	inline bool isRunning() const { return m_isRunning; }

private:
	void updateThreadMain(void *arg);

	Discovery m_discovery;
	LinkEval m_linkEval;
	JsonHttpClient m_client;
	JsonHttpServer m_server;

	bool m_isRunning;
	RttThread *m_updateThread;
};