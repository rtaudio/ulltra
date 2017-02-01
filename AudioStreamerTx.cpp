#include "AudioStreamerTx.h"

#include <rtt.h>
#include "AudioStreamEncoder.h"


AudioStreamerTx::AudioStreamerTx(LLLinkGenerator &linkGen, const Discovery::NodeDevice &nd, int port)
	: AudioStreamer(linkGen, nd, port)
{
	memset(m_linkBuffer, 0, sizeof(m_linkBuffer));
}


AudioStreamerTx::~AudioStreamerTx()
{
}


void AudioStreamerTx::streamingThread()
{
	/*

	sending:
	audio driver writes to the internal buffer of the AudioIOStream (m_aio).
	here we call blockRead() to get the samples
	each audiostreamer

	currently fixed: send stream frame every 1ms
	- this gives low latency, keeps datagram size low (e.g. for 48Khz stereo max. 48*2*4byte = 384byte)
	- on windows, lowest periodic timer interval is 1ms
	- in case of single datagram loss only need to recover 1ms
	*/

	//m_aio->blockRead()
	//RttTimer t(1000 * 1000);

	while (m_isRunning) {
		//t.wait(); // TODO put sleep?

		reinterpret_cast<StreamFrameHeader*>(m_linkBuffer)->streamToken = m_token;
		reinterpret_cast<StreamFrameHeader*>(m_linkBuffer)->frameIndex = m_frameIndex;
		m_frameIndex++;


		// try to read full frame until success
		int waiting = 0;
		std::vector<float *> samples;
		while (!m_aio->blockRead(samples, m_samplesPerFrame)) {
			usleep(1);
			waiting++;

			// wait on audio driver for 2e6 us = 2 s, then stop streaming
			if (waiting > 2e6) {
				m_isRunning = false;
				break;
			}
		}

		int dataLen = m_encoder->encode(samples, m_linkBuffer, sizeof(m_linkBuffer));
		if (dataLen == 0) {
			m_numErrorsLink++;
			continue;
		}

		if (!m_link->send((uint8_t*)m_linkBuffer, dataLen)) {
			m_numErrorsStream++;
		}
	}
}