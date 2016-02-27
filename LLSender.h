#pragma once

#include "networking.h"

#include <stdint.h>




class LLSender
{
public:
	LLSender(in_addr addr, int port);
	~LLSender();

	int send(const uint8_t  *data, int dataLength);
private:
	SOCKET m_soc;
	struct sockaddr_in m_addr;
};

