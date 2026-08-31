#include "../include/Xalloc.h"
#include<iostream>

namespace xalloc {
	namespace monotonicBrk {
		using Header = uint64_t;
		constexpr size_t HEADER_SIZE = sizeof(Header);
		constexpr uint64_t ALLOC_BIT = 0x1;
		constexpr uint64_t SIZE_MASK = ~ALLOC_BIT;
		size_t ALIGNED_SIZE = 8; //can change according to your need for eliminating SIGBUS error etc.
		Header* freelist = nullptr;
		constexpr size_t MIN_BLOCK_SIZE = HEADER_SIZE + sizeof(void*);
		uint64_t g_split_count = 0;
		uint64_t g_brk_count = 0;

		Header* header_of(void* mem) {
			return reinterpret_cast<Header*>(reinterpret_cast<char*>(mem) - HEADER_SIZE);
		}

		bool is_alloc(Header header) {
			return (header & ALLOC_BIT) != 0;
		}

		uint64_t block_size(Header header) {
			return (header & SIZE_MASK);
		}

		void* payload_of(Header* header) {
			return reinterpret_cast<char*>(header) + HEADER_SIZE;
		}

		static Header*& free_next_payload(Header* header) {
			return *reinterpret_cast<Header**>(payload_of(header));
		}

		void free_list_push(Header* header) {
			free_next_payload(header) = freelist;
			freelist = header;
		}

		Header* free_list_find_remove(size_t size) {
			Header** link = &freelist;
			Header* curr = freelist;

			while (curr) {
				if (size <= block_size(*curr)) {
					*link = free_next_payload(curr);
					return curr;
				}
				link = &free_next_payload(curr);
				curr = free_next_payload(curr);
			}
			return nullptr;
		}

		void* alloc(size_t size) {
			if (size == 0) return nullptr;
			size_t total = size + HEADER_SIZE;
			total = (total + (ALIGNED_SIZE - 1)) & ~(ALIGNED_SIZE - 1);
			if (total < MIN_BLOCK_SIZE) total = MIN_BLOCK_SIZE;

			Header* h = free_list_find_remove(total);
			if (h) {
				uint64_t block_length = block_size(*h);
				uint64_t left_over_size = block_length - total;
				if (left_over_size >= MIN_BLOCK_SIZE) {
					Header* leftOverHeader = reinterpret_cast<Header*>(reinterpret_cast<char*>(h) + total);
					*h = static_cast<uint64_t>(total) | ALLOC_BIT;
					*leftOverHeader = left_over_size;
					free_list_push(leftOverHeader);
					g_split_count++;
				}
				else {
					*h = block_size(*h) | ALLOC_BIT;
				}
				return payload_of(h);
			}
			void* mem = sbrk(static_cast<intptr_t>(total));
			if (mem == (void*)-1) return nullptr;
			Header* header = reinterpret_cast<Header*>(mem);
			*header = total | ALLOC_BIT;
			g_brk_count++;

			return reinterpret_cast<char*>(mem) + HEADER_SIZE;
		}

		void free(void* mem) {
			if (!mem) return;
			Header* header = header_of(mem);
			assert(is_alloc(*header) && "double free or invalid pointer detected!");
			*header = block_size(*header);
			free_list_push(header);
		}

		void set_aligned(size_t size) {
			assert(size > 0 && size % 8 == 0 && "Keep alignment x8, to minimize SIGBUS error!");
			ALIGNED_SIZE = size;
		}

		void test_metadata(size_t size) {
			//test 1: metadata test
			void* memory = alloc(size);
			Header* header = header_of(memory);
			assert(is_alloc(*header) && "Bro not allocated!");
			assert(block_size(*header) >= size + HEADER_SIZE);

			free(memory);
			assert(!is_alloc(*header) && "bro free didnt occur");
			std::cout << "metadata test passed" << std::endl;

			// test 2: reuse freelist test
			void* reuse = alloc(size);
			assert(reuse == memory && "freelist reuse failed!");
			free(reuse);
			std::cout << "freelist test passed" << std::endl;

			// test3: split use(atleast pass 32bytes for it to pass)
			assert(size >= 32 && "split will fail! atleast give 32 byte");
			void* memConsume = alloc(size/2);
			uint64_t block1size = block_size(*header_of(memConsume));
			std::cout << "first block: " << block1size << ", brk count:" << g_brk_count << ", split count: " << g_split_count << std::endl;
			assert((block1size > size / 2) && g_split_count <= 1 && "size allocated is much bigger!");
			void* memSplit = alloc(size / 4);
			uint64_t splitsize = block_size(*header_of(memSplit));
			std::cout << "split block: " << splitsize << ", split count" << g_split_count << std::endl;
			assert((splitsize > size / 4) && g_brk_count <= 1 && "size allocated is much bigger!");

			free(memSplit);
			std::cout << "Successfully freed memSplit" << std::endl;
			free(memConsume);
			std::cout << "Successfully freed memConsume" << std::endl;

			freelist = nullptr;
			g_brk_count = 0;
			g_split_count = 0;
		}

		void stats_external_fragmentation(long& count, double& avg_size) {
			Header* h = freelist;
			uint64_t total = 0;
			while (h) {
				count++;
				total += block_size(*h);
				h = free_next_payload(h);
			}

			avg_size = total / count;
		}

		uint64_t get_brk_count() {
			return g_brk_count;
		}

		uint64_t get_split_count() {
			return g_split_count;
		}
	}
}