#include "LLUdpLink.h"

#include <algorithm>
#include <rtt/rtt.h>


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

	// connect tx socket
	if (::connect(m_socketTx, &end.sa, sizeof(end)) != 0) {
		LOG(logERROR) << "Connect failed: " << lastError();
		return false;
	}


	// apply the timeout
	if (m_receiveBlockingTimeoutUs != -1) {
		if (!onBlockingTimeoutChanged(m_receiveBlockingTimeoutUs))
			return false;
	}

	int bitsPerSecond = 48000 * 2 * sizeof(float) * 8;
    if( !enableHighQoS(m_socketTx, bitsPerSecond).valid() || !enableHighQoS(m_socketRx, bitsPerSecond).valid()) {
        LOG(logERROR) << "QoS setup not completed!";
    }


	// set the buffer size, is this necessary?
	
	int n = 32;
	if (setsockopt(m_socketRx, SOL_SOCKET, SO_RCVBUF, (const char *)&n, sizeof(n)) == -1
		|| setsockopt(m_socketTx, SOL_SOCKET, SO_SNDBUF, (const char *)&n, sizeof(n)) == -1) {
		LOG(logERROR) << ("Cannot set rcvbuf size!");
		return false;
	}
	else {
		LOG(logDEBUG) << "Set rcvbuf size to " << n;
		m_desc += ",bufs=" + std::to_string(n);
	}

    // timestamping
	if (m_rxBlockingMode == Mode::KernelBlock || m_rxBlockingMode == Mode::Select) {
		if (!enableTimeStamps(m_socketRx, true))
			return false;
	}
	else {
		LOG(logINFO) << "timestamping currently only for kernel block and select!";
	}


	// On Linux: blocking send sockets perform better?
	if (!socketSetBlocking(m_socketTx, false))
		return false;
	m_desc += ",txasync";

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





int LLUdpLink::receive(uint8_t *buf, int max)
{
	// TODO: need to handle peer shutdown
	if (m_rxBlockingMode != LLCustomBlock::Mode::UserBlock) {
		if (m_rxBlockingMode == LLCustomBlock::Mode::Select) {
			int res = socketSelect(m_socketRx, m_receiveBlockingTimeoutUs);
			if (res == 0) {
				return 0; // timeout!
			}

			if (res < 0) {
				LOG(logERROR) << "Select failed " << res;
				return -1; // link broke! this should not happen!
			}

			// now we can receive!
		}

		int receivedBytes = socketReceive(m_socketRx, buf, max);
		if (receivedBytes == -1) { // -1: no data (or error)
			receivedBytes = 0;
		}
		return receivedBytes;
	}


	// user-space busy loop with timeout that blocks until we have data
	uint32_t i = 0;
	auto t0 = UlltraProto::getMicroSecondsCoarse();

	while (true) {
		int receivedBytes = socketReceive(m_socketRx, buf, max);
		if (receivedBytes >= 0)
			return receivedBytes;



		//Windows to be better with a sleep, than yield
		
		RttThread::YieldCurrent();

		//nsleep(1); // nanosleep on linux seems to be quiete good too (depending on the system!)
		
	

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


// windows does niot have MSG_DONTWAIT, but socket is marked non-blocking anyway
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif

bool LLUdpLink::send(const uint8_t *data, int dataLength)
{
	int ret = sendto(m_socketTx, (const char *)data, dataLength, MSG_DONTWAIT, (struct sockaddr*)&m_addr, sizeof(m_addr));

	if (ret == dataLength)
		return true;

#ifdef _WIN32	
	const auto le = WSAGetLastError();
	if (ret == -1 && (le == WSAEWOULDBLOCK || le == ERROR_IO_PENDING))
		return true;
#else
	if (ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK ))
		return true;
#endif

	LOG(logDEBUG1) << "sendto " << m_addr << " failed: " << ret << " != " << dataLength << lastError();
	return false;
}



void LLUdpLink::toString(std::ostream& stream) const
{
	stream << "udplink(rxblock=" << m_rxBlockingMode << ",to=" << m_receiveBlockingTimeoutUs << "us" << m_desc << ")";
	if (m_socketRx != -1)
		stream << " to " << m_addr;
}
