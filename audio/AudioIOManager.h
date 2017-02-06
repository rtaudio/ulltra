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

#include "audio/AudioCoder.h"
#include "AudioStreamer.h"

/*
stream hash:
[Api]:[DeviceIndex]:[ChannelOffset]:[ChannelCount]@[EncoderId]:[EncoderParams]

EncoderParams = {Bitrate}
*/

class AudioStreamer;

class AudioCodingStream;



class AudioIOManager
{
private:
	struct RtaudioCallbackData {
		AudioIOManager *mgr;
		std::vector<AudioCodingStream*> streams, addStreams;
		std::vector<std::pair<AudioCodingStream*, BinaryAudioStreamPump&>> addSinks;
		volatile bool hasStreamsToAdd;
		RtaudioCallbackData() : mgr(0), hasStreamsToAdd(false) {}
	};

public:
	struct DeviceState;

	struct StreamEndpointInfo {
		std::string deviceId;
		int channelOffset;
		int numChannels;
		int sampleRate;

		StreamEndpointInfo(const DeviceState & device)
			: deviceId(device.id), channelOffset(0)
			, numChannels(device.numChannels())
			, sampleRate(device.getSampleRateCurrentlyAvailable()[0])
		{ }
	};



	struct DeviceState {
		friend class AudioIOManager;
		// no copy
		//DeviceState(const DeviceState&) = delete;
		//DeviceState& operator=(const DeviceState&) = delete;

		//DeviceState(DeviceState&&);
		//DeviceState& operator=(DeviceState&&);

		DeviceState(const DeviceState&) = delete;
		DeviceState& operator = (const DeviceState&) = delete;

		DeviceState(DeviceState&&) = default;
		DeviceState& operator = (DeviceState&&) = default;

		int index;
		std::string id;
		RtAudio::DeviceInfo info;
		RtAudio *rta;
		

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
	private:
		RtaudioCallbackData cd;

		inline DeviceState copy(const std::string &idSuffix) {
			DeviceState c;
			c.index = index;
			c.id = id + idSuffix;
			c.info = info;
			c.rta = nullptr;
			c.isLoopback = isLoopback;
			c.isCapture = isCapture;
			return c;
		}
	};


	

private:
	

	//std::map<std::string, AudioIO*> m_audioIOs;
	//std::map<std::string, AudioStreamInfo> m_availableStreams;
	//std::map<std::string, AudioIOStream*> m_activeStreams;

	std::vector<DeviceState> deviceStates;

	std::unordered_map<AudioCodingStream::Info, AudioCodingStream*> streamers;



	RtAudio rta;

	void openDevice(DeviceState &device, int sampleRate, int blockSize);

	static int rtAudioInout(void *outputBuffer, void *inputBuffer, unsigned int nBufferFrames,
		double streamTime, RtAudioStreamStatus status, void *data);

	inline DeviceState & _getDevice(const std::string &id) {
		for (auto &s : deviceStates) {
			if (s.id == id)
				return s;
		}
		return DeviceState::NoDevice;
	}

public:
	AudioIOManager();
	~AudioIOManager();

	void update();

   const std::vector <DeviceState> & getDevices() const { return deviceStates; }

   inline const DeviceState & getDevice(const std::string &id) const { 
	   for (auto &s : deviceStates) {
		   if (s.id == id)
			   return s;
	   }
	   return DeviceState::NoDevice;
   }

   void streamFrom(StreamEndpointInfo &sei, AudioCoder::EncoderParams &encoder, BinaryAudioStreamPump &&pump);


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

