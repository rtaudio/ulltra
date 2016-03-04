#pragma once

struct IMMDevice;
struct IAudioClient;
struct IAudioCaptureClient;
struct tWAVEFORMATEX;
class RttThread;
class AudioDriverStream;

#include"AudioDriverStream.h"

class WindowsMM
{
public:
	WindowsMM();
	~WindowsMM();
	bool init();


	AudioStreamCollection getStreams();

private:
	void audioThread(IMMDevice *pMMDevice, AudioDriverStream *stream);

	RttThread *m_thread;

	IMMDevice *m_pDefaultMMDevice;
	IAudioClient *m_pAudioClient;
	IAudioCaptureClient *m_pAudioCaptureClient;

	void *m_hWakeUp;

	bool m_isRunning;

	tWAVEFORMATEX *m_pwfx;
	unsigned int m_nFrames;
	unsigned int m_numBufferFrames;
};

