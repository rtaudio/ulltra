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
	/*
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
	*/

	return true;
}

void AudioStreamer::update() {
	// TODO
}


bool AudioStreamer::isAlive() {
	return true; // TODO
}




void AudioStreamer::reportLinkError() {}
void AudioStreamer::reportStreamError() {}





AudioCodingStream::AudioCodingStream(Info streamInfo, AudioCoder::Factory coderFactory)
	:coder(nullptr), hasSinksToAdd(false), hasStopped(false)
{
	AudioCoder::CoderParams params(AudioCoder::Encoder);
	params.params.enc = streamInfo.encoderParams;
	coder = coderFactory(params);

	if (coder == nullptr) {
		throw std::runtime_error("Failed to create " + std::string(params.params.coderName) + " coder!");
	}

	binaryBuffer.resize(1024 * 10);
}

AudioCodingStream::~AudioCodingStream()
{
	hasStopped = true;
	if (coder)
		delete coder;
}

#include "autil/file_io.h"

bool AudioCodingStream::inputInterleaved(float *samples, unsigned int numFrames, int numChannels, double time)
{
//	autil::fileio::writeWave();
	if (hasStopped)
		return false;

	auto buf = binaryBuffer.data();
	
	int outBytes = coder->encodeInterleaved(samples, numFrames, buf, binaryBuffer.size());
	if (outBytes < 0) {
		hasStopped = true;
		return false;
	}
		

	if (hasSinksToAdd) {
		for (auto &s : addSinks) {
			sinks.push_back(s);
		}
		addSinks.clear();
		hasSinksToAdd = false;
	}
	
	// push to sinks, remove those that return false
	for (auto it = sinks.begin(); it != sinks.end();) {
		bool res = (*it)(buf, outBytes, numFrames);
		if (!res) {
			it = sinks.erase(it);
			if (sinks.size() == 0) {
				break;
			}
		}
		else {
			timeLastPushedToASink = time;
			it++;
		}
	}

	// keep running as long as we have sinks (delay for 2 seconds)
	if(sinks.size() > 0 || (time - timeLastPushedToASink) < 2)
		return true;
	else {
		hasStopped = true;
		return false;
	}
}

bool AudioCodingStream::outputInterleaved(float *samples, unsigned int numFrames, int numChannels, double time)
{
	return false;
}

void AudioCodingStream::notifyXRun() {
	LOG(logWARNING) << "XRUN";
}

void AudioCodingStream::addSink(const BinaryAudioStreamPump &sink)
{
	if (hasStopped) {
		throw std::runtime_error("Cannot add sink to stopped coding stream!");
	}

	int waitedMs = 0;
	while (hasSinksToAdd) {
		usleep(1000 * 1);
		waitedMs++;
		if (waitedMs > 2000) {
			throw std::runtime_error("Add sink coding stream timedout, audio I/O not polling!");
		}
	}

	addSinks.push_back(sink);
	hasSinksToAdd = true;
}