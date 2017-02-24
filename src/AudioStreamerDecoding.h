#pragma once

#include "concurrentqueue/concurrentqueue.h"

#include "AudioStreamerCoding.h"

#include <deque>

typedef std::function<int(uint8_t*buf, int max)> BinaryStreamRead;

class AudioStreamerDecoding : public AudioStreamerCoding
{
public:
	

	AudioStreamerDecoding(const Params &streamInfo, AudioCoder::Factory coderFactory);

	bool outputInterleaved(float *samples, unsigned int numFrames, int numChannels, StreamTime time = 0);

	void setSource(const BinaryStreamRead &&read) {
		pullSource = read;
	}

private:
	void codingThread();
	bool _inputBinary(const uint8_t *buffer, int len);

	void _statsInput(int decodedSamplesPerChannel);

private:
	BinaryAudioStreamPump pull;
	BinaryStreamRead pullSource;

	int delaySamples;

	moodycamel::ConcurrentQueue<DefaultSampleType> queue;
	std::vector<DefaultSampleType> stageBuffer;
	int stageBufferPos;

	bool recoverFromUnderRun, recoverFromOverRun;
	std::atomic<uint32_t> atomic_numSamplesSynthPerChannel;

	std::vector<DefaultSampleType> decoderSampleBuf;


	struct streamStatsAtomic {
		std::atomic<uint64_t> numSamplesPerChannelIn;
		std::atomic<uint64_t> numSamplesPerChannelOut;
		streamStatsAtomic() { reset(); }
		void reset() { numSamplesPerChannelIn = 0; numSamplesPerChannelOut = 0; }
	};

	struct streamStats {
		uint64_t numSamplesPerChannelIn;
		uint64_t numSamplesPerChannelOut;
		streamStats() : numSamplesPerChannelIn(0), numSamplesPerChannelOut(0) {}
		streamStats(const streamStatsAtomic &ssa) {
			numSamplesPerChannelIn = ssa.numSamplesPerChannelIn.load();
			numSamplesPerChannelOut = ssa.numSamplesPerChannelOut.load();
		}
	};


	streamStatsAtomic stats10Seconds;
	std::deque<streamStats> history10Seconds;
	std::atomic<uint64_t > atomic_numTotalSamplesIn, atomic_numTotalSamplesOut;


	uint8_t lastBinaryFrameIndex;
};
