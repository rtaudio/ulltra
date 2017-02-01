#pragma once
#include "AudioStreamer.h"

class AudioStreamDecoder;
class OrderingBuffer;

class AudioStreamerRx :
	public AudioStreamer
{
public:
	AudioStreamerRx(LLLinkGenerator &linkGen, const Discovery::NodeDevice &nd, int port);
	~AudioStreamerRx();

private:
	AudioStreamDecoder *m_decoder;
	OrderingBuffer *m_ordering;

	void streamingThread();
};

