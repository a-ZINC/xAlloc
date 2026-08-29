#pragma once

#include <cstddef>
#include <unistd.h>

namespace xalloc {
	namespace monotonicBrk {
		void* alloc(size_t size);
		void free(void* addr);
	}
}