#pragma once

#include "networking.h"

#include <stdint.h>



class SimpleUdpReceiver
{
public:
	~SimpleUdpReceiver();
	static SimpleUdpReceiver *create(int port, bool nonBlocking);
	const uint8_t * receive(int &receivedBytes, struct sockaddr_storage &remote);

private:
	SimpleUdpReceiver();
	SOCKET m_recvSocket;
	uint8_t  m_receiveBuffer[1024 * 8 + 1];
};

