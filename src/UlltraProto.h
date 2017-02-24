#pragma once

// windows screw up
#define NOMINMAX

#ifdef ANDROID
#include "ulltra-android/android-compat.h"
#endif

#include <stdint.h>

#ifdef _WIN32
#define _WINSOCKAPI_    // stops windows.h including winsock.h
#include<Windows.h>
#else
#include <sys/time.h>
#endif

#include <sstream>
#include <iostream>


#define MATLAB_REPORTS
#ifdef MATLAB_REPORTS
#include<fstream>
#include<vector>
#endif


#ifndef ANDROID
#include <thread>
#endif


// for trim function:
#include <algorithm>
#include <functional>
#include <cctype>
#include <locale>
#include <string>
#include <atomic>

#include "pclog/pclog.h"


#define ENERGY_SAVING 1

class UlltraProto {
private:
	static std::string deviceName;

public:
	static const int DiscoveryPort = 26025;
	static const int BroadcastIntervalMs = 2000; //milliseconds

	static const int LinkEvalPort = 26100;
	static const int LinkEvalTimeoutMS = 200; // milliseconds

    static const int DiscoveryTcpQueue = 8;
    static const int DiscoveryTcpTimeout = 800; // milliseconds

	static const int HttpControlPort = 26080;
    static const int HttpServerThreadPoolSize = 8;

	static const int HttpConnectTimeout = 0;// milliseconds
	static const int HttpResponseTimeoutMs = 1200;// milliseconds
	static const int HttpResponseTimeoutRand = 400;// milliseconds

	static const int UpdateIntervalUS = 20*1000;


	static const int TcpConnectTimeout = 4000; // milliseconds


	static const int NetStreamPort = 4000;


	static bool init();
	static const std::string& getDeviceName();

    static void tick() {
        static clock_t lastTick = 0;
        auto now = std::clock();
        if(lastTick > 0) {
            tickSeconds += (now - lastTick) / CLOCKS_PER_SEC;
        }
        lastTick = now;
    }

    static std::atomic<uint64_t> tickSeconds;

	inline static uint64_t getMicroSeconds()
	{
#ifndef _WIN32
		uint64_t t;
		struct timeval tv;
		gettimeofday(&tv, 0);
        t = (uint64_t)tv.tv_sec * (uint64_t)1e6 + (uint64_t)tv.tv_usec;
		return t;
#else
		LARGE_INTEGER t1;
		QueryPerformanceCounter(&t1);
		return (uint64_t)(((double)t1.QuadPart) / ((double)timerFreq.QuadPart) * 1000000.0);
#endif
	}

    inline static uint64_t getWallClockSeconds()
    {
        return tickSeconds.load();
    }

	// this is faster
	inline static uint64_t getMicroSecondsCoarse()
	{
#ifndef _WIN32
		uint64_t t;
		struct timeval tv;
		gettimeofday(&tv, 0);
        t = (uint64_t)tv.tv_sec * (uint64_t)1e6 + (uint64_t)tv.tv_usec;
		return t;
#else
		LARGE_INTEGER t1;
		QueryPerformanceCounter(&t1);
		return (uint64_t)(((double)t1.QuadPart) / ((double)timerFreq.QuadPart) * 1000000.0);
#endif
	}

	// returns a high-precision UTC timestamp (NTP synced)
	inline static uint64_t getSystemTimeNanoSeconds()
	{
#ifndef _WIN32
        struct timespec time;
        clock_gettime(CLOCK_REALTIME, &time);
        return (uint64_t) time.tv_sec * (uint64_t) 1e9 + (uint64_t) time.tv_nsec;
#else
		FILETIME tm;
		(*myGetSystemTime)(&tm);
		// get as 100-ns
		ULONGLONG t = ((ULONGLONG)tm.dwHighDateTime << 32) | (ULONGLONG)tm.dwLowDateTime;
		return t * 100;
#endif
	}



	inline void logInfo(std::string msg) {
		std::cout << msg << std::endl;
	}

	inline void logError(std::string msg) {
		std::cout << msg << std::endl;
	}





	// L must be odd
	template<typename T, unsigned int L>
	T median(const T *a)
	{
		static const int	Lh = (int)(L / 2) + 1;

		const T  *p;
		T left[Lh], right[Lh], median;
		unsigned char nLeft, nRight;

		// pick first value as median candidate
		p = a;
		median = *p++;
		nLeft = nRight = 1;

		for (;;)
		{
			// get next value
			T val = *p++;

			// if value is smaller than median, append to left heap
			if (val < median)
			{
				// move biggest value to the heap top
				unsigned char child = nLeft++, parent = (child - 1) / 2;
				while (parent && val > left[parent])
				{
					left[child] = left[parent];
					child = parent;
					parent = (parent - 1) / 2;
				}
				left[child] = val;

				// if left heap is full
				if (nLeft == Lh)
				{
					// for each remaining value
					for (unsigned char nVal = L - (p - a); nVal; --nVal)
					{
						// get next value
						val = *p++;

						// if value is to be inserted in the left heap
						if (val < median)
						{
							child = left[2] > left[1] ? 2 : 1;
							if (val >= left[child])
								median = val;
							else
							{
								median = left[child];
								parent = child;
								child = parent * 2 + 1;
								while (child < Lh)
								{
									if (child < (Lh - 1) && left[child + 1] > left[child])
										++child;
									if (val >= left[child])
										break;
									left[parent] = left[child];
									parent = child;
									child = parent * 2 + 1;
								}
								left[parent] = val;
							}
						}
					}
					return median;
				}
			}

			// else append to right heap
			else
			{
				// move smallest value to the heap top
				unsigned char child = nRight++, parent = (child - 1) / 2;
				while (parent && val < right[parent])
				{
					right[child] = right[parent];
					child = parent;
					parent = (parent - 1) / 2;
				}
				right[child] = val;

				// if right heap is full
				if (nRight == Lh)
				{
					// for each remaining value
					for (unsigned char nVal = L - (p - a); nVal; --nVal)
					{
						// get next value
						val = *p++;

						// if value is to be inserted in the right heap
						if (val > median)
						{
							child = right[2] < right[1] ? 2 : 1;
							if (val <= right[child])
								median = val;
							else
							{
								median = right[child];
								parent = child;
								child = parent * 2 + 1;
								while (child < Lh)
								{
									if (child < (Lh - 1) && right[child + 1] < right[child])
										++child;
									if (val <= right[child])
										break;
									right[parent] = right[child];
									parent = child;
									child = parent * 2 + 1;
								}
								right[parent] = val;
							}
						}
					}
					return median;
				}
			}
		}
	}


private:

#ifdef _WIN32
	static LARGE_INTEGER timerFreq;
	static VOID(WINAPI*myGetSystemTime)(LPFILETIME);
#endif
};

typedef UlltraProto UP;

#ifdef ANDROID
#include <rtt.h>
#endif

inline void spinWait(uint64_t usec)
{
	auto tu = UP::getMicroSeconds() + usec;
	while (tu > UP::getMicroSeconds()) { 
#ifndef ANDROID
		std::this_thread::yield();
#else
		nsleep(1);
#endif
	}
}





#ifdef MATLAB_REPORTS
inline std::ostream& operator << (std::ostream& os, const std::vector<std::string>& v)
{
	os << "";
	for (std::vector<std::string>::const_iterator ii = v.begin(); ii != v.end(); ++ii)
	{
		os << " '" << *ii<<"'";
		if(ii+1 != v.end()) os << ",";
	}
	os << "";
	return os;
}



template < typename T >
std::ostream& operator << (std::ostream& os, const std::vector<T>& v)
{
	int i = 0;
    os << "[";
    for (typename std::vector<T>::const_iterator ii = v.begin(); ii != v.end(); ++ii)
    {
        os << " " << *ii;
		i++;
		if (i%50 == 0)
			os << " ..." << std::endl;
    }
    os << "]'";
    return os;
}


template < typename T >
T sum(const std::vector<T>& v)
{
    T s = 0;
    for (typename std::vector<T>::const_iterator ii = v.begin(); ii != v.end(); ++ii)
    {
        s += *ii;
    }
    return s;
}

#endif

//inline std::ostream& operator << (std::ostream& os, const std::vector<float>& v);

//std::ostream& operator<<(std::ostream &strm, const std::vector<float> &fv);
//std::ostream& operator<<(std::ostream &strm, const std::vector<std::vector<float>> &fm);




// trim from start
static inline std::string &ltrim(std::string &s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
        return s;
}

// trim from end
static inline std::string &rtrim(std::string &s) {
        s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
        return s;
}

// trim from both ends
static inline std::string &trim(std::string &s) {
        return ltrim(rtrim(s));
}
