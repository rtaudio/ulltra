#pragma once

#include "../UlltraProto.h"

#include "rtaudio/RtAudio.h"

#ifdef WITH_JACK
#include "../audio/JackClient.h"
#endif
#ifdef _WIN32
#include "../audio/WindowsMM.h"
#endif

#include <unordered_map>
#include  <atomic>
#include <future>

#include "AudioCoder.h"
#include "../AudioStreamer.h"

#include <rtt/rtt.h>

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
		std::atomic<bool> hasStreamsToAdd;

        uint64_t lastCallbackTime;
        uint64_t lastIOTime;

        RtaudioCallbackData() : mgr(0), hasStreamsToAdd(false), lastCallbackTime(0), lastIOTime(0) {}


		RtaudioCallbackData(RtaudioCallbackData&& other)
			: hasStreamsToAdd(other.hasStreamsToAdd.load())	{
			mgr = other.mgr;
		}
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

        RtAudio::Api api;
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
            c.api = api;
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

	std::unordered_map<AudioCodingStream::Params, AudioCodingStream*> streamers;

	struct CoderDescription {
		AudioCoder::Factory factory;
		std::vector<int> supportedSampleRates;
	};

	std::map<std::string, CoderDescription> coderFactories;


	mutable RttMutex apiMutex;

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

     AudioCoder *createEncoder(const AudioCoder::EncoderParams &encParams);

   inline const std::vector <DeviceState> & getDevices() const { 
	   return deviceStates;
   }

   inline const DeviceState & getDevice(const std::string &id) const { 
	   for (auto &s : deviceStates) {
		   if (s.id == id)
			   return s;
	   }
	   return DeviceState::NoDevice;
   }


   std::future<void> streamFrom(StreamEndpointInfo &endpoint, AudioCoder::EncoderParams &encoder, BinaryAudioStreamPump pump);
   std::future<void> streamTo(BinaryAudioStreamPump pull, AudioCoder::DecoderParams &decParams, StreamEndpointInfo &endpoint);


   static bool compareSampleRatesTo48KHz(int a, int b) {
	   const int desiredSr = 48000; // (48000 + 44100 * 2) / 2 - 1;
	   a -= desiredSr;
	   b -= desiredSr;

	   // unbalanced-abs comparasion, rate-down lower sample rates
	   if (a < 0) a *= -8;
	   if (b < 0) b *= -8;

	   return a < b;
   }

   private:
	   void openEndpoint_(StreamEndpointInfo &endpoint, const AudioCoder::CoderParams &coderParams);
};

