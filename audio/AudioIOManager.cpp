#include "AudioIOManager.h"

#include "audio/AudioIO.h"

#include "AudioStreamer.h"


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
	auto n = rta.getDeviceCount();
	for (auto i = 0; i < n; i++) {
		DeviceState state;
		state.index = i;
		state.info = rta.getDeviceInfo(i);

		// windows devices sometimes have some weird spaces
		myReplace(state.info.name, "  ", " ");
		myReplace(state.info.name, " )", ")");
		myReplace(state.info.name, "( ", "(");

		if (state.info.inputChannels > 0) {
			state.isCapture = true;
			state.id = state.info.id + "#in";
			deviceStates.push_back(state);
		}

		if (state.info.outputChannels > 0) {
			state.isCapture = false;
			state.id = state.info.id + "#out";
			deviceStates.push_back(state);

			if (state.info.canLoopback) {
				// create a virtual device state for loopback
				state.isLoopback = true;
				state.info.name += " loopback";
				state.id = state.info.id + "#loop";
				state.info.inputChannels = state.info.outputChannels;
				state.info.outputChannels = 0;
				state.info.duplexChannels = 0;
				state.isCapture = true;

				deviceStates.push_back(state);
			}
		}		
	}
}


AudioIOManager::~AudioIOManager()
{
	for (auto io : deviceStates) {
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

	auto streamers = ds.cd.streamers;
	bool res;
	int si = 0;
	for (auto s : streamers) {

		if (ds.isCapture) {
			res = s->inputInterleaved((float*)inputBuffer, nBufferFrames, ds.numChannels(), streamTime);
		} else {
			res = s->outputInterleaved((float*)outputBuffer, nBufferFrames, ds.numChannels(), streamTime);
		}

		if (status && res) {
			s->notifyXRun();
		}

		if (!res) {
			// remove streamer
			ds.cd.streamers.erase(ds.cd.streamers.begin() + si);
			si--;
		}

		si++;
	}

	return streamers.size() > 0 ? 0 : 1;
}

void AudioIOManager::openDevice(DeviceState &device)
{
	int sampleRate = 48000;
	unsigned int bufferFrames = 512;

	if (!device.rta) {
		device.rta = new RtAudio(rta.getCurrentApi());
	}

	if (device.isOpen())
		throw std::runtime_error("Device already open!");

	RtAudio::StreamParameters params;

	// always open all device channels here, we split them later TODO
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
		LOG(logERROR) << "Cannot open device: " << e.getMessage() << std::endl;
		throw e;
	}
}