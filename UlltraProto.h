#pragma once

#include <stdint.h>

#ifdef _WIN32
#define _WINSOCKAPI_    // stops windows.h including winsock.h
#include<Windows.h>
#else
#include <sys/time.h>
#endif

#include <sstream>
#include <iostream>


#ifdef _DEBUG
#include<fstream>
#include<vector>
#endif

#include <thread>



class UlltraProto {
public:
	static const int DiscoveryPort = 26025;
	static const int BroadcastInterval = 2; //seconds
	static const int LinkEvalPort = 26100;
	static const int LinkEvalTimeoutMS = 200; // milliseconds

    static const int DiscoveryTcpQueue = 8;
    static const int DiscoveryTcpTimeout = 800; // milliseconds

	static const int HttpControlPort = 26080;

	static const int HttpConnectTimeout = 0;// milliseconds
	static const int HttpResponseTimeout = 800;// milliseconds


	static const int TcpConnectTimeout = 4000; // milliseconds


	static bool init();

	inline static uint64_t getMicroSeconds()
	{
#ifndef _WIN32
		uint64_t t;
		struct timeval tv;
		gettimeofday(&tv, 0);
		t = (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
		return t;
#else
		LARGE_INTEGER t1;
		QueryPerformanceCounter(&t1);
		return (uint64_t)(((double)t1.QuadPart) / ((double)timerFreq.QuadPart) * 1000000.0);
#endif
	}

	// this is faster
	inline static uint64_t getMicroSecondsCoarse()
	{
#ifndef _WIN32
		uint64_t t;
		struct timeval tv;
		gettimeofday(&tv, 0);
		t = (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
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
		uint64_t t;
		struct timeval tv;
		gettimeofday(&tv, 0);
		t = (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
		return t;
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




inline void spinWait(uint64_t usec)
{
	auto tu = UP::getMicroSeconds() + usec;
	while (tu > UP::getMicroSeconds()) { std::this_thread::yield(); }
}

// Log, version 0.1: a simple logging class
enum TLogLevel {
	logERROR, logWARNING, logINFO, logDEBUG, logDEBUG1,
	logDEBUG2, logDEBUG3, logDEBUG4, logDebugHttp
};


class Log
{
public:
	const TLogLevel lv;
#ifdef _WIN32
	CONSOLE_SCREEN_BUFFER_INFO scbi;
#endif

	inline Log(TLogLevel level = logINFO) : lv(level) {
	}	


	inline ~Log() {

#ifndef _WIN32
		if (lv == logERROR)
			std::cout << "\e[0m ";
#endif

		std::cout << std::endl << std::flush;
#ifdef _WIN32
		if (lv == logERROR)
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), scbi.wAttributes);
#endif
	}

	inline std::ostream& get() {
		std::string cl;

#ifdef _WIN32
		if (lv == logERROR) {
			GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &scbi);
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_INTENSITY);
		}
#else
		if (lv == logERROR)
			cl = "\e[91m";
#endif


		std::cout << (cl + std::string(lv > logDEBUG ? (lv - logDEBUG)*2 : 0, ' '));
		return std::cout;
	}

	inline static TLogLevel ReportingLevel() {
		return logDEBUG3; // logDebugHttp;
	}
};

#define LOG(level) if (level > Log::ReportingLevel()) ; else Log(level).get()


#ifdef _DEBUG

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
	os << "[";
	for (typename std::vector<T>::const_iterator ii = v.begin(); ii != v.end(); ++ii)
	{
		os << " " << *ii;
	}
	os << "]'";
	return os;
}

//inline std::ostream& operator << (std::ostream& os, const std::vector<float>& v);

//std::ostream& operator<<(std::ostream &strm, const std::vector<float> &fv);
//std::ostream& operator<<(std::ostream &strm, const std::vector<std::vector<float>> &fm);

#endif

#include <algorithm>
#include <functional>
#include <cctype>
#include <locale>

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
