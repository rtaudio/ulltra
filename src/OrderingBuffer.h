#pragma once
#include <vector>
#include <stdint.h>

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

