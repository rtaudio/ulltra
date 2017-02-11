#pragma once
#include "UlltraProto.h"

#include <rtt/rtt.h>

#include "net/Discovery.h"
#include "net/LinkEval.h"
#include "masaj/JsonHttpServer.h"
#include "masaj/JsonHttpClient.h"
#include "audio/AudioIOManager.h"
#include "audio/AudioCoder.h"
#include "audio/WebAudio.h"



class LLLink;
class AudioIO;
class AudioIOStream;
class AudioStreamer;

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
	void updateThreadMain();

	void listNodes();

	Discovery m_discovery;
	LinkEval m_linkEval;
	JsonHttpClient m_client;
	JsonHttpServer m_rpcServer;
	AudioIOManager m_audioManager;

	bool m_isRunning;
	RttThread *m_updateThread;

	std::vector<Discovery::NodeDevice> m_explicitNodes;

	std::vector<std::function<void(void)>> m_asyncActions;

	std::map<std::string,Discovery::NodeDevice> m_presentNodes;

	bool firstEncounter(const Discovery::NodeDevice &node);


	JsonNode const& rpc(const Discovery::NodeDevice &node, std::string const& method, JsonNode const& params);


	Discovery::NodeDevice const& validateOrigin(const SocketAddress &addr, const JsonNode &reqestData);

	LLLinkGeneratorSet m_linkCandidates;
	AudioCoderGeneratorSet m_encoderCandidates;


	std::vector<AudioStreamer*> m_streamers;

	WebAudio m_webAudio;


	void defaultLinkCandidates();
};
