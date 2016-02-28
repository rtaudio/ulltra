#include "LLUdpLink.h"

#include <algorithm>


LLUdpLink::LLUdpLink()
	: LLLink(-1), m_socketRx(-1), m_socketTx(-1)
{
	memset(&m_addr, 0, sizeof(m_addr));
}


LLUdpLink::~LLUdpLink()
{
	if (m_socketRx != -1)
		close(m_socketRx);
	if (m_socketTx != -1)
		close(m_socketTx);
}


bool LLUdpLink::connect(const LinkEndpoint &end)
{
	if (end.addr.ai_family != AF_INET6 && end.addr.ai_family != AF_INET) {
		LOG(logERROR) << "Unknown address family (not IPv4 nor IPv6)!";
		return false;
	}

	bool ipv6 = (end.addr.ai_family == AF_INET6);

	addrinfo addrHints = {}, *addrRes;
	addrHints.ai_family = end.addr.ai_family;
	addrHints.ai_socktype = SOCK_DGRAM;
	addrHints.ai_protocol = IPPROTO_UDP;


	// create sockets for rx and tx
	m_socketRx = socket(addrHints.ai_family, addrHints.ai_socktype, addrHints.ai_protocol);
	m_socketTx = socket(addrHints.ai_family, addrHints.ai_socktype, addrHints.ai_protocol);
	if (m_socketRx == -1 || m_socketTx == -1) {
		LOG(logERROR) << "Cannot create socket!";
		return false;
	}

	// bind rx socket
	addrHints.ai_flags = AI_PASSIVE; // passive for bind()
	getaddrinfo(NULL, std::to_string(end.port).c_str(), &addrHints, &addrRes);
	int rv = bind(m_socketRx, (struct sockaddr *)addrRes->ai_addr, addrRes->ai_addrlen);
	if (rv != 0) {
		LOG(logERROR) << "Cannot bind UDP socket to port " << end.port;
		return false;
	}
	freeaddrinfo(addrRes);

	// create tx address
	getaddrinfo(NULL, std::to_string(end.port).c_str(), &end.addr, &addrRes);
	if (addrRes->ai_addrlen != sizeof(m_addr)) {
		LOG(logERROR) << "addrinfo too long!";
	}
    memcpy(&m_addr, addrRes->ai_addr, (std::min)((int)addrRes->ai_addrlen, (int)sizeof(m_addr)));
	freeaddrinfo(addrRes);


	/* setup QoS */
	// see http://www.cisco.com/c/en/us/td/docs/switches/datacenter/nexus1000/sw/4_0/qos/configuration/guide/nexus1000v_qos/qos_6dscp_val.pdf
	/*
	we want 101 110 | 46 | High Priority | Expedited Forwarding (EF) N/A 101 - Critical
	*/
#ifndef _WIN32
	int iptos = ipv6 ? IPTOS_DSCP_EF : (IPTOS_LOWDELAY | IPTOS_PREC_CRITIC_ECP | IPTOS_THROUGHPUT);

	int so_priority = 7;
	if (setsockopt(m_socketTx, SOL_SOCKET, SO_PRIORITY, &so_priority, sizeof(so_priority)) < 0) {
		LOG(logERROR) << "Error: could not set socket SO_PRIORITY! " << lastError();;
	}
	else {
		LOG(logDEBUG) << "Set SO_PRIORITY to " << so_priority;
	}


	
	// IP_TOS not supported by windows, see https://msdn.microsoft.com/de-de/library/windows/desktop/ms738586(v=vs.85).aspx
	// have to use qWave API instead
	if (setsockopt(m_socketTx, ipv6 ? IPPROTO_IPV6 : SOL_IP, ipv6 ? IPV6_TCLASS : IP_TOS, &iptos, sizeof(iptos)) < 0) {
		LOG(logERROR) << "Error: could not set socket IP_TOS priority to  IPTOS_LOWDELAY | IPTOS_PREC_CRITIC_ECP | IPTOS_THROUGHPUT! " << lastError();
	}
	else {
		LOG(logDEBUG) << "Set IP_TOS to  IPTOS_LOWDELAY | IPTOS_PREC_CRITIC_ECP | IPTOS_THROUGHPUT";
	}
#endif
	// set the buffer size, is this necessary?
	int n = 1024 * 2;
	if (setsockopt(m_socketRx, SOL_SOCKET, SO_RCVBUF, (const char *)&n, sizeof(n)) == -1
		|| setsockopt(m_socketTx, SOL_SOCKET, SO_SNDBUF, (const char *)&n, sizeof(n)) == -1) {
		LOG(logERROR) << ("Cannot set rcvbuf size!");
		return false;
	}
	else {
		LOG(logDEBUG) << "Set rcvbuf size to " << n;
	}

	return true;
}

bool LLUdpLink::onBlockingTimeoutChanged(uint64_t timeoutUs)
{
	bool nonBlock = (timeoutUs == 0);

	if (!nonBlock && m_rxBlockingMode == BlockingMode::Undefined) {
		LOG(logERROR) << ("Cannot set timeout, blocking mode not set!");
		return false;
	}

	bool kblock = (!nonBlock && m_rxBlockingMode == BlockingMode::KernelBlock);


	// set timeout
#ifndef _WIN32
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = timeoutUs;
#else
	int tv = timeoutUs < 1000 ? 1UL : (timeoutUs / 1000UL);
#endif


	if (setsockopt(m_socketRx, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) < 0) {
		LOG(logERROR) << ("Cannot set recvtimeo!");
		return false;
	}


	if (!socketSetBlocking(m_socketRx, kblock))
		return false;

	return true;
}

bool LLUdpLink::setRxBlockingMode(BlockingMode mode)
{
	if (mode == BlockingMode::Undefined || m_rxBlockingMode == mode)
		return false;
	m_rxBlockingMode = mode;


	if (!onBlockingTimeoutChanged(m_receiveBlockingTimeoutUs))
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


inline int socketReceive(SOCKET soc, uint8_t *buffer, int bufferSize)
{
	struct sockaddr_storage remote;
	// todo: use connect + recv
#ifndef _WIN32
	socklen_t addr_len = sizeof(remote);
	return recvfrom((SOCKET)soc, buffer, bufferSize - 1, 0, (struct sockaddr *) &remote, &addr_len);
#else
	int addr_len = sizeof(remote);
	return recvfrom((SOCKET)soc, (char*)buffer, bufferSize - 1, 0, (struct sockaddr *) &remote, &addr_len);
#endif	
}

const uint8_t *LLUdpLink::receive(int &receivedBytes)
{
	if (m_rxBlockingMode != BlockingMode::UserBlock) {
		receivedBytes = socketReceive(m_socketRx, m_rxBuffer, sizeof(m_rxBuffer));
		if (receivedBytes == -1)
			return 0;
		return m_rxBuffer;
	}


	// user-space busy loop with timeout that blocks until we have data
	uint32_t i = 0;
	auto t0 = UlltraProto::getMicroSecondsCoarse();

	while (true) {
		receivedBytes = socketReceive(m_socketRx, m_rxBuffer, sizeof(m_rxBuffer));
		if (receivedBytes >= 0)
			return m_rxBuffer;

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

bool LLUdpLink::send(const uint8_t *data, int dataLength)
{
	return sendto(m_socketTx, (const char *)data, dataLength, 0, (struct sockaddr*)&m_addr, sizeof(m_addr)) == dataLength;
}



void LLUdpLink::toString(std::ostream& stream) const
{
	stream << "udplink(rxblock=" << m_rxBlockingMode << ",to=" << m_receiveBlockingTimeoutUs << "us)";
}
