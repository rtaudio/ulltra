#include "LinkEval.h"

#include"UlltraProto.h"
#include"SimpleUdpReceiver.h"
#include"LLReceiver.h"
#include"LLSender.h"
#include "NetworkManager.h"

#include<rtt.h>

#include<iostream>
#include<iomanip>

#include "networking.h"

#include <thread>

LinkEval::LinkEval() :  m_netMan(0)
{
}

bool LinkEval::init(NetworkManager *netManager)
{
	m_netMan = netManager;
	return true;
}

LinkEval::~LinkEval()
{
}

void LinkEval::update(time_t now)
{
}

void LinkEval::latencyTestMaster(const Discovery::NodeDevice &nd)
{
	LOG(logDEBUG) << "Starting link evaluation (MASTER) with node " << nd;

	std::function<void(void)> f([this, &nd]() {
		LLSender lls(nd.addr, UlltraProto::LinkEvalPort);
		LLReceiver llr(UlltraProto::LinkEvalPort, 80);
		llr.setBlocking(LLReceiver::BlockingMode::KernelBlock);

		sockaddr_in recvAddr;
		int recvLen;

		// discard any queued packets
		llr.clearBuffer();

		static const int blockSizes[] = {
			32, 64, 128, 256, 512, 1024, //, 2048
		};

		static const int nBlockSizes = sizeof(blockSizes) / sizeof(*blockSizes);
		static const int nPasses = 80;
		static const int nPassesWarmUp =  4;

		bool cancel = false;

#ifdef _DEBUG
		std::vector<std::vector<float>> statMat(nBlockSizes, std::vector<float>(nPasses+1, 0.0f));
		std::vector<int> statMatIdx(nPasses, 0);
		std::vector<std::string> legend(nBlockSizes, "");
#endif

		// generate random data blocks
		int key = rand();
		auto dataBlocks = new uint8_t[1024 * nPasses];
		for (int i = 0; i < 1024 * nPasses; i++) {
			dataBlocks[i] = 1 + (rand() % 254); // dont send zeros (0 means end of test)
		}

		LatencyStats<2> stats[nBlockSizes];	

		int timeouts = 0;
		int packetsSend = 0, packetsReceived = 0;

		for (int pi = 0; pi < nPasses; pi++) {
			LOG(logDEBUG) << "Pass (" << pi << "/"<<nPasses << ")";

			for (int bsi = 0; bsi < sizeof(blockSizes) / sizeof(*blockSizes); bsi++) {
				int blockSize = blockSizes[bsi];

				uint64_t now = 0;
				if (pi >= nPassesWarmUp) {
					now = stats[bsi].begin(0);
				}

				// set first packet byte to 1, followed by timestamp
				dataBlocks[blockSize * pi] = 1+bsi;
				*reinterpret_cast<int*>(&dataBlocks[blockSize * pi + 1]) = key;
				*reinterpret_cast<uint64_t*>(&dataBlocks[blockSize * pi + 1 + sizeof(int)]) = now;

				if (lls.send(&dataBlocks[blockSize * pi], blockSize) != blockSize) {
					LOG(logERROR) << "Failed sending data block during latency test, cancelling!";
					cancel = true;
					break;
				}

				packetsSend++;

				if (pi >= nPassesWarmUp)
					stats[bsi].begin(1);


				const uint8_t *receivedData;

				bool received = false;

				// always empty receive buffer before sending new packet!
				while ((receivedData = llr.receive(recvLen, recvAddr))) {
					if (recvAddr.sin_addr.s_addr != nd.addr.s_addr) {
						LOG(logINFO) << "Received packet from invalid host during latency test, ignoring.";
						continue;
					}

					if (recvLen < 16) {
						LOG(logINFO) << "Received packet with invalid size, ignoring.";
						continue;
					}

					// dont expect pacakges to arrive in-order, just check when it was sent!
					int rbsi = (int)receivedData[0] - 1;
					int rkey = *reinterpret_cast<const int*>(&receivedData[1]);

					if (rkey != key) {
						LOG(logINFO) << "Received with different session key, ignoring.";
						continue;
					}
					
					if (rbsi < 0 || rbsi >= nBlockSizes) {
						LOG(logINFO) << "Received packet of size " << recvLen << " with header byte = 0, cancelling!";
						cancel = true;
						break;
					}					

					received = true;
					packetsReceived++;

					uint64_t sentAt = *reinterpret_cast<const uint64_t*>(&receivedData[1+sizeof(int)]);
					if (sentAt > 0 && statMatIdx[bsi] < nPasses) {
						uint64_t rtt = UP::getMicroSeconds() - sentAt;
						//LOG(logINFO) << "recv rtt" << rtt << " bsi " << rbsi;
						statMat[rbsi][statMatIdx[bsi]++] = rtt;
					}
				}

				//std::this_thread::yield();

				if (!received) {
					//LOG(logINFO) << "Nothing received this cycle.";
				}

				if (cancel)
					break;
				/*
				if (pi >= nPassesWarmUp) {
					auto dt = stats[bsi].end();
					// limit bandwidth to 0.05 MB/s = 0.05B/µs
					double wait = (double)(blockSize) / 0.05 - (double)dt;
					if (wait < 1009.0) wait = 1000.0; // ...  and max 2 packets/ms
					usleep((uint32_t)wait);
					//spinWait((uint32_t)wait);
				}
				else {
					usleep(5000);
				}
				*/
			}
			if (cancel)
				break;
		}

		// let slave know that its over
		uint8_t z = 0;
		lls.send(&z, 1);

		delete dataBlocks;

		if (cancel)
			return;


		LOG(logDEBUG) <<"Packets send: " << packetsSend <<", recv: " <<  packetsReceived;


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
			//statMat[bsi] = stats[bsi].getAccTS<float>();
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

void LinkEval::latencyTestSlave(const Discovery::NodeDevice &nd)
{
	LOG(logDEBUG) << "Starting link evaluation (SLAVE) for node " <<nd << " (MASTER)";

	std::function<void(void)> f([this, &nd]() {
		LLSender lls(nd.addr, UlltraProto::LinkEvalPort);
		LLReceiver llr(UlltraProto::LinkEvalPort, UlltraProto::LinkEvalTimeoutMS*2);

		llr.setBlocking(LLReceiver::BlockingMode::KernelBlock);

		int len = 0;
		sockaddr_in ad;
	

		// just bounce packages until end flag received (or timeouts)
		while (true) {
			len = 0;
			auto data = llr.receive(len, ad);

			int retries = 1;
			while (!data && retries < 10) {
				std::cout << "Timeout during latency test, retrying." << std::endl;
				data = llr.receive(len, ad);
				retries++;
			}

			if (!data) {
				std::cout << "Reached max numnber of timeouts, canceling." << std::endl;
				break;
			}

			if (ad.sin_addr.s_addr != nd.addr.s_addr) {
				std::cout << "Received packt from invalid host during latency test, ignoring (expected " << nd << ", got " << std::string(inet_ntoa(nd.addr)) << ")" << std::endl;
				continue;
			}

			if (data[0] == 0) {
				std::cout << "Latency test ended (end flag)" << std::endl;
				break;
			}

			lls.send(data, len); // just send it back

			//LOG(logDEBUG1) << "bounced " << len;
		}

	});

	{ RttThread t(f, false); }
	LOG(logDEBUG1) << "Link evaluation ended";
}
