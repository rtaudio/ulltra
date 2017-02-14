#include "AacCoder.h"

#include "pclog/pclog.h"

#include <sndfile.hh>

AacCoder::AacCoder(const EncoderParams &params) : AudioCoder(params), convert_buf(0)
{
	/* AOT:
	- 2: MPEG-4 AAC Low Complexity.
    - 5: MPEG-4 AAC Low Complexity with Spectral Band Replication (HE-AAC).
    - 29: MPEG-4 AAC Low Complexity with Spectral Band Replication and Parametric Stereo (HE-AAC v2).
        This configuration can be used only with stereo input audio data.
    - 23: MPEG-4 AAC Low-Delay.
    - 39: MPEG-4 AAC Enhanced Low-Delay. Since there is no ::AUDIO_OBJECT_TYPE for ELD in
	combination with SBR defined, enable SBR explicitely by ::AACENC_SBR_MODE parameter.
	*/
	int aot = 2; // must be 2 for TT_MP4_ADTS (web streams)

	/*!< Configure SBR independently of the chosen Audio Object Type ::AUDIO_OBJECT_TYPE.
	This parameter is for ELD audio object type only.
	- -1: Use ELD SBR auto configurator (default).
	- 0: Disable Spectral Band Replication.
	- 1: Enable Spectral Band Replication. */
	int eld_sbr = 0; // was 0


	/*!< Bitrate mode. Configuration can be different kind of bitrate configurations:
	- 0: Constant bitrate, use bitrate according to ::AACENC_BITRATE. (default)
	Within none LD/ELD ::AUDIO_OBJECT_TYPE, the CBR mode makes use of full allowed bitreservoir.
	In contrast, at Low-Delay ::AUDIO_OBJECT_TYPE the bitreservoir is kept very small.
	- 8: LD/ELD full bitreservoir for packet based transmission. */
	int vbr = 0;


	/* only used if vbr == 0 */
	int bitrate = (params.maxBitrate <= 0) ? 256000 : params.maxBitrate;


	/*!< This parameter controls the use of the afterburner feature.
	The afterburner is a type of analysis by synthesis algorithm which increases the
	audio quality but also the required processing power. It is recommended to always
	activate this if additional memory consumption and processing power consumption
	is not a problem. If increased MHz and memory consumption are an issue then the MHz
	and memory cost of this optional module need to be evaluated against the improvement
	in audio quality on a case by case basis.
	- 0: Disable afterburner (default).
	- 1: Enable afterburner. */
	int afterburner = 1;


	switch (params.numChannels) {
	case 1: mode = MODE_1;       break;
	case 2: mode = MODE_2;       break;
	case 3: mode = MODE_1_2;     break;
	case 4: mode = MODE_1_2_1;   break;
	case 5: mode = MODE_1_2_2;   break;
	case 6: mode = MODE_1_2_2_1; break;
	default:
		throw std::runtime_error("Unsupported channels");
	}

	if (aacEncOpen(&handle, 0, params.numChannels) != AACENC_OK) {
		throw std::runtime_error("Unable to open encoder");
	}

	if (aacEncoder_SetParam(handle, AACENC_AOT, aot) != AACENC_OK) {
		throw std::runtime_error("Unable to set the AOT");
	}

	if (aot == 39 && eld_sbr) {
		if (aacEncoder_SetParam(handle, AACENC_SBR_MODE, 1) != AACENC_OK) {
			throw std::runtime_error("Unable to set SBR mode for ELD");			
		}
	}

	if (aacEncoder_SetParam(handle, AACENC_SAMPLERATE, params.sampleRate) != AACENC_OK) {
		throw std::runtime_error("Unable to set the sample rate!");
	}

	if (aacEncoder_SetParam(handle, AACENC_CHANNELMODE, mode) != AACENC_OK) {
		throw std::runtime_error("Unable to set the channel mode");
	}

	/*!< Input audio data channel ordering scheme:
	- 0: MPEG channel ordering (e. g. 5.1: C, L, R, SL, SR, LFE). (default)
	- 1: WAVE file format channel ordering (e. g. 5.1: L, R, C, LFE, SL, SR). */
	if (aacEncoder_SetParam(handle, AACENC_CHANNELORDER, 1) != AACENC_OK) {
		throw std::runtime_error("Unable to set the wav channel order");
	}

	if (vbr) {
		if (aacEncoder_SetParam(handle, AACENC_BITRATEMODE, vbr) != AACENC_OK) {
			throw std::runtime_error("Unable to set the VBR bitrate mode\n");
		}
	}
	else {
		if (aacEncoder_SetParam(handle, AACENC_BITRATE, bitrate) != AACENC_OK) {
			throw std::runtime_error("Unable to set the bitrate");
		}
	}

	/*!< Transport type to be used. See ::TRANSPORT_TYPE in FDK_audio.h. Following
	types can be configured in encoder library:
	- 0: raw access units
	- 1: ADIF bitstream format
	- 2: ADTS bitstream format
	- 6: Audio Mux Elements (LATM) with muxConfigPresent = 1
	- 7: Audio Mux Elements (LATM) with muxConfigPresent = 0, out of band StreamMuxConfig
	- 10: Audio Sync Stream (LOAS) */
	if (aacEncoder_SetParam(handle, AACENC_TRANSMUX, 2) != AACENC_OK) {
		throw std::runtime_error("Unable to set the ADTS transmux\n");
	}


	if (aacEncoder_SetParam(handle, AACENC_AFTERBURNER, afterburner) != AACENC_OK) {
		throw std::runtime_error("Unable to set the afterburner mode\n");
	}

	auto res = aacEncEncode(handle, NULL, NULL, NULL, NULL);
	if (res != AACENC_OK) {
		throw std::runtime_error("Unable to initialize the encoder");
	}
	if (aacEncInfo(handle, &info) != AACENC_OK) {
		throw std::runtime_error("Unable to get the encoder info");
	}

	int input_size = params.numChannels * 2 * info.frameLength;
	LOG(logINFO) << "AAC frameLength=" << info.frameLength << ", inputSize=" << input_size;

	convert_buf = (int16_t*)malloc(input_size);
}

//SndfileHandle fi("test.wav", SFM_WRITE, SF_FORMAT_WAV | SF_FORMAT_PCM_16, 2, 44100);

AacCoder::~AacCoder()
{
	if(convert_buf)
		free(convert_buf);
}

int AacCoder::encodeInterleaved(const float* inputSamples, int numSamplesPerChannel, uint8_t *outBuffer, int bufferLen)
{
	AACENC_BufDesc in_buf = { 0 }, out_buf = { 0 };
	AACENC_InArgs in_args = { 0 };
	AACENC_OutArgs out_args = { 0 };
	int in_identifier = IN_AUDIO_DATA;
	int in_size, in_elem_size;
	int out_identifier = OUT_BITSTREAM_DATA;
	int out_size, out_elem_size;
	int read;

	int numChannels = params.params.enc.numChannels;


	void *in_ptr, *out_ptr;

	//fi.write(inputSamples, numFrames * numChannels);
	//return 0;

    int numSamples = numSamplesPerChannel * numChannels;
    read = numSamples * sizeof(int16_t);

    if (numSamplesPerChannel <= 0) {
		in_args.numInSamples = -1;
	}
	else {

		// float -> int16_t
        sample_copy_float_to_int16(convert_buf, inputSamples, numSamples, 1);

		// here we assums 16bit PCM interleaved per sample
		in_ptr = convert_buf;
		in_size = read;
		in_elem_size = 2;

		in_args.numInSamples = read / 2; // 16 bit = 2*1bytes
		in_buf.numBufs = 1;
		in_buf.bufs = &in_ptr;
		in_buf.bufferIdentifiers = &in_identifier;
		in_buf.bufSizes = &in_size;
		in_buf.bufElSizes = &in_elem_size;
	}

	out_ptr = outBuffer;
	out_size = bufferLen;
	out_elem_size = 1;
	out_buf.numBufs = 1;
	out_buf.bufs = &out_ptr;
	out_buf.bufferIdentifiers = &out_identifier;
	out_buf.bufSizes = &out_size;
	out_buf.bufElSizes = &out_elem_size;


	auto err = aacEncEncode(handle, &in_buf, &out_buf, &in_args, &out_args);

	if (err != AACENC_OK) {
		if (err == AACENC_ENCODE_EOF) {
			return -1;
		}
        if(err == AACENC_ENCODE_ERROR) {
            LOG(logERROR) << "AACENC_ENCODE_ERROR, probably outBuffer to small (is " << bufferLen << ")";
        }
		return -1;
		//throw std::runtime_error("Encoding failed");
	}

	if (out_args.numOutBytes == 0)
		return 0;

	return out_args.numOutBytes;
}

void AacCoder::decodeInterleaved(const uint8_t *buffer, int bufferLen, float *samples, int numFrames)
{
	
}
