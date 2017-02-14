#include "OpusCoder.h"

#include "pclog/pclog.h"

#include <sndfile.hh>
#include <math.h>

#include <opus/opus.h>
#include <opus/opus_multistream.h>

OpusCoder::OpusCoder(const EncoderParams &params)
    : AudioCoder(params), MSenc(nullptr), enc(nullptr), MSdec(nullptr), dec(nullptr)
{
    int numCouledStreams = params.numChannels == 2 ? 1 : 0;

    unsigned char mapping[256] = {0,1,2,3,4,5,6,7,255};


    auto appl = params.lowDelay ? OPUS_APPLICATION_RESTRICTED_LOWDELAY : OPUS_APPLICATION_AUDIO;
    int ret_err = OPUS_BAD_ARG;


    MSenc = opus_multistream_encoder_create(params.sampleRate, params.numChannels,  params.numChannels, numCouledStreams, mapping, appl, &ret_err);


          if(ret_err != OPUS_OK ) {
              throw std::runtime_error("opus_multistream_encoder_create failed!");
          }

          /** Configures the bitrate in the encoder.
            * Rates from 500 to 512000 bits per second are meaningful, as well as the
            * special values #OPUS_AUTO and #OPUS_BITRATE_MAX.
            * The value #OPUS_BITRATE_MAX can be used to cause the codec to use as much
            * rate as it can, which is useful for controlling the rate by adjusting the
            * output buffer size.
            * @see OPUS_GET_BITRATE
            * @param[in] x <tt>opus_int32</tt>: Bitrate in bits per second. The default
            *                                   is determined based on the number of
            *                                   channels and the input sampling rate.
            * @hideinitializer */
          // TODO: OPUS_AUTO
          opus_int32 bitrate = params.maxBitrate;

          if(opus_multistream_encoder_ctl(MSenc, OPUS_SET_BITRATE(bitrate))!=OPUS_OK) {
              throw std::runtime_error("Opus: failed to set bitrate!");
          }


          if(opus_encoder_ctl(enc, OPUS_SET_BANDWIDTH(OPUS_AUTO))!=OPUS_OK) {
              throw std::runtime_error("Opus: failed to set bandwidth!");
          }
           // if(opus_encoder_ctl(enc, OPUS_SET_FORCE_MODE(-2))!=OPUS_BAD_ARG)test_failed();
         // if(opus_multistream_encoder_ctl(MSenc, OPUS_GET_LSB_DEPTH(&i))!=OPUS_OK)test_failed();

          if(params.complexity >= 0) {
           if(opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY((opus_int32)params.complexity))!=OPUS_OK) {
               throw std::runtime_error("Opus: failed to set complexity!");
           }
          }


        convertBuffer.resize(1024*2);
          // TODO OPUS_SET_PACKET_LOSS_PERC (0-100) 0: default

}

//SndfileHandle fi("test.wav", SFM_WRITE, SF_FORMAT_WAV | SF_FORMAT_PCM_16, 2, 44100);

OpusCoder::~OpusCoder()
{
        if(MSenc) {
                opus_multistream_encoder_destroy(MSenc);
        }
}


int OpusCoder::encodeInterleaved(const float* inputSamples, int numSamplesPerChannel, uint8_t *outBuffer, int bufferLen)
{
    auto ts = (numSamplesPerChannel * params.params.enc.numChannels) ;
    if(ts > convertBuffer.size()) {
        convertBuffer.resize(ts * 2);
    }

    auto buf = convertBuffer.data();
    sample_copy_float_to_int16(buf, inputSamples, ts, 1);

     auto len = opus_multistream_encode(MSenc, buf, numSamplesPerChannel, outBuffer, bufferLen);
     if(len < 0) {
         LOG(logERROR) << "Opus encoder error!";
         return -1;
     }
     return len;
}

void OpusCoder::decodeInterleaved(const uint8_t *buffer, int bufferLen, float *samples, int numFrames)
{
	
}
