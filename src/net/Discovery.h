#pragma once

#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <map>

#include <pclog/pclog.h>
#include "networking.h"


class SimpleUdpReceiver;

class Discovery
{
public:
	class NodeDevice {
	public:
		inline bool exists() const { return id.length() > 0 || name.length() > 0; }
		inline bool known() const { return id.length() > 2; }

		/**
		 * Checks node has been recently seen
		 * @return true if node is active
		 */
        inline bool alive() const { return (sinceVitalSign >= 0 && sinceVitalSign < (UP::BroadcastIntervalMs*1000*2)); }

		/**
		 * Random connection retry
		 * @param now
		 * @return
		 */
		bool shouldRetryConnection(time_t now) {
			if (difftime(now, timeLastConnectionTry) > 10) {
				timeLastConnectionTry = now + rand() % 5;
				return true;
			}
			return false;
		}



		void update(const std::string &newId, const std::string &newName) {
			id = newId;
			name = newName;
		}

		inline uint64_t nextRpcId() const { return ++m_rpcId; }

        inline const std::string & getId() const { return id; }
		inline const std::string & getName() const { return name; }
        static NodeDevice none, localAny, local4, local6;

		/* create from a hostname or address (used for manual explicit host discovery) */
		NodeDevice(const std::string &host);

		NodeDevice(const SocketAddress &addr, const std::string &clientId, const std::string &name) : NodeDevice(addr){
			this->id = clientId;
			this->name = name;
		}

		const SocketAddress getAddr(int port) const;
		inline const std::string & getHost() const { return hostname; }
	private:
        friend class Discovery;

		/**
		 * Creates a non-existing node with a local address wildcard
		 * @param family IPv4 or IPv6
		 */
		NodeDevice(int family);

		/* create from a incoming link (udp/tcp) */
		NodeDevice(const SocketAddress &addr);


		/**
		 * human readable name
		 */
		std::string name;

		/**
		 * unique id, usually the MAC address
		 */
		std::string id;

		std::string hostname;

		SocketAddress addrStorage;
        time_t sinceVitalSign;//seconds
        time_t timeLastConnectionTry;//seconds
		mutable uint64_t m_rpcId;


		friend std::ostream & operator<<(std::ostream &os, const NodeDevice& n);
		friend std::ostream & operator<<(std::ostream &os, const NodeDevice* n);
	};

	typedef std::function<void(const NodeDevice &node)> NodeEventHandler;

	static const std::vector<std::string> & getLocalIPAddresses();

	static void initNetworking();

	Discovery();
	~Discovery();
	void addExplicitHosts(const std::string &host);

	

	bool start(int broadcastPort);
	bool update(uint64_t now);


    const NodeDevice &getNode(const std::string &id) const;
    const NodeDevice &getNode(const sockaddr_storage &addr) const;

	NodeEventHandler onNodeDiscovered;
	NodeEventHandler onNodeLost;


	void on(std::string msg, const NodeEventHandler &handler)
	{
		m_customHandlers[msg] = handler;
	}

    bool send(const std::string &msg, const NodeDevice &node = NodeDevice::none);

	std::string getHwAddress();
    inline const std::string &getSelfName() const { return UP::getDeviceName(); }

private:
	bool initMulticast(int port);
	bool initBroadcast(int port);

	void tryConnectExplictHosts();
	NodeDevice &getNode(const std::string &id);	
	bool processMessage(const NodeAddr& nd, const char *message, std::vector<NodeDevice> &newNodes);


	int m_broadcastPort;
	SOCKET m_socBroadcast4, m_socMulticast;	
	struct sockaddr_storage m_multicastAddrBind, m_multicastAddrSend;



	uint32_t m_updateCounter;
	uint64_t m_lastBroadcast, m_lastUpdateTime;

	std::map<std::string, NodeDevice> m_discovered;
	std::vector<NodeDevice> m_explicitNodes;
	std::map<std::string, NodeEventHandler> m_customHandlers;

	SimpleUdpReceiver * m_receiver;
};

std::ostream& operator<<(std::ostream &strm, const Discovery::NodeDevice &nd);
