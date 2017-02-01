#ifdef WITH_JACK

#include "JackStream.h"



JackStream::JackStream(AudioStreamInfo const& info, std::vector<jack_port_t*> const& connectedPorts) :
	AudioIOStream(info)
{
}


JackStream::~JackStream()
{
}

#endif