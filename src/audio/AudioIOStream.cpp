#include "AudioIOStream.h"


const AudioStreamInfo AudioStreamInfo::invalid = AudioStreamInfo();


AudioIOStream::AudioIOStream(AudioStreamInfo const& info)
{
}

AudioIOStream::AudioIOStream(AudioIOStream *masterStream)
{

}


AudioIOStream::~AudioIOStream()
{
}

void AudioIOStream::blockWriteInterleaved(float *block, int channels, int nFrames) {
	if (nFrames != m_asi.blockSize)
		return;
}

void AudioIOStream::blockWriteSilence(int nFrames) {
	if (nFrames != m_asi.blockSize)
		return;
}


void AudioIOStream::notifyXRun() {
	m_nXruns++;
}

void AudioIOStream::blockWrite(std::vector<float *> const& channelBuffers, int nFrames)
{

}

bool AudioIOStream::blockRead(std::vector<float *> const& channelBuffers, int nFrames)
{
	return false;
}