// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Test for MemoryPool and TypedArray

#include <iostream>
#include <cstring>
#include <cassert>
#include <cmath>

#include "next/memory/memory-pool.hh"
#include "next/types/value.hh"

using namespace tinyusdz::next;

// Test basic memory pool allocation
void test_basic_allocation() {
  std::cout << "test_basic_allocation... ";

  MemoryPool pool;

  // Test simple allocations
  int* a = static_cast<int*>(pool.Allocate(sizeof(int)));
  assert(a != nullptr);
  *a = 42;
  assert(*a == 42);

  double* b = static_cast<double*>(pool.Allocate(sizeof(double)));
  assert(b != nullptr);
  *b = 3.14159;
  assert(std::abs(*b - 3.14159) < 0.0001);

  // Test aligned allocation
  void* aligned = pool.AllocateAligned(64, 64);
  assert(aligned != nullptr);
  assert(reinterpret_cast<uintptr_t>(aligned) % 64 == 0);

  auto stats = pool.GetStats();
  assert(stats.allocation_count >= 3);
  assert(stats.total_used > 0);

  std::cout << "PASSED" << std::endl;
}

// Test array allocation
void test_array_allocation() {
  std::cout << "test_array_allocation... ";

  MemoryPool pool;

  // Allocate array of floats
  float* floats = pool.AllocateArray<float>(100);
  assert(floats != nullptr);

  for (size_t i = 0; i < 100; ++i) {
    floats[i] = static_cast<float>(i) * 1.5f;
  }

  for (size_t i = 0; i < 100; ++i) {
    assert(std::abs(floats[i] - static_cast<float>(i) * 1.5f) < 0.001f);
  }

  // Allocate zeroed array
  int* zeroed = pool.AllocateArrayZeroed<int>(50);
  assert(zeroed != nullptr);
  for (size_t i = 0; i < 50; ++i) {
    assert(zeroed[i] == 0);
  }

  std::cout << "PASSED" << std::endl;
}

// Test string allocation
void test_string_allocation() {
  std::cout << "test_string_allocation... ";

  MemoryPool pool;

  const char* original = "Hello, TinyUSDZ!";
  char* copied = pool.AllocateString(original);
  assert(copied != nullptr);
  assert(std::strcmp(copied, original) == 0);

  // Test with length
  char* partial = pool.AllocateString(original, 5);
  assert(partial != nullptr);
  assert(std::strcmp(partial, "Hello") == 0);

  std::cout << "PASSED" << std::endl;
}

// Test large allocation (should go to heap)
void test_large_allocation() {
  std::cout << "test_large_allocation... ";

  MemoryPool pool(64 * 1024);  // 64KB tiles

  // Allocate more than half the tile size - should go to heap
  size_t large_size = 40 * 1024;  // 40KB > 32KB (half of tile)
  void* large = pool.Allocate(large_size);
  assert(large != nullptr);

  auto stats = pool.GetStats();
  assert(stats.large_alloc_count == 1);

  std::cout << "PASSED" << std::endl;
}

// Test pool reset
void test_pool_reset() {
  std::cout << "test_pool_reset... ";

  MemoryPool pool;

  // Make some allocations
  for (int i = 0; i < 100; ++i) {
    pool.Allocate(100);
  }

  auto stats_before = pool.GetStats();
  assert(stats_before.allocation_count == 100);
  assert(stats_before.tile_count > 0);

  // Reset - tiles should remain, allocations should be freed
  pool.Reset();

  auto stats_after = pool.GetStats();
  assert(stats_after.allocation_count == 0);
  assert(stats_after.total_used == 0);
  assert(stats_after.tile_count == stats_before.tile_count);  // Tiles retained

  std::cout << "PASSED" << std::endl;
}

// Test pool clear
void test_pool_clear() {
  std::cout << "test_pool_clear... ";

  MemoryPool pool;

  // Make some allocations
  for (int i = 0; i < 100; ++i) {
    pool.Allocate(100);
  }

  // Clear - everything should be freed
  pool.Clear();

  auto stats = pool.GetStats();
  assert(stats.tile_count == 0);
  assert(stats.total_allocated == 0);
  assert(stats.total_used == 0);

  std::cout << "PASSED" << std::endl;
}

// Test reserve
void test_pool_reserve() {
  std::cout << "test_pool_reserve... ";

  MemoryPool pool(64 * 1024);  // 64KB tiles

  // Reserve for 256KB of data
  pool.Reserve(256 * 1024);

  auto stats = pool.GetStats();
  assert(stats.tile_count >= 4);  // Should have at least 4 tiles

  std::cout << "PASSED" << std::endl;
}

// Test move semantics
void test_pool_move() {
  std::cout << "test_pool_move... ";

  MemoryPool pool1;
  pool1.Allocate(100);
  auto stats1 = pool1.GetStats();
  assert(stats1.allocation_count == 1);

  // Move construct
  MemoryPool pool2(std::move(pool1));
  auto stats2 = pool2.GetStats();
  assert(stats2.allocation_count == 1);

  // Original should be empty
  auto stats1_after = pool1.GetStats();
  assert(stats1_after.allocation_count == 0);

  // Move assign
  MemoryPool pool3;
  pool3 = std::move(pool2);
  auto stats3 = pool3.GetStats();
  assert(stats3.allocation_count == 1);

  std::cout << "PASSED" << std::endl;
}

// Test TypedArray basic operations
void test_typed_array_basic() {
  std::cout << "test_typed_array_basic... ";

  // Default construction
  TypedArray<float> empty;
  assert(empty.empty());
  assert(empty.size() == 0);
  assert(empty.data() == nullptr);

  // Construction with size (heap)
  TypedArray<float> arr(10);
  assert(!arr.empty());
  assert(arr.size() == 10);
  assert(arr.is_heap_owned());

  // Fill with data
  for (size_t i = 0; i < arr.size(); ++i) {
    arr[i] = static_cast<float>(i);
  }

  // Check values
  for (size_t i = 0; i < arr.size(); ++i) {
    assert(arr[i] == static_cast<float>(i));
  }

  std::cout << "PASSED" << std::endl;
}

// Test TypedArray with memory pool
void test_typed_array_pool() {
  std::cout << "test_typed_array_pool... ";

  MemoryPool pool;

  // Construction with size (pool)
  TypedArray<float> arr(100, &pool);
  assert(!arr.empty());
  assert(arr.size() == 100);
  assert(arr.is_pool_owned());

  // Fill with data
  for (size_t i = 0; i < arr.size(); ++i) {
    arr[i] = static_cast<float>(i) * 0.5f;
  }

  // Check values
  for (size_t i = 0; i < arr.size(); ++i) {
    assert(std::abs(arr[i] - static_cast<float>(i) * 0.5f) < 0.001f);
  }

  auto stats = pool.GetStats();
  assert(stats.total_used > 0);

  std::cout << "PASSED" << std::endl;
}

// Test TypedArray view
void test_typed_array_view() {
  std::cout << "test_typed_array_view... ";

  float data[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};

  // Create view (non-owning)
  auto view = TypedArray<float>::MakeView(data, 5);
  assert(!view.empty());
  assert(view.size() == 5);
  assert(view.is_view());
  assert(view.data() == data);

  // Check values through view
  for (size_t i = 0; i < view.size(); ++i) {
    assert(view[i] == data[i]);
  }

  // Modify original data - view should see changes
  data[2] = 99.0f;
  assert(view[2] == 99.0f);

  std::cout << "PASSED" << std::endl;
}

// Test TypedArray make_owned (convert view to owned copy)
void test_typed_array_make_owned() {
  std::cout << "test_typed_array_make_owned... ";

  float data[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};

  // Create view
  auto view = TypedArray<float>::MakeView(data, 5);
  assert(view.is_view());

  // Make owned copy
  bool converted = view.MakeOwned();
  assert(converted);
  assert(!view.is_view());
  assert(view.is_heap_owned());

  // Data should be independent now
  data[2] = 99.0f;
  assert(view[2] == 3.0f);  // Original value preserved

  // Make owned with pool
  float data2[] = {10.0f, 20.0f, 30.0f};
  auto view2 = TypedArray<float>::MakeView(data2, 3);

  MemoryPool pool;
  view2.MakeOwned(&pool);
  assert(view2.is_pool_owned());

  std::cout << "PASSED" << std::endl;
}

// Test TypedArray move semantics
void test_typed_array_move() {
  std::cout << "test_typed_array_move... ";

  TypedArray<int> arr1(10);
  for (size_t i = 0; i < arr1.size(); ++i) {
    arr1[i] = static_cast<int>(i * 2);
  }

  // Move construct
  TypedArray<int> arr2(std::move(arr1));
  assert(arr1.empty());
  assert(arr2.size() == 10);
  for (size_t i = 0; i < arr2.size(); ++i) {
    assert(arr2[i] == static_cast<int>(i * 2));
  }

  // Move assign
  TypedArray<int> arr3;
  arr3 = std::move(arr2);
  assert(arr2.empty());
  assert(arr3.size() == 10);

  std::cout << "PASSED" << std::endl;
}

// Test TypedArray copy semantics
void test_typed_array_copy() {
  std::cout << "test_typed_array_copy... ";

  TypedArray<double> arr1(5);
  for (size_t i = 0; i < arr1.size(); ++i) {
    arr1[i] = static_cast<double>(i) * 1.1;
  }

  // Copy construct (always goes to heap)
  TypedArray<double> arr2(arr1);
  assert(arr2.size() == arr1.size());
  assert(arr2.is_heap_owned());
  for (size_t i = 0; i < arr2.size(); ++i) {
    assert(arr2[i] == arr1[i]);
  }

  // Modify original - copy should be independent
  arr1[0] = 999.0;
  assert(arr2[0] != 999.0);

  // Copy assign
  TypedArray<double> arr3;
  arr3 = arr1;
  assert(arr3.size() == arr1.size());

  std::cout << "PASSED" << std::endl;
}

// Test TypedArray resize
void test_typed_array_resize() {
  std::cout << "test_typed_array_resize... ";

  TypedArray<int> arr(5);
  for (size_t i = 0; i < arr.size(); ++i) {
    arr[i] = static_cast<int>(i);
  }

  // Resize to larger (should copy existing data)
  arr.Resize(10);
  assert(arr.size() == 10);
  for (size_t i = 0; i < 5; ++i) {
    assert(arr[i] == static_cast<int>(i));
  }

  // Resize to smaller
  arr.Resize(3);
  assert(arr.size() == 3);
  for (size_t i = 0; i < 3; ++i) {
    assert(arr[i] == static_cast<int>(i));
  }

  // Resize to zero (should clear)
  arr.Resize(0);
  assert(arr.empty());

  std::cout << "PASSED" << std::endl;
}

// Test TypedArray assign
void test_typed_array_assign() {
  std::cout << "test_typed_array_assign... ";

  float data[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};

  TypedArray<float> arr;
  arr.Assign(data, 5);
  assert(arr.size() == 5);
  for (size_t i = 0; i < arr.size(); ++i) {
    assert(arr[i] == data[i]);
  }

  // Assign from vector
  std::vector<float> vec = {10.0f, 20.0f, 30.0f};
  arr.Assign(vec);
  assert(arr.size() == 3);
  for (size_t i = 0; i < arr.size(); ++i) {
    assert(arr[i] == vec[i]);
  }

  std::cout << "PASSED" << std::endl;
}

// Test TypedArray iterators
void test_typed_array_iterators() {
  std::cout << "test_typed_array_iterators... ";

  TypedArray<int> arr(5);
  for (size_t i = 0; i < arr.size(); ++i) {
    arr[i] = static_cast<int>(i);
  }

  // Range-based for loop (uses begin/end)
  int sum = 0;
  for (int val : arr) {
    sum += val;
  }
  assert(sum == 0 + 1 + 2 + 3 + 4);

  // Manual iterator usage
  auto it = arr.begin();
  assert(*it == 0);
  ++it;
  assert(*it == 1);

  assert(arr.front() == 0);
  assert(arr.back() == 4);

  std::cout << "PASSED" << std::endl;
}

// Test MemoryPoolRef (shared ownership)
void test_memory_pool_ref() {
  std::cout << "test_memory_pool_ref... ";

  MemoryPoolRef ref1 = MemoryPoolRef::Create(64 * 1024);
  assert(ref1.IsValid());
  assert(ref1.use_count() == 1);

  // Allocate through ref
  void* ptr = ref1->Allocate(100);
  assert(ptr != nullptr);

  // Share the pool
  MemoryPoolRef ref2 = ref1;
  assert(ref1.use_count() == 2);
  assert(ref2.use_count() == 2);

  // Both refs point to same pool
  auto stats1 = ref1->GetStats();
  auto stats2 = ref2->GetStats();
  assert(stats1.allocation_count == stats2.allocation_count);

  // Allocate through second ref
  ref2->Allocate(200);

  // First ref should see the allocation
  stats1 = ref1->GetStats();
  assert(stats1.allocation_count == 2);

  std::cout << "PASSED" << std::endl;
}

// Test common type aliases
void test_type_aliases() {
  std::cout << "test_type_aliases... ";

  // FloatArray
  FloatArray floats(10);
  assert(floats.size() == 10);

  // DoubleArray
  DoubleArray doubles(5);
  assert(doubles.size() == 5);

  // Int32Array
  Int32Array ints(20);
  assert(ints.size() == 20);

  // UInt32Array
  UInt32Array uints(15);
  assert(uints.size() == 15);

  // UInt8Array (for byte buffers)
  UInt8Array bytes(100);
  assert(bytes.size() == 100);

  std::cout << "PASSED" << std::endl;
}

// Test Value pool-allocated arrays
void test_value_pool_arrays() {
  std::cout << "test_value_pool_arrays... ";

  MemoryPool pool;

  // Create pool-allocated float array
  float float_data[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
  Value float_val = Value::MakeFloatArrayPooled(float_data, 5, &pool);

  assert(float_val.is_array());
  assert(float_val.is_pool_owned());
  assert(!float_val.is_view());
  assert(float_val.array_size() == 5);

  // Test array view accessor
  FloatArrayView fview = float_val.float_array_view();
  assert(fview.size() == 5);
  for (size_t i = 0; i < 5; ++i) {
    assert(fview[i] == float_data[i]);
  }

  // Create pool-allocated int array
  int32_t int_data[] = {10, 20, 30, 40};
  Value int_val = Value::MakeIntArrayPooled(int_data, 4, &pool);

  assert(int_val.is_array());
  assert(int_val.is_pool_owned());
  assert(int_val.array_size() == 4);

  Int32ArrayView iview = int_val.int_array_view();
  assert(iview.size() == 4);
  for (size_t i = 0; i < 4; ++i) {
    assert(iview[i] == int_data[i]);
  }

  // Test make_owned() on pool-allocated array (converts to heap)
  Value copy_val = Value::MakeFloatArrayPooled(float_data, 5, &pool);
  assert(copy_val.is_pool_owned());

  bool converted = copy_val.make_owned();
  assert(converted);
  assert(!copy_val.is_pool_owned());
  assert(!copy_val.is_view());

  // Should be accessible via as_float_array() now (heap-owned)
  const std::vector<float>* vec_ptr = copy_val.as_float_array();
  assert(vec_ptr != nullptr);
  assert(vec_ptr->size() == 5);

  // Test copy of pool-owned value creates heap copy
  Value original = Value::MakeFloatArrayPooled(float_data, 5, &pool);
  Value heap_copy(original);  // Copy should create heap-owned copy
  assert(!heap_copy.is_pool_owned());  // Copy is heap-owned, not pool-owned

  std::cout << "PASSED" << std::endl;
}

// Test Value pool arrays with move semantics
void test_value_pool_array_move() {
  std::cout << "test_value_pool_array_move... ";

  MemoryPool pool;

  float data[] = {1.0f, 2.0f, 3.0f};
  Value v1 = Value::MakeFloatArrayPooled(data, 3, &pool);
  assert(v1.is_pool_owned());

  // Move should transfer pool ownership
  Value v2(std::move(v1));
  assert(v2.is_pool_owned());
  assert(v1.is_empty());  // v1 should be empty after move

  FloatArrayView view = v2.float_array_view();
  assert(view.size() == 3);
  assert(view[0] == 1.0f);
  assert(view[1] == 2.0f);
  assert(view[2] == 3.0f);

  std::cout << "PASSED" << std::endl;
}

// Performance test - allocate many small objects
void test_performance_small_allocs() {
  std::cout << "test_performance_small_allocs... ";

  MemoryPool pool;
  const size_t count = 10000;

  for (size_t i = 0; i < count; ++i) {
    void* ptr = pool.Allocate(16);
    assert(ptr != nullptr);
    (void)ptr;
  }

  auto stats = pool.GetStats();
  assert(stats.allocation_count == count);

  std::cout << "PASSED (" << stats.tile_count << " tiles, "
            << stats.total_allocated / 1024 << "KB allocated)" << std::endl;
}

// Test tile boundary handling
void test_tile_boundaries() {
  std::cout << "test_tile_boundaries... ";

  MemoryPool pool(1024);  // Small tiles for testing

  // Allocate to fill first tile
  void* ptrs[64];
  for (int i = 0; i < 64; ++i) {
    ptrs[i] = pool.Allocate(15);  // 15 bytes + alignment
    assert(ptrs[i] != nullptr);
  }

  auto stats = pool.GetStats();
  assert(stats.tile_count > 1);  // Should have spilled to multiple tiles

  // Verify all allocations are valid and don't overlap
  for (int i = 0; i < 64; ++i) {
    std::memset(ptrs[i], i, 15);
  }
  for (int i = 0; i < 64; ++i) {
    unsigned char* p = static_cast<unsigned char*>(ptrs[i]);
    for (int j = 0; j < 15; ++j) {
      assert(p[j] == static_cast<unsigned char>(i));
    }
  }

  std::cout << "PASSED" << std::endl;
}

int main() {
  std::cout << "=== Memory Pool Tests ===" << std::endl;

  // Memory pool tests
  test_basic_allocation();
  test_array_allocation();
  test_string_allocation();
  test_large_allocation();
  test_pool_reset();
  test_pool_clear();
  test_pool_reserve();
  test_pool_move();

  std::cout << std::endl << "=== TypedArray Tests ===" << std::endl;

  // TypedArray tests
  test_typed_array_basic();
  test_typed_array_pool();
  test_typed_array_view();
  test_typed_array_make_owned();
  test_typed_array_move();
  test_typed_array_copy();
  test_typed_array_resize();
  test_typed_array_assign();
  test_typed_array_iterators();

  std::cout << std::endl << "=== Shared Pool Tests ===" << std::endl;

  test_memory_pool_ref();

  std::cout << std::endl << "=== Type Alias Tests ===" << std::endl;

  test_type_aliases();

  std::cout << std::endl << "=== Value Pool Array Tests ===" << std::endl;

  test_value_pool_arrays();
  test_value_pool_array_move();

  std::cout << std::endl << "=== Performance Tests ===" << std::endl;

  test_performance_small_allocs();
  test_tile_boundaries();

  std::cout << std::endl << "All memory pool tests PASSED!" << std::endl;
  return 0;
}
