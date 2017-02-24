#include "AudioStreamerDecoding.h"

#include "UlltraProto.h"

/*
#include <sndfile.hh>
SndfileHandle fi1("AudioStreamerDecoding_1.wav", SFM_WRITE, SF_FORMAT_WAV | SF_FORMAT_PCM_16, 2, 44100);
SndfileHandle fi2("AudioStreamerDecoding_2.wav", SFM_WRITE, SF_FORMAT_WAV | SF_FORMAT_PCM_16, 2, 44100);
*/

AudioStreamerDecoding::AudioStreamerDecoding(const Params &streamInfo, AudioCoder::Factory coderFactory)
	: AudioStreamerCoding(streamInfo, coderFactory)
{
	decoderSampleBuf.resize(2048 * 8);
	stageBufferPos = 0;
	recoverFromUnderRun = false;
	recoverFromOverRun = false;
	delaySamples = 20 * 1000;
	lastBinaryFrameIndex = -1;

	atomic_numTotalSamplesIn = 0;
	atomic_numTotalSamplesOut = 0;

	int fill = delaySamples * streamInfo.coderParams.numChannels;
	for (int i = 0; i < fill; i++) {
		queue.enqueue(0);
	}	
}

bool AudioStreamerDecoding::outputInterleaved(float *samples, unsigned int numSamplesPerChannel, int numChannels, StreamTime time)
{
	// this is pulled by audio driver
	// even if our buffer is underrun, we will always return some samples (either silence, or drop-out concealment)

	if (stopped())
		return false;

	int totalSamples = numSamplesPerChannel * numChannels;
	if (totalSamples != stageBuffer.size()) {
		if (stageBuffer.size() == 0) // init stage buffer on first output
			stageBuffer.resize(totalSamples);
		else {
			LOG(logERROR) << "AudioStreamerDecoding::outputInterleaved: stageBuffer size mismatch!";
			return false;
		}
	}


	auto stageBuf = stageBuffer.data();
	auto stageBufSize = stageBuffer.size();


	if (queue.size_approx() > 2 * delaySamples * numChannels) {
		LOG(logWARNING) << "Overrun! (" << queue.size_approx() << " > " << (delaySamples * numChannels) << ")";

		// flush
		int over = queue.size_approx() / numChannels - (2*delaySamples);
		float items[16];

		for (int i = 0; i < over; i++) {
			if (queue.try_dequeue_bulk(items, numChannels) != numChannels) {
				LOG(logERROR) << "Dropping failed!";
			}
		}
	}


	// TODO: make try_deq & numSamplesSynthPerChannel += x atomic

	int numDeq = queue.try_dequeue_bulk(&stageBuf[stageBufferPos], stageBufSize - stageBufferPos);
	

	stageBufferPos += numDeq;

	if (stageBufferPos == stageBufSize) {
		// yeah, we got the block
		memcpy(samples, stageBuf, stageBufSize * sizeof(*stageBuf));
		stageBufferPos = 0;
	}
	else
	{ 
		LOG(logWARNING) << "UNRRUN";
		// underrun / underflow :(
		// TODO: error concealment
		// the silence samples can be considered as a synth since there were "added" to our real stream
		atomic_numSamplesSynthPerChannel += (stageBufSize - stageBufferPos) / numChannels;

		// underrun!
		recoverFromUnderRun = true;

		// for now we just write-out the samples we have, then silcene TODO		
		memcpy(samples, stageBuf, stageBufferPos * sizeof(*stageBuf));

		for (int i = stageBufferPos; i < stageBufSize; i++) {
			samples[i] = 0;
		}
	}

	
	

	stats10Seconds.numSamplesPerChannelOut += numSamplesPerChannel;
	atomic_numTotalSamplesOut += numSamplesPerChannel;


	return true;
}



void AudioStreamerDecoding::codingThread() {
	recoverFromOverRun = false;
	recoverFromUnderRun = false;

	stats10Seconds.numSamplesPerChannelIn = 0;
	stats10Seconds.numSamplesPerChannelOut = 0;

	atomic_numSamplesSynthPerChannel = 0;

	auto buf = binaryBuffer.data();
	auto bufLen = binaryBuffer.size();

	auto lastRead = UP::getWallClockSeconds();

	while (!stopped()) {
		int readBytes = pullSource(buf, bufLen);

		auto now = UP::getWallClockSeconds();

		if (readBytes > 0) {
			lastRead = now;
			_inputBinary(buf, readBytes);
		}
		else {
			if ((now - lastRead) > 4) {
				LOG(logWARNING) << "Aborting AudioStreamerDecoding after no input for 4 seconds!";
				stop();
			}
			std::this_thread::yield(); 
		}
	}
}



bool AudioStreamerDecoding::_inputBinary(const uint8_t *buffer, int len)
{
	auto numChannels = getParams().coderParams.numChannels;
	auto sbuf = decoderSampleBuf.data();
	auto maxPerChannel = decoderSampleBuf.size() / numChannels;

	auto bufPos = 0;

	// read frame timing
	if (getParams().coderParams.withTiming) {
		uint8_t frameIndex = buffer[0]; bufPos++;

		uint8_t expectedFrameIndex = (lastBinaryFrameIndex + ((uint8_t)1));
		if (expectedFrameIndex != frameIndex) {
			LOG(logWARNING) << "Unexpected frameIndex in binary stream, expected " << ((int)expectedFrameIndex) << ", got " << ((int)frameIndex);
			// TODO: should reset current stats
		}

		lastBinaryFrameIndex = frameIndex;

		if (frameIndex == 0) {
			uint32_t streamClock = *reinterpret_cast<const uint32_t*>(&buffer[bufPos]);
			bufPos += sizeof(uint32_t);
		}
	}

	if (len == bufPos) {
		LOG(logWARNING) << "Received packet with empty payload, ignoring! (len" << len << ")";
		return true;
	}

	int decodedSamplesPerChannel = coder->decodeInterleaved(buffer + bufPos, len - bufPos, sbuf, maxPerChannel);

	if (decodedSamplesPerChannel < 0) {
		LOG(logERROR) << "Stream decoding failed!";
		return false;
	}

		
	_statsInput(decodedSamplesPerChannel);

	auto dropPerChannel = atomic_numSamplesSynthPerChannel.load();

	if (dropPerChannel > 0)
	{
		// drop samples if we previously inserted synth
		if (dropPerChannel >= decodedSamplesPerChannel) {
			atomic_numSamplesSynthPerChannel -= decodedSamplesPerChannel;
			return true; // drop whole frame
		}

		queue.enqueue_bulk(&sbuf[dropPerChannel*numChannels], (decodedSamplesPerChannel - dropPerChannel) * numChannels);
		atomic_numSamplesSynthPerChannel -= dropPerChannel;
	}
	else
	{
		queue.enqueue_bulk(sbuf, decodedSamplesPerChannel * numChannels);
	}

	if (queue.size_approx() > getParams().coderParams.sampleRate * numChannels * 12) {
		LOG(logERROR) << "Decoding queue overflow!";
		return false;
	}

	
	
	return true;
}


void AudioStreamerDecoding::_statsInput(int decodedSamplesPerChannel)
{
	stats10Seconds.numSamplesPerChannelIn += decodedSamplesPerChannel;

	atomic_numTotalSamplesIn += decodedSamplesPerChannel;

	if (stats10Seconds.numSamplesPerChannelIn >= (getParams().coderParams.sampleRate * 10)) {
		history10Seconds.push_back(stats10Seconds);
		stats10Seconds.reset();

		if (history10Seconds.size() > 6) {
			history10Seconds.pop_front();
		}

		streamStats recentStats;
		for (auto &s : history10Seconds) {
			recentStats.numSamplesPerChannelIn += s.numSamplesPerChannelIn;
			recentStats.numSamplesPerChannelOut += s.numSamplesPerChannelOut;
		}

		double ratio = ((double)recentStats.numSamplesPerChannelIn / (double)recentStats.numSamplesPerChannelOut);
		LOG(logWARNING) << "fs ratio = " << ratio;
		ratio = ((double)atomic_numTotalSamplesIn) / ((double)atomic_numTotalSamplesOut);
		LOG(logWARNING) << "fs ratio2 = " << ratio;
	}
}