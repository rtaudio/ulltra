#include "AudioIO.h"

#include "AudioIOStream.h"

AudioIO::AudioIO()
{
}


AudioIO::~AudioIO()
{
}


AudioIOStream *AudioIO::stream(std::string const& endpointId)
{
	return 0;
}

void AudioIO::update()
{

}

void AudioIO::onLocalEndpointAdded(std::function<void(AudioStreamInfo const&)> handler) {}
void AudioIO::onLocalEndpointRemoved(std::function<void(AudioStreamInfo const&)> handler) {}


AudioStreamInfo AudioIO::getEndpointStreamInfo(std::string const& endpointId) {
	return{};
}




void AudioIO::endpointsAdd(std::vector<AudioStreamInfo> streams) {}
void AudioIO::endpointsRemoved(std::vector<AudioStreamInfo> streams) {}

std::vector<AudioIOStream*> AudioIO::getActiveStreams() {
	return std::vector<AudioIOStream*>();
}