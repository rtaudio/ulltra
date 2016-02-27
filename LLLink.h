#pragma once

#include "networking.h"
#include "UlltraProto.h"
#include "Discovery.h"

class LLLink
{
public:
	inline LLLink(uint64_t defaultBlockingTimeout) : m_receiveBlockingTimeoutUs(defaultBlockingTimeout) {}
	virtual ~LLLink();

	/* common */
	virtual bool connect(const LinkEndpoint &addr) = 0;
	virtual void toString(std::ostream& stream) const = 0;

	/* rx */
	bool setBlockingTimeout(uint64_t timeoutUs);
	virtual bool onBlockingTimeoutChanged(uint64_t timeoutUs) = 0;
	virtual bool flushReceiveBuffer();
	virtual const uint8_t *receive(int &receivedBytes) = 0;

	/* tx */
	virtual bool send(const uint8_t *data, int dataLength) = 0;

	inline friend std::ostream& operator<< (std::ostream& stream, const LLLink& lll) {
		lll.toString(stream);
	}
protected:
	uint64_t m_receiveBlockingTimeoutUs;
};

