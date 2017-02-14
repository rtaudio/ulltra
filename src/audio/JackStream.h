#pragma once
#include "AudioIOStream.h"
#include <cstdint>
#include <jack/types.h>
#include <vector>

class JackStream :
	public AudioIOStream
{
public:
	JackStream(AudioStreamInfo const& info, std::vector<jack_port_t*> const& connectedPorts);
	~JackStream();

	bool activate();
	void deactivate();

private:
	friend class JackClient;
	std::vector<jack_port_t*> m_ports; // max 8 channels per stream
};

