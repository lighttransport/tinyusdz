// Test efficient vector/matrix printing functions
#include "src/str-util.hh"
#include "src/value-types.hh"
#include <iostream>
#include <cassert>
#include <cstring>

using namespace tinyusdz;
using namespace tinyusdz::value;

void test_buffer_dtos() {
  std::cout << "Testing buffer-based dtos..." << std::endl;

  char buffer[DTOS_MAX_CHARS_DOUBLE];

  // Test float
  size_t len = dtos(1.0f, buffer);
  buffer[len] = '\0';
  assert(strcmp(buffer, "1") == 0);
  std::cout << "  dtos(1.0f) = \"" << buffer << "\" [OK]" << std::endl;

  // Test double
  len = dtos(3.14159, buffer);
  buffer[len] = '\0';
  std::cout << "  dtos(3.14159) = \"" << buffer << "\" [OK]" << std::endl;

  // Test negative
  len = dtos(-1.0, buffer);
  buffer[len] = '\0';
  assert(strcmp(buffer, "-1") == 0);
  std::cout << "  dtos(-1.0) = \"" << buffer << "\" [OK]" << std::endl;
}

void test_float_vectors() {
  std::cout << "\nTesting float vector printing..." << std::endl;

  char buffer[PRINT_FLOAT4_MAX_CHARS];

  // float2
  float2 v2 = {1.0f, 2.0f};
  size_t len = print_float2(v2, buffer);
  buffer[len] = '\0';
  std::cout << "  float2{1.0, 2.0} = \"" << buffer << "\"" << std::endl;
  assert(strcmp(buffer, "(1, 2)") == 0);

  // float3
  float3 v3 = {1.0f, 2.0f, 3.0f};
  len = print_float3(v3, buffer);
  buffer[len] = '\0';
  std::cout << "  float3{1.0, 2.0, 3.0} = \"" << buffer << "\"" << std::endl;
  assert(strcmp(buffer, "(1, 2, 3)") == 0);

  // float4
  float4 v4 = {1.0f, 2.0f, 3.0f, 4.0f};
  len = print_float4(v4, buffer);
  buffer[len] = '\0';
  std::cout << "  float4{1.0, 2.0, 3.0, 4.0} = \"" << buffer << "\"" << std::endl;
  assert(strcmp(buffer, "(1, 2, 3, 4)") == 0);
}

void test_double_vectors() {
  std::cout << "\nTesting double vector printing..." << std::endl;

  char buffer[PRINT_DOUBLE4_MAX_CHARS];

  // double2
  double2 v2 = {1.5, 2.5};
  size_t len = print_double2(v2, buffer);
  buffer[len] = '\0';
  std::cout << "  double2{1.5, 2.5} = \"" << buffer << "\"" << std::endl;
  assert(strcmp(buffer, "(1.5, 2.5)") == 0);

  // double3
  double3 v3 = {1.0, 2.0, 3.0};
  len = print_double3(v3, buffer);
  buffer[len] = '\0';
  std::cout << "  double3{1.0, 2.0, 3.0} = \"" << buffer << "\"" << std::endl;
  assert(strcmp(buffer, "(1, 2, 3)") == 0);

  // double4
  double4 v4 = {0.1, 0.2, 0.3, 0.4};
  len = print_double4(v4, buffer);
  buffer[len] = '\0';
  std::cout << "  double4{0.1, 0.2, 0.3, 0.4} = \"" << buffer << "\"" << std::endl;
  assert(strcmp(buffer, "(0.1, 0.2, 0.3, 0.4)") == 0);
}

void test_half_vectors() {
  std::cout << "\nTesting half vector printing..." << std::endl;
  std::cout << "  (Skipped - requires full value-types library)" << std::endl;

  // TODO: Enable when linking with full tinyusdz library
  /*
  char buffer[PRINT_HALF4_MAX_CHARS];

  // half2
  half2 v2 = {value::float_to_half_full(1.0f), value::float_to_half_full(2.0f)};
  size_t len = print_half2(v2, buffer);
  buffer[len] = '\0';
  std::cout << "  half2{1.0, 2.0} = \"" << buffer << "\"" << std::endl;
  assert(strcmp(buffer, "(1, 2)") == 0);

  // half3
  half3 v3 = {value::float_to_half_full(1.0f), value::float_to_half_full(2.0f), value::float_to_half_full(3.0f)};
  len = print_half3(v3, buffer);
  buffer[len] = '\0';
  std::cout << "  half3{1.0, 2.0, 3.0} = \"" << buffer << "\"" << std::endl;
  assert(strcmp(buffer, "(1, 2, 3)") == 0);

  // half4
  half4 v4 = {value::float_to_half_full(1.0f), value::float_to_half_full(2.0f), value::float_to_half_full(3.0f), value::float_to_half_full(4.0f)};
  len = print_half4(v4, buffer);
  buffer[len] = '\0';
  std::cout << "  half4{1.0, 2.0, 3.0, 4.0} = \"" << buffer << "\"" << std::endl;
  assert(strcmp(buffer, "(1, 2, 3, 4)") == 0);
  */
}

void test_matrices() {
  std::cout << "\nTesting matrix printing..." << std::endl;

  // matrix2d - identity
  {
    char buffer[PRINT_MATRIX2D_MAX_CHARS];
    matrix2d m;
    m.m[0][0] = 1.0; m.m[0][1] = 0.0;
    m.m[1][0] = 0.0; m.m[1][1] = 1.0;

    size_t len = print_matrix2d(m, buffer);
    buffer[len] = '\0';
    std::cout << "  matrix2d (identity) = \"" << buffer << "\"" << std::endl;
    assert(strcmp(buffer, "((1, 0), (0, 1))") == 0);
  }

  // matrix3d - identity
  {
    char buffer[PRINT_MATRIX3D_MAX_CHARS];
    matrix3d m;
    m.m[0][0] = 1.0; m.m[0][1] = 0.0; m.m[0][2] = 0.0;
    m.m[1][0] = 0.0; m.m[1][1] = 1.0; m.m[1][2] = 0.0;
    m.m[2][0] = 0.0; m.m[2][1] = 0.0; m.m[2][2] = 1.0;

    size_t len = print_matrix3d(m, buffer);
    buffer[len] = '\0';
    std::cout << "  matrix3d (identity) = \"" << buffer << "\"" << std::endl;
    assert(strcmp(buffer, "((1, 0, 0), (0, 1, 0), (0, 0, 1))") == 0);
  }

  // matrix4d - identity
  {
    char buffer[PRINT_MATRIX4D_MAX_CHARS];
    matrix4d m;
    m.m[0][0] = 1.0; m.m[0][1] = 0.0; m.m[0][2] = 0.0; m.m[0][3] = 0.0;
    m.m[1][0] = 0.0; m.m[1][1] = 1.0; m.m[1][2] = 0.0; m.m[1][3] = 0.0;
    m.m[2][0] = 0.0; m.m[2][1] = 0.0; m.m[2][2] = 1.0; m.m[2][3] = 0.0;
    m.m[3][0] = 0.0; m.m[3][1] = 0.0; m.m[3][2] = 0.0; m.m[3][3] = 1.0;

    size_t len = print_matrix4d(m, buffer);
    buffer[len] = '\0';
    std::cout << "  matrix4d (identity) = \"" << buffer << "\"" << std::endl;
    assert(strcmp(buffer, "((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1))") == 0);
  }
}

void test_buffer_sizes() {
  std::cout << "\nTesting max buffer sizes..." << std::endl;

  std::cout << "  DTOS_MAX_CHARS_FLOAT = " << DTOS_MAX_CHARS_FLOAT << std::endl;
  std::cout << "  DTOS_MAX_CHARS_DOUBLE = " << DTOS_MAX_CHARS_DOUBLE << std::endl;
  std::cout << "  PRINT_FLOAT2_MAX_CHARS = " << PRINT_FLOAT2_MAX_CHARS << std::endl;
  std::cout << "  PRINT_FLOAT3_MAX_CHARS = " << PRINT_FLOAT3_MAX_CHARS << std::endl;
  std::cout << "  PRINT_FLOAT4_MAX_CHARS = " << PRINT_FLOAT4_MAX_CHARS << std::endl;
  std::cout << "  PRINT_MATRIX2D_MAX_CHARS = " << PRINT_MATRIX2D_MAX_CHARS << std::endl;
  std::cout << "  PRINT_MATRIX3D_MAX_CHARS = " << PRINT_MATRIX3D_MAX_CHARS << std::endl;
  std::cout << "  PRINT_MATRIX4D_MAX_CHARS = " << PRINT_MATRIX4D_MAX_CHARS << std::endl;
}

int main() {
  std::cout << "=== Testing Efficient Vector/Matrix Printing ===" << std::endl;
  std::cout << std::endl;

  test_buffer_dtos();
  test_float_vectors();
  test_double_vectors();
  test_half_vectors();
  test_matrices();
  test_buffer_sizes();

  std::cout << std::endl;
  std::cout << "All tests PASSED!" << std::endl;
  return 0;
}
