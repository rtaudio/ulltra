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



Discovery::NodeDevice Discovery::NodeDevice::none = Discovery::NodeDevice();

Discovery::Discovery()
    : m_receiver(0), m_broadcastPort(0)
{
	m_updateCounter = 0;
	m_lastBroadcast = 0;
}


Discovery::~Discovery()
{
	send("ULLTRA_DEV_Z");

	if (m_receiver)
		delete m_receiver;


	close(m_socBroadcast);
}

void Discovery::addExplicitHosts(const std::string &host)
{
    m_explicitNodes.push_back(NodeDevice(host));
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

	m_socBroadcast = soc;


	return true;
}

const Discovery::NodeDevice &Discovery::getNode(const addrinfo &addr) const
{
    if(addr.ai_addrlen == 0)
        return NodeDevice::none;

	int i = 0;
    for (auto &np : m_discovered) {
        auto &n(np.second);
        if(addr.ai_addrlen == n.addr.ai_addrlen && memcmp(&addr.ai_addr, &n.addr.ai_addr, addr.ai_addrlen) == 0) {
            return m_discovered.at(np.first);
		}
		i++;
	}

    return Discovery::NodeDevice::none;
}

bool Discovery::update(time_t now)
{
	bool sendNow = false;

    std::vector<const NodeDevice*> newNodes;

	while (true) {
		struct sockaddr_storage remote;
		int len;

		const uint8_t * recvBuffer = m_receiver->receive(len, remote);

		// would block? 
		if (len == -1 || !recvBuffer)
			break;	

		// just drop small packets
		if (len < 20 || recvBuffer[0] == 0)
			continue;

		std::string act((const char*)recvBuffer);

		
        NodeDevice nd(remote);
		nd.name = std::string((const char*)&recvBuffer[act.length() + 1]);
		nd.id = std::string((const char*)&recvBuffer[act.length() + nd.name.length() + 2]);
        nd.sinceVitalSign = 0;

		// check if we somehow received our own broadcast
		if (nd.id == getHwAddress()) {
			continue;
		}

		if ("ULLTRA_DEV_R" == act) { // register

            auto ex = m_discovered.find(nd.id);
			// already there?
            if (ex != m_discovered.end()) {
                (*ex).second.sinceVitalSign = 0;
				continue;
            }

            LOG(logINFO) << "Discovered node " << nd;
            m_discovered.insert(std::pair<std::string, NodeDevice>(nd.id, nd));
            send("ULLTRA_DEV_R", nd); // send a discovery message back
            sendNow = true;
            newNodes.push_back(&m_discovered.find(nd.id)->second);
		}
		else if ("ULLTRA_DEV_Z" == act) { // unregister
            auto it = m_discovered.find(nd.id);
            if(it != m_discovered.end()) {
                LOG(logINFO) << "Lost node " << nd;
                if (onNodeLost)
                    onNodeLost(it->second);
                m_discovered.erase(m_discovered.find(nd.id));
                sendNow = true;
            }
        }
		else if ("ULLTRA_DEV_H" == act) { // heartbeat
			LOG(logINFO) << "Heartbeat node " << nd;
			//ProcessHeartbeat(remote.sin_addr, ((HeartBeatData*)&recvBuffer[10]));
		}
		else if (m_customHandlers.find(act) != m_customHandlers.end() && m_customHandlers[act])
		{
			auto ex = m_discovered.find(nd.id);
            if (ex == m_discovered.end()) {
                 LOG(logDEBUG1) << "Received custom message " << act << " from unknown " << nd;
				continue;
            }			 

			exN.sinceVitalSign = 0;

			LOG(logINFO) << "Custom handler for message " << act << " from node " << nd;
			m_customHandlers[act](ex->second);
		}
		else {
            LOG(logDEBUG1) << "Received unknown message from " << nd;
		}
	}


	// auto-purge dead nodes
	for (auto it = m_discovered.begin(); it != m_discovered.end(); it++) {
        if(difftime(it->second.sinceVitalSign, 0) > (UlltraProto::BroadcastInterval*3)) {
            LOG(logINFO) << "Dead node " << it->second;
			m_discovered.erase(it);
			sendNow = true;
			break;
		}
		
		if(m_updateCounter > 0)
			it->second.sinceVitalSign += now - m_lastUpdateTime;
	}
	
	// broadcast a discovery packet every 10 updates or if something changed
	// e.g. if a new node appears make current node visible
	// this needs to be done beffore onNodeDiscovered() call!
	if (difftime(now, m_lastBroadcast) >= UlltraProto::BroadcastInterval || sendNow) {
		send("ULLTRA_DEV_R");
		m_lastBroadcast = now;
	}

	// delayed callback dispatch
    for (const NodeDevice *nd : newNodes) {
		if (onNodeDiscovered)
            onNodeDiscovered(*nd);
	}

	m_updateCounter++;
	m_lastUpdateTime = now;

	return true;
}



void  Discovery::send(const std::string &msg, const NodeDevice &node)
{
	bool isMulticast = !node.exists();
	SOCKET soc = (SOCKET)m_socBroadcast;

	char data[500];
	char *dp = &data[0];

	char hostname[32];
	gethostname(hostname, 31);


	// create 0-seperated string list
	strcpy(dp, msg.c_str()); dp += strlen(dp) + 1;
	strcpy(dp, hostname); dp += strlen(hostname) + 1;
	strcpy(dp, getHwAddress().c_str());  dp += getHwAddress().length() + 1;

	int dataLen = dp - &data[0];

	// can be a multicast address 
	auto &toAddr = node.getAddr(m_broadcastPort);
	int sent = sendto(soc, data, dataLen, 0, (struct sockaddr*)&toAddr, sizeof(toAddr));

	if (sent != dataLen) {
		LOG(logERROR) << "Could not send broadcast message! " << lastError();
	}


	// Windows broadcast fix (for ipv4)
	if (isMulticast) {
		char ac[200];
		if (gethostname(ac, sizeof(ac)) == -1)
			return;

		struct hostent *phe = gethostbyname(ac);
		if (phe == 0)
			return;

		struct sockaddr_in addr = {};
		addr.sin_family = AF_INET;

		for (int i = 0; phe->h_addr_list[i] != 0; ++i) {
			memcpy(&addr, phe->h_addr_list[i], sizeof(addr));
			addr.sin_addr.s_addr = addr.sin_addr.s_addr | (255) << (8 * 3);
			addr.sin_port = m_broadcastPort;

			std::string ip4(inet_ntoa(addr.sin_addr));
			std::cout << "broadcast message to " << ip4 << ":" << ntohs(addr.sin_port) << "!" << std::endl;

			sent = sendto(soc, data, dataLen, 0, (struct sockaddr*)&addr, sizeof(addr));
			if (sent != dataLen) {
				LOG(logERROR) << "Could not send broadcast message  to " << ip4 << "!" << std::endl;
			}
		}
	}

	// send to explicit nodes
	for (NodeDevice n : m_explicitNodes) {
		auto &s = n.getAddr(m_broadcastPort);
		sendto(soc, data, dataLen, 0, (struct sockaddr*)&s, sizeof(s));
	}
}



/* create from a hostname or address */
Discovery::NodeDevice::NodeDevice(const std::string &host) {
    initAddr();
    int r; addrinfo *a;
    if( (r = getaddrinfo(host.c_str(), NULL, &addr, &a)) < 0) {
        LOG(logERROR) << "getaddrinfo for host " << host << ": "<< gai_strerror(r);
        return;
    }
    addr = a[0];
    freeaddrinfo(a);
}

const addrinfo & Discovery::NodeDevice::getAddr(int port) const {
    if(portAddr.find(port) != portAddr.end())
        return portAddr[port];
    int r; addrinfo *a;
    if( (r = getaddrinfo(NULL, std::to_string(port).c_str(), &addr, &a)) < 0) {
        LOG(logERROR) << "getaddrinfo for node " << this << ":" << port << "failed: "<< gai_strerror(r);
        return none.addr;
    }
    portAddr[port] = a[0];
    freeaddrinfo(a);
    return portAddr[port];
}

std::ostream& operator<<(std::ostream &strm, const Discovery::NodeDevice &nd) {
    return strm << nd.name << " (id " << nd.id << ", host " << nd.addr.ai_canonname << ")";
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
