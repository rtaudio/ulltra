#pragma once

#include "UlltraProto.h"

#include <vector>

#include "concurrentqueue/concurrentqueue.h"

#include "AudioStreamerCoding.h"

class AudioStreamerEncoding : public AudioStreamerCoding
{
public:
	AudioStreamerEncoding(const Params &streamInfo, AudioCoder::Factory coderFactory);
	//~AudioStreamerEncoding();

	// Controller API:
	void addSink(const BinaryAudioStreamPump &pump);	

	// Audio API:
	bool inputInterleaved(float *samples, unsigned int numFrames, int numChannels, StreamTime time = 0.0);


	
private:
	moodycamel::ConcurrentQueue<DefaultSampleType> queue;

	void codingThread();

	bool encodeInterleaved(float *samples, unsigned int numFrames, int numChannels, StreamTime time = 0.0);

	std::vector<BinaryAudioStreamPump> sinks, addSinks;
	volatile bool hasSinksToAdd;
	double timeLastPushedToASink;

	volatile uint32_t streamStartTime;
	bool firstInput;

	uint8_t encodedFrameIndex;

	bool encoderBuffered;
	StreamTime encoderBufferTime;
};