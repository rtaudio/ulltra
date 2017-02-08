#pragma once
#include "net/LLLink.h"

#include "audio/AudioCoder.h"
#include "audio/AudioIOStream.h"

class RttThread;
class AudioIOStream;

typedef std::function<bool(const uint8_t *buffer, int bufferLen, int numSamples)> BinaryAudioStreamPump;

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
	bool start(AudioIOStream *streamSource, int token);
	virtual void streamingThread() = 0;

	void update();
	bool isAlive();

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

	virtual bool inputInterleaved(float *samples, unsigned int numFrames, int numChannels, double time = 0.0) = 0;
	virtual bool outputInterleaved(float *samples, unsigned int numFrames, int numChannels, double time = 0.0) = 0;

	void notifyXRun();
};


class AudioCodingStream
{
	//friend class AudioIOManager;
	// no copy
	AudioCodingStream(AudioCodingStream&) = delete;
	AudioCodingStream& operator=(AudioCodingStream&) = delete;


public:
	struct Info {
		char deviceId[64];
		AudioCoder::EncoderParams encoderParams;
		void setDeviceId(const std::string &id) {
			id.copy(deviceId, sizeof(deviceId) - 1);
		}

		bool operator==(const Info &other) const
		{
			return (strcmp(deviceId, other.deviceId) == 0 && encoderParams == other.encoderParams);
		}
	};

	AudioCodingStream(Info streamInfo, AudioCoder::Factory coderFactory);
	~AudioCodingStream();

	void addSink(const BinaryAudioStreamPump &pump);


	bool inputInterleaved(float *samples, unsigned int numFrames, int numChannels, double time = 0.0);
	bool outputInterleaved(float *samples, unsigned int numFrames, int numChannels, double time = 0.0);

	void notifyXRun();


	inline bool stopped() const { return hasStopped; }

private:
	AudioCoder *coder;
	std::vector<uint8_t> binaryBuffer;
	std::vector<BinaryAudioStreamPump> sinks, addSinks;

	volatile bool hasSinksToAdd;
	bool hasStopped;

	double timeLastPushedToASink;

	Info params;


};



namespace std {
	template <>
	struct hash<AudioCodingStream::Info> : public std::unary_function<const AudioCodingStream::Info &, std::size_t>
	{
		inline std::size_t operator()(const AudioCodingStream::Info & obj) const
		{
			const unsigned char* p = reinterpret_cast<const unsigned char*>(&obj);
			std::size_t h = 2166136261;
			for (unsigned int i = 0; i < sizeof(obj); ++i)
				h = (h * 16777619) ^ p[i];
			return h;
		}
	};
}