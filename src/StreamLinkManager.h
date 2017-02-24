#pragma once

#include <string>

class JsonHttpServer;
class AudioIOManager;

#include "audio/AudioCoder.h"
#include <masaj/JsonHttpServer.h>

class LLink;
class AudioStreamerCoding;
class Controller;

class StreamLinkManager
{
public:
	StreamLinkManager(Controller &controller);
	~StreamLinkManager();

	struct NetStreamHandle {
		int port;
		bool isMaster;
		LLink *link;
		AudioStreamerCoding *streamer;
	};
	
private:
	Controller &controller;
	AudioIOManager &audioManager;

    std::vector<NetStreamHandle> activeStreams_;

	/*
	struct MasterPortHandle {
		MasterPortHandle(MasterPortHandle&) = delete;
		MasterPortHandle& operator=(MasterPortHandle&) = delete;

		StreamLinkManager *mgr;

		MasterPortHandle(StreamLinkManager *mgr) : mgr(mgr) {
		}
		~MasterPortHandle(StreamLinkManager *mgr);
	};*/

	int getAvailableMasterPort();

	void startStreamCapture(const StreamEndpointInfo &captureFrom, const AudioCoder::CoderParams &encParams, const std::string &linkName, const SocketAddress &sa);


	int findSuitableCaptureSampleRate(std::vector<int> capture, std::vector<int> playback);
};

