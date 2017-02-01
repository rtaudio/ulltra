#pragma once
#include <vector>

class OrderingBuffer
{
	struct Block {
		std::vector<float*> samples;
		int size;

		void commit();
	};

public:
	OrderingBuffer();
	~OrderingBuffer();

	Block getBlockPointers(uint16_t blockIndex);

};

