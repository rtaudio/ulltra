
#include "UlltraProto.h"

#include<iostream>
#include "net/networking.h"

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


#if ANDROID
#include <stdio.h>
#include <string.h>
#include <sys/system_properties.h>

/* Get device name
--
1/ Compile with the Android NDK Toolchain:
arm-linux-androideabi-gcc -static pname.c -o pname

2/ Transfer on device:
adb push pname /data/local/tmp

3/ Run:
adb shell
$ /data/local/tmp/pname
[device]: [HTC/HTC Sensation Z710e]

NOTE: these properties can be queried via adb:
adb shell getprop ro.product.manufacturer */

std::string androidGetProperty(std::string prop)
{
	std::string command = "getprop " + prop;
	FILE* file = popen(command.c_str(), "r");
	if (!file) {
		return "";
	}

	char * line = NULL;
	size_t len = 0;
	std::string val;
	while ((getline(&line, &len, file)) != -1) {
		val += (std::string(line));
	}
	val = trim(val);

	// read the property value from file
	pclose(file);

	return val;
}

#endif



/*
Log::~Log()
{
	os << std::endl;
	fprintf(stderr, "%s", os.str().c_str());
	fflush(stderr);
}
*/
std::string UlltraProto::deviceName = "";

const std::string&  UlltraProto::getDeviceName()
{
	if (!deviceName.empty())
		return deviceName;

#if ANDROID
	deviceName = androidGetProperty("ro.product.manufacturer") + "/" + androidGetProperty("ro.product.model");
	LOG(logDEBUG) << "Device name: " << deviceName;
#else
	char hostname[32];
	gethostname(hostname, 31);
	deviceName = std::string(hostname);
#endif

	return deviceName;
}

