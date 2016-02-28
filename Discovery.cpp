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



// multicast utils
int
get_multicast_addrs(const char *hostname,
	const char *service,
	int         socktype,
struct sockaddr_storage *addrToBind,
struct sockaddr_storage *addrToSend);

int isMulticast(struct sockaddr_storage *addr);
int joinGroup(int sockfd, int loopBack, int mcastHop, struct sockaddr_storage *addr);

Discovery::NodeDevice Discovery::NodeDevice::none(-1);
Discovery::NodeDevice Discovery::NodeDevice::localAny(-1);
Discovery::NodeDevice Discovery::NodeDevice::local4(-1);
Discovery::NodeDevice Discovery::NodeDevice::local6(-1);

Discovery::Discovery()
    : m_updateCounter(0), m_receiver(0), m_broadcastPort(0),
	m_socMulticast(-1), m_socBroadcast4(-1)
{
	m_lastBroadcast = 0;
	m_lastUpdateTime = 0;
}


Discovery::~Discovery()
{
	send("ULLTRA_DEV_Z");

	if (m_receiver)
		delete m_receiver;

	if(m_socBroadcast4 != -1)
		close(m_socBroadcast4);

	if (m_socMulticast != -1)
		close(m_socMulticast);
}

void Discovery::addExplicitHosts(const std::string &host)
{
    m_explicitNodes.push_back(NodeDevice(host));
}


bool Discovery::start(int broadcastPort)
{
	if (m_broadcastPort)
		return false;


	NodeDevice::localAny = NodeDevice(0);
	NodeDevice::local4 = NodeDevice(AF_INET);
	NodeDevice::local6 = NodeDevice(AF_INET6);

	m_broadcastPort = broadcastPort;
	m_updateCounter = 0;

	auto hwid = getHwAddress();
	if (hwid.empty()) {
		LOG(logERROR) << "Not hardware id available!";
		return false;
	}
	LOG(logDEBUG) << "HWID: " << hwid;


    initMulticast(broadcastPort+1);


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

	m_socBroadcast4 = soc;


    m_socAccept = socket(NodeDevice::local6.addrStorage.ss_family, SOCK_STREAM, 0);

    auto bindAddr = NodeDevice::local6.getAddr(broadcastPort+3);
    if (bind(m_socAccept, (struct sockaddr *) &bindAddr, sizeof(bindAddr)) < 0) {
         LOG(logERROR) << "tcp bind to " <<  bindAddr << " failed!";
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

	return true;
}

const Discovery::NodeDevice &Discovery::getNode(const sockaddr_storage &addr) const
{
    if(addr.ss_family == 0)
        return NodeDevice::none;

    for (auto &np : m_discovered) {
        auto &n(np.second);
        if(addr.ss_family != n.addrStorage.ss_family)
            continue;

        if(addr.ss_family == AF_INET) {
            if(((sockaddr_in*)&addr)->sin_addr.s_addr == ((sockaddr_in*)&n.addrStorage)->sin_addr.s_addr)
                return m_discovered.at(np.first);
        }
        else if(addr.ss_family == AF_INET6) {
            if(memcmp( &((struct sockaddr_in6 *)&addr)->sin6_addr,&((struct sockaddr_in6 *)&n.addrStorage)->sin6_addr,
                               sizeof(struct in6_addr)) == 0)
                return m_discovered.at(np.first);
        }
	}

    return NodeDevice::none;
}

void Discovery::tryConnectExplictHosts()
{
    for(const NodeDevice &n : m_explicitNodes) {

		/*
        auto a = n.getAddr(UlltraProto::DiscoveryPort);

        // initiate a non-blocking connection attempt
        // if its succeeds within a short time, instantly set
        // it up, otherwise postpone to next update
        connect(m_socConnect, &a, sizeof(a));
        SOCKET s = socketSelectTimeout(m_socConnect, 50);
        if(s != -1) {
            auto t = [s](){
                char buf[1000];
                int rlen;

                read(s, buf, sizeof(buf)-1);
                rlen = socketSelectTimeout(s, 2000);


            };
            RttThread();
        }
		*/
    }
}

bool Discovery::update(time_t now)
{
	bool sendNow = false;

    std::vector<const NodeDevice*> newNodes;

	struct sockaddr_storage remote = {};

	while (true) {

		int len;
		socklen_t remoteAddrLen = sizeof(remote);

        //const uint8_t * recvBuffer = m_receiver->receive(len, remote);
        //if (len == -1 || !recvBuffer)
//			break;


		char recvBuffer[1024*2];
		memset(recvBuffer, 0, sizeof(recvBuffer));
		len = recvfrom(m_socMulticast, recvBuffer, sizeof(recvBuffer)-1, 0, (struct sockaddr *)&remote,
			&remoteAddrLen);

		if (len == -1)
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

			ex->second.sinceVitalSign = 0;

			LOG(logINFO) << "Custom handler for message " << act << " from node " << nd;
			m_customHandlers[act](ex->second);
		}
		else {
            LOG(logDEBUG1) << "Received unknown message from " << nd;
		}
	}

	/*
    while(true) {
        int remoteAddrLen = sizeof(remote);
        SOCKET newsockfd = accept(m_socAccept, (struct sockaddr *)&remote, &remoteAddrLen);
        if(newsockfd < 0)
            break;
    }
	*/

	// auto-purge dead nodes
	for (auto it = m_discovered.begin(); it != m_discovered.end(); it++) {
		const NodeDevice &n = it->second;
        if(difftime(n.sinceVitalSign, 0) > (UlltraProto::BroadcastInterval*3)) {
            LOG(logINFO) << "Dead node " << n;
			m_discovered.erase(it);
			sendNow = true;
			break;
		}
		
		if(m_updateCounter > 0)
			it->second.sinceVitalSign += difftime(now, m_lastUpdateTime);
	}
	
	// broadcast a discovery packet every 10 updates or if something changed
	// e.g. if a new node appears make current node visible
	// this needs to be done beffore onNodeDiscovered() call!
	if (difftime(now, m_lastBroadcast) >= UlltraProto::BroadcastInterval || sendNow) {
		send("ULLTRA_DEV_R");
        tryConnectExplictHosts();
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



bool  Discovery::send(const std::string &msg, const NodeDevice &node)
{
	bool isMulticast = !node.exists();

	char data[500];
	char *dp = &data[0];

	char hostname[32];
	gethostname(hostname, 31);

	// create 0-seperated string list
	strcpy(dp, msg.c_str()); dp += strlen(dp) + 1;
	strcpy(dp, hostname); dp += strlen(hostname) + 1;
	strcpy(dp, getHwAddress().c_str());  dp += getHwAddress().length() + 1;

	int dataLen = dp - &data[0];

	
	if (m_socBroadcast4 != -1) {
		if (!isMulticast) {
			auto na = node.getAddr(m_broadcastPort);
			if (na.ss_family == AF_INET) {
				if (sendto(m_socBroadcast4, data, dataLen, 0, (struct sockaddr*)&na, sizeof(na)) != dataLen) {
					LOG(logERROR) << "Could not send broadcast message to " << node << lastError();
					return false;
				}
			}
		}
		else
		{
			// broadcast
			struct sockaddr_in addr4;
			memset(&addr4, 0, sizeof(addr4));
			addr4.sin_family = AF_INET;
			addr4.sin_port = htons(m_broadcastPort);
			addr4.sin_addr.s_addr = htonl(INADDR_BROADCAST);

			if (sendto(m_socBroadcast4, data, dataLen, 0, (struct sockaddr*)&addr4, sizeof(addr4)) != dataLen) {
				LOG(logERROR) << "Could not send broadcast message on INADDR_BROADCAST! " << lastError();
				return false;
			}

			// Windows broadcast fix (for ipv4)
			char ac[200];
			if (gethostname(ac, sizeof(ac)) == -1)
				return false;

			struct hostent *phe = gethostbyname(ac);
			if (phe == 0)
				return false;

			for (int i = 0; phe->h_addr_list[i] != 0; ++i) {
				struct in_addr daddr;
				memcpy(&daddr, phe->h_addr_list[i], sizeof(daddr));
				addr4.sin_addr.s_addr = daddr.s_addr | (255) << (8 * 3);

				std::string ip4(inet_ntoa(addr4.sin_addr));
				//std::cout << "broadcast message to " << ip4 << ":" << ntohs(addr4.sin_port) << "" << std::endl;

				if (sendto(m_socBroadcast4, data, dataLen, 0, (struct sockaddr*)&addr4, sizeof(addr4)) != dataLen) {
					LOG(logERROR) << "Could not send broadcast message  to " << ip4 << "!" << std::endl;
				}
			}
		}
	}


	if (isMulticast)
	{
		if (m_socMulticast != -1) {
			if (sendto(m_socMulticast, data, dataLen, 0,
				(struct sockaddr *)&m_multicastAddrSend, sizeof(m_multicastAddrSend))
				!= dataLen) {
				LOG(logERROR) << "sending multicast message failed! " << lastError();
				return false;
			}
		}
		

	}

/*
	// send to explicit nodes
	for (NodeDevice n : m_explicitNodes) {
		auto s = n.getAddr(m_broadcastPort);
		if (!s) {
			LOG(logERROR) << "Failed to get address for node " << n;
			continue;
		}
		sendto(m_socBroadcast4, data, dataLen, 0, (struct sockaddr*)s, sizeof(*s));
	}
	*/
}



/* create from a hostname or address */
Discovery::NodeDevice::NodeDevice(const std::string &host) {
    int r;

    name = host;
    id = "?";
    memset(&addrStorage, 0, sizeof(addrStorage));


	struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_CANONNAME;

    if( (r = getaddrinfo(host.c_str(), NULL, &hints, &res)) < 0) {
        LOG(logERROR) << "getaddrinfo for host " << host << ": "<< gai_strerror(r);
        return;
    }

    if(res->ai_canonname)
        name += " " + std::string(res->ai_canonname);

    memcpy(&addrStorage, res->ai_addr, res->ai_addrlen);

    freeaddrinfo(res);
}

Discovery::NodeDevice::NodeDevice(const sockaddr_storage &s)
{
    addrStorage = s;
}

const struct sockaddr_storage Discovery::NodeDevice::getAddr(int port) const {
    struct sockaddr_storage s = addrStorage;

	if (s.ss_family == AF_INET6) {		
		((struct sockaddr_in6 *)&s)->sin6_port = htons(port);
	}
	else if(s.ss_family == AF_INET) {
		((struct sockaddr_in *)&s)->sin_port = htons(port);
	}
	return s;
}


std::ostream& operator<<(std::ostream &strm, const Discovery::NodeDevice &nd) {
    return strm << nd.name << " (id " << nd.id << ", host " << nd.addrStorage << ")";
}

std::ostream& operator<<(std::ostream &strm, const Discovery::NodeDevice *nd) {
	return strm << *nd;
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



bool Discovery::initMulticast(int port)
{
    const char *mcastaddr = "ff08::1"; // "FF01:0:0:0:0:0:0:1"; // all nodes // "FF01::1111";
	//const char *mcastaddr = "FF01:0:0:0:0:0:0:1";

	memset(&m_multicastAddrBind, 0, sizeof(m_multicastAddrBind));
	memset(&m_multicastAddrSend, 0, sizeof(m_multicastAddrSend));


    if (get_multicast_addrs(mcastaddr, std::to_string(port).c_str(), SOCK_DGRAM, &m_multicastAddrBind, &m_multicastAddrSend) <0)
	{
		LOG(logERROR) << "get_addr error:: could not find multicast "
			<< "address=[" << mcastaddr << "] port=[" << port << " ]";
		return false;
	}
	

	if (!isMulticast(&m_multicastAddrSend)) {
		LOG(logERROR) << "This address does not seem a multicast address " << mcastaddr;
		return false;
	}

	m_socMulticast = socket(m_multicastAddrBind.ss_family, SOCK_DGRAM, 0);

    if (!socketSetBlocking(m_socMulticast, false))
        return false;


	if (bind(m_socMulticast, (struct sockaddr *)&m_multicastAddrBind, sizeof(m_multicastAddrBind)) < 0) {
		LOG(logERROR) << "bind error on port " << port;
		return false;
	}

	LOG(logDEBUG) << "bound multicast socket to " << mcastaddr << " on port " << port;

	if (joinGroup(m_socMulticast, 0, 8, &m_multicastAddrSend) <0) {
		LOG(logERROR) << "failed to join multicast group!";
		return false;
	}

}


int
get_multicast_addrs(const char *hostname,
	const char *service,
	int         socktype,
struct sockaddr_storage *addrToBind,
struct sockaddr_storage *addrToSend)
{
	struct addrinfo hints, *res, *ressave;
	int n, sockfd, retval;

	retval = -1;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = socktype;


	// see https://msdn.microsoft.com/en-us/library/ms737550(v=vs.85).aspx ('For multicast operations...')
	// on windows bind to local wildcard, then join the multicast group and send to multicast addrress
	hints.ai_flags = AI_PASSIVE;
	n = getaddrinfo(NULL, service, &hints, &res);


	if (n != 0) {
		fprintf(stderr,
			"getaddrinfo error:: [%s]\n",
			gai_strerror(n));
		return retval;
	}

	ressave = res;

	sockfd = -1;
	while (res) {
		LOG(logDEBUG) << "trying to bind to " << res->ai_addr;
		sockfd = socket(res->ai_family,
			res->ai_socktype,
			res->ai_protocol);

		if (!(sockfd < 0)) {
			int br = bind(sockfd, res->ai_addr, res->ai_addrlen);
			close(sockfd); sockfd = -1;

			if ((br) == 0) {
				memcpy(addrToBind, res->ai_addr, sizeof(*addrToBind));
				// see https://msdn.microsoft.com/en-us/library/ms737550(v=vs.85).aspx ('For multicast operations...')
				// on windows bind to local wildcard, then join the multicast group and send to multicast addrress
				struct addrinfo hints2, *res2;
				memset(&hints2, 0, sizeof(struct addrinfo));
				hints2.ai_family = res->ai_family;
				hints2.ai_socktype = res->ai_socktype;
				//hints2.ai_protocol = res->ai_protocol;
				if ((n = getaddrinfo(hostname, service, &hints2, &res2)) == 0) {
					memcpy(addrToSend, res2->ai_addr, sizeof(*addrToSend));
					freeaddrinfo(res2);
					retval = 0;
					break;
				}
				else {
					LOG(logDEBUG) << "getaddrinfo failed for " << hostname <<".  " << gai_strerror(n);
					//continue;
					// fallback to bind addr
					//memcpy(addrToSend, res->ai_addr, sizeof(*addrToSend));
				}
			}
			else {
				LOG(logDEBUG) << "bind failed: " << lastError() << " return val = " << br << " addrlen = " << res->ai_addrlen;
			}
		}
		res = res->ai_next;
	}

	freeaddrinfo(ressave);

	return retval;
}

int
joinGroup(int sockfd, int loopBack, int mcastTTL,
struct sockaddr_storage *addr)
{
	int r1, r2, r3, retval;

	retval = -1;

	switch (addr->ss_family) {
	case AF_INET: {
		struct ip_mreq      mreq;

		mreq.imr_multiaddr.s_addr =
			((struct sockaddr_in *)addr)->sin_addr.s_addr;
		mreq.imr_interface.s_addr = INADDR_ANY;

		r1 = setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_LOOP,
			(const char*)&loopBack, sizeof(loopBack));
		if (r1<0)
			perror("joinGroup:: IP_MULTICAST_LOOP:: ");

		r2 = setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_TTL,
			(const char*)&mcastTTL, sizeof(mcastTTL));
		if (r2<0)
			perror("joinGroup:: IP_MULTICAST_TTL:: ");

		r3 = setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
			(const char *)&mreq, sizeof(mreq));
		if (r3<0)
			perror("joinGroup:: IP_ADD_MEMBERSHIP:: ");

	} break;

	case AF_INET6: {
		struct ipv6_mreq    mreq6;

		memcpy(&mreq6.ipv6mr_multiaddr,
			&(((struct sockaddr_in6 *)addr)->sin6_addr),
			sizeof(struct in6_addr));

		mreq6.ipv6mr_interface = 0; // cualquier interfaz

		r1 = setsockopt(sockfd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
			(const char*)&loopBack, sizeof(loopBack));
		if (r1<0)
			perror("joinGroup:: IPV6_MULTICAST_LOOP:: ");

		r2 = setsockopt(sockfd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
			(const char*)&mcastTTL, sizeof(mcastTTL));
		if (r2<0)
			perror("joinGroup:: IPV6_MULTICAST_HOPS::  ");

		r3 = setsockopt(sockfd, IPPROTO_IPV6,
			IPV6_ADD_MEMBERSHIP, (const char*)&mreq6, sizeof(mreq6));
		if (r3<0)
			perror("joinGroup:: IPV6_ADD_MEMBERSHIP:: ");

	} break;

	default:
		r1 = r2 = r3 = -1;
	}

	if ((r1 >= 0) && (r2 >= 0) && (r3 >= 0))
		retval = 0;

	return retval;
}


int
isMulticast(struct sockaddr_storage *addr)
{
	int retVal;

	retVal = 0;

	switch (addr->ss_family) {
	case AF_INET: {
		struct sockaddr_in *addr4 = (struct sockaddr_in *)addr;
		retVal = IN_MULTICAST(ntohl(addr4->sin_addr.s_addr));
	} break;

	case AF_INET6: {
		struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)addr;
		retVal = IN6_IS_ADDR_MULTICAST(&addr6->sin6_addr);
	} break;

	default:
		;
	}

	return retVal;
}
