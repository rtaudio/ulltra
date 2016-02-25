#include "SimpleUdpReceiver.h"


#include "networking.h"

#include <iostream>
#include <cstring>

void print_last_errno(int err = 0);

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

	if (nonBlocking) {
#ifndef _WIN32
		if (-1 == fcntl((SOCKET)sur->m_recvSocket, F_SETFL, O_NONBLOCK)) {
			LOG(logERROR) << ("Cannot set non-blocking I/O!") << lastError();
			return 0;
		}
#else
		u_long iMode = 1; // non-blocking
		if (ioctlsocket((SOCKET)sur->m_recvSocket, FIONBIO, &iMode) != NO_ERROR)
		{
			LOG(logERROR) << ("Cannot set non-blocking I/O!") << lastError();
			return 0;
		}
#endif
	}

	return sur;
}


const uint8_t  *SimpleUdpReceiver::receive(int &receivedBytes, struct sockaddr_in &remote) {
#ifndef _WIN32
	socklen_t addr_len = sizeof(remote);
	receivedBytes = recvfrom((SOCKET)m_recvSocket, m_receiveBuffer, sizeof(m_receiveBuffer)-1, 0, (struct sockaddr *) &remote, &addr_len);
#else
	int addr_len = sizeof(remote);
	receivedBytes = recvfrom((SOCKET)m_recvSocket, (char*)m_receiveBuffer, sizeof(m_receiveBuffer)-1, 0, (struct sockaddr *) &remote, &addr_len);
#endif	

	if (receivedBytes == -1)
		return 0;

	

	return m_receiveBuffer;
}

