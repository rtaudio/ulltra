#include "WebAudio.h"

#include "masaj/JsonHttpServer.h"
#include "autil/test.h"
#include "rtt/rtt.h"

#include "AudioIOManager.h"


#include "coders/AacCoder.h"
#include "coders/OpusCoder.h"

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
        streamParams["t"] = (double)std::clock(); // no-cache (Firefox has some weird issues with stalls otherwiese)


        response["streamUrl"] = "/streamd/monitor?" + streamParams.toQueryString();
        response["wsUrl"] = "/monitor?" + streamParams.toQueryString();

		response["ok"] = 1;
	});

    server.onStream("test", [this](const JsonHttpServer::StreamRequest &request, JsonHttpServer::StreamResponse &response) {

		const int fillBufferSeconds = 8;

		const int bufSize = 1024 * 2;
		const int fs = 48000;
		const int numChannels = 2;
		const int numSamplesMusic = 30 * fs;


        AudioCoder::EncoderParams params;
        params.reset();
        params.maxBitrate = std::stoi(request.data["bitrate"].str);
		params.numChannels = numChannels;
		params.sampleRate = fs;
		params.channelOffset = 0;
		params.setCoderName(chooseCoder(request, response));

        std::unique_ptr<AudioCoder> coderPtr(audioMgr.createEncoder(params));
        auto &coder(*coderPtr);

		int blockSize = coder.getBlockSize();

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

    server.onStream("monitor", [this](const JsonHttpServer::StreamRequest &request, JsonHttpServer::StreamResponse &response) {
        auto captureDeviceId = request.data["captureDeviceId"].str;

        AudioCoder::EncoderParams encParams;
                encParams.sampleRate = std::stoi(request.data["sampleRate"].str);
                encParams.channelOffset = std::stoi(request.data["channelOffset"].str);
                encParams.numChannels = std::stoi(request.data["numChannels"].str);
        encParams.maxBitrate = std::stoi(request.data["bitrate"].str);

	
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
		
		encParams.setCoderName(chooseCoder(request, response));

        response.setSendBufferSize(0); // instantly flush

		auto pump = BinaryAudioStreamPump([&](const uint8_t *buffer, int bufferLen, int numSamples) {
			return response.write(buffer, bufferLen);
		});

		audioMgr.streamFrom(ei, encParams, pump);

		while (response.isConnected()) {
			usleep(200 * 1000);
		}

		LOG(logINFO) << "webstream end";
	});









    server.onStream("text-stream-test", [this](const JsonHttpServer::StreamRequest & /*request*/, JsonHttpServer::StreamResponse &response) {
		response.addHeader("Content-Type", "text/html");
		response.setSendBufferSize(0);
		response.write("<html><head><title>textstream</title></head><body><pre>");
		int i = 0;
		while (true) {
			std::string l = "LINE:" + std::to_string(i++) + " @ " + std::to_string(std::clock()) + "\n";
			if (!response.write(l.c_str(), l.length() + 1))
				break;
			usleep(1000 * 1000);
		}
	});


	/*
	stream compressed audio over a websocket connection
	issues with AAC: it does not decode properly, neither using the native AudioContext.decodeAudio(), nor audiocogs/aac.js
	(NaNs in decoded audio)
	*/
    server.onWS("monitor", [this](const JsonHttpServer::WsRequest &request, JsonHttpServer::WsConnection &conn) {
        auto captureDeviceId = request.data["captureDeviceId"].str;

        AudioCoder::EncoderParams encParams;
                encParams.sampleRate = std::stoi(request.data["sampleRate"].str);
                encParams.channelOffset = std::stoi(request.data["channelOffset"].str);
                encParams.numChannels = std::stoi(request.data["numChannels"].str);
        encParams.maxBitrate = std::stoi(request.data["bitrate"].str);


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


        encParams.setCoderName("opus");
		encParams.complexity = -1;

		bool running = true; // TODO audioMgr.streamFrom should return a future or handle that can be stopped
		auto cp = &conn;
        auto pump = BinaryAudioStreamPump([cp, &running](const uint8_t *buffer, int bufferLen, int numSamples) {
            return running && cp->write({buffer, buffer + bufferLen});
        });

        audioMgr.streamFrom(ei, encParams, pump);

       while (conn.isConnected()) {
            usleep(100 * 1000);
       }
			running = false;
			usleep(400 * 1000);

        LOG(logINFO) << "webstream end";
    });


    server.onWS("test", [this](const JsonHttpServer::WsRequest &request, JsonHttpServer::WsConnection &conn) {

        int i = 0;
        while (conn.isConnected()) {
            std::string l = "LINE:" + std::to_string(i++) + " @ " + std::to_string(std::clock()) + "\n";
            if (!conn.write(std::vector<uint8_t>((uint8_t*)l.c_str(), (uint8_t*)l.c_str()+ l.length())))
                break;

            uint8_t buf[1000];
            size_t read;
            if( (read = conn.tryRead(buf, sizeof(buf)))) {
                conn.write({buf, buf + read});
            }

            usleep(1000 * 1000);
        }

        LOG(logINFO) << "webstream end";
    });
}

 std::string WebAudio::chooseCoder(const JsonHttpServer::StreamRequest &request, JsonHttpServer::StreamResponse &response) const
 {
	 auto coderName = request.data["codec"].str;

	 if (coderName.empty() || coderName == "aac")
	 {
		 response.setContentType("audio/aac");
		 coderName = "aac";
	 }
	 else {
		 if(coderName == "opus") {
			 response.setContentType("audio/ogg; codecs=opus");
		 }
	 }

	 return coderName;

	 //  request.getHeader("User-Agent");
    //if(userAgent.find("Firefox") != std::string::npos || userAgent.find("Chrome") != std::string::npos || userAgent.find("Chromium") != std::string::npos) {
    //    params.setCoderName("opus");
    //    response.setContentType("audio/opus");
    //} else {
        //params.setCoderName("aac");
        
   // }
 }
