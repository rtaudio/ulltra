#pragma once
#include <ostream>
#include "LLLink.h"

class LLCustomBlock
	: public LLLink
{
public:
	enum class Mode { KernelBlock, UserBlock, Select, Undefined };

	inline LLCustomBlock(uint64_t defaultBlockingTimeout)
		: LLLink(defaultBlockingTimeout), m_rxBlockingMode(Mode::Undefined) {}
	~LLCustomBlock();

	friend inline std::ostream & operator<<(std::ostream & Str, const Mode b) {
		switch (b) {
		case Mode::KernelBlock: return Str << "KernelBlock";
		case Mode::UserBlock: return Str << "UserBlock";
		case Mode::Select: return Str << "Select";
		case Mode::Undefined: return Str << "Undefined";
		}
		return Str;
	}

	bool setRxBlockingMode(Mode mode);

protected:
	Mode m_rxBlockingMode;
};

