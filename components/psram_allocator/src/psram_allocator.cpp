#include "psram_allocator.h"

#ifdef CONFIG_SPIRAM
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_log.h"
#endif

static const char* TAG = "PsramAllocator";

PsramAllocator& PsramAllocator::instance() {
    static PsramAllocator allocator;
    return allocator;
}

bool PsramAllocator::initialize() {
#ifdef CONFIG_SPIRAM
    if (psram_available_) {
        return true;
    }

    if (!esp_psram_is_initialized()) {
        ESP_LOGW(TAG, "PSRAM is not initialized");
        psram_available_ = false;
        total_size_ = 0;
        return false;
    }

    total_size_ = esp_psram_get_size();
    psram_available_ = true;
    ESP_LOGI(TAG, "PSRAM initialized: %u bytes", static_cast<unsigned>(total_size_));
    return true;
#else
    return false;
#endif
}

void* PsramAllocator::allocateBytes(size_t byteCount) {
    if (byteCount == 0) {
        return nullptr;
    }

    if (!initialize()) {
        return nullptr;
    }

    return allocateRaw(byteCount);
}

bool PsramAllocator::freeBytes(void* ptr) {
    if (!ptr) {
        return false;
    }

    return freeRaw(ptr);
}

size_t PsramAllocator::getTotalSize() const {
    return total_size_;
}

size_t PsramAllocator::getManagedUsedSize() const {
    return managed_used_size_;
}

size_t PsramAllocator::getManagedRemainingSize() const {
    if (total_size_ > managed_used_size_) {
        return total_size_ - managed_used_size_;
    }
    return 0;
}

size_t PsramAllocator::getAllocationCount() const {
    return managed_allocation_count_;
}

size_t PsramAllocator::getActiveAllocationCount() const {
    return active_allocation_count_;
}

bool PsramAllocator::isPsramAvailable() const {
    return psram_available_;
}

void* PsramAllocator::allocateRaw(size_t byteCount) {
#ifdef CONFIG_SPIRAM
    const size_t totalBytes = byteCount + sizeof(AllocationHeader);
    void* raw = heap_caps_malloc(totalBytes, MALLOC_CAP_SPIRAM);
    if (!raw) {
        ESP_LOGW(TAG, "PSRAM allocation failed for %u bytes", static_cast<unsigned>(byteCount));
        return nullptr;
    }

    AllocationHeader* header = static_cast<AllocationHeader*>(raw);
    header->size = byteCount;
    void* userPtr = static_cast<void*>(header + 1);

    managed_used_size_ += byteCount;
    managed_allocation_count_ += 1;
    active_allocation_count_ += 1;

    return userPtr;
#else
    return nullptr;
#endif
}

bool PsramAllocator::freeRaw(void* rawPtr) {
#ifdef CONFIG_SPIRAM
    AllocationHeader* header = static_cast<AllocationHeader*>(rawPtr) - 1;
    const size_t byteCount = header->size;

    if (managed_used_size_ >= byteCount) {
        managed_used_size_ -= byteCount;
    } else {
        managed_used_size_ = 0;
    }

    if (active_allocation_count_ > 0) {
        active_allocation_count_ -= 1;
    }

    heap_caps_free(header);
    return true;
#else
    return false;
#endif
}




#include "psram_allocator_c.h"

#ifdef __cplusplus
extern "C" {
#endif

bool psram_allocator_cpp_init(void) {
    return PsramAllocator::instance().initialize();
}

void* psram_allocator_cpp_malloc(size_t bytes) {
    return PsramAllocator::instance().allocateBytes(bytes);
}

bool psram_allocator_cpp_free(void* ptr) {
    return PsramAllocator::instance().freeBytes(ptr);
}

size_t psram_allocator_cpp_get_total_size(void) {
    return PsramAllocator::instance().getTotalSize();
}

size_t psram_allocator_cpp_get_used_size(void) {
    return PsramAllocator::instance().getManagedUsedSize();
}

size_t psram_allocator_cpp_get_remaining_size(void) {
    return PsramAllocator::instance().getManagedRemainingSize();
}

size_t psram_allocator_cpp_get_allocation_count(void) {
    return PsramAllocator::instance().getAllocationCount();
}

size_t psram_allocator_cpp_get_active_allocation_count(void) {
    return PsramAllocator::instance().getActiveAllocationCount();
}

#ifdef __cplusplus
}
#endif
