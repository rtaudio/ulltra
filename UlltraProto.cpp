
#include "UlltraProto.h"

#include<iostream>


#ifdef _WIN32
LARGE_INTEGER UlltraProto::timerFreq;
VOID(WINAPI*UlltraProto::myGetSystemTime)(_Out_ LPFILETIME);
static UINT gPeriod = 0;
#endif

bool UlltraProto::init() {

#ifdef _WIN32
	TIMECAPS caps;

	QueryPerformanceFrequency(&timerFreq);
	if (timeGetDevCaps(&caps, sizeof(TIMECAPS)) != TIMERR_NOERROR) {
		LOG(logERROR) <<  ("InitTime : could not get timer device");
		return false;
	}
	else {
		gPeriod = caps.wPeriodMin;
		if (timeBeginPeriod(gPeriod) != TIMERR_NOERROR) {
			LOG(logERROR) << ("InitTime : could not set minimum timer");
			gPeriod = 0;
			return false;
		}
		else {
			LOG(logINFO) << "InitTime : multimedia timer resolution set to " << gPeriod << " milliseconds";
		}
	}

	// load GetSystemTimePreciseAsFileTime
	FARPROC fp;
	myGetSystemTime = 0;
	auto hk32 = LoadLibraryW(L"kernel32.dll");
	if (!hk32) {
		LOG(logERROR) << ("Failed to load kernel32.dll");
		return false;
	}
	
	fp = GetProcAddress(hk32, "GetSystemTimePreciseAsFileTime");
	if (fp == NULL) {
		LOG(logERROR) << ("Failed to load GetSystemTimePreciseAsFileTime()");
		return false;
	}

	myGetSystemTime = (VOID(WINAPI*)(LPFILETIME)) fp;
#endif

	return true;
}




/*
Log::~Log()
{
	os << std::endl;
	fprintf(stderr, "%s", os.str().c_str());
	fflush(stderr);
}
*/