#pragma once

#include "networking.h"

#include <string>
#include <vector>
#include <functional>
#include <map>

class SimpleUdpReceiver;

class Discovery
{
public:
	struct NodeDevice {
		static NodeDevice none;
		
		std::string name;
		std::string id;
		in_addr addr;
		time_t vitalSign;

		bool exists() const {
			return id.length() > 0;
		}
	};

	typedef std::function<void(const NodeDevice &node)> NodeEventHandler;


	Discovery();
	~Discovery();

	std::string getHwAddress();

	bool start(int broadcastPort);
	bool update(time_t now);


	const NodeDevice &getNode(const std::string &id) const;
	const NodeDevice &getNode(const in_addr &addr) const;

	NodeEventHandler onNodeDiscovered;
	NodeEventHandler onNodeLost;

	void on(std::string msg, const NodeEventHandler &handler)
	{
		m_customHandlers[msg] = handler;
	}

	void send(const std::string &msg, const NodeDevice &node = NodeDevice::none);

private:
	NodeDevice &getNode(const std::string &id);

	int m_broadcastPort;
	SOCKET m_soc;
	SimpleUdpReceiver * m_receiver;

	uint32_t m_updateCounter;

	std::vector<NodeDevice> m_discovered;

	std::map<std::string, NodeEventHandler> m_customHandlers;

	time_t m_lastBroadcast;


};

std::ostream& operator<<(std::ostream &strm, const Discovery::NodeDevice &nd);
