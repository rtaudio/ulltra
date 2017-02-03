#include "AudioCoder.h"



AudioCoder::AudioCoder()
{
}


AudioCoder::~AudioCoder()
{
}

int AudioCoder::encode(std::vector<float*> samples, uint8_t *buffer, int bufferLen)
{
	return 0;
}

int AudioCoder::encode(float* samples, int numSamples, uint8_t *buffer, int bufferLen)
{
	return 0;
}

void AudioCoder::decode(const uint8_t *ptr, int len, std::vector<float *> const& targetChannelBuffers, int blockSize)
{

}
