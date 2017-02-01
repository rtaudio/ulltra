#ifdef WITH_JACK

#include <stdint.h>
#ifndef _STDINT_H
#define _STDINT_H
#endif
#include <jack/jack.h>
#include <rtt.h>
#include "../UlltraProto.h"

#include "JackClient.h"



int JackClient::jackProcess(jack_nframes_t nframes, void *arg)
{
	auto client = static_cast<JackClient*>(arg);

	static int init = 1;
	if (init) {
		auto &thread(RttThread::GetCurrent());
		thread.SetName("jackprocess");
		init = 0;
	}



	for (auto &stream : client->getActiveStreams())
	{
		auto jackStream = static_cast<JackStream*>(stream);

		std::vector<float*> channelBuffers;

		for (auto &port : jackStream->m_ports) {
			channelBuffers.push_back((float *)jack_port_get_buffer(port, nframes));
		}

		if (stream->isCapture())
		{
			stream->blockRead(channelBuffers, nframes); // fill channelBuffers from stream
		}
		else
		{
			stream->blockWrite(channelBuffers, nframes); // write channelBuffers to stream
		}
	}

	return 0;
}

void jackShutdown(void *arg)
{

}


int JackClient::jackXRun(void *arg)
{
	auto client = static_cast<JackClient*>(arg);

	for (auto &stream : client->m_streams) {
		stream->notifyXRun();
	}

	return 0;
}

JackClient::~JackClient() {
	for (auto &s : m_streams) {
		delete s;
		s = 0;
	}
	m_streams.clear();
}

bool JackClient::open()
{	
	m_clientName = "ulltra";
	const char *server_name = NULL;
	jack_options_t options = JackNoStartServer;
	jack_status_t status;

	int retries = 0;
	while ((m_client = jack_client_open(m_clientName, options, &status)) == NULL) {
		if (retries == 20) {
			LOG(logERROR) << "Unable to connect to JACK2 server!";
			return false;
		}
		retries++;
		LOG(logERROR) << "jack_client_open() failed, retrying...";
		usleep(1000 * 200);
	}

	if (status & JackServerStarted) {
		fprintf(stderr, "JACK server started\n");
	}

	if (status & JackNameNotUnique) {
		m_clientName = jack_get_client_name(m_client);
		LOG(logINFO) << "JackClient unique name " << m_clientName << " assigned.";
	}


	jack_set_process_callback(m_client, jackProcess, this);
	jack_on_shutdown(m_client, jackShutdown, this);
	jack_set_xrun_callback(m_client, jackXRun, this);


	if (jack_activate(m_client)) {
		LOG(logERROR) << "Failed to activate JACK client!";
		return false;
	}

	// foreach  physical playback port:
	//		register a new port
	//		connect the new port to its corresponding physical
	//		store new port handle in a map of client names

	int portIndex = 0;
	std::map<std::string, std::vector<jack_port_t*>> portClientMap;
	const char** physOutputPorts = jack_get_ports(m_client, NULL, JACK_DEFAULT_AUDIO_TYPE, JackPortIsPhysical | JackPortIsInput);
	if (physOutputPorts) {
		while (*physOutputPorts++) {
			std::string portName(*physOutputPorts);
			auto p = portName.find(':');
			if (p == std::string::npos || p == 0 || p > (portName.length() - 1))
				continue;
			std::string clientName = portName.substr(0, p);
			if (portClientMap.find(clientName) == portClientMap.end())
				portClientMap[clientName] = std::vector<jack_port_t*>();

			auto registeredPort = jack_port_register(m_client, std::string("out" + std::string(portIndex < 10 ? "0" : "") + std::to_string(portIndex)).c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

			if (jack_connect(m_client, jack_port_name(registeredPort), *physOutputPorts)) {
				continue;
			}
			portClientMap[portName.substr(0, p)].push_back(registeredPort);
		}

		jack_free(physOutputPorts);
	}

	// temporary hold pointers for call addStreams()
	std::vector <AudioStreamInfo> streamInfos;
	
	AudioStreamInfo streamInfo;
	// all jack streams share this:
	streamInfo.blockSize = jack_get_buffer_size(m_client);
	streamInfo.quantizationFormat = AudioStreamInfo::Quantization::Float;
	streamInfo.sampleRate = jack_get_sample_rate(m_client);

	// use the port map to create a multichannel-stream for each client
	for (auto it : portClientMap) {
		streamInfo.channelCount = it.second.size();
		streamInfo.id = "jack:" + it.first;
		streamInfo.name = it.first;
		streamInfo.direction = AudioStreamInfo::Direction::Playback;
		streamInfos.push_back(streamInfo);
	}
	
	endpointsAdd(streamInfos);
}

#endif