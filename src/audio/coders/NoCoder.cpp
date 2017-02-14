#include "NoCoder.h"
#include <math.h>

NoCoder::NoCoder(const EncoderParams &params)
    : AudioCoder(params)
{

}


NoCoder::~NoCoder()
{

}

int NoCoder::encodeInterleaved(const float* inputSamples, int numFrames, uint8_t *outBuffer, int bufferLen)
{
        return -1;
}

void NoCoder::decodeInterleaved(const uint8_t *buffer, int bufferLen, float *samples, int numFrames)
{
        return;
}
