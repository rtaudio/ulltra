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
#include <chrono>

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

	int RttMin = 500; // us
	int MaxBytesPerSecond = 1024 * 64;

	std::function<void(void)> f([this, &nd]() {
		LLSender lls(nd.addr, UlltraProto::LinkEvalPort);
		LLReceiver llr(UlltraProto::LinkEvalPort, 4000);

		// linux performs better with user space blocking! (at least on non RT)
#ifndef _WIN32
		llr.setBlocking(BlockingMode::UserBlock);
#else
		llr.setBlocking(BlockingMode::KernelBlock);
#endif

		struct sockaddr_storage recvAddr = {};
		int recvLen;

		// discard any queued packets
		llr.clearBuffer();

		static const int blockSizes[] = {
			//32, 64, 128, 256, 512, 1024, //, 2048
			//32
			1024
		};

		static const int nBlockSizes = sizeof(blockSizes) / sizeof(*blockSizes);
		static const int nPasses = 200;

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
		uint64_t bytesSentTotal = 0;

		int bytesInFlight = 0, packetsInFlight = 0;

		uint64_t tLastSend = 0;

		//int currentReceiveTimeout = 4000;

		
			//if(pi %(nPasses/10) == 0)
			//	LOG(logDEBUG) << "Pass (" << pi << "/"<<nPasses << ")";

		for (int bsi = 0; bsi < sizeof(blockSizes) / sizeof(*blockSizes); bsi++) {
			for (int pi = 0; pi < nPasses; pi++) {
				int blockSize = blockSizes[bsi];
				bool finalPass = (pi == nPasses - 1);


				bool skipSend = false;
				if (bytesInFlight > 512) {
					skipSend = true;
					//LOG(logDEBUG) << "Skipping send bytes in fligt:" << bytesInFlight << " packets " << packetsInFlight;
					//pi--;
				}

				uint64_t now = UlltraProto::getSystemTimeNanoSeconds();

				if (!finalPass || skipSend) {
					dataBlocks[blockSize * pi] = 1 + bsi; // set first packet byte to >1
					*reinterpret_cast<int*>(&dataBlocks[blockSize * pi + 1]) = key;
					*reinterpret_cast<uint64_t*>(&dataBlocks[blockSize * pi + 1 + sizeof(int)]) = now;

					if (lls.send(&dataBlocks[blockSize * pi], blockSize) != blockSize) {
						LOG(logERROR) << "Failed sending data block during latency test, cancelling!";
						cancel = true; break;
					}

					tLastSend = now;

					bytesSentTotal += (uint64_t)blockSize;
					bytesInFlight += (int)blockSize;
					packetsInFlight++;
					packetsSend++;
				}


				bool received = false;

				if ((packetsSend - packetsReceived) > 50) {
					// increase the timeout
				}

				bool tooManyInFlight = false;

				const uint8_t *receivedData;
				while ((receivedData = llr.receive(recvLen, recvAddr))) {

					if (recvLen < 16) {
						LOG(logINFO) << "Received packet with invalid size " << recvLen << ", ignoring.";
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

					packetsInFlight--;
					bytesInFlight -= (int)recvLen;

					uint64_t sentAt = *reinterpret_cast<const uint64_t*>(&receivedData[1 + sizeof(int)]);
					if (sentAt > 0 && statMatIdx[rbsi] < nPasses) {
						//uint64_t rtt = UP::getMicroSeconds() - sentAt;
						uint64_t rtt = UP::getSystemTimeNanoSeconds() - sentAt;
						if (rtt > 0)
							statMat[rbsi][statMatIdx[rbsi]++] = rtt;
					}
				}

				if (cancel)
					break;
			}
			if (cancel)
				break;
		}

		// let slave know that its over
		uint8_t z[32];
		memset(z, 0, sizeof(z));
		*reinterpret_cast<int*>(z + 1) = key;
		lls.send(z, sizeof(z));


		delete dataBlocks;

		if (cancel)
			return;


		LOG(logDEBUG) <<"Packets send: " << packetsSend <<" ( " << ((float)bytesSentTotal/1000.0f/1000.0f) << " MB), recv: " <<  packetsReceived;



		for (int bsi = 0; bsi < sizeof(blockSizes) / sizeof(*blockSizes); bsi++) {
			int blockSize = blockSizes[bsi];
		/*	uint64_t min, max, med, maxs, maxr;

			LOG(logDEBUG1) << "BS=" << blockSize << ":";

			stats[bsi].getStats(min, maxs, med, 0);
			stats[bsi].getStats(min, maxr, med, 1);
			LOG(logDEBUG2) << "send max: " << std::setw(8) << maxs << ", recv max: " << std::setw(8) << maxr;
			stats[bsi].getStats(min, max, med);
			LOG(logDEBUG2) << "acc : min: " << std::setw(8) << min << ", max: " << std::setw(8) << max << ", median: " << std::setw(8) << med;
			*/
#ifdef _DEBUG
			//statMat[bsi] = stats[bsi].getAccTS<float>();
			legend[bsi] = "bs " + std::to_string(blockSize);
#endif
		}		

		float packetLoss = 1.0f - (float)(packetsReceived) / (float)packetsSend;

		if (packetLoss > 0.4f) {
			LOG(logERROR) << "Packet loss " << packetLoss << " too high!";
			return;
		}

#ifdef _DEBUG
		char hostname[32];
		gethostname(hostname, 31);

		std::string fn("./latencies_" + nd.name + "_" + std::to_string(UlltraProto::getMicroSeconds()) + ".m");
		std::ofstream f(fn);
		f << "figure; l=(" << statMat << "');" << std::endl;
		f << "  plot(medfilt1(l, 25), '-*', 'LineWidth', 4); hold on;  grid on; ylim([1000, 10000]*1000);" << std::endl;
		f << " ax = gca; ax.ColorOrderIndex = 1; plot(l, 'LineWidth', 1); " << std::endl;
		f << "legend(" << legend << "); title('latency " << hostname << " <-> " <<  nd << " " << llr << "'); ";
		LOG(logDEBUG) << "data written to " << fn;
#endif		
	});

	{RttThread t(f, true); }
	LOG(logINFO) << "latency test thread ended";
}

void LinkEval::latencyTestSlave(const Discovery::NodeDevice &nd)
{
	LOG(logDEBUG) << "Starting link evaluation (SLAVE) for node " << nd << " (MASTER)";

	std::function<void(void)> f([this, &nd]() {
		LLSender lls(nd.addr, UlltraProto::LinkEvalPort);
		LLReceiver llr(UlltraProto::LinkEvalPort, UlltraProto::LinkEvalTimeoutMS*1000);


		// TODO: need to check if user block performs better on linux!
#ifndef _WIN32
		llr.setBlocking(BlockingMode::UserBlock);
#else
		llr.setBlocking(BlockingMode::KernelBlock);
#endif


		int len = 0;
		struct sockaddr_storage ad = {};
	
		uint32_t nPackets = 0;

		int key;

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
				std::cout << "Reached max number of " << retries << " timeouts after receiving " << nPackets << " packets, canceling." << std::endl;
				break;
			}

			if (len < 20) {
				LOG(logINFO) <<  "received small package, ignoring.";
				continue;
			}

			int rkey = *reinterpret_cast<const int*>(&data[1]);

			if (nPackets == 0) {
				key = rkey;
			}
			else if (key != rkey) {
				LOG(logINFO) << "Received with different session key, ignoring.";
				continue;
			}

			nPackets++;

			if (data[0] == 0) {
				std::cout << "Latency test ended (end flag)" << std::endl;
				break;
			}

			if (lls.send(data, len) != len) { // just send it back
				LOG(logERROR) << "send failed, cancelling!";
				break;
			}

			//LOG(logDEBUG1) << "bounced " << len;
		}

	});

	{ RttThread t(f, true); }
	LOG(logDEBUG1) << "Link evaluation ended";
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