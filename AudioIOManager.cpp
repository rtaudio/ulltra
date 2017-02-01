#include "AudioIOManager.h"

#include "audio/AudioIO.h"

AudioIOManager::AudioIOManager()
{

	std::vector<AudioIO*> aios;
#ifdef WITH_JACK
	aios.push_back(new JackClient());
#endif

#ifdef _WIN32
	aios.push_back(new WindowsMM());
#endif

	// init audio IOs and either add or delete them
	for (auto &aio : aios)
	{
		/* // TODO
		aio->onLocalEndpointAdded([this](AudioIOStream *aios) {
			m_audioIOs[aios->getName()] = aios;
		});

		aio->onLocalEndpointRemoved([this](AudioIOStream *aios) {
			auto w = m_audioIOs.find(aios->getName());
			if (w != m_audioIOs.end())
				m_audioIOs.erase(w);
		});

		// try to open all audio IOs, delete failing ones
		if (!aio->open())
		{
			delete aio;
		}
		else
		{
			m_audioIOs.push_back(aio);
		}
		*/
	}
}


AudioIOManager::~AudioIOManager()
{
	for (auto io : m_audioIOs) {
		delete io.second;
	}
}

void AudioIOManager::update()
{
	for (auto s : m_audioIOs) {
		s.second->update();
	}
}

AudioIOStream *AudioIOManager::stream(std::string const& id)
{
	auto sp = id.find('.');
	if (sp == std::string::npos || sp == 0 || sp == (id.length()-1)) {
		LOG(logERROR) << " requested start invalid stream " << id;
		return NULL;
	}

	// split the id
	auto ioID = id.substr(0, sp);
	auto endpointID = id.substr(0, sp);

	// find IO
	auto aioi = m_audioIOs.find(ioID);
	if (aioi == m_audioIOs.end()) {
		LOG(logERROR) << " requested start of unknown IO " << ioID;
		return NULL;
	}

	// find endpoint stream info
	auto streamInfo = aioi->second->getEndpointStreamInfo(endpointID);
	if (streamInfo == AudioStreamInfo::invalid) {
		LOG(logERROR) << " requested start of unknown stream " << id;
		return NULL;
	}


	// check if endpoint already streaming or create new stream
	auto activeStreamI = m_activeStreams.find(id);	
	if (activeStreamI == m_activeStreams.end()) {
		m_activeStreams[id] = aioi->second->stream(endpointID);
	}

	auto activeStream = m_activeStreams[id];

	// return a new child stream
	return new AudioIOStream(activeStream);
}


AudioStreamCollection AudioIOManager::getAvailableStreams() {
	return{};
}