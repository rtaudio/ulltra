#pragma once
#include <vector>
#include "../AudioCoder.h"

#include <opus/opus.h>
#include <opus/opus_multistream.h>

class OpusCoder : public AudioCoder
{
public:
        OpusCoder(const EncoderParams &params);
        virtual ~OpusCoder();

	int encodeInterleaved(const float* samples, int numFrames, uint8_t *buffer, int bufferLen);
	void decodeInterleaved(const uint8_t *buffer, int bufferLen, float *samples, int numFrames);
	
	int getBlockSize() {
		//return info.frameLength / params.params.enc.numChannels;
		return -1;
	}
private:
	OpusEncoder *enc;
        OpusMSEncoder *MSenc;

       OpusDecoder *dec;
       OpusMSDecoder *MSdec;



           std::vector<int16_t> convertBuffer;




	
};

