#include "LLLink.h"

LLLink::~LLLink()
{
}


bool LLLink::setBlockingTimeout(uint64_t timeoutUs) {
	if (m_receiveBlockingTimeoutUs != timeoutUs) {
		if (onBlockingTimeoutChanged(timeoutUs))
			m_receiveBlockingTimeoutUs = timeoutUs;
		else {
			LOG(logERROR) << "Blocking timeout not changed!";
			return false;
		}
	}
	return true;
}

bool LLLink::flushReceiveBuffer() {
	auto t = m_receiveBlockingTimeoutUs;
	if (t != 0) setBlockingTimeout(0);
	int recvLen;
	while (receive(recvLen)) {}
	if (t != 0) return setBlockingTimeout(t); // restore previous timeout
	return true;
}

bool LLLink::enableHighQoS(SOCKET soc)
{
	SocketAddress sa;
	socklen_t len = sizeof(sa);
	if (getsockname(soc, &sa.sa, &len) != 0) {
		LOG(logERROR) << " getsockname failed!" << lastError();
		return false;
	}

	bool ipv6 = sa.getFamily() == AF_INET6;

	/* setup QoS */
	// see http://www.cisco.com/c/en/us/td/docs/switches/datacenter/nexus1000/sw/4_0/qos/configuration/guide/nexus1000v_qos/qos_6dscp_val.pdf
	/*
	we want 101 110 | 46 | High Priority | Expedited Forwarding (EF) N/A 101 - Critical
	*/
#ifndef _WIN32
	int iptos = ipv6 ? IPTOS_DSCP_EF : (IPTOS_LOWDELAY | IPTOS_PREC_CRITIC_ECP | IPTOS_THROUGHPUT);

	int so_priority = 7;
	if (setsockopt(soc, SOL_SOCKET, SO_PRIORITY, &so_priority, sizeof(so_priority)) < 0) {
		LOG(logERROR) << "Error: could not set socket SO_PRIORITY to  " << so_priority << "! " << lastError();
		return false;
	}
	else {
		LOG(logDEBUG) << "Set SO_PRIORITY to " << so_priority;
	}



	// IP_TOS not supported by windows, see https://msdn.microsoft.com/de-de/library/windows/desktop/ms738586(v=vs.85).aspx
	// have to use qWave API instead
	if (setsockopt(soc, ipv6 ? IPPROTO_IPV6 : SOL_IP, ipv6 ? IPV6_TCLASS : IP_TOS, &iptos, sizeof(iptos)) < 0) {
		LOG(logERROR) << "Error: could not set socket IP_TOS priority to  IPTOS_LOWDELAY | IPTOS_PREC_CRITIC_ECP | IPTOS_THROUGHPUT! " << lastError();
		return false;
	}
	else {
		LOG(logDEBUG) << "Set IP_TOS to  IPTOS_LOWDELAY | IPTOS_PREC_CRITIC_ECP | IPTOS_THROUGHPUT";
	}
#else
	LOG(logERROR) << "No QoS imple on Windows yet! ipv6:" << ipv6;
	return false;
#endif
}