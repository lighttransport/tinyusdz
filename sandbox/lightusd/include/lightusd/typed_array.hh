// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// TypedArray<T> - Vector-like container with chunked storage support
// Buffer<N> - Raw byte storage with Small Buffer Optimization
//
// Designed to reduce peak memory usage in WASM environments where
// the linear heap makes large contiguous allocations problematic.

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <iterator>
#include <new>

namespace lightusd {
namespace v1 {

// Forward declarations
template<size_t SboSize>
class Buffer;

template<typename T, size_t SboSize>
class TypedArray;

// =============================================================================
// Buffer<N> - Raw byte storage with Small Buffer Optimization
// =============================================================================
//
// Storage modes:
// - Inline: Small allocations stored on stack (SBO)
// - Contiguous: Single heap allocation (like std::vector)
// - Chunked: Multiple smaller allocations to avoid large linear memory blocks
//
template<size_t SboSize = 16>
class Buffer {
public:
    static constexpr size_t kDefaultChunkSize = 64 * 1024;  // 64KB chunks
    static constexpr size_t kSboSize = SboSize;

    enum class StorageMode {
        Inline,      // Stored in inline buffer (SBO)
        Contiguous,  // Single heap allocation
        Chunked      // Multiple chunk allocations
    };

    // -------------------------------------------------------------------------
    // Constructors / Destructor
    // -------------------------------------------------------------------------

    Buffer() noexcept : size_(0), capacity_(SboSize), chunk_size_(kDefaultChunkSize),
                        mode_(StorageMode::Inline), contiguous_data_(nullptr) {
        std::memset(inline_, 0, SboSize);
    }

    explicit Buffer(size_t size, StorageMode preferred_mode = StorageMode::Chunked)
        : size_(0), capacity_(0), chunk_size_(kDefaultChunkSize),
          mode_(StorageMode::Inline), contiguous_data_(nullptr) {
        std::memset(inline_, 0, SboSize);
        if (size > 0) {
            resize_internal(size, preferred_mode);
        }
    }

    Buffer(const Buffer& other)
        : size_(0), capacity_(0), chunk_size_(other.chunk_size_),
          mode_(StorageMode::Inline), contiguous_data_(nullptr) {
        std::memset(inline_, 0, SboSize);
        if (other.size_ > 0) {
            resize_internal(other.size_, other.mode_);
            copy_from(other);
        }
    }

    Buffer(Buffer&& other) noexcept
        : size_(other.size_), capacity_(other.capacity_),
          chunk_size_(other.chunk_size_), mode_(other.mode_),
          contiguous_data_(other.contiguous_data_),
          chunks_(std::move(other.chunks_)) {
        if (other.mode_ == StorageMode::Inline) {
            std::memcpy(inline_, other.inline_, SboSize);
        }
        // Reset other to empty inline state
        other.size_ = 0;
        other.capacity_ = SboSize;
        other.mode_ = StorageMode::Inline;
        other.contiguous_data_ = nullptr;
        std::memset(other.inline_, 0, SboSize);
    }

    ~Buffer() {
        free_storage();
    }

    // -------------------------------------------------------------------------
    // Assignment
    // -------------------------------------------------------------------------

    Buffer& operator=(const Buffer& other) {
        if (this != &other) {
            free_storage();
            mode_ = StorageMode::Inline;
            size_ = 0;
            capacity_ = SboSize;
            chunk_size_ = other.chunk_size_;
            contiguous_data_ = nullptr;

            if (other.size_ > 0) {
                resize_internal(other.size_, other.mode_);
                copy_from(other);
            }
        }
        return *this;
    }

    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            free_storage();

            size_ = other.size_;
            capacity_ = other.capacity_;
            chunk_size_ = other.chunk_size_;
            mode_ = other.mode_;
            contiguous_data_ = other.contiguous_data_;
            chunks_ = std::move(other.chunks_);

            if (other.mode_ == StorageMode::Inline) {
                std::memcpy(inline_, other.inline_, SboSize);
            }

            other.size_ = 0;
            other.capacity_ = SboSize;
            other.mode_ = StorageMode::Inline;
            other.contiguous_data_ = nullptr;
            std::memset(other.inline_, 0, SboSize);
        }
        return *this;
    }

    // -------------------------------------------------------------------------
    // Size Management
    // -------------------------------------------------------------------------

    void resize(size_t new_size) {
        resize(new_size, mode_ == StorageMode::Inline ? StorageMode::Chunked : mode_);
    }

    void resize(size_t new_size, StorageMode preferred_mode) {
        if (new_size <= capacity_) {
            size_ = new_size;
            return;
        }
        resize_internal(new_size, preferred_mode);
    }

    void reserve(size_t new_capacity) {
        reserve(new_capacity, mode_ == StorageMode::Inline ? StorageMode::Chunked : mode_);
    }

    void reserve(size_t new_capacity, StorageMode preferred_mode) {
        if (new_capacity <= capacity_) {
            return;
        }
        size_t old_size = size_;
        resize_internal(new_capacity, preferred_mode);
        size_ = old_size;
    }

    void clear() noexcept {
        size_ = 0;
    }

    void shrink_to_fit() {
        if (mode_ == StorageMode::Chunked && size_ < capacity_) {
            // Remove unused chunks
            size_t needed_chunks = (size_ + chunk_size_ - 1) / chunk_size_;
            if (needed_chunks == 0 && size_ <= SboSize) {
                // Can shrink back to inline
                Buffer temp;
                temp.resize(size_, StorageMode::Inline);
                for (size_t i = 0; i < size_; ++i) {
                    temp[i] = (*this)[i];
                }
                *this = std::move(temp);
            } else {
                while (chunks_.size() > needed_chunks) {
                    delete[] chunks_.back();
                    chunks_.pop_back();
                }
                capacity_ = chunks_.size() * chunk_size_;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Element Access
    // -------------------------------------------------------------------------

    uint8_t& operator[](size_t index) {
        return const_cast<uint8_t&>(static_cast<const Buffer*>(this)->operator[](index));
    }

    const uint8_t& operator[](size_t index) const {
        switch (mode_) {
            case StorageMode::Inline:
                return inline_[index];
            case StorageMode::Contiguous:
                return contiguous_data_[index];
            case StorageMode::Chunked: {
                size_t chunk_idx = index / chunk_size_;
                size_t offset = index % chunk_size_;
                return chunks_[chunk_idx][offset];
            }
        }
        // Should never reach here
        return inline_[0];
    }

    uint8_t& at(size_t index) {
        if (index >= size_) {
            throw std::out_of_range("Buffer::at: index out of range");
        }
        return (*this)[index];
    }

    const uint8_t& at(size_t index) const {
        if (index >= size_) {
            throw std::out_of_range("Buffer::at: index out of range");
        }
        return (*this)[index];
    }

    // Get contiguous data pointer (only valid for Inline/Contiguous modes)
    // Returns nullptr for Chunked mode
    uint8_t* data() noexcept {
        switch (mode_) {
            case StorageMode::Inline:
                return inline_;
            case StorageMode::Contiguous:
                return contiguous_data_;
            case StorageMode::Chunked:
                return nullptr;
        }
        return nullptr;
    }

    const uint8_t* data() const noexcept {
        switch (mode_) {
            case StorageMode::Inline:
                return inline_;
            case StorageMode::Contiguous:
                return contiguous_data_;
            case StorageMode::Chunked:
                return nullptr;
        }
        return nullptr;
    }

    // Access a specific chunk (for Chunked mode)
    uint8_t* chunk_data(size_t chunk_index) noexcept {
        if (mode_ != StorageMode::Chunked || chunk_index >= chunks_.size()) {
            return nullptr;
        }
        return chunks_[chunk_index];
    }

    const uint8_t* chunk_data(size_t chunk_index) const noexcept {
        if (mode_ != StorageMode::Chunked || chunk_index >= chunks_.size()) {
            return nullptr;
        }
        return chunks_[chunk_index];
    }

    // -------------------------------------------------------------------------
    // Properties
    // -------------------------------------------------------------------------

    size_t size() const noexcept { return size_; }
    size_t capacity() const noexcept { return capacity_; }
    bool empty() const noexcept { return size_ == 0; }
    StorageMode mode() const noexcept { return mode_; }
    bool is_contiguous() const noexcept { return mode_ != StorageMode::Chunked; }

    // -------------------------------------------------------------------------
    // Chunk Configuration
    // -------------------------------------------------------------------------

    void set_chunk_size(size_t chunk_size) {
        if (chunk_size > 0 && mode_ == StorageMode::Inline) {
            chunk_size_ = chunk_size;
        }
    }

    size_t chunk_size() const noexcept { return chunk_size_; }

    size_t chunk_count() const noexcept {
        if (mode_ != StorageMode::Chunked) {
            return is_contiguous() ? 1 : 0;
        }
        return chunks_.size();
    }

    // -------------------------------------------------------------------------
    // Utility
    // -------------------------------------------------------------------------

    // Copy a range of bytes to external buffer
    void copy_to(uint8_t* dest, size_t offset, size_t count) const {
        if (offset + count > size_) {
            throw std::out_of_range("Buffer::copy_to: range out of bounds");
        }

        if (mode_ != StorageMode::Chunked) {
            std::memcpy(dest, data() + offset, count);
        } else {
            size_t remaining = count;
            size_t src_offset = offset;
            uint8_t* dst_ptr = dest;

            while (remaining > 0) {
                size_t chunk_idx = src_offset / chunk_size_;
                size_t in_chunk_offset = src_offset % chunk_size_;
                size_t bytes_in_chunk = std::min(remaining, chunk_size_ - in_chunk_offset);

                std::memcpy(dst_ptr, chunks_[chunk_idx] + in_chunk_offset, bytes_in_chunk);

                dst_ptr += bytes_in_chunk;
                src_offset += bytes_in_chunk;
                remaining -= bytes_in_chunk;
            }
        }
    }

    // Copy from external buffer
    void copy_from(const uint8_t* src, size_t offset, size_t count) {
        if (offset + count > size_) {
            throw std::out_of_range("Buffer::copy_from: range out of bounds");
        }

        if (mode_ != StorageMode::Chunked) {
            std::memcpy(data() + offset, src, count);
        } else {
            size_t remaining = count;
            size_t dst_offset = offset;
            const uint8_t* src_ptr = src;

            while (remaining > 0) {
                size_t chunk_idx = dst_offset / chunk_size_;
                size_t in_chunk_offset = dst_offset % chunk_size_;
                size_t bytes_in_chunk = std::min(remaining, chunk_size_ - in_chunk_offset);

                std::memcpy(chunks_[chunk_idx] + in_chunk_offset, src_ptr, bytes_in_chunk);

                src_ptr += bytes_in_chunk;
                dst_offset += bytes_in_chunk;
                remaining -= bytes_in_chunk;
            }
        }
    }

private:
    void free_storage() {
        if (mode_ == StorageMode::Contiguous && contiguous_data_) {
            delete[] contiguous_data_;
            contiguous_data_ = nullptr;
        } else if (mode_ == StorageMode::Chunked) {
            for (auto* chunk : chunks_) {
                delete[] chunk;
            }
            chunks_.clear();
        }
        mode_ = StorageMode::Inline;
        capacity_ = SboSize;
    }

    void resize_internal(size_t new_size, StorageMode preferred_mode) {
        // Determine actual mode based on size
        StorageMode new_mode;
        if (new_size <= SboSize) {
            new_mode = StorageMode::Inline;
        } else {
            new_mode = preferred_mode;
            if (new_mode == StorageMode::Inline) {
                new_mode = StorageMode::Chunked;  // Can't use inline for large sizes
            }
        }

        if (new_mode == mode_ && new_size <= capacity_) {
            size_ = new_size;
            return;
        }

        // Save old data
        size_t old_size = size_;
        StorageMode old_mode = mode_;
        uint8_t* old_contiguous = contiguous_data_;
        std::vector<uint8_t*> old_chunks = std::move(chunks_);
        uint8_t old_inline[SboSize];
        if (old_mode == StorageMode::Inline) {
            std::memcpy(old_inline, inline_, SboSize);
        }

        // Allocate new storage
        contiguous_data_ = nullptr;
        chunks_.clear();

        switch (new_mode) {
            case StorageMode::Inline:
                capacity_ = SboSize;
                break;

            case StorageMode::Contiguous:
                contiguous_data_ = new uint8_t[new_size];
                capacity_ = new_size;
                break;

            case StorageMode::Chunked: {
                size_t num_chunks = (new_size + chunk_size_ - 1) / chunk_size_;
                chunks_.reserve(num_chunks);
                for (size_t i = 0; i < num_chunks; ++i) {
                    chunks_.push_back(new uint8_t[chunk_size_]);
                }
                capacity_ = num_chunks * chunk_size_;
                break;
            }
        }

        mode_ = new_mode;
        size_ = new_size;

        // Copy old data
        size_t copy_size = std::min(old_size, new_size);
        if (copy_size > 0) {
            if (old_mode == StorageMode::Inline) {
                copy_from(old_inline, 0, copy_size);
            } else if (old_mode == StorageMode::Contiguous) {
                copy_from(old_contiguous, 0, copy_size);
            } else {
                // Copy from old chunks
                size_t remaining = copy_size;
                size_t src_offset = 0;

                while (remaining > 0) {
                    size_t chunk_idx = src_offset / chunk_size_;
                    size_t in_chunk_offset = src_offset % chunk_size_;
                    size_t bytes_in_chunk = std::min(remaining, chunk_size_ - in_chunk_offset);

                    // Copy to new storage
                    if (new_mode != StorageMode::Chunked) {
                        std::memcpy(data() + src_offset, old_chunks[chunk_idx] + in_chunk_offset, bytes_in_chunk);
                    } else {
                        size_t dst_chunk = src_offset / chunk_size_;
                        size_t dst_offset = src_offset % chunk_size_;
                        std::memcpy(chunks_[dst_chunk] + dst_offset, old_chunks[chunk_idx] + in_chunk_offset, bytes_in_chunk);
                    }

                    src_offset += bytes_in_chunk;
                    remaining -= bytes_in_chunk;
                }
            }
        }

        // Free old storage
        if (old_mode == StorageMode::Contiguous && old_contiguous) {
            delete[] old_contiguous;
        } else if (old_mode == StorageMode::Chunked) {
            for (auto* chunk : old_chunks) {
                delete[] chunk;
            }
        }
    }

    void copy_from(const Buffer& other) {
        size_t copy_size = std::min(size_, other.size_);
        for (size_t i = 0; i < copy_size; ++i) {
            (*this)[i] = other[i];
        }
    }

    // Member variables
    alignas(8) uint8_t inline_[SboSize];
    uint8_t* contiguous_data_;
    std::vector<uint8_t*> chunks_;
    size_t size_;
    size_t capacity_;
    size_t chunk_size_;
    StorageMode mode_;
};

// =============================================================================
// TypedArray<T> - Vector-like container with chunked storage
// =============================================================================
//
// Provides std::vector-like interface but uses Buffer<N> for storage,
// enabling chunked memory allocation to reduce peak memory in WASM.
//
template<typename T, size_t SboSize = 16>
class TypedArray {
public:
    using value_type = T;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;

    // Number of T elements that fit in SBO
    static constexpr size_t kSboElements = SboSize / sizeof(T) > 0 ? SboSize / sizeof(T) : 1;
    static constexpr size_t kActualSboSize = kSboElements * sizeof(T);

    // -------------------------------------------------------------------------
    // Iterator Classes
    // -------------------------------------------------------------------------

    class const_iterator;  // Forward declaration

    class iterator {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = T;
        using difference_type = ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        iterator() : array_(nullptr), index_(0) {}
        iterator(TypedArray* arr, size_t idx) : array_(arr), index_(idx) {}

        reference operator*() const { return (*array_)[index_]; }
        pointer operator->() const { return &(*array_)[index_]; }

        iterator& operator++() { ++index_; return *this; }
        iterator operator++(int) { iterator tmp = *this; ++index_; return tmp; }
        iterator& operator--() { --index_; return *this; }
        iterator operator--(int) { iterator tmp = *this; --index_; return tmp; }

        iterator& operator+=(difference_type n) { index_ += n; return *this; }
        iterator& operator-=(difference_type n) { index_ -= n; return *this; }

        iterator operator+(difference_type n) const { return iterator(array_, index_ + n); }
        iterator operator-(difference_type n) const { return iterator(array_, index_ - n); }
        difference_type operator-(const iterator& other) const {
            return static_cast<difference_type>(index_) - static_cast<difference_type>(other.index_);
        }

        reference operator[](difference_type n) const { return (*array_)[index_ + n]; }

        bool operator==(const iterator& other) const { return index_ == other.index_; }
        bool operator!=(const iterator& other) const { return index_ != other.index_; }
        bool operator<(const iterator& other) const { return index_ < other.index_; }
        bool operator<=(const iterator& other) const { return index_ <= other.index_; }
        bool operator>(const iterator& other) const { return index_ > other.index_; }
        bool operator>=(const iterator& other) const { return index_ >= other.index_; }

    private:
        friend class const_iterator;
        TypedArray* array_;
        size_t index_;
    };

    class const_iterator {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = T;
        using difference_type = ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        const_iterator() : array_(nullptr), index_(0) {}
        const_iterator(const TypedArray* arr, size_t idx) : array_(arr), index_(idx) {}
        const_iterator(const iterator& it) : array_(it.array_), index_(it.index_) {}

        reference operator*() const { return (*array_)[index_]; }
        pointer operator->() const { return &(*array_)[index_]; }

        const_iterator& operator++() { ++index_; return *this; }
        const_iterator operator++(int) { const_iterator tmp = *this; ++index_; return tmp; }
        const_iterator& operator--() { --index_; return *this; }
        const_iterator operator--(int) { const_iterator tmp = *this; --index_; return tmp; }

        const_iterator& operator+=(difference_type n) { index_ += n; return *this; }
        const_iterator& operator-=(difference_type n) { index_ -= n; return *this; }

        const_iterator operator+(difference_type n) const { return const_iterator(array_, index_ + n); }
        const_iterator operator-(difference_type n) const { return const_iterator(array_, index_ - n); }
        difference_type operator-(const const_iterator& other) const {
            return static_cast<difference_type>(index_) - static_cast<difference_type>(other.index_);
        }

        reference operator[](difference_type n) const { return (*array_)[index_ + n]; }

        bool operator==(const const_iterator& other) const { return index_ == other.index_; }
        bool operator!=(const const_iterator& other) const { return index_ != other.index_; }
        bool operator<(const const_iterator& other) const { return index_ < other.index_; }
        bool operator<=(const const_iterator& other) const { return index_ <= other.index_; }
        bool operator>(const const_iterator& other) const { return index_ > other.index_; }
        bool operator>=(const const_iterator& other) const { return index_ >= other.index_; }

    private:
        const TypedArray* array_;
        size_t index_;

        friend class iterator;
    };

    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    // -------------------------------------------------------------------------
    // Constructors / Destructor
    // -------------------------------------------------------------------------

    TypedArray() : buffer_(), size_(0) {}

    explicit TypedArray(size_t count) : buffer_(), size_(0) {
        resize(count);
    }

    TypedArray(size_t count, const T& value) : buffer_(), size_(0) {
        resize(count, value);
    }

    TypedArray(std::initializer_list<T> init) : buffer_(), size_(0) {
        reserve(init.size());
        for (const auto& val : init) {
            push_back(val);
        }
    }

    template<typename InputIt,
             typename = typename std::enable_if<
                 std::is_base_of<std::input_iterator_tag,
                     typename std::iterator_traits<InputIt>::iterator_category>::value>::type>
    TypedArray(InputIt first, InputIt last) : buffer_(), size_(0) {
        for (; first != last; ++first) {
            push_back(*first);
        }
    }

    TypedArray(const TypedArray& other) : buffer_(), size_(0) {
        reserve(other.size_);
        for (size_t i = 0; i < other.size_; ++i) {
            push_back(other[i]);
        }
    }

    TypedArray(TypedArray&& other) noexcept
        : buffer_(std::move(other.buffer_)), size_(other.size_) {
        other.size_ = 0;
    }

    ~TypedArray() {
        clear();
    }

    // -------------------------------------------------------------------------
    // Assignment
    // -------------------------------------------------------------------------

    TypedArray& operator=(const TypedArray& other) {
        if (this != &other) {
            clear();
            reserve(other.size_);
            for (size_t i = 0; i < other.size_; ++i) {
                push_back(other[i]);
            }
        }
        return *this;
    }

    TypedArray& operator=(TypedArray&& other) noexcept {
        if (this != &other) {
            clear();
            buffer_ = std::move(other.buffer_);
            size_ = other.size_;
            other.size_ = 0;
        }
        return *this;
    }

    TypedArray& operator=(std::initializer_list<T> init) {
        clear();
        reserve(init.size());
        for (const auto& val : init) {
            push_back(val);
        }
        return *this;
    }

    // -------------------------------------------------------------------------
    // Element Access
    // -------------------------------------------------------------------------

    reference operator[](size_t index) {
        return *element_ptr(index);
    }

    const_reference operator[](size_t index) const {
        return *element_ptr(index);
    }

    reference at(size_t index) {
        if (index >= size_) {
            throw std::out_of_range("TypedArray::at: index out of range");
        }
        return (*this)[index];
    }

    const_reference at(size_t index) const {
        if (index >= size_) {
            throw std::out_of_range("TypedArray::at: index out of range");
        }
        return (*this)[index];
    }

    reference front() { return (*this)[0]; }
    const_reference front() const { return (*this)[0]; }
    reference back() { return (*this)[size_ - 1]; }
    const_reference back() const { return (*this)[size_ - 1]; }

    // Returns contiguous data pointer (nullptr if chunked)
    pointer data() noexcept {
        return reinterpret_cast<T*>(buffer_.data());
    }

    const_pointer data() const noexcept {
        return reinterpret_cast<const T*>(buffer_.data());
    }

    // -------------------------------------------------------------------------
    // Iterators
    // -------------------------------------------------------------------------

    iterator begin() noexcept { return iterator(this, 0); }
    iterator end() noexcept { return iterator(this, size_); }
    const_iterator begin() const noexcept { return const_iterator(this, 0); }
    const_iterator end() const noexcept { return const_iterator(this, size_); }
    const_iterator cbegin() const noexcept { return const_iterator(this, 0); }
    const_iterator cend() const noexcept { return const_iterator(this, size_); }

    reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
    reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
    const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }
    const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }

    // -------------------------------------------------------------------------
    // Capacity
    // -------------------------------------------------------------------------

    bool empty() const noexcept { return size_ == 0; }
    size_t size() const noexcept { return size_; }
    size_t max_size() const noexcept { return SIZE_MAX / sizeof(T); }

    size_t capacity() const noexcept {
        return buffer_.capacity() / sizeof(T);
    }

    void reserve(size_t new_cap) {
        if (new_cap > capacity()) {
            buffer_.reserve(new_cap * sizeof(T));
        }
    }

    void shrink_to_fit() {
        buffer_.shrink_to_fit();
    }

    // -------------------------------------------------------------------------
    // Modifiers
    // -------------------------------------------------------------------------

    void clear() noexcept {
        // Destroy all elements
        for (size_t i = 0; i < size_; ++i) {
            element_ptr(i)->~T();
        }
        size_ = 0;
        buffer_.clear();
    }

    void push_back(const T& value) {
        ensure_capacity(size_ + 1);
        new (element_ptr(size_)) T(value);
        ++size_;
    }

    void push_back(T&& value) {
        ensure_capacity(size_ + 1);
        new (element_ptr(size_)) T(std::move(value));
        ++size_;
    }

    template<typename... Args>
    reference emplace_back(Args&&... args) {
        ensure_capacity(size_ + 1);
        T* ptr = element_ptr(size_);
        new (ptr) T(std::forward<Args>(args)...);
        ++size_;
        return *ptr;
    }

    void pop_back() {
        if (size_ > 0) {
            --size_;
            element_ptr(size_)->~T();
        }
    }

    void resize(size_t count) {
        if (count < size_) {
            // Destroy excess elements
            for (size_t i = count; i < size_; ++i) {
                element_ptr(i)->~T();
            }
            size_ = count;
        } else if (count > size_) {
            ensure_capacity(count);
            // Default-construct new elements
            for (size_t i = size_; i < count; ++i) {
                new (element_ptr(i)) T();
            }
            size_ = count;
        }
    }

    void resize(size_t count, const T& value) {
        if (count < size_) {
            for (size_t i = count; i < size_; ++i) {
                element_ptr(i)->~T();
            }
            size_ = count;
        } else if (count > size_) {
            ensure_capacity(count);
            for (size_t i = size_; i < count; ++i) {
                new (element_ptr(i)) T(value);
            }
            size_ = count;
        }
    }

    void swap(TypedArray& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(size_, other.size_);
    }

    // Insert at position
    iterator insert(const_iterator pos, const T& value) {
        size_t index = pos - cbegin();
        ensure_capacity(size_ + 1);

        // Shift elements
        if (index < size_) {
            // Move construct the last element
            new (element_ptr(size_)) T(std::move((*this)[size_ - 1]));
            // Move assign backwards
            for (size_t i = size_ - 1; i > index; --i) {
                (*this)[i] = std::move((*this)[i - 1]);
            }
            (*this)[index] = value;
        } else {
            new (element_ptr(size_)) T(value);
        }
        ++size_;
        return iterator(this, index);
    }

    iterator insert(const_iterator pos, T&& value) {
        size_t index = pos - cbegin();
        ensure_capacity(size_ + 1);

        if (index < size_) {
            new (element_ptr(size_)) T(std::move((*this)[size_ - 1]));
            for (size_t i = size_ - 1; i > index; --i) {
                (*this)[i] = std::move((*this)[i - 1]);
            }
            (*this)[index] = std::move(value);
        } else {
            new (element_ptr(size_)) T(std::move(value));
        }
        ++size_;
        return iterator(this, index);
    }

    iterator erase(const_iterator pos) {
        size_t index = pos - cbegin();
        if (index >= size_) {
            return end();
        }

        // Move elements left
        for (size_t i = index; i < size_ - 1; ++i) {
            (*this)[i] = std::move((*this)[i + 1]);
        }
        // Destroy last element
        element_ptr(size_ - 1)->~T();
        --size_;

        return iterator(this, index);
    }

    iterator erase(const_iterator first, const_iterator last) {
        size_t start = first - cbegin();
        size_t end_idx = last - cbegin();

        if (start >= end_idx || start >= size_) {
            return iterator(this, start);
        }

        size_t count = end_idx - start;
        if (end_idx > size_) {
            end_idx = size_;
            count = end_idx - start;
        }

        // Move elements
        for (size_t i = start; i + count < size_; ++i) {
            (*this)[i] = std::move((*this)[i + count]);
        }
        // Destroy trailing elements
        for (size_t i = size_ - count; i < size_; ++i) {
            element_ptr(i)->~T();
        }
        size_ -= count;

        return iterator(this, start);
    }

    // -------------------------------------------------------------------------
    // Chunking Configuration
    // -------------------------------------------------------------------------

    void set_chunk_size(size_t bytes) {
        buffer_.set_chunk_size(bytes);
    }

    size_t chunk_size() const noexcept {
        return buffer_.chunk_size();
    }

    bool is_contiguous() const noexcept {
        return buffer_.is_contiguous();
    }

    size_t chunk_count() const noexcept {
        return buffer_.chunk_count();
    }

    typename Buffer<kActualSboSize>::StorageMode storage_mode() const noexcept {
        return buffer_.mode();
    }

    // -------------------------------------------------------------------------
    // Comparison
    // -------------------------------------------------------------------------

    bool operator==(const TypedArray& other) const {
        if (size_ != other.size_) return false;
        for (size_t i = 0; i < size_; ++i) {
            if (!((*this)[i] == other[i])) return false;
        }
        return true;
    }

    bool operator!=(const TypedArray& other) const {
        return !(*this == other);
    }

private:
    T* element_ptr(size_t index) {
        size_t byte_offset = index * sizeof(T);
        return reinterpret_cast<T*>(&buffer_[byte_offset]);
    }

    const T* element_ptr(size_t index) const {
        size_t byte_offset = index * sizeof(T);
        return reinterpret_cast<const T*>(&buffer_[byte_offset]);
    }

    void ensure_capacity(size_t required) {
        size_t required_bytes = required * sizeof(T);
        if (required_bytes > buffer_.capacity()) {
            // Grow by 1.5x or to required, whichever is larger
            size_t new_capacity = std::max(required_bytes, buffer_.capacity() * 3 / 2);
            if (new_capacity < 64) {
                new_capacity = 64;
            }
            buffer_.reserve(new_capacity);
        }
        if (buffer_.size() < required_bytes) {
            buffer_.resize(required_bytes);
        }
    }

    Buffer<kActualSboSize> buffer_;
    size_t size_;
};

// Free function swap
template<typename T, size_t SboSize>
void swap(TypedArray<T, SboSize>& a, TypedArray<T, SboSize>& b) noexcept {
    a.swap(b);
}

} // namespace v1
} // namespace lightusd
