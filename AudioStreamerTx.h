#pragma once
#include "AudioStreamer.h"

class AudioCoder;

class AudioStreamerTx :
	public AudioStreamer
{
public:
	AudioStreamerTx(LLLinkGenerator &linkGen, const Discovery::NodeDevice &nd, int port);
	~AudioStreamerTx();

protected:
	void streamingThread();

private:
	uint16_t m_frameIndex;
	AudioCoder *m_encoder;
	uint8_t m_linkBuffer[2048];
};

