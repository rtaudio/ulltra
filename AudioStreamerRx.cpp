#include "AudioStreamerRx.h"

#include "OrderingBuffer.h"
#include "audio/AudioCoder.h"

AudioStreamerRx::AudioStreamerRx(LLLinkGenerator &linkGen, const Discovery::NodeDevice &nd, int port)
	: AudioStreamer(linkGen, nd, port)
{
	
}


AudioStreamerRx::~AudioStreamerRx()
{
}

void AudioStreamerRx::streamingThread()
{
	int dataLen = 0;
	const uint8_t *data;

	while (m_isRunning)
	{
		data = m_link->receive(dataLen);
		if (dataLen < sizeof(StreamFrameHeader)) {
			reportLinkError();
			continue;
		}

		auto sfh = reinterpret_cast<const StreamFrameHeader*>(data);

		// validate stream token
		if (sfh->streamToken != m_token) {
			reportStreamError();
			continue;
		}

		// compute actual size of encoded data bytes
		int contentLen = dataLen - sfh->Overhead;

		// decode data directly into the ordering buffer
		auto targetBlock = m_ordering->getBlockPointers(sfh->frameIndex);
		//m_decoder->decode(&sfh->content.firstByte, contentLen, targetBlock.samples, targetBlock.size);
		targetBlock.commit(); // make the decoded block available for audio rendering (playback or resampler)
	}
}