#include "CircularBuffer.h"



CircularBuffer::CircularBuffer(int len)
{
	buffer = new T[len];
	memset(buffer, 0, sizeof(T)*len);

	writeIndex = 0;
	readIndex = len-1;
}


CircularBuffer::~CircularBuffer()
{
	delete[] buffer;
}

size_t CircularBuffer::write(T *data, int len)
{
	size_t numFree = canWrite();
	if (numFree == 0)
		return 0;

	size_t n1, n2;
	size_t numWrite = len > numFree ? numFree : len;
	size_t cnt2 = writeIndex + numWrite;

	if (cnt2 > size)
	{
		n1 = size - writeIndex;
		n2 = cnt2 & sizeMask;
	}
	else
	{
		n1 = numWrite;
		n2 = 0;
	}

	memcpy(&(buffer[writeIndex]), data, n1);
	writeIndex = (writeIndex + n1) & sizeMask;

	if (n2)
	{
		memcpy(&(buffer[writeIndex]), data + n1, n2);
		writeIndex = (writeIndex + n2) & sizeMask;
	}

	return numWrite;
}

/*
uint8_t *CircularBuffer::readBegin(int len) // TODO! param len
{
	if (readIndex == writeIndex)
		return NULL;
	return blocks[readIndex];
}



void CircularBuffer::readEnd()
{
	readIndex = (readIndex + 1) % numBlocks;
}
*/

size_t CircularBuffer::canRead()
{
	if (writeIndex > readIndex)
		return writeIndex - readIndex;

	return (writeIndex - readIndex + size) & sizeMask;
}

size_t CircularBuffer::canWrite()
{
	size_t w, r;

	w = writeIndex;
	r = readIndex;

	if (w > r)
	{
		return ((r - w + size) & sizeMask) - 1;
	}
	else if (w < r)
	{
		return (r - w) - 1;
	}
	else
	{
		return size - 1;
	}
}