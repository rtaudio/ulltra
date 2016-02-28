#pragma once
#include "LLLink.h"
class LLTcp :
	public LLLink
{
public:
	LLTcp();
	~LLTcp();
private:
	SOCKET m_socAccept; //TCP accept
	SOCKET m_socConnect; //TCP accept
};

