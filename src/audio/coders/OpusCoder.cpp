#include "OpusCoder.h"

#include "pclog/pclog.h"

#include <sndfile.hh>
#include <math.h>

#include <opus.h>
#include <opus_multistream.h>

OpusCoder::OpusCoder(const CoderParams &params)
    : AudioCoder(params), MSenc(nullptr), enc(nullptr), MSdec(nullptr), dec(nullptr)
{

    int numCouledStreams = params.numChannels == 2 ? 1 : 0;
	int numStreams = params.numChannels == 2 ? 1 : params.numChannels;

	//unsigned char mapping[256] = { 0,1,2,3,4,5,6,7,255 };
	unsigned char mapping[256] = { 0,1,255 };


    auto appl = params.enc.lowDelay ? OPUS_APPLICATION_RESTRICTED_LOWDELAY : OPUS_APPLICATION_AUDIO;
    int ret_err = -10;

	if (params.sampleRate != 48000)
		LOG(logWARNING) << "Warning: Opus coder requestes with other sample rate than 48000, using 48000!";
		//throw std::invalid_argument("Opus only supports fs=48kHz");

    MSenc = opus_multistream_encoder_create(48000, params.numChannels,  numStreams, numCouledStreams, mapping, appl, &ret_err);


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
          opus_int32 bitrate = params.enc.maxBitrate;

          if(opus_multistream_encoder_ctl(MSenc, OPUS_SET_BITRATE(bitrate))!=OPUS_OK) {
              throw std::runtime_error("Opus: failed to set bitrate!");
          }

		  

          if(opus_multistream_encoder_ctl(MSenc, OPUS_SET_BANDWIDTH(OPUS_AUTO))!=OPUS_OK) {
              throw std::runtime_error("Opus: failed to set bandwidth!");
          }
           // if(opus_encoder_ctl(enc, OPUS_SET_FORCE_MODE(-2))!=OPUS_BAD_ARG)test_failed();
         // if(opus_multistream_encoder_ctl(MSenc, OPUS_GET_LSB_DEPTH(&i))!=OPUS_OK)test_failed();

          if(params.enc.complexity >= 0) {
           if(opus_multistream_encoder_ctl(MSenc, OPUS_SET_COMPLEXITY((opus_int32)params.enc.complexity))!=OPUS_OK) {
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

int OpusCoder::getHeader(uint8_t *outBuffer, int bufferLen) const {
	//https://wiki.xiph.org/OggOpus
	uint8_t head[19];
	auto hp(head);

	if (bufferLen < sizeof(head)) {
		throw std::runtime_error("buffer to small to create header");
	}

	strcpy((char*)hp, "OpusHead"); hp += 8;
	*hp = 0x01; hp += 1; // ver
	*hp = params.numChannels; hp += 1; // numChannels
	
	*reinterpret_cast<uint16_t*>(hp) = 3840;  // pre-skip (spec says convergences after 3840)
	hp += 2;

	if (params.sampleRate != 48000) {
		LOG(logWARNING) << "Opus Warning: sending header with 48KHz samplring rate, it actually is " << params.sampleRate;
	}
	
	*reinterpret_cast<uint32_t*>(hp) = 48000;  // params.sampleRate; // sampleRate
	hp += 4;

	*reinterpret_cast<int16_t*>(hp) = 0;// gain
	hp += 2;
	
	*hp = params.numChannels <= 2 ? 0 : 1; // channel mapping family (0 = one stream: mono or L,R stereo)

	memcpy(outBuffer, head, sizeof(head));
	return sizeof(head);
}

int OpusCoder::encodeInterleaved(const float* inputSamples, int numSamplesPerChannel, uint8_t *outBuffer, int bufferLen)
{
	/*
	about opus frame_size: take a look at opus_encoder.c / frame_size_select()
	Opus supports frame durations of 2.5 ms, 5 ms, 10 ms, etc.
	We fix Fs to 48Khz, want low latency, so our min frame size is 120 samples
	*/


    auto ts = (numSamplesPerChannel * params.numChannels) ;
    if(ts > convertBuffer.size()) {
        convertBuffer.resize(ts * 2);
    }

    auto buf = convertBuffer.data();
    sample_copy_float_to_int16(buf, inputSamples, ts, 1);

     auto len = opus_multistream_encode(MSenc, buf, numSamplesPerChannel, outBuffer, bufferLen);
     if(len < 0) {
         LOG(logERROR) << "Opus encoder error encoding " << numSamplesPerChannel << " samples per channel";
         return -1;
     }
     return len;
}

int OpusCoder::decodeInterleaved(const uint8_t *buffer, int bufferLen, float *samples, int numSamplesPerChannel)
{
	return -1;
}
