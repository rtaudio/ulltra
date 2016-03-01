#pragma once


#include "Discovery.h"
#include "LinkEval.h"
#include "JsonHttpServer.h"
#include "JsonHttpClient.h"
#include <rtt.h>

class LLLink;

class Controller
{
public:
    struct Params {
        std::string nodesFileName;
    };
    Controller(const Params &nodes);
	~Controller();
	bool init();

	inline bool isRunning() const { return m_isRunning; }

	inline bool isSlave(Discovery::NodeDevice const& other) {
		return other.getId().length() > 2 && other.getId() > m_discovery.getHwAddress();
	}

	inline bool isInPresentList(Discovery::NodeDevice const& other) {
		return m_presentNodes.find(other.getId()) != m_presentNodes.end();
	}

private:
	void updateThreadMain(void *arg);

	Discovery m_discovery;
	LinkEval m_linkEval;
	JsonHttpClient m_client;
	JsonHttpServer m_server;

	bool m_isRunning;
	RttThread *m_updateThread;

	std::vector<Discovery::NodeDevice> m_explicitNodes;

	std::vector<Discovery::NodeDevice> m_helloNodes;

	std::vector<std::function<void(void)>> m_asyncActions;

	std::map<std::string,Discovery::NodeDevice> m_presentNodes;

	bool firstEncounter(const Discovery::NodeDevice &node);


	JsonNode const& rpc(const Discovery::NodeDevice &node, std::string const& method, JsonNode const& params);


	Discovery::NodeDevice const& validateOrigin(const SocketAddress &addr, const std::string &id);

	LLLinkGeneratorSet m_linkCandidates;

	void defaultLinkCandidates();
};
