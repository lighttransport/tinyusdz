#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-timesamples.h"
#include "prim-types.hh"
#include "math-util.inc"

using namespace tinyusdz;

void timesamples_test(void) {

  {
    value::token tok1("bora");
    value::token tok2("muda");

    Animatable<value::token> toks;
    toks.add_sample(0, tok1);
    toks.add_sample(10, tok2);

    {
      value::token tok;
      TEST_CHECK(toks.get(value::TimeCode::Default(), &tok));
      // return the value of the first item(= timecode 0)
      TEST_CHECK(tok.str() == "bora");
    }

    // Held interpolation
    {
      value::token tok;
      TEST_CHECK(toks.get(0.0, &tok));
      TEST_CHECK(tok.str() == "bora");

      TEST_CHECK(toks.get(-1.0, &tok));
      TEST_CHECK(tok.str() == "bora");

      TEST_CHECK(toks.get(1.0, &tok));
      TEST_CHECK(tok.str() == "bora");

      TEST_CHECK(toks.get(10.0, &tok));
      TEST_CHECK(tok.str() == "muda");

      TEST_CHECK(toks.get(1000.0, &tok));
      TEST_CHECK(tok.str() == "muda");
    }
  }

  {
    Animatable<float> samples;
    samples.add_sample(0, 0.0f);
    samples.add_sample(1, 10.0f);

    {
      float f;
      TEST_CHECK(samples.get(value::TimeCode::Default(), &f));
      // return the value of the first item(= timecode 0)
      TEST_CHECK(math::is_close(f, 0.0f));
    }

    // Linear interpolation
    {
      float f;
      TEST_CHECK(samples.get(0.0, &f));
      TEST_CHECK(math::is_close(f, 0.0f));

      TEST_CHECK(samples.get(0.5, &f));
      TEST_CHECK(math::is_close(f, 5.0f));

      TEST_CHECK(samples.get(1.0, &f));
      TEST_CHECK(math::is_close(f, 10.0f));

    }
  }

  {
    primvar::PrimVar pbar;
    value::TimeSamples ts;
    ts.add_sample(0, value::Value(0.0f));
    ts.add_sample(1, value::Value(10.0f));
    pbar.set_timesamples(ts);
    pbar.set_value(2000.0f); // default value

    {
      float f;
      TEST_CHECK(pbar.get_interpolated_value(value::TimeCode::Default(), value::TimeSampleInterpolationType::Held, &f));
      // return the value of the first item(= timecode 0)
      TEST_CHECK(math::is_close(f, 2000.0f));
    }

    // Linear interpolation
    {
      float f;
      TEST_CHECK(pbar.get_interpolated_value(-10.0, value::TimeSampleInterpolationType::Linear, &f));
      TEST_CHECK(math::is_close(f, 0.0f));

      TEST_CHECK(pbar.get_interpolated_value(0.0, value::TimeSampleInterpolationType::Linear, &f));
      TEST_CHECK(math::is_close(f, 0.0f));

      TEST_CHECK(pbar.get_interpolated_value(0.5, value::TimeSampleInterpolationType::Linear, &f));
      TEST_CHECK(math::is_close(f, 5.0f));

      TEST_CHECK(pbar.get_interpolated_value(1.0, value::TimeSampleInterpolationType::Linear, &f));
      TEST_CHECK(math::is_close(f, 10.0f));

      TEST_CHECK(pbar.get_interpolated_value(value::TimeCode::Default(), value::TimeSampleInterpolationType::Linear, &f));
      TEST_CHECK(math::is_close(f, 2000.0f));
    }
  }

  {
    primvar::PrimVar pbar;
    value::TimeSamples ts;
    ts.add_sample(0, value::Value(0.0f));
    ts.add_sample(1, value::Value(10.0f));
    pbar.set_timesamples(ts);
    pbar.set_value(2000.0f); // default value

    Attribute attr;
    attr.set_var(pbar);

    {
      float f;
      TEST_CHECK(attr.get(value::TimeCode::Default(), &f, value::TimeSampleInterpolationType::Held));
      // return the value of the first item(= timecode 0)
      TEST_CHECK(math::is_close(f, 2000.0f));
    }

    // Linear interpolation
    {
      float f;
      TEST_CHECK(attr.get(-10.0, &f, value::TimeSampleInterpolationType::Linear));
      TEST_CHECK(math::is_close(f, 0.0f));

      TEST_CHECK(attr.get(0.0, &f, value::TimeSampleInterpolationType::Linear));
      TEST_CHECK(math::is_close(f, 0.0f));

      TEST_CHECK(attr.get(0.5, &f, value::TimeSampleInterpolationType::Linear));
      TEST_CHECK(math::is_close(f, 5.0f));

      TEST_CHECK(attr.get(1.0, &f, value::TimeSampleInterpolationType::Linear));
      TEST_CHECK(math::is_close(f, 10.0f));

      TEST_CHECK(attr.get(value::TimeCode::Default(), &f, value::TimeSampleInterpolationType::Linear));
      TEST_CHECK(math::is_close(f, 2000.0f));
    }
  }

  {
    primvar::PrimVar pbar;
    value::TimeSamples ts;
    std::vector<value::float2> ts0 = {{0.0f, 5.0f}};
    std::vector<value::float2> ts1 = {{10.0f, 15.0f}};

    ts.add_sample(0, ts0);
    ts.add_sample(1, ts1);
    pbar.set_timesamples(ts);
    std::vector<value::float2> default_value = {{100.0f, 200.0f}};
    pbar.set_value(default_value); // default value

    Attribute attr;
    attr.set_var(pbar);

    {
      std::vector<value::float2> v;
      TEST_CHECK(attr.get(value::TimeCode::Default(), &v, value::TimeSampleInterpolationType::Held));
      TEST_CHECK(v.size() == 1);

      TEST_CHECK(math::is_close(v[0][0], 100.0f));
      TEST_CHECK(math::is_close(v[0][1], 200.0f));
    }

    // Linear interpolation
    {
      std::vector<value::float2> vs;
      TEST_CHECK(attr.get(0.0, &vs, value::TimeSampleInterpolationType::Linear));
      TEST_CHECK(vs.size() == 1);
      TEST_CHECK(math::is_close(vs[0][0], 0.0f));
      TEST_CHECK(math::is_close(vs[0][1], 5.0f));

      TEST_CHECK(attr.get(0.5, &vs, value::TimeSampleInterpolationType::Linear));
      TEST_CHECK(vs.size() == 1);
      TEST_CHECK(math::is_close(vs[0][0], 5.0f));
      TEST_CHECK(math::is_close(vs[0][1], 10.0f));

    }
  }

  {
    TEST_CHECK(value::IsLerpSupportedType(value::TypeTraits<value::float2>::type_id()));
    TEST_CHECK(value::IsLerpSupportedType(value::TypeTraits<std::vector<value::float2>>::type_id()));
    TEST_CHECK(value::IsLerpSupportedType(value::TypeTraits<value::texcoord2f>::type_id()));
    TEST_CHECK(value::IsLerpSupportedType(value::TypeTraits<std::vector<value::texcoord2f>>::type_id()));
    TEST_CHECK(!value::IsLerpSupportedType(value::TypeTraits<int>::type_id()));
    TEST_CHECK(!value::IsLerpSupportedType(value::TypeTraits<std::vector<int>>::type_id()));
    TEST_CHECK(!value::IsLerpSupportedType(value::TypeTraits<std::string>::type_id()));
    TEST_CHECK(!value::IsLerpSupportedType(value::TypeTraits<std::vector<std::string>>::type_id()));
  }

  // Test TimeSamples sorting
  {
    value::TimeSamples ts;

    // Add samples out of order
    ts.add_sample(5.0, value::Value(50.0f));
    ts.add_sample(2.0, value::Value(20.0f));
    ts.add_sample(8.0, value::Value(80.0f));
    ts.add_sample(1.0, value::Value(10.0f));

    // Force sorting by accessing samples
    const auto& samples = ts.get_samples();

    // Verify sorted order
    TEST_CHECK(samples.size() == 4);
    TEST_CHECK(math::is_close(samples[0].t, 1.0));
    TEST_CHECK(math::is_close(samples[1].t, 2.0));
    TEST_CHECK(math::is_close(samples[2].t, 5.0));
    TEST_CHECK(math::is_close(samples[3].t, 8.0));

    // Verify values are correctly sorted with times
    {
      const float* f1 = samples[0].value.as<float>();
      TEST_CHECK(f1 != nullptr);
      if (f1) TEST_CHECK(math::is_close(*f1, 10.0f));
    }
    {
      const float* f2 = samples[1].value.as<float>();
      TEST_CHECK(f2 != nullptr);
      if (f2) TEST_CHECK(math::is_close(*f2, 20.0f));
    }
    {
      const float* f3 = samples[2].value.as<float>();
      TEST_CHECK(f3 != nullptr);
      if (f3) TEST_CHECK(math::is_close(*f3, 50.0f));
    }
    {
      const float* f4 = samples[3].value.as<float>();
      TEST_CHECK(f4 != nullptr);
      if (f4) TEST_CHECK(math::is_close(*f4, 80.0f));
    }
  }

  // Test TimeSamples with blocked (None) values
  {
    value::TimeSamples ts;

    // Add mix of regular and blocked samples using ValueBlock
    ts.add_sample(0.0, value::Value(10.0));
    ts.add_sample(1.0, value::Value(20.0));
    ts.add_sample(2.0, value::Value(value::ValueBlock()));  // blocked sample
    ts.add_sample(3.0, value::Value(30.0));
    ts.add_sample(4.0, value::Value(value::ValueBlock()));  // blocked sample
    ts.add_sample(5.0, value::Value(50.0));

    const auto& samples = ts.get_samples();

    TEST_CHECK(samples.size() == 6);

    // Check blocked flags
    TEST_CHECK(samples[0].blocked == false);
    TEST_CHECK(samples[1].blocked == false);
    TEST_CHECK(samples[2].blocked == true);
    TEST_CHECK(samples[3].blocked == false);
    TEST_CHECK(samples[4].blocked == true);
    TEST_CHECK(samples[5].blocked == false);

    // Verify values for non-blocked samples
    {
      const double* v = samples[0].value.as<double>();
      TEST_CHECK(v != nullptr);
      if (v) TEST_CHECK(math::is_close(*v, 10.0));
    }
    {
      const double* v = samples[1].value.as<double>();
      TEST_CHECK(v != nullptr);
      if (v) TEST_CHECK(math::is_close(*v, 20.0));
    }
    {
      const double* v = samples[3].value.as<double>();
      TEST_CHECK(v != nullptr);
      if (v) TEST_CHECK(math::is_close(*v, 30.0));
    }
    {
      const double* v = samples[5].value.as<double>();
      TEST_CHECK(v != nullptr);
      if (v) TEST_CHECK(math::is_close(*v, 50.0));
    }
  }

  // Test sorting with blocked values
  {
    value::TimeSamples ts;

    // Add samples out of order with blocked values
    ts.add_sample(5.0, value::Value(50.0));
    ts.add_sample(2.0, value::Value(value::ValueBlock()));
    ts.add_sample(1.0, value::Value(10.0));
    ts.add_sample(4.0, value::Value(value::ValueBlock()));
    ts.add_sample(3.0, value::Value(30.0));

    const auto& samples = ts.get_samples();

    // Verify sorted order
    TEST_CHECK(samples.size() == 5);
    TEST_CHECK(math::is_close(samples[0].t, 1.0));
    TEST_CHECK(math::is_close(samples[1].t, 2.0));
    TEST_CHECK(math::is_close(samples[2].t, 3.0));
    TEST_CHECK(math::is_close(samples[3].t, 4.0));
    TEST_CHECK(math::is_close(samples[4].t, 5.0));

    // Verify blocked flags after sorting
    TEST_CHECK(samples[0].blocked == false);
    TEST_CHECK(samples[1].blocked == true);
    TEST_CHECK(samples[2].blocked == false);
    TEST_CHECK(samples[3].blocked == true);
    TEST_CHECK(samples[4].blocked == false);
  }

  // Test empty TimeSamples
  {
    value::TimeSamples ts;
    TEST_CHECK(ts.empty() == true);
    TEST_CHECK(ts.size() == 0);

    const auto& samples = ts.get_samples();
    TEST_CHECK(samples.size() == 0);
  }

  // Test single sample
  {
    value::TimeSamples ts;
    ts.add_sample(5.0, value::Value(50.0));

    TEST_CHECK(ts.empty() == false);
    TEST_CHECK(ts.size() == 1);

    const auto& samples = ts.get_samples();
    TEST_CHECK(samples.size() == 1);
    TEST_CHECK(math::is_close(samples[0].t, 5.0));

    const double* v = samples[0].value.as<double>();
    TEST_CHECK(v != nullptr);
    if (v) TEST_CHECK(math::is_close(*v, 50.0));
  }

  // Test has_sample_at and get_sample_at
  {
    value::TimeSamples ts;
    ts.add_sample(1.0, value::Value(10.0f));
    ts.add_sample(2.0, value::Value(20.0f));
    ts.add_sample(3.0, value::Value(30.0f));

    TEST_CHECK(ts.has_sample_at(1.0) == true);
    TEST_CHECK(ts.has_sample_at(2.0) == true);
    TEST_CHECK(ts.has_sample_at(3.0) == true);
    TEST_CHECK(ts.has_sample_at(1.5) == false);
    TEST_CHECK(ts.has_sample_at(4.0) == false);

    value::TimeSamples::Sample* sample = nullptr;
    TEST_CHECK(ts.get_sample_at(2.0, &sample) == true);
    TEST_CHECK(sample != nullptr);
    if (sample) {
      TEST_CHECK(math::is_close(sample->t, 2.0));
      const float* v = sample->value.as<float>();
      TEST_CHECK(v != nullptr);
      if (v) TEST_CHECK(math::is_close(*v, 20.0f));
    }

    TEST_CHECK(ts.get_sample_at(4.0, &sample) == false);
  }

  // Test get_time API
  {
    value::TimeSamples ts;
    ts.add_sample(5.0, value::Value(50.0));
    ts.add_sample(2.0, value::Value(20.0));
    ts.add_sample(8.0, value::Value(80.0));

    // Get times at indices (after sorting)
    auto t0 = ts.get_time(0);
    TEST_CHECK(t0.has_value());
    if (t0.has_value()) {
      TEST_CHECK(math::is_close(t0.value(), 2.0));
    }

    auto t1 = ts.get_time(1);
    TEST_CHECK(t1.has_value());
    if (t1.has_value()) {
      TEST_CHECK(math::is_close(t1.value(), 5.0));
    }

    auto t2 = ts.get_time(2);
    TEST_CHECK(t2.has_value());
    if (t2.has_value()) {
      TEST_CHECK(math::is_close(t2.value(), 8.0));
    }

    // Out of bounds
    auto t3 = ts.get_time(3);
    TEST_CHECK(!t3.has_value());
  }

  // Test PODTimeSamples with underlying_type_id support
  // This tests that PODTimeSamples can get values using role types
  // even when the actual stored type is the underlying type.
  {
    PODTimeSamples samples;

    // Test 1: Store as float3, retrieve as normal3f
    {
      value::float3 v1 = {1.0f, 2.0f, 3.0f};
      value::float3 v2 = {4.0f, 5.0f, 6.0f};

      // Add samples as float3
      std::string err;
      TEST_CHECK(samples.add_sample(1.0, v1, &err));
      TEST_CHECK(samples.add_sample(2.0, v2, &err));

      // Retrieve as normal3f (role type)
      value::normal3f retrieved;
      bool blocked;
      TEST_CHECK(samples.get_value_at(0, &retrieved, &blocked));
      TEST_CHECK(!blocked);

      // Verify values
      TEST_CHECK(math::is_close(retrieved[0], 1.0f));
      TEST_CHECK(math::is_close(retrieved[1], 2.0f));
      TEST_CHECK(math::is_close(retrieved[2], 3.0f));

      // Get second value
      TEST_CHECK(samples.get_value_at(1, &retrieved, &blocked));
      TEST_CHECK(math::is_close(retrieved[0], 4.0f));
      TEST_CHECK(math::is_close(retrieved[1], 5.0f));
      TEST_CHECK(math::is_close(retrieved[2], 6.0f));
    }

    samples.clear();

    // Test 2: Store as normal3f, retrieve as float3
    {
      value::normal3f n1 = {0.577f, 0.577f, 0.577f};
      value::normal3f n2 = {1.0f, 0.0f, 0.0f};

      // Add samples as normal3f (will be stored as underlying float3)
      std::string err;
      TEST_CHECK(samples.add_sample(1.0, n1, &err));
      TEST_CHECK(samples.add_sample(2.0, n2, &err));

      // Retrieve as float3 (underlying type)
      value::float3 retrieved;
      bool blocked;
      TEST_CHECK(samples.get_value_at(0, &retrieved, &blocked));
      TEST_CHECK(!blocked);

      // Verify values
      TEST_CHECK(math::is_close(retrieved[0], 0.577f, 1e-3f));
      TEST_CHECK(math::is_close(retrieved[1], 0.577f, 1e-3f));
      TEST_CHECK(math::is_close(retrieved[2], 0.577f, 1e-3f));
    }

    samples.clear();

    // Test 3: Mixed role types with same underlying type
    {
      value::point3f p1 = {100.0f, 200.0f, 300.0f};

      // Add sample as point3f
      std::string err;
      TEST_CHECK(samples.add_sample(1.0, p1, &err));

      // Retrieve as color3f (different role, same underlying type)
      value::color3f color_retrieved;
      bool blocked;
      TEST_CHECK(samples.get_value_at(0, &color_retrieved, &blocked));

      // Verify values
      TEST_CHECK(math::is_close(color_retrieved[0], 100.0f));
      TEST_CHECK(math::is_close(color_retrieved[1], 200.0f));
      TEST_CHECK(math::is_close(color_retrieved[2], 300.0f));

      // Also retrieve as vector3f
      value::vector3f vec_retrieved;
      TEST_CHECK(samples.get_value_at(0, &vec_retrieved, &blocked));

      TEST_CHECK(math::is_close(vec_retrieved[0], 100.0f));
      TEST_CHECK(math::is_close(vec_retrieved[1], 200.0f));
      TEST_CHECK(math::is_close(vec_retrieved[2], 300.0f));
    }

    samples.clear();

    // Test 4: get_value_at_time with role types
    {
      value::float3 v1 = {1.0f, 1.0f, 1.0f};
      value::float3 v2 = {2.0f, 2.0f, 2.0f};

      // Add samples
      std::string err;
      TEST_CHECK(samples.add_sample(10.0, v1, &err));
      TEST_CHECK(samples.add_sample(20.0, v2, &err));

      // Get value at specific time as normal3f
      value::normal3f retrieved;
      bool blocked;
      TEST_CHECK(samples.get_value_at_time(10.0, &retrieved, &blocked));
      TEST_CHECK(math::is_close(retrieved[0], 1.0f));

      TEST_CHECK(samples.get_value_at_time(20.0, &retrieved, &blocked));
      TEST_CHECK(math::is_close(retrieved[0], 2.0f));
    }

    samples.clear();

    // Test 5: Blocked samples with role types
    {
      value::float3 v1 = {1.0f, 1.0f, 1.0f};

      // Add regular sample and blocked sample
      std::string err;
      TEST_CHECK(samples.add_sample(1.0, v1, &err));
      TEST_CHECK(samples.add_blocked_sample<value::float3>(2.0, &err));

      // Get blocked sample as normal3f
      value::normal3f retrieved;
      bool blocked;
      TEST_CHECK(samples.get_value_at(1, &retrieved, &blocked));
      TEST_CHECK(blocked);

      // Blocked values should be default-initialized
      TEST_CHECK(retrieved[0] == 0.0f);
      TEST_CHECK(retrieved[1] == 0.0f);
      TEST_CHECK(retrieved[2] == 0.0f);
    }

    samples.clear();

    // Test 6: Type consistency with multiple role types
    {
      value::float3 v1 = {1.0f, 1.0f, 1.0f};
      value::normal3f n1 = {2.0f, 2.0f, 2.0f};
      value::color3f c1 = {3.0f, 3.0f, 3.0f};

      // Add samples with different role types but same underlying type
      std::string err;
      TEST_CHECK(samples.add_sample(1.0, v1, &err));
      TEST_CHECK(samples.add_sample(2.0, n1, &err));
      TEST_CHECK(samples.add_sample(3.0, c1, &err));

      // Verify we can retrieve all as any of the role types
      value::point3f retrieved;
      TEST_CHECK(samples.get_value_at(0, &retrieved, nullptr));
      TEST_CHECK(math::is_close(retrieved[0], 1.0f));

      TEST_CHECK(samples.get_value_at(1, &retrieved, nullptr));
      TEST_CHECK(math::is_close(retrieved[0], 2.0f));

      TEST_CHECK(samples.get_value_at(2, &retrieved, nullptr));
      TEST_CHECK(math::is_close(retrieved[0], 3.0f));
    }

    samples.clear();

    // Test 7: Test with double3 and related role types
    {
      value::double3 d1 = {1.0, 2.0, 3.0};
      value::normal3d n1 = {4.0, 5.0, 6.0};

      std::string err;
      TEST_CHECK(samples.add_sample(1.0, d1, &err));
      TEST_CHECK(samples.add_sample(2.0, n1, &err));

      // Retrieve double3 as normal3d
      value::normal3d retrieved_n;
      TEST_CHECK(samples.get_value_at(0, &retrieved_n, nullptr));
      TEST_CHECK(math::is_close(retrieved_n[0], 1.0));

      // Retrieve normal3d as double3
      value::double3 retrieved_d;
      TEST_CHECK(samples.get_value_at(1, &retrieved_d, nullptr));
      TEST_CHECK(math::is_close(retrieved_d[0], 4.0));
    }
  }


  // Test duplicate time entries (std::stable_sort preserves order)
  {
    value::TimeSamples ts;
    ts.add_sample(1.0, value::Value(10.0f));
    ts.add_sample(2.0, value::Value(20.0f));
    ts.add_sample(1.0, value::Value(15.0f)); // Duplicate time

    const auto& samples = ts.get_samples();
    TEST_CHECK(samples.size() == 3);
    TEST_CHECK(math::is_close(samples[0].t, 1.0));
    TEST_CHECK(math::is_close(samples[1].t, 1.0));
    TEST_CHECK(math::is_close(samples[2].t, 2.0));
    const float* v0 = samples[0].value.as<float>();
    const float* v1 = samples[1].value.as<float>();
    TEST_CHECK(v0 != nullptr);
    TEST_CHECK(v1 != nullptr);
    if (v0) TEST_CHECK(math::is_close(*v0, 10.0f));
    if (v1) TEST_CHECK(math::is_close(*v1, 15.0f));
  }

  // Test interpolation of arrays with different sizes
  {
    primvar::PrimVar pvar;
    value::TimeSamples ts;
    std::vector<float> v1 = {1.0f, 2.0f};
    std::vector<float> v2 = {3.0f, 4.0f, 5.0f};
    ts.add_sample(0.0, value::Value(v1));
    ts.add_sample(1.0, value::Value(v2));
    pvar.set_timesamples(ts);

    value::Value result_val;
    // Linear interpolation should fail because array sizes are different,
    // and it should return the value of the lower sample (held interpolation).
    TEST_CHECK(pvar.get_interpolated_value(0.5, value::TimeSampleInterpolationType::Linear, &result_val) == true);
    const std::vector<float> *result = result_val.as<std::vector<float>>();
    TEST_CHECK(result != nullptr);
    if (result) {
        TEST_CHECK(result->size() == 2);
        TEST_CHECK(math::is_close((*result)[0], 1.0f));
        TEST_CHECK(math::is_close((*result)[1], 2.0f));
    }
  }

  // ==========================================================================
  // OpenUSD Behavior Compatibility Tests
  // ==========================================================================
  // These tests ensure TinyUSDZ matches OpenUSD's timeSamples evaluation behavior

  // Test 1: Single TimeSample Behavior (should be held constant for all times)
  {
    primvar::PrimVar pvar;
    value::TimeSamples ts;
    value::float3 scale_value = {0.1f, 0.2f, 0.3f};
    ts.add_sample(0.0, value::Value(scale_value));
    pvar.set_timesamples(ts);

    value::float3 result;

    // Test before the sample (t = -10)
    TEST_CHECK(pvar.get_interpolated_value(-10.0, value::TimeSampleInterpolationType::Linear, &result));
    TEST_CHECK(math::is_close(result[0], 0.1f));
    TEST_CHECK(math::is_close(result[1], 0.2f));
    TEST_CHECK(math::is_close(result[2], 0.3f));

    // Test at the sample (t = 0)
    TEST_CHECK(pvar.get_interpolated_value(0.0, value::TimeSampleInterpolationType::Linear, &result));
    TEST_CHECK(math::is_close(result[0], 0.1f));
    TEST_CHECK(math::is_close(result[1], 0.2f));
    TEST_CHECK(math::is_close(result[2], 0.3f));

    // Test after the sample (t = 10)
    TEST_CHECK(pvar.get_interpolated_value(10.0, value::TimeSampleInterpolationType::Linear, &result));
    TEST_CHECK(math::is_close(result[0], 0.1f));
    TEST_CHECK(math::is_close(result[1], 0.2f));
    TEST_CHECK(math::is_close(result[2], 0.3f));
  }

  // Test 2: Default Value vs TimeSamples Coexistence
  // Default value should be returned for Default TimeCode,
  // TimeSamples should be used for numeric time codes
  {
    primvar::PrimVar pvar;
    value::TimeSamples ts;
    value::float3 sample_value = {0.1f, 0.2f, 0.3f};
    value::float3 default_value = {7.0f, 8.0f, 9.0f};

    ts.add_sample(0.0, value::Value(sample_value));
    pvar.set_timesamples(ts);
    pvar.set_value(default_value);  // Set default value

    value::float3 result;

    // Test Default TimeCode - should return default value (7, 8, 9)
    TEST_CHECK(pvar.get_interpolated_value(value::TimeCode::Default(), value::TimeSampleInterpolationType::Linear, &result));
    TEST_CHECK(math::is_close(result[0], 7.0f));
    TEST_CHECK(math::is_close(result[1], 8.0f));
    TEST_CHECK(math::is_close(result[2], 9.0f));

    // Test numeric time codes - should use time samples
    TEST_CHECK(pvar.get_interpolated_value(-10.0, value::TimeSampleInterpolationType::Linear, &result));
    TEST_CHECK(math::is_close(result[0], 0.1f));
    TEST_CHECK(math::is_close(result[1], 0.2f));
    TEST_CHECK(math::is_close(result[2], 0.3f));

    TEST_CHECK(pvar.get_interpolated_value(0.0, value::TimeSampleInterpolationType::Linear, &result));
    TEST_CHECK(math::is_close(result[0], 0.1f));
    TEST_CHECK(math::is_close(result[1], 0.2f));
    TEST_CHECK(math::is_close(result[2], 0.3f));

    TEST_CHECK(pvar.get_interpolated_value(10.0, value::TimeSampleInterpolationType::Linear, &result));
    TEST_CHECK(math::is_close(result[0], 0.1f));
    TEST_CHECK(math::is_close(result[1], 0.2f));
    TEST_CHECK(math::is_close(result[2], 0.3f));
  }

  // Test 3: Multiple TimeSamples with Linear Interpolation
  {
    primvar::PrimVar pvar;
    value::TimeSamples ts;
    value::float3 sample1 = {0.1f, 0.1f, 0.1f};
    value::float3 sample2 = {0.5f, 0.5f, 0.5f};
    value::float3 sample3 = {1.0f, 1.0f, 1.0f};

    ts.add_sample(-5.0, value::Value(sample1));
    ts.add_sample(0.0, value::Value(sample2));
    ts.add_sample(5.0, value::Value(sample3));
    pvar.set_timesamples(ts);

    value::float3 result;

    // Test before first sample (t = -10) - should hold first value
    TEST_CHECK(pvar.get_interpolated_value(-10.0, value::TimeSampleInterpolationType::Linear, &result));
    TEST_CHECK(math::is_close(result[0], 0.1f));
    TEST_CHECK(math::is_close(result[1], 0.1f));
    TEST_CHECK(math::is_close(result[2], 0.1f));

    // Test at first sample (t = -5)
    TEST_CHECK(pvar.get_interpolated_value(-5.0, value::TimeSampleInterpolationType::Linear, &result));
    TEST_CHECK(math::is_close(result[0], 0.1f));

    // Test between samples (t = -2.5) - should interpolate
    TEST_CHECK(pvar.get_interpolated_value(-2.5, value::TimeSampleInterpolationType::Linear, &result));
    TEST_CHECK(math::is_close(result[0], 0.3f));  // Linear interpolation between 0.1 and 0.5
    TEST_CHECK(math::is_close(result[1], 0.3f));
    TEST_CHECK(math::is_close(result[2], 0.3f));

    // Test at middle sample (t = 0)
    TEST_CHECK(pvar.get_interpolated_value(0.0, value::TimeSampleInterpolationType::Linear, &result));
    TEST_CHECK(math::is_close(result[0], 0.5f));

    // Test between samples (t = 2.5) - should interpolate
    TEST_CHECK(pvar.get_interpolated_value(2.5, value::TimeSampleInterpolationType::Linear, &result));
    TEST_CHECK(math::is_close(result[0], 0.75f));  // Linear interpolation between 0.5 and 1.0
    TEST_CHECK(math::is_close(result[1], 0.75f));
    TEST_CHECK(math::is_close(result[2], 0.75f));

    // Test at last sample (t = 5)
    TEST_CHECK(pvar.get_interpolated_value(5.0, value::TimeSampleInterpolationType::Linear, &result));
    TEST_CHECK(math::is_close(result[0], 1.0f));

    // Test after last sample (t = 10) - should hold last value
    TEST_CHECK(pvar.get_interpolated_value(10.0, value::TimeSampleInterpolationType::Linear, &result));
    TEST_CHECK(math::is_close(result[0], 1.0f));
    TEST_CHECK(math::is_close(result[1], 1.0f));
    TEST_CHECK(math::is_close(result[2], 1.0f));
  }

  // Test 4: Attribute::get() with Default Value and TimeSamples
  {
    primvar::PrimVar pvar;
    value::TimeSamples ts;
    value::float3 sample1 = {0.1f, 0.1f, 0.1f};
    value::float3 sample2 = {0.5f, 0.5f, 0.5f};
    value::float3 sample3 = {1.0f, 1.0f, 1.0f};
    value::float3 default_value = {7.0f, 8.0f, 9.0f};

    ts.add_sample(-5.0, value::Value(sample1));
    ts.add_sample(0.0, value::Value(sample2));
    ts.add_sample(5.0, value::Value(sample3));
    pvar.set_timesamples(ts);
    pvar.set_value(default_value);

    Attribute attr;
    attr.set_var(pvar);

    value::float3 result;

    // Test Default TimeCode via Attribute::get()
    TEST_CHECK(attr.get(value::TimeCode::Default(), &result, value::TimeSampleInterpolationType::Linear));
    TEST_CHECK(math::is_close(result[0], 7.0f));
    TEST_CHECK(math::is_close(result[1], 8.0f));
    TEST_CHECK(math::is_close(result[2], 9.0f));

    // Test numeric time codes via Attribute::get()
    TEST_CHECK(attr.get(-10.0, &result, value::TimeSampleInterpolationType::Linear));
    TEST_CHECK(math::is_close(result[0], 0.1f));  // Before samples, held constant

    TEST_CHECK(attr.get(-2.5, &result, value::TimeSampleInterpolationType::Linear));
    TEST_CHECK(math::is_close(result[0], 0.3f));  // Interpolated

    TEST_CHECK(attr.get(0.0, &result, value::TimeSampleInterpolationType::Linear));
    TEST_CHECK(math::is_close(result[0], 0.5f));  // At sample

    TEST_CHECK(attr.get(2.5, &result, value::TimeSampleInterpolationType::Linear));
    TEST_CHECK(math::is_close(result[0], 0.75f));  // Interpolated

    TEST_CHECK(attr.get(10.0, &result, value::TimeSampleInterpolationType::Linear));
    TEST_CHECK(math::is_close(result[0], 1.0f));  // After samples, held constant
  }

  // Test 5: Default Value Only (no time samples)
  {
    primvar::PrimVar pvar;
    value::float3 default_value = {7.0f, 8.0f, 9.0f};
    pvar.set_value(default_value);  // Only default value, no time samples

    Attribute attr;
    attr.set_var(pvar);

    value::float3 result;

    // All time codes should return the default value when no time samples exist
    TEST_CHECK(attr.get(value::TimeCode::Default(), &result, value::TimeSampleInterpolationType::Linear));
    TEST_CHECK(math::is_close(result[0], 7.0f));
    TEST_CHECK(math::is_close(result[1], 8.0f));
    TEST_CHECK(math::is_close(result[2], 9.0f));

    TEST_CHECK(attr.get(-10.0, &result, value::TimeSampleInterpolationType::Linear));
    TEST_CHECK(math::is_close(result[0], 7.0f));
    TEST_CHECK(math::is_close(result[1], 8.0f));
    TEST_CHECK(math::is_close(result[2], 9.0f));

    TEST_CHECK(attr.get(0.0, &result, value::TimeSampleInterpolationType::Linear));
    TEST_CHECK(math::is_close(result[0], 7.0f));
    TEST_CHECK(math::is_close(result[1], 8.0f));
    TEST_CHECK(math::is_close(result[2], 9.0f));

    TEST_CHECK(attr.get(10.0, &result, value::TimeSampleInterpolationType::Linear));
    TEST_CHECK(math::is_close(result[0], 7.0f));
    TEST_CHECK(math::is_close(result[1], 8.0f));
    TEST_CHECK(math::is_close(result[2], 9.0f));
  }

  // Test 6: Held Interpolation Mode
  {
    primvar::PrimVar pvar;
    value::TimeSamples ts;
    value::float3 sample1 = {1.0f, 1.0f, 1.0f};
    value::float3 sample2 = {2.0f, 2.0f, 2.0f};
    value::float3 sample3 = {3.0f, 3.0f, 3.0f};

    ts.add_sample(0.0, value::Value(sample1));
    ts.add_sample(5.0, value::Value(sample2));
    ts.add_sample(10.0, value::Value(sample3));
    pvar.set_timesamples(ts);

    value::float3 result;

    // With Held interpolation, values between samples should hold the earlier sample
    TEST_CHECK(pvar.get_interpolated_value(2.5, value::TimeSampleInterpolationType::Held, &result));
    TEST_CHECK(math::is_close(result[0], 1.0f));  // Should hold earlier value

    TEST_CHECK(pvar.get_interpolated_value(7.5, value::TimeSampleInterpolationType::Held, &result));
    TEST_CHECK(math::is_close(result[0], 2.0f));  // Should hold earlier value

    // At exact sample times
    TEST_CHECK(pvar.get_interpolated_value(5.0, value::TimeSampleInterpolationType::Held, &result));
    TEST_CHECK(math::is_close(result[0], 2.0f));  // Exact value at sample
  }

  // Test 7: Edge Cases - Empty TimeSamples with Default Value
  {
    primvar::PrimVar pvar;
    value::float3 default_value = {100.0f, 200.0f, 300.0f};
    pvar.set_value(default_value);  // Default value only

    // TimeSamples exist but are empty
    value::TimeSamples ts;
    pvar.set_timesamples(ts);

    value::float3 result;

    // Should still return default value for all time codes
    TEST_CHECK(pvar.get_interpolated_value(value::TimeCode::Default(), value::TimeSampleInterpolationType::Linear, &result));
    TEST_CHECK(math::is_close(result[0], 100.0f));
    TEST_CHECK(math::is_close(result[1], 200.0f));
    TEST_CHECK(math::is_close(result[2], 300.0f));

    TEST_CHECK(pvar.get_interpolated_value(0.0, value::TimeSampleInterpolationType::Linear, &result));
    TEST_CHECK(math::is_close(result[0], 100.0f));
  }

  // Test 8: Test Boundary Conditions with Epsilon Values
  {
    primvar::PrimVar pvar;
    value::TimeSamples ts;
    ts.add_sample(0.0, value::Value(10.0f));
    ts.add_sample(1.0, value::Value(20.0f));
    pvar.set_timesamples(ts);

    float result;
    const float epsilon = 1e-6f;

    // Just before and after samples
    TEST_CHECK(pvar.get_interpolated_value(0.0 - epsilon, value::TimeSampleInterpolationType::Linear, &result));
    TEST_CHECK(math::is_close(result, 10.0f, 1e-3f));  // Should be very close to first sample

    TEST_CHECK(pvar.get_interpolated_value(1.0 + epsilon, value::TimeSampleInterpolationType::Linear, &result));
    TEST_CHECK(math::is_close(result, 20.0f, 1e-3f));  // Should be very close to last sample
  }
}