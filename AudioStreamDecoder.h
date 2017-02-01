#pragma once

#include <vector>

class AudioStreamDecoder
{
public:
	AudioStreamDecoder();
	virtual ~AudioStreamDecoder();

	void decode(const uint8_t *ptr, int len, std::vector<float *> const& targetChannelBuffers, int blockSize);
};

