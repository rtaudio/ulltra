#pragma once
#include "../AudioCoder.h"


class NoCoder : public AudioCoder
{
public:
	NoCoder(const EncoderParams &params);
	virtual ~NoCoder();

	int encodeInterleaved(const float* samples, int numFrames, uint8_t *buffer, int bufferLen);
	void decodeInterleaved(const uint8_t *buffer, int bufferLen, float *samples, int numFrames);
	int getBlockSize() { return blockSize; }

private:
	int blockSize;
	
};

