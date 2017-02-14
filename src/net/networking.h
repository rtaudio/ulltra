#pragma once


#include "../UlltraProto.h"
#include "../masaj/net.h"
#include "../masaj/SocketAddress.h"

#include <string>

typedef struct sockaddr_storage NodeAddr;

#ifdef _WIN32
        typedef char * socket_buffer_t;
#else
        typedef unsigned char * socket_buffer_t;
#endif


inline int socketSelect(SOCKET &soc, uint64_t toUs)
{
	fd_set myset;
	struct timeval tv;
	int res;

	tv.tv_sec = (long)(toUs / 1000000UL);
	tv.tv_usec = (long)(toUs - tv.tv_sec);


	FD_ZERO(&myset);
	FD_SET(soc, &myset);

	res = select(soc + 1/* this is ignored on windows*/, &myset, &myset, &myset, &tv);
	//LOG(logDEBUG1) << "res= " << res;
	if (res == -1) {
		LOG(logERROR) << "error while select: " << lastError();
		return -1;
	}

	socklen_t olen = sizeof(int);
	int o = -1;
	if (getsockopt(soc, SOL_SOCKET, SO_ERROR, (char*)(&o), &olen) < 0) {
		LOG(logERROR) << "Error in getsockopt() " << lastError();
		return -1;
	}

	//LOG(logDEBUG1) << "o = " << o;

	if (res == 0) {
		//LOG(logDEBUG1) << "timeout during select " << toUs;
		return 0;
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

typedef SocketAddress LinkEndpoint;

