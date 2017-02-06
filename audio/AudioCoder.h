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
	// no copy
	AudioCoder(AudioCoder&) = delete;
	AudioCoder& operator=(AudioCoder&) = delete;

public:

	struct EncoderParams {
		char coderName[16];
		int maxBitrate;
		int sampleRate;
		uint8_t numChannels;
		uint8_t channelOffset;

		void setCoderName(const std::string &name) {
			name.copy(coderName, sizeof(coderName) - 1);
		}

		bool operator==(const EncoderParams &other) const
		{
			return (strcmp(coderName, other.coderName) == 0
				&& maxBitrate == other.maxBitrate
				&& sampleRate == other.sampleRate
				&& numChannels == other.numChannels
				&& channelOffset == other.channelOffset);
		}
	};

	AudioCoder();
	virtual ~AudioCoder();

	int encode(std::vector<float*> samples, uint8_t *buffer, int bufferLen);
	int encode(float* samples, int numSamples, uint8_t *buffer, int bufferLen);
	void decode(const uint8_t *ptr, int len, std::vector<float *> const& targetChannelBuffers, int blockSize);
};
