#ifdef _WIN32

#include "LLUdpQWave.h"

#pragma comment(lib, "Qwave.lib")



SOCKET createWSASocket(const SocketAddress &dest, LPFN_TRANSMITPACKETS  *transmitPackets);

LLUdpQWave::LLUdpQWave()
	:LLLink(0),
	m_socketRx(-1), m_socketTx(-1)
{
	m_sentBefore = 0;
	m_sending = false;

	m_transmitEl = (LPTRANSMIT_PACKETS_ELEMENT)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(TRANSMIT_PACKETS_ELEMENT) * 1);
	m_transmitEl[0].dwElFlags = TP_ELEMENT_MEMORY | TP_ELEMENT_EOP;
	m_transmitEl[0].pBuffer = m_txBuffer;

	ZeroMemory(&m_sendOverlapped, sizeof(m_sendOverlapped));
	m_sendOverlapped.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
}


LLUdpQWave::~LLUdpQWave()
{

	QOSRemoveSocketFromFlow(s_qosHandle, NULL, m_qos.handle, 0);

	HeapFree(GetProcessHeap(), 0, m_transmitEl);
	CloseHandle(m_sendOverlapped.hEvent);

	if (m_socketRx != -1)
		close(m_socketRx);
	if (m_socketTx != -1)
		close(m_socketTx);
	m_socketRx = m_socketTx = -1;
}

bool LLUdpQWave::connect(const LinkEndpoint &addr, bool isMaster)
{
	m_addr = addr;
	m_socketTx = createWSASocket(addr, &m_sendFnc);
	if(m_socketTx == INVALID_SOCKET)
		return false;




	addrinfo addrHints = {}, *addrRes;
	addrHints.ai_family = m_addr.getFamily();
	addrHints.ai_socktype = SOCK_DGRAM;
	addrHints.ai_protocol = IPPROTO_UDP;

	m_socketRx = socket(addrHints.ai_family, addrHints.ai_socktype, addrHints.ai_protocol);
	// bind rx socket
	addrHints.ai_flags = AI_PASSIVE; // passive for bind()
	getaddrinfo(NULL, std::to_string(m_addr.getPort()).c_str(), &addrHints, &addrRes);
	int rv = bind(m_socketRx, (struct sockaddr *)addrRes->ai_addr, addrRes->ai_addrlen);
	if (rv != 0) {
		LOG(logERROR) << "Cannot bind UDP socket to port " << m_addr.getPort();
		return false;
	}
	freeaddrinfo(addrRes);

	// apply the timeout
	if (m_receiveBlockingTimeoutUs != -1) {
		if (!onBlockingTimeoutChanged(m_receiveBlockingTimeoutUs))
			return false;
	}


	// set the buffer size, is this necessary?

	int n = 32;
	if (setsockopt(m_socketRx, SOL_SOCKET, SO_RCVBUF, (const char *)&n, sizeof(n)) == -1
		|| setsockopt(m_socketTx, SOL_SOCKET, SO_SNDBUF, (const char *)&n, sizeof(n)) == -1) {
		LOG(logERROR) << ("Cannot set rcvbuf size!");
		return false;
	}
	else {
		LOG(logDEBUG) << "Set rcvbuf size to " << n;
		m_desc += ",bufs=" + std::to_string(n);
	}




	// assume 2 channel 32-bit @ 48Khz
	int bitsPerSecond = 48000 * 2 * sizeof(float) * 8;
	m_qos = enableHighQoS(m_socketTx, bitsPerSecond);
	if (!m_qos.valid())
		return false;

}


SOCKET createWSASocket(const SocketAddress &dest, LPFN_TRANSMITPACKETS  *transmitPackets)
{
	INT                 returnValue;
	DWORD               bytesReturned;
	GUID TransmitPacketsGuid = WSAID_TRANSMITPACKETS;

	SOCKET socket;

	// create the socket
	socket = WSASocket(dest.getFamily(), SOCK_DGRAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	if (socket == INVALID_SOCKET) {
		fprintf(stderr, "%s:%d - WSASocket failed (%d)\n", __FILE__, __LINE__, WSAGetLastError());
		return INVALID_SOCKET;
	}

	// Connect the new socket to the destination
	returnValue = WSAConnect(socket,
		(PSOCKADDR)&dest,
		sizeof(dest),
		NULL,
		NULL,
		NULL, // TODO: Qos params?
		NULL);

	if (returnValue != NO_ERROR) {
		fprintf(stderr, "%s:%d - WSAConnect failed (%d)\n", __FILE__, __LINE__, WSAGetLastError());
		return INVALID_SOCKET;
	}

	// Query the function pointer for the TransmitPacket function
	returnValue = WSAIoctl(socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&TransmitPacketsGuid,
		sizeof(GUID),
		transmitPackets,
		sizeof(PVOID),
		&bytesReturned,
		NULL,
		NULL);

	if (returnValue == SOCKET_ERROR) {
		fprintf(stderr, "%s:%d - WSAIoctl failed (%d)\n", __FILE__, __LINE__, WSAGetLastError());
		return INVALID_SOCKET;
	}

	return socket;
}

bool LLUdpQWave::send(const uint8_t *data, int dataLength)
{
	if (dataLength > 1400)
		return false;

	m_transmitEl[0].pBuffer = (PVOID)data;
	m_transmitEl[0].cLength = dataLength;
	//lastLen = len;

	//return send(soc, (const char*)data, len, 0) == len;

	BOOL result = (*m_sendFnc)(m_socketTx,
		m_transmitEl,
		1, //currentPacketRate, 
		0xFFFFFFFF,
		&m_sendOverlapped,
		TF_USE_KERNEL_APC);

	if (result == FALSE) {
		DWORD lastError;

		lastError = WSAGetLastError();
		if (lastError != ERROR_IO_PENDING) {
			fprintf(stderr, "%s:%d - TransmitPackets failed (%d)\n",
				__FILE__, __LINE__, GetLastError());
			m_sentBefore = 0;
			return false;
		}
	}

	m_sending = false;

	return true;
}



const uint8_t *LLUdpQWave::receive(int &receivedBytes)
{
	receivedBytes = socketReceive(m_socketRx, m_rxBuffer, sizeof(m_rxBuffer));
	if (receivedBytes == -1) {
		receivedBytes = 0;
		return 0;
	}

	return m_rxBuffer;
}


void LLUdpQWave::toString(std::ostream& stream) const
{
	stream << "qwaveudp(to=" << m_receiveBlockingTimeoutUs << "us" << m_desc << ")";
	if (m_socketRx != -1)
		stream << " to " << m_addr;
}


bool LLUdpQWave::onBlockingTimeoutChanged(uint64_t timeoutUs)
{
	if (m_socketRx == -1)
		return true;

	bool nonBlock = (timeoutUs == 0); // 0 means async!

	// set timeout
#ifndef _WIN32
	struct timeval tv;
	tv.tv_sec = timeoutUs / 1000000UL;
	tv.tv_usec = timeoutUs % 1000000UL; // 0 means no timeout (block forever)
#else
	int tv = (timeoutUs < 1000 ? 1UL : (timeoutUs / 1000UL));
#endif


	if (setsockopt(m_socketRx, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) < 0) {
		LOG(logERROR) << "Cannot set recvtimeo to " << timeoutUs << "us! " << lastError();
		return false;
	}

	LOG(logDEBUG3) << "Set  SO_RCVTIMEO of socket (" << m_socketRx << ") to " << timeoutUs << "us";


	if (!socketSetBlocking(m_socketRx, !nonBlock))
		return false;


	return true;
}
#endif