#include "UlltraProto.h"

#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <math.h>
#include <fstream>
#include "rtt/rtt.h"
#include "rtt/stress.h"

#include "LinkEval.h"


#include "SimpleUdpReceiver.h"
#include "LLUdpLink.h"
#include "histogram.h"

struct TestDataHeader {
    uint8_t moreData;
    int testRunKey;
    uint8_t blockSizeIndex;
    uint64_t tSent;
};


typedef std::function<LLUdpLink *(LinkEndpoint end)> LinkGenerator;

void runTestAsMaster(const Discovery::NodeDevice &nd, LLLink *link, const std::string &testName);

void runTestAsSlave(const Discovery::NodeDevice &nd, LLLink *link);


LinkEval::LinkEval() {
}


LinkEval::~LinkEval() {
}

void LinkEval::update(time_t now) {
}

void systemTimerAccuracy() {
    const static int tDurationUs = 1e6;
    LOG(logINFO) << "evaluating timer for  " << (tDurationUs / 1000) << " ms";

    int numHits = 0;
    int *pNumHits = &numHits;
    Histogram h;


    uint64_t lt = 0;

	std::vector<uint64_t> timerIntervalsUs;
	timerIntervalsUs.reserve(tDurationUs / 1000 * 2);

    {
        RttTimer t([&numHits, &h, &lt, &timerIntervalsUs]() {
            //LOG(logDEBUG) << "wait!";
            uint64_t t = UP::getSystemTimeNanoSeconds();
            //LOG(logDEBUG) << "hits!";
            if (lt > 0) {
				auto dt = (t - lt);
                h.addLog(dt);
				timerIntervalsUs.push_back((double)dt / 1e3);
                numHits++;
            }
            lt = t;

            return true;
        }, (uint64_t) 1e6); // 1ms
        { LOG(logDEBUG) << "sleep..."; }
        usleep(tDurationUs);
        { LOG(logDEBUG) << "waiting for timer thread to shutdown, currently " << numHits << " hits."; }
    }


    std::vector <Histogram> hv(1, h);
    Histogram::equalize(hv);

    std::string fn("./out/timer_acc.m");
    std::ofstream fs(fn);
    if (fs.good()) {
        fs << hv << "title('timer period (ns)');";
        LOG(logINFO) << "timer histogram with " << numHits << " hits data written to " << fn;
    }
    

	LOG(logINFO) << "Results (ms):";
	LOG(logINFO) << "mean: " << statsmath::mean(timerIntervalsUs) << "\t stdd: " << statsmath::std(timerIntervalsUs);
	LOG(logINFO) << "min: " << statsmath::min(timerIntervalsUs) << "\t max: " << statsmath::max(timerIntervalsUs);
	LOG(logINFO) << "timer evaluation done!";
}


void LinkEval::systemLatencyEval() {
#ifndef ANDROID
    // on android, timers are buggy!
    systemTimerAccuracy();
#endif

    static const uint64_t runTimeMs = (uint64_t) (1e3 * 1);
    static const uint64_t nsStep = 1000;
    static const int stressStartDelayUs = 20 * 1000;

    std::vector <Histogram> hMat(6);
    //volatile bool err = false;


    std::function<void(Histogram *, int wait)> measure([](Histogram *hCur, int wait) {
        uint64_t tl = UP::getSystemTimeNanoSeconds(), t, dt;
        uint64_t te = tl + runTimeMs * (uint64_t) 1e6;
        while (tl < te) {
            if (wait == 0)
                std::this_thread::yield();
            else if (wait > 0)
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

        rtt::stress::stop();
        //{LOG(logINFO) << (tl - te); }
    });

    {
        rtt::stress::startFlag();
		int numHogs = RttThread::GetSystemNumCores() + 1;

        LOG(logINFO) << "Stressing systems for " << (runTimeMs) << " ms with " << numHogs
                     << " workers per task. Tasks are: cpu,vm";


        {
            // create measuremnet (non-rt & rt)
            //RttThread mt4([&measure, &hMat]() { measure(&hMat[4], 1); }, false); // sleep 1us
            //RttThread mt0([&measure, &hMat]() { measure(&hMat[0], 0); }, false, "mt0");  // yield


            RttThread rtSleep([&measure, &hMat]() { measure(&hMat[5], 1); }, true, "mt_sleep_1ns"); // sleep 1us
            RttThread rtYield([&measure, &hMat]() { measure(&hMat[1], 0); }, true, "mt_yield"); // yield

            //RttThread mt2([&measure, &hMat]() { measure(&hMat[2], -1); }, false);  // no yield/sleep
            RttThread mt3([&measure, &hMat]() { measure(&hMat[3], -1); }, true, "mt_busy");



            // give the measurement threads time to spin up
            usleep(stressStartDelayUs);


            std::vector <RttThread> stressCpu(numHogs, RttThreadPrototype([]() {
				RttThread::GetCurrent().SetPriority(RttThread::Low);
                usleep(stressStartDelayUs);
                rtt::stress::cpu();
            }, false, "cpu_hog"));

            std::vector <RttThread> stressVm(numHogs, RttThreadPrototype([numHogs]() {
                RttThread::GetCurrent().SetPriority(RttThread::Low);
                usleep(stressStartDelayUs);
                rtt::stress::vm(256 * 1024 * 1024 / numHogs);
            }, false, "vm_hog"));

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

        rtt::stress::stop();
    }

    LOG(logDEBUG) << "Test done.";

    //if (err)
//		LOG(logWARNING) << "... with errors!";

    Histogram::equalize(hMat);

    std::vector <std::string> legend({
                                             "yield   ", "yield-rt",
                                             "busy    ", "busy-rt ",
                                             "nsleep1 ", "nslp1-rt"
                                     });


    // generate legend and compute best
    uint64_t best = -1, len = 0, vi = 0;
    for (auto &v : hMat) {
        uint64_t hits = v.hits();
        std::string leg = "max: " + std::to_string(v.maxLin() / 1000) + " us";
        legend[vi++] += leg;
        LOG(logDEBUG) << std::setw(30) << legend[vi - 1] << "   (hits " << std::setw(8) << (hits / 1000) << "k)";

        if (v.maxLin() > 1 && hits > 1000 && v.maxLin() < best) {
            best = v.maxLin();
        }
    }


    std::string fn("./out/sys_wakeup" + std::to_string(2) + ".m");
    std::ofstream fs(fn);
    if (fs.good()) {
        fs << hMat << "title('thread wakeup (" << nsStep << "ns)'); legend(" << legend
           << ");"; // xlim([-10 "<<(1e6/ nsStep) << "]); ";
        LOG(logINFO) << "wkaeup data written to " << fn;
    } else {
        LOG(logERROR) << "failed to write to " << fn;
    }


    if (best > 900 * 1000) { // us
        LOG(logERROR) << " !!! System has poor real-time performance (wakeup:" << (best / 1000) << " us) !!! ";
    }
}

void runTimedAsSlave(const Discovery::NodeDevice &nd, LLLink *link, const std::string &testName);

void runTimedMaster(const Discovery::NodeDevice &nd, LLLink *link, const std::string &testName);

void LinkEval::session(LLLinkGeneratorSet const &candidates, const Discovery::NodeDevice &nd, bool master) {
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

            if (!ll->connect(nd.getAddr(UP::LinkEvalPort + i), master)) {
                LOG(logERROR) << "Connection failed! (port " << (UP::LinkEvalPort + i) << ")";
                break;
            }

            if (!ll->setBlockingTimeout(master ? 2000 : (2000 * 20))) {
                LOG(logERROR) << "Failed to set blocking mode!";
                break;
            }

            LOG(logINFO) << "evaluating [" << (master ? "MASTER" : "SLAVE") << "] me  <-- " << testName << ":" << *ll
                         << " -->  " << nd.getName() << " [" << (!master ? "MASTER" : "SLAVE") << "]";

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

            { RttThread t(f, true); }

        } while (0);

        LOG(logINFO) << "evaluation of " << c.first << " ended ###";

        delete ll;
        i++;
    }
}


void runTimedMaster(const Discovery::NodeDevice &nd, LLLink *link, const std::string &testName) {
    static const int testDurationMs = 420;
    static const int blockSizes[] = {
            64, 128, 512
    };
    std::vector <std::string> legend({"64", "128", "512"});

    static const int nBlockSizes = sizeof(blockSizes) / sizeof(*blockSizes);

    std::vector <Histogram> timerHist(nBlockSizes);

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
                *reinterpret_cast<TestDataHeader *>(pDataBlock) = tdh;

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
            }, (uint64_t) 1e6);

            usleep(testDurationMs * 1000UL / nBlockSizes / 10UL);
        }
    }

    // let slave know that its over
    TestDataHeader tdh;
    memset(&tdh, 0, sizeof(tdh));
    tdh.testRunKey = testKey;
    link->send((const uint8_t *) &tdh, sizeof(tdh));

    LOG(logDEBUG) << "Packets send: " << nSent;


    auto now = UlltraProto::getMicroSeconds();

    {
        std::string fn("./out/timed_master_hist_" + testName + "_" + nd.getName() + "_" + std::to_string(now) + ".m");
        std::ofstream f(fn);
        if (f.good()) {
            f << timerHist << "legend(" << legend << "); title('latency (100us) " << UP::getDeviceName() << " <-["
              << *link << "]-> " << nd << "'); ";
            LOG(logDEBUG) << "data written to " << fn;
        }
    }
}


const uint8_t *slaveReceive(LLLink *link, int &testRunKey, uint64_t &tLastReceive, int dataLen) {
    const int MaxTimeouts = 50;
    const uint8_t *data;

    if (tLastReceive == 0)
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

            auto tdh = reinterpret_cast<const TestDataHeader *>(data);

            if (testRunKey == 0) {
                testRunKey = tdh->testRunKey;
                LOG(logDEBUG1) << "Test session key:" << testRunKey;
            } else if (testRunKey != tdh->testRunKey) {
                LOG(logINFO) << "Received with different session key, ignoring. (key:" << tdh->testRunKey << ", len: "
                             << dataLen << " sent " << ((UlltraProto::getSystemTimeNanoSeconds() - tdh->tSent) / 1000)
                             << " us ago)";
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
        } else {
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


void runTimedAsSlave(const Discovery::NodeDevice &nd, LLLink *link, const std::string &testName) {
    std::vector <std::string> legend({"64", "128", "512"});


    uint32_t nPackets = 0;
    int key = 0, len;
    const uint8_t *data;
    uint64_t tLastReceiveMs = 0, tNs;

    int lastBsi = -1;

    std::vector <Histogram> hist(3);
    std::vector <uint64_t> tLast(3, 0);
    std::vector <uint64_t> tMaxTWithoutRecv(3, 0);

    while (true) {
        len = 0;
        data = slaveReceive(link, key, tLastReceiveMs, len);

        if (!data)
            break;

        TestDataHeader *tdh = (TestDataHeader *) data;

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
        LOG(logINFO) << " BS=" << legend[i++] << "\tmax:" << h.maxLin() / 1e6 << " ms";
    }


    auto now = UlltraProto::getMicroSeconds();

    Histogram::equalize(hist);

    {
        std::string fn("./out/timed_slave_hist_" + testName + "_" + nd.getName() + "_" + std::to_string(now) + ".m");
        std::ofstream f(fn);
        if (f.good()) {
            f << hist << "legend(" << legend << "); title('receiver jitter (ns) " << UP::getDeviceName() << " <-["
              << *link << "]-> " << nd << "'); ";
            LOG(logDEBUG) << "data written to " << fn;
        }
    }
}


void runTestAsMaster(const Discovery::NodeDevice &nd, LLLink *link, const std::string &testName) {
    static const int nPasses = 10000;
    //static const int nPasses = 200;
    static const int blockSizes[] = {
            //32, 64, 128, 256, 512, 1024, //, 2048
            //	32, 128, 512
            64, 128, 512
    };


    static const int nBlockSizes = sizeof(blockSizes) / sizeof(*blockSizes);

    // state
    bool cancel = false;
    int timeouts = 0;
    int packetsSend = 0, packetsReceived = 0;
    uint64_t bytesSentTotal = 0;
    int bytesInFlight = 0, packetsInFlight = 0;
    uint64_t tLastReceive = UlltraProto::getSystemTimeNanoSeconds();

    std::vector <Histogram> hists(nBlockSizes);

#ifdef MATLAB_REPORTS
    std::vector <std::vector<float>> statMat(nBlockSizes, std::vector<float>(nPasses + 1, 0.0f));
    std::vector<int> statMatIdx(nPasses, 0);
    std::vector <std::string> legend(nBlockSizes, "");
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
                *reinterpret_cast<TestDataHeader *>(&dataBlocks[blockSize * (pi % nRandBlocks)]) = tdh;

                if (!link->send(&dataBlocks[blockSize * (pi % nRandBlocks)], blockSize)) {
                    LOG(logERROR) << "Failed sending data block during latency test, cancelling!";
                    cancel = true;
                    break;
                }

                bytesSentTotal += (uint64_t) blockSize;
                bytesInFlight += (int) blockSize;
                packetsInFlight++;
                packetsSend++;
            }

            const uint8_t *receivedData;
            int recvLen = 0;
            while (packetsInFlight > 0 && (receivedData = link->receive(recvLen))) {
                if (recvLen < sizeof(TestDataHeader)) {
                    LOG(logINFO) << "Received packet with invalid size " << recvLen << ", ignoring.";
                    continue;
                }

                auto tdh = reinterpret_cast<const TestDataHeader *>(receivedData);

                // dont expect packages to arrive in-order, just check when it was sent!
                if (tdh->testRunKey != key) {
                    LOG(logINFO) << "Received with different session key, ignoring. (sent "
                                 << ((UlltraProto::getSystemTimeNanoSeconds() - tdh->tSent) / 1000) << " us ago)";
                    continue;
                }

                if (tdh->blockSizeIndex < 0 || tdh->blockSizeIndex >= nBlockSizes) {
                    LOG(logINFO) << "Received packet of size " << recvLen << " with header byte = 0, cancelling!";
                    cancel = true;
                    break;
                }

                packetsReceived++;
                packetsInFlight--;
                bytesInFlight -= (int) recvLen;
                tLastReceive = now;

                if (tdh->tSent > 0) {
                    //uint64_t rtt = UP::getMicroSeconds() - tdh->tSent;
                    uint64_t rtt = UP::getSystemTimeNanoSeconds() - tdh->tSent;
                    hists[tdh->blockSizeIndex].addLog(rtt);
#ifdef MATLAB_REPORTS
                    if (rtt > 0 && statMatIdx[tdh->blockSizeIndex] < nPasses)
                        statMat[tdh->blockSizeIndex][statMatIdx[tdh->blockSizeIndex]++] = (float) rtt;
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
    link->send((const uint8_t *) &tdh, sizeof(tdh));

    LOG(logDEBUG) << "Packets send: " << packetsSend << " ( " << ((float) bytesSentTotal / 1000.0f / 1000.0f)
                  << " MB), recv: " << packetsReceived;


    float packetLoss = 1.0f - (float) (packetsReceived) / (float) packetsSend;

    if (packetLoss > 0.4f) {
        LOG(logERROR) << "Packet loss " << packetLoss << " too high!";
        return;
    }

    auto now = UlltraProto::getMicroSeconds();

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
        if (max) {
            int i = 0;
            while (!(rttmin = (int) statMat[bsi][statMat[bsi].size() - 1 - i])) { i++; };
        }
        LOG(logINFO) << "\tBS " << std::setw(8) << blockSize << "  RTT "
                     << "     min: " << std::setw(5) << (int) (rttmin / 1000.0f) << " us"
                     << "     med: " << std::setw(5) << (int) (median / 1000.0f) << " us"
                     << "     max: " << std::setw(5) << (int) (max / 1000.0f) << " us"
                     << "     max(1%): " << std::setw(5) << (int) (max1 / 1000.0f) << " us"
                     << "     max(1): " << std::setw(5) << (int) (statMat[bsi][1] / 1000.0f) << " us";

        legend[bsi] = "bs " + std::to_string(blockSize) + " med=" + std::to_string(median);
    }
    LOG(logINFO) << std::endl;


    auto hostname = UP::getDeviceName();

    std::string fn("./out/" + testName + "_" + nd.getName() + "_" + std::to_string(now) + ".m");
    std::ofstream f(fn);
    if (f.good()) {
        f << "figure; l=(" << statMat << "');" << std::endl;
        f
                << "  plot(medfilt1(l, 25), '-*', 'LineWidth', 4); hold on;  grid on; ylim([1000, 10000]*1000); xlim(size(l,1)*[-0.005 0.05]);"
                << std::endl;
        f << " ax = gca; ax.ColorOrderIndex = 1; plot(l, 'LineWidth', 1);  " << std::endl;
        f << "legend(" << legend << "); title('latency " << hostname << " <-[" << *link << "]-> " << nd << "'); ";
        LOG(logDEBUG) << "data written to " << fn;
    }

    {
        std::string fn("./out/hist_" + testName + "_" + nd.getName() + "_" + std::to_string(now) + ".m");
        std::ofstream f(fn);
        if (f.good()) {
            f << hists << "legend(" << legend << "); title('latency (ns) " << hostname << " <-[" << *link << "]-> "
              << nd << "'); ";
            LOG(logDEBUG) << "data written to " << fn;
        }
    }
#endif

    if (cancel) {
        LOG(logDEBUG) << "Test cancelled due to errors!";
        return;
    }
}


void runTestAsSlave(const Discovery::NodeDevice &nd, LLLink *link) {
    const int MaxTimeouts = 50;

    uint32_t nPackets = 0;
    int key = 0;

    const uint8_t *data;
    int len = 0;

    uint64_t tLastReceive = UP::getMicroSecondsCoarse();

    // just bounce packages until end flag received (or timeouts)
    while (true) {
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


void LinkEval::outputExtendedLinkStats(LLLink *link, std::string const &testName) {
    auto &hist = link->getStackDelayHistogramUs();
    if (hist.size() <= 2) {
        LOG(logINFO) << "NO stack delay data available!";
        return;
    }

    auto hits = sum(hist);
    if (hits < 100) {
        LOG(logINFO) << "Stack delay hist consists of " << hits << " hits, need 100 at least for output!";
        return;
    }

    int maxNon0;
    for (maxNon0 = hist.size() - 1; maxNon0 > 0; maxNon0--) {
        if (hist[maxNon0] > 0) break;
    }

    LOG(logINFO) << "Max stack delay: " << maxNon0 << " us";

    std::string fn(
            "./out/stack_delay_" + testName + "_" + std::to_string(UP::getSystemTimeNanoSeconds() / 1000 / 1000) +
            ".m");
    std::ofstream f(fn);
    if (f.good()) {
        f << "stack_delay_hist_usec=(" << hist << ");";
        f
                << "figure; plot(stack_delay_hist_usec(1:max(find(stack_delay_hist_usec)))); title('stack delay (us) histogram "
                << *link << "'); grid on;";
        LOG(logINFO) << "stack delay hist written to " << fn << " (" << hits << " hits)";
    } else {
        LOG(logERROR) << "failed to write to " << fn;
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
                    // limit bandwidth to 0.05 MB/s = 0.1B/�s
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
