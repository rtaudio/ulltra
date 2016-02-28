#pragma once

#include "JsonHttpServer.h"
#include "HttpClient.h"


class Controller
{
public:
	Controller();
	~Controller();
	bool init();
	bool update(time_t now);

private:
	JsonHttpClient m_client;
	JsonHttpServer m_server;
};

