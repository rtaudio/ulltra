#include "AudioCoder.h"



AudioCoder::AudioCoder(const EncoderParams &encParams) :
	params(encParams)
{

}


AudioCoder::~AudioCoder()
{
}


size_t AudioCoder::getRequiredBinaryBufferSize()
{
	size_t size = getBlockSize() * params.params.enc.maxBitrate / 8 / params.params.enc.sampleRate;
	return size * 2 + 128;
}
