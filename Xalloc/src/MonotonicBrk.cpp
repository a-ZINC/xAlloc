#include "../include/Xalloc.h"
#include<iostream>

namespace xalloc {
	namespace monotonicBrk {
		using Header = uint64_t;
		constexpr size_t HEADER_SIZE = sizeof(Header);
		constexpr uint64_t ALLOC_BIT = 0x1;
		constexpr uint64_t SIZE_MASK = ~ALLOC_BIT;
		size_t ALIGNED_SIZE = 8; //can change according to your need for eliminating SIGBUS error etc.

		Header* header_of(void* mem) {
			return reinterpret_cast<Header*>(reinterpret_cast<char*>(mem) - HEADER_SIZE);
		}

		bool is_alloc(Header header) {
			return (header & ALLOC_BIT) != 0;
		}

		uint64_t block_size(Header header) {
			return (header & SIZE_MASK);
		}

		void* alloc(size_t size) {
			if (size == 0) return nullptr;
			size_t total = size + HEADER_SIZE;
			total = (total + (ALIGNED_SIZE - 1)) & ~(ALIGNED_SIZE - 1);
			void* mem = sbrk(static_cast<intptr_t>(total));
			if (mem == (void*)-1) return nullptr;
			Header* header = reinterpret_cast<Header*>(mem);
			*header = total | ALLOC_BIT;

			return reinterpret_cast<char*>(mem) + HEADER_SIZE;
		}

		void free(void* mem) {
			if (!mem) return;
			Header* header = header_of(mem);
			assert(is_alloc(*header) && "double free or invalid pointer detected!");
			*header = block_size(*header);
		}

		void set_aligned(size_t size) {
			assert(size > 0 && size % 8 == 0 && "Keep alignment x8, to minimize SIGBUS error!");
			ALIGNED_SIZE = size;
		}

		void test_metadata(size_t size) {
			void* memory = alloc(size);
			Header* header = header_of(memory);
			assert(is_alloc(*header) && "Bro not allocated!");
			assert(block_size(*header) >= size + HEADER_SIZE);

			free(memory);
			assert(!is_alloc(*header) && "bro free didnt occur");
			std::cout << "metadata test passed" << std::endl;
		}
	}
}