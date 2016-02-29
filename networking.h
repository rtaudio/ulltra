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
#include <string.h>
#ifndef SOCKET
#define SOCKET int
#endif
#endif

#include "UlltraProto.h"

#include <string>

typedef struct sockaddr_storage NodeAddr;



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

    tv.tv_sec = (long)(toUs/1000000UL);
    tv.tv_usec = (long)(toUs - tv.tv_sec);


    FD_ZERO(&myset);
    FD_SET(soc, &myset);

    res = select(soc+1/* this is ignored on windows*/, NULL, &myset, NULL, &tv);

    if(res == -1) {
        LOG(logERROR) << "error while select: " << lastError();
        return -1;
    }

    if(res == 0) {
        LOG(logDEBUG1) << "timeout during select";
        return 0;
    }

    socklen_t olen = sizeof(int);
    int o = -1;
    if (getsockopt(soc, SOL_SOCKET, SO_ERROR, (char*)(&o), &olen) < 0) {
        LOG(logERROR) << "Error in getsockopt() " << lastError();
        return -1;
    }

    // some error occured
    if(o != 0) {
        return -1;
    }

    return res;
 }



inline std::ostream & operator<<(std::ostream &os, const struct sockaddr_storage* s)
{
	char host[64], host2[64], serv[32];
	int r = getnameinfo((const sockaddr*)s, sizeof(*s), host, sizeof(host), serv, sizeof(serv), NI_NUMERICHOST);
	if (r == 0) {
		if (getnameinfo((const sockaddr*)s, sizeof(*s), host2, sizeof(host2), serv, sizeof(serv), 0) == 0 && strcmp(host, host2) != 0 && strcmp("localhost", host2) != 0)
			os << "[" << host << "]("<< host2 << "):" << serv;
		else
			os << "[" << host << "]:" << serv;
	}
	else {
		os << "[getnameinfo error!]";
	}
	return os;
}

inline std::ostream & operator<<(std::ostream &os, const struct sockaddr_storage& s) { return os << &s; }
inline std::ostream & operator<<(std::ostream &os, const struct sockaddr& s) { return os << ((const struct sockaddr_storage *)&s); }
inline std::ostream & operator<<(std::ostream &os, const struct sockaddr_in& s) { return os << ((const struct sockaddr_storage *)&s); }
inline std::ostream & operator<<(std::ostream &os, const struct sockaddr* s) { return os << ((const struct sockaddr_storage *)s); }
inline std::ostream & operator<<(std::ostream &os, const struct sockaddr_in* s) { return os << ((const struct sockaddr_storage *)s); }

inline std::ostream & operator<<(std::ostream &os, const struct addrinfo* s)
{
	const struct sockaddr_storage *ss = (sockaddr_storage *)s->ai_addr;

        if (s->ai_socktype == SOCK_STREAM) {
                os << "tcp://";
        }
        else if (s->ai_socktype == SOCK_DGRAM) {
                os << "udp://";
        }
        else if (s->ai_socktype == SOCK_RAW) {
                os << "raw://";
        }

	os << *ss;

	if (s->ai_canonname) {
		os << "(" << s->ai_canonname << ")";
	}

	if (s->ai_next) {
		os << ", " << s->ai_next;
	}

	return os;
}

inline std::ostream & operator<<(std::ostream &os, const struct addrinfo& s) { return os << &s; }

union SocketAddress {
    struct sockaddr sa;
    struct sockaddr_in sin;
    struct sockaddr_in6 sin6;
    // union! no more fields!

    SocketAddress(const sockaddr_storage &ss)
    {
        memcpy(&sa, &ss, sizeof(sin6));
    }

    inline void setPort(int port) {
        if(sa.sa_family == AF_INET6)
            sin6.sin6_port = htons(port);
        else
            sin.sin_port = htons(port);
    }

    inline std::string toString() const {
        char host[64], serv[32];
        int r = getnameinfo((const sockaddr*)&sin6, sizeof(sin6), host, sizeof(host), serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
        if(r != 0) {
            LOG(logERROR) << "getnameinfo() failed! " << gai_strerror(r) << lastError();
            return "";
        }

        if(sin6.sin6_family == AF_INET6) {
            return "["+std::string(host)+"]:" + std::string(serv);
        } else {
            return std::string(host) + ":" + std::string(serv);
        }
    }
};

