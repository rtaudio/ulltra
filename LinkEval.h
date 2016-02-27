#pragma once

#include "networking.h"
#include "UlltraProto.h"
#include "Discovery.h"

#include <vector>
#include <algorithm>



class NetworkManager;
class SimpleUdpReceiver;
class LLReceiver;

class LinkEval
{
public:
	template<uint8_t N>
	struct LatencyStats {
		uint64_t tLast;
		uint8_t lastStep;

		std::vector<uint64_t> ts[N];

		LatencyStats() : tLast(0), lastStep(0) {
			for (uint8_t i = 0; i < N; i++) {
				ts[i].reserve(2000);
			}
		}

		inline uint64_t begin(uint8_t step, bool end=false) {
			if (step >= N)
				return 0;

			auto now = UlltraProto::getMicroSeconds();
			//auto now = UlltraProto::getSystemTimeNanoSeconds() / 1000;
			uint64_t dt;

			if (tLast > 0) {
				dt = now - tLast;
				ts[lastStep].push_back(dt);
			}
			else {
				dt = 0;
			}

			tLast = end ? 0 : now;
			lastStep = end ? -1 : step;

			return now;
		}

		inline uint64_t end() {
			return begin(lastStep, true);
		}

		template<typename T>
		std::vector<T> getAccTS() {
			uint32_t s(ts[0].size() - 1);
			std::vector<T> tsv(s, 0);
			

			for (uint8_t si = 0; si < N; si++) {
				for (uint32_t i = 0; i < s; i++) {
					tsv[i] += (T)ts[si][i];
				}
			}
			return tsv;
		}

		void getStats(uint64_t &min, uint64_t &max, uint64_t &median, int step = -1) {
			if (step >= N || ts[0].size() < 3)
			{
				max = min = median = -1;
				return;
			}
				

			std::vector<uint64_t> tsv;

			// get accumulated median
			if (step < 0)
				tsv = getAccTS<uint64_t>();
			else
				tsv = ts[step];

			std::sort(tsv.begin(), tsv.end());
			
			median = tsv[tsv.size() / 2];
			min = tsv[0];
			max = tsv[tsv.size()-1];
		}
	};

	LinkEval();
	~LinkEval();
	bool init(NetworkManager *netManager);

	void update(time_t now);
	void latencyTestMaster(const Discovery::NodeDevice &nd);
	void latencyTestSlave(const Discovery::NodeDevice &nd);

private:
	NetworkManager *m_netMan;

};

