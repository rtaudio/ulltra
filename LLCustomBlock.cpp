#include "LLCustomBlock.h"

#include "UlltraProto.h"



LLCustomBlock::~LLCustomBlock()
{
}


bool LLCustomBlock::setRxBlockingMode(Mode mode)
{
	if (mode == Mode::Undefined || m_rxBlockingMode == mode) {
		LOG(logDEBUG3) << " setRxBlockingMode: ignore setting to " << mode << ", is " << m_rxBlockingMode;
		return false;
	}
		

	m_rxBlockingMode = mode;


	if (!onBlockingTimeoutChanged(m_receiveBlockingTimeoutUs))
		return false;

#if _DEBUG
	if (mode == LLCustomBlock::Mode::KernelBlock) {
		LOG(logDEBUG1) << "socket blocking mode set to kernel blocking";
	}
	else {
		LOG(logDEBUG1) << "socket blocking mode set to user blocking";
	}
#endif

	return true;
}