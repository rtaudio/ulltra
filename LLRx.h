#pragma once

#include "UlltraProto.h"

#include "networking.h"
#include "Discovery.h"

class LLRx
{
public:
	inline LLRx(uint64_t defaultBlockingTimeout) : m_blockingTimeoutUs(defaultBlockingTimeout) {}
	virtual ~LLRx() = 0;
	virtual bool connect(const LinkEndpoint &addr) = 0;
	bool setBlockingTimeout(uint64_t timeoutUs);
	virtual bool onBlockingTimeoutChanged(uint64_t timeoutUs) = 0;
	virtual bool flushBuffer();
	virtual const uint8_t *receive(int &receivedBytes) = 0;

	virtual void toString(std::ostream& stream) const = 0;

	inline friend std::ostream& operator<< (std::ostream& stream, const LLRx& llr) {
		llr.toString(stream);
	}

protected:
	uint64_t m_blockingTimeoutUs;
};

