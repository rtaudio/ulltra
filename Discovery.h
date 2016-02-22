#pragma once

#include <string>
#include <vector>


#include<winsock2.h>
//#include <inaddr.h>

class Discovery
{
public:
	struct NodeDevice {
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

