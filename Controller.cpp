#include "Controller.h"



Controller::Controller()
{
	m_server.on("link_eval", [](const NodeAddr &addr, const JsonObject &request) {
		std::string resp;

		return resp;
	});
}


Controller::~Controller()
{
}

bool Controller::init()
{
	if (!m_server.start(UP::HttpControlPort))
		return false;

	return true;
}

bool Controller::update(time_t now)
{
	m_server.update();
	return true;
}
