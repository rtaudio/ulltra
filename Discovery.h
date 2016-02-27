#pragma once

#include "networking.h"

#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <map>

class SimpleUdpReceiver;

class Discovery
{
public:
	class NodeDevice {
	public:
		inline bool exists() const { return id.length() > 0; }
		inline const std::string & getId() const { return id; }
		inline const std::string & getName() const { return name; }
		static NodeDevice any;

	private:
		friend class Discovery;
		void initAddr(bool realHost) {
			static bool sNeedInit = true;
			if (sNeedInit) {
				sNeedInit = false;
			}
			memset(&saddr, 0 , sizeof(saddr));
			memset(&addr, 0, sizeof(addr));
			
			if (realHost) {
				addr.ai_flags = AI_NUMERICHOST | AI_CANONNAME;
				addr.ai_addr = (sockaddr*)&saddr;
				addr.ai_addrlen = sizeof(saddr);
			}
			else {
				addr.ai_flags = AI_PASSIVE;
			}
		}

		/* reflects a non-existing node with a multicast address */
		NodeDevice() {
			initAddr(false);
		}

		/* create from a hostname or address (used for explicit host discovery) */
		NodeDevice(const std::string &host);

		/* create from a incoming link (udp/tcp) */
		NodeDevice(const sockaddr_storage &addr);


		std::string name;
		std::string id;

		struct sockaddr_storage addrStorage;
		time_t sinceVitalSign;//seconds

		const struct sockaddr_storage getAddr(int port) const;
		friend std::ostream & operator<<(std::ostream &os, const NodeDevice& n);
		friend std::ostream & operator<<(std::ostream &os, const NodeDevice* n);
	};

	typedef std::function<void(const NodeDevice &node)> NodeEventHandler;

	Discovery();
	~Discovery();
	void addExplicitHosts(const std::string &host);

	std::string getHwAddress();

	bool start(int broadcastPort);
	bool update(time_t now);


	const NodeDevice &getNode(const std::string &id) const;
	const NodeDevice &getNode(const addrinfo &addr) const;

	NodeEventHandler onNodeDiscovered;
	NodeEventHandler onNodeLost;

	void on(std::string msg, const NodeEventHandler &handler)
	{
		m_customHandlers[msg] = handler;
	}

	bool send(const std::string &msg, const NodeDevice &node = NodeDevice::any);

private:
	NodeDevice &getNode(const std::string &id);

	int m_broadcastPort;

	SOCKET m_socBroadcast4, m_socMulticast; //UDP
	SOCKET m_socAccept; //TCP accept


	bool initMulticast(int port);
	struct sockaddr_storage m_multicastAddrBind, m_multicastAddrSend;

	SimpleUdpReceiver * m_receiver;

	uint32_t m_updateCounter;

	std::map<std::string, NodeDevice> m_discovered;
	std::vector<NodeDevice> m_explicitNodes;

	std::map<std::string, NodeEventHandler> m_customHandlers;

	time_t m_lastBroadcast, m_lastUpdateTime;


};

std::ostream& operator<<(std::ostream &strm, const Discovery::NodeDevice &nd);
