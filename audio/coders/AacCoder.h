#pragma once
#include "../AudioCoder.h"

#include "fdk-aac/libAACenc/include/aacenc_lib.h"

class AacCoder : public AudioCoder
{
public:
	struct Params {
		int bitrate;
		Params() : bitrate(64000) {}
	};

	AacCoder(const EncoderParams &params);
	virtual ~AacCoder();

	int encodeInterleaved(const float* samples, int numFrames, uint8_t *buffer, int bufferLen);
	void decodeInterleaved(const uint8_t *buffer, int bufferLen, float *samples, int numFrames);
	
	int getBlockSize() {
		return info.frameLength / params.params.enc.numChannels;
	}
private:
	HANDLE_AACENCODER handle;
	CHANNEL_MODE mode;
	AACENC_InfoStruct info = { 0 };
	int16_t *convert_buf;




	
};

