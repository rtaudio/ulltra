#pragma once
#include "LLLink.h"
#include "LLCustomBlock.h"

/*
PF_PACKAT, MMAP
readings:
http://www.linuxquestions.org/questions/programming-9/sending-receiving-udp-packets-through-a-pf_packet-socket-887762/
*/


class LLUdpLink :
	public LLCustomBlock
{
public:
	LLUdpLink();
	~LLUdpLink();

	bool connect(const LinkEndpoint &addr, bool isMaster);
	void toString(std::ostream& stream) const;

	/* rx */
	bool onBlockingTimeoutChanged(uint64_t timeoutUs);
	//const uint8_t *receive(int &receivedBytes);
	int receive(uint8_t *buffer, int max);

	/* tx */
	bool send(const uint8_t *data, int dataLength);
	
private:
	SOCKET m_socketRx, m_socketTx;
	SocketAddress m_addr;
	std::string m_desc;
};

