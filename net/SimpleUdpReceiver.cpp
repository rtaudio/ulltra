#include "SimpleUdpReceiver.h"


#include "networking.h"

#include <iostream>
#include <cstring>


SimpleUdpReceiver::SimpleUdpReceiver()
{
	memset(m_receiveBuffer, 0, sizeof(m_receiveBuffer));
}


SimpleUdpReceiver::~SimpleUdpReceiver()
{
	shutdown(m_recvSocket, 2);
#ifndef _WIN32
	close((SOCKET)m_recvSocket);
#endif
}


SimpleUdpReceiver  *SimpleUdpReceiver::create(int port, bool nonBlocking)
{
	SimpleUdpReceiver *sur = new SimpleUdpReceiver();
	// bind receiving socket
	sur->m_recvSocket = socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in local;
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_port = htons(port);
	local.sin_addr.s_addr = INADDR_ANY;
	int rv = bind((SOCKET)sur->m_recvSocket, (struct sockaddr *) &local, sizeof(local));

	if (rv != 0) {
		delete sur;
		LOG(logERROR) << "Failed to bind UDP receiving socket!" << lastError();
		return 0;
	}

	if (!socketSetBlocking(sur->m_recvSocket, !nonBlocking)) {
		return 0;
	}


	return sur;
}

const uint8_t *SimpleUdpReceiver::receive(int &receivedBytes, struct sockaddr_storage &remote) {
	socklen_t addr_len = sizeof(remote);
	receivedBytes = recvfrom(m_recvSocket, (socket_buffer_t)m_receiveBuffer, sizeof(m_receiveBuffer)-1, 0,
		(struct sockaddr *) &remote, &addr_len);

	if (receivedBytes == -1)
		return 0;

	return m_receiveBuffer;
}

