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
	//const uint8_t *receive(int &receivedBytes);
	int receive(uint8_t *buffer, int max);

	/* tx */
	bool send(const uint8_t *data, int dataLength);

private:
	SOCKET m_socAccept; //TCP accept
	SOCKET m_soc; //TCP connection

	std::string m_desc;

	int m_recvTimeoutUs;

	std::atomic<bool> m_connected;
	std::atomic<bool> m_connectionFailed;
};

