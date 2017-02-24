#include "StreamLinkManager.h"
#include "Controller.h"
#include "audio/StreamEndpointInfo.h"

/*
How a stream init works:
- a start-stream request is send (by the user) to the playback node
- the playback node sends a start-stream-capture request to the capture node
- both initiate a link, where the playbacl is master
*/


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


void StreamLinkManager::startStreamCapture(const StreamEndpointInfo &captureFrom, const AudioCoder::CoderParams &encParams, const std::string &linkName, const SocketAddress &addrWithPort) {
	StreamEndpointInfo cap2 = captureFrom; // TODO make validOrThrow not mutating
	auto &captureDevice = cap2.validOrThrow(audioManager);
	
	auto linkFactory = controller.m_linkCandidates[linkName];
	auto link = linkFactory();	

	LOG(logDEBUG) << "start-stream-capture: connecting link (as slave) to master: [ME] <-- " << *link << " --> " << addrWithPort;
	
	if (!link->connect(addrWithPort, false/*false=slave*/)) { // playback is master
		delete link;
		throw std::runtime_error("connection to master failed!");
	}
	// todo: need a running var to prevent the lamda write function to call link->send after the conn 

	// start our stream
	auto sh = audioManager.streamFrom(cap2, encParams, [link](const uint8_t *buffer, int bufferLen, int numSamples) {
		return link->send(buffer, bufferLen);
	});
	//sh.detach();
};

StreamLinkManager::StreamLinkManager(Controller &cntrl)
	: controller(cntrl), audioManager(cntrl.m_audioManager)
{
	// from gui
	// request for starting a stream, handled on playback node
	controller.m_rpcServer.on("start-stream", [this](const SocketAddress &addr, const JsonNode &request, JsonNode &response) {
		// TODO: need to authenticate browser

		auto captureNodeId = request["captureNodeId"].str;
		StreamEndpointInfo captureEndpoint(request["captureEndpoint"]);
		StreamEndpointInfo playbackEndpoint(request["playbackEndpoint"]);

		playbackEndpoint.sampleRate = captureEndpoint.sampleRate = 44100;

		playbackEndpoint.validOrThrow(audioManager);

		if (captureNodeId.empty())
			throw std::runtime_error("Specify captureNodeId and captureDeviceId!");

		// include available link names
		std::vector<std::string> linkIds;
		for (auto &l : controller.m_linkCandidates) {
			linkIds.push_back(l.first);
		}

		auto playbackSampleRates = audioManager.getDevice(playbackEndpoint.deviceId).getSampleRateCurrentlyAvailable();



		std::string selectedLink = linkIds[0];
		int linkPort = getAvailableMasterPort();
		std::string selectedCoder = audioManager.getCoderNames()[0];

		
		auto selfId = Discovery::getHwAddress();

		int captureSampleRate;


		bool selfLoop = (selfId == captureNodeId);

		auto &captureNode(selfLoop ? Discovery::NodeDevice::localAny : controller.m_discovery.getNode(captureNodeId));

		if (!selfLoop && !captureNode.exists())
			throw std::runtime_error("Unknown captureNode!");

		LinkEndpoint linkEndpoint(captureNode.getAddr(linkPort));	

		if (selfLoop) {
			linkEndpoint.sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		}

		
		auto linkFactory = controller.m_linkCandidates[selectedLink];
		auto link = linkFactory();

		LOG(logDEBUG) << "start-stream: connecting link (as master) to slave: [ME] <-- " << *link << " --> " << linkEndpoint;
		auto connectResult = std::async(std::launch::async, [link, linkEndpoint] { return link->connect(linkEndpoint, true); });


		if (selfLoop) {
			LOG(logDEBUG) << "start-stream request with self node";

			// find sample rate match
			auto captureSampleRates = audioManager.getDevice(playbackEndpoint.deviceId).getSampleRateCurrentlyAvailable();
			captureSampleRate = 44100; // findSuitableCaptureSampleRate(captureSampleRates, playbackSampleRates);
			captureEndpoint.sampleRate = captureSampleRate;

			captureEndpoint.validOrThrow(audioManager);

			AudioCoder::CoderParams encParams(selectedCoder, AudioCoder::Encoder, captureEndpoint);
			encParams.enc.maxBitrate = request["encoderParams"]["maxBitrate"].asNum();
			encParams.withTiming = true;
			
			startStreamCapture(captureEndpoint, encParams, selectedLink, linkEndpoint);
		}
		else
		{
			JsonNode captureRequest;
			captureRequest["capture"] = request["capture"];
			captureRequest["playbackSampleRates"] = playbackSampleRates;
			captureRequest["coders"] = audioManager.getCoderNames();
			captureRequest["links"] = linkIds;
			captureRequest["linkPort"] = linkPort;
			captureRequest["encoderParams"] = request["encoderParams"];

			auto captureResponse = controller.rpc(captureNode, "start-stream-capture", captureRequest);

			selectedLink = captureResponse["link"].str;
			selectedCoder = captureResponse["coder"].str;
			captureSampleRate = captureResponse["sampleRate"].asNum();
			linkEndpoint = captureNode.getAddr(linkPort);
			
			LOG(logDEBUG) << "captureResponse: " << captureResponse;
		}


		if (!connectResult.get()) {
			throw std::runtime_error("Connection to slave failed!");
		}

		LOG(logINFO) << "Connected to slave!";

		AudioCoder::CoderParams decParams(selectedCoder, AudioCoder::Decoder, playbackEndpoint);
		decParams.sampleRate = captureSampleRate;
		decParams.withTiming = true;

		audioManager.streamTo([link](uint8_t*buf, int max) {
			return link->receive(buf, max);
		}, decParams, playbackEndpoint);
	});




	// from playback node
	controller.m_rpcServer.on("start-stream-capture", [this](const SocketAddress &addr, const JsonNode &request, JsonNode &response) {
		auto n = controller.validateOrigin(addr, request);
		if (!n.exists()) {
			response["error"] = "Who are you?";
			return;
		}

		StreamEndpointInfo captureEndpoint(request["capture"]);
		auto &playbackSampleRatesJ = request["playbackSampleRates"].arr;
		std::vector<std::string> otherCoders (request["coders"].arr.begin(), request["coders"].arr.end());
		std::vector<std::string> otherLinks(request["links"].arr.begin(), request["links"].arr.end());
		int linkPort = request["linkPort"].asNum();
		auto &encParamsJ = request["encParams"];


		auto &captureDevice = captureEndpoint.validOrThrow(audioManager);
		

		// find sample rate match
		auto captureSampleRates = captureDevice.getSampleRateCurrentlyAvailable();
		std::vector<int> playbackSampleRates(playbackSampleRatesJ.begin(), playbackSampleRatesJ.end());
		int captureSampleRate = findSuitableCaptureSampleRate(captureSampleRates, playbackSampleRates);


		// find link match // TODO need to do link evaluation
		std::string selectedLink;
		for (auto &l : controller.m_linkCandidates) {
			if (std::find(otherLinks.begin(), otherLinks.end(), l.first) != otherLinks.end()) {
				selectedLink = l.first;
				break;
			}
		}
		if (selectedLink.empty()) {
			throw std::runtime_error("No compatible link available");
		}

		
		// find coder 
		std::string selectedCoder;
		for (auto &cn : audioManager.getCoderNames()) {
			if (std::find(otherCoders.begin(), otherCoders.end(), cn) != otherCoders.end()) {
				selectedCoder = cn;
				break;
			}
		}
		if (selectedCoder.empty()) {
			throw std::runtime_error("No compatible coder available");
		}



		AudioCoder::CoderParams encParams(selectedCoder, AudioCoder::Encoder, captureEndpoint);
		encParams.enc.maxBitrate = encParamsJ["bitrate"].asNum();		
	
		startStreamCapture(captureEndpoint, encParams, selectedLink, n.getAddr(linkPort));

		
		response["link"] = selectedLink;
		response["coder"] = selectedCoder;
		response["sampleRate"] = captureSampleRate;
		response["ok"] = 1;
	});

}


int StreamLinkManager::findSuitableCaptureSampleRate(std::vector<int> capture, std::vector<int> playback)
{
	// skip first entry because this is preferred sample rate
	std::sort(capture.begin() + 1, capture.end(), AudioIOManager::compareSampleRatesTo48KHz);
	std::sort(playback.begin() + 1, playback.end(), AudioIOManager::compareSampleRatesTo48KHz);

	std::vector<int> sampleRateMatches;
	for (auto &psr : playback) {
		for (auto csr : capture) {
			if (csr == psr) {
				return csr;
			}
		}
	}

	return capture[0];
}


StreamLinkManager::~StreamLinkManager()
{

}

int StreamLinkManager::getAvailableMasterPort()
{
	std::vector<int> portsInUse;
	for (auto s : activeStreams_) {
		portsInUse.push_back(s.port);
	}

	int p = UP::NetStreamPort;

	while (std::find(portsInUse.begin(), portsInUse.end(), p) != portsInUse.end()) {
		p++;
	}

	return p;
}



// 
/*
sink -> source stream request
The sink sends {source_id,links,codecs} in the request
source_id: This is a the id of the stream source
*/
/*
m_rpcServer.on("stream-start", [this](const SocketAddress &addr, const JsonNode &request, JsonNode &response) {
auto n = validateOrigin(addr, request);
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

//auto streamer = new AudioStreamerTx(linkGen, n, 6000);
//streamer->start(stream, streamToken);
//m_streamers.push_back(streamer);
});*/