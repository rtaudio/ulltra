#pragma once

#ifdef _WIN32
#include<winsock2.h>
//#include<ws2def.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <Iphlpapi.h>
#include <Assert.h>
#pragma comment(lib, "iphlpapi.lib")

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

#include <string>


inline std::string lastError( int err=0)
{
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
#ifdef ETOOMANYREFS
	case ETOOMANYREFS: return("ETOOMANYREFS\n"); break;
#endif
#ifdef EUNATCH
	case EUNATCH: return("EUNATCH\n"); break;
#endif
	default: return("UNKNOWN\n"); break;
	}
}