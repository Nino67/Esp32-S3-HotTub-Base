#include "psram_allocator_c.h"

#include <stdbool.h>
#include <stddef.h>

/* C++ wrapper prototypes exposed by psram_allocator.cpp */
#ifdef __cplusplus
extern "C" {
#endif

bool psram_allocator_cpp_init(void);
void* psram_allocator_cpp_malloc(size_t bytes);
bool psram_allocator_cpp_free(void* ptr);
size_t psram_allocator_cpp_get_total_size(void);
size_t psram_allocator_cpp_get_used_size(void);
size_t psram_allocator_cpp_get_remaining_size(void);
size_t psram_allocator_cpp_get_allocation_count(void);
size_t psram_allocator_cpp_get_active_allocation_count(void);

#ifdef __cplusplus
}
#endif

bool psram_allocator_init(void) {
    return psram_allocator_cpp_init();
}

void* psram_allocator_malloc(size_t bytes) {
    return psram_allocator_cpp_malloc(bytes);
}

bool psram_allocator_free(void* ptr) {
    return psram_allocator_cpp_free(ptr);
}

size_t psram_allocator_get_total_size(void) {
    return psram_allocator_cpp_get_total_size();
}

size_t psram_allocator_get_used_size(void) {
    return psram_allocator_cpp_get_used_size();
}

size_t psram_allocator_get_remaining_size(void) {
    return psram_allocator_cpp_get_remaining_size();
}

size_t psram_allocator_get_allocation_count(void) {
    return psram_allocator_cpp_get_allocation_count();
}

size_t psram_allocator_get_active_allocation_count(void) {
    return psram_allocator_cpp_get_active_allocation_count();
}
