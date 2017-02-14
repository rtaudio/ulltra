#pragma once
#include <stdint.h>
#include <vector>



class CircularBuffer
{
	struct StereoSample { float left, right; };
	typedef StereoSample T;

public:
	CircularBuffer(int len);
	~CircularBuffer();

	size_t write(T *data, int len);//non-blocking copying write

	T *readBegin(int len);
	void readEnd();

	size_t canRead();
	size_t canWrite();

private:
	T *buffer;
	size_t	size;
	size_t	sizeMask;
	volatile size_t writeIndex;
	volatile size_t readIndex;
};

