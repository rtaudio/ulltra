
#include "LinkEval.h"

#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <rtt.h>
#include "stress/stress.h"

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


void LinkEval::systemLatencyEval()
{
    static const uint64_t runTimeMs = 1e3*1; // 1e3 * 1;
	static const uint64_t nsStep = 1000;
	static const int stressStartDelayUs = 20 * 1000;

	std::vector<std::vector<uint64_t>> hMat(6, std::vector<uint64_t>(1e4, 0));
	//volatile bool err = false;

	
	std::function<void(std::vector<uint64_t>*, int wait)> measure([](std::vector<uint64_t> *hCur, int wait) {
		uint64_t tl = UP::getSystemTimeNanoSeconds(), t, dt;
		uint64_t te = tl + runTimeMs*(uint64_t)1e6;
		while (tl < te) {
			if (wait == 0)
				std::this_thread::yield();
			else if(wait > 0)
				nsleep(wait);
			t = UP::getSystemTimeNanoSeconds();
			dt = (t - tl) / nsStep; // dt in 100ns = (1/10)us

			if (dt > 1e10 / nsStep) {
				LOG(logERROR) << "system latency too high or timer broken. cancel test";
				//err = true;
				break;
			}

			if (dt >= hCur->size()) {
				hCur->resize(10 + dt + dt / 6, 0);
				(*hCur)[dt]++;
				tl = UP::getSystemTimeNanoSeconds();
				continue;
			}

			tl = t;
			(*hCur)[dt]++;
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
	//	return;

	auto hits1 = sum(hMat[0]);
	auto hits2 = sum(hMat[1]);

	std::vector<std::string> legend({
		"yield   ", "yield-rt",
		"busy    ", "busy-rt ",
		"nsleep1 ", "nslp1-rt"
	});

	uint32_t best = -1;

	// make equal length
	int len = 0, vi = 0;
	for (auto &v : hMat) {
		int maxNon0;
		for (maxNon0 = v.size()-1; maxNon0 > 0; maxNon0--) {
			if (v[maxNon0] > 0) break;
		}

		uint64_t hits = sum(v);

		if (maxNon0 == 0 && hits == 0)
			maxNon0 = -1;

		std::string leg = "max: " + std::to_string(maxNon0*nsStep / 1000) + " us";
		legend[vi++] += leg;
		LOG(logDEBUG) << std::setw(30) << legend[vi-1] << "   (hits " << std::setw(8) << (hits/1000) << "k)";

		if (maxNon0 > 1 && hits > 1000 && maxNon0 < best) {
			best = maxNon0;
		}

		if (maxNon0 > len)
			len = maxNon0;
	}

	// avoid big files
	if (len > 30000)
		len = 30000;

	for (auto &v : hMat) v.resize(len,0);

	std::string fn("./out/sys_wakeup" + std::to_string(2) + ".m");
	std::ofstream fs(fn);
	if (fs.good()) {
		fs << "h=(" << hMat << ")';";
		fs << "figure; semilogy(1+h); title('thread wakeup (" << nsStep << "ns)'); grid on; legend(" << legend << "); xlim([-10 "<<(1e6/ nsStep) << "]);";
		LOG(logINFO) << "wkaeup data written to " << fn;
	}
	else {
		LOG(logERROR) << "failed to write to " << fn;
	}


	if (best > 900) { // us
		LOG(logERROR) << " !!! System has poor real-time performance !!! ";
	}
}

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
				if (master)
					runTestAsMaster(nd, ll, testName);
				else
					runTestAsSlave(nd, ll);

                outputExtendedLinkStats(ll, testName);
			});

			{RttThread t(f, true); }

		} while (0);
		
		LOG(logINFO) << "evaluation of " << c.first << " ended ###";

		delete ll;
		i++;
	}
}


void runTestAsMaster(const Discovery::NodeDevice &nd, LLLink *link, const std::string &testName)
{
    static const int nPasses = 4000;
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

#ifdef MATLAB_REPORTS
	std::vector<std::vector<float>> statMat(nBlockSizes, std::vector<float>(nPasses + 1, 0.0f));
	std::vector<int> statMatIdx(nPasses, 0);
	std::vector<std::string> legend(nBlockSizes, "");
#endif

    const static int nRandBlocks = 16;
	// generate random data blocks
	int key = rand();
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

	char hostname[32];
	gethostname(hostname, 31);

    auto now = UlltraProto::getMicroSeconds();

    std::string fn("./out/"+ testName+"_" + nd.getName() + "_" + std::to_string(now) + ".m");
	std::ofstream f(fn);
	if (f.good()) {
		f << "figure; l=(" << statMat << "');" << std::endl;
		f << "  plot(medfilt1(l, 25), '-*', 'LineWidth', 4); hold on;  grid on; ylim([1000, 10000]*1000); xlim(size(l,1)*[-0.005 0.05]);" << std::endl;
		f << " ax = gca; ax.ColorOrderIndex = 1; plot(l, 'LineWidth', 1);  " << std::endl;
		f << "legend(" << legend << "); title('latency " << hostname << " <-[" << *link << "]-> " << nd << "'); ";
		LOG(logDEBUG) << "data written to " << fn;
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
	int key;

	const uint8_t *data;
	int len = 0;

	uint64_t tLastReceive = UP::getMicroSecondsCoarse();

	// just bounce packages until end flag received (or timeouts)
	while (true) {
		len = 0;
		for (int i = 0; i <= MaxTimeouts; i++) {
			data = link->receive(len);

			if (len == -1) {
				LOG(logINFO) << "Link broke, cancelling!";
				break;
			}

			if (data) break;
			
			auto dt((UP::getMicroSecondsCoarse() - tLastReceive));
			if (dt > 1000 * 1000 * 6) {
				LOG(logDEBUG) << "Not receiving anything for 6 s, cancel!";
				len = -1;
				break;
			}
			else {
				LOG(logDEBUG) << "Timeout during latency test, retrying. " << nPackets << "|" << dt;
			}
		}

		if (len == -1)
			break;

		if (!data) {
			LOG(logERROR) << "Reached max number of " << MaxTimeouts << " timeouts after receiving " << nPackets << " packets, canceling.";
			break;
		}

		if (len < sizeof(TestDataHeader)) {
			LOG(logINFO) << "received small package, ignoring.";
			continue;
		}

		auto tdh = reinterpret_cast<const TestDataHeader*>(data);

		if (nPackets == 0) {
			key = tdh->testRunKey;
		}
		else if (key != tdh->testRunKey) {
			LOG(logINFO) << "Received with different session key, ignoring. (sent " << ((UlltraProto::getSystemTimeNanoSeconds() - tdh->tSent) / 1000) << " us ago)";
			continue;
		}		

		if (!tdh->moreData) {
			std::cout << "Latency test ended (end flag)" << std::endl;
			break;
		}

		nPackets++;
		tLastReceive = UP::getMicroSecondsCoarse();

		if (!link->send(data, len)) { // just send it back
			LOG(logERROR) << "send failed, cancelling after " << nPackets << " packets " << *link;
			break;
		}
		//LOG(logDEBUG1) << "bounced " << len;
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
