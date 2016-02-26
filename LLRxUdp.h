#pragma once
#include "LLRx.h"

/*
PF_PACKAT, MMAP
readings:
http://www.linuxquestions.org/questions/programming-9/sending-receiving-udp-packets-through-a-pf_packet-socket-887762/
*/


class LLRxUdp :
	public LLRx
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

	LLRxUdp();
	~LLRxUdp();

	bool connect(const LinkEndpoint &addr);

	bool onBlockingTimeoutChanged(uint64_t timeoutUs);
	bool flushBuffer();
	const uint8_t *receive(int &receivedBytes, struct sockaddr_storage &remote);


	void toString(std::ostream& stream) const;

	bool setBlockingMode(BlockingMode mode);
private:
	SOCKET m_socket;
	BlockingMode m_blockingMode;
	uint8_t  m_receiveBuffer[1024 * 8 + 1];
};

