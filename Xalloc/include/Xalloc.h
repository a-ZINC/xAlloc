#pragma once

#include <cstddef>
#include <unistd.h>
#include <cstdint>
#include <array>
#include <vector>
#include <cstring>
#include <cassert>

namespace xalloc {
	namespace monotonicBrk {
		void* alloc(size_t size);
		void free(void* addr);
		void set_aligned(size_t size);

		// OPTIONAL:
		uint64_t get_brk_count();
		uint64_t get_coalesce_count();
		uint64_t get_split_count();
		int get_num_class();
		size_t get_base_class_size();

		// TODO: remove once testing done
		void test_metadata(size_t size);
		void stats_external_fragmentation(long& count, double& avg_size);
		void stats_class_based(std::vector<long>& classes);
	}
}