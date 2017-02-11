#include "AudioIOManager.h"

#include "audio/AudioIO.h"
#include "audio/coders/AacCoder.h"

#include "AudioStreamer.h"

#include <chrono>
#include <thread>

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
		state.index = i;
		state.info = rta.getDeviceInfo(i);

        // windows devices sometimes have some weird spaces
        myReplace(state.info.name, "  ", " ");
        myReplace(state.info.name, " )", ")");
        myReplace(state.info.name, "( ", "(");

        if(state.info.name == "default" && state.info.outputChannels > 16) {
            continue;
        }

        if(state.info.id.empty()) {
            LOG(logWARNING) << "Audio Device " << state.info.name << " with empty id, using name.";
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



	// register coders
	coderFactories["aac"] = AudioCoder::Factory([](const AudioCoder::CoderParams &params) {
		if (params.type == AudioCoder::Encoder)
			return new AacCoder(params.params.enc);
		else
			return (AacCoder*)nullptr; // TODO
	});
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



int AudioIOManager::rtAudioInout(void *outputBuffer, void *inputBuffer, unsigned int nBufferFrames,
	double streamTime, RtAudioStreamStatus status, void *data) {
	DeviceState &ds(*(DeviceState*)data);
	RtaudioCallbackData &rcd(ds.cd);
	auto &streams(rcd.streams);

	if (rcd.hasStreamsToAdd) {
		for (auto as : rcd.addStreams)
			streams.push_back(as);
		rcd.addStreams.clear();
		rcd.hasStreamsToAdd = false;
	}
	
	bool res;
	int si = 0;

	for (auto it = streams.begin(); it != streams.end();) {
		auto &s(*it);

		if (status) {
			s->notifyXRun();
		}

		if (ds.isCapture) {
			res = s->inputInterleaved((float*)inputBuffer, nBufferFrames, ds.numChannels(), streamTime);
		} else {
			res = s->outputInterleaved((float*)outputBuffer, nBufferFrames, ds.numChannels(), streamTime);
		}


		if (!res) {
			it = streams.erase(it);
			if (streams.size() == 0)
				break;
		}
		else
			it++;
	}
	if (streams.size() == 0) {
		LOG(logDEBUG) << "end";
	}
	return streams.size() > 0 ? 0 : 1;
}

void AudioIOManager::openDevice(DeviceState &device, int sampleRate, int blockSize)
{
	unsigned int bufferFrames = (unsigned int)blockSize;

	if (!device.rta) {
		device.rta = new RtAudio(rta.getCurrentApi());
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
			&AudioIOManager::rtAudioInout, (void *)&device);
	}
	catch (RtAudioError& e) {
		LOG(logERROR) << "Failed to open device: " << e.getMessage() << std::endl;
		throw e;
	}
}


void AudioIOManager::streamFrom(StreamEndpointInfo &sei, AudioCoder::EncoderParams &encParams, BinaryAudioStreamPump pump)
{
	RttLocalLock ll(apiMutex);

	// here we manage 2 classes of resources:
	// 1. the audio devices
	// 2. the encoders

	if (sei.numChannels != encParams.numChannels || sei.channelOffset != encParams.channelOffset || sei.sampleRate != encParams.sampleRate) {
		throw std::runtime_error("StreamEndpointInfo and CoderParams do not match in channels and sample rate!");
	}

	auto &device = _getDevice(sei.deviceId);
	if (!device.exists() || !device.isCapture)
		throw std::runtime_error("Unknown device " + sei.deviceId);

	/*
	RtAudio bug?: WASAPI:
	- capture only works with bs=512,sr=44100 (event though GetMixFormat reports 48000) ???
	*/
	// open device if needed // TODO: need to close!
	bool startDevice = false;
	if (!device.isOpen()) {
		openDevice(device, sei.sampleRate, 512); // TODO
		startDevice = true;
	}
	else if(!device.rta->isStreamRunning()) {
		startDevice = true;
	}

	AudioCodingStream::Params acsi;
	acsi.setDeviceId(device.id);
	acsi.encoderParams = encParams;
	acsi.async = true; // TODO

	// find an existing streamer with same info
	// the hash includes: device and encoder params (coderName, sampleRate, channels, bitrate)
	auto streamerIt = streamers.find(acsi);
	auto isNewStreamer = (streamerIt == streamers.end());

	if (!isNewStreamer && streamerIt->second->stopped()) {
		delete streamerIt->second;
		streamers.erase(streamerIt);
		isNewStreamer = true;
	}

	if (isNewStreamer) {
		auto cfIt = coderFactories.find(encParams.coderName);
		if (cfIt == coderFactories.end()) {
			throw std::runtime_error("Unknown encoder " + std::string(encParams.coderName));
		}
		streamers[acsi] = new AudioCodingStream(acsi, cfIt->second); // TODO acsi.encoderParams
	}

	auto &streamer(streamers[acsi]);

	streamer->addSink(pump);

	while (device.cd.hasStreamsToAdd) { std::this_thread::yield(); }
	if(isNewStreamer)
		device.cd.addStreams.push_back(streamer);
	device.cd.hasStreamsToAdd = true;
	if (startDevice)
		device.rta->startStream();
}