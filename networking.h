#pragma once



#ifdef _WIN32
#include<winsock2.h>
//#include<ws2def.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <Iphlpapi.h>
#include <Assert.h>
#pragma comment(lib, "iphlpapi.lib")

#define close closesocket

#else
#include<unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#ifndef SOCKET
#define SOCKET int
#endif
#endif

#include "UlltraProto.h"

#include <string>

typedef struct addrinfo NodeAddr;



#ifdef _WIN32
        typedef char * socket_buffer_t;
#else
        typedef unsigned char * socket_buffer_t;
#endif

inline std::string lastError( int err=0)
{
#if _WIN32
	err = err ? err : WSAGetLastError();
#endif

	switch (err ? err : errno) {
	case EADDRINUSE: return("EADDRINUSE\n"); break;
	case EADDRNOTAVAIL: return("EADDRNOTAVAIL\n"); break;
	case EBADF: return("EBADF\n"); break;
	case ECONNABORTED: return("ECONNABORTED\n"); break;
	case EINVAL: return("EINVAL\n"); break;
	case EIO: return("EIO\n"); break;
	case ENOBUFS: return("ENOBUFS\n"); break;
	case ENOPROTOOPT: return("ENOPROTOOPT\n"); break;
	case ENOTCONN: return("ENOTCONN\n"); break;
	case ENOTSOCK: return("ENOTSOCK\n"); break;
	case EPERM: return("EPERM\n"); break;
	
#ifdef _WIN32
	case WSAEADDRNOTAVAIL: return "WSAEADDRNOTAVAIL"; break;
#endif

#ifdef ETOOMANYREFS
	case ETOOMANYREFS: return("ETOOMANYREFS\n"); break;
#endif
#ifdef EUNATCH
	case EUNATCH: return("EUNATCH\n"); break;
#endif
	default: return("UNKNOWN\n"); break;
	}
}


struct LinkEndpoint {
	struct addrinfo addr;
	int port;
};


inline bool socketSetBlocking(SOCKET &soc, bool block) {
#ifndef _WIN32
	const int flags = fcntl(soc, F_GETFL, 0);
        int curNonBlock = (flags & O_NONBLOCK);
        if (curNonBlock == !block) {
		LOG(logDEBUG) << "Kernel blocking mode of socket already set to " << block;
		return true;
}

	if (-1 == fcntl(soc, F_SETFL, block ? flags ^ O_NONBLOCK : flags | O_NONBLOCK)) {
		LOG(logERROR) << "Cannot set socket kernel blocking to " << block << "!";
		return false;
	}
#else
	u_long iMode = block ? 0 : 1;
	if (ioctlsocket(soc, FIONBIO, &iMode) != NO_ERROR)
	{
		LOG(logERROR) << "Cannot set socket kernel blocking to " << block << "!";
		return false;
	}
#endif
	return true;
}

inline int socketSelectTimeout(SOCKET &soc, uint64_t toUs)
{
    fd_set myset;
    struct timeval tv;
    int res;

    tv.tv_sec = toUs/1000000;
    tv.tv_usec = toUs - tv.tv_sec;

    FD_ZERO(&myset);
    FD_SET(soc, &myset);

    res = select(soc+1, NULL, &myset, NULL, &tv);

    if (res < 0 && errno != EINTR) {
        LOG(logERROR) << "error while select: " << errno << " " << strerror(errno);
        return res;
    }
    else if (res > 0)
    {
        // get the actual result
        socklen_t olen = sizeof(int);
        int o;
        if ((res = getsockopt(soc, SOL_SOCKET, SO_ERROR, (void*)(&o), &olen)) < 0) {
            LOG(logERROR) << "Error in getsockopt() " << errno << " " << strerror(errno);
            return res;
        }
        return o;
    }
    else {
        LOG(logDEBUG) << "select() timeout";
        return -1;
    }
 }



inline std::ostream & operator<<(std::ostream &os, const struct sockaddr_storage& s)
{
    char host[64], serv[32];
    getnameinfo((const sockaddr*)&s, sizeof(s), host, sizeof(host), serv, sizeof(serv), 0);
    os << "[" << host << "]:" << serv;
}
