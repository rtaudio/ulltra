#pragma once
#include "net/LLLink.h"

#include "audio/AudioIOStream.h"

class RttThread;
class AudioIOStream;

struct StreamFrameHeader {
	uint16_t streamToken;//a "unique" stream token constant over all frames in a stream to avoid routing and format errors.
	uint16_t frameIndex;//the index of the first sample within the frame

	union data
	{
		uint8_t firstByte;
	};

	data content;

	const int Overhead = sizeof(StreamFrameHeader) - sizeof(content);
};

class AudioStreamer
{
	friend class AudioIOManager;

public:
	AudioStreamer(LLLinkGenerator &linkGen, const Discovery::NodeDevice &nd, int port);
	~AudioStreamer();
	bool start(AudioIOStream *streamSource, int token);
	virtual void streamingThread() = 0;

	void update();
	bool isAlive();

protected:
	LLLink *m_link;
	AudioIOStream *m_aio;
	uint16_t m_token;
	bool m_isRunning;
	int m_numErrorsLink, m_numErrorsStream;

	int m_samplesPerFrame;

	void reportLinkError();
	void reportStreamError();

private:
	RttThread *m_thread;

	virtual bool inputInterleaved(float *samples, unsigned int numFrames, int numChannels, double time=0.0) = 0;
	virtual bool outputInterleaved(float *samples, unsigned int numFrames, int numChannels, double time=0.0) = 0;

	void notifyXRun();
};

