#include <iostream>
#include <string>
#include <cstring>
#include <fstream>
#include <map>

#include "networking.h"

#include "UlltraProto.h"
#include "Discovery.h"
#include "SimpleUdpReceiver.h"

#ifndef _WIN32
#include <dirent.h>
#endif

#include <rtt.h>


std::ostream& operator<<(std::ostream &strm, const Discovery::NodeDevice &nd) {
	std::string ip4(inet_ntoa(nd.addr));
	return strm << nd.name << " (id " << nd.id << ", ip4 address " << ip4 << ")";
}


Discovery::NodeDevice Discovery::NodeDevice::none = Discovery::NodeDevice();

Discovery::Discovery() : m_receiver(0), m_broadcastPort(0)
{
	m_updateCounter = 0;
	m_lastBroadcast = 0;
}


Discovery::~Discovery()
{
	send("ULLTRA_DEV_Z");

	if (m_receiver)
		delete m_receiver;

	shutdown((SOCKET)m_soc, 2);
#ifndef _WIN32
	close((SOCKET)m_soc);
#endif
}

bool Discovery::start(int broadcastPort)
{
	if (m_broadcastPort)
		return false;

	auto hwid = getHwAddress();
	if (hwid.empty()) {
		LOG(logERROR) << "Not hardware id available!";
		return false;
	}

	LOG(logDEBUG) << "HWID: " << hwid;

	m_broadcastPort = broadcastPort;
	m_updateCounter = 0;

	m_receiver = SimpleUdpReceiver::create(broadcastPort, true);
	if (!m_receiver) {
		LOG(logERROR) << ("Error: could not create receiver socket!") << lastError();
		return false;
	}

	SOCKET soc = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (soc == -1) {
		LOG(logERROR) << ("Error: could not create broadcast socket!") << lastError();
		return false;
	}
#ifdef _WIN32
	char yes = 1;
#else
	int yes = 1;
#endif
	// allow broadcast
	int rv = setsockopt(soc, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
	if (rv == -1) {
		LOG(logERROR) << ("Error: could not set broadcast socket options!") << lastError();
		return false;
	}

	m_soc = soc;


	return true;
}

const Discovery::NodeDevice &Discovery::getNode(const std::string &id) const
{
	int i = 0;
	for (auto &n : m_discovered) {
		if (n.id == id) {
			return m_discovered[i];
		}
		i++;
	}
	return NodeDevice::none;
}



Discovery::NodeDevice &Discovery::getNode(const std::string &id)
{
	int i = 0;
	for (auto &n : m_discovered) {
		if (n.id == id) {
			return m_discovered[i];
		}
		i++;
	}
	return NodeDevice::none;
}

const Discovery::NodeDevice &Discovery::getNode(const in_addr &addr) const
{
	int i = 0;
	for (auto &n : m_discovered) {
		if (n.addr.s_addr == addr.s_addr) {
			return m_discovered[i];
		}
		i++;
	}
	return NodeDevice::none;
}

bool Discovery::update(time_t now)
{
	bool sendNow = false;

	std::map<std::string, NodeDevice> newlyDiscovered;

	while (true) {
		struct sockaddr_in remote;
		int len;

		const uint8_t * recvBuffer = m_receiver->receive(len, remote);

		// would block? 
		if (len == -1 || !recvBuffer)
			break;	

		if (len < 20)
			continue;


		std::string act((const char*)recvBuffer);
		
		NodeDevice nd;
		nd.name = std::string((const char*)&recvBuffer[act.length() + 1]);
		nd.id = std::string((const char*)&recvBuffer[act.length() + nd.name.length() + 2]);
		nd.addr = remote.sin_addr;

		// check if we somehow received our own broadcast
		if (nd.id == getHwAddress()) {
			continue;
		}

		if ("ULLTRA_DEV_R" == act) { // register
			auto &exN = getNode(nd.id);
			// already there?
			if (exN.exists()) {
				exN.vitalSign = now;
				continue;
			}

			nd.vitalSign = now;

			if (newlyDiscovered.find(nd.id) == newlyDiscovered.end()) {
				newlyDiscovered[nd.id] = nd;
				LOG(logINFO) << "Discovered node " << nd;

				send("ULLTRA_DEV_R", nd);

				sendNow = true;
			}
		}
		else if ("ULLTRA_DEV_Z" == act) { // unregister
			for (auto it = m_discovered.begin(); it != m_discovered.end(); it++) {
				if (it->id == nd.id || it->addr.s_addr == nd.addr.s_addr) {
										
					{ LOG(logINFO) << "Lost node " << nd; }
					sendNow = true;

					if (onNodeLost)
						onNodeLost(*it);

					m_discovered.erase(it);
					break;
				}
			}
		}
		else if ("ULLTRA_DEV_H" == act) { // heartbeat
			LOG(logINFO) << "Heartbeat node " << nd;
			//ProcessHeartbeat(remote.sin_addr, ((HeartBeatData*)&recvBuffer[10]));
		}
		else if (m_customHandlers.find(act) != m_customHandlers.end() && m_customHandlers[act])
		{
			// only accept custom messages from known nodes
			auto &exN = getNode(nd.id);
			if (!exN.exists() && newlyDiscovered.find(nd.id) == newlyDiscovered.end()) {
				{ LOG(logDEBUG1) << "Received custom message " << act << " from unknown " << nd; }
				continue;
			}

			exN.vitalSign = now;

			{ LOG(logINFO) << "Custom handler for message " << act << " from node " << nd; }
			m_customHandlers[act](nd);

			exN.vitalSign = UP::getMicroSeconds();
		}
		else {
			{ LOG(logDEBUG1) << "Received unknown message from " << nd; }
		}
	}


	// auto-purge dead nodes
	for (auto it = m_discovered.begin(); it != m_discovered.end(); it++) {
		if(difftime(now, it->vitalSign) > (UlltraProto::BroadcastInterval*3)) {
			LOG(logINFO) << "Dead node " << (*it);
			m_discovered.erase(it);
			sendNow = true;
			break;
		}
	}
	
	// broadcast a discovery packet every 10 updates or if something changed
	// e.g. if a new node appears make current node visible
	// this needs to be done beffore onNodeDiscovered() call!
	if (difftime(now, m_lastBroadcast) >= UlltraProto::BroadcastInterval || sendNow) {
		usleep(1000 * 100);
		send("ULLTRA_DEV_R");
		m_lastBroadcast = now;
		usleep(1000 * 100);
	}

	for (auto &nd : newlyDiscovered) {
		if (onNodeDiscovered)
			onNodeDiscovered(nd.second);
		m_discovered.push_back(nd.second);
	}

	m_updateCounter++;

	return true;
}



std::string Discovery::getHwAddress()
{
	static std::string addr;
	if (addr.length() > 0)
		return addr;

#ifndef _WIN32

	std::ifstream infile("/sys/class/net/et*/address");
	if (infile.good()) {
		std::getline(infile, addr);
		return addr;
	}

	infile = std::ifstream("/sys/class/net/wl*/address");
	if (infile.good()) {
		std::getline(infile, addr);
		return addr;
	}


	DIR *dir;
	struct dirent *ent;
	if (!(dir = opendir("/sys/class/net/")))
		return "";

	
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.' || strlen(ent->d_name) < 4)
			continue;
		printf("%s\n", ent->d_name);
		if ((ent->d_name[0] == 'w' && ent->d_name[1] == 'l') || ent->d_name[0] == 'e') {
			infile = std::ifstream("/sys/class/net/" + std::string(ent->d_name )+"/address");
			if (infile.good()) {
				std::getline(infile, addr);
				closedir(dir);
				LOG(logDEBUG) << "Using " << ent->d_name << "'s address";
				return addr;
			}
		}		
	}
	closedir(dir);

#else	
	char *mac_addr = (char*)malloc(17);
	IP_ADAPTER_INFO AdapterInfo[16];	
	DWORD dwBufLen = sizeof(AdapterInfo);

	GetAdaptersInfo(AdapterInfo, &dwBufLen);

	if (GetAdaptersInfo(AdapterInfo, &dwBufLen) != NO_ERROR) {
		LOG(logERROR) << "GetAdaptersInfo failed!";
		return "";
	}

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

void  Discovery::send(const std::string &msg, const NodeDevice &node)
{
	SOCKET soc = (SOCKET)m_soc;

	char data[500];

	char hostname[32];
	gethostname(hostname, 31);

	auto hwAddress = getHwAddress();

	char *dp = &data[0];



	// create 0-seperated string list
	strcpy(dp, msg.c_str()); dp += strlen(dp) + 1;
	strcpy(dp, hostname); dp += strlen(hostname) + 1;
	strcpy(dp, hwAddress.c_str());  dp += hwAddress.length() + 1;

	int dataLen = dp - &data[0];

	struct sockaddr_in s;
	memset(&s, 0, sizeof(struct sockaddr_in));
	s.sin_family = AF_INET;
	s.sin_port = htons(m_broadcastPort);
	s.sin_addr.s_addr = node.exists() ? node.addr.s_addr : htonl(INADDR_BROADCAST);

	int sent = sendto(soc, data, dataLen, 0, (struct sockaddr*)&s, sizeof(struct sockaddr_in));

	if (sent != dataLen) {
		LOG(logERROR) << "Could not send broadcast message!";
	}

	//#ifdef _WIN32
	// win32 broadcast fix
	// use this on linux too?
	char ac[200];
	if (gethostname(ac, sizeof(ac)) == -1)
		return;

	struct hostent *phe = gethostbyname(ac);
	if (phe == 0)
		return;

	for (int i = 0; phe->h_addr_list[i] != 0; ++i) {
		struct in_addr addr;
		memcpy(&addr, phe->h_addr_list[i], sizeof(struct in_addr));
		addr.s_addr = addr.s_addr | (255) << (8 * 3);
		s.sin_addr.s_addr = addr.s_addr | (255) << (8 * 3);

		std::string ip4(inet_ntoa(s.sin_addr));
		//std::cout << "broadcast message to " << ip4 << ":" << ntohs(s.sin_port) << "!" << std::endl;

		sent = sendto(soc, data, dataLen, 0, (struct sockaddr*)&s, sizeof(struct sockaddr_in));
		if (sent != dataLen) {
			LOG(logERROR) << "Could not send broadcast message  to " << ip4 << "!" << std::endl;
		}
	}
	//#else
	//	inet_aton("10.0.0.255", &s.sin_addr);
	//	sendto(soc, data, 16 + 32, 0, (struct sockaddr*)&s, sizeof(struct sockaddr_in));
	//#endif
}

