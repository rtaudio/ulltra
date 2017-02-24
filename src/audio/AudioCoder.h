#pragma once
#include <vector>
#include <stdint.h>
#include <functional>
#include <map>
#include <cstring>

// VS's Call to 'std::basic_string::copy' with parameters that may be unsafe
#pragma warning( disable : 4996 )

class AudioCoder;
struct StreamEndpointInfo;


typedef std::function<AudioCoder*()> AudioCoderGenerator;
typedef std::map<std::string, AudioCoderGenerator> AudioCoderGeneratorSet;



class AudioCoder
{
	// no copy
	AudioCoder(AudioCoder&) = delete;
	AudioCoder& operator=(AudioCoder&) = delete;

	// but move
	AudioCoder(AudioCoder&&) = default;
	AudioCoder& operator = (AudioCoder&&) = default;

public:


	enum CoderType {
		Encoder,
		Decoder
	};


	struct CoderParams {
		CoderType type;
		char coderName[16];
		int sampleRate;
		uint8_t numChannels;
		uint8_t channelOffset;
		bool withTiming;

		struct EncoderParams {
			int maxBitrate;
			bool lowDelay;
			int8_t complexity;//0-10, -1: default
			EncoderParams() :maxBitrate(0), lowDelay(false), complexity(-1) {}
		};
		EncoderParams enc;


		struct decparams {
		};
		decparams dec;


		CoderParams(const std::string &name, CoderType type, const StreamEndpointInfo &ei);

		CoderParams(const std::string &name, CoderType type, int sampleRate, int numChannels, int channelsOffset = 0);

		virtual bool isEqual(const CoderParams &other) const {
			return (type == other.type
				&& strcmp(coderName, other.coderName) == 0
				&& sampleRate == other.sampleRate
				&& numChannels == other.numChannels
				&& channelOffset == other.channelOffset
				&& (type == Encoder
					? (enc.complexity == other.enc.complexity && enc.lowDelay == other.enc.lowDelay && enc.maxBitrate == enc.maxBitrate)
					: (true)
					)
				);
		}

		inline bool operator==(const CoderParams &other) const { return isEqual(other); }
	};


	typedef std::function<AudioCoder * (const AudioCoder::CoderParams& params)> Factory;

	AudioCoder(const CoderParams &params);
	virtual ~AudioCoder();
	
	virtual int getBlockSize() { return 1024 * 8; }; // just choose large block size we wont reach with audio hardware

	virtual int getHeader(uint8_t *outBuffer, int bufferLen) const {
		return 0;
	}

	virtual int encodeInterleaved(const float* samples, int numSamplesPerChannel, uint8_t *buffer, int bufferLen) = 0;
	virtual int decodeInterleaved(const uint8_t *buffer, int bufferLen, float *samples, int maxSamplesPerChannel) = 0;


	virtual size_t getRequiredBinaryBufferSize();


	inline int encodeInterleaved(std::vector<float> samples, uint8_t *buffer, int bufferLen) {
		return encodeInterleaved(samples.data(), (int)samples.size(), buffer, bufferLen);
	}

protected:
	CoderParams params;

	// see https://github.com/jackaudio/jack2/blob/master/common/memops.c
    // float -> int16 ( sample_move_d16_sS )
	void sample_copy_float_to_int16(int16_t *dst, const float *src, unsigned long nsamples, unsigned long dst_skip);

	// int16 -> float  ( sample_move_dS_s16 )
	void sample_copy_int16_to_float(float *dst, const int16_t *src, unsigned long nsamples, unsigned long src_skip);

};
