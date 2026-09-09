#include "../include/Xalloc.h"
#include<iostream>

namespace xalloc {
	namespace monotonicBrk {
		using Header = uint64_t;
		size_t ALIGNED_SIZE = 16; //can change according to your need for eliminating SIGBUS, SIGSEV error etc.
		size_t HEADER_SIZE = ALIGNED_SIZE;
		constexpr size_t FOOTER_SIZE = sizeof(Header);
		constexpr uint64_t ALLOC_BIT = 0x1;
		constexpr uint64_t SIZE_MASK = ~ALLOC_BIT;
		size_t MIN_BLOCK_SIZE = HEADER_SIZE + FOOTER_SIZE + sizeof(void*);
		uint64_t g_split_count = 0;
		uint64_t g_brk_count = 0;
		void* g_brk_start = nullptr;
		void* g_brk_end = nullptr;
		uint64_t g_coalesce_count = 0;
		bool developer_inteligence = false;
		// class size
		constexpr int num_classes = 10;
		constexpr size_t class_base_size = 32;

		constexpr std::array<size_t, num_classes> make_class_size() {
			std::array<size_t, num_classes> classes;
			for (int i = 0; i < num_classes; i++) {
				classes[i] = class_base_size * (1<<i);
			}
			return std::move(classes);
		}
		constexpr std::array<size_t, num_classes> class_size = make_class_size();
		std::array<Header*, num_classes> freelists{};

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

		int class_index(size_t size) {
			for (int i = 0; i < num_classes - 1; i++) {
				if (size <= class_size[i]) return i;
			}
			return num_classes - 1;
		}

		void free_list_push(Header* header) {
			free_next_payload(header) = freelists[class_index(block_size(*header))];
			freelists[class_index(block_size(*header))] = header;
			return;
		}

		void free_list_remove(Header* header) {
			int c = class_index(block_size(*header));
			Header** link = &freelists[c];
			Header* curr = freelists[c];
			while (curr) {
				if (curr == header) {
					*link = free_next_payload(curr);
					return;
				}
				link = &free_next_payload(curr);
				curr = free_next_payload(curr);
			}
		}

		Header* pop_from_any_class(int c) {
			Header* freelist = freelists[c];
			if (freelist) {
				freelists[c] = free_next_payload(freelist);
			}
			return freelist;
		}

		Header* find_fit_in_class(int c, size_t size) {
			Header** link = &freelists[c];
			Header* curr = freelists[c];

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

			int start_class = class_index(total);
			Header* h = find_fit_in_class(start_class, total);
			if (!h) {
				for (int i = start_class + 1; i < num_classes && !h; i++) {
					if (i == num_classes - 1) {
						h = find_fit_in_class(i, total);
					}
					else {
						h = pop_from_any_class(i);
					}
				}
			}

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

			if (base > static_cast<char*>(g_brk_start)) {  // check BEFORE reading anything
				uint64_t prev_size = block_size(*reinterpret_cast<Header*>(base - FOOTER_SIZE));
				Header* prev_header = reinterpret_cast<Header*>(base - prev_size);
				if (!is_alloc(*prev_header)) {
					free_list_remove(prev_header);
					base = reinterpret_cast<char*>(prev_header);
					size += prev_size;
					g_coalesce_count++;
				}
			}


			Header* merge = reinterpret_cast<Header*>(base);
			write_block(merge, size, false);
			free_list_push(merge);
		}

		void set_aligned(size_t size) {
			assert(size > 0 && (size & (size - 1)) == 0 && "alignment must be a power of two");
			assert(g_brk_start == nullptr && "set_aligned must be called before any allocation");
			ALIGNED_SIZE = size;
			HEADER_SIZE = ALIGNED_SIZE;
			MIN_BLOCK_SIZE = HEADER_SIZE + FOOTER_SIZE + sizeof(void*);
		}

		void reset_allocator() {
			if (g_brk_start && developer_inteligence) { // Reset the program break to the initial position(dont do this in production code, only for testing purposes)
				brk(g_brk_start);
			}
			freelists = {};
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
			//assert(size >= 32 && "split will fail! atleast give 32 byte");
			//void* memConsume = alloc(size/2);
			//uint64_t block1size = block_size(*header_of(memConsume));
			//std::cout << "first block: " << block1size << ", brk count:" << g_brk_count << ", split count: " << g_split_count << std::endl;
			//assert((block1size > size / 2) && g_split_count <= 1 && "size allocated is much bigger!");
			//void* memSplit = alloc(size / 4);
			//uint64_t splitsize = block_size(*header_of(memSplit));
			//std::cout << "split block: " << splitsize << ", split count" << g_split_count << std::endl;
			//assert((splitsize > size / 4) && g_brk_count <= 1 && "size allocated is much bigger!");

			//free(memSplit);
			//free(memConsume);

			reset_allocator();

			//test 4: coalesce test
			//void* block1 = alloc(size); // eg: size = 64, block1 = 82, freelist = []
			//Header* header1 = header_of(block1);
			//free(block1); // free block1, freelist = [82]
			//assert(g_split_count <= 0 && "split did occur1!");
			//assert(g_coalesce_count <= 0 && "coalesce did occur1!");

			//void* block2 = alloc(size + 24); // eg: size = 88, block2 = 104, freelist = [82]
			//Header* header2 = header_of(block2);
			//free(block2); // free block2, freelist = [186]
			//assert(g_split_count <= 0 && "split did occur2!");
			//assert(g_coalesce_count > 0 && "coalesce did not occur2!");

			//void* block3 = alloc(size + 32); // eg: size = 96, block3 = 112, freelist = [186]
			//Header* header3 = header_of(block3);
			//free(block3); // free block3, freelist = [74]
			//assert(g_split_count > 0 && "split did not occur3!");
			//assert(g_coalesce_count > 1 && "coalesce did occur3!");

			reset_allocator();
		}

		void stats_external_fragmentation(long& count, double& avg_size) {
			uint64_t total = 0;
			for (int i = 0; i < num_classes; i++) {
				Header* h = freelists[i];
				while (h) {
					count++;
					total += block_size(*h);
					h = free_next_payload(h);
				}
			}

			avg_size = (count > 0) ? static_cast<double>(total) / count : 0.0;
		}

		void stats_class_based(std::vector<long>& classes) {
			for (int i = 0; i < num_classes; i++) {
				Header* h = freelists[i];
				while (h) {
					classes[i]++;
					h = free_next_payload(h);
				}
			}
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

		int get_num_class() {
			return num_classes;
		}

		size_t get_base_class_size() {
			return class_base_size;
		}
	}
}