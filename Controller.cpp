#include "Controller.h"

#include <rtt.h>
#include<time.h>
#include<iostream>

#include"UlltraProto.h"

#include<fstream>

Controller::Controller(const Params &params)
{
#if _WIN32
    static bool needWSInit = true;

    if (needWSInit) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 0), &wsa);
        needWSInit = false;
    }
#endif


    LOG(logDEBUG) << "Node file: " << params.nodesFileName;
    std::ifstream nfs(params.nodesFileName);
    while(nfs.good() && !nfs.eof()) {
        std::string nh;
        nfs >> nh;
        trim(nh);
        if(nh.empty() || nh[0] == '#') continue;
        Discovery::NodeDevice nd(nh);
        LOG(logINFO) << "Explicit node: " << nd;
        m_explicitNodes.push_back((nd));
        //m_discovery.addExplicitHosts(nh);
    }



    m_isRunning = true;
    RttThread::Routine updateThread(std::bind(&Controller::updateThreadMain, this, std::placeholders::_1));
    m_updateThread = new RttThread(updateThread);
	

	
	m_server.on("hello", [this](const SocketAddress &addr, const std::string &id, JsonNode &request, JsonNode &response) {

		if (request["name"].str.empty() || request["id"].str.empty()) {
			response["error"] = "Say your name!";
			return;
		}

		if (!validateOrigin(addr, id).known()) {
			auto skipClientNodeId = id.find('-');
			auto clienId = id.substr(0, skipClientNodeId);
			if (skipClientNodeId != std::string::npos && clienId == request["id"].str
				&& m_presentNodes.find(clienId) == m_presentNodes.end()) {
				Discovery::NodeDevice nd(*(sockaddr_storage*)&addr);
				nd.id = clienId;
				nd.name = request["name"].str;
				m_helloNodes.push_back(nd);
			}
		}

		response["name"] = m_discovery.getSelfName();
		response["id"] = m_discovery.getHwAddress();
		return;
	});

	m_server.on("bye", [this](const SocketAddress &addr, const std::string &id, const JsonNode &request, JsonNode &response) {
		auto n = validateOrigin(addr, id);
		if (!n.known()) {
			response["error"] = "Who are you?";
			return;
		}
		m_presentNodes.erase(n.getId());
		response["bye"] = "see you " +n.getName();// just send a dummy response
		LOG(logINFO) << " bye from " << addr;
		return;
	});

	m_server.on("link_eval", [this](const SocketAddress &addr, const std::string &id, const JsonNode &request, JsonNode &response) {
		auto n = validateOrigin(addr, id);
		if (!n.exists()) {
			response["error"] = "Who are you?";
			return;
		}

		if (request["candidate"].str.empty()) {
			LOG(logERROR) << addr << " requested link eval without specifying a candidate! " << request;
			response["error"] = "Specifiy candidate";
			return;
		}

		m_linkEval.latencyTestSlave(n);
	});
}


Controller::~Controller()
{
	m_isRunning = false;
	delete m_updateThread;
}

Discovery::NodeDevice const& Controller::validateOrigin(const SocketAddress &addr, const std::string &id)
{
	auto skipClientNodeId = id.find('-');
	auto skipBothNodeId = id.find('@');
	if (skipClientNodeId == std::string::npos || skipBothNodeId == std::string::npos) {
		return Discovery::NodeDevice::none;
	}

	
	auto selfId = id.substr(skipClientNodeId + 1, skipBothNodeId - skipClientNodeId - 1);

	if (selfId != m_discovery.getHwAddress()) {
		return Discovery::NodeDevice::none;
	}
	
	auto clienId = id.substr(0, skipClientNodeId);
	auto it = m_presentNodes.find(clienId);
	if (it == m_presentNodes.end()) {
		return Discovery::NodeDevice::none;
	}

	SocketAddress naddr(it->second.getAddr(addr.getPort()));
	
	if (naddr != addr) {
		LOG(logDEBUG2) << naddr << " != " << addr;
 		return Discovery::NodeDevice::none;
	}

	return m_presentNodes.at(clienId);
}


void Controller::updateThreadMain(void *arg)
{
	//m_discovery.onNodeDiscovered = std::bind(&Controller::firstEncounter, this, std::placeholders::_1); 
	
    m_discovery.onNodeLost = [this](const Discovery::NodeDevice &node) {
		m_presentNodes.erase(node.getId());
    };

    m_discovery.on(UlltraProto::LatencyTestStartToken, [this](const Discovery::NodeDevice &node) {
        m_linkEval.latencyTestSlave(node);
    });

    if (!m_discovery.start(UlltraProto::DiscoveryPort)) {
        m_isRunning = false;
        LOG(logERROR) << "Discovery init failed!";
        return;
    }

    SocketAddress httpBindAddr(Discovery::NodeDevice::localAny.addrStorage);
    httpBindAddr.setPort(UP::HttpControlPort);

    if (!m_server.start(httpBindAddr.toString())) {
        LOG(logERROR) << "Control server init failed!";
        return;
    }


    if (!m_linkEval.init()) {
        m_isRunning = false;
        LOG(logERROR) << "Link evaluation init failed!";
        return;
    }

    time_t now;

    while (m_isRunning) {
        now = time(NULL);

        m_discovery.update(now);
        //m_linkEval.update(now);
        m_server.update();

		for (auto &n : m_helloNodes) {
			firstEncounter(n);
		}

        for(Discovery::NodeDevice &n : m_explicitNodes)
        {
			try {
				if (!n.alive() && difftime(now, n.timeLastConnectionTry) > 10)
				{
					n.timeLastConnectionTry = now + rand() % 5;

					auto resp = rpc(n, "hello", JsonNode({
						"name", m_discovery.getSelfName(),
						"id", m_discovery.getHwAddress()
					}));
					
					LOG(logDEBUG1) << "got hello response from " << resp;

					if (resp["id"].isUndefined() || resp["name"].isUndefined()) {
						LOG(logERROR) << "invalid hello response " << resp;
					}
					else {
						n.id = resp["id"].str;
						n.name = resp["name"].str;
						firstEncounter(n);
					}					
				}
			} 
			catch (const JsonHttpClient::Exception ex) {
				LOG(logDEBUG1) << " http client exception: " << ex.statusString;
			}
        }
		
        usleep(1000*100);
    }


	for (auto n : m_presentNodes)
	{
		try
		{
			LOG(logDEBUG1) << "saying goodbye to " << n.second;
			auto res = rpc(n.second, "bye", JsonNode({
				"name", m_discovery.getSelfName(),
				"id",  m_discovery.getHwAddress()			
			}));
			LOG(logDEBUG2) << res;
		}
		catch (const JsonHttpClient::Exception ex) {
			LOG(logDEBUG1) << " http client exception: " << ex.statusString;
		}
	}

}

bool Controller::firstEncounter(const Discovery::NodeDevice &node)
{
	if (node.id.empty() || m_presentNodes.find(node.id) != m_presentNodes.end()) {
		return false;
	}
	m_presentNodes.insert(std::pair<std::string, Discovery::NodeDevice>(node.id, node));;

	LOG(logINFO) << "first encounter with " << node;

	if (node.getId() > m_discovery.getHwAddress()) {
		LOG(logINFO) << "initiating link evaluation to node " << node;

		try {
			auto res = rpc(node, "link_eval", JsonNode({"candidate", "udp"}));
			m_linkEval.latencyTestMaster(node);
		}
		catch (const JsonHttpClient::Exception &ex) {
			LOG(logDEBUG1) << "request failed: " << ex.statusString;
		}

		// request a latency test start
		m_linkEval.latencyTestMaster(node);
	}

	return true;
}

JsonNode const& Controller::rpc(Discovery::NodeDevice const& node, std::string const& method, JsonNode const& params)
{
	// auto-retry!
	auto id = m_discovery.getHwAddress() + '-' + node.getId() + '@' + std::to_string(node.nextRpcId());
	return m_client.rpc(node.getAddr(UP::HttpControlPort), id, method, params);
}
