#pragma once

#include "audio/JackClient.h"
#include "audio/WindowsMM.h"

class AudioIOManager
{
private:
	std::map<std::string, AudioIO*> m_audioIOs;
	//std::map<std::string, AudioStreamInfo> m_availableStreams;
	std::map<std::string, AudioIOStream*> m_activeStreams;

public:
	AudioIOManager();
	~AudioIOManager();

	void update();

   AudioStreamCollection getAvailableStreams();


   AudioIOStream *stream(std::string const& id);
};

