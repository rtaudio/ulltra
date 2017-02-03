#pragma once
#include "LLLink.h"
#include "LLCustomBlock.h"

class LLTcp :
	public LLCustomBlock
{
public:
	LLTcp();
	~LLTcp();

	bool connect(const LinkEndpoint &addr, bool isMaster);
	void toString(std::ostream& stream) const;

	/* rx */
	bool onBlockingTimeoutChanged(uint64_t timeoutUs);
	bool flushBuffer();
	const uint8_t *receive(int &receivedBytes);

	/* tx */
	bool send(const uint8_t *data, int dataLength);

private:
	SOCKET m_socAccept; //TCP accept
	SOCKET m_soc; //TCP connection
	uint8_t  m_rxBuffer[1024 * 8 + 1];

	std::string m_desc;

	int m_recvTimeoutUs;
};

