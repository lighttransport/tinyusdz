// SPDX-License-Identifier: MIT
// Copyright 2025 - Present, Light Transport Entertainment Inc.
//
// Test TypedArray factory functions
//

#include <iostream>
#include <cstring>
#include <cassert>

#include "../../../src/typed-array.hh"

using namespace tinyusdz;

// Helper function to check test results
#define TEST(name) \
    std::cout << "Testing " << #name << "... "; \
    if (test_##name()) { \
        std::cout << "✓ PASS" << std::endl; \
        passed++; \
    } else { \
        std::cout << "✗ FAIL" << std::endl; \
        failed++; \
    }

int passed = 0;
int failed = 0;

// Test MakeOwnedTypedArray
bool test_MakeOwnedTypedArray() {
    auto* impl = new TypedArrayImpl<float>(10);
    for (size_t i = 0; i < 10; ++i) {
        (*impl)[i] = static_cast<float>(i);
    }

    TypedArray<float> owned = MakeOwnedTypedArray(impl);

    // Check ownership flag
    if (owned.is_dedup()) return false;

    // Check data
    if (owned.size() != 10) return false;
    if (owned[0] != 0.0f) return false;
    if (owned[9] != 9.0f) return false;

    // owned will delete impl on destruction
    return true;
}

// Test MakeDedupTypedArray
bool test_MakeDedupTypedArray() {
    auto* impl = new TypedArrayImpl<int>(20);
    for (size_t i = 0; i < 20; ++i) {
        (*impl)[i] = static_cast<int>(i * 2);
    }

    TypedArray<int> dedup = MakeDedupTypedArray(impl);

    // Check dedup flag
    if (!dedup.is_dedup()) return false;

    // Check data
    if (dedup.size() != 20) return false;
    if (dedup[0] != 0) return false;
    if (dedup[10] != 20) return false;

    // Need to manually delete since dedup won't delete it
    delete impl;
    return true;
}

// Test MakeSharedTypedArray
bool test_MakeSharedTypedArray() {
    auto* impl = new TypedArrayImpl<double>(15);
    for (size_t i = 0; i < 15; ++i) {
        (*impl)[i] = static_cast<double>(i) * 1.5;
    }

    TypedArray<double> shared = MakeSharedTypedArray(impl);

    // Check dedup flag (shared is same as dedup)
    if (!shared.is_dedup()) return false;

    // Check data
    if (shared.size() != 15) return false;
    if (shared[0] != 0.0) return false;
    if (shared[10] != 15.0) return false;

    // Need to manually delete
    delete impl;
    return true;
}

// Test MakeMmapTypedArray
bool test_MakeMmapTypedArray() {
    float mmap_buffer[100];
    for (size_t i = 0; i < 100; ++i) {
        mmap_buffer[i] = static_cast<float>(i);
    }

    auto* impl = new TypedArrayImpl<float>(mmap_buffer, 100, true);
    TypedArray<float> mmap_arr = MakeMmapTypedArray(impl);

    // Check dedup flag
    if (!mmap_arr.is_dedup()) return false;

    // Check it's a view
    if (!impl->is_view()) return false;

    // Check data
    if (mmap_arr.size() != 100) return false;
    if (mmap_arr[0] != 0.0f) return false;
    if (mmap_arr[99] != 99.0f) return false;

    // Modify original buffer
    mmap_buffer[50] = 999.0f;
    if (mmap_arr[50] != 999.0f) return false;

    delete impl;
    return true;
}

// Test MakeTypedArrayCopy
bool test_MakeTypedArrayCopy() {
    int data[] = {1, 2, 3, 4, 5};
    auto copy = MakeTypedArrayCopy(data, 5);

    // Check data was copied
    if (copy.size() != 5) return false;
    for (size_t i = 0; i < 5; ++i) {
        if (copy[i] != data[i]) return false;
    }

    // Modify copy - should not affect original
    copy[0] = 999;
    if (data[0] != 1) return false;
    if (copy[0] != 999) return false;

    return true;
}

// Test MakeTypedArrayView
bool test_MakeTypedArrayView() {
    double buffer[50];
    for (size_t i = 0; i < 50; ++i) {
        buffer[i] = static_cast<double>(i) * 2.0;
    }

    auto view = MakeTypedArrayView(buffer, 50);

    // Check it's a view
    if (!view.is_view()) return false;

    // Check data
    if (view.size() != 50) return false;
    if (view[0] != 0.0) return false;
    if (view[25] != 50.0) return false;

    // Modify through view - should affect original
    view[10] = 999.0;
    if (buffer[10] != 999.0) return false;

    return true;
}

// Test MakeTypedArrayMmap
bool test_MakeTypedArrayMmap() {
    float mmap_buffer[75];
    for (size_t i = 0; i < 75; ++i) {
        mmap_buffer[i] = static_cast<float>(i);
    }

    auto mmap_view = MakeTypedArrayMmap(mmap_buffer, 75);

    // Check it's a view
    if (!mmap_view.is_view()) return false;

    // Check data
    if (mmap_view.size() != 75) return false;
    if (mmap_view[0] != 0.0f) return false;
    if (mmap_view[74] != 74.0f) return false;

    return true;
}

// Test MakeTypedArrayReserved
bool test_MakeTypedArrayReserved() {
    auto reserved = MakeTypedArrayReserved<float>(1000);

    // Check capacity
    if (reserved.capacity() < 1000) return false;

    // Check initially empty
    if (reserved.size() != 0) return false;

    // Add some elements
    for (int i = 0; i < 10; ++i) {
        reserved.push_back(static_cast<float>(i));
    }

    if (reserved.size() != 10) return false;
    if (reserved[5] != 5.0f) return false;

    return true;
}

// Test CreateOwnedTypedArray (from data)
bool test_CreateOwnedTypedArray_data() {
    float src[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    auto owned = CreateOwnedTypedArray(src, 5);

    // Check ownership
    if (owned.is_dedup()) return false;

    // Check data
    if (owned.size() != 5) return false;
    for (size_t i = 0; i < 5; ++i) {
        if (owned[i] != src[i]) return false;
    }

    // Modify owned - should not affect original
    owned[0] = 999.0f;
    if (src[0] != 1.0f) return false;

    return true;
}

// Test CreateOwnedTypedArray (with size)
bool test_CreateOwnedTypedArray_size() {
    auto owned = CreateOwnedTypedArray<int>(100);

    // Check ownership
    if (owned.is_dedup()) return false;

    // Check size
    if (owned.size() != 100) return false;

    // Initialize and check
    for (size_t i = 0; i < 100; ++i) {
        owned[i] = static_cast<int>(i);
    }
    if (owned[50] != 50) return false;

    return true;
}

// Test CreateOwnedTypedArray (with size and value)
bool test_CreateOwnedTypedArray_value() {
    auto owned = CreateOwnedTypedArray<double>(50, 3.14);

    // Check ownership
    if (owned.is_dedup()) return false;

    // Check size
    if (owned.size() != 50) return false;

    // Check all values
    for (size_t i = 0; i < 50; ++i) {
        if (owned[i] != 3.14) return false;
    }

    return true;
}

// Test CreateDedupTypedArray
bool test_CreateDedupTypedArray() {
    TypedArrayImpl<float> impl_stack(10);
    for (size_t i = 0; i < 10; ++i) {
        impl_stack[i] = static_cast<float>(i) * 0.5f;
    }

    auto dedup = CreateDedupTypedArray(&impl_stack);

    // Check dedup flag
    if (!dedup.is_dedup()) return false;

    // Check data
    if (dedup.size() != 10) return false;
    if (dedup[5] != 2.5f) return false;

    return true;
}

// Test CreateMmapTypedArray
bool test_CreateMmapTypedArray() {
    float mmap_buffer[200];
    for (size_t i = 0; i < 200; ++i) {
        mmap_buffer[i] = static_cast<float>(i);
    }

    auto mmap = CreateMmapTypedArray(mmap_buffer, 200);

    // Check dedup flag (mmap arrays are marked as dedup)
    if (!mmap.is_dedup()) return false;

    // Check data
    if (mmap.size() != 200) return false;
    if (mmap[100] != 100.0f) return false;

    // Modify original
    mmap_buffer[150] = 999.0f;
    if (mmap[150] != 999.0f) return false;

    return true;
}

// Test DuplicateTypedArray
bool test_DuplicateTypedArray() {
    float data[] = {1.0f, 2.0f, 3.0f};
    auto original = CreateOwnedTypedArray(data, 3);

    auto copy = DuplicateTypedArray(original);

    // Check ownership
    if (copy.is_dedup()) return false;

    // Check data copied
    if (copy.size() != 3) return false;
    for (size_t i = 0; i < 3; ++i) {
        if (copy[i] != original[i]) return false;
    }

    // Modify copy - should not affect original
    copy[0] = 999.0f;
    if (original[0] != 1.0f) return false;
    if (copy[0] != 999.0f) return false;

    // Check they're independent
    if (copy.data() == original.data()) return false;

    return true;
}

// Test DuplicateTypedArrayImpl
bool test_DuplicateTypedArrayImpl() {
    TypedArrayImpl<int> original(10);
    for (size_t i = 0; i < 10; ++i) {
        original[i] = static_cast<int>(i);
    }

    auto copy = DuplicateTypedArrayImpl(original);

    // Check data copied
    if (copy.size() != 10) return false;
    for (size_t i = 0; i < 10; ++i) {
        if (copy[i] != original[i]) return false;
    }

    // Modify copy - should not affect original
    copy[5] = 999;
    if (original[5] != 5) return false;
    if (copy[5] != 999) return false;

    // Check they're independent
    if (copy.data() == original.data()) return false;

    return true;
}

// Test deduplication use case
bool test_deduplication_pattern() {
    // Simulate deduplication cache
    TypedArrayImpl<float>* cached_impl = new TypedArrayImpl<float>(5);
    for (size_t i = 0; i < 5; ++i) {
        (*cached_impl)[i] = static_cast<float>(i);
    }

    // Store in cache as owned
    TypedArray<float> cache_entry = MakeOwnedTypedArray(cached_impl);

    // Return deduplicated reference (won't delete)
    TypedArray<float> dedup_ref = MakeDedupTypedArray(cached_impl);

    // Check both point to same data
    if (cache_entry.data() != dedup_ref.data()) return false;

    // Check dedup ref won't delete
    if (!dedup_ref.is_dedup()) return false;

    // Check cache entry will delete
    if (cache_entry.is_dedup()) return false;

    // cache_entry will delete cached_impl on destruction
    return true;
}

int main() {
    std::cout << "Testing TypedArray Factory Functions\n" << std::endl;

    // Test factory functions
    TEST(MakeOwnedTypedArray);
    TEST(MakeDedupTypedArray);
    TEST(MakeSharedTypedArray);
    TEST(MakeMmapTypedArray);
    TEST(MakeTypedArrayCopy);
    TEST(MakeTypedArrayView);
    TEST(MakeTypedArrayMmap);
    TEST(MakeTypedArrayReserved);
    TEST(CreateOwnedTypedArray_data);
    TEST(CreateOwnedTypedArray_size);
    TEST(CreateOwnedTypedArray_value);
    TEST(CreateDedupTypedArray);
    TEST(CreateMmapTypedArray);
    TEST(DuplicateTypedArray);
    TEST(DuplicateTypedArrayImpl);
    TEST(deduplication_pattern);

    std::cout << "\n----------------------------------------" << std::endl;
    std::cout << "Total: " << (passed + failed) << " tests" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;

    if (failed == 0) {
        std::cout << "\n✓ All tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "\n✗ Some tests failed!" << std::endl;
        return 1;
    }
}
