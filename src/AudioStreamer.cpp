#include "AudioStreamer.h"
#include "audio/AudioIOStream.h"
#include "audio/AudioCoder.h"
#include <rtt/rtt.h>
#include "autil/file_io.h"

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





unsigned long upper_power_of_two(unsigned long v)
{
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;

}



AudioCodingStream::AudioCodingStream(Params streamInfo, AudioCoder::Factory coderFactory)
	:coder(nullptr), hasSinksToAdd(false), hasStopped(false), params(streamInfo)
{
	AudioCoder::CoderParams codeParams(AudioCoder::Encoder);
	codeParams.params.enc = params.encoderParams;
	coder = coderFactory(codeParams);

	if (coder == nullptr) {
		throw std::runtime_error("Failed to create " + std::string(codeParams.params.coderName) + " coder!");
	}

    size_t bufSize = upper_power_of_two(1024*2 + coder->getRequiredBinaryBufferSize());
    if(bufSize > 1024*1024*4)
        throw std::runtime_error("Coder requires more than 4 MB frame buffer, thats too much!");
    binaryBuffer.resize(bufSize);

	if (streamInfo.async) {
		async.sampleBuffer.resize(coder->getBlockSize() * params.encoderParams.numChannels);
		async.thread = new RttThread([this]() { codingThread(); }, true);
	}
	else {
		async.thread = nullptr;
	}
}

AudioCodingStream::~AudioCodingStream()
{
	hasStopped = true;
	if (coder)
		delete coder;

	if (async.thread)
		delete async.thread;
}


void AudioCodingStream::codingThread() {
    RttThread::GetCurrent().SetName(("coding-thread"));
	auto buf = async.sampleBuffer.data();
	auto bufSize = async.sampleBuffer.size();
    auto lastDeq = UP::getWallClockSeconds();

	while (!hasStopped) {
		auto numSamplesAllChannels = async.queue.try_dequeue_bulk(buf, bufSize);
        auto now = UP::getWallClockSeconds();
		if (numSamplesAllChannels > 0) {
			if ((numSamplesAllChannels % params.encoderParams.numChannels) != 0) {
				LOG(logWARNING) << "Async coding stream dequeued unaligned sample count!";
			}
            if(!_inputInterleaved(buf, numSamplesAllChannels / params.encoderParams.numChannels, params.encoderParams.numChannels)) { // TODO time
                hasStopped = true;
            }
            lastDeq = now;
        } else {
            if((now - lastDeq) > 4) {
                LOG(logWARNING) << "Aborting AudioCodintStream after no input for 4 seconds!";
                hasStopped = true;
            }
            std::this_thread::yield();
        }
	}
}

bool AudioCodingStream::inputInterleaved(float *samples, unsigned int numFrames, int numChannels, double time)
{
    if(hasStopped)
            return false;

	if (params.async) {
		// timeout of 4 seconds
		if (async.queue.size_approx() > (numChannels * params.encoderParams.sampleRate * 4)) {
			return false;
		}
		return async.queue.enqueue_bulk(samples, numFrames * numChannels);
	}
	else
	{
		return _inputInterleaved(samples, numFrames, numChannels, time);
	}
}

bool AudioCodingStream::_inputInterleaved(float *samples, unsigned int numFrames, int numChannels, double time)
{
//	autil::fileio::writeWave();
	if (hasStopped)
		return false;

	auto buf = binaryBuffer.data();
	
	int outBytes = coder->encodeInterleaved(samples, numFrames, buf, binaryBuffer.size());
	if (outBytes < 0) {
		hasStopped = true;
        LOG(logERROR) << "Stopping streamer after encoder error!";
		return false;
	}

    if(outBytes > binaryBuffer.size()) {
        LOG(logERROR) << "Encoder output is longer than buffer, heap might be corrupted, exit!";
        exit(1);
    }

	



	if (hasSinksToAdd) {
		for (auto &s : addSinks) {
			sinks.push_back(s);
		}
		addSinks.clear();
		hasSinksToAdd = false;
	}
	
	if (outBytes > 0) {
		// push to sinks, remove those that return false
		for (auto it = sinks.begin(); it != sinks.end();) {
			bool res = (*it)(buf, outBytes, numFrames);
			if (!res) {
                LOG(logDEBUG) << "Removing sink after write failure!";
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
	}

    // keep running as long as we have sinks (delay for 4 seconds)
    if(sinks.size() > 0 || (time - timeLastPushedToASink) < 4)
		return true;
	else {
        LOG(logDEBUG) << "Stopping streamer after 4 seconds of no successful sink write!";
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