#include <cstddef>
#include <iostream>
#include <random>
#include <vector>
#include <fstream>
#include <cstring>
#include <sstream>
#include <chrono>
#include "../include/Xalloc.h"

struct Op {
	bool is_alloc;
	size_t size;
	int free_idx;
};

constexpr size_t ITR = 200'000;
constexpr int SEED = 69;
static_assert(SEED == 69, "Fuck off");

class MonotonicSbrkBench {
private:
	long current_rss_kb() {
		std::ifstream f("/proc/self/status");
		std::string line;
		while (std::getline(f, line)) {
			if (line.rfind("VmRSS:") == 0) {
				std::istringstream in(line.substr(6));
				long kb;
				in >> kb;
				return kb;
			}
		}
		return -1;
	}

	long peak_rss_kb() {
		std::ifstream f("/proc/self/status");
		std::string line;
		while (std::getline(f, line)) {
			if (line.rfind("VmHWM:", 0) == 0) {
				std::istringstream in(line.substr(6));
				long kb;
				in >> kb;
				return kb;
			}
		}
		return -1;
	}
public:
	std::vector<Op> generate_workload(size_t itr, int seed) {
		std::mt19937_64 rng(seed);
		std::discrete_distribution<int> alloc_size_perc({ 70, 25, 5 });
		std::uniform_int_distribution<size_t> small_alloc(0, 64 );
		std::uniform_int_distribution<size_t> medium_alloc(65, 512);
		std::uniform_int_distribution<size_t> large_alloc(513, 4096);

		std::vector<Op> ops;
		std::vector<int> outstanding;

		for (int i = 0; i < itr; i++) {
			size_t size;
			switch (alloc_size_perc(rng)) {
				case 0: size = small_alloc(rng);
				case 1: size = medium_alloc(rng);
				case 2: size = large_alloc(rng);
			}
			ops.push_back(Op{ true, size, -1 });
			outstanding.push_back(i);

			if (!outstanding.empty() && rng() % 3 == 0) {
				std::uniform_int_distribution<int> free(0, outstanding.size() - 1);
				int free_id = free(rng);
				ops.push_back(Op{ false, 0, outstanding[free_id]});
				outstanding.erase(outstanding.begin() + free_id);
			}
		}
		return ops;
	}

	template <typename Allocfn, typename FreeFn>
	void run_workload(const char* label, std::vector<Op> ops, Allocfn allocfn, FreeFn freefn) {
		std::vector<void*> live(ops.size() - 1, nullptr);
		long before_rss = current_rss_kb();
		auto before_time = std::chrono::high_resolution_clock::now();

		int counter = 0;
		for (const auto& op : ops) {
			if (op.is_alloc) {
				void* mem = allocfn(op.size);
				if (mem) std::memset(mem, 0xFF, op.size);
				live[counter] = mem;
				counter++;
			}
			else {
				if (live[op.free_idx]) {
					freefn(live[op.free_idx]);
					live[op.free_idx] = nullptr;
				}
			}
		}

		auto after_time = std::chrono::high_resolution_clock::now();
		long after_rss = current_rss_kb();
		long peak_rss = peak_rss_kb();
		auto time_dur = std::chrono::duration<double, std::milli>(after_time - before_time).count();

		std::cout << std::format("===={}====\n", label);
		std::cout << std::format("   Time:          {}ms\n", time_dur);
		std::cout << std::format("   Throughput:    {}op/sec\n", (double)ops.size() / (time_dur / 1000));
		std::cout << std::format("   Before RSS:    {}kb\n", before_rss);
		std::cout << std::format("   After RSS:     {}kb\n", after_rss);
		std::cout << std::format("   Peak RSS:      {}kb\n", peak_rss);
		std::cout << std::format("   RSS Growth:    {}mb\n", (after_rss - before_rss) / 1024);
	}
};

int main() {
	xalloc::monotonicBrk::test_metadata(23);

	MonotonicSbrkBench mbb;
	std::vector<Op> ops = mbb.generate_workload(ITR, SEED);

	mbb.run_workload("Monotonic sbrk alloc", ops, xalloc::monotonicBrk::alloc, xalloc::monotonicBrk::free);
	mbb.run_workload("Standard malloc", ops, [](size_t size) {return std::malloc(size);}, [](void* mem) {return std::free(mem);});
	return 0;
}