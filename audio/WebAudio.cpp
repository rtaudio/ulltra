#include "WebAudio.h"

#include "net/JsonHttpServer.h"
#include "audio/coders/AacCoder.h"
#include "autil/test.h"
#include "rtt/rtt.h"

#include "audio/AudioIOManager.h"

WebAudio::WebAudio(AudioIOManager &audioMgr) : audioMgr(audioMgr)
{
}


WebAudio::~WebAudio()
{
}

void WebAudio::registerWithServer(JsonHttpServer &server)
{
	this->server = &server;


	server.on("web-monitor-stream", [this](const SocketAddress &addr, const JsonNode &request, JsonNode &response) {
		auto captureDeviceId = request["captureDeviceId"].str;
		auto channelOffset = request["channelOffset"].num;
		auto numChannels = request["numChannels"].num;

		auto &captureDevice = audioMgr.getDevice(captureDeviceId);
		if (!captureDevice.exists() || !captureDevice.isCapture) {
			LOG(logERROR) << "start-stream-capture request with non-existing capture device!";
			response["error"] = "Capture devices does not exist!";
			return;
		}

		if (channelOffset < 0 || std::isnan(channelOffset))
			channelOffset = 0;

		if (numChannels == 0 || std::isnan(numChannels))
			numChannels = captureDevice.numChannels() - channelOffset;

		if (captureDevice.numChannels() < (channelOffset + numChannels)) {
			LOG(logERROR) << "start-stream-capture request with more channels than capture device has!";
			response["error"] = "Invalid channel count for capture!";
			return;
		}

		// TODO: temporary fix to 44khz, due to RtAudio bug with WASAPI
		auto sampleRate = 44100; // captureDevice.getSampleRateCurrentlyAvailable()[0];

		response["sampleRate"] = sampleRate;

		JsonNode streamParams;
		streamParams["captureDeviceId"] = captureDeviceId;
		streamParams["channelOffset"] = channelOffset;
		streamParams["numChannels"] = numChannels;
		streamParams["sampleRate"] = sampleRate;
		streamParams["bitrate"] = 64000;


		response["streamUrl"] = "/streamd/monitor?" + streamParams.toQueryString();

		response["ok"] = 1;
	});

	server.onStream("test", [this](const SocketAddress &addr, const JsonNode &request, JsonHttpServer::StreamResponse &response) {
		response.addHeader("Content-Type", "audio/aac");
		const int fillBufferSeconds = 8;

		const int bufSize = 1024 * 2;
		const int fs = 48000;
		const int numChannels = 2;
		const int numSamplesMusic = 30 * fs;


		AacCoder::EncoderParams params;
		params.maxBitrate = std::stoi(request["bitrate"].str);
		params.numChannels = numChannels;
		params.sampleRate = fs;
		params.channelOffset = 0;

		AacCoder coder(params);
		int blockSize = coder.getFrameLength();

		std::vector<float> musicBuf(numSamplesMusic, 0.0f);
		autil::test::generateMusic(musicBuf.data(), numSamplesMusic, true);
		size_t musicPtr = 0;

		std::vector<float> sweepBuf(numSamplesMusic, 0.0f);
		autil::test::generateSweep(sweepBuf.data(), numSamplesMusic, (float)fs);
		size_t sweepPtr = 0;

		uint8_t buf[bufSize];

		std::vector<float> frame(blockSize * numChannels);

		auto pump = [&]() {
			for (int i = 0; i < blockSize; i++) {
				frame[i * numChannels + 0] = musicBuf[(musicPtr++) % numSamplesMusic];
				frame[i * numChannels + 1] = sweepBuf[(sweepPtr++) % numSamplesMusic];
			}

			int numBytes = coder.encodeInterleaved(frame.data(), blockSize, buf, bufSize);
			if (numBytes == -1)
				return false;
			if (!response.write(buf, numBytes))
				return false;

			return true;
		};


		// prefill buffers
		int fillBlocks = (fillBufferSeconds * fs / blockSize);
		for (int i = 0; i < fillBlocks; i++) {
			if (!pump())
				return;

		}

		std::function<bool()> wait;
		RttTimer ti(&wait, (int)(blockSize * 1e9 / fs), 0);

		while (pump() && wait()) {}

		LOG(logWARNING) << "Streaming end!";
	});

	server.onStream("monitor", [this](const SocketAddress &addr, const JsonNode &request, JsonHttpServer::StreamResponse &response) {
		auto captureDeviceId = request["captureDeviceId"].str;

		AudioCoder::EncoderParams encParams;
		encParams.sampleRate = std::stoi(request["sampleRate"].str);
		encParams.channelOffset = std::stoi(request["channelOffset"].str);
		encParams.numChannels = std::stoi(request["numChannels"].str);
		encParams.maxBitrate = std::stoi(request["bitrate"].str);
		encParams.setCoderName("aac");


	
		auto &captureDevice = audioMgr.getDevice(captureDeviceId);
		if (!captureDevice.exists() || !captureDevice.isCapture) {
			throw std::runtime_error("monitor stream with non-existing capture device!");
		}

		if (captureDevice.numChannels() < (encParams.channelOffset + encParams.numChannels)) {
			throw std::runtime_error("monitor stream start with more channels than capture device has!");
		}


		AudioIOManager::StreamEndpointInfo ei(captureDevice);
		ei.sampleRate = encParams.sampleRate;
		ei.channelOffset = encParams.channelOffset;
		ei.numChannels = encParams.numChannels;
		
		
		response.addHeader("Content-Type", "audio/aac");

		auto pump = BinaryAudioStreamPump([&](const uint8_t *buffer, int bufferLen, int numSamples) {
			return response.write(buffer, bufferLen);
		});

		audioMgr.streamFrom(ei, encParams, pump);

		while (response.isConnected()) {
			usleep(200 * 1000);
		}

		LOG(logINFO) << "webstream end";
	});
}
