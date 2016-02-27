#include "LLLink.h"

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
	if (t != 0) setBlockingTimeout(0);
	int recvLen;
	while (receive(recvLen)) {}
	if (t != 0) return setBlockingTimeout(t); // restore previous timeout
	return true;
}