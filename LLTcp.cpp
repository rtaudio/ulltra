#include "LLTcp.h"

#ifndef _WIN32
#include <netinet/tcp.h>
#endif


LLTcp::LLTcp()
	: LLCustomBlock(-1)
{
	m_soc = -1;
	m_socAccept = -1;
	//m_socAccept = socket(NodeDevice::local6.addrStorage.ss_family, SOCK_STREAM, 0);

	/*
	auto bindAddr = NodeDevice::local6.getAddr(broadcastPort + 3);
	if (bind(m_socAccept, (struct sockaddr *) &bindAddr, sizeof(bindAddr)) < 0) {
		LOG(logERROR) << "tcp bind to " << bindAddr << " failed!";
		return false;
	}

	if(listen(m_socAccept, UlltraProto::DiscoveryTcpQueue) != 0) {
	LOG(logERROR) << "tcp listen() on " <<  bindAddr << " failed! " << lastError();
	return false;
	}
	if(!socketSetBlocking(m_socAccept, false)) {
	return false;
	}


	LOG(logERROR) << "accepting tcp connections on " <<  bindAddr;

	*/


	/*
	while(true) {
	int remoteAddrLen = sizeof(remote);
	SOCKET newsockfd = accept(m_socAccept, (struct sockaddr *)&remote, &remoteAddrLen);
	if(newsockfd < 0)
	break;
	}
	*/
}


LLTcp::~LLTcp()
{
	if (m_socAccept != -1)
		close(m_socAccept);
}

#include <rtt.h>
#include "Discovery.h"

bool LLTcp::connect(const LinkEndpoint &end, bool isMaster)
{
	if (end.getFamily() != AF_INET6 &&end.getFamily() != AF_INET) {
		LOG(logERROR) << "Unknown address family (not IPv4 nor IPv6)!";
		return false;
	}

	LOG(logDEBUG1) << "setting up tcp link to " << end;


	if (!isMaster)
	{
		m_socAccept = socket(end.getFamily(), SOCK_STREAM, 0);

		// set SO_REUSEADDR on a socket to true (1):
		int optval = 1;
		if (setsockopt(m_socAccept, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof optval) != 0) {
			LOG(logERROR) << "tcp socket set SO_REUSEADDR failed! " << lastError();
			close(m_socAccept);
			return false;
		}


		auto bindAddr = Discovery::NodeDevice::localAny.getAddr(end.getPort());
		if (bind(m_socAccept, (struct sockaddr *) &bindAddr, sizeof(bindAddr)) < 0) {
			LOG(logERROR) << "tcp bind to " << bindAddr << " failed! " << lastError();
			close(m_socAccept);
			return false;
		}

		if (listen(m_socAccept, UlltraProto::DiscoveryTcpQueue) != 0) {
			LOG(logERROR) << "tcp listen() on " << bindAddr << " failed! " << lastError();
			close(m_socAccept);
			return false;
		}

		socketSetBlocking(m_socAccept, false);

		LOG(logDEBUG2) << "waiting for slave to connect ...";
		SocketAddress client;
		socklen_t len = sizeof(client);
		int clientSocket;
		while (1) {
			clientSocket = accept(m_socAccept, &client.sa, &len);

			if (clientSocket == 0)
				break;

			if (clientSocket > 0) {
				client.setPort(end.getPort()); // ignore the port of transfer
				if (client != end) {
					LOG(logERROR) << " invalid client " << client << " connected, expected " << end;
					close(clientSocket);
					continue;
				}

				break;
			}

			usleep(100);
		}

		// do not accept any more clients!!
		close(m_socAccept);
		m_socAccept = -1;

		LOG(logDEBUG) << "client connected " << client;
		m_soc = clientSocket;
	}
	else {
		m_soc = socket(end.getFamily(), SOCK_STREAM, 0);


		SocketAddress server = end;
		socklen_t len = sizeof(server);

		LOG(logDEBUG2) << "connecting to " << server;

		socketSetBlocking(m_soc, false);
		::connect(m_soc, &server.sa, len);
		int res = socketConnectTimeout(m_soc, 1000 * 4000);

		if (res == 0) { // timeout
			close(m_soc);
			return false;
		}

		if (res < 0) { // error
			close(m_soc);
			return false;
		}

		//socketSetBlocking(m_soc, true);


		LOG(logDEBUG3) << "connected to " << server;
	}


	int flag = 1;
	int result = setsockopt(m_soc, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(flag));
	if (result < 0) {
		LOG(logERROR) << "failed to enable TCP_NODELAY! " << lastError();
		close(m_socAccept);
		m_socAccept = -1;
		return false;
	}

	LOG(logDEBUG3) << "enabled TCP_NODELAY!";
	
	return true;
}


const uint8_t *LLTcp::receive(int &receivedBytes)
{
	int res = socketSelect(m_soc, 1000*40);
	if (res == 0) {
		return 0; // timeout
	}

	if (res < 0) {
		LOG(logERROR) << *this << "->receive failed! " << lastError();
		return 0;
	}

	receivedBytes = recv(m_soc, (char*)m_rxBuffer, sizeof(m_rxBuffer), 0);
	if (receivedBytes <= 0) {
		LOG(logERROR) << *this << "->receive failed: nothing recved after select! " << lastError();
		receivedBytes = -1;
		return 0;
	}

	return m_rxBuffer;
}

bool LLTcp::send(const uint8_t *data, int dataLength)
{
	return ::send(m_soc, (const char*)data, dataLength, 0) == dataLength;
}



void LLTcp::toString(std::ostream& stream) const
{
	stream << "tcp";
}


bool LLTcp::onBlockingTimeoutChanged(uint64_t timeoutUs)
{
	return true;
}