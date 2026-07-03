#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

bool psram_allocator_init(void);
void* psram_allocator_malloc(size_t bytes);
bool psram_allocator_free(void* ptr);

size_t psram_allocator_get_total_size(void);
size_t psram_allocator_get_used_size(void);
size_t psram_allocator_get_remaining_size(void);
size_t psram_allocator_get_allocation_count(void);
size_t psram_allocator_get_active_allocation_count(void);

#ifdef __cplusplus
}
#endif
