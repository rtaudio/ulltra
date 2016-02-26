#include "LLRx.h"


bool LLRx::setBlockingTimeout(uint64_t timeoutUs) {
	if (m_blockingTimeoutUs != timeoutUs) {
		if (onBlockingTimeoutChanged(timeoutUs))
			m_blockingTimeoutUs = timeoutUs;
		else
			LOG(logERROR) << "Blocking timeout not changed!";
	}
}

bool LLRx::flushBuffer() {
	auto t = m_blockingTimeoutUs;
	if (t != 0) setBlockingTimeout(0);
	int recvLen;
	struct sockaddr_storage addr;
	while (receive(recvLen, addr)) {}
	if (t != 0) return setBlockingTimeout(t); // restore previous timeout
	return true;
}