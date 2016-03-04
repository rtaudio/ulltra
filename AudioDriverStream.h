#pragma once

#include<stdint.h>
#include <string>

#include <map>

struct AudioStreamInfo {
	enum class Quantization {
		Float
	};

	const static AudioStreamInfo invalid;

	int blockSize;
	int sampleRate;
	int channels;
	Quantization quantizationFormat;


	std::string name, id;
};

typedef std::map<std::string, AudioStreamInfo> AudioStreamCollection;


class AudioDriverStream
{
public:
	AudioDriverStream();
	~AudioDriverStream();

	void setBlockSize(int blockSize);
	void setSampleRate(int sampleRate);
	void setNumChannels(int channels);	
	void setQuantizationFormat(AudioStreamInfo::Quantization q);

	void addBlockInterleaved(void *block, int nFrames);
	void addBlockSilemce(int nFrames);

	void notifyXRun();

	void end();
	bool start();
private:
	AudioStreamInfo m_asi;
	uint64_t m_nXruns;
};




inline std::ostream & operator<<(std::ostream &os, const AudioStreamInfo& asi)
{
	os << asi.name << "(id " << asi.id << ") " << asi.channels <<"ch@"<< asi.sampleRate << "Hz";
	return os;
}

inline std::ostream & operator<<(std::ostream &os, const AudioStreamCollection& asc)
{
	for (auto it = asc.begin(); it != asc.end(); it++) {
		os << it->second;
	}
	return os;
}