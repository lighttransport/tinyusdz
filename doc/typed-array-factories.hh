// SPDX-License-Identifier: MIT
// Copyright 2025 - Present, Light Transport Entertainment Inc.
//
// Proposed factory functions for TypedArray and TypedArrayImpl
// Add these to the end of src/typed-array.hh

namespace tinyusdz {

// ============================================================================
// TypedArray Factory Functions (Smart Pointer Wrapper)
// ============================================================================

///
/// Create TypedArray for owned array (will be deleted by TypedArray)
/// Use this when TypedArray should manage the lifetime of the implementation.
///
/// Example:
///   auto* impl = new TypedArrayImpl<float>(100);
///   TypedArray<float> arr = MakeOwnedTypedArray(impl);
///   // arr will delete impl when destroyed
///
template<typename T>
TypedArray<T> MakeOwnedTypedArray(TypedArrayImpl<T>* ptr) {
    return TypedArray<T>(ptr, false);  // dedup_flag = false: will delete
}

///
/// Create TypedArray for deduplicated array (shared, won't be deleted)
/// Use this when the array is shared/cached and managed elsewhere.
///
/// Example:
///   // Array is stored in dedup cache
///   auto it = _dedup_float_array.find(value_rep);
///   TypedArray<float> arr = MakeDedupTypedArray(it->second.get());
///   // arr won't delete the cached array
///
template<typename T>
TypedArray<T> MakeDedupTypedArray(TypedArrayImpl<T>* ptr) {
    return TypedArray<T>(ptr, true);  // dedup_flag = true: won't delete
}

///
/// Create TypedArray for shared array (alias for MakeDedupTypedArray)
/// Use this when the array is shared among multiple owners.
///
template<typename T>
TypedArray<T> MakeSharedTypedArray(TypedArrayImpl<T>* ptr) {
    return TypedArray<T>(ptr, true);  // dedup_flag = true: won't delete
}

///
/// Create TypedArray for memory-mapped array (non-owning, won't be deleted)
/// Use this for arrays backed by mmap'd files or external memory.
///
/// Example:
///   float* mmap_data = static_cast<float*>(mmap_ptr);
///   auto* impl = new TypedArrayImpl<float>(mmap_data, count, true);
///   TypedArray<float> arr = MakeMmapTypedArray(impl);
///
template<typename T>
TypedArray<T> MakeMmapTypedArray(TypedArrayImpl<T>* ptr) {
    return TypedArray<T>(ptr, true);  // dedup_flag = true: won't delete
}

// ============================================================================
// TypedArrayImpl Factory Functions (Array Implementation)
// ============================================================================

///
/// Create TypedArrayImpl with owned copy of data
/// Copies the data into internal storage.
///
/// Example:
///   float data[] = {1.0f, 2.0f, 3.0f};
///   auto arr = MakeTypedArrayCopy(data, 3);
///
template<typename T>
TypedArrayImpl<T> MakeTypedArrayCopy(const T* data, size_t count) {
    return TypedArrayImpl<T>(data, count);  // Copies data
}

///
/// Create non-owning view over external memory
/// Does not copy data, just references it. Caller must ensure memory lifetime.
///
/// Example:
///   float external_buffer[1000];
///   auto view = MakeTypedArrayView(external_buffer, 1000);
///   // view doesn't own the data
///
template<typename T>
TypedArrayImpl<T> MakeTypedArrayView(T* data, size_t count) {
    return TypedArrayImpl<T>(data, count, true);  // is_view = true
}

///
/// Create non-owning view for memory-mapped data
/// Alias for MakeTypedArrayView with clearer intent for mmap use cases.
///
/// Example:
///   float* mmap_ptr = static_cast<float*>(mmap(fd, ...));
///   auto arr = MakeTypedArrayMmap(mmap_ptr, element_count);
///
template<typename T>
TypedArrayImpl<T> MakeTypedArrayMmap(T* data, size_t count) {
    return TypedArrayImpl<T>(data, count, true);  // is_view = true
}

///
/// Create empty TypedArrayImpl with specified capacity
/// Reserves memory without initializing elements.
///
/// Example:
///   auto arr = MakeTypedArrayReserved<double>(1000);
///   for (int i = 0; i < 500; ++i) {
///       arr.push_back(i * 1.5);
///   }
///
template<typename T>
TypedArrayImpl<T> MakeTypedArrayReserved(size_t capacity) {
    TypedArrayImpl<T> arr;
    arr.reserve(capacity);
    return arr;
}

// ============================================================================
// Combined Convenience Functions
// ============================================================================

///
/// Create owned TypedArray from data copy
/// Combines allocation, copy, and wrapping in one call.
///
/// Example:
///   float data[] = {1.0f, 2.0f, 3.0f};
///   TypedArray<float> arr = CreateOwnedTypedArray(data, 3);
///
template<typename T>
TypedArray<T> CreateOwnedTypedArray(const T* data, size_t count) {
    auto* impl = new TypedArrayImpl<T>(data, count);
    return MakeOwnedTypedArray(impl);
}

///
/// Create owned TypedArray with specified size (uninitialized)
/// Allocates array with given size, elements are uninitialized.
///
/// Example:
///   TypedArray<int> arr = CreateOwnedTypedArray<int>(100);
///   for (size_t i = 0; i < arr.size(); ++i) {
///       arr[i] = static_cast<int>(i);
///   }
///
template<typename T>
TypedArray<T> CreateOwnedTypedArray(size_t count) {
    auto* impl = new TypedArrayImpl<T>(count);
    return MakeOwnedTypedArray(impl);
}

///
/// Create owned TypedArray with specified size and default value
/// Allocates and initializes all elements with the given value.
///
/// Example:
///   TypedArray<float> arr = CreateOwnedTypedArray<float>(100, 1.0f);
///
template<typename T>
TypedArray<T> CreateOwnedTypedArray(size_t count, const T& value) {
    auto* impl = new TypedArrayImpl<T>(count, value);
    return MakeOwnedTypedArray(impl);
}

///
/// Create deduplicated TypedArray from existing implementation pointer
/// Use this when storing in deduplication cache.
///
/// Example:
///   TypedArrayImpl<int32_t>& cached = _dedup_int32_array[value_rep];
///   TypedArray<int32_t> arr = CreateDedupTypedArray(&cached);
///
template<typename T>
TypedArray<T> CreateDedupTypedArray(TypedArrayImpl<T>* ptr) {
    return MakeDedupTypedArray(ptr);
}

///
/// Create mmap TypedArray over external memory
/// Combines view creation and wrapping for mmap use cases.
///
/// Example:
///   float* mmap_data = static_cast<float*>(mmap_ptr);
///   TypedArray<float> arr = CreateMmapTypedArray(mmap_data, count);
///
template<typename T>
TypedArray<T> CreateMmapTypedArray(T* data, size_t count) {
    auto* impl = new TypedArrayImpl<T>(data, count, true);  // View mode
    return MakeMmapTypedArray(impl);
}

///
/// Deep copy an existing TypedArray
/// Creates a new independent copy with its own storage.
///
/// Example:
///   TypedArray<double> original = ...;
///   TypedArray<double> copy = DuplicateTypedArray(original);
///   // copy is completely independent
///
template<typename T>
TypedArray<T> DuplicateTypedArray(const TypedArray<T>& source) {
    if (!source || source.empty()) {
        return TypedArray<T>();
    }
    auto* impl = new TypedArrayImpl<T>(source.data(), source.size());
    return MakeOwnedTypedArray(impl);
}

///
/// Deep copy a TypedArrayImpl
/// Creates a new implementation with copied data.
///
/// Example:
///   TypedArrayImpl<float> original = ...;
///   TypedArrayImpl<float> copy = DuplicateTypedArrayImpl(original);
///
template<typename T>
TypedArrayImpl<T> DuplicateTypedArrayImpl(const TypedArrayImpl<T>& source) {
    if (source.empty()) {
        return TypedArrayImpl<T>();
    }
    return TypedArrayImpl<T>(source.data(), source.size());
}

// ============================================================================
// Shorter Named Aliases (Optional - use if preferred)
// ============================================================================

#ifdef TINYUSDZ_USE_SHORT_TYPED_ARRAY_NAMES

template<typename T>
TypedArray<T> OwnedArray(TypedArrayImpl<T>* ptr) {
    return MakeOwnedTypedArray(ptr);
}

template<typename T>
TypedArray<T> SharedArray(TypedArrayImpl<T>* ptr) {
    return MakeSharedTypedArray(ptr);
}

template<typename T>
TypedArray<T> MmapArray(TypedArrayImpl<T>* ptr) {
    return MakeMmapTypedArray(ptr);
}

template<typename T>
TypedArrayImpl<T> ArrayCopy(const T* data, size_t count) {
    return MakeTypedArrayCopy(data, count);
}

template<typename T>
TypedArrayImpl<T> ArrayView(T* data, size_t count) {
    return MakeTypedArrayView(data, count);
}

#endif // TINYUSDZ_USE_SHORT_TYPED_ARRAY_NAMES

} // namespace tinyusdz
