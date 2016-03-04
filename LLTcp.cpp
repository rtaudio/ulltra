#include "LLTcp.h"

#ifndef _WIN32
#include <netinet/tcp.h>
#endif


LLTcp::LLTcp()
	: LLCustomBlock(-1)
{
	m_soc = -1;
	m_socAccept = -1;

	static int init = 1;


#ifndef _WIN32
	if (init) {
		init = 0;
		pclose(popen("sysctl -w net.ipv4.tcp_window_scaling=0", "r"));
		pclose(popen("sysctl -w net.ipv4.tcp_low_latency=1", "r"));
		pclose(popen("sysctl -w net.ipv4.tcp_sack=0", "r"));
		pclose(popen("sysctl -w net.ipv4.tcp_timestamps=0", "r"));
		pclose(popen("sysctl -w net.ipv4.tcp_fastopen=1", "r"));
		pclose(popen("sysctl -w net.core.rmem_default=10000000", "r"));
		pclose(popen("sysctl -w net.core.wmem_default=10000000", "r"));
		pclose(popen("sysctl -w net.core.rmem_max=16777216", "r"));
		pclose(popen("sysctl -w net.core.wmem_max=16777216", "r"));
		//ernel.sched_wakeup_granularity_ns=2000000
	}
#endif
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

	m_recvTimeoutUs = isMaster ? 20000 : 60000;

	if (isMaster)
	{
		m_socAccept = socket(end.getFamily(), SOCK_STREAM, 0);

		// set SO_REUSEADDR on a socket to true (1):
		int optval = 1;
		if (setsockopt(m_socAccept, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof optval) != 0) {
			LOG(logERROR) << "tcp socket set SO_REUSEADDR failed! " << lastError();
			close(m_socAccept); m_socAccept = -1;
			return false;
		}


		auto bindAddr = Discovery::NodeDevice::localAny.getAddr(end.getPort());
		if (bind(m_socAccept, (struct sockaddr *) &bindAddr, sizeof(bindAddr)) < 0) {
			LOG(logERROR) << "tcp bind to " << bindAddr << " failed! " << lastError();
			close(m_socAccept); m_socAccept = -1;
			return false;
		}

		if (listen(m_socAccept, UlltraProto::DiscoveryTcpQueue) != 0) {
			LOG(logERROR) << "tcp listen() on " << bindAddr << " failed! " << lastError();
			close(m_socAccept); m_socAccept = -1;
			return false;
		}

		socketSetBlocking(m_socAccept, false);

		LOG(logDEBUG2) << "waiting for slave to connect ...";
		SocketAddress client;
		socklen_t len = sizeof(client);
		int clientSocket;

		auto t0 = UP::getMicroSecondsCoarse();

		while (1) {
			clientSocket = accept(m_socAccept, &client.sa, &len);

			if (clientSocket == 0)
				break;

			if (clientSocket > 0) {
				client.setPort(end.getPort()); // ignore the port of transfer
				if (client != end) {
					LOG(logERROR) << " invalid client " << client << " connected, expected " << end;
					close(clientSocket); clientSocket = -1;
					continue;
				}

				break;
			}

			if ((UP::getMicroSecondsCoarse() - t0) > (UP::TcpConnectTimeout*1000)) {
				LOG(logERROR) << " accept() timed out after  " << UP::TcpConnectTimeout << " ms waiting for " << end;
				close(m_socAccept); m_socAccept = -1;
				return false;
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

		//socketSetBlocking(m_soc, true);
		int res;

		for (int i = 0; i < 10; i++) {
			res = ::connect(m_soc, &server.sa, len);
			/*res = socketConnectTimeout(m_soc, 1000 * UP::TcpConnectTimeout);
			if (res == 0) { // timeout
				LOG(logERROR) << " connect() timed out after  " << UP::TcpConnectTimeout << " ms waiting for " << end;
				close(m_soc); m_soc = -1;
				return false;
			}*/


			if (res == 0)
				break;

			LOG(logDEBUG3) << "Connection failed, retry...";
			usleep(10000);
		}

		if (res < 0) { // error
			close(m_soc); m_soc = -1;
			return false;
		}

		//socketSetBlocking(m_soc, true);


		LOG(logDEBUG3) << "connected ("<<res<<") to server " << server;
	}

#if _WIN32
	DWORD  flagYes = 1;
#else
	int flagYes = 1;
#endif

	int result = setsockopt(m_soc, IPPROTO_TCP, TCP_NODELAY, (char *)&flagYes, sizeof(flagYes));
	if (result < 0) {
		LOG(logERROR) << "failed to enable TCP_NODELAY! " << lastError();
		close(m_socAccept);
		m_socAccept = -1;
		return false;
	}

	LOG(logDEBUG3) << "enabled TCP_NODELAY!";
	m_desc += ",nodelay";

#ifdef TCP_EXPEDITED_1122
	result = setsockopt(m_soc, IPPROTO_TCP, TCP_EXPEDITED_1122, (char *)&flagYes, sizeof(flagYes));
	if (result < 0) {
		LOG(logERROR) << "failed to enable TCP_EXPEDITED_1122! " << lastError();
		close(m_socAccept);
		m_socAccept = -1;
		return false;
	}
	LOG(logDEBUG3) << "enabled TCP_EXPEDITED_1122!";
	m_desc += ",exp1122";
#endif





	if(enableHighQoS(m_soc))
		m_desc += ",qos";
	else
		m_desc += ",NOqos";

	
	// set the buffer size, is this necessary?
	int n = 256; // 1024 * 8;
	if (setsockopt(m_soc, SOL_SOCKET, SO_RCVBUF, (const char *)&n, sizeof(n)) == -1
		|| setsockopt(m_soc, SOL_SOCKET, SO_SNDBUF, (const char *)&n, sizeof(n)) == -1) {
		LOG(logERROR) << ("Cannot set rcvbuf size!");
		return false;
	}
	else {
		LOG(logDEBUG) << "Set rcvbuf size to " << n;

		m_desc += ",bufs=" + std::to_string(n);
	}

	return true;
}


const uint8_t *LLTcp::receive(int &receivedBytes)
{
	int res = socketSelect(m_soc, m_recvTimeoutUs);
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
	int res = ::send(m_soc, (const char*)data, dataLength, 0);
	if (res == dataLength)
		return true;
	LOG(logDEBUG) << "TCP send failed: " << res << "" << lastError();
	return false;
}



void LLTcp::toString(std::ostream& stream) const
{
	stream << "tcp(" + m_desc + ")";
}


bool LLTcp::onBlockingTimeoutChanged(uint64_t timeoutUs)
{
	return true;
}