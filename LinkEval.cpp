
#include "LinkEval.h"

#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <rtt.h>
#include "stress/stress.h"
#include <math.h>
#include<fstream>

#include "UlltraProto.h"
#include "SimpleUdpReceiver.h"

#include "LLUdpLink.h"


struct TestDataHeader {
	uint8_t moreData;
	int testRunKey;
	uint8_t blockSizeIndex;
	uint64_t tSent;
};


// from http://stackoverflow.com/questions/11376288/fast-computing-of-log2-for-64-bit-integers
static const int tab64[64] = {
	63,  0, 58,  1, 59, 47, 53,  2,
	60, 39, 48, 27, 54, 33, 42,  3,
	61, 51, 37, 40, 49, 18, 28, 20,
	55, 30, 34, 11, 43, 14, 22,  4,
	62, 57, 46, 52, 38, 26, 32, 41,
	50, 36, 17, 19, 29, 10, 13, 21,
	56, 45, 25, 31, 35, 16,  9, 12,
	44, 24, 15,  8, 23,  7,  6,  5 };



struct Histogram {
	std::vector<uint64_t> h;
    uint64_t maxVal, s, maxValLog;

	static const int P = 50, R = 1, B = 100;

	Histogram() {
		s = 1e4;
		maxVal = 0;
        maxValLog = 0;
		h.resize(s, 0);
	}


	static int log2_64(uint64_t value)
	{
		value |= value >> 1;
		value |= value >> 2;
		value |= value >> 4;
		value |= value >> 8;
		value |= value >> 16;
		value |= value >> 32;
		return tab64[((uint64_t)((value - (value >> 1)) * 0x07EDD5E59A4E28C2)) >> 58];
	}


	// returns >0 if reached a new max, this could caused a memory intensive resize
    uint64_t addLog(uint64_t v) {
		uint64_t d;

        auto logV = (int)(std::log10( (float)((v / (uint64_t)B)*(uint64_t)B +(uint64_t)R)  )*(float)P);// v/1000UL; // log2_64(v*1000UL);
        if(v > maxVal) {
            d = v - maxVal;
            maxVal = v;
            maxValLog = logV;
        } else {
            d = 0;
        }

        if (logV >= s) {
            h.resize(logV + logV / 6 + 10, 0);
            s = h.size();
        }

		h[logV]++;

		return d;
	}


	// returns >0 if reached a new max, this could caused a memory intensive resize
	uint64_t addLin(uint64_t v) {
		uint64_t d;
		if (v > maxVal) {
			d = v - maxVal;
			maxVal = v;
			if (v >= s) {
				h.resize(v + v / 6UL + 10UL, 0);
				s = h.size();
			}
		}
		else {
			d = 0;
		}
		h[v]++;
		return d;
	}

	uint64_t hits() {
		return sum(h);
	}

    int64_t maxLin() {
        if (maxVal == 0 && h[0] == 0)
            return -1;
        return maxVal;
    }

    int64_t maxLog() {
        if (maxVal == 0 && h[0] == 0)
            return 0;
        return maxValLog;
    }


	static void equalize(std::vector<Histogram> &hist, uint64_t maxLen=100000)
	{
		uint64_t m = 0;
		for (auto &h : hist) {
            if (h.maxLog() > m)
                m = h.maxLog();
		}

		if (m > maxLen)
			m = maxLen;

		for (auto &h : hist) {
			h.h.resize(m+2, 0);
		}
	}

	inline friend std::ostream& operator << (std::ostream& os, const std::vector<Histogram>& v)
	{
		os << "h=(";

		{
			int i = 0;
			os << "[";
			for (std::vector<Histogram>::const_iterator ii = v.begin(); ii != v.end(); ++ii)
			{
				os << " " << ii->h;
				i++;
				if (i % 50 == 0)
					os << " ..." << std::endl;
			}
			os << "]'";
		}
		
		os << ")';" << std::endl << std::endl;

        int len = v[0].h.size();
        std::vector<float> x(len,0.0f);
        for(int i = 0; i < len; i++) {
            x[i] = (std::pow(10.0f, ((float)i)/(float)P)-(float)R);
        }

		os << "x=(" << x << ");" << std::endl << std::endl;
		os << "o=ceil(" << std::log10(1.0+R) * (float)P << ")+1;" << std::endl;

        os << "figure; loglog(x(o:end,:), 1+h(o:end,:), 'LineWidth',2); grid on; " << std::endl;


		return os;
	}

};


typedef std::function<LLUdpLink *(LinkEndpoint end)> LinkGenerator;

void runTestAsMaster(const Discovery::NodeDevice &nd, LLLink *link, const std::string &testName);
void runTestAsSlave(const Discovery::NodeDevice &nd, LLLink *link);


LinkEval::LinkEval()
{
}

bool LinkEval::init()
{
	return true;
}

LinkEval::~LinkEval()
{
}

void LinkEval::update(time_t now)
{
}

void systemTimerAccuracy()
{
    const static int tDurationUs = 1e6;
	LOG(logINFO) << "evaluating timer for  " << (tDurationUs /1000) << " ms";

	int numHits = 0;
	int *pNumHits = &numHits;
	Histogram h;

	
	uint64_t lt = 0;

	{
		RttTimer t([&numHits, &h, &lt]() {
			uint64_t t = UP::getSystemTimeNanoSeconds();
			if (lt > 0)
			{
                h.addLog(t - lt);
				numHits++;
			}
			lt = t;
			
			return true;
        }, 1e6); // 1ms
		{LOG(logDEBUG) << "sleep..."; }
		usleep(tDurationUs);
		{LOG(logDEBUG) << "waiting for timer thread to shutdown"; }
	}


	std::vector<Histogram> hv(1,h);
	Histogram::equalize(hv);

	std::string fn("./out/timer_acc.m");
	std::ofstream fs(fn);
	if (fs.good()) {
        fs << hv << "title('timer period (ns)');";
		LOG(logINFO) << "timer histogram with " << numHits << " hits data written to " << fn;
	}
	LOG(logINFO) << "timer evaluation done!";
}


void LinkEval::systemLatencyEval()
{
	systemTimerAccuracy();

    static const uint64_t runTimeMs = 1e3*1; // 1e3 * 1;
	static const uint64_t nsStep = 1000;
	static const int stressStartDelayUs = 20 * 1000;

	std::vector<Histogram> hMat(6);
	//volatile bool err = false;

	
	std::function<void(Histogram*, int wait)> measure([](Histogram *hCur, int wait) {
		uint64_t tl = UP::getSystemTimeNanoSeconds(), t, dt;
		uint64_t te = tl + runTimeMs*(uint64_t)1e6;
		while (tl < te) {
			if (wait == 0)
				std::this_thread::yield();
			else if(wait > 0)
				nsleep(wait);
			t = UP::getSystemTimeNanoSeconds();
            dt = (t - tl); // dt in 100ns = (1/10)us

            if (dt > 1e10) {
				LOG(logERROR) << "system latency too high or timer broken. cancel test";
				//err = true;
				break;
			}

            tl = hCur->addLog(dt) ? UP::getSystemTimeNanoSeconds() : t;
		}

		stressStop();
		//{LOG(logINFO) << (tl - te); }
	});

	{
		stressStartSetFlag();
		int numHogs = RttThread::GetSystemNumCores()+1;

		LOG(logINFO) << "Stressing systems for " << (runTimeMs) << " ms with " << numHogs << " workers per task. Tasks are: cpu,vm";


		{
			// create measuremnet (non-rt & rt)
			//RttThread mt4([&measure, &hMat]() { measure(&hMat[4], 1); }, false); // sleep 1us
			//RttThread mt0([&measure, &hMat]() { measure(&hMat[0], 0); }, false, "mt0");  // yield			
			

			RttThread rtSleep([&measure, &hMat]() {	measure(&hMat[5], 1); }, true); // sleep 1us
			RttThread rtYield([&measure, &hMat]() {	measure(&hMat[1], 0); }, true, "mt1"); // yield

			//RttThread mt2([&measure, &hMat]() { measure(&hMat[2], -1); }, false);  // no yield/sleep
			RttThread mt3([&measure, &hMat]() {	measure(&hMat[3], -1); }, true);
			

			
			// give the measurement threads time to spin up
			usleep(stressStartDelayUs);

			
			std::vector<RttThread> stressCpu(numHogs, RttThreadPrototype([]() { usleep(stressStartDelayUs); hogcpu(); }, false, "cpu_hog"));
			std::vector<RttThread> stressVm(numHogs, RttThreadPrototype([numHogs]() { usleep(stressStartDelayUs); hogvm(256 / numHogs * 1024 * 1024, 4069, -1, 0); }, false, "vm_hog"));

#if !defined(_WIN32) && 0
			LOG(logWARNING) << "WARNING: Running hoghdd, this might wear your SSD!";
			std::vector<RttThread> stressIo(numHogs, RttThreadPrototype([]() {usleep(stressStartDelayUs);  hogio(); }, false, "io_hog"));
			std::vector<RttThread> stressHdd(2, RttThreadPrototype([]() {
				usleep(stressStartDelayUs);
				hoghdd(1024 * 1024 * 2);
				LOG(logINFO) << "hdd worker finished!";
			}, false, "hdd_hog"));
#endif 

			/*rtSleep.Join();
			rtYield.Join();
			mt3.Join();
			stressStop(); */
		}

		stressStop();
	}

	LOG(logDEBUG) << "Test done.";

	//if (err)
//		LOG(logWARNING) << "... with errors!";

    Histogram::equalize(hMat);

	std::vector<std::string> legend({
		"yield   ", "yield-rt",
		"busy    ", "busy-rt ",
		"nsleep1 ", "nslp1-rt"
	});


	// generate legend and compute best
	uint32_t best = -1, len = 0, vi = 0;
	for (auto &v : hMat) {
		uint64_t hits = v.hits();
        std::string leg = "max: " + std::to_string(v.maxLin() / 1000) + " us";
		legend[vi++] += leg;
		LOG(logDEBUG) << std::setw(30) << legend[vi-1] << "   (hits " << std::setw(8) << (hits/1000) << "k)";

        if (v.maxLin() > 1 && hits > 1000 && v.maxLin() < best) {
            best = v.maxLin();
		}
	}


	std::string fn("./out/sys_wakeup" + std::to_string(2) + ".m");
	std::ofstream fs(fn);
	if (fs.good()) {
		fs << hMat << "title('thread wakeup (" << nsStep << "ns)'); legend(" << legend << ");"; // xlim([-10 "<<(1e6/ nsStep) << "]); ";
		LOG(logINFO) << "wkaeup data written to " << fn;
	}
	else {
		LOG(logERROR) << "failed to write to " << fn;
	}


	if (best > 900*1000) { // us
		LOG(logERROR) << " !!! System has poor real-time performance !!! ";
	}
}

void runTimedAsSlave(const Discovery::NodeDevice &nd, LLLink *link, const std::string &testName);
void runTimedMaster(const Discovery::NodeDevice &nd, LLLink *link, const std::string &testName);

void LinkEval::chooseLinkCandidate(LLLinkGeneratorSet const& candidates, const Discovery::NodeDevice &nd, bool master)
{
	int i = 0;
	for (auto &c : candidates) {
		LOG(logINFO) << "### setup evaluation of " << c.first << "...";
		LLLink *ll = c.second();
		auto &testName = c.first;

		usleep(1000 * 200);

		do {
			if (!ll) {
				LOG(logERROR) << "link generation for " << testName << " failed!";
				break;
			}

			if (!ll->connect(nd.getAddr(UP::LinkEvalPort+i), master)) {
				LOG(logERROR) << "Connection failed! (port " << (UP::LinkEvalPort+i) << ")";
				break;
			}

			if (!ll->setBlockingTimeout(master ? 2000 : (2000*20))) {
				LOG(logERROR) << "Failed to set blocking mode!";
				break;
			}

			LOG(logINFO) << "evaluating [" << (master ? "MASTER" : "SLAVE") << "] me  <-- " << testName << ":" << *ll << " -->  " << nd.getName() << " [" << (!master ? "MASTER" : "SLAVE") << "]";

			if (master) {
				// discard any queued packets
				ll->flushReceiveBuffer();
			}

			std::function<void(void)> f([this, &nd, master, ll, testName]() {
				//if (master)	runTestAsMaster(nd, ll, testName);
				//else runTestAsSlave(nd, ll);

				if (master)
					runTimedMaster(nd, ll, testName);
				else
					runTimedAsSlave(nd, ll, testName);

                outputExtendedLinkStats(ll, testName);
			});

			{RttThread t(f, true); }

		} while (0);
		
		LOG(logINFO) << "evaluation of " << c.first << " ended ###";

		delete ll;
		i++;
	}
}


void runTimedMaster(const Discovery::NodeDevice &nd, LLLink *link, const std::string &testName)
{
	static const int testDurationMs = 120000;
	static const int blockSizes[] = {
		64,128,512
	};
	std::vector<std::string> legend({ "64", "128", "512" });
	
	static const int nBlockSizes = sizeof(blockSizes) / sizeof(*blockSizes);

	std::vector<Histogram> timerHist(nBlockSizes);

	bool cancel = false;

	int nSent = 0;
	
	uint8_t dataBlock[1024 * 2];
	uint8_t *pDataBlock = dataBlock;
	int testKey = rand();
	for (int i = 0; i < sizeof(dataBlock); i++) {
		dataBlock[i] = 1 + (rand() % 254);
	}


	for (int ii = 0; ii < 10; ii++) { // interleave!

		for (int bsi = 0; bsi < nBlockSizes; bsi++) {
			uint64_t tLast = 0;

			RttTimer t([testKey, pDataBlock, bsi, &nSent, link, &timerHist, &tLast]() {
				int blockSize = blockSizes[bsi];
				auto now = UlltraProto::getSystemTimeNanoSeconds();

				TestDataHeader tdh;
				tdh.moreData = 1;
				tdh.blockSizeIndex = bsi;
				tdh.testRunKey = testKey;
				tdh.tSent = now;
				*reinterpret_cast<TestDataHeader*>(pDataBlock) = tdh;

				if (!link->send(pDataBlock, blockSize)) {
					LOG(logERROR) << "Failed sending data block during latency test, cancelling!";
					return false;
				}

				if (tLast > 0) {
					timerHist[bsi].addLog((now - tLast));
				}

				tLast = now;

				nSent++;
				return true;
			}, 1e6);

			usleep(testDurationMs * 1000 / nBlockSizes / 10);
		}
	}

	// let slave know that its over
	TestDataHeader tdh;
	memset(&tdh, 0, sizeof(tdh));
	tdh.testRunKey = testKey;
	link->send((const uint8_t*)&tdh, sizeof(tdh));

	LOG(logDEBUG) << "Packets send: " << nSent;


	auto now = UlltraProto::getMicroSeconds();
	char hostname[32];
	gethostname(hostname, 31);

	


	{
		std::string fn("./out/timed_master_hist_" + testName + "_" + nd.getName() + "_" + std::to_string(now) + ".m");
		std::ofstream f(fn);
		if (f.good()) {
			f << timerHist << "legend(" << legend << "); title('latency (100us) " << hostname << " <-[" << *link << "]-> " << nd << "'); ";
			LOG(logDEBUG) << "data written to " << fn;
		}
	}
}


const uint8_t *slaveReceive(LLLink *link, int &testRunKey, uint64_t &tLastReceive, int dataLen)
{
	const int MaxTimeouts = 50;
	const uint8_t *data;

	if(tLastReceive == 0)
		tLastReceive = UP::getMicroSecondsCoarse();;

	for (int i = 0; i <= MaxTimeouts; i++) {
		data = link->receive(dataLen);

		if (dataLen == -1) {
			LOG(logINFO) << "Link broke, cancelling!";
			return 0;
		}

		if (data) {

			if (dataLen < sizeof(TestDataHeader)) {
				LOG(logINFO) << "received small package, ignoring.";
				data = 0;
				continue;
			}

			if (dataLen > 1024) {
				LOG(logINFO) << "package is big!";
			}

			auto tdh = reinterpret_cast<const TestDataHeader*>(data);

			if (testRunKey == 0) {
				testRunKey = tdh->testRunKey;
				LOG(logDEBUG1) << "Test session key:" << testRunKey;
			}
			else if (testRunKey != tdh->testRunKey) {
				LOG(logINFO) << "Received with different session key, ignoring. (key:" << tdh->testRunKey << ", len: " << dataLen <<" sent " << ((UlltraProto::getSystemTimeNanoSeconds() - tdh->tSent) / 1000) << " us ago)";
				continue;
			}

			if (!tdh->moreData) {
				std::cout << "Latency test ended (end flag)" << std::endl;
				return 0;
			}

			break;
		}

		auto t = UP::getMicroSecondsCoarse();
		auto dt((t - tLastReceive));
		if (dt > 1000 * 1000 * 6) {
			LOG(logDEBUG) << "Not receiving anything for 6 s, cancel!";
			dataLen = -1;
			return 0;
		}
		else {
			LOG(logDEBUG) << "Timeout during latency test, retrying. " << dt;
		}
	}

	if (dataLen == -1)
		return 0;

	if (!data) {
		LOG(logERROR) << "Reached max number of " << MaxTimeouts << " timeouts/errors after receiving, canceling.";
		return 0;
	}

	tLastReceive = UP::getMicroSecondsCoarse();

	return data;
}


void runTimedAsSlave(const Discovery::NodeDevice &nd, LLLink *link, const std::string &testName)
{
	std::vector<std::string> legend({ "64", "128", "512" });


	uint32_t nPackets = 0;
	int key = 0, len;
	const uint8_t *data;
	uint64_t tLastReceiveMs = 0, tNs;

	int lastBsi = -1;

	std::vector<Histogram> hist(3);
	std::vector<uint64_t> tLast(3, 0);
	std::vector<uint64_t> tMaxTWithoutRecv(3, 0);

	while (true) {
		len = 0;
		data = slaveReceive(link, key, tLastReceiveMs, len);

		if (!data)
			break;

		TestDataHeader *tdh = (TestDataHeader *)data;

		if (tdh->blockSizeIndex < 0 || tdh->blockSizeIndex >= legend.size()) {
			break;
		}

		tNs = UP::getSystemTimeNanoSeconds();		

		// on bsi change, skip
		if (tLast[tdh->blockSizeIndex] > 0 && lastBsi == tdh->blockSizeIndex) {
            hist[tdh->blockSizeIndex].addLog(tNs - tLast[tdh->blockSizeIndex]);			
		}
		tLast[tdh->blockSizeIndex] = tNs;
		lastBsi = tdh->blockSizeIndex;

		nPackets++;
	}

	LOG(logDEBUG) << "Packets received: " << nPackets;

	int i = 0;
	for (auto &h : hist) {
		LOG(logINFO) << " BS=" << legend[i++] << "\tmax:" << h.maxLin()/1e6 << " ms";
	}


	auto now = UlltraProto::getMicroSeconds();
	char hostname[32];
	gethostname(hostname, 31);


	Histogram::equalize(hist);

	{
		std::string fn("./out/timed_slave_hist_" + testName + "_" + nd.getName() + "_" + std::to_string(now) + ".m");
		std::ofstream f(fn);
		if (f.good()) {
			f << hist << "legend(" << legend << "); title('receiver jitter (ns) " << hostname << " <-[" << *link << "]-> " << nd << "'); ";
			LOG(logDEBUG) << "data written to " << fn;
		}
	}
}



void runTestAsMaster(const Discovery::NodeDevice &nd, LLLink *link, const std::string &testName)
{
    static const int nPasses = 10000;
	//static const int nPasses = 200;
	static const int blockSizes[] = {
		//32, 64, 128, 256, 512, 1024, //, 2048
	//	32, 128, 512
		64,128,512
	};

	
	static const int nBlockSizes = sizeof(blockSizes) / sizeof(*blockSizes);

	// state
	bool cancel = false;
	int timeouts = 0;
	int packetsSend = 0, packetsReceived = 0;
	uint64_t bytesSentTotal = 0;
	int bytesInFlight = 0, packetsInFlight = 0;
	uint64_t tLastReceive = UlltraProto::getSystemTimeNanoSeconds();

	std::vector<Histogram> hists(nBlockSizes);

#ifdef MATLAB_REPORTS
	std::vector<std::vector<float>> statMat(nBlockSizes, std::vector<float>(nPasses + 1, 0.0f));
	std::vector<int> statMatIdx(nPasses, 0);
	std::vector<std::string> legend(nBlockSizes, "");
#endif

    const static int nRandBlocks = 16;
	// generate random data blocks
	int key = 0;
	while (key == 0) { key = rand(); }
    auto dataBlocks = new uint8_t[1024 * nRandBlocks];
    for (int i = 0; i < 1024 * nRandBlocks; i++) {
		dataBlocks[i] = 1 + (rand() % 254); // dont send zeros (0 means end of test)
	}

	for (int pi = 0; pi < nPasses; pi++) {
	for (int bsi = 0; bsi < sizeof(blockSizes) / sizeof(*blockSizes); bsi++) {
		
			int blockSize = blockSizes[bsi];
			bool finalPass = (pi == nPasses - 1);
			uint64_t now = UlltraProto::getSystemTimeNanoSeconds();

			bool skipSend = false;
			if (bytesInFlight > 1024) {
				skipSend = true;
			}

			

			if (!finalPass && !skipSend) {
				TestDataHeader tdh;
				tdh.moreData = 1;
				tdh.blockSizeIndex = bsi;
				tdh.testRunKey = key;
				tdh.tSent = now;
                *reinterpret_cast<TestDataHeader*>(&dataBlocks[blockSize * (pi % nRandBlocks)]) = tdh;

                if (!link->send(&dataBlocks[blockSize * (pi % nRandBlocks)], blockSize)) {
					LOG(logERROR) << "Failed sending data block during latency test, cancelling!";
					cancel = true;
					break;
				}

				bytesSentTotal += (uint64_t)blockSize;
				bytesInFlight += (int)blockSize;
				packetsInFlight++;
				packetsSend++;
			}

			const uint8_t *receivedData;
			int recvLen = 0;
			while (packetsInFlight > 0 && (receivedData = link->receive(recvLen)))
			{
				if (recvLen < sizeof(TestDataHeader)) {
					LOG(logINFO) << "Received packet with invalid size " << recvLen << ", ignoring.";
					continue;
				}

				auto tdh = reinterpret_cast<const TestDataHeader*>(receivedData);

				// dont expect packages to arrive in-order, just check when it was sent!
				if (tdh->testRunKey != key) {
					LOG(logINFO) << "Received with different session key, ignoring. (sent " << ((UlltraProto::getSystemTimeNanoSeconds() - tdh->tSent)/1000) << " us ago)";
					continue;
				}

				if (tdh->blockSizeIndex < 0 || tdh->blockSizeIndex >= nBlockSizes) {
					LOG(logINFO) << "Received packet of size " << recvLen << " with header byte = 0, cancelling!";
					cancel = true;
					break;
				}

				packetsReceived++;
				packetsInFlight--;
				bytesInFlight -= (int)recvLen;
				tLastReceive = now;

				if (tdh->tSent > 0) {
					//uint64_t rtt = UP::getMicroSeconds() - tdh->tSent;
					uint64_t rtt = UP::getSystemTimeNanoSeconds() - tdh->tSent;
                    hists[tdh->blockSizeIndex].addLog(rtt);
#ifdef MATLAB_REPORTS
					if (rtt > 0 && statMatIdx[tdh->blockSizeIndex] < nPasses)
						statMat[tdh->blockSizeIndex][statMatIdx[tdh->blockSizeIndex]++] = rtt;
#endif
				}
			}

			if (recvLen == -1) {
				LOG(logINFO) << "Link broke, cancelling!";
				cancel = true;
				break;
			}


			// 1 second timeout
			if ((now - tLastReceive) > 6e9) {
				LOG(logINFO) << "Not receiving anything for 6 s, cancelling!";
				cancel = true;
				break;
			}

			if (!packetsInFlight)
				usleep(1000);

			if (cancel)
				break;
		}
		if (cancel)
			break;
	}

	delete dataBlocks;

	// let slave know that its ove
	TestDataHeader tdh;
	memset(&tdh, 0, sizeof(tdh));
	tdh.testRunKey = key;
	link->send((const uint8_t*)&tdh, sizeof(tdh));

	LOG(logDEBUG) << "Packets send: " << packetsSend << " ( " << ((float)bytesSentTotal / 1000.0f / 1000.0f) << " MB), recv: " << packetsReceived;



	float packetLoss = 1.0f - (float)(packetsReceived) / (float)packetsSend;

	if (packetLoss > 0.4f) {
		LOG(logERROR) << "Packet loss " << packetLoss << " too high!";
		return;
	}
	
	auto now = UlltraProto::getMicroSeconds();
	char hostname[32];
	gethostname(hostname, 31);



	Histogram::equalize(hists, 50000);

#ifdef MATLAB_REPORTS
	LOG(logINFO) << std::endl << "# Results for " << testName << ":";
	for (int bsi = 0; bsi < sizeof(blockSizes) / sizeof(*blockSizes); bsi++) {
		int blockSize = blockSizes[bsi];

		std::sort(statMat[bsi].begin(), statMat[bsi].end());
		std::reverse(statMat[bsi].begin(), statMat[bsi].end());

		int rttmin = 0;
		auto median = statMat[bsi][statMat[bsi].size() / 2];
		auto max = statMat[bsi][0];
		auto max1 = statMat[bsi][statMat[bsi].size() / (100 / 1)];
		if (max) { int i = 0; while (!(rttmin = (int)statMat[bsi][statMat[bsi].size() - 1 - i])) { i++; }; }
		LOG(logINFO) << "\tBS " << std::setw(8) << blockSize << "  RTT "
			<< "     min: " << std::setw(5) << (int)(rttmin / 1000.0f) << " us"
			<< "     med: " << std::setw(5) << (int)(median / 1000.0f) << " us"
			<< "     max: " << std::setw(5) << (int)(max / 1000.0f) << " us"
			<< "     max(1%): " << std::setw(5) << (int)(max1 / 1000.0f) << " us"
			<< "     max(1): " << std::setw(5) << (int)(statMat[bsi][1] / 1000.0f) << " us";

		legend[bsi] = "bs " + std::to_string(blockSize) + " med=" + std::to_string(median);
	}
	LOG(logINFO) << std::endl;



   

    std::string fn("./out/"+ testName+"_" + nd.getName() + "_" + std::to_string(now) + ".m");
	std::ofstream f(fn);
	if (f.good()) {
		f << "figure; l=(" << statMat << "');" << std::endl;
		f << "  plot(medfilt1(l, 25), '-*', 'LineWidth', 4); hold on;  grid on; ylim([1000, 10000]*1000); xlim(size(l,1)*[-0.005 0.05]);" << std::endl;
		f << " ax = gca; ax.ColorOrderIndex = 1; plot(l, 'LineWidth', 1);  " << std::endl;
		f << "legend(" << legend << "); title('latency " << hostname << " <-[" << *link << "]-> " << nd << "'); ";
		LOG(logDEBUG) << "data written to " << fn;
	}

	{
		std::string fn("./out/hist_" + testName + "_" + nd.getName() + "_" + std::to_string(now) + ".m");
		std::ofstream f(fn);
		if (f.good()) {
			f << hists << "legend(" << legend << "); title('latency (ns) " << hostname << " <-[" << *link << "]-> " << nd << "'); ";
			LOG(logDEBUG) << "data written to " << fn;
		}
	}
#endif	

	if (cancel) {
		LOG(logDEBUG) << "Test cancelled due to errors!";
		return;
	}
}


void runTestAsSlave(const Discovery::NodeDevice &nd, LLLink *link)
{
	const int MaxTimeouts = 50;

	uint32_t nPackets = 0;
	int key = 0;

	const uint8_t *data;
	int len = 0;

	uint64_t tLastReceive = UP::getMicroSecondsCoarse();

	// just bounce packages until end flag received (or timeouts)
	while (true)
	{
		len = 0;
		data = slaveReceive(link, key, tLastReceive, len);
		if (!data)
			break;

		nPackets++;

		if (!link->send(data, len)) {
			LOG(logERROR) << "send failed, cancelling after " << nPackets << " packets " << *link;
			break;
		}
	}
}



void LinkEval::outputExtendedLinkStats(LLLink *link, std::string const& testName)
{
    auto &hist = link->getStackDelayHistogramUs();
    if(hist.size() <= 2) {
        LOG(logINFO) << "NO stack delay data available!";
        return;
    }

    auto hits = sum(hist);
    if(hits < 100) {
        LOG(logINFO) << "Stack delay hist consists of " << hits << " hits, need 100 at least for output!";
        return;
    }

	int maxNon0;
	for (maxNon0 = hist.size() - 1; maxNon0 > 0; maxNon0--) {
		if (hist[maxNon0] > 0) break;
	}

	LOG(logINFO) << "Max stack delay: " << maxNon0 << " us";
	
    std::string fn("./out/stack_delay_"+ testName+"_" + std::to_string(UP::getSystemTimeNanoSeconds()/1000/1000) + ".m");
    std::ofstream f(fn);
    if (f.good()) {
        f << "stack_delay_hist_usec=(" << hist<< ");";
        f << "figure; plot(stack_delay_hist_usec(1:max(find(stack_delay_hist_usec)))); title('stack delay (us) histogram " << *link << "'); grid on;";
        LOG(logINFO) << "stack delay hist written to " << fn << " (" << hits << " hits)";
    } else {
        LOG(logERROR) << "failed to write to " <<fn;
    }
}

#if 0

// alternate master code

void LinkEval::latencyTestMaster(const Discovery::NodeDevice &nd)
{
	LOG(logDEBUG) << "Starting link evaluation (MASTER) with node " << nd;

	std::function<void(void)> f([this, &nd]() {
		LLSender lls(nd.addr, UlltraProto::LinkEvalPort);
		LLReceiver &llr(*m_receiver);
		llr.setBlocking(LLReceiver::BlockingMode::KernelBlock);

		sockaddr_in recvAddr;
		int recvLen;

		// clear receive buffer
		LOG(logDEBUG1) << "Flushing buffers... ";
		llr.clearBuffer();
		LOG(logDEBUG2) << "done!";

		static const int blockSizes[] = {
			512, 16, 32, 64, 128, 1024, 256, //, 2048
		};

		static const int nBlockSizes = sizeof(blockSizes) / sizeof(*blockSizes);
		static const int nPasses = 1000;
		static const int nPassesWarmUp = 4;

		bool err = false;

#ifdef _DEBUG
		std::vector<std::vector<float>> statMat(nBlockSizes, std::vector<float>(nPasses, 0));
		std::vector<std::string> legend(nBlockSizes, "");
#endif

		// generate random data blocks
		auto dataBlocks = new uint8_t[blockSizes[nBlockSizes - 1] * nPasses];
		for (int i = 0; i < blockSizes[nBlockSizes - 1] * nPasses; i++) {
			dataBlocks[i] = 1 + (rand() % 254); // dont send zeros (0 means end of test)
		}

		LatencyStats<2> stats[nBlockSizes];

		int timeouts = 0;

		for (int pi = 0; pi < nPasses; pi++) {
			for (int bsi = 0; bsi < sizeof(blockSizes) / sizeof(*blockSizes); bsi++) {
				int blockSize = blockSizes[bsi];

				if (pi >= nPassesWarmUp)
					stats[bsi].begin(0);

				if (lls.send(&dataBlocks[blockSize * pi], blockSize) != blockSize) {
					LOG(logERROR) << "Failed sending data block during latency test, cancelling!";
					err = true;
					break;
				}

				if (pi >= nPassesWarmUp)
					stats[bsi].begin(1);


				auto receivedData = llr.receive(recvLen, recvAddr);

				if (!receivedData) {
					LOG(logERROR) << "Timeout during latency test (pass " << pi << ", bs = " << blockSize << "), retrying";

					stats[bsi].end();
					bsi--;
					timeouts++;

					if (timeouts >= 10) {
						LOG(logERROR) << "Reached max number of timeouts (" << timeouts << "), cancelling!";
						err = true;
						break;
					}
					continue;
				}

				//LOG(logDEBUG1) << "received " << recvLen;

				if (recvLen != blockSize) {
					LOG(logERROR) << "Bounce length not matching (" << blockSize << " vs. " << recvLen << "), cancelling!";
					stats[bsi].end();
					continue;

					//err = true;
					//break;
				}

				if (recvAddr.sin_addr.s_addr != nd.addr.s_addr) {
					LOG(logINFO) << "Received packet from invalid host during latency test, ignoring.";
					continue;
				}

				if (pi >= nPassesWarmUp) {
					auto dt = stats[bsi].end();
					// limit bandwidth to 0.05 MB/s = 0.1B/µs
					double wait = (double)(blockSize) / 0.05 - (double)dt;
					if (wait < 100.0) wait = 100.0; // ...  and max 2 packets/ms
					usleep((uint32_t)wait);
					//spinWait((uint32_t)wait);
				}
				else {
					usleep(5000);
				}
			}
			if (err)
				break;
		}

		// let slave know that its over
		uint8_t z = 0;
		lls.send(&z, 1);

		delete dataBlocks;

		if (err)
			return;


		for (int bsi = 0; bsi < sizeof(blockSizes) / sizeof(*blockSizes); bsi++) {
			int blockSize = blockSizes[bsi];
			uint64_t min, max, med, maxs, maxr;

			LOG(logDEBUG1) << "BS=" << blockSize << ":";

			stats[bsi].getStats(min, maxs, med, 0);
			stats[bsi].getStats(min, maxr, med, 1);
			LOG(logDEBUG2) << "send max: " << std::setw(8) << maxs << ", recv max: " << std::setw(8) << maxr;
			stats[bsi].getStats(min, max, med);
			LOG(logDEBUG2) << "acc : min: " << std::setw(8) << min << ", max: " << std::setw(8) << max << ", median: " << std::setw(8) << med;

#ifdef _DEBUG
			statMat[bsi] = stats[bsi].getAccTS<float>();
			legend[bsi] = "bs " + std::to_string(blockSize);
#endif
		}

#ifdef _DEBUG
		std::string fn("./latencies_" + nd.name + "_" + std::to_string(UlltraProto::getMicroSeconds()) + ".m");
		std::ofstream f(fn);
		f << "figure; plot(" << statMat << "','LineWidth',2);" << std::endl << "legend(" << legend << "); title('" << nd << "');";
		LOG(logDEBUG) << "data written to " << fn;
#endif		
	});

	{RttThread t(f, false); }
	LOG(logINFO) << "latency test thread ended";
}

#endif
