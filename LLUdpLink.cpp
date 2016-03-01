#include "LLUdpLink.h"

#include <algorithm>


LLUdpLink::LLUdpLink()
	: LLCustomBlock(-1), m_socketRx(-1), m_socketTx(-1)
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


bool LLUdpLink::connect(const LinkEndpoint &end, bool isMaster)
{
	if (end.getFamily() != AF_INET6 &&end.getFamily() != AF_INET) {
		LOG(logERROR) << "Unknown address family (not IPv4 nor IPv6)!";
		return false;
	}

	// store endpoint address (used for sending later)
	m_addr = end;

	bool ipv6 = (m_addr.getFamily() == AF_INET6);

	addrinfo addrHints = {}, *addrRes;
	addrHints.ai_family = m_addr.getFamily();
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
	getaddrinfo(NULL, std::to_string(m_addr.getPort()).c_str(), &addrHints, &addrRes);
	int rv = bind(m_socketRx, (struct sockaddr *)addrRes->ai_addr, addrRes->ai_addrlen);
	if (rv != 0) {
		LOG(logERROR) << "Cannot bind UDP socket to port " << m_addr.getPort();
		return false;
	}
	freeaddrinfo(addrRes);


	// apply the timeout
	if (m_receiveBlockingTimeoutUs != -1) {
		if (!onBlockingTimeoutChanged(m_receiveBlockingTimeoutUs))
			return false;
	}


	/* setup QoS */
	// see http://www.cisco.com/c/en/us/td/docs/switches/datacenter/nexus1000/sw/4_0/qos/configuration/guide/nexus1000v_qos/qos_6dscp_val.pdf
	/*
	we want 101 110 | 46 | High Priority | Expedited Forwarding (EF) N/A 101 - Critical
	*/
#ifndef _WIN32
	int iptos = ipv6 ? IPTOS_DSCP_EF : (IPTOS_LOWDELAY | IPTOS_PREC_CRITIC_ECP | IPTOS_THROUGHPUT);

	int so_priority = 7;
	if (setsockopt(m_socketTx, SOL_SOCKET, SO_PRIORITY, &so_priority, sizeof(so_priority)) < 0) {
		LOG(logERROR) << "Error: could not set socket SO_PRIORITY to  " << so_priority << "! " << lastError();;
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
	if (m_socketRx == -1)
		return true;

	bool nonBlock = (timeoutUs == 0);

	if (!nonBlock && m_rxBlockingMode == LLCustomBlock::Mode::Undefined) {
		LOG(logERROR) << ("Cannot set timeout, blocking mode not set!");
		return false;
	}

	// set kernel blocking for kernel-block mode and select
	bool kblock = (!nonBlock && m_rxBlockingMode != LLCustomBlock::Mode::UserBlock);

	bool socketTimeout = (m_rxBlockingMode == LLCustomBlock::Mode::KernelBlock);


	if (!socketTimeout) timeoutUs = 0;
	// set timeout
#ifndef _WIN32
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = timeoutUs; // o means no timeout
#else
	int tv = (timeoutUs < 1000 ? 1UL : (timeoutUs / 1000UL));
#endif


	if (setsockopt(m_socketRx, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) < 0) {
		LOG(logERROR) << "Cannot set recvtimeo to " << timeoutUs << "us! " << lastError();
		return false;
	}

	LOG(logDEBUG3) << "Set  SO_RCVTIMEO of socket (" << m_socketRx << ") to " << timeoutUs << "us";


	if (!socketSetBlocking(m_socketRx, kblock))
		return false;


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
	if (m_rxBlockingMode != LLCustomBlock::Mode::UserBlock) {
		if (m_rxBlockingMode == LLCustomBlock::Mode::Select) {
			int res = socketSelect(m_socketRx, m_receiveBlockingTimeoutUs);
			if (res == 0) {
				return 0; // timeout!
			}

			if (res < 0) {
				LOG(logERROR) << "Select failed " << res;
				receivedBytes = -1; //link broke!
				return 0; // this should not happen!
			}

			// now we can receive!
		}

		receivedBytes = socketReceive(m_socketRx, m_rxBuffer, sizeof(m_rxBuffer));
		if (receivedBytes == -1) {
			receivedBytes = 0;
			return 0;
		}
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
				receivedBytes = 0;
				return 0;
			}
		}
		i++;
	}
}

bool LLUdpLink::send(const uint8_t *data, int dataLength)
{
	int ret = sendto(m_socketTx, (const char *)data, dataLength, 0, (struct sockaddr*)&m_addr, sizeof(m_addr));

	if (ret == dataLength)
		return true;

	LOG(logDEBUG1) << "sendto " << m_addr << " failed: " << ret << " != " << dataLength;
	return false;
}



void LLUdpLink::toString(std::ostream& stream) const
{
	stream << "udplink(rxblock=" << m_rxBlockingMode << ",to=" << m_receiveBlockingTimeoutUs << "us)";
	if (m_socketRx != -1)
		stream << " to " << m_addr;
}
