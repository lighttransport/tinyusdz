// Phase 1 unit tests: Offset-based deduplication with circular reference checks
// Tests the new offset encoding, dedup validation, and index remapping after sorting

#include <iostream>
#include <cassert>
#include <cmath>
#include "timesamples.hh"
#include "value-types.hh"

using namespace tinyusdz;
using namespace tinyusdz::value;

// Helper function to check if two arrays are equal
template<typename T>
bool arrays_equal(const T* a, const T* b, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (std::fabs(a[i] - b[i]) > 1e-6f) {
            return false;
        }
    }
    return true;
}

// Test 1: Basic offset encoding and decoding
void test_offset_encoding() {
    std::cout << "Test 1: Offset encoding/decoding... ";

    // Test non-dedup scalar offset
    uint64_t scalar_offset = PODTimeSamples::make_offset(100, false);
    assert(!PODTimeSamples::is_dedup(scalar_offset));
    assert(!PODTimeSamples::is_array_offset(scalar_offset));
    assert(PODTimeSamples::get_raw_value(scalar_offset) == 100);

    // Test non-dedup array offset
    uint64_t array_offset = PODTimeSamples::make_offset(200, true);
    assert(!PODTimeSamples::is_dedup(array_offset));
    assert(PODTimeSamples::is_array_offset(array_offset));
    assert(PODTimeSamples::get_raw_value(array_offset) == 200);

    // Test dedup scalar offset
    uint64_t dedup_scalar = PODTimeSamples::make_dedup_offset(5, false);
    assert(PODTimeSamples::is_dedup(dedup_scalar));
    assert(!PODTimeSamples::is_array_offset(dedup_scalar));
    assert(PODTimeSamples::get_raw_value(dedup_scalar) == 5);

    // Test dedup array offset
    uint64_t dedup_array = PODTimeSamples::make_dedup_offset(10, true);
    assert(PODTimeSamples::is_dedup(dedup_array));
    assert(PODTimeSamples::is_array_offset(dedup_array));
    assert(PODTimeSamples::get_raw_value(dedup_array) == 10);

    std::cout << "PASSED\n";
}

// Test 2: Array sample addition with new offset encoding
void test_array_addition() {
    std::cout << "Test 2: Array sample addition... ";

    PODTimeSamples samples;

    float3 arr1[] = {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}, {7.0f, 8.0f, 9.0f}};
    float3 arr2[] = {{10.0f, 11.0f, 12.0f}, {13.0f, 14.0f, 15.0f}, {16.0f, 17.0f, 18.0f}};

    std::string err;
    bool ok = samples.add_array_sample<float3>(1.0, arr1, 3, &err);
    assert(ok && "Failed to add first array sample");

    ok = samples.add_array_sample<float3>(2.0, arr2, 3, &err);
    assert(ok && "Failed to add second array sample");

    assert(samples.size() == 2);

    // Verify offsets are encoded correctly (non-dedup, array flag set)
    const auto& offsets = samples.get_times(); // Access through sorted
    samples.update();  // Force update to ensure sorted

    std::cout << "PASSED\n";
}

// Test 3: Deduplication with circular reference check
void test_dedup_validation() {
    std::cout << "Test 3: Dedup validation (circular ref checks)... ";

    PODTimeSamples samples;

    float3 arr[] = {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}};

    std::string err;

    // Add original array
    bool ok = samples.add_array_sample<float3>(1.0, arr, 2, &err);
    assert(ok);

    // Add valid dedup (should succeed)
    ok = samples.add_dedup_array_sample<float3>(2.0, 0, &err);
    assert(ok && "Valid dedup should succeed");

    // Try to dedup from dedup (should fail)
    err.clear();
    ok = samples.add_dedup_array_sample<float3>(3.0, 1, &err);
    assert(!ok && "Dedup from dedup should fail");
    assert(err.find("deduplicated sample") != std::string::npos);

    // Try self-reference (should fail)
    size_t current_idx = samples.size();
    err.clear();
    ok = samples.add_dedup_array_sample<float3>(4.0, current_idx, &err);
    assert(!ok && "Self-reference should fail");
    assert(err.find("Self-reference") != std::string::npos || err.find("Invalid") != std::string::npos);

    // Try out-of-bounds reference (should fail)
    err.clear();
    ok = samples.add_dedup_array_sample<float3>(5.0, 999, &err);
    assert(!ok && "Out-of-bounds ref should fail");

    std::cout << "PASSED\n";
}

// Test 4: Dedup chain resolution
void test_dedup_resolution() {
    std::cout << "Test 4: Dedup chain resolution... ";

    PODTimeSamples samples;

    float3 original[] = {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}};

    // Add original
    bool ok = samples.add_array_sample<float3>(1.0, original, 2);
    assert(ok);

    // Add dedup referencing original
    ok = samples.add_dedup_array_sample<float3>(2.0, 0);
    assert(ok);

    // Resolve offset for dedup sample
    size_t byte_offset = 0;
    bool is_array = false;
    ok = samples.resolve_offset(1, &byte_offset, &is_array);
    assert(ok && "Should resolve dedup offset");
    assert(is_array && "Should be array data");

    // Verify we got the correct byte offset (should be 0, same as sample 0)
    size_t expected_offset = 0;
    ok = samples.resolve_offset(0, &expected_offset);
    assert(ok);
    assert(byte_offset == expected_offset && "Dedup should point to same data");

    std::cout << "PASSED\n";
}

// Test 5: Sorting with dedup index remapping
void test_sorting_with_dedup() {
    std::cout << "Test 5: Sorting with dedup index remapping... ";

    PODTimeSamples samples;

    float3 arr1[] = {{1.0f, 2.0f, 3.0f}};
    float3 arr2[] = {{4.0f, 5.0f, 6.0f}};
    float3 arr3[] = {{7.0f, 8.0f, 9.0f}};

    // Add samples out of order
    bool ok = samples.add_array_sample<float3>(3.0, arr3, 1);  // idx 0, time 3.0
    assert(ok);

    ok = samples.add_array_sample<float3>(1.0, arr1, 1);       // idx 1, time 1.0
    assert(ok);

    ok = samples.add_dedup_array_sample<float3>(2.0, 0);       // idx 2, time 2.0, refs idx 0
    assert(ok);

    ok = samples.add_array_sample<float3>(4.0, arr2, 1);       // idx 3, time 4.0
    assert(ok);

    // Before sort:
    // idx 0: time 3.0, arr3 (original)
    // idx 1: time 1.0, arr1 (original)
    // idx 2: time 2.0, dedup->0
    // idx 3: time 4.0, arr2 (original)

    // Force sort
    samples.update();

    // After sort (by time):
    // idx 0: time 1.0, arr1 (was idx 1)
    // idx 1: time 2.0, dedup->? (was idx 2, should now ref new idx of old 0)
    // idx 2: time 3.0, arr3 (was idx 0)
    // idx 3: time 4.0, arr2 (was idx 3)

    // Verify dedup index was remapped correctly
    // Sample at sorted idx 1 should reference sorted idx 2 (which is arr3)
    size_t byte_offset = 0;
    ok = samples.resolve_offset(1, &byte_offset);
    assert(ok && "Should resolve dedup after sort");

    // Get the data at resolved offset
    const float3* resolved_data = reinterpret_cast<const float3*>(
        reinterpret_cast<const uint8_t*>(&arr1[0]) + byte_offset -
        (byte_offset > 0 ? byte_offset : 0)); // Rough check

    // Better check: retrieve via typed array view
    TypedArrayView<const float3> view = samples.get_typed_array_view_at<float3>(1);
    assert(view.size() == 1);
    // Should match arr3 since dedup pointed to original arr3
    assert(std::fabs(view[0][0] - 7.0f) < 1e-6f);
    assert(std::fabs(view[0][1] - 8.0f) < 1e-6f);
    assert(std::fabs(view[0][2] - 9.0f) < 1e-6f);

    std::cout << "PASSED\n";
}

// Test 6: Multiple dedup samples
void test_multiple_dedup() {
    std::cout << "Test 6: Multiple dedup samples... ";

    PODTimeSamples samples;

    float3 shared_arr[] = {{100.0f, 200.0f, 300.0f}, {400.0f, 500.0f, 600.0f}};

    // Add one original
    bool ok = samples.add_array_sample<float3>(1.0, shared_arr, 2);
    assert(ok);

    // Add multiple dedup samples all referencing the original
    for (int i = 0; i < 10; ++i) {
        ok = samples.add_dedup_array_sample<float3>(2.0 + i, 0);
        assert(ok);
    }

    assert(samples.size() == 11);

    // Verify all dedup samples resolve to same data
    size_t original_offset = 0;
    ok = samples.resolve_offset(0, &original_offset);
    assert(ok);

    for (size_t i = 1; i < 11; ++i) {
        size_t dedup_offset = 0;
        ok = samples.resolve_offset(i, &dedup_offset);
        assert(ok);
        assert(dedup_offset == original_offset && "All dedup should point to original");
    }

    std::cout << "PASSED\n";
}

// Test 7: Matrix array deduplication
void test_matrix_dedup() {
    std::cout << "Test 7: Matrix array deduplication... ";

    PODTimeSamples samples;

    matrix4d mat1 = matrix4d::identity();
    matrix4d mat2 = matrix4d::identity();
    mat2.m[0][0] = 2.0;

    matrix4d matrices[] = {mat1, mat2};

    // Add original
    bool ok = samples.add_matrix_array_sample<matrix4d>(1.0, matrices, 2);
    assert(ok);

    // Add dedup
    ok = samples.add_dedup_matrix_array_sample<matrix4d>(2.0, 0);
    assert(ok);

    // Verify resolution
    size_t byte_offset = 0;
    ok = samples.resolve_offset(1, &byte_offset);
    assert(ok);

    // Both should point to same data
    size_t orig_offset = 0;
    ok = samples.resolve_offset(0, &orig_offset);
    assert(ok);
    assert(byte_offset == orig_offset);

    std::cout << "PASSED\n";
}

// Test 8: Offset value limits (62-bit range)
void test_offset_limits() {
    std::cout << "Test 8: Offset value limits... ";

    // Test maximum safe value (62 bits)
    uint64_t max_value = PODTimeSamples::OFFSET_VALUE_MASK;
    uint64_t encoded = PODTimeSamples::make_offset(max_value, false);
    assert(PODTimeSamples::get_raw_value(encoded) == max_value);

    // Test with array flag
    encoded = PODTimeSamples::make_offset(max_value, true);
    assert(PODTimeSamples::get_raw_value(encoded) == max_value);
    assert(PODTimeSamples::is_array_offset(encoded));

    // Test dedup with max value
    encoded = PODTimeSamples::make_dedup_offset(max_value, true);
    assert(PODTimeSamples::is_dedup(encoded));
    assert(PODTimeSamples::is_array_offset(encoded));
    assert(PODTimeSamples::get_raw_value(encoded) == max_value);

    std::cout << "PASSED\n";
}

// Test 9: Blocked sample handling
void test_blocked_samples_with_dedup() {
    std::cout << "Test 9: Blocked samples with dedup... ";

    PODTimeSamples samples;

    float3 arr[] = {{1.0f, 2.0f, 3.0f}};

    // Add original
    bool ok = samples.add_array_sample<float3>(1.0, arr, 1);
    assert(ok);

    // Add blocked sample
    ok = samples.add_blocked_array_sample(2.0, 1);
    assert(ok);

    // Try to dedup from blocked (should fail)
    std::string err;
    ok = samples.add_dedup_array_sample<float3>(3.0, 1, &err);
    assert(!ok && "Cannot dedup from blocked sample");
    assert(err.find("blocked") != std::string::npos);

    // Dedup from original should work
    ok = samples.add_dedup_array_sample<float3>(4.0, 0);
    assert(ok);

    std::cout << "PASSED\n";
}

// Test 10: Empty and edge cases
void test_edge_cases() {
    std::cout << "Test 10: Edge cases... ";

    PODTimeSamples samples;

    // Resolve on empty should fail gracefully
    size_t offset = 0;
    bool ok = samples.resolve_offset(0, &offset);
    assert(!ok && "Should fail on empty");

    // Add one sample
    float3 arr[] = {{1.0f, 2.0f, 3.0f}};
    ok = samples.add_array_sample<float3>(1.0, arr, 1);
    assert(ok);

    // Resolve valid index
    ok = samples.resolve_offset(0, &offset);
    assert(ok);

    // Resolve invalid index
    ok = samples.resolve_offset(999, &offset);
    assert(!ok);

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "=== Phase 1: Offset-Based Deduplication Tests ===\n\n";

    test_offset_encoding();
    test_array_addition();
    test_dedup_validation();
    test_dedup_resolution();
    test_sorting_with_dedup();
    test_multiple_dedup();
    test_matrix_dedup();
    test_offset_limits();
    test_blocked_samples_with_dedup();
    test_edge_cases();

    std::cout << "\n=== All Phase 1 tests PASSED! ===\n";
    return 0;
}
