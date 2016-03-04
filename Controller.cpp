#include "Controller.h"

#include <rtt.h>
#include<time.h>
#include<iostream>

#include"UlltraProto.h"

#include<fstream>

Controller::Controller(const Params &params)
{
	m_linkEval.systemLatencyEval();

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

	defaultLinkCandidates();

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
				m_asyncActions.push_back([this, nd]() {
					firstEncounter(nd);
				});
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


	
	m_server.on("link_choose", [this](const SocketAddress &addr, const std::string &id, const JsonNode &request, JsonNode &response) {
		auto n = validateOrigin(addr, id);
		if (!n.exists()) {
			response["error"] = "Who are you?";
			return;
		}

		m_asyncActions.push_back([this, n]() {
			m_linkEval.chooseLinkCandidate(m_linkCandidates, n, false);
		});

		int i = 0;
		for (auto &c : m_linkCandidates) {
			response["candidates"][i++] = c.first;
		}
	});


	m_server.on("link_eval", [this](const SocketAddress &addr, const std::string &id, const JsonNode &request, JsonNode &response) {
		auto n = validateOrigin(addr, id);
		if (!n.exists()) {
			response["error"] = "Who are you?";
			return;
		}

		auto cand = request["candidate"].str;

		if (cand.empty()) {
			LOG(logERROR) << addr << " requested link eval without specifying a candidate! " << request;
			response["error"] = "Specifiy candidate";
			return;
		}

		if (m_linkCandidates.find(cand) == m_linkCandidates.end()) {
			LOG(logERROR) << addr << " requested link eval of unknown " << cand;
			response["error"] = "Unknown candidate " + cand;
			return;
		}

		m_asyncActions.push_back([this, n]() {
			m_linkEval.chooseLinkCandidate(m_linkCandidates, n, false);
		});

		response["candidates"] = "ok";
	});
}


Controller::~Controller()
{
	m_isRunning = false;
	if (!m_updateThread->Join(4000)) {
		LOG(logINFO) << "joining update thread failed, killing.";
		m_updateThread->Kill();
		LOG(logDEBUG1) << "thread killed!";
	}
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


void Controller::listNodes()
{
	LOG(logINFO) << "Present nodes:";
	for (auto &n : m_presentNodes) {
		LOG(logINFO) << "    " << n.second;
	}
	LOG(logINFO) << std::endl;
}

void Controller::updateThreadMain(void *arg)
{
	// we use 2-stage discovery: after actual discovery must say hello over tcp
	// any inital actions are done in on("hello") handler
	m_discovery.onNodeDiscovered = [this](const Discovery::NodeDevice &node) {
		try {
			if (isSlave(node)) {
				auto resp = rpc(node, "hello", JsonNode({
					"name", m_discovery.getSelfName(),
					"id", m_discovery.getHwAddress()
				}));
				firstEncounter(node);
			}
		}
		catch (...) {

		}
	};
		
		//std::bind(&Controller::firstEncounter, this, std::placeholders::_1);
	
    m_discovery.onNodeLost = [this](const Discovery::NodeDevice &node) {
		LOG(logDEBUG) << "Discovery said " << node << " is lost, ignoring";
		listNodes();
		//m_presentNodes.erase(node.getId());
    };


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
		uint64_t nowUs = UP::getMicroSeconds();

		if (m_helloNodes.size()) {
			for (auto &n : m_helloNodes) {
				firstEncounter(n);
			}
			m_helloNodes.clear();
		}


		if (m_asyncActions.size()) {
			auto aa = m_asyncActions; // work on a copy!
			m_asyncActions.clear();
			for (auto &f : aa) {
				f();
			}
		}


        m_discovery.update(nowUs);
        //m_linkEval.update(now);
        m_server.update();



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
		
        usleep(UP::UpdateIntervalUS);
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

	if (isSlave(node)) {
		LOG(logINFO) << "initiating link evaluation to node " << node;

		try {
			auto res = rpc(node, "link_choose", JsonNode({}));
			m_asyncActions.push_back([this, node]() {
				m_linkEval.chooseLinkCandidate(m_linkCandidates, node, true);
			});	
			LOG(logINFO) << "queued deferred start of link evaluation with" << node;
		}
		catch (const JsonHttpClient::Exception &ex) {
			LOG(logDEBUG1) << "request failed: " << ex.statusString;
		}
	}

	listNodes();

	return true;
}

JsonNode const& Controller::rpc(Discovery::NodeDevice const& node, std::string const& method, JsonNode const& params)
{
	// auto-retry!
	auto id = m_discovery.getHwAddress() + '-' + node.getId() + '@' + std::to_string(node.nextRpcId());
	return m_client.rpc(node.getAddr(UP::HttpControlPort), id, method, params);
}

#include "LLUdpLink.h"
#include "LLTcp.h"

//#define DeleteSafe(p) p ?
void Controller::defaultLinkCandidates()
{
	auto &candidates(m_linkCandidates);


	
	// chose best known block modes (linux better in user space)
	candidates["udp_ablock"] = ([]() {
		LLUdpLink *ll = new LLUdpLink();
		if (!ll->setRxBlockingMode(
#ifndef _WIN32
            LLCustomBlock::Mode::KernelBlock // UserBlock
#else
            LLCustomBlock::Mode::KernelBlock
#endif
			)) {
			delete ll;
			return (ll = 0);
		} 
		return ll;
	});

	return;
	
	candidates["tcp_block"] = ([]() {
		auto ll = new LLTcp();
		if (!ll->setRxBlockingMode(LLCustomBlock::Mode::Select)) {
			delete ll;
			return (ll = 0);
		}
		return ll;
	});
	candidates["tcp_block"] = ([]() {
		auto ll = new LLTcp();
		if (!ll->setRxBlockingMode(LLCustomBlock::Mode::Select)) {
			delete ll;
			return (ll = 0);
		}
		return ll;
	});


    return;

	candidates["udp_kblock"] = ([]() {
		LLUdpLink *ll = new LLUdpLink();
		if (!ll->setRxBlockingMode(LLCustomBlock::Mode::KernelBlock)) {
			delete ll;
			return (ll = 0);
		}
		return ll;
	});

	candidates["udp_sblock"] = ([]() {
		LLUdpLink *ll = new LLUdpLink();
		if (!ll->setRxBlockingMode(LLCustomBlock::Mode::Select)) {
			delete ll;
			return (ll = 0);
		}
		return ll;
	});



	candidates["udp_ublock"] = ([]() {
		LLUdpLink *ll = new LLUdpLink();
		if (!ll->setRxBlockingMode(LLCustomBlock::Mode::UserBlock)) {
			delete ll;
			return (ll = 0);
		}
		return ll;
	});



	/*
	candidates["tcp_kblock"] = ([](Discovery::NodeDevice const& nd, int port, uint64_t timeout) {
		LLTcp *ll = new LLTcp();
		ll->connect(nd.getAddr(port));
		bool res = ll->setRxBlockingMode(LLCustomBlock::Mode::Select);
		if (!res || !ll->setBlockingTimeout(timeout)) {
			delete ll;
			return ll = 0;
		}
		return ll;
	}); */
}
