#pragma once

#include "networking.h"

#include "UlltraProto.h"

class LLReceiver
{
public:
	enum class BlockingMode { KernelBlock, UserBlock, NoBlock };

	LLReceiver(int port, int receiveBlockingTimeoutMs);
	virtual ~LLReceiver();

	const uint8_t  *receive(int &receivedBytes, struct sockaddr_in &remote);
	
	bool setBlocking(BlockingMode mode);
	bool clearBuffer();
private:
	SOCKET m_socket;

	BlockingMode m_isBlocking;

	int m_receiveBlockingTimeoutUs;

	uint8_t  m_receiveBuffer[1024 * 8 + 1];
};

