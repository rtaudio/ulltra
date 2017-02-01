#include"UlltraProto.h"

#include "Controller.h"

#include<time.h>
#include<iostream>
#include<fstream>

#include <rtt.h>


#include "audio/AudioIOStream.h"
#include "AudioStreamerTx.h"
#include "AudioStreamerRx.h"


JsonNode streamInfo2Json(AudioStreamInfo const& asi)
{
	JsonNode jsi;
	jsi["name"] = asi.name;
	jsi["sample_rate"] = asi.sampleRate;
	jsi["block_size"] = asi.blockSize;
	jsi["channel_count"] = asi.channelCount;
	jsi["latency"] = asi.latency;
	return jsi;
}


Controller::Controller(const Params &params)
{
	int seed = time(NULL);
	srand(seed);

	m_linkEval.systemLatencyEval();

#if _WIN32
    static bool needWSInit = true;

    if (needWSInit) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 0), &wsa);
        needWSInit = false;
    }
#endif

    LOG(logINFO) << "Nodes file: " << params.nodesFileName;
    std::ifstream nfs(params.nodesFileName);
    while(nfs.good() && !nfs.eof()) {
        std::string nh;
        nfs >> nh;
        trim(nh);
        if(nh.empty() || nh[0] == '#') continue;
        Discovery::NodeDevice nd(nh);
        LOG(logINFO) << "Explicit node: " << nd;
        m_explicitNodes.push_back((nd));
        m_discovery.addExplicitHosts(nh); // TODO comment out?
    }

	defaultLinkCandidates();

    m_isRunning = true;
	m_updateThread = new RttThread([this]() {
		updateThreadMain();
	});

	// after discovery each node should say hello
	m_rpcServer.on("hello", [this](const SocketAddress &addr, const std::string &id, JsonNode &request, JsonNode &response) {
	
		if (request["name"].str.empty() || request["id"].str.empty()) {
			response["error"] = "Say your name!";
			return;
		}

		// make sure the sender has been discovered
		// if not yet done, do a firstEncounter
		if (!validateOrigin(addr, id).known()) {
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

	m_rpcServer.on("bye", [this](const SocketAddress &addr, const std::string &id, const JsonNode &request, JsonNode &response) {
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


	// link evaluation session by request from master, this is executed on the slave
	// reponse: a list of available link candidates
	m_rpcServer.on("link-eval-session", [this](const SocketAddress &addr, const std::string &id, const JsonNode &request, JsonNode &response) {
		auto n = validateOrigin(addr, id);
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

	m_rpcServer.on("streams-available", [this](const SocketAddress &addr, const std::string &rid, const JsonNode &request, JsonNode &response) {
		auto n = validateOrigin(addr, rid);

		auto sis = m_audioManager.getAvailableStreams();

		int i = 0;
		for (auto &s : sis) {
			response[i] = streamInfo2Json(s.second);
			i++;
		}
	});

	

	// 
	/*
		sink -> source stream request
		The sink sends {source_id,links,codecs} in the request
		source_id: This is a the id of the stream source
	*/
	m_rpcServer.on("stream-start", [this](const SocketAddress &addr, const std::string &rid, const JsonNode &request, JsonNode &response) {
		auto n = validateOrigin(addr, rid);
		if (!n.exists()) {
			response["error"] = "Who are you?";
			return;
		}

		auto source_id = request["source_id"].str;
		auto links = request["links"].arr; // available links on the requesting sink
		auto codecs = request["codecs"].arr; // available codecs on the requesting sink

		if (source_id.empty()) {
			LOG(logERROR) << addr << " requested stream-start without specifiying a source_id! " << request;
			response["error"] = "Specify stream id";
			return;
		}

		auto stream = m_audioManager.stream(source_id);

		if (!stream) {
			response["error"] = "Unknown stream " + source_id;
			return;
		}

		// just choose first requested link generator!
		auto selectedLinkGen = links.front().str;

		// check if selected link is available
		auto slgi = m_linkCandidates.find(selectedLinkGen);
		if (slgi == m_linkCandidates.end()) {
			LOG(logERROR) << "selected stream link " << selectedLinkGen << " not available!";
			response["error"] = "No supported link available!";
			return;
		}

		LLLinkGenerator linkGen = slgi->second;

		// generate a random stream token for validation
		// this is included in the header of each stream frame
		auto streamToken = rand();

		response["link"] = selectedLinkGen;
		response["token"] = streamToken;
		response["info"] = streamInfo2Json(stream->getInfo());
		
		auto streamer = new AudioStreamerTx(linkGen, n, 6000);
		streamer->start(stream, streamToken);
		m_streamers.push_back(streamer);
	});

	// JS!
	m_rpcServer.on("get_graph", [this](const SocketAddress &addr, const std::string &id, JsonNode &request, JsonNode &response) {
		response["self_name"] = UP::getDeviceName();
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

		response["nodes"][i]["name"] = "dummy";
		response["nodes"][i]["id"] = "dummy" + std::to_string(i);
		response["nodes"][i]["hostname"] = "s";
		i++;
	});
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

void Controller::updateThreadMain()
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
			catch (const JsonHttpClient::Exception ex) {
				LOG(logDEBUG1) << " http client exception: " << ex.statusString;
			}
        }

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
		
        usleep(UP::UpdateIntervalUS);
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
		catch (const JsonHttpClient::Exception ex) {
			LOG(logDEBUG1) << " http client exception: " << ex.statusString;
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
#include "LLUdpQWave.h"

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
