#include "AudioStreamer.h"
#include "audio/AudioIOStream.h"
#include "audio/AudioCoder.h"
#include <rtt/rtt.h>

#define MIN_SAMPLES_PER_FRAME 60


AudioStreamer::AudioStreamer(LLLinkGenerator &linkGen, const Discovery::NodeDevice &nd, int port)
	: m_link(0), m_thread(0), m_aio(0), m_samplesPerFrame(0)
{
	m_link = linkGen();
}


AudioStreamer::~AudioStreamer()
{
	if(m_thread)
		delete m_thread;
	delete m_link;
}


bool AudioStreamer::start(AudioIOStream *streamSource, int token)
{
	if (!m_link) {
		LOG(logERROR) << "Failed to start stream, link creation failed!";
		return false;
	}

	//  we have a timer with 1ms accuracy, so chose the smallest possible frame size based on samples/ms+10%
	int samplesPerMs = streamSource->getInfo().sampleRate / 1000;
	m_samplesPerFrame = (std::max)(samplesPerMs + samplesPerMs / 10, MIN_SAMPLES_PER_FRAME);
	
	m_token = token;

	m_thread = new RttThread([this]() {
		streamingThread();
	}, true);
}

void AudioStreamer::update() {
	// TODO
}


bool AudioStreamer::isAlive() {
	return true; // TODO
}




void AudioStreamer::reportLinkError() {}
void AudioStreamer::reportStreamError() {}


void AudioStreamer::notifyXRun() {

}