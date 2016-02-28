#include "LLTcp.h"



LLTcp::LLTcp()
	: LLLink(-1)
{
	//m_socAccept = socket(NodeDevice::local6.addrStorage.ss_family, SOCK_STREAM, 0);

	/*
	auto bindAddr = NodeDevice::local6.getAddr(broadcastPort + 3);
	if (bind(m_socAccept, (struct sockaddr *) &bindAddr, sizeof(bindAddr)) < 0) {
		LOG(logERROR) << "tcp bind to " << bindAddr << " failed!";
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

	*/


	/*
	while(true) {
	int remoteAddrLen = sizeof(remote);
	SOCKET newsockfd = accept(m_socAccept, (struct sockaddr *)&remote, &remoteAddrLen);
	if(newsockfd < 0)
	break;
	}
	*/
}


LLTcp::~LLTcp()
{
}
