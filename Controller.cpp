#include "Controller.h"



Controller::Controller()
{
}


Controller::~Controller()
{
}

bool Controller::init()
{
	if (!m_server.start(UP::HttpControlPort))
		return false;
}

bool Controller::update(time_t now)
{
	m_server.update();
	return true;
}
