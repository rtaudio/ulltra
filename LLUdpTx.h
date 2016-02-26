#pragma once
#include "networking.h"
#include "UlltraProto.h"

#include "LLTx.h"
class LLUdpTx :
	public LLTx
{
public:
	LLUdpTx();
	~LLUdpTx();

	bool connect(const LinkEndpoint &addr);
	bool send(const uint8_t *data, int dataLength);

	void toString(std::ostream& stream) const;

private:
	SOCKET m_socket;
	addrinfo m_addr;
};

