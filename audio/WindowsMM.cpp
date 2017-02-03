#ifdef _WIN32
#include "WindowsMM.h"
#include "../UlltraProto.h"
#include "../net/networking.h"

#include <windows.h>
#include <stdio.h>
#include <mmsystem.h>
#include <mmeapi.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <intsafe.h>
#include <functiondiscoverykeys_devpkey.h>

#include "AudioIOStream.h"

#include <rtt/rtt.h>

std::string getLongDeviceName(IMMDevice *dev, std::string &id);
IMMDevice *getDefaultDevice(bool capture);


WindowsMM::WindowsMM() :
	m_pDefaultMMDevice(0), m_hWakeUp(0), m_thread(0)
{
}

WindowsMM::~WindowsMM()
{
	m_isRunning = false;

	if(m_thread)
		delete m_thread;

	if (m_hWakeUp) {
		CancelWaitableTimer(m_hWakeUp);
		CloseHandle(m_hWakeUp);
	}



	if (m_pDefaultMMDevice)
		m_pDefaultMMDevice->Release();
	
	CoUninitialize();
}


WindowsMMStream *getDeviceStream(IMMDevice *pMMDevice)
{
	HRESULT hr;
	IAudioClient *pAudioClient;
	tWAVEFORMATEX wfx;

	// activate an IAudioClient
	hr = pMMDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient);
	if (FAILED(hr)) {
		LOG(logERROR) << "IMMDevice::Activate(IAudioClient) failed: hr = " << hr << " " << lastError();
		return 0;
	}

	// get the default device periodicity
	REFERENCE_TIME hnsDefaultDevicePeriod, hnsMinimumDevicePeriod;
	hr = pAudioClient->GetDevicePeriod(&hnsDefaultDevicePeriod, &hnsMinimumDevicePeriod);
	if (FAILED(hr)) {
		pAudioClient->Release();
		printf("IAudioClient::GetDevicePeriod failed: hr = 0x%08x\n", hr);
		return 0;
	}

	REFERENCE_TIME streamLatency;
	uint32_t blockSize;

	pAudioClient->GetStreamLatency(&streamLatency);
	pAudioClient->GetBufferSize(&blockSize);

	{
		// get the default device format
		tWAVEFORMATEX *pwfx;
		hr = pAudioClient->GetMixFormat(&pwfx);
		pAudioClient->Release(); // we only use it to getmixformat!

		if (FAILED(hr)) {			
			printf("IAudioClient::GetMixFormat failed: hr = 0x%08x\n", hr);
			return 0;
		}

		wfx = *pwfx;
		CoTaskMemFree(pwfx);
	}

	if (wfx.wFormatTag != WAVE_FORMAT_EXTENSIBLE) {
		printf("Expected    WAVE_FORMAT_EXTENSIBLE\n");
		return 0;
	}
	else
	{
		PWAVEFORMATEXTENSIBLE pEx = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(&wfx);
		if (pEx->SubFormat != KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
		{
			printf("Expected SubFormat  KSDATAFORMAT_SUBTYPE_IEEE_FLOAT\n");
			return 0;
		}
	}

	AudioStreamInfo asi;
	asi.channelCount = (wfx.nChannels);
	asi.quantizationFormat = (AudioStreamInfo::Quantization::Float);
	asi.sampleRate = wfx.nSamplesPerSec;
	asi.name = getLongDeviceName(pMMDevice, asi.id/*out*/);
	asi.latency = streamLatency;
	asi.blockSize = blockSize;
	asi.direction = AudioStreamInfo::Direction::Capture;

	return new WindowsMMStream(asi, pMMDevice, wfx);
}

void WindowsMM::audioThread()
{
	HRESULT hr;

	// create a periodic waitable timer
	m_hWakeUp = CreateWaitableTimer(NULL, FALSE, NULL);
	if (NULL == m_hWakeUp) {
		DWORD dwErr = GetLastError();
		printf("CreateWaitableTimer failed: last error = %u\n", dwErr);
		return;
	}

	LONGLONG perfFreq, perfTickNow;
	QueryPerformanceFrequency((LARGE_INTEGER*)&perfFreq);
	long long blockIndex = 0, prevBI = 0;
	long long prevTick = 0;

	// set the waitable timer
	LARGE_INTEGER liFirstFire;
	liFirstFire.QuadPart = -1; // +1ms from now
	LONG lTimeBetweenFires = 1; // 1ms (lowest possible)
	BOOL bOK = SetWaitableTimer(m_hWakeUp, &liFirstFire, lTimeBetweenFires, NULL, NULL, FALSE);
	if (!bOK) {
		DWORD dwErr = GetLastError();
		printf("SetWaitableTimer failed: last error = %u\n", dwErr);
		return;
	}

	// loopback capture loop
	DWORD dwWaitResult;

	bool bDone = false;
	bool bFirstPacket = true;
	while (m_isRunning)
	{
		dwWaitResult = WaitForSingleObject(m_hWakeUp, INFINITE);
		if (WAIT_OBJECT_0 != dwWaitResult) {
			LOG(logERROR) << "Unexpected WaitForMultipleObjects return value " << dwWaitResult;
			break;
		}

		for (auto stream : getActiveStreams())
		{
			auto mmStream = static_cast<WindowsMMStream*>(stream);

			// check if we have data yet
			UINT32 nNextPacketSize;
			hr = mmStream->m_pAudioCaptureClient->GetNextPacketSize(&nNextPacketSize);
			if (FAILED(hr)) {
				LOG(logERROR) << "IAudioCaptureClient::GetNextPacketSize failed " << hr;
				mmStream->deactivate();
				continue;
			}

			if (nNextPacketSize <= 0)
				continue;

			// get the captured data
			BYTE *pData;
			UINT32 nNumFramesToRead;
			DWORD dwFlags;

			hr = mmStream->m_pAudioCaptureClient->GetBuffer(&pData, &nNumFramesToRead, &dwFlags, NULL, NULL);
			if (FAILED(hr)) {
				LOG(logERROR) << "IAudioCaptureClient::GetBuffer failed " << hr;
				mmStream->deactivate();
				continue;
			}


			if ((AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY & dwFlags) == AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
				stream->notifyXRun();
				LOG(logDEBUG) << "IAudioCaptureClient AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY";
			}
			else if ((AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR & dwFlags) == AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) {
				LOG(logDEBUG) << "IAudioCaptureClient AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR";
			}

			if (0 == nNumFramesToRead) {
				LOG(logERROR) << "IAudioCaptureClient::GetBuffer said to read 0 frames on pass";
				mmStream->deactivate();
				continue;
			}

			if ((AUDCLNT_BUFFERFLAGS_SILENT & dwFlags) == AUDCLNT_BUFFERFLAGS_SILENT)
			{
				stream->blockWriteSilence(nNumFramesToRead);
			}
			else
			{
				stream->blockWriteInterleaved((float*)pData, mmStream->m_wfx.nBlockAlign, nNumFramesToRead);
			}

			hr = mmStream->m_pAudioCaptureClient->ReleaseBuffer(nNumFramesToRead);
			if (FAILED(hr)) {
				LOG(logERROR) << "IAudioCaptureClient::ReleaseBuffer failed! " << hr;
				mmStream->deactivate();
				break;
			}
		} // for each stream
	} // while(running)
}



bool WindowsMM::open()
{
	if (m_isRunning)
		return false;

	HRESULT hr = S_OK;

	m_isRunning = true;

	hr = CoInitialize(NULL);
	if (FAILED(hr)) {
		LOG(logERROR) << "CoInitialize failed: hr = 0x%08x " << lastError();
		return false;
	}

	// inital update of available endpoints
	endpointsAdd(getAvailableDeviceStreams());

/*	hr = get_default_device(&m_pDefaultMMDevice);
	if (FAILED(hr)) {
		return false;
	}	
	LOG(logINFO) << "Default Device: " << getDeviceStreamInfo(m_pDefaultMMDevice);
*/

	m_thread = new RttThread([this]() {
		audioThread();
	}, true, "mmaudio" );

	return 0;
}

void WindowsMM::close()
{
	for (auto s : m_activeStreams)
	{
		delete s;
	}
	m_activeStreams.clear();
}


std::vector<AudioStreamInfo> WindowsMM::getAvailableDeviceStreams()
{
	std::vector<AudioStreamInfo> streamList;

	HRESULT hr = S_OK;

	// get an enumerator
	IMMDeviceEnumerator *pMMDeviceEnumerator;

	hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void**)&pMMDeviceEnumerator
		);
	if (FAILED(hr)) {
		printf("CoCreateInstance(IMMDeviceEnumerator) failed: hr = 0x%08x\n", hr);
		return streamList;
	}

	IMMDeviceCollection *pMMDeviceCollection;

	// get all the active render endpoints
	hr = pMMDeviceEnumerator->EnumAudioEndpoints(
		eRender, DEVICE_STATE_ACTIVE, &pMMDeviceCollection
		);
	pMMDeviceEnumerator->Release();
	if (FAILED(hr)) {
		printf("IMMDeviceEnumerator::EnumAudioEndpoints failed: hr = 0x%08x\n", hr);
		return streamList;
	}

	UINT count;
	hr = pMMDeviceCollection->GetCount(&count);
	if (FAILED(hr)) {
		pMMDeviceCollection->Release();
		printf("IMMDeviceCollection::GetCount failed: hr = 0x%08x\n", hr);
		return streamList;
	}
	printf("Active render endpoints found: %u\n", count);

	for (UINT i = 0; i < count; i++) {
		IMMDevice *pMMDevice;

		// get the "n"th device
		hr = pMMDeviceCollection->Item(i, &pMMDevice);
		if (FAILED(hr)) {
			pMMDeviceCollection->Release();
			printf("IMMDeviceCollection::Item failed: hr = 0x%08x\n", hr);
			return streamList;
		}
		
		// temporarlye create a stream to retrieve stream format
 		auto ds = getDeviceStream(pMMDevice);
		if (ds) {
			streamList.push_back(ds->getInfo());
			delete ds;
		}
	}
	pMMDeviceCollection->Release();

	return streamList;
}


std::string getLongDeviceName(IMMDevice *dev, std::string &id)
{
	HRESULT hr = S_OK;
	std::string name;

	// open the property store on that device
	IPropertyStore *pPropertyStore;
	hr = dev->OpenPropertyStore(STGM_READ, &pPropertyStore);
	if (FAILED(hr)) {
		printf("IMMDevice::OpenPropertyStore failed: hr = 0x%08x\n", hr);
		return "";
	}

	// get the long name property
	PROPVARIANT pv; PropVariantInit(&pv);
	hr = pPropertyStore->GetValue(PKEY_Device_FriendlyName, &pv);
	if (FAILED(hr)) {
		printf("IPropertyStore::GetValue failed: hr = 0x%08x\n", hr);
		return "";
	}

	if (VT_LPWSTR != pv.vt) {
		printf("PKEY_Device_FriendlyName variant type is %u - expected VT_LPWSTR", pv.vt);

		PropVariantClear(&pv);
		return "";
	}

	typedef std::codecvt_utf8<wchar_t> convert_type;
	std::wstring_convert<convert_type, wchar_t> converter;
	name = converter.to_bytes(pv.pwszVal);

	PropVariantClear(&pv);

	LPWSTR str;
	dev->GetId(&str);
	id = converter.to_bytes(str);
	return name;
}

IMMDevice *getDefaultDevice(bool capture)
{
	HRESULT hr = S_OK;
	IMMDeviceEnumerator *pMMDeviceEnumerator;
	IMMDevice *ppMMDevice;

	// activate a device enumerator
	hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void**)&pMMDeviceEnumerator
		);
	if (FAILED(hr)) {
		printf("CoCreateInstance(IMMDeviceEnumerator) failed: hr = 0x%08x\n", hr);
		return 0;
	}

	// get the default render endpoint
	hr = pMMDeviceEnumerator->GetDefaultAudioEndpoint(capture? eCapture : eRender, eConsole, &ppMMDevice);
	pMMDeviceEnumerator->Release();
	if (FAILED(hr)) {
		printf("IMMDeviceEnumerator::GetDefaultAudioEndpoint failed: hr = 0x%08x\n", hr);
		return 0;
	}

	return ppMMDevice;
}

#endif