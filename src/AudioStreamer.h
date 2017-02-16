#pragma once
#include "net/LLLink.h"

#include "audio/AudioCoder.h"
#include "audio/AudioIOStream.h"

#include "concurrentqueue/concurrentqueue.h"

class RttThread;
class AudioIOStream;

typedef std::function<bool(const uint8_t *buffer, int bufferLen, int numSamples)> BinaryAudioStreamPump;

typedef float DefaultSampleType;

struct StreamFrameHeader {
	uint16_t streamToken;//a "unique" stream token constant over all frames in a stream to avoid routing and format errors.
	uint16_t frameIndex;//the index of the first sample within the frame

	union data
	{
		uint8_t firstByte;
	};

	data content;

	const int Overhead = sizeof(StreamFrameHeader) - sizeof(content);
};


class AudioStreamer
{
	friend class AudioIOManager;

public:
	AudioStreamer(LLLinkGenerator &linkGen, const Discovery::NodeDevice &nd, int port);
	~AudioStreamer();

	// Controller API:
	void update();
	bool isAlive();
	bool start(AudioIOStream *streamSource, int token);

	virtual void streamingThread() = 0;// TODO

	// Audio API:
	virtual bool inputInterleaved(float *samples, unsigned int numFrames, int numChannels, double time = 0.0) = 0;
	virtual bool outputInterleaved(float *samples, unsigned int numFrames, int numChannels, double time = 0.0) = 0;
	void notifyXRun();




protected:
	LLLink *m_link;
	AudioIOStream *m_aio;
	uint16_t m_token;
	bool m_isRunning;
	int m_numErrorsLink, m_numErrorsStream;

	int m_samplesPerFrame;

	void reportLinkError();
	void reportStreamError();

private:
	RttThread *m_thread;



	
};


class AudioCodingStream
{
	//friend class AudioIOManager;
	// no copy
	AudioCodingStream(AudioCodingStream&) = delete;
	AudioCodingStream& operator=(AudioCodingStream&) = delete;


public:
	struct Params {
		char deviceId[64];
		bool async;
		AudioCoder::EncoderParams encoderParams;

		Params() { memset(this, 0, sizeof(*this)); }

		void setDeviceId(const std::string &id) {
			id.copy(deviceId, sizeof(deviceId) - 1);
		}

		bool operator==(const Params &other) const
		{
			return (strcmp(deviceId, other.deviceId) == 0 && encoderParams == other.encoderParams);
		}
	};

	AudioCodingStream(Params streamInfo, AudioCoder::Factory coderFactory);
	~AudioCodingStream();

	// Controller API:
	void addSink(const BinaryAudioStreamPump &pump);
	inline bool stopped() const { return hasStopped; }

	// Audio API:
	bool inputInterleaved(float *samples, unsigned int numFrames, int numChannels, double time = 0.0);
	bool outputInterleaved(float *samples, unsigned int numFrames, int numChannels, double time = 0.0);
	void notifyXRun();

private:
	void codingThread();

	bool _inputInterleaved(float *samples, unsigned int numFrames, int numChannels, double time = 0.0);

	Params params;

	struct asyncData {
		moodycamel::ConcurrentQueue<DefaultSampleType> queue;
		RttThread * thread;
	};

	asyncData async;

	

	AudioCoder *coder;
	std::vector<uint8_t> binaryBuffer;

	std::vector<BinaryAudioStreamPump> sinks, addSinks;
	volatile bool hasSinksToAdd;

	bool hasStopped;
	double timeLastPushedToASink;

	bool energySaving;
};



namespace std {
	template <>
	struct hash<AudioCodingStream::Params> : public std::unary_function<const AudioCodingStream::Params &, std::size_t>
	{
		inline std::size_t operator()(const AudioCodingStream::Params & obj) const
		{
			const unsigned char* p = reinterpret_cast<const unsigned char*>(&obj);
			std::size_t h = 2166136261;
			for (unsigned int i = 0; i < sizeof(obj); ++i)
				h = (h * 16777619) ^ p[i];
			return h;
		}
	};
}