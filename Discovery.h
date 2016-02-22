#pragma once

#include <string>
#include <vector>

#ifdef WIN32
#include<winsock2.h>
#else
#include <netinet/in.h>
#endif
//#include <inaddr.h>

class Discovery
{
public:
	struct NodeDevice {
		const static NodeDevice none;
		
		std::string name;
		std::string id;
		in_addr addr;

		bool exists() const {
			return id.length() > 0;
		}
	};

	Discovery();
	~Discovery();

	std::string getHwAddress();

	bool start(int broadcastPort);
	bool update();


	const NodeDevice &findById(const std::string &id) const;

private:
	int m_broadcastPort;
	void *m_soc;
	void * m_recvSocket;
	void sayIAmHere(bool notGoodby);

	uint32_t m_updateCounter;

	std::vector<NodeDevice> m_discovered;
};

