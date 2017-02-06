#pragma once

class JsonHttpServer;
class AudioIOManager;

class WebAudio
{
public:
	WebAudio(AudioIOManager &audioMgr);
	~WebAudio();

	void registerWithServer(JsonHttpServer &server);

private:
	JsonHttpServer *server;
	AudioIOManager &audioMgr;
};

