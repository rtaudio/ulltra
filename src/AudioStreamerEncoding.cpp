#include "AudioStreamerEncoding.h"
#include <rtt/rtt.h>

AudioStreamerEncoding::AudioStreamerEncoding(const Params &streamInfo, AudioCoder::Factory coderFactory)
	: AudioStreamerCoding(streamInfo, coderFactory)
	, hasSinksToAdd(false)
	, firstInput(true)
	, encodedFrameIndex(0)
	, encoderBuffered(false)
	, encoderBufferTime(0)
{
}


void AudioStreamerEncoding::codingThread() {
	std::vector<DefaultSampleType> sampleBuffer;
	auto bufSize = coder->getBlockSize() * getParams().coderParams.numChannels;
	sampleBuffer.resize(bufSize);
	auto buf = sampleBuffer.data();	
	int bufPos = 0;

	
	uint32_t consumeClock = 0;

    auto lastDeq = UP::getWallClockSeconds();

	// TODO: should make this monotonic!

	uint64_t blockSamplesPerChannel = coder->getBlockSize();

	std::function<bool()> wait;
	int periodNs = ((uint64_t)1e9) * blockSamplesPerChannel / (uint64_t)getParams().coderParams.sampleRate;
	RttTimer timer(&wait, periodNs);

	// wait for first sample
	while (!stopped()) {
		bufPos += queue.try_dequeue_bulk(&buf[bufPos], bufSize - bufPos);
		if (bufPos > 0)
			break;
	}

	// streamStartTime is the device clock of first sample of the stream
	uint32_t clock = streamStartTime;

	while (!stopped()) {
		
        auto now = UP::getWallClockSeconds();
		if (bufPos == bufSize) {
			auto samplesPerChannel = bufSize / getParams().coderParams.numChannels;
            if(!encodeInterleaved(buf, samplesPerChannel, getParams().coderParams.numChannels, clock)) { // TODO time
				stop();
            }
			bufPos = 0;
            lastDeq = now;

			/*

			if (!wait()) {
				LOG(logERROR) << "Aborting AudioStreamerEncoding after timer failure!";
				stop();
			}
			*/

			clock += samplesPerChannel;
        } else {
            if((now - lastDeq) > 4) {
                LOG(logWARNING) << "Aborting AudioCodintStream after no input for 4 seconds!";
				stop();
            }

			// send out samples as soon as we have a full block, TODO check if this is monotonic
			std::this_thread::yield();
        }

		bufPos += queue.try_dequeue_bulk(&buf[bufPos], bufSize - bufPos);
	}
}

bool AudioStreamerEncoding::inputInterleaved(DefaultSampleType *samples, uint32_t numFrames, int numChannels, StreamTime streamClock)
{
    if(stopped())
            return false;

	if (firstInput) {
		streamStartTime = streamClock;
		firstInput = false;
	}

	if (getParams().async) {
		// timeout of 4 seconds
		if (queue.size_approx() > (numChannels * getParams().coderParams.sampleRate * 4)) {
			return false;
		}
		return queue.enqueue_bulk(samples, numFrames * numChannels);
	}
	else
	{
		return encodeInterleaved(samples, numFrames, numChannels, streamClock);
	}
}

bool AudioStreamerEncoding::encodeInterleaved(float *samples, unsigned int numFrames, int numChannels, StreamTime streamTime)
{
	if (stopped())
		return false;

	const auto buf = binaryBuffer.data();
	const auto bufSize = binaryBuffer.size();

	size_t bufPos = 0;
	auto bufAvailable = bufSize;

	if(!encoderBuffered)
		encoderBufferTime = streamTime;

	if (hasSinksToAdd) {
		if (getParams().coderParams.withTiming) {
			buf[0] = encodedFrameIndex + 255; // -1
			bufPos++; bufAvailable--;
			if (buf[0] == 0) {
				*reinterpret_cast<uint32_t*>(&buf[bufPos]) = encoderBufferTime;
				bufPos += sizeof(uint32_t); bufAvailable -= sizeof(uint32_t);
			}
		}
		int headerLen = coder->getHeader(&buf[bufPos], bufAvailable);
		// dont update bufPos,bufAvailable because we will overwrite
		for (auto &s : addSinks) {
			sinks.push_back(s);
			if (headerLen > 0) {
				bool res = s(buf, bufPos + headerLen, 0);
				if (!res) {
					LOG(logERROR) << "Failed to send stream header to sink!";
				}
			}
		}

		addSinks.clear();
		hasSinksToAdd = false;

		bufPos = 0;
		bufAvailable = bufSize;
	}


	// write 1 or 5 sync bytes
	if (getParams().coderParams.withTiming) {
		buf[0] = encodedFrameIndex;
		bufPos++; bufAvailable--;

		// write stream time after 255 frames
		if (buf[0] == 0) {
			*reinterpret_cast<uint32_t*>(&buf[bufPos]) = encoderBufferTime;
			bufPos += sizeof(uint32_t); bufAvailable -= sizeof(uint32_t);
		}
	}

	
	int outBytes = coder->encodeInterleaved(samples, numFrames, buf + bufPos, bufAvailable);
	if (outBytes < 0) {
		stop();
        LOG(logERROR) << "Stopping streamer after encoder error!";
		return false;
	}


	bufPos += outBytes;
	bufAvailable -= outBytes;

    if(bufPos > bufSize) {
        LOG(logERROR) << "Encoder output is longer than buffer, heap might be corrupted, exit!";
        exit(1);
    }

	auto now = UP::getWallClockSeconds();
	
	if (outBytes > 0) {
		// push to sinks, remove those that return false
		for (auto it = sinks.begin(); it != sinks.end();) {
			bool res = (*it)(buf, bufPos, numFrames);
			if (!res) {
                LOG(logDEBUG) << "Removing sink after write failure!";
				it = sinks.erase(it);
				if (sinks.size() == 0) {
					break;
				}
			}
			else {
				timeLastPushedToASink = now;
				it++;
			}
		}
		encodedFrameIndex++;
		encoderBuffered = false;
	}
	else {
		encoderBuffered = true;
	}

    // keep running as long as we have sinks (delay for 4 seconds)
    if(sinks.size() > 0 || (now - timeLastPushedToASink) < 4)
		return true;
	else {
        LOG(logDEBUG) << "Stopping streamer after 4 seconds of no successful sink write!";
		stop();
		return false;
	}
}


void AudioStreamerEncoding::addSink(const BinaryAudioStreamPump &sink)
{
	if (stopped()) {
		throw std::runtime_error("Cannot add sink to stopped coding stream!");
	}

	int waitedMs = 0;
	while (hasSinksToAdd) {
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(1ms);
		waitedMs++;
		if (waitedMs > 2000) {
			throw std::runtime_error("Add sink coding stream timedout, audio I/O not polling!");
			stop();
		}
	}

	addSinks.push_back(sink);
	hasSinksToAdd = true;
}