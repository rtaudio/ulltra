#include "AudioCoder.h"

#include "AudioIOManager.h"
#include "StreamEndpointInfo.h"


#include <math.h>

#define NORMALIZED_FLOAT_MIN -1.0f
#define NORMALIZED_FLOAT_MAX  1.0f
#define SAMPLE_16BIT_SCALING  32767.0f
const short SAMPLE_16BIT_MAX = 32767;
const short SAMPLE_16BIT_MIN = -32767;


#define f_round(f) lrintf(f)

#define float_16(s, d)\
    if ((s) <= NORMALIZED_FLOAT_MIN) {\
        (d) = SAMPLE_16BIT_MIN;\
    } else if ((s) >= NORMALIZED_FLOAT_MAX) {\
        (d) = SAMPLE_16BIT_MAX;\
    } else {\
        (d) = f_round ((s) * SAMPLE_16BIT_SCALING);\
    }


AudioCoder::AudioCoder(const CoderParams &encParams) :
	params(encParams)
{

}


AudioCoder::~AudioCoder()
{
}


size_t AudioCoder::getRequiredBinaryBufferSize()
{
	size_t size = getBlockSize() * params.enc.maxBitrate / 8 / params.sampleRate;
    return size * 2 + 512;
}





// float -> int16 
void  AudioCoder::sample_copy_float_to_int16(int16_t *dst, const float *src, unsigned long nsamples, unsigned long dst_skip)
{
    while (nsamples--) {
        float_16(*src, *(dst));
        dst += dst_skip;
        src++;
    }
}


void AudioCoder::sample_copy_int16_to_float(float *dst, const int16_t *src, unsigned long nsamples, unsigned long src_skip)
{
	/* ALERT: signed sign-extension portability !!! */
	const float scaling = 1.0 / SAMPLE_16BIT_SCALING;
	while (nsamples--) {
		*dst = (*src) * scaling;
		dst++;
		src += src_skip;
	}
}

  AudioCoder::CoderParams::CoderParams(const std::string &name, CoderType type, const StreamEndpointInfo &ei)
	  : CoderParams(name, type, ei.sampleRate, ei.numChannels, ei.channelOffset)
{
}


  AudioCoder::CoderParams::CoderParams(const std::string &name, CoderType type, int sampleRate, int numChannels, int channelOffset)
	  : type(type), sampleRate(sampleRate), numChannels(numChannels), channelOffset(channelOffset), withTiming(false)
  {
	  if (numChannels == 0)
		  throw std::invalid_argument("cannot create CoderParams with 0 channels");

	  if (sampleRate == 0)
		  throw std::invalid_argument("cannot create CoderParams with fs=0");

	  std::memset(coderName, 0, sizeof(coderName));
	  if (name.size() >= sizeof(coderName) - 1) {
		  throw std::invalid_argument("coder name too long");
	  }
	  name.copy(coderName, sizeof(coderName) - 1);
  }