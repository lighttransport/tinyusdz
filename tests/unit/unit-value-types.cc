#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-value-types.h"
#include "value-types.hh"
#include "typed-array.hh"
#include "math-util.inc"

using namespace tinyusdz;

void value_types_test(void) {

  //
  // Basic token tests
  //
  {
    value::token tok1("bora");
    value::token tok2("muda");
    value::token tok3("bora");
    TEST_CHECK(tok1 == tok1);
    TEST_CHECK(tok1 != tok2);
    TEST_CHECK(tok1 == tok3);
  }

  //
  // Type name/id lookup tests
  //
  {
    TEST_CHECK(value::GetTypeName(value::TYPE_ID_TOKEN) == "token");
    TEST_CHECK(value::GetTypeName(value::TYPE_ID_TOKEN|value::TYPE_ID_STL_ARRAY_BIT) == "token[]");

    TEST_CHECK(value::GetTypeId("token") == value::TYPE_ID_TOKEN);
    TEST_CHECK(value::GetTypeId("token[]") == (value::TYPE_ID_TOKEN|value::TYPE_ID_STL_ARRAY_BIT));

    TEST_CHECK(!value::TryGetTypeName(value::TYPE_ID_ALL));

    // TypedArray type ID should include both STL_ARRAY_BIT and TYPED_ARRAY_BIT
    uint32_t typed_array_float_id = value::TypeTraits<TypedArray<float>>::type_id();
    TEST_CHECK((typed_array_float_id & value::TYPE_ID_STL_ARRAY_BIT) != 0);
    TEST_CHECK((typed_array_float_id & value::TYPE_ID_TYPED_ARRAY_BIT) != 0);

    // std::vector type ID should only have STL_ARRAY_BIT
    uint32_t stl_vec_float_id = value::TypeTraits<std::vector<float>>::type_id();
    TEST_CHECK((stl_vec_float_id & value::TYPE_ID_STL_ARRAY_BIT) != 0);
    TEST_CHECK((stl_vec_float_id & value::TYPE_ID_TYPED_ARRAY_BIT) == 0);
  }

  //
  // Scalar role type cast tests
  //
  {
    // texCoord2f <-> float2 cast
    value::float2 uv{1.0f, 2.0f};
    value::Value val(uv);

    value::texcoord2f *tex2f = val.as<value::texcoord2f>();
    TEST_CHECK(tex2f != nullptr);
    if (tex2f) {
      TEST_CHECK(math::is_close(tex2f->s, 1.0f));
      TEST_CHECK(math::is_close(tex2f->t, 2.0f));
    }

    // float2 -> float2 (exact match)
    value::float2 *f2 = val.as<value::float2>();
    TEST_CHECK(f2 != nullptr);
    if (f2) {
      TEST_CHECK(math::is_close((*f2)[0], 1.0f));
      TEST_CHECK(math::is_close((*f2)[1], 2.0f));
    }

    // strict_cast should prevent role type conversion
    value::texcoord2f *tex2f_strict = val.as<value::texcoord2f>(/*strict_cast=*/true);
    TEST_CHECK(tex2f_strict == nullptr);  // Should fail because stored type is float2, not texcoord2f
  }

  //
  // More scalar role type tests: float3 variants
  //
  {
    value::float3 pos{1.0f, 2.0f, 3.0f};
    value::Value val(pos);

    // float3 -> vector3f (role type conversion)
    value::vector3f *vec3f = val.as<value::vector3f>();
    TEST_CHECK(vec3f != nullptr);
    if (vec3f) {
      TEST_CHECK(math::is_close(vec3f->x, 1.0f));
      TEST_CHECK(math::is_close(vec3f->y, 2.0f));
      TEST_CHECK(math::is_close(vec3f->z, 3.0f));
    }

    // float3 -> point3f (role type conversion)
    value::point3f *pt3f = val.as<value::point3f>();
    TEST_CHECK(pt3f != nullptr);

    // float3 -> normal3f (role type conversion)
    value::normal3f *nrm3f = val.as<value::normal3f>();
    TEST_CHECK(nrm3f != nullptr);

    // float3 -> color3f (role type conversion)
    value::color3f *col3f = val.as<value::color3f>();
    TEST_CHECK(col3f != nullptr);

    // strict cast should prevent conversion
    value::vector3f *vec3f_strict = val.as<value::vector3f>(/*strict_cast=*/true);
    TEST_CHECK(vec3f_strict == nullptr);
  }

  //
  // Role type -> underlying type cast tests
  //
  {
    value::color3f col{0.5f, 0.6f, 0.7f};
    value::Value val(col);

    // color3f -> float3 (underlying type)
    value::float3 *f3 = val.as<value::float3>();
    TEST_CHECK(f3 != nullptr);
    if (f3) {
      TEST_CHECK(math::is_close((*f3)[0], 0.5f));
      TEST_CHECK(math::is_close((*f3)[1], 0.6f));
      TEST_CHECK(math::is_close((*f3)[2], 0.7f));
    }

    // color3f -> vector3f (both have same underlying type float3)
    value::vector3f *vec3f = val.as<value::vector3f>();
    TEST_CHECK(vec3f != nullptr);

    // color3f -> color3f (exact match)
    value::color3f *col3f = val.as<value::color3f>();
    TEST_CHECK(col3f != nullptr);
  }

  //
  // Scalar type mismatch tests (should fail)
  //
  {
    value::float3 f3{1.0f, 2.0f, 3.0f};
    value::Value val(f3);

    // float3 cannot be cast to float2 (different dimensions)
    value::float2 *f2 = val.as<value::float2>();
    TEST_CHECK(f2 == nullptr);

    // float3 cannot be cast to float4
    value::float4 *f4 = val.as<value::float4>();
    TEST_CHECK(f4 == nullptr);

    // float3 cannot be cast to double3 (different precision)
    value::double3 *d3 = val.as<value::double3>();
    TEST_CHECK(d3 == nullptr);

    // float3 cannot be cast to int (different type)
    int *i = val.as<int>();
    TEST_CHECK(i == nullptr);
  }

  //
  // std::vector<T> (STL array) tests
  //
  {
    std::vector<float> vec{1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    value::Value val(vec);

    // Check type_id has STL_ARRAY_BIT but not TYPED_ARRAY_BIT
    TEST_CHECK((val.type_id() & value::TYPE_ID_STL_ARRAY_BIT) != 0);
    TEST_CHECK((val.type_id() & value::TYPE_ID_TYPED_ARRAY_BIT) == 0);

    // Check is_array() returns true
    TEST_CHECK(val.is_array());

    // Check array_size()
    TEST_CHECK(val.array_size() == 5);

    // Direct access via as()
    std::vector<float> *pvec = val.as<std::vector<float>>();
    TEST_CHECK(pvec != nullptr);
    if (pvec) {
      TEST_CHECK(pvec->size() == 5);
      TEST_CHECK(math::is_close((*pvec)[0], 1.0f));
      TEST_CHECK(math::is_close((*pvec)[4], 5.0f));
    }

    // get_value() copy
    auto opt_vec = val.get_value<std::vector<float>>();
    TEST_CHECK(opt_vec.has_value());
    if (opt_vec) {
      TEST_CHECK(opt_vec->size() == 5);
      TEST_CHECK(math::is_close((*opt_vec)[2], 3.0f));
    }
  }

  //
  // std::vector<T> with role types
  //
  {
    std::vector<value::float3> vec;
    vec.push_back(value::float3{1.0f, 2.0f, 3.0f});
    vec.push_back(value::float3{4.0f, 5.0f, 6.0f});
    value::Value val(vec);

    // float3[] -> vector3f[] conversion (role type array)
    std::vector<value::vector3f> *pvec = val.as<std::vector<value::vector3f>>();
    TEST_CHECK(pvec != nullptr);
    if (pvec) {
      TEST_CHECK(pvec->size() == 2);
      TEST_CHECK(math::is_close((*pvec)[0].x, 1.0f));
      TEST_CHECK(math::is_close((*pvec)[1].z, 6.0f));
    }

    // float3[] -> point3f[] conversion
    std::vector<value::point3f> *ppts = val.as<std::vector<value::point3f>>();
    TEST_CHECK(ppts != nullptr);

    // float3[] -> color3f[] conversion
    std::vector<value::color3f> *pcols = val.as<std::vector<value::color3f>>();
    TEST_CHECK(pcols != nullptr);

    // strict_cast should prevent role type conversion
    std::vector<value::vector3f> *pvec_strict = val.as<std::vector<value::vector3f>>(/*strict_cast=*/true);
    TEST_CHECK(pvec_strict == nullptr);
  }

  //
  // TypedArray<T> tests
  //
  {
    TypedArray<float> tarr = CreateOwnedTypedArray<float>(5);
    for (size_t i = 0; i < 5; ++i) {
      tarr[i] = static_cast<float>(i + 1);
    }
    value::Value val(tarr);

    // Check type_id has both STL_ARRAY_BIT and TYPED_ARRAY_BIT
    TEST_CHECK((val.type_id() & value::TYPE_ID_STL_ARRAY_BIT) != 0);
    TEST_CHECK((val.type_id() & value::TYPE_ID_TYPED_ARRAY_BIT) != 0);

    // Check is_array() returns true
    TEST_CHECK(val.is_array());

    // Direct access via as<TypedArray<T>>()
    TypedArray<float> *ptarr = val.as<TypedArray<float>>();
    TEST_CHECK(ptarr != nullptr);
    if (ptarr) {
      TEST_CHECK(ptarr->size() == 5);
      TEST_CHECK(math::is_close((*ptarr)[0], 1.0f));
      TEST_CHECK(math::is_close((*ptarr)[4], 5.0f));
    }

    // Cannot cast TypedArray<float> to std::vector<float> - different storage
    std::vector<float> *pvec = val.as<std::vector<float>>();
    TEST_CHECK(pvec == nullptr);  // Should fail!

    // get_value() for TypedArray
    auto opt_tarr = val.get_value<TypedArray<float>>();
    TEST_CHECK(opt_tarr.has_value());
    if (opt_tarr) {
      TEST_CHECK(opt_tarr->size() == 5);
      TEST_CHECK(math::is_close((*opt_tarr)[2], 3.0f));
    }

    // get_value() should not return std::vector for TypedArray
    auto opt_vec = val.get_value<std::vector<float>>();
    TEST_CHECK(!opt_vec.has_value());  // Should fail!
  }

  //
  // TypedArray<T> with role types
  //
  {
    TypedArray<value::float3> tarr = CreateOwnedTypedArray<value::float3>(2);
    tarr[0] = value::float3{1.0f, 2.0f, 3.0f};
    tarr[1] = value::float3{4.0f, 5.0f, 6.0f};
    value::Value val(tarr);

    // TypedArray<float3> -> TypedArray<vector3f> role type conversion
    TypedArray<value::vector3f> *ptarr = val.as<TypedArray<value::vector3f>>();
    TEST_CHECK(ptarr != nullptr);
    if (ptarr) {
      TEST_CHECK(ptarr->size() == 2);
      TEST_CHECK(math::is_close((*ptarr)[0].x, 1.0f));
    }

    // TypedArray<float3> -> TypedArray<point3f>
    TypedArray<value::point3f> *ppts = val.as<TypedArray<value::point3f>>();
    TEST_CHECK(ppts != nullptr);

    // TypedArray<float3> -> TypedArray<color3f>
    TypedArray<value::color3f> *pcols = val.as<TypedArray<value::color3f>>();
    TEST_CHECK(pcols != nullptr);

    // Cannot cast TypedArray to std::vector - different storage types
    std::vector<value::float3> *pvec = val.as<std::vector<value::float3>>();
    TEST_CHECK(pvec == nullptr);

    std::vector<value::vector3f> *pvec2 = val.as<std::vector<value::vector3f>>();
    TEST_CHECK(pvec2 == nullptr);
  }

  //
  // std::vector cannot be cast to TypedArray
  //
  {
    std::vector<float> vec{1.0f, 2.0f, 3.0f};
    value::Value val(vec);

    // Cannot get TypedArray<float> from std::vector<float>
    TypedArray<float> *ptarr = val.as<TypedArray<float>>();
    TEST_CHECK(ptarr == nullptr);

    auto opt_tarr = val.get_value<TypedArray<float>>();
    TEST_CHECK(!opt_tarr.has_value());
  }

  //
  // as_view() tests for std::vector
  //
  {
    std::vector<float> vec{1.0f, 2.0f, 3.0f, 4.0f};
    value::Value val(vec);

    // Get view to float array
    auto view = val.as_view<float>();
    TEST_CHECK(view.size() == 4);
    TEST_CHECK(math::is_close(view[0], 1.0f));
    TEST_CHECK(math::is_close(view[3], 4.0f));
  }

  //
  // as_view() tests for std::vector with role types
  //
  {
    std::vector<value::float3> vec;
    vec.push_back(value::float3{1.0f, 2.0f, 3.0f});
    vec.push_back(value::float3{4.0f, 5.0f, 6.0f});
    value::Value val(vec);

    // Get view as float3
    auto view_f3 = val.as_view<value::float3>();
    TEST_CHECK(view_f3.size() == 2);

    // Get view as vector3f (role type conversion)
    auto view_v3 = val.as_view<value::vector3f>();
    TEST_CHECK(view_v3.size() == 2);
    if (view_v3.size() == 2) {
      TEST_CHECK(math::is_close(view_v3[0].x, 1.0f));
      TEST_CHECK(math::is_close(view_v3[1].z, 6.0f));
    }
  }

  //
  // Empty array tests
  //
  {
    std::vector<float> empty_vec;
    value::Value val(empty_vec);

    TEST_CHECK(val.is_array());
    TEST_CHECK(val.array_size() == 0);

    auto *pvec = val.as<std::vector<float>>();
    TEST_CHECK(pvec != nullptr);
    if (pvec) {
      TEST_CHECK(pvec->empty());
    }
  }

  //
  // Empty TypedArray tests
  //
  {
    TypedArray<float> empty_tarr = CreateOwnedTypedArray<float>(0);
    value::Value val(empty_tarr);

    TEST_CHECK(val.is_array());

    auto *ptarr = val.as<TypedArray<float>>();
    TEST_CHECK(ptarr != nullptr);
    if (ptarr) {
      TEST_CHECK(ptarr->empty());
    }
  }

  //
  // Value copy/move semantics with arrays
  //
  {
    std::vector<float> vec{1.0f, 2.0f, 3.0f};
    value::Value val1(vec);

    // Copy construction - now works properly with SFINAE on templated constructors
    value::Value val2(val1);
    auto *pvec1 = val1.as<std::vector<float>>();
    auto *pvec2 = val2.as<std::vector<float>>();
    TEST_CHECK(pvec1 != nullptr);
    TEST_CHECK(pvec2 != nullptr);
    if (pvec1 && pvec2) {
      TEST_CHECK(pvec1->size() == pvec2->size());
      // Values should be equal (deep copy)
      TEST_CHECK(math::is_close((*pvec1)[0], (*pvec2)[0]));
    }

    // Copy assignment
    value::Value val2b;
    val2b = val1;
    auto *pvec2b = val2b.as<std::vector<float>>();
    TEST_CHECK(pvec2b != nullptr);
    if (pvec2b) {
      TEST_CHECK(pvec2b->size() == 3);
    }

    // Move construction
    value::Value val3(std::move(val1));
    auto *pvec3 = val3.as<std::vector<float>>();
    TEST_CHECK(pvec3 != nullptr);
    if (pvec3) {
      TEST_CHECK(pvec3->size() == 3);
    }
  }

  //
  // Value copy/move semantics with TypedArray
  //
  {
    TypedArray<float> tarr = CreateOwnedTypedArray<float>(3);
    tarr[0] = 1.0f; tarr[1] = 2.0f; tarr[2] = 3.0f;
    value::Value val1(std::move(tarr));

    // Copy construction - now works properly with SFINAE on templated constructors
    value::Value val2(val1);
    auto *ptarr1 = val1.as<TypedArray<float>>();
    auto *ptarr2 = val2.as<TypedArray<float>>();
    TEST_CHECK(ptarr1 != nullptr);
    TEST_CHECK(ptarr2 != nullptr);
    if (ptarr1 && ptarr2) {
      TEST_CHECK(ptarr1->size() == ptarr2->size());
    }

    // Copy assignment
    value::Value val2b;
    val2b = val1;
    auto *ptarr2b = val2b.as<TypedArray<float>>();
    TEST_CHECK(ptarr2b != nullptr);
    if (ptarr2b) {
      TEST_CHECK(ptarr2b->size() == 3);
    }
  }

  //
  // Matrix type tests
  //
  {
    value::matrix4d mat = value::matrix4d::identity();
    value::Value val(mat);

    auto *pmat = val.as<value::matrix4d>();
    TEST_CHECK(pmat != nullptr);
    if (pmat) {
      // Check identity matrix
      TEST_CHECK(math::is_close(pmat->m[0][0], 1.0));
      TEST_CHECK(math::is_close(pmat->m[1][1], 1.0));
      TEST_CHECK(math::is_close(pmat->m[2][2], 1.0));
      TEST_CHECK(math::is_close(pmat->m[3][3], 1.0));
    }

    // frame4d is a role type of matrix4d
    value::frame4d *pframe = val.as<value::frame4d>();
    TEST_CHECK(pframe != nullptr);

    // matrix4d cannot be cast to matrix3d or matrix4f
    value::matrix3d *pmat3 = val.as<value::matrix3d>();
    TEST_CHECK(pmat3 == nullptr);

    value::matrix4f *pmatf = val.as<value::matrix4f>();
    TEST_CHECK(pmatf == nullptr);
  }

  //
  // Quaternion type tests
  //
  {
    value::quatf q{0.0f, 0.0f, 0.0f, 1.0f};  // identity quaternion (w=1)
    value::Value val(q);

    auto *pq = val.as<value::quatf>();
    TEST_CHECK(pq != nullptr);
    if (pq) {
      TEST_CHECK(math::is_close(pq->imag[0], 0.0f));
      TEST_CHECK(math::is_close(pq->imag[1], 0.0f));
      TEST_CHECK(math::is_close(pq->imag[2], 0.0f));
      TEST_CHECK(math::is_close(pq->real, 1.0f));
    }

    // quatf cannot be cast to quatd (different precision)
    value::quatd *pqd = val.as<value::quatd>();
    TEST_CHECK(pqd == nullptr);

    // quatf cannot be cast to quath
    value::quath *pqh = val.as<value::quath>();
    TEST_CHECK(pqh == nullptr);
  }

  //
  // Integer type tests
  //
  {
    int32_t i = 42;
    value::Value val(i);

    auto *pi = val.as<int32_t>();
    TEST_CHECK(pi != nullptr);
    if (pi) {
      TEST_CHECK(*pi == 42);
    }

    // int32 cannot be cast to int64 or uint32
    int64_t *pi64 = val.as<int64_t>();
    TEST_CHECK(pi64 == nullptr);

    uint32_t *pu32 = val.as<uint32_t>();
    TEST_CHECK(pu32 == nullptr);

    // int32 cannot be cast to float
    float *pf = val.as<float>();
    TEST_CHECK(pf == nullptr);
  }

  //
  // String and token tests
  //
  {
    std::string str = "hello";
    value::Value val(str);

    auto *pstr = val.as<std::string>();
    TEST_CHECK(pstr != nullptr);
    if (pstr) {
      TEST_CHECK(*pstr == "hello");
    }

    // string cannot be cast to token
    value::token *ptok = val.as<value::token>();
    TEST_CHECK(ptok == nullptr);
  }

  //
  // Token test
  //
  {
    value::token tok("world");
    value::Value val(tok);

    auto *ptok = val.as<value::token>();
    TEST_CHECK(ptok != nullptr);
    if (ptok) {
      TEST_CHECK(ptok->str() == "world");
    }

    // token cannot be cast to string
    std::string *pstr = val.as<std::string>();
    TEST_CHECK(pstr == nullptr);
  }

  //
  // is_empty() and is_none() tests
  //
  {
    value::Value empty_val;
    TEST_CHECK(empty_val.is_empty());
    TEST_CHECK(!empty_val.is_none());
    TEST_CHECK(!empty_val.is_array());
  }

  //
  // Array of different types cannot be cross-cast
  //
  {
    std::vector<float> float_vec{1.0f, 2.0f, 3.0f};
    value::Value val(float_vec);

    // Cannot cast float[] to int[]
    std::vector<int32_t> *pint_vec = val.as<std::vector<int32_t>>();
    TEST_CHECK(pint_vec == nullptr);

    // Cannot cast float[] to double[]
    std::vector<double> *pdouble_vec = val.as<std::vector<double>>();
    TEST_CHECK(pdouble_vec == nullptr);
  }

  //
  // as_view() tests for TypedArray
  //
  {
    TypedArray<float> tarr = CreateOwnedTypedArray<float>(4);
    tarr[0] = 1.0f; tarr[1] = 2.0f; tarr[2] = 3.0f; tarr[3] = 4.0f;
    value::Value val(tarr);

    // Verify the TypedArray is stored correctly
    TEST_CHECK(val.is_array());
    TEST_CHECK((val.type_id() & value::TYPE_ID_TYPED_ARRAY_BIT) != 0);

    // Verify we can get the TypedArray back via as()
    TypedArray<float>* ptarr = val.as<TypedArray<float>>();
    TEST_CHECK(ptarr != nullptr);
    if (ptarr) {
      TEST_CHECK(ptarr->size() == 4);
    }

    // Get view to float array
    auto view = val.as_view<float>();
    TEST_CHECK(view.size() == 4);
    if (view.size() == 4) {
      TEST_CHECK(math::is_close(view[0], 1.0f));
      TEST_CHECK(math::is_close(view[3], 4.0f));
    }
  }

  //
  // as_view() tests for TypedArray with role types
  //
  {
    TypedArray<value::float3> tarr = CreateOwnedTypedArray<value::float3>(2);
    tarr[0] = value::float3{1.0f, 2.0f, 3.0f};
    tarr[1] = value::float3{4.0f, 5.0f, 6.0f};
    value::Value val(tarr);

    // Get view as float3
    auto view_f3 = val.as_view<value::float3>();
    TEST_CHECK(view_f3.size() == 2);

    // Get view as vector3f (role type conversion)
    auto view_v3 = val.as_view<value::vector3f>();
    TEST_CHECK(view_v3.size() == 2);
    if (view_v3.size() == 2) {
      TEST_CHECK(math::is_close(view_v3[0].x, 1.0f));
      TEST_CHECK(math::is_close(view_v3[1].z, 6.0f));
    }

    // Get view as point3f
    auto view_p3 = val.as_view<value::point3f>();
    TEST_CHECK(view_p3.size() == 2);

    // Get view as color3f
    auto view_c3 = val.as_view<value::color3f>();
    TEST_CHECK(view_c3.size() == 2);
  }

  //
  // TypedArray element type mismatch tests
  //
  {
    TypedArray<float> tarr = CreateOwnedTypedArray<float>(3);
    tarr[0] = 1.0f; tarr[1] = 2.0f; tarr[2] = 3.0f;
    value::Value val(tarr);

    // Cannot cast TypedArray<float> to TypedArray<int32_t>
    TypedArray<int32_t> *ptarr_int = val.as<TypedArray<int32_t>>();
    TEST_CHECK(ptarr_int == nullptr);

    // Cannot cast TypedArray<float> to TypedArray<double>
    TypedArray<double> *ptarr_double = val.as<TypedArray<double>>();
    TEST_CHECK(ptarr_double == nullptr);
  }

  //
  // Test type_id bit flags for arrays
  //
  {
    // std::vector<float> should have STL_ARRAY_BIT but not TYPED_ARRAY_BIT
    uint32_t vec_float_id = value::TypeTraits<std::vector<float>>::type_id();
    TEST_CHECK((vec_float_id & value::TYPE_ID_STL_ARRAY_BIT) != 0);
    TEST_CHECK((vec_float_id & value::TYPE_ID_TYPED_ARRAY_BIT) == 0);

    // TypedArray<float> should have both STL_ARRAY_BIT and TYPED_ARRAY_BIT
    uint32_t tarr_float_id = value::TypeTraits<TypedArray<float>>::type_id();
    TEST_CHECK((tarr_float_id & value::TYPE_ID_STL_ARRAY_BIT) != 0);
    TEST_CHECK((tarr_float_id & value::TYPE_ID_TYPED_ARRAY_BIT) != 0);

    // Underlying element type should be the same (float)
    uint32_t vec_underlying = value::TypeTraits<std::vector<float>>::underlying_type_id() & (~value::TYPE_ID_ARRAY_BIT_MASK);
    uint32_t tarr_underlying = value::TypeTraits<TypedArray<float>>::underlying_type_id() & (~value::TYPE_ID_ARRAY_BIT_MASK);
    TEST_CHECK(vec_underlying == tarr_underlying);
    TEST_CHECK(vec_underlying == value::TYPE_ID_FLOAT);
  }

  //
  // Test type_id bit flags for arrays with role types
  //
  {
    // std::vector<float3> underlying should be TYPE_ID_FLOAT3
    uint32_t vec_f3_id = value::TypeTraits<std::vector<value::float3>>::underlying_type_id() & (~value::TYPE_ID_ARRAY_BIT_MASK);
    TEST_CHECK(vec_f3_id == value::TYPE_ID_FLOAT3);

    // std::vector<vector3f> underlying should also be TYPE_ID_FLOAT3 (role type)
    uint32_t vec_v3_id = value::TypeTraits<std::vector<value::vector3f>>::underlying_type_id() & (~value::TYPE_ID_ARRAY_BIT_MASK);
    TEST_CHECK(vec_v3_id == value::TYPE_ID_FLOAT3);

    // TypedArray<float3> underlying should be TYPE_ID_FLOAT3
    uint32_t tarr_f3_id = value::TypeTraits<TypedArray<value::float3>>::underlying_type_id() & (~value::TYPE_ID_ARRAY_BIT_MASK);
    TEST_CHECK(tarr_f3_id == value::TYPE_ID_FLOAT3);
  }

  //
  // Large array tests
  //
  {
    std::vector<float> large_vec(10000);
    for (size_t i = 0; i < large_vec.size(); ++i) {
      large_vec[i] = static_cast<float>(i);
    }
    value::Value val(large_vec);

    TEST_CHECK(val.array_size() == 10000);
    auto *pvec = val.as<std::vector<float>>();
    TEST_CHECK(pvec != nullptr);
    if (pvec) {
      TEST_CHECK(pvec->size() == 10000);
      TEST_CHECK(math::is_close((*pvec)[0], 0.0f));
      TEST_CHECK(math::is_close((*pvec)[9999], 9999.0f));
    }
  }

  //
  // Large TypedArray tests
  //
  {
    TypedArray<float> large_tarr = CreateOwnedTypedArray<float>(10000);
    for (size_t i = 0; i < large_tarr.size(); ++i) {
      large_tarr[i] = static_cast<float>(i);
    }
    value::Value val(std::move(large_tarr));

    TEST_CHECK(val.is_array());
    auto *ptarr = val.as<TypedArray<float>>();
    TEST_CHECK(ptarr != nullptr);
    if (ptarr) {
      TEST_CHECK(ptarr->size() == 10000);
      TEST_CHECK(math::is_close((*ptarr)[0], 0.0f));
      TEST_CHECK(math::is_close((*ptarr)[9999], 9999.0f));
    }
  }

  //
  // Nested role type tests (color3f[] -> float3[] -> vector3f[])
  //
  {
    std::vector<value::color3f> colors;
    colors.push_back(value::color3f{1.0f, 0.0f, 0.0f});  // Red
    colors.push_back(value::color3f{0.0f, 1.0f, 0.0f});  // Green
    colors.push_back(value::color3f{0.0f, 0.0f, 1.0f});  // Blue
    value::Value val(colors);

    // color3f[] -> float3[] should work
    std::vector<value::float3> *pf3 = val.as<std::vector<value::float3>>();
    TEST_CHECK(pf3 != nullptr);

    // color3f[] -> vector3f[] should work
    std::vector<value::vector3f> *pv3 = val.as<std::vector<value::vector3f>>();
    TEST_CHECK(pv3 != nullptr);

    // color3f[] -> point3f[] should work
    std::vector<value::point3f> *pp3 = val.as<std::vector<value::point3f>>();
    TEST_CHECK(pp3 != nullptr);

    // color3f[] -> normal3f[] should work
    std::vector<value::normal3f> *pn3 = val.as<std::vector<value::normal3f>>();
    TEST_CHECK(pn3 != nullptr);

    // color3f[] -> color4f[] should NOT work (different dimensions)
    std::vector<value::color4f> *pc4 = val.as<std::vector<value::color4f>>();
    TEST_CHECK(pc4 == nullptr);
  }

  //
  // Bool type tests
  //
  {
    bool b = true;
    value::Value val(b);

    bool *pb = val.as<bool>();
    TEST_CHECK(pb != nullptr);
    if (pb) {
      TEST_CHECK(*pb == true);
    }

    // bool cannot be cast to int
    int *pi = val.as<int>();
    TEST_CHECK(pi == nullptr);
  }

  //
  // Double type tests
  //
  {
    double d = 3.14159265358979;
    value::Value val(d);

    double *pd = val.as<double>();
    TEST_CHECK(pd != nullptr);
    if (pd) {
      TEST_CHECK(math::is_close(*pd, 3.14159265358979));
    }

    // double cannot be cast to float (different precision)
    float *pf = val.as<float>();
    TEST_CHECK(pf == nullptr);
  }

  //
  // half type tests (if available)
  //
  {
    value::half h;
    h.value = 0x3C00;  // 1.0 in half precision
    value::Value val(h);

    value::half *ph = val.as<value::half>();
    TEST_CHECK(ph != nullptr);

    // half cannot be cast to float
    float *pf = val.as<float>();
    TEST_CHECK(pf == nullptr);
  }

  //
  // ============================================================================
  // Copy/Move Constructor and Assignment Operator Tests
  // ============================================================================
  //
  // Note: Before SFINAE was added to the templated constructor, the following
  // code would NOT compile because the templated constructor
  //   template<class T> Value(T &&v)
  // would match Value& better than the copy constructor Value(const Value&).
  // This caused "incomplete type TypeTraits<Value>" errors.
  //
  // The fix uses std::enable_if with std::decay to exclude Value from the
  // templated constructor:
  //   template <class T,
  //             typename std::enable_if<
  //               !std::is_same<typename std::decay<T>::type, Value>::value,
  //               int>::type = 0>
  //   Value(T &&v) noexcept { ... }
  //
  // ============================================================================

  //
  // Basic copy construction from lvalue
  //
  {
    float f = 42.0f;
    value::Value val1(f);

    // This used to fail to compile without SFINAE fix:
    // Error: "incomplete type 'tinyusdz::value::TypeTraits<Value>' used"
    value::Value val2(val1);

    float *pf1 = val1.as<float>();
    float *pf2 = val2.as<float>();
    TEST_CHECK(pf1 != nullptr);
    TEST_CHECK(pf2 != nullptr);
    if (pf1 && pf2) {
      TEST_CHECK(math::is_close(*pf1, 42.0f));
      TEST_CHECK(math::is_close(*pf2, 42.0f));
      // After copy, both should be independent
      TEST_CHECK(pf1 != pf2);
    }
  }

  //
  // Copy construction from const lvalue
  //
  {
    int32_t i = 123;
    value::Value val1(i);
    const value::Value& cref = val1;

    // Copy from const reference should work
    value::Value val2(cref);

    int32_t *pi1 = val1.as<int32_t>();
    int32_t *pi2 = val2.as<int32_t>();
    TEST_CHECK(pi1 != nullptr);
    TEST_CHECK(pi2 != nullptr);
    if (pi1 && pi2) {
      TEST_CHECK(*pi1 == 123);
      TEST_CHECK(*pi2 == 123);
    }
  }

  //
  // Move construction
  //
  {
    std::vector<double> vec{1.0, 2.0, 3.0, 4.0, 5.0};
    value::Value val1(vec);

    // Move construction
    value::Value val2(std::move(val1));

    // val2 should have the data
    std::vector<double> *pvec2 = val2.as<std::vector<double>>();
    TEST_CHECK(pvec2 != nullptr);
    if (pvec2) {
      TEST_CHECK(pvec2->size() == 5);
      TEST_CHECK(math::is_close((*pvec2)[0], 1.0));
      TEST_CHECK(math::is_close((*pvec2)[4], 5.0));
    }

    // val1 may be in valid but unspecified state after move
    // We don't check val1's contents after move
  }

  //
  // Copy assignment operator
  //
  {
    value::float3 f3{1.0f, 2.0f, 3.0f};
    value::Value val1(f3);

    value::Value val2;
    TEST_CHECK(val2.is_empty());

    // Copy assignment
    val2 = val1;

    value::float3 *pf1 = val1.as<value::float3>();
    value::float3 *pf2 = val2.as<value::float3>();
    TEST_CHECK(pf1 != nullptr);
    TEST_CHECK(pf2 != nullptr);
    if (pf1 && pf2) {
      TEST_CHECK(math::is_close((*pf1)[0], 1.0f));
      TEST_CHECK(math::is_close((*pf2)[0], 1.0f));
      TEST_CHECK(math::is_close((*pf1)[2], 3.0f));
      TEST_CHECK(math::is_close((*pf2)[2], 3.0f));
    }
  }

  //
  // Copy assignment from const lvalue
  //
  {
    value::token tok("test_token");
    value::Value val1(tok);
    const value::Value& cref = val1;

    value::Value val2;
    val2 = cref;

    value::token *ptok1 = val1.as<value::token>();
    value::token *ptok2 = val2.as<value::token>();
    TEST_CHECK(ptok1 != nullptr);
    TEST_CHECK(ptok2 != nullptr);
    if (ptok1 && ptok2) {
      TEST_CHECK(ptok1->str() == "test_token");
      TEST_CHECK(ptok2->str() == "test_token");
    }
  }

  //
  // Move assignment operator
  //
  {
    TypedArray<int32_t> tarr = CreateOwnedTypedArray<int32_t>(100);
    for (size_t i = 0; i < 100; ++i) {
      tarr[i] = static_cast<int32_t>(i);
    }
    value::Value val1(std::move(tarr));

    value::Value val2;

    // Move assignment
    val2 = std::move(val1);

    // val2 should have the data
    TypedArray<int32_t> *ptarr2 = val2.as<TypedArray<int32_t>>();
    TEST_CHECK(ptarr2 != nullptr);
    if (ptarr2) {
      TEST_CHECK(ptarr2->size() == 100);
      TEST_CHECK((*ptarr2)[0] == 0);
      TEST_CHECK((*ptarr2)[99] == 99);
    }
  }

  //
  // Self-assignment (copy)
  //
  {
    double d = 2.71828;
    value::Value val(d);

    // Self-assignment should be safe
    val = val;

    double *pd = val.as<double>();
    TEST_CHECK(pd != nullptr);
    if (pd) {
      TEST_CHECK(math::is_close(*pd, 2.71828));
    }
  }

  //
  // Self-assignment (move) - should be safe
  //
  {
    std::string str = "hello world";
    value::Value val(str);

    // Self-move-assignment should be safe (even if technically undefined behavior)
    // Most implementations handle this gracefully
    val = std::move(val);

    // After self-move, value may be in valid but unspecified state
    // Some implementations keep it unchanged
  }

  //
  // Chained copy assignment
  //
  {
    value::float2 f2{5.0f, 6.0f};
    value::Value val1(f2);
    value::Value val2;
    value::Value val3;

    // Chained assignment
    val3 = val2 = val1;

    value::float2 *pf1 = val1.as<value::float2>();
    value::float2 *pf2 = val2.as<value::float2>();
    value::float2 *pf3 = val3.as<value::float2>();
    TEST_CHECK(pf1 != nullptr);
    TEST_CHECK(pf2 != nullptr);
    TEST_CHECK(pf3 != nullptr);
    if (pf1 && pf2 && pf3) {
      TEST_CHECK(math::is_close((*pf1)[0], 5.0f));
      TEST_CHECK(math::is_close((*pf2)[0], 5.0f));
      TEST_CHECK(math::is_close((*pf3)[0], 5.0f));
    }
  }

  //
  // Copy/move with different value types
  //
  {
    // Copy from Value containing scalar
    int32_t i = 42;
    value::Value val_int(i);
    value::Value val_int_copy(val_int);
    TEST_CHECK(val_int_copy.as<int32_t>() != nullptr);

    // Copy from Value containing array
    std::vector<float> vec{1.0f, 2.0f};
    value::Value val_vec(vec);
    value::Value val_vec_copy(val_vec);
    TEST_CHECK(val_vec_copy.as<std::vector<float>>() != nullptr);

    // Copy from Value containing TypedArray
    TypedArray<double> tarr = CreateOwnedTypedArray<double>(3);
    value::Value val_tarr(tarr);
    value::Value val_tarr_copy(val_tarr);
    TEST_CHECK(val_tarr_copy.as<TypedArray<double>>() != nullptr);

    // Copy from Value containing string
    std::string str = "test";
    value::Value val_str(str);
    value::Value val_str_copy(val_str);
    TEST_CHECK(val_str_copy.as<std::string>() != nullptr);

    // Copy from Value containing token
    value::token tok("tok");
    value::Value val_tok(tok);
    value::Value val_tok_copy(val_tok);
    TEST_CHECK(val_tok_copy.as<value::token>() != nullptr);

    // Copy from Value containing matrix
    value::matrix4d mat = value::matrix4d::identity();
    value::Value val_mat(mat);
    value::Value val_mat_copy(val_mat);
    TEST_CHECK(val_mat_copy.as<value::matrix4d>() != nullptr);

    // Copy from Value containing quaternion
    value::quatf q{0.0f, 0.0f, 0.0f, 1.0f};
    value::Value val_quat(q);
    value::Value val_quat_copy(val_quat);
    TEST_CHECK(val_quat_copy.as<value::quatf>() != nullptr);
  }

  //
  // Overwrite with different type via assignment
  //
  {
    int32_t i = 42;
    value::Value val(i);
    TEST_CHECK(val.as<int32_t>() != nullptr);

    // Create new Value with different type and assign
    float f = 3.14f;
    value::Value val2(f);
    val = val2;

    // val should now contain float, not int
    TEST_CHECK(val.as<int32_t>() == nullptr);
    TEST_CHECK(val.as<float>() != nullptr);
    if (val.as<float>()) {
      TEST_CHECK(math::is_close(*val.as<float>(), 3.14f));
    }
  }

  //
  // Copy/move empty Value
  //
  {
    value::Value empty_val;
    TEST_CHECK(empty_val.is_empty());

    // Copy empty value
    value::Value copy_of_empty(empty_val);
    TEST_CHECK(copy_of_empty.is_empty());

    // Move empty value
    value::Value move_of_empty(std::move(empty_val));
    TEST_CHECK(move_of_empty.is_empty());

    // Assign empty value
    float f = 1.0f;
    value::Value non_empty(f);
    TEST_CHECK(!non_empty.is_empty());

    value::Value another_empty;
    non_empty = another_empty;
    TEST_CHECK(non_empty.is_empty());
  }

  //
  // Copy/move with complex nested types (array of role types)
  //
  {
    std::vector<value::color3f> colors;
    colors.push_back(value::color3f{1.0f, 0.0f, 0.0f});
    colors.push_back(value::color3f{0.0f, 1.0f, 0.0f});
    value::Value val1(colors);

    // Copy construction
    value::Value val2(val1);

    auto *pcols1 = val1.as<std::vector<value::color3f>>();
    auto *pcols2 = val2.as<std::vector<value::color3f>>();
    TEST_CHECK(pcols1 != nullptr);
    TEST_CHECK(pcols2 != nullptr);
    if (pcols1 && pcols2) {
      TEST_CHECK(pcols1->size() == 2);
      TEST_CHECK(pcols2->size() == 2);
      // Should be independent copies
      TEST_CHECK(pcols1->data() != pcols2->data());
    }

    // Move construction
    value::Value val3(std::move(val2));
    auto *pcols3 = val3.as<std::vector<value::color3f>>();
    TEST_CHECK(pcols3 != nullptr);
    if (pcols3) {
      TEST_CHECK(pcols3->size() == 2);
    }
  }

  //
  // ============================================================================
  // COMPILE ERROR EXAMPLES (commented out)
  // ============================================================================
  //
  // The following code demonstrates what would cause compile errors if the
  // templated constructor did NOT use SFINAE to exclude Value types.
  //
  // Without SFINAE, when you write:
  //   Value val2(val1);  // where val1 is Value&
  //
  // The compiler considers both:
  //   1. Copy constructor: Value(const Value&)
  //   2. Templated constructor: template<class T> Value(T&&)
  //
  // For a non-const lvalue (val1), the templated version is a better match
  // because T&& with T=Value& is an exact match, while the copy constructor
  // requires const-qualification.
  //
  // This causes instantiation of TypeTraits<Value> which is incomplete,
  // resulting in a compile error.
  //

  // EXAMPLE 1: Direct copy from non-const lvalue
  // Without SFINAE, this would fail:
  //
  // value::Value val1(42);
  // value::Value val2(val1);  // ERROR without SFINAE
  //                           // Works correctly with SFINAE
  //

  // EXAMPLE 2: Passing Value by value to a function
  // Without SFINAE, this pattern could fail in some cases:
  //
  // void process(value::Value v) { ... }
  // value::Value val(42);
  // process(val);  // Copy needed - might trigger templated ctor without SFINAE
  //

  // EXAMPLE 3: Returning Value from function and copying
  // value::Value make_value() { return value::Value(42); }
  // value::Value v1 = make_value();
  // value::Value v2(v1);  // ERROR without SFINAE
  //

  // EXAMPLE 4: Assignment could also be affected
  // The same SFINAE pattern is applied to operator=(T&&) to prevent
  // similar issues with move-assignment from Value lvalues.
  //
  // value::Value val1(42);
  // value::Value val2;
  // val2 = val1;  // Uses operator=(const Value&), not operator=(T&&)
  //

  // ============================================================================

}

