#ifdef  WITH_JACK

#include <cstdint>
#include<jack/types.h>
#include <vector>
#include "JackStream.h"
#include "AudioIO.h"

class JackClient :
	public AudioIO
{
public:
	JackClient();
	~JackClient();

	bool open();
	void close();

private:
	static int JackClient::jackProcess(jack_nframes_t nframes, void *arg);
	static int JackClient::jackXRun(void *arg);

	jack_client_t *m_client;
	char *m_clientName;
	std::vector<JackStream*> m_streams;
};

#endif //  WITH_JACK
