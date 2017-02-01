#pragma once

#include "networking.h"
#include "UlltraProto.h"
#include "Discovery.h"

#include <functional>

class LLLink;

typedef std::function<LLLink*()> LLLinkGenerator;


class LLLink
{
public:
	struct QosHandle {
		unsigned long handle;
		static QosHandle invalid;
		inline bool valid() { return handle != -1; }

		QosHandle(unsigned long h=-1) : handle(h) {}
	};

	inline LLLink(uint64_t defaultBlockingTimeout) : m_receiveBlockingTimeoutUs(defaultBlockingTimeout) {}
	virtual ~LLLink();

	/* common */
	virtual bool connect(const LinkEndpoint &addr, bool isMaster) = 0;
	virtual void toString(std::ostream& stream) const = 0;

	/* rx */
	bool setBlockingTimeout(uint64_t timeoutUs);
	virtual bool onBlockingTimeoutChanged(uint64_t timeoutUs) = 0;
	virtual bool flushReceiveBuffer();
	virtual const uint8_t *receive(int &receivedBytes) = 0;//receive using an interal buffer
	//virtual const uint8_t *receive(uint8_t *buffer, int &receivedBytes) = 0;

	/* tx */
	virtual bool send(const uint8_t *data, int dataLength) = 0;

	inline friend std::ostream& operator<< (std::ostream& stream, const LLLink& lll) {
		lll.toString(stream);
		return stream;
	}

    static int s_timeStampingEnabled;
	static QosHandle enableHighQoS(SOCKET soc, int bitsPerSeconds);
    bool enableTimeStamps(SOCKET soc, bool enable=true);
    int socketReceive(SOCKET soc, uint8_t *buffer, int bufferSize);


    inline std::vector<int> const& getStackDelayHistogramUs() {
        return m_stackDelayHist;
    }

 private:
    std::vector<int> m_stackDelayHist;


protected:
	uint64_t m_receiveBlockingTimeoutUs;

#ifdef _WIN32
	static HANDLE s_qosHandle;
#endif
};

