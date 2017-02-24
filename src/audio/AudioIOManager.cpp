#include <chrono>
#include <thread>


#include "AudioIOManager.h"
#include "AudioIO.h"
#include "StreamEndpointInfo.h"

#include "coders/AacCoder.h"
#include "coders/OpusCoder.h"


/*
 *
 *
 * TODO
 * - device guard, check for last callback time, if beafore 4 sec, restart device
 */

AudioIOManager::DeviceState AudioIOManager::DeviceState::NoDevice;


std::vector<int> AudioIOManager::DeviceState::getSampleRateCurrentlyAvailable() const {
	if (isOpen())
		return{ (int)rta->getStreamSampleRate() };

	std::vector<int> rates({ (int)info.preferredSampleRate });
	for (auto sr : info.sampleRates) {
		if(sr != info.preferredSampleRate && sr > 22000)
			rates.push_back((int)sr);
	}

	std::sort(rates.begin() + 1, rates.end(), AudioIOManager::compareSampleRatesTo48KHz);

	return rates;
}

void myReplace(std::string& str,
	const std::string& oldStr,
	const std::string& newStr)
{
	std::string::size_type pos = 0u;
	while ((pos = str.find(oldStr, pos)) != std::string::npos) {
		str.replace(pos, oldStr.length(), newStr);
		pos += newStr.length();
}
}

AudioIOManager::AudioIOManager()
{
	/*
	std::vector<AudioIO*> aios;
#ifdef WITH_JACK
	aios.push_back(new JackClient());
#endif

#ifdef _WIN32
	aios.push_back(new WindowsMM());
#endif
*/

    auto enumApiDevices = [this] (RtAudio &rta){
	/*
	here we create our device list.
	On Windows, for each physical device RtAudio lists a seperate device for input and output
	with eather inputChannels > 0 or outputChannels > 0, but not both
	On Alsa, each RtAudio device represents a physical device. We can open 2 RtAudio devices
	pointing to the same physical device, but one for input and one for output only.
	So we create an extra "virtual" device if a physical device has both input and output.
	Additionally, create a "virtual" loopback device if a playback device supports it
	*/
    auto n = (int)rta.getDeviceCount();
	for (auto i = 0; i < n; i++) {
		DeviceState state;
        state.api = rta.getCurrentApi();
		state.index = i;
		state.info = rta.getDeviceInfo(i);

        // windows devices sometimes have some weird spaces
        myReplace(state.info.name, "  ", " ");
        myReplace(state.info.name, " )", ")");
        myReplace(state.info.name, "( ", "(");

        if(state.info.name == "default" && state.info.outputChannels > 16) {
            continue;
        }

        if(!state.info.probed) {
            continue;
        }

        if(state.info.id.empty()) {
            //LOG(logWARNING) << "Audio Device " << state.info.name << " with empty id, using name.";
            state.info.id = state.info.name;
        }

		state.id = state.info.id;



		if (state.info.inputChannels > 0) {
			state.isCapture = true;
			deviceStates.emplace_back(state.copy("#in"));
		}

		if (state.info.outputChannels > 0) {
			state.isCapture = false;
			deviceStates.emplace_back(state.copy("#out"));

			if (state.info.canLoopback) {
				// create a virtual device state for loopback
				state.isLoopback = true;
				state.info.name += " loopback";
				state.info.inputChannels = state.info.outputChannels;
				state.info.outputChannels = 0;
				state.info.duplexChannels = 0;
				state.isCapture = true;

				deviceStates.emplace_back(state.copy("#loop"));
			}
		}		
	}
    };



    // iterate over all apis and enum their devices
    std::vector<RtAudio::Api> apis;
     RtAudio::getCompiledApi(apis);
    for(RtAudio::Api api : apis) {
       RtAudio rta(api);
       enumApiDevices(rta);
    }



	// register coders


    // AAC
	coderFactories["aac"] = { AudioCoder::Factory([](const AudioCoder::CoderParams &params) {
		return new AacCoder(params);
	}), {8000, 16000, 22050, 44100, 48000, 88200, 96000}, false };



#ifdef WITH_OPUS // Opus Codec    
	coderFactories["opus"] = { AudioCoder::Factory([](const AudioCoder::CoderParams &params) {
		if (params.type == AudioCoder::Encoder)
			return new OpusCoder(params);
		else
			return (OpusCoder*)nullptr; // TODO
	}), {48000}, false };
#endif
}


AudioIOManager::~AudioIOManager()
{
	for (auto &io : deviceStates) {
		if (io.rta) {
			delete io.rta;
			io.rta = nullptr;
		}
	}
}

void AudioIOManager::update()
{
}



AudioCoder *AudioIOManager::createEncoder(const AudioCoder::CoderParams &encParams) {
    auto cfIt = coderFactories.find(encParams.coderName);
    if (cfIt == coderFactories.end()) {
		throw std::runtime_error("Unknown encoder!");
        return nullptr;
    }
    auto &f(cfIt->second);
    return f.factory(encParams);
}





void AudioIOManager::openDevice(DeviceState &device, int sampleRate, int blockSize)
{
	unsigned int bufferFrames = (unsigned int)blockSize;

	if (!device.rta) {
        device.rta = new RtAudio(device.api);
	}

	if (device.isOpen())
		throw std::runtime_error("Device already open!");

	RtAudio::StreamParameters params;

	// always open all device channels here, we split them later TODO
	params.deviceId = device.index;
	params.firstChannel = 0;
	params.nChannels = device.numChannels();

	device.cd.mgr = this;

	try {
		device.rta->openStream(device.isCapture ? NULL : &params,
			device.isCapture ? &params : NULL,
			RTAUDIO_FLOAT32,
			sampleRate,
			&bufferFrames,
            &rtAudioInout, (void *)&device);
	}
	catch (RtAudioError& e) {
		LOG(logERROR) << "Failed to open device: " << e.getMessage() << std::endl;
		throw e;
	}
}


ActiveStreamHandle AudioIOManager::streamFrom(const StreamEndpointInfo &sei, const AudioCoder::CoderParams &encParams, BinaryAudioStreamPump &&pump)
{
	RttLocalLock ll(apiMutex);

	if (encParams.type != AudioCoder::Encoder) {
		throw std::invalid_argument("called streamFrom() with decoder params!");
	}

	// here we manage 2 classes of resources:
	// 1. the audio devices
	// 2. the encoders

	auto &device = startEndpoint_(sei, encParams);	


	if (!device.isCapture)
		throw std::runtime_error("Cannot stream from playback endpoint " + device.info.name);

	AudioStreamerEncoding::Params acsi(device.id, encParams);
	acsi.async = true; // TODO

	// find an existing streamer with same info
	// the hash includes: device and encoder params (coderName, sampleRate, channels, bitrate)
	auto streamerIt = streamersTx.find(acsi);
	auto isNewStreamer = (streamerIt == streamersTx.end());

	// check existing streamer stopped and re-create it
	if (!isNewStreamer && streamerIt->second->stopped()) {
		delete streamerIt->second;
		streamersTx.erase(streamerIt);
		isNewStreamer = true;
	}

	if (isNewStreamer) {
		auto cfIt = coderFactories.find(encParams.coderName);
		if (cfIt == coderFactories.end()) {
			throw std::runtime_error("Unknown encoder " + std::string(encParams.coderName));
		}
		streamersTx[acsi] = new AudioStreamerEncoding(acsi, cfIt->second.factory); // TODO acsi.encoderParams
	}

	auto &streamer(streamersTx[acsi]);

	streamer->addSink(pump);

    if(isNewStreamer) {
		streamer->start();

		while (device.cd.hasStreamsToAdd) { using namespace std::chrono_literals;  std::this_thread::sleep_for(10ms); }
		device.cd.addStreams.push_back(streamer);
		LOG(logDEBUG) << "notifying audio io thread about new streamer";
		device.cd.hasStreamsToAdd = true;
    }
	

	return ActiveStreamHandle(streamer); // todo: this is not correct, streamers are shared
}



ActiveStreamHandle AudioIOManager::streamTo(BinaryStreamRead &&pull, const AudioCoder::CoderParams &decParams, const StreamEndpointInfo &sei)
{
	RttLocalLock ll(apiMutex);

	if (decParams.type != AudioCoder::Decoder) {
		throw std::invalid_argument("called streamTo() with encoder params!");
	}
	
	auto &device = startEndpoint_(sei, decParams);		

	if (device.isCapture)
		throw std::runtime_error("Cannot stream to capture endpoint " + device.info.name);
	
	AudioStreamerCoding::Params acsi(device.id, decParams);
	acsi.async = true; // TODO

	auto streamer = (AudioStreamerDecoding*)newStreamer(acsi);
	streamer->setSource(std::move(pull));	
	streamer->start();


	// todo: put this in a function:
	while (device.cd.hasStreamsToAdd) { using namespace std::chrono_literals;  std::this_thread::sleep_for(10ms); }
	device.cd.addStreams.push_back(streamer);
	LOG(logDEBUG) << "notifying audio io thread about new streamer";
	device.cd.hasStreamsToAdd = true;

	return ActiveStreamHandle(streamer);
}


AudioIOManager::DeviceState & AudioIOManager::startEndpoint_(const StreamEndpointInfo &endpoint, const AudioCoder::CoderParams &coderParams)
{
	if (endpoint.numChannels != coderParams.numChannels
		|| endpoint.channelOffset != coderParams.channelOffset
		|| endpoint.sampleRate != coderParams.sampleRate) {
		throw std::runtime_error("StreamEndpointInfo and CoderParams do not match in channels and sample rate!");
	}

	auto &device = _getDevice(endpoint.deviceId);
	if (!device.exists())
		throw std::runtime_error("Unknown device " + endpoint.deviceId);

	if (device.isCapture != (coderParams.type == AudioCoder::Encoder)) {
		throw std::runtime_error("Coder type does not fit device!");
	}

	/*
	RtAudio bug?: WASAPI:
	- capture only works with bs=512,sr=44100 (event though GetMixFormat reports 48000) ???
	*/
	// open device if needed // TODO: need to close!
	bool startDevice = false;
	if (!device.isOpen()) {
		openDevice(device, endpoint.sampleRate, 512); // TODO
		startDevice = true;
	}
	else if (!device.rta->isStreamRunning()) {
		startDevice = true;
	}


	if (startDevice)
		device.rta->startStream();

	return  device;
}

AudioStreamerCoding *AudioIOManager::newStreamer(const AudioStreamerCoding::Params &params)
{

	auto cfIt = coderFactories.find(params.coderParams.coderName);
	if (cfIt == coderFactories.end()) {
		throw std::runtime_error("Unknown encoder " + std::string(params.coderParams.coderName));
	}

	if (params.coderParams.type == AudioCoder::Encoder) {
		return new AudioStreamerEncoding(params, cfIt->second.factory);
	}
	else
	{
		return new AudioStreamerDecoding(params, cfIt->second.factory);
	}
}

int AudioIOManager::rtAudioInout(void *outputBuffer, void *inputBuffer, unsigned int nBufferFrames,
	double streamTime, RtAudioStreamStatus status, void *data) {
	DeviceState &ds(*(DeviceState*)data);
	RtaudioCallbackData &rcd(ds.cd);
	auto &streams(rcd.streams);

	rcd.lastCallbackTime = UP::getWallClockSeconds();

	if (rcd.hasStreamsToAdd) {
		LOG(logDEBUG) << "rtAudioInout adding stream!" << rcd.addStreams.size();
		if (rcd.addStreams.size() == 0) {
			LOG(logERROR) << "hasStreamsToAdd but empty vector!";
		}
		for (auto as : rcd.addStreams)
			streams.push_back(as);
		rcd.addStreams.clear();
		rcd.hasStreamsToAdd = false;
	}

	bool res;
	bool anyOk = false;

	for (auto it = streams.begin(); it != streams.end();) {
		auto &s(*it);

		if (status) {
			s->notifyXRun();
		}

		if (ds.isCapture) {
			res = s->inputInterleaved((float*)inputBuffer, nBufferFrames, ds.numChannels(), ds.clock);
		}
		else {
			res = s->outputInterleaved((float*)outputBuffer, nBufferFrames, ds.numChannels(), ds.clock);
		}


		if (!res) {
			it = streams.erase(it);
			LOG(logDEBUG) << "removed stream, still active:" << streams.size();
			if (streams.size() == 0)
				break;
		}
		else {
			it++;
			anyOk = true;
		}
	}

	if (anyOk) {
		rcd.lastIOTime = rcd.lastCallbackTime;
	}

	ds.clock += nBufferFrames;

	if (streams.size() == 0 && (rcd.lastCallbackTime - rcd.lastIOTime) > 4) {
		LOG(logDEBUG) << "rtAudioInout end after 4 seconds, no more streams!";
		return 1;
	}
	return 0;
}