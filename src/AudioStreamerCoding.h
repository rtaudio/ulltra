#pragma once

#include<functional>

#include "audio/AudioCoder.h"

typedef std::function<bool(const uint8_t *buffer, int bufferLen, int numSamples)> BinaryAudioStreamPump;

typedef float DefaultSampleType;

class RttThread;

typedef uint32_t StreamTime;

class AudioStreamerCoding
{
	// no copy
	AudioStreamerCoding(AudioStreamerCoding&) = delete;
	AudioStreamerCoding& operator=(AudioStreamerCoding&) = delete;

public:
	struct Params {
		char deviceId[128];
		bool async;


		AudioCoder::CoderParams coderParams;

		Params(const std::string &deviceId_, const AudioCoder::CoderParams &coderParams) : async(false), coderParams(coderParams) {
			memset(deviceId, 0, sizeof(deviceId));
			deviceId_.copy(deviceId, sizeof(deviceId) - 1);
		}

		bool operator==(const Params &other) const
		{
			return (strcmp(deviceId, other.deviceId) == 0
				&& async == other.async
				&& (coderParams.isEqual(other.coderParams)));
		}
	};


	struct SampleBlock {

	};

	AudioStreamerCoding(Params streamInfo, AudioCoder::Factory coderFactory);

	~AudioStreamerCoding();


	virtual bool inputInterleaved(DefaultSampleType *samples, uint32_t numFrames, int numChannels, StreamTime time = 0) {
		throw std::runtime_error("inputInterleaved not implemented");
	};
	virtual bool outputInterleaved(DefaultSampleType *samples, uint32_t numFrames, int numChannels, StreamTime time = 0) {
		throw std::runtime_error("outputInterleaved not implemented");
	}

	inline bool stopped() const { return hasStopped; }

	void start();

	inline const Params &getParams() const { return params; }

	void notifyXRun();

private:
	Params params;

	RttThread * thread;	
	
	bool hasStopped;
	bool energySaving;


	virtual void codingThread() = 0;

	

protected:
	AudioCoder *coder;
	std::vector<uint8_t> binaryBuffer;

	void stop() { hasStopped = true;  }


	void waitPeriod();
};


namespace std {
	template <>
	struct hash<AudioStreamerCoding::Params> : public std::unary_function<const AudioStreamerCoding::Params &, std::size_t>
	{
		inline std::size_t operator()(const AudioStreamerCoding::Params & obj) const
		{
			const unsigned char* p = reinterpret_cast<const unsigned char*>(&obj);
			std::size_t h = 2166136261;
			for (unsigned int i = 0; i < sizeof(obj); ++i)
				h = (h * 16777619) ^ p[i];
			return h;
		}
	};
}