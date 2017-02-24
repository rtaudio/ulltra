#pragma once

#ifdef WITH_OPUS
#include <vector>
#include "../AudioCoder.h"

struct OpusEncoder;
struct OpusMSEncoder;
struct OpusDecoder;
struct OpusMSDecoder;

class OpusCoder : public AudioCoder
{
public:
        OpusCoder(const CoderParams &params);
        virtual ~OpusCoder();

		int getHeader(uint8_t *outBuffer, int bufferLen) const;

	int encodeInterleaved(const float* samples, int numSamplesPerChannel, uint8_t *buffer, int bufferLen);
	int decodeInterleaved(const uint8_t *buffer, int bufferLen, float *samples, int numSamplesPerChannel);

	virtual int getBlockSize() {
		/*
			*also see definition of encodeInterleaved*
			we fix Fs to 48Khz, opus supports frame(aka. block) durations of 2.5ms, 5ms, 10ms...
			we want to work with lowest delay, so use 48khz*2.5ms = 120
		*/
		return 120*8;
	};
	
private:
	OpusEncoder *enc;
        OpusMSEncoder *MSenc;

       OpusDecoder *dec;
       OpusMSDecoder *MSdec;

       std::vector<int16_t> convertBuffer;

};

#endif