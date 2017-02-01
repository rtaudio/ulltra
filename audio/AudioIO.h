#pragma once

#include<vector>
#include<functional>
#include <string>

class AudioStreamInfo;
class AudioIOStream;

struct AudioDeviceParams {
    std::string name;

    int numChannelsRecord;
    int numChannelsPlayback;

    std::vector<int> sampleRates;
    std::vector<int> blockSizes;

    int extraLatencyRecord;
    int extraLatencyPlayback;
};

/**
 *  The AudioIO is an abstract base class for (local) JACK/WindowsMM/ASIO/Portaudio/CoreAudio... instances
 *  It provides streams from/to devices.
 *  It also handles signal splitting, i.e. stream from a local capture endpoint to multiple network nodes.
 *  It keeps audio devices open as long there are one or more active streams, otherwise they are closed to save resources.
 */
class AudioIO
{
public:
	AudioIO();
	virtual ~AudioIO();

	virtual bool open() = 0;//open the underlying audio driver
	virtual void close() = 0;//close the underlying audio driver
	virtual void update();//periodic update the audio driver (e.g. detect new endpoints)

	void onLocalEndpointAdded(std::function<void(AudioStreamInfo const&)> handler);
	void onLocalEndpointRemoved(std::function<void(AudioStreamInfo const&)> handler);

	AudioIOStream *stream(std::string const& endpointId);

	AudioStreamInfo getEndpointStreamInfo(std::string const& endpointId);



protected:
	void endpointsAdd(std::vector<AudioStreamInfo> streams);
	void endpointsRemoved(std::vector<AudioStreamInfo> streams);

	std::vector<AudioIOStream*> getActiveStreams();
};

