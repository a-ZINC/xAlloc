#include "../include/Xalloc.h"

namespace xalloc {
	namespace monotonicBrk {
		void* alloc(size_t size) {
			if (size == 0) return nullptr;
			size_t aligned = (size + 7) & ~(size_t(7));
			void* curr = sbrk(static_cast<intptr_t>(aligned));
			if (curr == (void*)-1) return nullptr;
			return curr;
		}

		void free(void* addr) {}
	}
}