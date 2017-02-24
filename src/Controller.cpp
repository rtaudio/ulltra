#include"UlltraProto.h"

#include "Controller.h"

#include<time.h>
#include<iostream>
#include<fstream>

#include <rtt/rtt.h>


#include "net/LLUdpLink.h"
#include "net/LLTcp.h"
#include "net/LLUdpQWave.h"




JsonNode deviceState2Json(AudioIOManager::DeviceState const& state)
{
	JsonNode jsi;
	jsi["name"] = state.info.name;
	jsi["id"] = state.id;
	jsi["sampleRates"] = state.getSampleRateCurrentlyAvailable();
	jsi["blockSize"] = 512; // TODO
	jsi["numChannels"] = state.numChannels();
	jsi["isCapture"] = state.isCapture;
	jsi["isPlayback"] = !state.isCapture;
	jsi["latency"] = 0; // TODO
	jsi["isDefault"] = state.isCapture ? state.info.isDefaultInput : state.info.isDefaultOutput;
	jsi["isOpen"] = state.isOpen();
	return jsi;
}

std::vector<std::string> readLines(const std::string &fileName) {
	std::ifstream nfs(fileName);
	std::vector<std::string> lines;
	while (nfs.good() && !nfs.eof()) {
		std::string nh;
		nfs >> nh;
		trim(nh);
		if (nh.empty() || nh[0] == '#') continue;
		lines.push_back(nh);
	}
	return lines;
}

Controller::Controller(const Params &params)
	: m_webAudio(m_audioManager)
	, m_rpcServer(UP::HttpServerThreadPoolSize)
	, m_streamManager(*this)
{
	auto seed = (unsigned int)time(NULL);
	srand(seed);

	//m_linkEval.systemLatencyEval();

    LOG(logINFO) << "Nodes file: " << params.nodesFileName;
	auto nodeHosts = readLines(params.nodesFileName);
	for (auto nh : nodeHosts) {
		Discovery::NodeDevice nd(nh);
		LOG(logINFO) << "Explicit node: " << nd;
		m_explicitNodes.push_back(nd);
		m_discovery.addExplicitHosts(nh); // TODO comment out?
	}

	defaultLinkCandidates();

    m_isRunning = true;
	m_updateThread = new RttThread([this]() {
		updateThreadMain();
	}, false, "cntrlupdate");

	// after discovery each node should say hello
	m_rpcServer.on("hello", [this](const SocketAddress &addr, const JsonNode &request, JsonNode &response) {	
		if (request["name"].str.empty() || request["id"].str.empty()) {
			response["error"] = "Say your name!";
			return;
		}

		// make sure the sender has been discovered
		// if not yet done, do a firstEncounter
		if (!validateOrigin(addr, request).known()) {
			auto id = request["deviceId"].str;
			auto skipClientNodeId = id.find('-');
			auto clienId = id.substr(0, skipClientNodeId);
			if (skipClientNodeId != std::string::npos && clienId == request["id"].str
				&& m_presentNodes.find(clienId) == m_presentNodes.end()) {
				Discovery::NodeDevice nd(*(sockaddr_storage*)&addr, clienId, request["name"].str);
				m_asyncActions.push_back([this, nd]() {
					firstEncounter(nd);
				});
			}
		}

		response["name"] = m_discovery.getSelfName();
		response["id"] = m_discovery.getHwAddress();
		return;
	});

	m_rpcServer.on("bye", [this](const SocketAddress &addr, const JsonNode &request, JsonNode &response) {
		auto n = validateOrigin(addr, request);
		if (!n.known()) {
			response["error"] = "Who are you?";
			return;
		}
		m_presentNodes.erase(n.getId());
		response["bye"] = "see you " +n.getName();// just send a dummy response
		LOG(logINFO) << " bye from " << addr;
		return;
	});


	// link evaluation session by request from master, this is executed on the slave
	// reponse: a list of available link candidates
	m_rpcServer.on("link-eval-session", [this](const SocketAddress &addr, const JsonNode &request, JsonNode &response) {
		auto n = validateOrigin(addr, request);
		if (!n.exists()) {
			response["error"] = "Who are you?";
			return;
		}

		m_asyncActions.push_back([this, n]() {
			m_linkEval.sessionSlave(m_linkCandidates, n);
		});

		int i = 0;
		for (auto &c : m_linkCandidates) {
			response["candidates"][i++] = c.first;
		}
	});

	m_rpcServer.on("list-devices", [this](const SocketAddress &addr,  const JsonNode &request, JsonNode &response) {
		auto n = validateOrigin(addr, request);

		auto &devList = m_audioManager.getDevices();
		int i = 0;
		for (auto &d : devList) {
			response[i++] = deviceState2Json(d);
		}
	});

	// JS!
	m_rpcServer.on("get_graph", [this](const SocketAddress &addr, const JsonNode &request, JsonNode &response) {
		response["self_name"] = UP::getDeviceName();
		response["self_addresses"] = Discovery::getLocalIPAddresses();

		response["nodes"][0]["name"] = UP::getDeviceName();
		response["nodes"][0]["id"] = m_discovery.getHwAddress();

		int i = 1;
		for (auto &ni : m_presentNodes) {
			Discovery::NodeDevice &n(ni.second);
			response["nodes"][i]["name"] = n.getName();
			response["nodes"][i]["id"] = n.getId();
			response["nodes"][i]["hostname"] = n.getHost();
			i++;
		}
		/*
		response["nodes"][i]["name"] = "dummy";
		response["nodes"][i]["id"] = "dummy" + std::to_string(i);
		response["nodes"][i]["hostname"] = "s";
		i++; */
	});

	m_webAudio.registerWithServer(m_rpcServer);	
}


Controller::~Controller()
{
	m_isRunning = false;
	if (!m_updateThread->Join(2000)) {
		LOG(logINFO) << "joining update thread failed, killing.";
		m_updateThread->Kill();
		LOG(logDEBUG1) << "thread killed!";
	}
	delete m_updateThread;
}

Discovery::NodeDevice const& Controller::validateOrigin(const SocketAddress &addr, const JsonNode &reqestData)
{
	auto id = reqestData["deviceId"].str;
	if(id.empty()) {
		return Discovery::NodeDevice::none;
	}

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
	LOG(logINFO) << "====== Present nodes: ======";
	for (auto &n : m_presentNodes) {
		LOG(logINFO) << "    " << n.second;
	}
	LOG(logINFO) << " ====== ====== ====== ======" << std::endl;
}

void Controller::updateThreadMain()
{
    UP::tick();

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
			LOG(logDEBUG) << "an exception occured during hello handshake!";
		}
	};
			
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

    SocketAddress httpBindAddr(Discovery::NodeDevice::localAny.getAddr(UP::HttpControlPort));
    if (!m_rpcServer.start(httpBindAddr.toString())) {
		m_isRunning = false;
        LOG(logERROR) << "RPC server init failed on address " << httpBindAddr.toString() << "!";
        return;
    }


    time_t now;

    while (m_isRunning) {
        now = time(NULL);
		uint64_t nowUs = UP::getMicroSeconds();

		// process async actions
		if (m_asyncActions.size()) {
			auto aa = m_asyncActions; // work on a copy!
			m_asyncActions.clear();
			for (auto &f : aa) {
				f();
			}
		}


        m_discovery.update(nowUs);
        m_rpcServer.update();


		// always try to reach explicit nodes
        for(Discovery::NodeDevice &n : m_explicitNodes)
        {
			try {
				if (!n.alive() && n.shouldRetryConnection(now))
				{
					auto resp = rpc(n, "hello", JsonNode({
						"name", m_discovery.getSelfName(),
						"id", m_discovery.getHwAddress()
					}));
					
					if (resp["id"].isUndefined() || resp["name"].isUndefined()) {
						LOG(logERROR) << "invalid hello response " << resp;
					}
					else {
						n.update(resp["id"].str, resp["name"].str);		
						LOG(logDEBUG1) << "got hello response from " << n << ": " << resp;
						firstEncounter(n);
					}					
				}
			} 
			catch (const std::exception &ex) {
				LOG(logDEBUG1) << " http client exception: " << ex.what();
			}
        }

		/*
		auto streamers(m_streamers);
		for (auto si = m_streamers.begin(); si != m_streamers.end(); )
		{
			auto s(*si);
			if (s->isAlive()) {
				s->update();
				si++;
			}
			else {
				delete s;
				si = m_streamers.erase(si);
			}
		}
		*/
		
		//if (!m_rpcServer.hasActiveConnection())
//			usleep(UP::UpdateIntervalUS);
		//else
//			LOG(logDEBUG) << "rpc has active conns, spinning";
    } // while(runnning)


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
		catch (const std::exception &ex) {
			LOG(logDEBUG1) << " http client exception: " << ex.what();
		}
	}

}

// node handshaking
bool Controller::firstEncounter(const Discovery::NodeDevice &node)
{
	if (!node.exists() || m_presentNodes.find(node.getId()) != m_presentNodes.end()) {
		return false;
	}
	m_presentNodes.insert(std::pair<std::string, Discovery::NodeDevice>(node.getId(), node));;

	LOG(logINFO) << "first encounter with " << node;

	/* moved link evaluation to stream init"
	if (isSlave(node)) {
		LOG(logINFO) << "initiating link evaluation to slave node " << node;

		try {
			auto res = rpc(node, "link-eval-session", JsonNode({}));
			m_asyncActions.push_back([this, node]() {
				m_linkEval.sessionMaster(m_linkCandidates, node);
			});	
			LOG(logINFO) << "queued deferred start of link evaluation with" << node;
		}
		catch (const JsonHttpClient::Exception &ex) {
			LOG(logDEBUG1) << "request failed: " << ex.statusString;
		}
	}
	*/

	listNodes();

	return true;
}

JsonNode const& Controller::rpc(Discovery::NodeDevice const& node, std::string const& method, JsonNode const& params)
{
	// auto-retry!
	auto id = m_discovery.getHwAddress() + '-' + node.getId() + '@' + std::to_string(node.nextRpcId());

	auto paramsCopy = params;
	paramsCopy["deviceId"] = id;

	return m_client.rpc(node.getAddr(UP::HttpControlPort), method, paramsCopy);
}



//#define DeleteSafe(p) p ?
void Controller::defaultLinkCandidates()
{
	auto &candidates(m_linkCandidates);

	/*
	// chose best known block modes (linux better in user space)
	candidates["udp_ablock"] = ([]() {


#ifndef _WIN32
		LLUdpLink *ll = new LLUdpLink();
		if (!ll->setRxBlockingMode(LLCustomBlock::Mode::KernelBlock)) {
			delete ll;
			return (ll = 0);
		}
#else
		LLUdpQWave *ll = new LLUdpQWave();
#endif

		return ll;
	});
	**/

	candidates["tcp_block"] = ([]() {
		auto ll = new LLTcp();
		if (!ll->setRxBlockingMode(LLCustomBlock::Mode::Select)) {
			delete ll;
			return (ll = 0);
		}
		return ll;
		});

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
