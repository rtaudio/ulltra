#include "LLSender.h"

#include "networking.h"

#include <iostream>
#include <cstring>

#include "UlltraProto.h"

/*
PF_PACKAT, MMAP
readings:
http://www.linuxquestions.org/questions/programming-9/sending-receiving-udp-packets-through-a-pf_packet-socket-887762/
*/


LLSender::LLSender(in_addr addr, int port)
{
	memset(&m_addr, 0, sizeof(struct sockaddr_in));
	m_addr.sin_family = AF_INET;
	m_addr.sin_port = htons(port);
	m_addr.sin_addr = addr;

	SOCKET soc = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (soc == -1) {
		LOG(logERROR) << "Error: could not create socket! " << lastError();
	}
	m_soc = soc;


	// see http://www.cisco.com/c/en/us/td/docs/switches/datacenter/nexus1000/sw/4_0/qos/configuration/guide/nexus1000v_qos/qos_6dscp_val.pdf
	/*
	we want 101 110 | 46 | High Priority | Expedited Forwarding (EF) N/A 101 - Critical
	*/
#ifndef _WIN32

	int so_priority = 7;
	if (setsockopt(soc, SOL_SOCKET, SO_PRIORITY, &so_priority, sizeof(so_priority)) < 0) {
		LOG(logERROR) << "Error: could not set socket SO_PRIORITY! " << lastError();;
	}
	else {
		LOG(logDEBUG) << "Set SO_PRIORITY to " << so_priority;
	}


	int iptos = IPTOS_LOWDELAY | IPTOS_PREC_CRITIC_ECP | IPTOS_THROUGHPUT;
	// IP_TOS not supported by windows, see https://msdn.microsoft.com/de-de/library/windows/desktop/ms738586(v=vs.85).aspx
	// have to use qWave API instead
	if (setsockopt(soc, SOL_IP, IP_TOS, &iptos, sizeof(iptos)) < 0) {
		LOG(logERROR) << "Error: could not set socket IP_TOS priority to  IPTOS_LOWDELAY | IPTOS_PREC_CRITIC_ECP | IPTOS_THROUGHPUT! " << lastError();
	}
	else {
		LOG(logDEBUG) << "Set IP_TOS to  IPTOS_LOWDELAY | IPTOS_PREC_CRITIC_ECP | IPTOS_THROUGHPUT";
	}


#endif

	// set the buffer size, is this necessary?
	int n = 1024 * 2;

	if (setsockopt(soc, SOL_SOCKET, SO_SNDBUF, (const char *)&n, sizeof(n)) == -1) {
		LOG(logERROR) << "Cannot set sndbuf size!";
	} else {
		LOG(logDEBUG) << "Set sndbuf size to " << n;
	}
}


LLSender::~LLSender()
{
	shutdown((SOCKET)m_soc, 2);
#ifndef _WIN32
	close((SOCKET)m_soc);
#endif
}

int LLSender::send(const uint8_t  *data, int dataLength)
{
	return sendto((SOCKET)m_soc, (const char *)data, dataLength, 0, (struct sockaddr*)&m_addr, sizeof(m_addr));
}
