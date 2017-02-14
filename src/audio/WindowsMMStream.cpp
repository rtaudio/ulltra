#include "WindowsMM.h"

#include <windows.h>
#include <mmsystem.h>
#include <mmeapi.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
//#include <avrt.h>
//#include <intsafe.h>
//#include <functiondiscoverykeys_devpkey.h>


WindowsMMStream::WindowsMMStream(AudioStreamInfo const& info, IMMDevice *mmDevice, tWAVEFORMATEX const& wfx) :
	AudioIOStream(info), m_pMMDevice(mmDevice), m_wfx(wfx),
	m_pAudioCaptureClient(0), m_pAudioClient(0)
{
}

WindowsMMStream::~WindowsMMStream()
{
	if (m_pAudioCaptureClient) {
		m_pAudioCaptureClient->Release();
	}

	if (m_pAudioClient) {
		m_pAudioClient->Stop();
		m_pAudioClient->Release();
	}
	
	m_pMMDevice->Release();
}


bool WindowsMMStream::activate()
{
	HRESULT hr;

	// activate an IAudioClient
	hr = m_pMMDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&m_pAudioClient);
	if (FAILED(hr)) {
		LOG(logERROR) << "IMMDevice::Activate(IAudioClient) failed: hr = " << hr;
		return false;
	}

	hr = m_pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&m_pAudioCaptureClient);
	if (FAILED(hr)) {
		printf("IAudioClient::GetService(IAudioCaptureClient) failed: hr 0x%08x\n", hr);
		return false;
	}


	// call IAudioClient::Initialize
	// note that AUDCLNT_STREAMFLAGS_LOOPBACK and AUDCLNT_STREAMFLAGS_EVENTCALLBACK
	// do not work together...
	// the "data ready" event never gets set
	// so we're going to do a timer-driven loop
	hr = m_pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
		//AUDCLNT_SHAREMODE_EXCLUSIVE, // TODO: period size only applies on exclusive sharemode!!
		AUDCLNT_STREAMFLAGS_LOOPBACK,
		// hnsMinimumDevicePeriod/2, hnsMinimumDevicePeriod/2,
		0, 0,
		&m_wfx, 0
		);
	if (FAILED(hr)) {
		printf("IAudioClient::Initialize failed: hr = 0x%08x\n", hr);
		return false;
	}

	// call IAudioClient::Start
	hr = m_pAudioClient->Start();
	if (FAILED(hr)) {
		printf("IAudioClient::Start failed: hr = 0x%08x\n", hr);
		return false;
	}
}

void WindowsMMStream::deactivate()
{
	// todo
}