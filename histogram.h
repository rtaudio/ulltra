#define NOMINMAX

#include <limits>
#include <cstddef>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

namespace statsmath {
	template<typename T> double mean(const std::vector<T> &v) {
		double sum = 0.0;
		for (T a : v)
			sum += (double)a;
		return sum / (double)v.size();
	}

	template<typename T> double var(const std::vector<T> &v) {
		double m = mean(v);
		double temp = 0, t;
		for (T a : v) {
			t = static_cast<double>(a) - m;
			temp += t*t;
		}
		return temp / (double)v.size();
	}

	template<typename T> double std(const std::vector<T> &v) {
		return sqrt(var(v));
	}

	template<typename T> double min(const std::vector<T> &v) {
		double min = std::numeric_limits<double>::max();
		for (T a : v) {
			auto d = static_cast<double>(a);
			if (d < min) min = d;
		}
		return min;
	}
	template<typename T> double max(const std::vector<T> &v) {
		double max = std::numeric_limits<double>::min();
		for (T a : v) {
			auto d = static_cast<double>(a);
			if (d > max) max = d;
		}
		return max;
	}
}


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
		s = (uint64_t)1e4;
		maxVal = 0;
        maxValLog = 0;
		h.resize((size_t)s, 0);
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
				h.resize(static_cast<size_t>(v + v / 6UL + 10UL), 0);
				s = h.size();
			}
		}
		else {
			d = 0;
		}
		h[(size_t)v]++;
		return d;
	}

	uint64_t hits() {
		return sum(h);
	}

    uint64_t maxLin() {
        if (maxVal == 0 && h[0] == 0)
            return 0;
        return maxVal;
    }

    uint64_t maxLog() {
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
			h.h.resize(static_cast<size_t>(m+2), 0);
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