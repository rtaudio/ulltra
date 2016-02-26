#include "LLUdpTx.h"



LLUdpTx::LLUdpTx()
{
	m_addr = {};
	m_socket = -1;
}


LLUdpTx::~LLUdpTx()
{
	if (m_socket != -1)
		close(m_socket);
}



bool LLUdpTx::connect(const LinkEndpoint &addr)
{
	if (addr.addr.ai_family != AF_INET6 && addr.addr.ai_family != AF_INET) {
		LOG(logERROR) << "Unknown address family (not IPv4 nor IPv6)!";
		return false;
	}
	bool ipv6 = (addr.addr.ai_family == AF_INET6);


	addrinfo *res;
	m_addr.ai_family = addr.addr.ai_family;
	m_addr.ai_socktype = SOCK_DGRAM;
	m_addr.ai_protocol = IPPROTO_UDP;
	getaddrinfo(NULL, std::to_string(addr.port).c_str(), &m_addr, &res);
	
	// create socket
	m_socket = socket(m_addr.ai_family, m_addr.ai_socktype, m_addr.ai_protocol);
	if (m_socket == -1) {
		LOG(logERROR) << "Cannot create socket!";
		return false;
	}

	// see http://www.cisco.com/c/en/us/td/docs/switches/datacenter/nexus1000/sw/4_0/qos/configuration/guide/nexus1000v_qos/qos_6dscp_val.pdf
	/*
	we want 101 110 | 46 | High Priority | Expedited Forwarding (EF) N/A 101 - Critical
	*/
#ifndef _WIN32

	int so_priority = 7;
	if (setsockopt(m_socket, SOL_SOCKET, SO_PRIORITY, &so_priority, sizeof(so_priority)) < 0) {
		LOG(logERROR) << "Error: could not set socket SO_PRIORITY! " << lastError();;
	}
	else {
		LOG(logDEBUG) << "Set SO_PRIORITY to " << so_priority;
	}


	int iptos = ipv6 ? IPTOS_DSCP_EF : (IPTOS_LOWDELAY | IPTOS_PREC_CRITIC_ECP | IPTOS_THROUGHPUT);
	// IP_TOS not supported by windows, see https://msdn.microsoft.com/de-de/library/windows/desktop/ms738586(v=vs.85).aspx
	// have to use qWave API instead
	if (setsockopt(soc, ipv6 ? IPPROTO_IPV6 : SOL_IP, ipv6 ? IPV6_TCLASS : IP_TOS, &iptos, sizeof(iptos)) < 0) {
		LOG(logERROR) << "Error: could not set socket IP_TOS priority to  IPTOS_LOWDELAY | IPTOS_PREC_CRITIC_ECP | IPTOS_THROUGHPUT! " << lastError();
	}
	else {
		LOG(logDEBUG) << "Set IP_TOS to  IPTOS_LOWDELAY | IPTOS_PREC_CRITIC_ECP | IPTOS_THROUGHPUT";
	}


#endif

	// set the buffer size, is this necessary?
	int n = 1024 * 2;

	if (setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, (const char *)&n, sizeof(n)) == -1) {
		LOG(logERROR) << "Cannot set sndbuf size!";
	}
	else {
		LOG(logDEBUG) << "Set sndbuf size to " << n;
	}
}


bool LLUdpTx::send(const uint8_t *data, int dataLength)
{
	return sendto(m_socket, (const char *)data, dataLength, 0, (struct sockaddr*)&m_addr, sizeof(m_addr)) == dataLength;
}

void LLUdpTx::toString(std::ostream& stream) const
{

}