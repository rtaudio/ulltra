#pragma once
#include "LLLink.h"

/*
PF_PACKAT, MMAP
readings:
http://www.linuxquestions.org/questions/programming-9/sending-receiving-udp-packets-through-a-pf_packet-socket-887762/
*/


class LLUdpLink :
	public LLLink
{
public:
	enum class BlockingMode { KernelBlock, UserBlock, Undefined };

	friend inline std::ostream & operator<<(std::ostream & Str, const BlockingMode b) {
		switch (b) {
		case BlockingMode::KernelBlock: return Str << "KernelBlock";
		case BlockingMode::UserBlock: return Str << "UserBlock";
		case BlockingMode::Undefined: return Str << "Undefined";
		}
	}

	LLUdpLink();
	~LLUdpLink();

	bool connect(const LinkEndpoint &addr);
	void toString(std::ostream& stream) const;

	/* rx */
	bool onBlockingTimeoutChanged(uint64_t timeoutUs);
	bool flushBuffer();
	const uint8_t *receive(int &receivedBytes);
	bool setRxBlockingMode(BlockingMode mode);


	/* tx */
	bool send(const uint8_t *data, int dataLength);
	
private:
	SOCKET m_socketRx, m_socketTx;

	BlockingMode m_rxBlockingMode;
	uint8_t  m_rxBuffer[1024 * 8 + 1];


	struct sockaddr_storage m_addr;
};

