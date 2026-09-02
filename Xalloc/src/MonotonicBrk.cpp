#include "../include/Xalloc.h"
#include<iostream>

namespace xalloc {
	namespace monotonicBrk {
		using Header = uint64_t;
		constexpr size_t HEADER_SIZE = sizeof(Header);
		constexpr size_t FOOTER_SIZE = sizeof(Header);
		constexpr uint64_t ALLOC_BIT = 0x1;
		constexpr uint64_t SIZE_MASK = ~ALLOC_BIT;
		size_t ALIGNED_SIZE = 8; //can change according to your need for eliminating SIGBUS error etc.
		Header* freelist = nullptr;
		constexpr size_t MIN_BLOCK_SIZE = HEADER_SIZE + FOOTER_SIZE + sizeof(void*);
		uint64_t g_split_count = 0;
		uint64_t g_brk_count = 0;
		void* g_brk_start = nullptr;
		void* g_brk_end = nullptr;
		uint64_t g_coalesce_count = 0;

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

		void free_list_remove(Header* header) {
			Header** link = &freelist;
			Header* curr = freelist;
			while (curr) {
				if (curr == header) {
					*link = free_next_payload(curr);
					return;
				}
				link = &free_next_payload(curr);
				curr = free_next_payload(curr);
			}
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

		void write_block(Header* header, size_t size, bool allocated) {
			uint64_t header_value = size | (allocated ? ALLOC_BIT : 0);
			*header = header_value;
			Header* footer = reinterpret_cast<Header*>(reinterpret_cast<char*>(header) + size - FOOTER_SIZE);
			*footer = size;
		}

		void* alloc(size_t size) {
			if (size == 0) return nullptr;
			size_t total = size + HEADER_SIZE + FOOTER_SIZE;
			total = (total + (ALIGNED_SIZE - 1)) & ~(ALIGNED_SIZE - 1);
			if (total < MIN_BLOCK_SIZE) total = MIN_BLOCK_SIZE;

			Header* h = free_list_find_remove(total);
			if (h) {
				uint64_t block_length = block_size(*h);
				uint64_t left_over_size = block_length - total;
				if (left_over_size >= MIN_BLOCK_SIZE) {
					Header* leftOverHeader = reinterpret_cast<Header*>(reinterpret_cast<char*>(h) + total);
					write_block(h, total, true);
					write_block(leftOverHeader, left_over_size, false);
					free_list_push(leftOverHeader);
					g_split_count++;
				}
				else {
					write_block(h, block_length, true);
				}
				return payload_of(h);
			}
			void* mem = sbrk(static_cast<intptr_t>(total));
			if (mem == (void*)-1) return nullptr;
			if (!g_brk_start) g_brk_start = mem;
			g_brk_end = reinterpret_cast<char*>(mem) + total;
			Header* header = reinterpret_cast<Header*>(mem);
			g_brk_count++;
			write_block(header, total, true);
			return reinterpret_cast<char*>(mem) + HEADER_SIZE;
		}

		void free(void* mem) {
			if (!mem) return;
			Header* header = header_of(mem);
			assert(is_alloc(*header) && "double free or invalid pointer detected!");
			uint64_t size = block_size(*header);
			char* base = reinterpret_cast<char*>(header);


			Header* next_block_header = reinterpret_cast<Header*>(reinterpret_cast<char*>(header) + size);
			char* next_block_start = reinterpret_cast<char*>(header) + size;
			if (next_block_start < static_cast<char*>(g_brk_end) && !is_alloc(*next_block_header)) {
				uint64_t next_block_size = block_size(*next_block_header);
				free_list_remove(next_block_header);
				size += next_block_size;
				g_coalesce_count++;
			}

			uint64_t prev__block_size = block_size(*reinterpret_cast<Header*>(reinterpret_cast<char*>(header) - FOOTER_SIZE));
			Header* prev_block_header = reinterpret_cast<Header*>(reinterpret_cast<char*>(header) - prev__block_size);
			char* prev_block_start = reinterpret_cast<char*>(header) - prev__block_size;
			if (prev_block_start >= static_cast<char*>(g_brk_start) && !is_alloc(*prev_block_header)) {
				free_list_remove(prev_block_header);
				size += prev__block_size;
				base = prev_block_start;
				g_coalesce_count++;
			}


			Header* merge = reinterpret_cast<Header*>(base);
			write_block(merge, size, false);
			free_list_push(merge);
		}

		void set_aligned(size_t size) {
			assert(size > 0 && size % 8 == 0 && "Keep alignment x8, to minimize SIGBUS error!");
			ALIGNED_SIZE = size;
		}

		void reset_allocator() {
			freelist = nullptr;
			g_brk_count = 0;
			g_split_count = 0;
			g_coalesce_count = 0;
			g_brk_start = nullptr;
			g_brk_end = nullptr;
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

			// test3: split use(atleast pass 32bytes for it to pass) + 8 for coalesce test
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
			free(memConsume);

			reset_allocator();

			//test 4: coalesce test
			void* block1 = alloc(size); // eg: size = 64, block1 = 82, freelist = []
			Header* header1 = header_of(block1);
			free(block1); // free block1, freelist = [82]
			assert(g_split_count <= 0 && "split did occur1!");
			assert(g_coalesce_count <= 0 && "coalesce did occur1!");

			void* block2 = alloc(size + 24); // eg: size = 88, block2 = 104, freelist = [82]
			Header* header2 = header_of(block2);
			free(block2); // free block2, freelist = [186]
			assert(g_split_count <= 0 && "split did occur2!");
			assert(g_coalesce_count > 0 && "coalesce did not occur2!");

			void* block3 = alloc(size + 32); // eg: size = 96, block3 = 112, freelist = [186]
			Header* header3 = header_of(block3);
			free(block3); // free block3, freelist = [74]
			assert(g_split_count > 0 && "split did not occur3!");
			assert(g_coalesce_count > 1 && "coalesce did occur3!");
		}

		void stats_external_fragmentation(long& count, double& avg_size) {
			Header* h = freelist;
			uint64_t total = 0;
			while (h) {
				count++;
				total += block_size(*h);
				h = free_next_payload(h);
			}

			avg_size = (count > 0) ? static_cast<double>(total) / count : 0.0;
		}

		uint64_t get_brk_count() {
			return g_brk_count;
		}

		uint64_t get_split_count() {
			return g_split_count;
		}

		uint64_t get_coalesce_count() {
			return g_coalesce_count;
		}
	}
}