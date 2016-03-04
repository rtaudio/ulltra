#include "AudioDriverStream.h"


const AudioStreamInfo AudioStreamInfo::invalid;


AudioDriverStream::AudioDriverStream()
{
}


AudioDriverStream::~AudioDriverStream()
{
}

void AudioDriverStream::setBlockSize(int blockSize) {
	m_asi.blockSize = blockSize;
}

void AudioDriverStream::setSampleRate(int sampleRate) {
	m_asi.sampleRate = sampleRate;
}

void AudioDriverStream::setNumChannels(int channels) {
	m_asi.channels = channels;
}

void AudioDriverStream::setQuantizationFormat(AudioStreamInfo::Quantization q) {
	m_asi.quantizationFormat = q;
}


bool AudioDriverStream::start() {
	return true;
}


void AudioDriverStream::addBlockInterleaved(void *block, int nFrames) {
	if (nFrames != m_asi.blockSize)
		return;
}

void AudioDriverStream::addBlockSilemce(int nFrames) {
	if (nFrames != m_asi.blockSize)
		return;
}


void AudioDriverStream::notifyXRun() {
	m_nXruns++;
}


void AudioDriverStream::end()
{

}
