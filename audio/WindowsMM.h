#ifdef WIN32
#pragma once

struct IMMDevice;
struct IAudioClient;
struct IAudioCaptureClient;
struct tWAVEFORMATEX;
class RttThread;
class AudioIOStream;
class WindowsMM;

#include"AudioIOStream.h"
#include"AudioIO.h"

class WindowsMMStream :
	public AudioIOStream
{
public:
	WindowsMMStream(AudioStreamInfo const& info, IMMDevice *mmDevice, tWAVEFORMATEX const& pwfx);
	~WindowsMMStream();

	bool activate();
	void deactivate();

private:
	friend class WindowsMM;
	IMMDevice *m_pMMDevice;
	IAudioClient *m_pAudioClient;
	IAudioCaptureClient *m_pAudioCaptureClient;
	tWAVEFORMATEX m_wfx;
};


class WindowsMM :
	public AudioIO
{
public:
	WindowsMM();
	~WindowsMM();

	/* AudioIO implementations */
	bool open();
	void close();

private:
	RttThread *m_thread;
	IMMDevice *m_pDefaultMMDevice;
	void *m_hWakeUp;
	bool m_isRunning;
	std::vector<WindowsMMStream*> m_activeStreams;

	void audioThread();
	std::vector<AudioStreamInfo> getAvailableDeviceStreams();
};

#endif