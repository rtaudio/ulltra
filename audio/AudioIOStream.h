#pragma once

#include "../UlltraProto.h"

#include<stdint.h>
#include <string>

#include <map>

class AudioIO;

class AudioStreamInfo {
public:
	enum class Quantization { Float	};
	enum class Direction { Capture, Playback };
	
	AudioStreamInfo()
	{
	}

	const static AudioStreamInfo invalid;

	friend bool operator== (const AudioStreamInfo &asi1, const AudioStreamInfo &asi2);

	Direction direction;
	uint32_t blockSize;
	int sampleRate;
	int channelCount;
	Quantization quantizationFormat;
	uint32_t latency;//in samples
	std::string name, id;

};

inline bool operator== (const AudioStreamInfo &asi1, const AudioStreamInfo &asi2)
{
	return (asi1.id == asi2.id); // TODO
}

typedef std::map<std::string, AudioStreamInfo> AudioStreamCollection;


class AudioIOStream
{
public:
	
	
	AudioIOStream(AudioStreamInfo const& info);
	AudioIOStream(AudioIOStream *masterStream);

	~AudioIOStream();
	
	/* getters */
	inline bool isCapture() const { return m_asi.direction == AudioStreamInfo::Direction::Capture; }
	inline std::string getName() const { return m_asi.name; }
	inline AudioStreamInfo const& getInfo() const { return m_asi; }


	/* stream ops: write */
	void blockWriteInterleaved(float *block, int channels, int nFrames);
	void blockWrite(std::vector<float *> const& channelBuffers, int nFrames);
	void blockWriteSilence(int nFrames);

	/* stream ops: read (non-blocking) */
	bool blockRead(std::vector<float *> const& channelBuffers, int nFrames);

	void notifyXRun();

	/* // so far we keep stream properties constant with an instance, they are not mutable yet
	void setBlockSize(int blockSize);
	void setSampleRate(int sampleRate);
	void setNumChannels(int channels);
	void setQuantizationFormat(AudioStreamInfo::Quantization q);
	*/

	const AudioIO *getIO();

private:
	AudioStreamInfo m_asi;
	uint64_t m_nXruns;
};



inline std::ostream & operator<<(std::ostream &os, const AudioStreamInfo& asi)
{
	os << asi.name
		<< asi.channelCount <<"ch@"<< asi.sampleRate << "Hz";
	return os;
}

inline std::ostream & operator<<(std::ostream &os, const AudioStreamCollection& asc)
{
	for (auto it = asc.begin(); it != asc.end(); it++) {
		os << it->second;
	}
	return os;
}