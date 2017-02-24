#include <chrono>
#include <thread>

#include <rtt/rtt.h>
#include <pclog/pclog.h>

#include "AudioStreamerCoding.h"
#include "audio/AudioCoder.h"

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

AudioStreamerCoding::AudioStreamerCoding(Params streamInfo, AudioCoder::Factory coderFactory)
	: params(streamInfo), thread(nullptr), hasStopped(false) {
	energySaving = false;

	coder = coderFactory(streamInfo.coderParams);

	if (coder == nullptr) {
		throw std::runtime_error("Failed to create " + std::string(streamInfo.coderParams.coderName) + " coder!");
	}

	int blockSize = coder->getBlockSize();
	if (blockSize <= 0) blockSize = 1024 * 4; // fallback

	size_t bufSize = upper_power_of_two(1024 * 4 + coder->getRequiredBinaryBufferSize());
	if (bufSize > 1024 * 1024 * 4)
		throw std::runtime_error("Coder requires more than 4 MB frame buffer, thats too much!");
	binaryBuffer.resize(bufSize);

	
}

void AudioStreamerCoding::start()
{
	if (params.async) {
		if (thread)
			throw std::runtime_error("already started");

		thread = new RttThread([this]() {
			RttThread::GetCurrent().SetName(("coding-thread"));
			codingThread();
		}, true);
	}
}

AudioStreamerCoding::~AudioStreamerCoding() {
	hasStopped = true;
	if (coder)
		delete coder;

	if (thread)
		delete thread;
}

void AudioStreamerCoding::notifyXRun() {
	LOG(logWARNING) << "XRUN";
}



void AudioStreamerCoding::waitPeriod() {
//#if ENERGY_SAVING
		//using namespace std::chrono_literals;
		//auto halfFramePeriod = 1000ms * coder->getBlockSize() / getParams().coderParams.sampleRate / 2;
		//std::this_thread::sleep_for(halfFramePeriod);
//#else
		std::this_thread::yield();
//#endif
}