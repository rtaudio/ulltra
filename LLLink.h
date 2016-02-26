#pragma once
#include "networking.h"

class LLLink
{
public:
	LLLink();
	virtual ~LLLink();


	virtual bool connect(const LinkEndpoint &addr) = 0;
	virtual bool send(const uint8_t *data, int dataLength) = 0;

	virtual void toString(std::ostream& stream) const = 0;

	inline friend std::ostream& operator<< (std::ostream& stream, const LLLink& lll) {
		lll.toString(stream);
	}
};

