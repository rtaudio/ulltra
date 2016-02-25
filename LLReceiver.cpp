#include "networking.h"

#include "LLReceiver.h"

#include "UlltraProto.h"

#include <time.h>

#include <iostream>
#include <cstring>
#include <thread>

//#define USE_SELECT

LLReceiver::LLReceiver(int port, int receiveBlockingTimeoutUs) : m_receiveBlockingTimeoutUs(receiveBlockingTimeoutUs)
{
	// bind receiving socket
	m_socket = socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in local;
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_port = htons(port);
	local.sin_addr.s_addr = INADDR_ANY;
	int rv = bind((SOCKET)m_socket, (struct sockaddr *) &local, sizeof(local));

	if (rv != 0) {
		LOG(logERROR) << "Cannot bind UDP socket to port " << port;
	}

	// default: blocking
	setBlocking(BlockingMode::KernelBlock);

	typedef const char * optval;


#ifndef USE_SELECT
#ifndef _WIN32
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = m_receiveBlockingTimeoutUs;
#else
	int tv = m_receiveBlockingTimeoutUs < 1000 ? 1UL : (m_receiveBlockingTimeoutUs / 1000UL);
#endif
	if (setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (optval)&tv, sizeof(tv)) < 0) {
		LOG(logERROR) << ("Cannot set recvtimeo!");
	}
#endif


	// set the buffer size, is this necessary?
	int n = 1024 * 2;

	if (setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, (optval)&n, sizeof(n)) == -1) {
		LOG(logERROR) << ("Cannot set rcvbuf size!");
	} else {
		LOG(logDEBUG) << "Set rcvbuf size to " << n;
	}
}


LLReceiver::~LLReceiver()
{
	int yes = 1;
	setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

	shutdown((SOCKET)m_socket, 2);
#ifndef _WIN32
	close((SOCKET)m_socket);
#endif
}

bool LLReceiver::setBlocking(BlockingMode  blocking)
{
	bool kblock = (blocking == BlockingMode::KernelBlock);

#ifndef _WIN32
	const int flags = fcntl(m_socket, F_GETFL, 0);
	if ((flags & O_NONBLOCK) == kblock) {
		LOG(logDEBUG) << "Kernel blocking mode of socket already set to " << kblock;
		return true;
	}

	if (-1 == fcntl((SOCKET)m_socket, F_SETFL, kblock ? flags ^ O_NONBLOCK : flags | O_NONBLOCK)) {
		LOG(logERROR) << "Cannot set socket kernel blocking to " << kblock << "!";
		return false;
	}
#else
	u_long iMode = kblock ? 0 : 1;
	if (ioctlsocket((SOCKET)m_socket, FIONBIO, &iMode) != NO_ERROR)
	{
		LOG(logERROR) << "Cannot set socket kernel blocking to " << kblock << "!";
		return false;
	}
#endif

#if _DEBUG
	if (kblock) {
		LOG(logDEBUG1) << "socket blocking mode set to kernel blocking";
	} else if(blocking == BlockingMode::UserBlock) {
		LOG(logDEBUG1) << "socket blocking mode set to user blocking";
	}
	else {
		LOG(logDEBUG1) << "socket blocking mode set to non-blocking";
	}
#endif

	m_isBlocking = blocking;

	return true;
}


bool LLReceiver::clearBuffer()
{
	auto bl = m_isBlocking;
	if (bl != BlockingMode::NoBlock) {
		if (!setBlocking(BlockingMode::NoBlock))
			return false;
	}
	int recvLen;
	struct sockaddr_in addr;
	while (receive(recvLen, addr)) {}
	if (bl != BlockingMode::NoBlock) return setBlocking(bl); // restore previous blocking state
	return true;
}

inline int socketReceive(SOCKET soc, uint8_t *buffer, int bufferSize, struct sockaddr_in &remote)
{
#ifndef _WIN32
	socklen_t addr_len = sizeof(remote);
	return recvfrom((SOCKET)soc, buffer, bufferSize - 1, 0, (struct sockaddr *) &remote, &addr_len);
#else
	int addr_len = sizeof(remote);
	return recvfrom((SOCKET)soc, (char*)buffer, bufferSize - 1, 0, (struct sockaddr *) &remote, &addr_len);
#endif	
}

const uint8_t  *LLReceiver::receive(int &receivedBytes, struct sockaddr_in &remote) {
	if (m_isBlocking != BlockingMode::UserBlock) {
		receivedBytes = socketReceive(m_socket, m_receiveBuffer, sizeof(m_receiveBuffer), remote);
		if (receivedBytes == -1)
			return 0;
		return m_receiveBuffer;
	}


	// user-space busy loop with timeout that blocks until we have data
	uint32_t i = 0;
	auto t0 = UlltraProto::getMicroSecondsCoarse();

	while (true) {
		receivedBytes = socketReceive(m_socket, m_receiveBuffer, sizeof(m_receiveBuffer), remote);
		if (receivedBytes >= 0)
			return m_receiveBuffer;

		std::this_thread::yield();

		// check for timeout every 10 cycles
		if ((i % 10) == 0) {
			auto t1 = UlltraProto::getMicroSecondsCoarse();
			if ((t1 - t0) > m_receiveBlockingTimeoutUs) {
				return 0;
			}
		}
		i++;
	}
}



