#pragma once

#ifndef _PSRAM_ALLOCATOR_H_
#define _PSRAM_ALLOCATOR_H_

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>
#include <cassert>

class PsramAllocator {
public:
    static PsramAllocator& instance();

    bool initialize();
    void* allocateBytes(size_t byteCount);
    bool freeBytes(void* ptr);

    size_t getTotalSize() const;
    size_t getManagedUsedSize() const;
    size_t getManagedRemainingSize() const;
    size_t getAllocationCount() const;
    size_t getActiveAllocationCount() const;
    bool isPsramAvailable() const;

    template <typename T>
    T* allocate(size_t count = 1) {
        static_assert(!std::is_void<T>::value, "Cannot allocate void type");
        if (count == 0) {
            return nullptr;
        }

        size_t byteCount = sizeof(T) * count;
        void* raw = allocateBytes(byteCount);
        if (!raw) {
            return nullptr;
        }

        T* ptr = static_cast<T*>(raw);
        if constexpr (!std::is_trivially_default_constructible<T>::value) {
            for (size_t i = 0; i < count; ++i) {
                new (&ptr[i]) T();
            }
        }

        return ptr;
    }

    template <typename T>
    bool deallocate(T* ptr, size_t count = 1) {
        if (!ptr) {
            return false;
        }

        if constexpr (!std::is_trivially_destructible<T>::value) {
            for (size_t i = 0; i < count; ++i) {
                ptr[i].~T();
            }
        }

        return freeBytes(static_cast<void*>(ptr));
    }

    template <typename T>
    class PsramArray {
    public:
        using value_type = T;
        using size_type = size_t;

        PsramArray() noexcept : allocator_(PsramAllocator::instance()), data_(nullptr), size_(0) {}
        explicit PsramArray(size_t size) : allocator_(PsramAllocator::instance()), data_(nullptr), size_(0) {
            reset(size);
        }

        ~PsramArray() {
            reset(0);
        }

        PsramArray(const PsramArray&) = delete;
        PsramArray& operator=(const PsramArray&) = delete;

        PsramArray(PsramArray&& other) noexcept
            : allocator_(other.allocator_), data_(other.data_), size_(other.size_) {
            other.data_ = nullptr;
            other.size_ = 0;
        }

        PsramArray& operator=(PsramArray&& other) noexcept {
            if (this != &other) {
                reset(0);
                data_ = other.data_;
                size_ = other.size_;
                other.data_ = nullptr;
                other.size_ = 0;
            }
            return *this;
        }

        void reset(size_t newSize) {
            if (data_) {
                allocator_.deallocate(data_, size_);
                data_ = nullptr;
                size_ = 0;
            }

            if (newSize == 0) {
                return;
            }

            data_ = allocator_.allocate<T>(newSize);
            if (data_) {
                size_ = newSize;
            }
        }

        T& operator[](size_t index) {
            assert(index < size_);
            return data_[index];
        }

        const T& operator[](size_t index) const {
            assert(index < size_);
            return data_[index];
        }

        size_t size() const noexcept {
            return size_;
        }

        T* data() noexcept {
            return data_;
        }

        const T* data() const noexcept {
            return data_;
        }

        T* release() noexcept {
            T* released = data_;
            data_ = nullptr;
            size_ = 0;
            return released;
        }

        bool empty() const noexcept {
            return data_ == nullptr;
        }

    private:
        PsramAllocator& allocator_;
        T* data_;
        size_t size_;
    };

private:
    PsramAllocator() = default;
    ~PsramAllocator() = default;
    PsramAllocator(const PsramAllocator&) = delete;
    PsramAllocator& operator=(const PsramAllocator&) = delete;

    struct AllocationHeader {
        size_t size;
    };

    void* allocateRaw(size_t byteCount);
    bool freeRaw(void* rawPtr);

    bool psram_available_ = false;
    size_t total_size_ = 0;
    size_t managed_used_size_ = 0;
    size_t managed_allocation_count_ = 0;
    size_t active_allocation_count_ = 0;
};

#endif // _PSRAM_ALLOCATOR_H_
