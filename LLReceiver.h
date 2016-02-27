#pragma once

#include "networking.h"

#include "UlltraProto.h"

enum class BlockingMode { KernelBlock, UserBlock, NoBlock };

inline std::ostream & operator<<(std::ostream & Str, const BlockingMode b) {
	switch (b) {
	case BlockingMode::KernelBlock: return Str << "KernelBlock";
	case BlockingMode::UserBlock: return Str << "UserBlock";
	case BlockingMode::NoBlock: return Str << "NoBlock";
	}
}


class LLReceiver
{
public:
	

	LLReceiver(int port, int receiveBlockingTimeoutUs);
	virtual ~LLReceiver();

	const uint8_t  *receive(int &receivedBytes, struct sockaddr_storage &remote);
	
	bool setBlocking(BlockingMode mode);
	bool clearBuffer();

	inline friend std::ostream& operator<< (std::ostream& stream, const LLReceiver& llr) {
		return stream << "udpsocket(block=" << llr.m_isBlocking <<",to="<< llr.m_receiveBlockingTimeoutUs<<"us)";
	}

private:
	SOCKET m_socket;

	BlockingMode m_isBlocking;

	int m_receiveBlockingTimeoutUs;

	uint8_t  m_receiveBuffer[1024 * 8 + 1];
};




