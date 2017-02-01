#pragma once
#include <vector>
#include <stdint.h>

class AudioStreamEncoder
{
public:
	AudioStreamEncoder();
	virtual ~AudioStreamEncoder();

	int encode(std::vector<float*> samples, uint8_t *buffer, int bufferLen);
};

