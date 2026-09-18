#include "../include/Xalloc.h"

namespace xalloc {
	namespace arena {
		using Header = uint64_t;
		size_t ALIGNED_SIZE = 16;
		size_t HEADER_SIZE = ALIGNED_SIZE;
		size_t FOOTER_SIZE = 8;
		uint64_t ALLOC_BIT = 0x1;
		uint64_t SIZE_MASK = ~ALLOC_BIT;
		size_t MIN_BLOCK_SIZE = HEADER_SIZE + FOOTER_SIZE + sizeof(void*);
		uint64_t g_split_cnt = 0;
		void* g_heap_start = nullptr;
		void* g_heap_end = nullptr;
		uint64_t g_coalesce_cnt = 0;
		uint64_t g_sbrk_cnt = 0;
		Header* freelist = nullptr;

		void* payload_of(Header* header) {
			return reinterpret_cast<char*>(header) + HEADER_SIZE;
		}
		static Header*& next_block_of(Header* link) {
			return *reinterpret_cast<Header**>(payload_of(link));
		}
		uint64_t block_size(Header header) {
			return header & SIZE_MASK;
		}
		uint64_t aligned_of(size_t size) {
			return (size + (ALIGNED_SIZE - 1)) & ~(ALIGNED_SIZE - 1);
		}
		bool is_alloc(Header header) {
			return (header & ALLOC_BIT) != 0;
		}
		void write_block(Header* header, size_t size, bool alloc) {
			*header = size | (alloc ? ALLOC_BIT : 0);
			Header* footer = reinterpret_cast<Header*>(reinterpret_cast<char*>(header) + size - FOOTER_SIZE);
			*footer = size;
		}
		Header* header_of(void* mem) {
			return reinterpret_cast<Header*>(reinterpret_cast<char*>(mem) - HEADER_SIZE);
		}

		Header* find_and_remove(size_t size) {
			Header* curr = freelist;
			Header** link = &freelist;
			while (curr) {
				if (size == block_size(*curr)) {
					*link = next_block_of(curr);
					return curr;
				}
				curr = next_block_of(curr);
				link = &next_block_of(curr);
			}
			return nullptr;
		}

		void push_to_freelist(Header* header) {
			next_block_of(header) = freelist;
			freelist = header;
		}

		void free_list_remove(Header* header) {
			Header** link = &freelist;
			Header* curr = freelist;
			while (curr) {
				if (curr == header) {
					*link = next_block_of(curr);
					return;
				}
				link = &next_block_of(curr);
				curr = next_block_of(curr);
			}
		}

		void* alloc(size_t size) {
			if (size <= 0) return nullptr;
			size_t total = aligned_of(size + HEADER_SIZE + FOOTER_SIZE);
			if (total < MIN_BLOCK_SIZE) total = MIN_BLOCK_SIZE;
			Header* h = find_and_remove(size);
			if (h) {
				size_t leftover_size = block_size(*h) - total;
				if (leftover_size >= MIN_BLOCK_SIZE) {
					Header* free_split = reinterpret_cast<Header*>(reinterpret_cast<char*>(h) + total);
					write_block(h, total, true);
					write_block(free_split, leftover_size, false);
					push_to_freelist(free_split);
					g_split_cnt++;
				}
				else {
					write_block(h, size, true);
				}
				return payload_of(h);
			}
			void* mem = sbrk(static_cast<intptr_t>(size));
			if (mem == (void*)-1) return nullptr;
			g_sbrk_cnt++;
			h = reinterpret_cast<Header*>(mem);
			if (!g_heap_start) g_heap_start = mem;
			g_heap_end = reinterpret_cast<char*>(mem) + total;
			write_block(h, size, true);
			return payload_of(h);
		} 

		void free(void* mem) {
			if (!mem) return;
			Header* h = header_of(mem);
			assert(is_alloc(*h) && "Double free or invalid");
			size_t size = block_size(*h);
			char* base = reinterpret_cast<char*>(h);

			char* next_block_size = reinterpret_cast<char*>(base) + size;
			if (next_block_size < static_cast<char*>(g_heap_end)) {
				Header* next_h = reinterpret_cast<Header*>(next_block_size);
				if (!is_alloc(*next_h)) {
					size_t next_size = block_size(*next_h);
					size += next_size;
					free_list_remove(next_h);
					g_coalesce_cnt++;
				}
			}
			
			if (base > static_cast<char*>(g_heap_start)) {  // check BEFORE reading anything
				uint64_t prev_size = block_size(*reinterpret_cast<Header*>(base - FOOTER_SIZE));
				Header* prev_header = reinterpret_cast<Header*>(base - prev_size);
				if (!is_alloc(*prev_header)) {
					free_list_remove(prev_header);
					base = reinterpret_cast<char*>(prev_header);
					size += prev_size;
					g_coalesce_cnt++;
				}
			}

			Header* merge = reinterpret_cast<Header*>(base);
			write_block(merge, size, false);
			push_to_freelist(merge);
		}
	}
}