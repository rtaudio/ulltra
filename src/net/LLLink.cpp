#include "LLLink.h"

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/ioctl.h>

#ifndef  ANDROID
#include <linux/net_tstamp.h>
#endif
#include <linux/errqueue.h>

#include <sys/types.h>

#ifndef ANDROID
	#include <ifaddrs.h> // for interface listing
#endif

#else
#include <mswsock.h>
#include <qos2.h> //qwave
HANDLE LLLink::s_qosHandle = NULL;
#endif

#include<iomanip>

#include "../UlltraProto.h"

int LLLink::s_timeStampingEnabled = 0;

LLLink::QosHandle LLLink::QosHandle::invalid(-1);

std::string getInterfaceName(int index);

LLLink::~LLLink()
{
}


bool LLLink::setBlockingTimeout(uint64_t timeoutUs) {
	if (m_receiveBlockingTimeoutUs != timeoutUs) {
		if (onBlockingTimeoutChanged(timeoutUs))
			m_receiveBlockingTimeoutUs = timeoutUs;
		else {
			LOG(logERROR) << "Blocking timeout not changed!";
			return false;
		}
	}
	return true;
}

bool LLLink::flushReceiveBuffer() {
	auto t = m_receiveBlockingTimeoutUs;
	if (t != 0) setBlockingTimeout(0); // 0 means asyc!
	int recvLen;
	std::vector<uint8_t> buf;
	while (receive(buf.data(), buf.size())) {}
	if (t != 0) return setBlockingTimeout(t); // restore previous timeout
	return true;
}

LLLink::QosHandle LLLink::enableHighQoS(SOCKET soc, int bitsPerSecond)
{
#ifndef _WIN32
	SocketAddress sa;
	socklen_t len = sizeof(sa);
	if (getsockname(soc, &sa.sa, &len) != 0) {
		LOG(logERROR) << " getsockname failed!" << lastError();
		return  QosHandle::invalid;
	}

	bool ipv6 = sa.getFamily() == AF_INET6;

	/* setup QoS */
	// see http://www.cisco.com/c/en/us/td/docs/switches/datacenter/nexus1000/sw/4_0/qos/configuration/guide/nexus1000v_qos/qos_6dscp_val.pdf
	/*
	we want 101 110 | 46 | High Priority | Expedited Forwarding (EF) N/A 101 - Critical
	*/
#if !defined(IPTOS_DSCP_EF)
#define IPTOS_DSCP_EF           0xb8
#endif

	int iptos = ipv6 ? IPTOS_DSCP_EF : (IPTOS_LOWDELAY | IPTOS_PREC_CRITIC_ECP | IPTOS_THROUGHPUT);

	int so_priority = 7;
	if (setsockopt(soc, SOL_SOCKET, SO_PRIORITY, &so_priority, sizeof(so_priority)) < 0) {
		LOG(logERROR) << "Error: could not set socket SO_PRIORITY to  " << so_priority << "! " << lastError();
		return  QosHandle::invalid;
	}
	else {
		LOG(logDEBUG) << "Set SO_PRIORITY to " << so_priority;
	}



	// IP_TOS not supported by windows, see https://msdn.microsoft.com/de-de/library/windows/desktop/ms738586(v=vs.85).aspx
	// have to use qWave API instead
	if (setsockopt(soc, ipv6 ? IPPROTO_IPV6 : SOL_IP, ipv6 ? IPV6_TCLASS : IP_TOS, &iptos, sizeof(iptos)) < 0) {
		LOG(logERROR) << "Error: could not set socket IP_TOS priority to  IPTOS_LOWDELAY | IPTOS_PREC_CRITIC_ECP | IPTOS_THROUGHPUT! " << lastError();
		return  QosHandle::invalid;
	}
	else {
		LOG(logDEBUG) << "Set IP_TOS to  IPTOS_LOWDELAY | IPTOS_PREC_CRITIC_ECP | IPTOS_THROUGHPUT";
	}

	return QosHandle(1); // linux dont have handles for QoS
#else

	if (!s_qosHandle) {
		static QOS_VERSION QosVersion = { 1 , 0 };
		if (FALSE == QOSCreateHandle(&QosVersion, &s_qosHandle)) {
			fprintf(stderr, "%s:%d - QOSCreateHandle failed (%d)\n",
				__FILE__, __LINE__, GetLastError());
			return QosHandle::invalid;
		}
	}

	QOS_FLOWID flowID = 0;
	BOOL result = QOSAddSocketToFlow(s_qosHandle,
		soc,
		NULL, // destination (use from WSAConnect)
		QOSTrafficTypeVoice, //QOSTrafficTypeControl, //QOSTrafficTypeVoice, //QOSTrafficTypeExcellentEffort, // TODO
		QOS_NON_ADAPTIVE_FLOW, // no Experiments!
		&flowID);
	if (result == FALSE) {
		LOG(logERROR) << "QOSAddSocketToFlow failed: " << lastError();
		// if 87 error, connection failed, a destination in QOSAddSocketToFlow must be specified then!
		fprintf(stderr, "%s:%d - QOSAddSocketToFlow failed (%d)\n",
			__FILE__, __LINE__, GetLastError());
		return QosHandle::invalid;
	}

	//ERROR_BAD_NET_NAME

	QOS_FLOWRATE_OUTGOING       flowRate;

	// Calculate the real bandwidth we will need to be shaped to.
	flowRate.Bandwidth = bitsPerSecond;

	// Set shaping characteristics on our QOS flow to smooth out our bursty
	// traffic.
	flowRate.ShapingBehavior = QOSShapeAndMark;

	// The reason field is not applicable for the initial call.
	flowRate.Reason = QOSFlowRateNotApplicable;
	result = QOSSetFlow(s_qosHandle,
		flowID,
		QOSSetOutgoingRate,
		sizeof(flowRate),
		&flowRate,
		0,
		NULL);
	if (result == FALSE) {
		fprintf(stderr, "%s:%d - QOSSetFlow failed (%d)\n",
			__FILE__, __LINE__, GetLastError());
		return QosHandle::invalid;
	}

	// DSCP Bits						
	//						/ IP-Pr.
	//						|		/ 1=LowDelay
	//						|		|	/ 1=HighThroughput
	//						|		|	|	/ 1=High Reliability
	//						|		|	|	|	/ 1= Minimise Monetary cost
	//						V		V	V	V	V	/ = 0
	//bool dscpBits[] = { 1,0,1,	    1,   1,	0,	0,	0 };

	// its reverse: (dscpVal = 46, see cisco link above)
	bool dscpBits[] = { 0,1,1,	   1,	0,	1,	0,	0 };

	DWORD dscpVal = 0;
	for (int i = 0; i < sizeof(dscpBits); i++) {
		dscpVal |= dscpBits[i] << i;
	}

	dscpVal = 63;
	//ERROR_ACCESS_DISABLED_BY_POLICY
	result = QOSSetFlow(s_qosHandle,
		flowID,
		QOSSetOutgoingDSCPValue,
		sizeof(dscpVal),
		&dscpVal,
		0,
		NULL);
	if (result == FALSE) {
		LOG(logERROR) << lastError();
		fprintf(stderr, "%s:%d - QOSSetFlow(QOSSetOutgoingDSCPValue) failed (%d)\n",
			__FILE__, __LINE__, GetLastError());
		
		return QosHandle::invalid;
	}

	// TODO:
	// 101 1 0 1 0 0

	return QosHandle(flowID);
#endif
}

#ifdef SIOCGSTAMPNS
static int printpacket(struct msghdr *msg, int res,
	char *data,
    int sock, int recvmsg_flags, int *usecStackDelay)
{
	struct sockaddr_in *from_addr = (struct sockaddr_in *)msg->msg_name;
	struct cmsghdr *cmsg;
	struct timeval tv;
	struct timespec ts;
	struct timeval now;

	gettimeofday(&now, 0);

    //printf("%ld.%06ld: received %s data, %d bytes from %s, %zu bytes control messages\n", (long)now.tv_sec, (long)now.tv_usec, (recvmsg_flags & MSG_ERRQUEUE) ? "error" : "regular", res, inet_ntoa(from_addr->sin_addr), msg->msg_controllen);
    for (cmsg = CMSG_FIRSTHDR(msg);	cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
		switch (cmsg->cmsg_level) {
		case SOL_SOCKET:
			switch (cmsg->cmsg_type) {
            case SO_TIMESTAMP: {
                    struct timeval *stamp =
                        (struct timeval *)CMSG_DATA(cmsg);
                    *usecStackDelay = (int)((long)now.tv_usec-(long)stamp->tv_usec);
                    //printf("SO_TIMESTAMP %ld.%06ld, usecD = %d", (long)stamp->tv_sec, (long)stamp->tv_usec, *usecStackDelay);
                    break;
                }
			case SO_TIMESTAMPNS: {
                struct timespec *stamp = (struct timespec *)CMSG_DATA(cmsg);
                printf("SO_TIMESTAMPNS %ld.%09ld  now:%lu",	(long)stamp->tv_sec,(long)stamp->tv_nsec, now);
                break;
			}
			case SO_TIMESTAMPING: {
				struct timespec *stamp =
					(struct timespec *)CMSG_DATA(cmsg);
				printf("SO_TIMESTAMPING ");
				printf("SW %ld.%09ld ",
					(long)stamp->tv_sec,
					(long)stamp->tv_nsec);
				stamp++;
				/* skip deprecated HW transformed */
				stamp++;
				printf("HW raw %ld.%09ld",
					(long)stamp->tv_sec,
					(long)stamp->tv_nsec);
				break;
			}
			default:
				printf("type %d", cmsg->cmsg_type);
				break;
			}
			break;
		case IPPROTO_IP:
			printf("IPPROTO_IP ");
			switch (cmsg->cmsg_type) {
			case IP_RECVERR: {
				struct sock_extended_err *err =
					(struct sock_extended_err *)CMSG_DATA(cmsg);
				printf("IP_RECVERR ee_errno '%s' ee_origin %d => %s",
					strerror(err->ee_errno),
					err->ee_origin,
#ifdef SO_EE_ORIGIN_TIMESTAMPING
					err->ee_origin == SO_EE_ORIGIN_TIMESTAMPING ?
					"bounced packet" : "unexpected origin"
#else
					"probably SO_EE_ORIGIN_TIMESTAMPING"
#endif
					);

                // data here!
                LOG(logDEBUG1) << "received bytes:" << res;
                return res;

				break;
			}
			case IP_PKTINFO: {
				struct in_pktinfo *pktinfo =
					(struct in_pktinfo *)CMSG_DATA(cmsg);
				printf("IP_PKTINFO interface index %u",
					pktinfo->ipi_ifindex);
				break;
			}
			default:
				printf("type %d", cmsg->cmsg_type);
				break;
			}
			break;
		default:
			printf("level %d type %d",
				cmsg->cmsg_level,
				cmsg->cmsg_type);
			break;
		}
        //printf("\n");
	}

    // more accurate socket timestamping, see https://www.kernel.org/doc/Documentation/networking/timestamping/timestamping.c
    if (0) { // siocgstampns
		if (ioctl(sock, SIOCGSTAMPNS, &ts))
			printf("   %s: %s\n", "SIOCGSTAMPNS", strerror(errno));
		else
			printf("SIOCGSTAMPNS %ld.%09ld\n",
				(long)ts.tv_sec,
				(long)ts.tv_nsec);
	}

    return res;
}

static int recvpacket(int sock, char *pdata, int dataLen, int recvmsg_flags, int *usecStackDelay)
{
	struct msghdr msg;
	struct iovec entry;
	struct sockaddr_in from_addr;
	struct {
		struct cmsghdr cm;
		char control[512];
} control;
	int res;

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &entry;
	msg.msg_iovlen = 1;
    entry.iov_base = pdata;
    entry.iov_len = dataLen;
	msg.msg_name = (caddr_t)&from_addr;
	msg.msg_namelen = sizeof(from_addr);
	msg.msg_control = &control;
	msg.msg_controllen = sizeof(control);

    res = recvmsg(sock, &msg, recvmsg_flags /*| MSG_DONTWAIT*/);
  /*  if(errno == EAGAIN) {
       return -1;
    } */
	if (res < 0) {
		/* printf("%s %s: %s\n",
			"recvmsg",
			(recvmsg_flags & MSG_ERRQUEUE) ? "error" : "regular",
			strerror(errno)); 
			*/
        return res;
	}
	else {
        return printpacket(&msg, res, pdata,
            sock, recvmsg_flags, usecStackDelay);
	}
}
#endif

/*
returns:
 0:	peer has shutdown
-1:	on error:
	- if no data and non-blocking socket (sets errno to EAGAIN or EWOULDBLOCK)
*/
int LLLink::socketReceive(SOCKET soc, uint8_t *buffer, int bufferSize)
{
	struct sockaddr_storage remote;
	// todo: use connect + recv
#ifdef SIOCGSTAMPNS
    if(s_timeStampingEnabled) {
        int usecStackDelay = 0;
        int res = recvpacket(soc, (char*)buffer, bufferSize, 0, &usecStackDelay);
        if(usecStackDelay > 0) {
            if(usecStackDelay >= m_stackDelayHist.size())
                m_stackDelayHist.resize(2000 + usecStackDelay + usecStackDelay/2, 0);
            m_stackDelayHist[usecStackDelay]++;
        }
        return res;
    }
    socklen_t addr_len = sizeof(remote);
	return recvfrom((SOCKET)soc, buffer, bufferSize - 1, 0, (struct sockaddr *) &remote, &addr_len);
#else
	int addr_len = sizeof(remote);
	return recvfrom((SOCKET)soc, (char*)buffer, bufferSize - 1, 0, (struct sockaddr *) &remote, &addr_len);
#endif	
}

bool LLLink::enableTimeStamps(SOCKET soc, bool enable)
{
#ifdef SO_TIMESTAMP
    int enabled = enable ? 1 : 0;

    m_stackDelayHist.clear();

    // verify
    int val;
    socklen_t len = sizeof(val);
    if (getsockopt(soc, SOL_SOCKET, SO_TIMESTAMP, &val, &len) < 0 ) {
        LOG(logERROR) << "Check: Failed to enable SO_TIMESTAMP " << lastError();
    }

    if( val == enabled) {
        LOG(logERROR) << "SO_TIMESTAMP already enabled for socket";
    }

    if (setsockopt(soc, SOL_SOCKET, SO_TIMESTAMP, &enabled, sizeof(enabled)) < 0) {
        LOG(logERROR) << "Failed to enable SO_TIMESTAMP " << lastError();
	}

	// verify
    len = sizeof(val);
    if (getsockopt(soc, SOL_SOCKET, SO_TIMESTAMP, &val, &len) < 0 || val != enabled) {
        LOG(logERROR) << "Check: Failed to enable SO_TIMESTAMP " << lastError();
	}


    s_timeStampingEnabled += enable ? 1 : -1;

    if(s_timeStampingEnabled > 0)
        LOG(logWARNING) << " SO_TIMESTAMP enabled! This might be system-wide! ";
    else
        LOG(logWARNING) << " SO_TIMESTAMP disabled! ";

    return true;
#else
    LOG(logERROR) << " SO_TIMESTAMP not available! ignoring";
    return true;
#endif

}

#if !defined(_WIN32) && !defined(ANDROID)



std::string getInterfaceName(int index)
{
    struct ifaddrs *ifap, *ic;
    if(getifaddrs(&ifap) != 0)
        return "";


    for(ic = ifap; ic && index; ic = ic->ifa_next)
    {
        index--;
    }

    std::string name;
    if(ic) {
        //SocketAddress sa(ic->ifa_addr);
        name = std::string(ic->ifa_name) ;//+ "" + sa.toString();
    }

    freeifaddrs(ifap);
    return name;
}

#endif