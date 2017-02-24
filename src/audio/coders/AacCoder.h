#pragma once
#include "../AudioCoder.h"

#include "fdk-aac/libAACenc/include/aacenc_lib.h"

#include "fdk-aac/libAACdec/include/aacdecoder_lib.h"

class AacCoder : public AudioCoder
{
public:
	struct Params {
		int bitrate;
		Params() : bitrate(64000) {}
	};

	AacCoder(const CoderParams &params);
	virtual ~AacCoder();

	int encodeInterleaved(const float* samples, int numSamplesPerChannel, uint8_t *buffer, int bufferLen);
	int decodeInterleaved(const uint8_t *buffer, int bufferLen, float *samples, int numSamplesPerChannel);
	
	int getBlockSize() {
		return info.frameLength / params.numChannels;
	}
private:
	HANDLE_AACENCODER encHandle;
	AACENC_InfoStruct info = { 0 };

	HANDLE_AACDECODER decHandle;

	CHANNEL_MODE mode;

	std::vector<int16_t> convert_buf;	
};

