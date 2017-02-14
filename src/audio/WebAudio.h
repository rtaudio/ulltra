#pragma once

#include <string>

class JsonHttpServer;
class AudioIOManager;

#include "AudioCoder.h"
#include "../masaj/JsonHttpServer.h"


class WebAudio
{
public:
	WebAudio(AudioIOManager &audioMgr);
	~WebAudio();

	void registerWithServer(JsonHttpServer &server);

private:
	JsonHttpServer *server;
	AudioIOManager &audioMgr;

    AudioCoder *createCoder(AudioCoder::EncoderParams &params, const std::string &userAgent, JsonHttpServer::StreamResponse &response) const;
};

