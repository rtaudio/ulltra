#include "AudioCoder.h"



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


AudioCoder::AudioCoder(const EncoderParams &encParams) :
	params(encParams)
{

}


AudioCoder::~AudioCoder()
{
}


size_t AudioCoder::getRequiredBinaryBufferSize()
{
	size_t size = getBlockSize() * params.params.enc.maxBitrate / 8 / params.params.enc.sampleRate;
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
