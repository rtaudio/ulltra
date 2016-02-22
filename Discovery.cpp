#include "Discovery.h"

#include <iostream>
#include <string>
#include <fstream>

#ifdef WIN32
#include <ws2tcpip.h>

#include <Windows.h>
#include <Iphlpapi.h>
#include <Assert.h>
#pragma comment(lib, "iphlpapi.lib")

#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <unistd.h>
#include <fcntl.h>

#endif



void print_last_errno(int err = 0);

Discovery::Discovery()
{
	m_updateCounter = 0;
}


Discovery::~Discovery()
{
	shutdown((SOCKET)m_recvSocket, 2);
	shutdown((SOCKET)m_soc, 2);

#ifndef WIN32
	close((SOCKET)m_recvSocket);
	close((SOCKET)m_soc);
#endif
}

bool Discovery::start(int broadcastPort)
{

	m_broadcastPort = broadcastPort;
	m_updateCounter = 0;

	// bind receiving socket
	m_recvSocket = (void*)socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in local;
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_port = htons(m_broadcastPort);
	local.sin_addr.s_addr = INADDR_ANY;
	int rv = bind((SOCKET)m_recvSocket, (struct sockaddr *) &local, sizeof(local));

	if (rv != 0) {
		printf("Failed to bind UDP receiving socket: ");
		print_last_errno();
		return false;
	}

#ifndef WIN32
	if (-1 == fcntl((SOCKET)m_recvSocket, F_GETFL, 0)) {
		printf("Cannot set non-blocking I/O!\n");
		return false;
	}
#else
	u_long iMode = 1; // non-blocking
	if (ioctlsocket((SOCKET)m_recvSocket, FIONBIO, &iMode) != NO_ERROR)
	{
		printf("Cannot set non-blocking I/O!\n");
		return false;
	}

#endif



	SOCKET soc = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (soc == -1) {
		printf("Error: could not create broadcast socket!\n");
		return false;
	}
#ifdef WIN32
	char yes = 1;
#else
	int yes = 1;
#endif
	// allow broadcast
	rv = setsockopt(soc, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
	if (rv == -1) {
		printf("Error: could not set broadcast socket options!\n");
		print_last_errno();
		return false;
	}

	m_soc = (void*)soc;


	return true;
}

const Discovery::NodeDevice &Discovery::findById(const std::string &id) const
{
	for (auto n : m_discovered) {
		if (n.id == id) {
			return n;
		}
	}
	return NodeDevice();
}

bool Discovery::update()
{
	while (true) {
		struct sockaddr_in remote;  // UDP Sender IPv4 Adress und Port Struktur
#ifndef WIN32
		unsigned
#endif
			char recvBuffer[1024];
		int addr_len = sizeof(remote);
		int len = recvfrom((SOCKET)m_recvSocket, (char*)recvBuffer, sizeof(recvBuffer), 0, (struct sockaddr *) &remote, &addr_len);
		
		// would block? 
		if (len == -1 || len > sizeof(recvBuffer))
			break;	

		if (len < 20)
			continue;


		recvBuffer[1000] = 0;

		NodeDevice nd;
		nd.name = std::string(&recvBuffer[strlen("ULLTRA_DEV_R") + 1]);
		nd.id = std::string(&recvBuffer[strlen("ULLTRA_DEV_R") + nd.name.length() + 2]);
		nd.addr = remote.sin_addr;

		if (strcmp("ULLTRA_DEV_R", recvBuffer) == 0) { // register
			// check if we somehow received our own broadcast
			if (nd.id ==  getHwAddress()) {
				continue;
			}

			// already there?
			if (findById(nd.id).exists()) {
				continue;
			}

			m_discovered.push_back(nd);
		}
		else if (strcmp("ULLTRA_DEV_Z", recvBuffer) == 0) { // unregister
			char client_name[32];
			strncpy_s(client_name, sizeof(client_name), &recvBuffer[18], 32);

			for (auto it = m_discovered.begin(); it != m_discovered.end(); it++) {
				if (it->id == nd.id || it->addr.s_addr == nd.addr.s_addr) {
					m_discovered.erase(it);
					break;
				}
			}
		}
		else if ((strcmp("ULLTRA_DEV_H", recvBuffer)) == 0) { // heartbeat

			//ProcessHeartbeat(remote.sin_addr, ((HeartBeatData*)&recvBuffer[10]));
		}
	}
	

	if(m_updateCounter % 4 == 0)
		sayIAmHere(true);

	m_updateCounter++;

	return true;
}


std::string Discovery::getHwAddress()
{
	static std::string addr;
	if (addr.length() > 0)
		return addr;

#ifndef WIN32
	std::ifstream infile("/sys/class/net/eth0/address");
	if (infile.good()) {
		std::getline(infile, addr);
	}
	else {
		infile = std::ifstream("/sys/class/net/wlan0/address");
		if (infile.good()) {
			std::getline(infile, addr);
		}
	}
#else	
	char *mac_addr = (char*)malloc(17);
	IP_ADAPTER_INFO AdapterInfo[16];	
	DWORD dwBufLen = sizeof(AdapterInfo);

	GetAdaptersInfo(AdapterInfo, &dwBufLen);

	if (GetAdaptersInfo(AdapterInfo, &dwBufLen) != NO_ERROR)
		return "";

	PIP_ADAPTER_INFO pAdapterInfo = AdapterInfo;

	do {
		sprintf(mac_addr, "%02X:%02X:%02X:%02X:%02X:%02X",
			pAdapterInfo->Address[0], pAdapterInfo->Address[1],
			pAdapterInfo->Address[2], pAdapterInfo->Address[3],
			pAdapterInfo->Address[4], pAdapterInfo->Address[5]);
		//printf("Address: %s, mac: %s\n", pAdapterInfo->IpAddressList.IpAddress.String, mac_addr);
		addr = std::string(mac_addr);
		break;
		pAdapterInfo = pAdapterInfo->Next;
	} while (pAdapterInfo);
#endif

	return addr;
}

void Discovery::sayIAmHere(bool notGoodby)
{
	SOCKET soc = (SOCKET)m_soc;

	char data[500];

	char hostname[32];
	gethostname(hostname, 31);

	auto hwAddress = getHwAddress();

	char *dp = &data[0];

	// create 0-seperated string list
	strcpy(dp, notGoodby ? "ULLTRA_DEV_R" : "ULLTRA_DEV_Z"); dp += strlen(dp) + 1;
	strcpy(dp, hostname); dp += strlen(hostname) + 1;
	strcpy(dp, hwAddress.c_str());  dp += hwAddress.length() + 1;

	int dataLen = dp - &data[0];

	struct sockaddr_in s;
	memset(&s, 0, sizeof(struct sockaddr_in));
	s.sin_family = AF_INET;
	s.sin_port = htons(m_broadcastPort);
	s.sin_addr.s_addr = htonl(INADDR_BROADCAST);

	int sent = sendto(soc, data, dataLen, 0, (struct sockaddr*)&s, sizeof(struct sockaddr_in));

	if (sent != dataLen) {
		printf("Could not send broadcast message!\n");
	}

#ifdef WIN32
	// win32 broadcast fix
	// use this on linux too?
	char ac[200];
	if (gethostname(ac, sizeof(ac)) == SOCKET_ERROR)
		return;

	struct hostent *phe = gethostbyname(ac);
	if (phe == 0)
		return;

	for (int i = 0; phe->h_addr_list[i] != 0; ++i) {
		struct in_addr addr;
		memcpy(&addr, phe->h_addr_list[i], sizeof(struct in_addr));
		addr.s_addr = addr.s_addr | (255) << (8 * 3);

		s.sin_addr.s_addr = addr.s_addr | (255) << (8 * 3);

		sent = sendto(soc, data, dataLen, 0, (struct sockaddr*)&s, sizeof(struct sockaddr_in));
		if (sent != dataLen) {
			printf("Could not send broadcast message!\n");
		}
	}
#else
	inet_aton("10.0.0.255", &s.sin_addr);
	sendto(soc, data, 16 + 32, 0, (struct sockaddr*)&s, sizeof(struct sockaddr_in));
#endif
}

void print_last_errno(int err)
{
	switch (err ? err : errno) {
	case EADDRINUSE: printf("EADDRINUSE\n"); break;
	case EADDRNOTAVAIL: printf("EADDRNOTAVAIL\n"); break;
	case EBADF: printf("EBADF\n"); break;
	case ECONNABORTED: printf("ECONNABORTED\n"); break;
	case EINVAL: printf("EINVAL\n"); break;
	case EIO: printf("EIO\n"); break;
	case ENOBUFS: printf("ENOBUFS\n"); break;
	case ENOPROTOOPT: printf("ENOPROTOOPT\n"); break;
	case ENOTCONN: printf("ENOTCONN\n"); break;
	case ENOTSOCK: printf("ENOTSOCK\n"); break;
	case EPERM: printf("EPERM\n"); break;
#ifdef ETOOMANYREFS
	case ETOOMANYREFS: printf("ETOOMANYREFS\n"); break;
#endif
#ifdef EUNATCH
	case EUNATCH: printf("EUNATCH\n"); break;
#endif
	default: printf("UNKNOWN\n"); break;
	}
}