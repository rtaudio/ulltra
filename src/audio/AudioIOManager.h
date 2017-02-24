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
#include "../AudioStreamerCoding.h"
#include "../AudioStreamerEncoding.h"
#include "../AudioStreamerDecoding.h"

#include <rtt/rtt.h>

/*
stream hash:
[Api]:[DeviceIndex]:[ChannelOffset]:[ChannelCount]@[EncoderId]:[EncoderParams]

EncoderParams = {Bitrate}
*/

struct StreamEndpointInfo;

struct ActiveStreamHandle {
	// no copy
	ActiveStreamHandle(const ActiveStreamHandle&) = delete;
	ActiveStreamHandle& operator=(const ActiveStreamHandle&) = delete;
	// move only
	ActiveStreamHandle(ActiveStreamHandle&&) = default;
	ActiveStreamHandle& operator = (ActiveStreamHandle&&) = default;

	ActiveStreamHandle(AudioStreamerCoding *streamer) {} // TODO

	bool isAlive() const { return true; }
	//void requestCancel();
	//void waitUntilEnd() const;

	//void detach();

	// destructor syncs
	//~ActiveStreamHandle() { waitUntilEnd(); }
};


class AudioIOManager
{
private:
	struct RtaudioCallbackData {
		AudioIOManager *mgr;
		std::vector<AudioStreamerCoding*> streams, addStreams;
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

		uint32_t clock;

		bool isLoopback;
		bool isCapture;

		DeviceState(): rta(nullptr), isLoopback(false), index(-1), clock(0) {}
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

	// keep encoding streamers in a map to share them
	std::unordered_map<AudioStreamerCoding::Params, AudioStreamerEncoding*> streamersTx;

	std::vector<AudioStreamerDecoding*> streamersRx;

	struct CoderDescription {
		AudioCoder::Factory factory;
		std::vector<int> supportedSampleRates;
		bool lossless;
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

     AudioCoder *createEncoder(const AudioCoder::CoderParams &encParams);

	 inline const std::vector <DeviceState> & getDevices() const {
		 return deviceStates;
	 }

	 inline const std::vector <std::string> getCoderNames() const {
		 std::vector<std::string> names;
		 for (auto it : coderFactories) {
			 names.push_back(it.first);
		 }
		 return names;
	 }

   inline const DeviceState & getDevice(const std::string &id) const { 
	   for (auto &s : deviceStates) {
		   if (s.id == id)
			   return s;
	   }
	   return DeviceState::NoDevice;
   }


   ActiveStreamHandle streamFrom(const StreamEndpointInfo &endpoint, const AudioCoder::CoderParams &encoder, BinaryAudioStreamPump &&pump);
   ActiveStreamHandle streamTo(BinaryStreamRead &&pull, const AudioCoder::CoderParams &decParams, const StreamEndpointInfo &endpoint);


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
	   DeviceState &startEndpoint_(const StreamEndpointInfo &endpoint, const AudioCoder::CoderParams &coderParams);
	   AudioStreamerCoding *newStreamer(const AudioStreamerCoding::Params &params);
};
