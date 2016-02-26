#include "LLRxUdp.h"



LLRxUdp::LLRxUdp() : LLRx(-1), m_socket(-1)
{

}


LLRxUdp::~LLRxUdp()
{
	if(m_socket != -1)
		close(m_socket);
}


bool LLRxUdp::connect(const LinkEndpoint &addr)
{
	addrinfo bindAddrHints = {}, *res;
	bindAddrHints.ai_family = addr.addr.ai_family;
	bindAddrHints.ai_socktype = SOCK_DGRAM;
	bindAddrHints.ai_flags = AI_PASSIVE;
	bindAddrHints.ai_protocol = IPPROTO_UDP;
	getaddrinfo(NULL, std::to_string(addr.port).c_str(), &bindAddrHints, &res);

	// create socket
	m_socket = socket(bindAddrHints.ai_family, bindAddrHints.ai_socktype, bindAddrHints.ai_protocol);
	if (m_socket == -1) {
		LOG(logERROR) << "Cannot create socket!";
		return false;
	}

	// bind socket
	int rv = bind(m_socket, (struct sockaddr *)res->ai_addr, res->ai_addrlen);
	if (rv != 0) {
		LOG(logERROR) << "Cannot bind UDP socket to port " << addr.port;
		return false;
	}

	freeaddrinfo(res);

	// set the buffer size, is this necessary?
	int n = 1024 * 2;
	if (setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, (const char *)&n, sizeof(n)) == -1) {
		LOG(logERROR) << ("Cannot set rcvbuf size!");
		return false;
	}
	else {
		LOG(logDEBUG) << "Set rcvbuf size to " << n;
	}
	return true;
}

bool LLRxUdp::onBlockingTimeoutChanged(uint64_t timeoutUs)
{
	bool nonBlock = (timeoutUs == 0);

	if (!nonBlock && m_blockingMode == BlockingMode::Undefined) {
		LOG(logERROR) << ("Cannot set timeout, blocking mode not set!");
		return false;
	}

	bool kblock = (!nonBlock && m_blockingMode == BlockingMode::KernelBlock);


	// set timeout
#ifndef _WIN32
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = timeoutUs;
#else
	int tv = timeoutUs < 1000 ? 1UL : (timeoutUs / 1000UL);
#endif


	if (setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) < 0) {
		LOG(logERROR) << ("Cannot set recvtimeo!");
		return false;
	}


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
}

bool LLRxUdp::setBlockingMode(BlockingMode mode)
{
	if (mode == BlockingMode::Undefined || m_blockingMode == mode)
		return false;
	m_blockingMode = mode;


	if (!onBlockingTimeoutChanged(m_blockingTimeoutUs))
		return false;

#if _DEBUG
	if (mode == BlockingMode::KernelBlock) {
		LOG(logDEBUG1) << "socket blocking mode set to kernel blocking";
	} else {
		LOG(logDEBUG1) << "socket blocking mode set to user blocking";
	}
#endif

	return true;
}


inline int socketReceive(SOCKET soc, uint8_t *buffer, int bufferSize, struct sockaddr_storage &remote)
{
#ifndef _WIN32
	socklen_t addr_len = sizeof(remote);
	return recvfrom((SOCKET)soc, buffer, bufferSize - 1, 0, (struct sockaddr *) &remote, &addr_len);
#else
	int addr_len = sizeof(remote);
	return recvfrom((SOCKET)soc, (char*)buffer, bufferSize - 1, 0, (struct sockaddr *) &remote, &addr_len);
#endif	
}

const uint8_t *LLRxUdp::receive(int &receivedBytes, struct sockaddr_storage &remote)
{
	if (m_blockingMode != BlockingMode::UserBlock) {
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
			if ((t1 - t0) > m_blockingTimeoutUs) {
				return 0;
			}
		}
		i++;
	}
}


void LLRxUdp::toString(std::ostream& stream) const
{
	stream << "udpsocket(block=" << m_blockingMode << ",to=" << m_blockingTimeoutUs << "us)";
}