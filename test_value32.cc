// Test program for new Value32 implementation

#include "src/value-types-handler.hh"
#include "src/value-types.hh"

#include <iostream>
#include <cassert>

using namespace tinyusdz;
using namespace tinyusdz::value;

void test_basic_types() {
  std::cout << "Testing basic types...\n";

  // Test int32_t
  {
    Value32 v(int32_t(42));
    assert(!v.is_empty());
    assert(v.type_id() == TYPE_ID_INT32);
    
    const int32_t* ptr = v.as<int32_t>();
    assert(ptr != nullptr);
    assert(*ptr == 42);
    std::cout << "  int32_t: OK (value=" << *ptr << ")\n";
  }
  
  // Test float
  {
    Value32 v(3.14f);
    assert(!v.is_empty());
    assert(v.type_id() == TYPE_ID_FLOAT);
    
    const float* ptr = v.as<float>();
    assert(ptr != nullptr);
    assert(*ptr == 3.14f);
    std::cout << "  float: OK (value=" << *ptr << ")\n";
  }
  
  // Test double
  {
    Value32 v(2.718);
    assert(!v.is_empty());
    assert(v.type_id() == TYPE_ID_DOUBLE);
    
    const double* ptr = v.as<double>();
    assert(ptr != nullptr);
    assert(*ptr == 2.718);
    std::cout << "  double: OK (value=" << *ptr << ")\n";
  }
  
  // Test bool
  {
    Value32 v(true);
    assert(!v.is_empty());
    assert(v.type_id() == TYPE_ID_BOOL);
    
    const bool* ptr = v.as<bool>();
    assert(ptr != nullptr);
    assert(*ptr == true);
    std::cout << "  bool: OK (value=" << (*ptr ? "true" : "false") << ")\n";
  }
}

void test_string() {
  std::cout << "Testing string (heap allocated)...\n";
  
  std::string test_str = "Hello, TinyUSDZ!";
  Value32 v(test_str);
  
  assert(!v.is_empty());
  assert(v.type_id() == TYPE_ID_STRING);
  
  const std::string* ptr = v.as<std::string>();
  assert(ptr != nullptr);
  assert(*ptr == test_str);
  std::cout << "  string: OK (value=\"" << *ptr << "\")\n";
}

void test_copy_move() {
  std::cout << "Testing copy and move...\n";
  
  // Test copy constructor
  {
    Value32 v1(int32_t(99));
    Value32 v2(v1);  // Copy
    
    assert(!v2.is_empty());
    assert(v2.type_id() == TYPE_ID_INT32);
    
    const int32_t* ptr1 = v1.as<int32_t>();
    const int32_t* ptr2 = v2.as<int32_t>();
    assert(ptr1 != nullptr && ptr2 != nullptr);
    assert(*ptr1 == 99 && *ptr2 == 99);
    std::cout << "  copy constructor: OK\n";
  }
  
  // Test move constructor
  {
    Value32 v1(int32_t(77));
    Value32 v2(std::move(v1));  // Move
    
    assert(v1.is_empty());  // v1 should be empty after move
    assert(!v2.is_empty());
    assert(v2.type_id() == TYPE_ID_INT32);
    
    const int32_t* ptr = v2.as<int32_t>();
    assert(ptr != nullptr);
    assert(*ptr == 77);
    std::cout << "  move constructor: OK\n";
  }
  
  // Test copy assignment
  {
    Value32 v1(int32_t(55));
    Value32 v2;
    v2 = v1;  // Copy assign
    
    assert(!v2.is_empty());
    const int32_t* ptr = v2.as<int32_t>();
    assert(ptr != nullptr);
    assert(*ptr == 55);
    std::cout << "  copy assignment: OK\n";
  }
}

void test_size() {
  std::cout << "Testing sizeof...\n";
  
  size_t size = sizeof(Value32);
  std::cout << "  sizeof(Value32) = " << size << " bytes\n";
  assert(size == 32);
  std::cout << "  size check: OK (exactly 32 bytes)\n";
}

int main() {
  std::cout << "=== Value32 Test Program ===\n\n";
  
  test_size();
  std::cout << "\n";
  
  test_basic_types();
  std::cout << "\n";
  
  test_string();
  std::cout << "\n";
  
  test_copy_move();
  std::cout << "\n";
  
  std::cout << "=== All tests passed! ===\n";
  return 0;
}
