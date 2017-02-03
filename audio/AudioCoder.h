#pragma once
#include <vector>
#include <stdint.h>
#include <functional>
#include <map>

class AudioCoder;

typedef std::function<AudioCoder*()> AudioCoderGenerator;
typedef std::map<std::string, AudioCoderGenerator> AudioCoderGeneratorSet;

class AudioCoder
{
public:
	AudioCoder();
	virtual ~AudioCoder();

	int encode(std::vector<float*> samples, uint8_t *buffer, int bufferLen);
	int encode(float* samples, int numSamples, uint8_t *buffer, int bufferLen);
	void decode(const uint8_t *ptr, int len, std::vector<float *> const& targetChannelBuffers, int blockSize);
};

