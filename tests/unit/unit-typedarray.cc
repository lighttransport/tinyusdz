// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Light Transport Entertainment Inc.
//
// Unit tests for TypedArray with Buffer-based storage

#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <vector>

#include "unit-typedarray.h"
#include "typed-array.hh"
#include "value-types.hh"

using namespace tinyusdz;
using namespace tinyusdz::value;

//
// Basic TypedArray construction and operations
//

void typedarray_default_constructor_test(void) {
  TypedArray<float> arr;

  TEST_CHECK(arr.empty() == true);
  TEST_CHECK(arr.size() == 0);
  TEST_CHECK(arr.capacity() >= 0);
  TEST_CHECK(arr.is_contiguous() == true);
}

void typedarray_size_constructor_test(void) {
  TypedArray<int> arr(5);

  TEST_CHECK(arr.empty() == false);
  TEST_CHECK(arr.size() == 5);

  // Elements should be default-constructed (0 for int)
  for (size_t i = 0; i < arr.size(); ++i) {
    TEST_CHECK(arr[i] == 0);
  }
}

void typedarray_value_constructor_test(void) {
  TypedArray<double> arr(3, 42.5);

  TEST_CHECK(arr.size() == 3);

  for (size_t i = 0; i < arr.size(); ++i) {
    TEST_CHECK(arr[i] == 42.5);
  }
}

void typedarray_initializer_list_test(void) {
  TypedArray<int> arr = {1, 2, 3, 4, 5};

  TEST_CHECK(arr.size() == 5);
  TEST_CHECK(arr[0] == 1);
  TEST_CHECK(arr[1] == 2);
  TEST_CHECK(arr[2] == 3);
  TEST_CHECK(arr[3] == 4);
  TEST_CHECK(arr[4] == 5);
}

void typedarray_raw_data_constructor_test(void) {
  float data[] = {1.0f, 2.0f, 3.0f, 4.0f};
  TypedArray<float> arr(data, 4);

  TEST_CHECK(arr.size() == 4);
  TEST_CHECK(arr[0] == 1.0f);
  TEST_CHECK(arr[1] == 2.0f);
  TEST_CHECK(arr[2] == 3.0f);
  TEST_CHECK(arr[3] == 4.0f);
}

//
// Copy and move semantics
//

void typedarray_copy_constructor_test(void) {
  TypedArray<int> arr1 = {10, 20, 30};
  TypedArray<int> arr2(arr1);

  TEST_CHECK(arr2.size() == 3);
  TEST_CHECK(arr2[0] == 10);
  TEST_CHECK(arr2[1] == 20);
  TEST_CHECK(arr2[2] == 30);

  // Verify deep copy - modifying arr2 shouldn't affect arr1
  arr2[0] = 100;
  TEST_CHECK(arr1[0] == 10);
  TEST_CHECK(arr2[0] == 100);
}

void typedarray_copy_assignment_test(void) {
  TypedArray<int> arr1 = {10, 20, 30};
  TypedArray<int> arr2;

  arr2 = arr1;

  TEST_CHECK(arr2.size() == 3);
  TEST_CHECK(arr2[0] == 10);
  TEST_CHECK(arr2[1] == 20);
  TEST_CHECK(arr2[2] == 30);

  // Verify deep copy
  arr2[1] = 200;
  TEST_CHECK(arr1[1] == 20);
  TEST_CHECK(arr2[1] == 200);
}

void typedarray_move_constructor_test(void) {
  TypedArray<int> arr1 = {10, 20, 30};
  size_t original_size = arr1.size();

  TypedArray<int> arr2(std::move(arr1));

  TEST_CHECK(arr2.size() == original_size);
  TEST_CHECK(arr2[0] == 10);
  TEST_CHECK(arr2[1] == 20);
  TEST_CHECK(arr2[2] == 30);
}

void typedarray_move_assignment_test(void) {
  TypedArray<int> arr1 = {10, 20, 30};
  TypedArray<int> arr2;

  arr2 = std::move(arr1);

  TEST_CHECK(arr2.size() == 3);
  TEST_CHECK(arr2[0] == 10);
  TEST_CHECK(arr2[1] == 20);
  TEST_CHECK(arr2[2] == 30);
}

//
// Modifiers
//

void typedarray_push_back_test(void) {
  TypedArray<float> arr;

  arr.push_back(1.5f);
  arr.push_back(2.5f);
  arr.push_back(3.5f);

  TEST_CHECK(arr.size() == 3);
  TEST_CHECK(arr[0] == 1.5f);
  TEST_CHECK(arr[1] == 2.5f);
  TEST_CHECK(arr[2] == 3.5f);
}

void typedarray_pop_back_test(void) {
  TypedArray<int> arr = {10, 20, 30};

  arr.pop_back();
  TEST_CHECK(arr.size() == 2);
  TEST_CHECK(arr[0] == 10);
  TEST_CHECK(arr[1] == 20);

  arr.pop_back();
  TEST_CHECK(arr.size() == 1);
  TEST_CHECK(arr[0] == 10);

  arr.pop_back();
  TEST_CHECK(arr.size() == 0);
  TEST_CHECK(arr.empty() == true);
}

void typedarray_resize_test(void) {
  TypedArray<int> arr = {1, 2, 3};

  // Resize larger
  arr.resize(5);
  TEST_CHECK(arr.size() == 5);
  TEST_CHECK(arr[0] == 1);
  TEST_CHECK(arr[1] == 2);
  TEST_CHECK(arr[2] == 3);
  TEST_CHECK(arr[3] == 0);  // Default-constructed
  TEST_CHECK(arr[4] == 0);

  // Resize smaller
  arr.resize(2);
  TEST_CHECK(arr.size() == 2);
  TEST_CHECK(arr[0] == 1);
  TEST_CHECK(arr[1] == 2);
}

void typedarray_resize_with_value_test(void) {
  TypedArray<int> arr = {1, 2, 3};

  arr.resize(5, 99);
  TEST_CHECK(arr.size() == 5);
  TEST_CHECK(arr[0] == 1);
  TEST_CHECK(arr[1] == 2);
  TEST_CHECK(arr[2] == 3);
  TEST_CHECK(arr[3] == 99);
  TEST_CHECK(arr[4] == 99);
}

void typedarray_clear_test(void) {
  TypedArray<float> arr = {1.0f, 2.0f, 3.0f};

  arr.clear();
  TEST_CHECK(arr.empty() == true);
  TEST_CHECK(arr.size() == 0);
}

void typedarray_reserve_test(void) {
  TypedArray<int> arr;

  arr.reserve(100);
  TEST_CHECK(arr.capacity() >= 100);
  TEST_CHECK(arr.size() == 0);  // Reserve doesn't change size

  // Add elements - should not reallocate
  for (int i = 0; i < 50; ++i) {
    arr.push_back(i);
  }
  TEST_CHECK(arr.size() == 50);
  TEST_CHECK(arr.capacity() >= 100);
}

//
// Element access
//

void typedarray_element_access_test(void) {
  TypedArray<int> arr = {10, 20, 30, 40, 50};

  // operator[]
  TEST_CHECK(arr[0] == 10);
  TEST_CHECK(arr[2] == 30);
  TEST_CHECK(arr[4] == 50);

  // Modify
  arr[2] = 300;
  TEST_CHECK(arr[2] == 300);

  // front() and back()
  TEST_CHECK(arr.front() == 10);
  TEST_CHECK(arr.back() == 50);

  // data()
  int* ptr = arr.data();
  TEST_CHECK(ptr != nullptr);
  TEST_CHECK(ptr[0] == 10);
  TEST_CHECK(ptr[1] == 20);
}

void typedarray_const_element_access_test(void) {
  const TypedArray<int> arr = {10, 20, 30};

  TEST_CHECK(arr[0] == 10);
  TEST_CHECK(arr[1] == 20);
  TEST_CHECK(arr[2] == 30);

  TEST_CHECK(arr.front() == 10);
  TEST_CHECK(arr.back() == 30);

  const int* ptr = arr.data();
  TEST_CHECK(ptr != nullptr);
  TEST_CHECK(ptr[1] == 20);
}

//
// Iterators
//

void typedarray_iterator_test(void) {
  TypedArray<int> arr = {1, 2, 3, 4, 5};

  // begin() and end()
  int sum = 0;
  for (auto it = arr.begin(); it != arr.end(); ++it) {
    sum += *it;
  }
  TEST_CHECK(sum == 15);

  // Range-based for loop
  sum = 0;
  for (int val : arr) {
    sum += val;
  }
  TEST_CHECK(sum == 15);
}

void typedarray_const_iterator_test(void) {
  const TypedArray<int> arr = {10, 20, 30};

  int sum = 0;
  for (auto it = arr.begin(); it != arr.end(); ++it) {
    sum += *it;
  }
  TEST_CHECK(sum == 60);

  // cbegin() and cend()
  sum = 0;
  for (auto it = arr.cbegin(); it != arr.cend(); ++it) {
    sum += *it;
  }
  TEST_CHECK(sum == 60);
}

//
// value::Value integration tests
//

void typedarray_to_value_test(void) {
  TypedArray<float> arr = {1.0f, 2.0f, 3.0f};

  // Store TypedArray in Value
  Value v(arr);

  // Verify type
  TEST_CHECK(v.type_id() != 0);

  // Retrieve TypedArray from Value
  const TypedArray<float>* retrieved = v.as<TypedArray<float>>();
  TEST_CHECK(retrieved != nullptr);

  if (retrieved) {
    TEST_CHECK(retrieved->size() == 3);
    TEST_CHECK((*retrieved)[0] == 1.0f);
    TEST_CHECK((*retrieved)[1] == 2.0f);
    TEST_CHECK((*retrieved)[2] == 3.0f);
  }
}

void typedarray_value_copy_test(void) {
  // Test that multiple TypedArrays can be stored in separate Value objects
  {
    TypedArray<int> arr1 = {10, 20, 30};
    Value v1(arr1);

    const TypedArray<int>* p1 = v1.as<TypedArray<int>>();
    TEST_CHECK(p1 != nullptr);
    if (p1) {
      TEST_CHECK(p1->size() == 3);
    }
  }

  {
    TypedArray<int> arr2 = {40, 50, 60};
    Value v2(arr2);

    const TypedArray<int>* p2 = v2.as<TypedArray<int>>();
    TEST_CHECK(p2 != nullptr);
    if (p2) {
      TEST_CHECK(p2->size() == 3);
    }
  }
}

void typedarray_value_move_test(void) {
  // Test that TypedArray can be moved into Value
  TypedArray<double> arr = {1.5, 2.5, 3.5};
  Value v(std::move(arr));

  // Retrieve from Value
  const TypedArray<double>* p = v.as<TypedArray<double>>();
  TEST_CHECK(p != nullptr);

  if (p) {
    TEST_CHECK(p->size() == 3);
    TEST_CHECK((*p)[0] == 1.5);
    TEST_CHECK((*p)[1] == 2.5);
    TEST_CHECK((*p)[2] == 3.5);
  }
}

void typedarray_value_different_types_test(void) {
  // Test with various types
  {
    TypedArray<uint8_t> arr = {1, 2, 3};
    Value v(arr);
    const TypedArray<uint8_t>* p = v.as<TypedArray<uint8_t>>();
    TEST_CHECK(p != nullptr);
    if (p) TEST_CHECK(p->size() == 3);
  }

  {
    TypedArray<int32_t> arr = {100, 200, 300};
    Value v(arr);
    const TypedArray<int32_t>* p = v.as<TypedArray<int32_t>>();
    TEST_CHECK(p != nullptr);
    if (p) TEST_CHECK(p->size() == 3);
  }

  {
    TypedArray<double> arr = {1.1, 2.2, 3.3};
    Value v(arr);
    const TypedArray<double>* p = v.as<TypedArray<double>>();
    TEST_CHECK(p != nullptr);
    if (p) TEST_CHECK(p->size() == 3);
  }
}

void typedarray_value_type_mismatch_test(void) {
  TypedArray<float> arr = {1.0f, 2.0f};
  Value v(arr);

  // Try to retrieve as wrong type - should return nullptr
  const TypedArray<double>* p = v.as<TypedArray<double>>();
  TEST_CHECK(p == nullptr);

  const TypedArray<int>* p2 = v.as<TypedArray<int>>();
  TEST_CHECK(p2 == nullptr);
}

//
// TypedArray with USD value types
//

void typedarray_usd_types_test(void) {
  // Test with float2
  {
    TypedArray<float2> arr;
    arr.push_back(float2{1.0f, 2.0f});
    arr.push_back(float2{3.0f, 4.0f});

    TEST_CHECK(arr.size() == 2);
    TEST_CHECK(arr[0][0] == 1.0f);
    TEST_CHECK(arr[0][1] == 2.0f);
    TEST_CHECK(arr[1][0] == 3.0f);
    TEST_CHECK(arr[1][1] == 4.0f);

    // Store in Value
    Value v(arr);
    const TypedArray<float2>* p = v.as<TypedArray<float2>>();
    TEST_CHECK(p != nullptr);
    if (p) {
      TEST_CHECK(p->size() == 2);
    }
  }

  // Test with float3
  {
    TypedArray<float3> arr;
    arr.push_back(float3{1.0f, 2.0f, 3.0f});
    arr.push_back(float3{4.0f, 5.0f, 6.0f});

    TEST_CHECK(arr.size() == 2);
    TEST_CHECK(arr[0][0] == 1.0f);
    TEST_CHECK(arr[1][2] == 6.0f);
  }

  // Test with int2
  {
    TypedArray<int2> arr;
    arr.push_back(int2{10, 20});
    arr.push_back(int2{30, 40});

    TEST_CHECK(arr.size() == 2);
    TEST_CHECK(arr[0][0] == 10);
    TEST_CHECK(arr[1][1] == 40);
  }
}

//
// Buffer integration
//

void typedarray_buffer_access_test(void) {
  TypedArray<int> arr = {1, 2, 3, 4, 5};

  // Access underlying buffer
  const Buffer<16>& buf = arr.get_buffer();
  TEST_CHECK(buf.size() == 5 * sizeof(int));
  TEST_CHECK(!buf.empty());

  // Verify data is accessible through buffer
  const int* data_ptr = reinterpret_cast<const int*>(buf.data());
  TEST_CHECK(data_ptr[0] == 1);
  TEST_CHECK(data_ptr[4] == 5);
}

void typedarray_packed_value_test(void) {
  TypedArray<float> arr = {1.0f, 2.0f, 3.0f};

  // Get packed value (pointer as uint64_t)
  uint64_t packed = arr.get_packed_value();
  TEST_CHECK(packed != 0);

  // Convert back to pointer
  TypedArray<float>* ptr = reinterpret_cast<TypedArray<float>*>(packed);
  TEST_CHECK(ptr == &arr);

  // Verify data is accessible
  TEST_CHECK(ptr->size() == 3);
  TEST_CHECK((*ptr)[0] == 1.0f);
  TEST_CHECK((*ptr)[2] == 3.0f);
}

//
// Empty and edge cases
//

void typedarray_empty_operations_test(void) {
  TypedArray<int> arr;

  TEST_CHECK(arr.empty() == true);
  TEST_CHECK(arr.size() == 0);

  // Operations on empty array
  arr.clear();  // Should not crash
  TEST_CHECK(arr.empty() == true);

  arr.reserve(10);
  TEST_CHECK(arr.empty() == true);  // Still empty after reserve
  TEST_CHECK(arr.capacity() >= 10);
}

void typedarray_single_element_test(void) {
  TypedArray<float> arr;
  arr.push_back(42.0f);

  TEST_CHECK(arr.size() == 1);
  TEST_CHECK(arr.front() == 42.0f);
  TEST_CHECK(arr.back() == 42.0f);
  TEST_CHECK(arr[0] == 42.0f);

  arr.pop_back();
  TEST_CHECK(arr.empty() == true);
}

void typedarray_large_array_test(void) {
  TypedArray<int> arr;

  // Push many elements
  const size_t count = 10000;
  for (size_t i = 0; i < count; ++i) {
    arr.push_back(static_cast<int>(i));
  }

  TEST_CHECK(arr.size() == count);
  TEST_CHECK(arr[0] == 0);
  TEST_CHECK(arr[count - 1] == static_cast<int>(count - 1));

  // Verify all elements
  bool all_correct = true;
  for (size_t i = 0; i < count; ++i) {
    if (arr[i] != static_cast<int>(i)) {
      all_correct = false;
      break;
    }
  }
  TEST_CHECK(all_correct == true);
}

//
// TypedArrayView integration
//

void typedarray_view_test(void) {
  TypedArray<float> arr = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};

  // Create view from TypedArray
  TypedArrayView<float> view(arr);

  TEST_CHECK(view.size() == 5);
  TEST_CHECK(view[0] == 1.0f);
  TEST_CHECK(view[4] == 5.0f);

  // Modify through view
  view[2] = 30.0f;
  TEST_CHECK(arr[2] == 30.0f);
}

void typedarray_const_view_test(void) {
  const TypedArray<int> arr = {10, 20, 30};

  // Create const view
  TypedArrayView<const int> view(arr);

  TEST_CHECK(view.size() == 3);
  TEST_CHECK(view[0] == 10);
  TEST_CHECK(view[1] == 20);
  TEST_CHECK(view[2] == 30);
}

//
// Assignment operations
//

void typedarray_assign_test(void) {
  TypedArray<int> arr;

  arr.assign(5, 42);

  TEST_CHECK(arr.size() == 5);
  for (size_t i = 0; i < arr.size(); ++i) {
    TEST_CHECK(arr[i] == 42);
  }
}

void typedarray_swap_test(void) {
  TypedArray<int> arr1 = {1, 2, 3};
  TypedArray<int> arr2 = {10, 20, 30, 40};

  arr1.swap(arr2);

  TEST_CHECK(arr1.size() == 4);
  TEST_CHECK(arr2.size() == 3);

  TEST_CHECK(arr1[0] == 10);
  TEST_CHECK(arr1[3] == 40);

  TEST_CHECK(arr2[0] == 1);
  TEST_CHECK(arr2[2] == 3);
}

// Tests are registered in unit-main.cc
