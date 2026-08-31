#pragma once

#include <cstddef>
#include <unistd.h>
#include <cstdint>
#include <cassert>

namespace xalloc {
	namespace monotonicBrk {
		void* alloc(size_t size);
		void free(void* addr);
		void set_aligned(size_t size);

		// OPTIONAL:
		uint64_t get_brk_count();

		uint64_t get_split_count();

		// TODO: remove once testing done
		void test_metadata(size_t size);
		void stats_external_fragmentation(long& count, double& avg_size);
	}
}