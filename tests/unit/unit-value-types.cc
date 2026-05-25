#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-value-types.h"
#include "chunked-typed-array.hh"
#include "value-types.hh"
#include "math-util.inc"

#include <cstring>

using namespace tinyusdz;

void value_types_test(void) {

  value::token tok1("bora");
  value::token tok2("muda");
  value::token tok3("bora");
  TEST_CHECK(tok1 == tok1);
  TEST_CHECK(tok1 != tok2);
  TEST_CHECK(tok1 == tok3);

  TEST_CHECK(value::GetTypeName(value::TYPE_ID_TOKEN) == "token");
  TEST_CHECK(value::GetTypeName(value::TYPE_ID_TOKEN|value::TYPE_ID_1D_ARRAY_BIT) == "token[]");

  TEST_CHECK(value::GetTypeId("token") == value::TYPE_ID_TOKEN);
  TEST_CHECK(value::GetTypeId("token[]") == (value::TYPE_ID_TOKEN|value::TYPE_ID_1D_ARRAY_BIT));

  TEST_CHECK(!value::TryGetTypeName(value::TYPE_ID_ALL));

  // texCoord2f <-> float2 cast
  value::float2 uv{1.0f, 2.0f};
  value::Value value(uv);

  value::texcoord2f *tex2f = value.as<value::texcoord2f>();
  TEST_CHECK(tex2f != nullptr);
  if (tex2f) {
    TEST_CHECK(math::is_close(tex2f->s, 1.0f));
    TEST_CHECK(math::is_close(tex2f->t, 2.0f));
  }

}

// Test zero-copy RoleTypeCast functionality
void role_type_cast_test(void) {

  // Test 1: Scalar float2 -> texcoord2f
  {
    value::float2 uv{1.5f, 2.5f};
    value::Value val(uv);

    // Get pointer to underlying data before cast
    const void* ptr_before = val.get_raw().cast<value::float2>();

    // Perform zero-copy cast
    bool result = RoleTypeCast(value::TypeTraits<value::texcoord2f>::type_id(), val);
    TEST_CHECK(result == true);
    TEST_CHECK(val.type_id() == value::TypeTraits<value::texcoord2f>::type_id());

    // Verify data is unchanged (zero-copy)
    const void* ptr_after = val.get_raw().cast<value::texcoord2f>();
    TEST_CHECK(ptr_before == ptr_after);

    // Verify values are preserved
    auto* tex = val.as<value::texcoord2f>();
    TEST_CHECK(tex != nullptr);
    if (tex) {
      TEST_CHECK(math::is_close(tex->s, 1.5f));
      TEST_CHECK(math::is_close(tex->t, 2.5f));
    }
  }

  // Test 2: Scalar float3 -> normal3f
  {
    value::float3 n{0.0f, 1.0f, 0.0f};
    value::Value val(n);

    const void* ptr_before = val.get_raw().cast<value::float3>();

    bool result = RoleTypeCast(value::TypeTraits<value::normal3f>::type_id(), val);
    TEST_CHECK(result == true);
    TEST_CHECK(val.type_id() == value::TypeTraits<value::normal3f>::type_id());

    const void* ptr_after = val.get_raw().cast<value::normal3f>();
    TEST_CHECK(ptr_before == ptr_after);

    auto* norm = val.as<value::normal3f>();
    TEST_CHECK(norm != nullptr);
    if (norm) {
      TEST_CHECK(math::is_close(norm->x, 0.0f));
      TEST_CHECK(math::is_close(norm->y, 1.0f));
      TEST_CHECK(math::is_close(norm->z, 0.0f));
    }
  }

  // Test 3: Scalar float3 -> point3f
  {
    value::float3 p{1.0f, 2.0f, 3.0f};
    value::Value val(p);

    bool result = RoleTypeCast(value::TypeTraits<value::point3f>::type_id(), val);
    TEST_CHECK(result == true);
    TEST_CHECK(val.type_id() == value::TypeTraits<value::point3f>::type_id());

    auto* pt = val.as<value::point3f>();
    TEST_CHECK(pt != nullptr);
    if (pt) {
      TEST_CHECK(math::is_close(pt->x, 1.0f));
      TEST_CHECK(math::is_close(pt->y, 2.0f));
      TEST_CHECK(math::is_close(pt->z, 3.0f));
    }
  }

  // Test 4: Scalar float3 -> vector3f
  {
    value::float3 v{4.0f, 5.0f, 6.0f};
    value::Value val(v);

    bool result = RoleTypeCast(value::TypeTraits<value::vector3f>::type_id(), val);
    TEST_CHECK(result == true);
    TEST_CHECK(val.type_id() == value::TypeTraits<value::vector3f>::type_id());

    auto* vec = val.as<value::vector3f>();
    TEST_CHECK(vec != nullptr);
    if (vec) {
      TEST_CHECK(math::is_close(vec->x, 4.0f));
      TEST_CHECK(math::is_close(vec->y, 5.0f));
      TEST_CHECK(math::is_close(vec->z, 6.0f));
    }
  }

  // Test 5: Scalar float3 -> color3f
  {
    value::float3 c{0.5f, 0.6f, 0.7f};
    value::Value val(c);

    bool result = RoleTypeCast(value::TypeTraits<value::color3f>::type_id(), val);
    TEST_CHECK(result == true);
    TEST_CHECK(val.type_id() == value::TypeTraits<value::color3f>::type_id());

    auto* col = val.as<value::color3f>();
    TEST_CHECK(col != nullptr);
    if (col) {
      TEST_CHECK(math::is_close(col->r, 0.5f));
      TEST_CHECK(math::is_close(col->g, 0.6f));
      TEST_CHECK(math::is_close(col->b, 0.7f));
    }
  }

  // Test 6: Scalar float4 -> color4f
  {
    value::float4 c{0.1f, 0.2f, 0.3f, 1.0f};
    value::Value val(c);

    bool result = RoleTypeCast(value::TypeTraits<value::color4f>::type_id(), val);
    TEST_CHECK(result == true);
    TEST_CHECK(val.type_id() == value::TypeTraits<value::color4f>::type_id());

    auto* col = val.as<value::color4f>();
    TEST_CHECK(col != nullptr);
    if (col) {
      TEST_CHECK(math::is_close(col->r, 0.1f));
      TEST_CHECK(math::is_close(col->g, 0.2f));
      TEST_CHECK(math::is_close(col->b, 0.3f));
      TEST_CHECK(math::is_close(col->a, 1.0f));
    }
  }

  // Test 7: Array float2[] -> texcoord2f[]
  {
    std::vector<value::float2> uvs = {{1.0f, 2.0f}, {3.0f, 4.0f}, {5.0f, 6.0f}};
    value::Value val(uvs);

    const void* ptr_before = val.get_raw().cast<std::vector<value::float2>>();

    bool result = RoleTypeCast(value::TypeTraits<std::vector<value::texcoord2f>>::type_id(), val);
    TEST_CHECK(result == true);
    TEST_CHECK(val.type_id() == value::TypeTraits<std::vector<value::texcoord2f>>::type_id());

    // Verify zero-copy: same vector object
    const void* ptr_after = val.get_raw().cast<std::vector<value::texcoord2f>>();
    TEST_CHECK(ptr_before == ptr_after);

    auto* texs = val.as<std::vector<value::texcoord2f>>();
    TEST_CHECK(texs != nullptr);
    if (texs) {
      TEST_CHECK(texs->size() == 3);
      TEST_CHECK(math::is_close((*texs)[0].s, 1.0f));
      TEST_CHECK(math::is_close((*texs)[0].t, 2.0f));
      TEST_CHECK(math::is_close((*texs)[1].s, 3.0f));
      TEST_CHECK(math::is_close((*texs)[1].t, 4.0f));
      TEST_CHECK(math::is_close((*texs)[2].s, 5.0f));
      TEST_CHECK(math::is_close((*texs)[2].t, 6.0f));
    }
  }

  // Test 8: Array float3[] -> normal3f[]
  {
    std::vector<value::float3> normals = {{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    value::Value val(normals);

    const void* ptr_before = val.get_raw().cast<std::vector<value::float3>>();

    bool result = RoleTypeCast(value::TypeTraits<std::vector<value::normal3f>>::type_id(), val);
    TEST_CHECK(result == true);

    const void* ptr_after = val.get_raw().cast<std::vector<value::normal3f>>();
    TEST_CHECK(ptr_before == ptr_after);

    auto* norms = val.as<std::vector<value::normal3f>>();
    TEST_CHECK(norms != nullptr);
    if (norms) {
      TEST_CHECK(norms->size() == 3);
      TEST_CHECK(math::is_close((*norms)[0].y, 1.0f));
      TEST_CHECK(math::is_close((*norms)[1].x, 1.0f));
      TEST_CHECK(math::is_close((*norms)[2].z, 1.0f));
    }
  }

  // Test 9: Double precision - double3 -> normal3d
  {
    value::double3 n{0.0, 1.0, 0.0};
    value::Value val(n);

    bool result = RoleTypeCast(value::TypeTraits<value::normal3d>::type_id(), val);
    TEST_CHECK(result == true);
    TEST_CHECK(val.type_id() == value::TypeTraits<value::normal3d>::type_id());

    auto* norm = val.as<value::normal3d>();
    TEST_CHECK(norm != nullptr);
    if (norm) {
      TEST_CHECK(math::is_close(norm->y, 1.0));
    }
  }

  // Test 10: Half precision - half3 -> normal3h
  {
    value::half3 n{value::float_to_half_full(0.0f), value::float_to_half_full(1.0f), value::float_to_half_full(0.0f)};
    value::Value val(n);

    bool result = RoleTypeCast(value::TypeTraits<value::normal3h>::type_id(), val);
    TEST_CHECK(result == true);
    TEST_CHECK(val.type_id() == value::TypeTraits<value::normal3h>::type_id());
  }

  // Test 11: Invalid cast - type mismatch should fail
  {
    value::float2 uv{1.0f, 2.0f};
    value::Value val(uv);

    // Try to cast float2 to normal3f (incompatible)
    bool result = RoleTypeCast(value::TypeTraits<value::normal3f>::type_id(), val);
    TEST_CHECK(result == false);

    // Type should remain unchanged
    TEST_CHECK(val.type_id() == value::TypeTraits<value::float2>::type_id());
  }

  // Test 12: matrix4d -> frame4d
  {
    value::matrix4d m = value::matrix4d::identity();
    value::Value val(m);

    bool result = RoleTypeCast(value::TypeTraits<value::frame4d>::type_id(), val);
    TEST_CHECK(result == true);
    TEST_CHECK(val.type_id() == value::TypeTraits<value::frame4d>::type_id());
  }

}

void value_types_typed_array_memory_test(void) {
  {
    TypedArray<float> arr;
    arr.reserve(4096);
    arr.resize(16);

    value::Value val(arr);
    size_t estimate = val.estimate_memory_usage();
    TEST_CHECK(estimate >= arr.memory_usage());
  }

  {
    ChunkedTypedArray<float> arr(4096);
    value::Value val(arr);
    size_t estimate = val.estimate_memory_usage();
    TEST_CHECK(estimate >= arr.memory_usage());
  }
}
