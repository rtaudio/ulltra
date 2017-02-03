#pragma once

#include "UlltraProto.h"

#include "rtaudio/RtAudio.h"

#ifdef WITH_JACK
#include "audio/JackClient.h"
#endif
#ifdef _WIN32
#include "audio/WindowsMM.h"
#endif

#include <unordered_map>

/*
stream hash:
[Api]:[DeviceIndex]:[ChannelOffset]:[ChannelCount]@[EncoderId]:[EncoderParams]

EncoderParams = {Bitrate}
*/

class AudioStreamer;

class AudioIOManager
{
public:
	struct DeviceState;

	struct StreamEndpointInfo {
		std::string deviceId;
		int channelOffset;
		int numChannels;
		int sampleRate;

		StreamEndpointInfo(const DeviceState & device)
			: channelOffset(0)
			, numChannels(device.numChannels())
			, sampleRate(device.getSampleRateCurrentlyAvailable()[0])
		{ }
	};

	struct RtaudioCallbackData {
		AudioIOManager *mgr;
		std::vector<AudioStreamer*> streamers;
	};

	struct DeviceState {
		int index;
		std::string id;
		RtAudio::DeviceInfo info;
		RtAudio *rta;
		RtaudioCallbackData cd;

		bool isLoopback;

		bool isCapture;

		DeviceState(): rta(nullptr), isLoopback(false), index(-1) {}
		inline bool isOpen() const { return rta && rta->isStreamOpen(); }
		inline int numChannels() const {
			return isCapture ? info.inputChannels : info.outputChannels;
		}


		std::vector<int> getSampleRateCurrentlyAvailable() const;

		inline bool exists() const { return index >= 0; }

		static DeviceState NoDevice;
	};


private:
	//std::map<std::string, AudioIO*> m_audioIOs;
	//std::map<std::string, AudioStreamInfo> m_availableStreams;
	//std::map<std::string, AudioIOStream*> m_activeStreams;

	std::vector<DeviceState> deviceStates;



	RtAudio rta;

	void openDevice(DeviceState &device);

	static int rtAudioInout(void *outputBuffer, void *inputBuffer, unsigned int nBufferFrames,
		double streamTime, RtAudioStreamStatus status, void *data);

public:
	AudioIOManager();
	~AudioIOManager();

	void update();

   //AudioStreamCollection getAvailableStreams();

   const std::vector <DeviceState> & getDevices() const { return deviceStates; }

   inline const DeviceState & getDevice(const std::string &id) const { 
	   for (auto &s : deviceStates) {
		   if (s.id == id)
			   return s;
	   }
	   return DeviceState::NoDevice;
   }


   //AudioIOStream *stream(std::string const& id);

   static bool compareSampleRatesTo48KHz(int a, int b) {
	   const int desiredSr = 48000; // (48000 + 44100 * 2) / 2 - 1;
	   a -= desiredSr;
	   b -= desiredSr;

	   // unbalanced-abs comparasion, rate-down lower sample rates
	   if (a < 0) a *= -8;
	   if (b < 0) b *= -8;

	   return a < b;
   }
};

