#pragma once
#include <vector>
#include <stdint.h>
#include <functional>
#include <map>
#include <cstring>

// VS's Call to 'std::basic_string::copy' with parameters that may be unsafe
#pragma warning( disable : 4996 )

class AudioCoder;

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

	struct CoderParams {
		char coderName[16];
		int sampleRate;
		uint8_t numChannels;
		uint8_t channelOffset;
	};
	

	struct EncoderParams : CoderParams {		
		int maxBitrate;	
        bool lowDelay;
        int8_t complexity;//0-10

        void reset() {
            memset(this, 0, sizeof(*this));
            complexity = -1;
        }

		void setCoderName(const std::string &name) {
			memset(coderName, 0, sizeof(coderName));
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

	struct DecoderParams : CoderParams {
	};

	enum CoderType {
		Encoder,
		Decoder
	};

	struct CoderParams {
		CoderType type;
		union paramsencdec
		{
			char coderName[16];
			EncoderParams enc;
		};	

		paramsencdec params;

		CoderParams(CoderType type) : type(type) {
            std::memset(&params, 0, sizeof(params));
		}

		CoderParams(const EncoderParams& enc) {
			type = Encoder;
			params.enc = enc;
			std::copy(std::begin(enc.coderName), std::end(enc.coderName), std::begin(params.coderName));
		}
	};

	typedef std::function<AudioCoder * (const AudioCoder::CoderParams& params)> Factory;

	AudioCoder(const EncoderParams &params);
	virtual ~AudioCoder();
	
	virtual int getBlockSize() { return 1024 * 8; }; // just choose large block size we wont reach with audio hardware

	virtual int getHeader(uint8_t *outBuffer, int bufferLen) const {
		return 0;
	}

	virtual int encodeInterleaved(const float* samples, int numSamples, uint8_t *buffer, int bufferLen) = 0;
	virtual void decodeInterleaved(const uint8_t *buffer, int bufferLen, float *samples, int numFrames) = 0;


	virtual size_t getRequiredBinaryBufferSize();


	inline int encodeInterleaved(std::vector<float> samples, uint8_t *buffer, int bufferLen) {
		return encodeInterleaved(samples.data(), (int)samples.size(), buffer, bufferLen);
	}

protected:
	CoderParams params;
    // float -> int16
    void sample_copy_float_to_int16(int16_t *dst, const float *src, unsigned long nsamples, unsigned long dst_skip);

};
