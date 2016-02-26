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
        class NodeDevice {
            public:
                inline bool exists() const { return id.length() > 0; }
                inline const std::string & getId() const { return id; }
                static NodeDevice none;

            private:
                friend class Discovery;
                void initAddr() {
                    //memset(&addr, 0 , sizeof(addr));
                    addr = {};
                    addr.ai_family   = PF_UNSPEC;
                    addr.ai_socktype = SOCK_DGRAM;
                    addr.ai_flags    = AI_NUMERICHOST | AI_CANONNAME;
                }

                /* reflects a non-existing node with a multicast address */
                NodeDevice() {
                    initAddr();
                }

                /* create from a hostname or address */
                NodeDevice(const std::string &host);

                NodeDevice(const sockaddr_storage &addr);

		
		std::string name;
		std::string id;
                addrinfo addr;
                time_t sinceVitalSign;

                mutable std::map<int,addrinfo> portAddr;

                const addrinfo &getAddr(int port) const;
                friend std::ostream & operator<<(std::ostream &os, const NodeDevice& n);
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

	void send(const std::string &msg, const NodeDevice &node = NodeDevice::none);

private:
	NodeDevice &getNode(const std::string &id);

        int m_broadcastPort;

    SOCKET m_socBroadcast; //UDP broadcast
    SOCKET m_socAccept; //TCP accept

	SimpleUdpReceiver * m_receiver;

	uint32_t m_updateCounter;

        std::map<std::string, NodeDevice> m_discovered;
        std::vector<NodeDevice> m_explicitNodes;

	std::map<std::string, NodeEventHandler> m_customHandlers;

	time_t m_lastBroadcast;


};

std::ostream& operator<<(std::ostream &strm, const Discovery::NodeDevice &nd);
