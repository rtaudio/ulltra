#pragma once
#ifdef _WIN32
#include "LLLink.h"

#include <winsock2.h>
#include <mswsock.h>
#include <qos2.h> //qwave

class LLUdpQWave :
	public LLLink
{
public:
	LLUdpQWave();
	~LLUdpQWave();

	bool connect(const LinkEndpoint &addr, bool isMaster);
	void toString(std::ostream& stream) const;

	/* rx */
	bool onBlockingTimeoutChanged(uint64_t timeoutUs);
	const uint8_t *receive(int &receivedBytes);

	/* tx */
	bool send(const uint8_t *data, int dataLength);
private:
	

	SOCKET m_socketTx, m_socketRx;
	SocketAddress m_addr;

	uint8_t  m_txBuffer[1024 * 8 + 1];
	uint8_t  m_rxBuffer[1024 * 8 + 1];

	WSAOVERLAPPED               m_sendOverlapped;
	LPTRANSMIT_PACKETS_ELEMENT  m_transmitEl;//buffer
	LPFN_TRANSMITPACKETS m_sendFnc;//quieried send function pointer
	
	QosHandle m_qos;


	volatile int m_sentBefore;
	volatile bool m_sending;

	std::string m_desc;

};

#endif