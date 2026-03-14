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

  // Test dedup array sorting with index remap
  {
    value::TimeSamples ts;

    // Build nearly-sorted samples so insertion sort would be chosen without
    // dedup-aware fallback.
    for (size_t i = 0; i < 25; ++i) {
      float v = static_cast<float>(i);
      TEST_CHECK(ts.add_array_sample<float>(static_cast<double>(i), &v, 1));
    }

    // Reference an index that will shift after sorting.
    TEST_CHECK(ts.add_dedup_array_sample<float>(12.5, 20));

    std::vector<float> out;
    TEST_CHECK(ts.get_vector_at_time<float>(12.5, &out));
    TEST_CHECK(out.size() == 1);
    if (out.size() == 1) {
      TEST_CHECK(math::is_close(out[0], 20.0f));
    }
  }

  // Unified small-binary storage should keep values aligned with sorted times.
  {
    value::TimeSamples ts;

    TEST_CHECK(ts.add_sample<float>(5.0, 50.0f));
    TEST_CHECK(ts.add_blocked_sample<float>(3.0));
    TEST_CHECK(ts.add_sample<float>(1.0, 10.0f));
    TEST_CHECK(ts.add_sample<float>(4.0, 40.0f));

    const auto& samples = ts.get_samples();
    TEST_CHECK(samples.size() == 4);
    TEST_CHECK(math::is_close(samples[0].t, 1.0));
    TEST_CHECK(math::is_close(samples[1].t, 3.0));
    TEST_CHECK(math::is_close(samples[2].t, 4.0));
    TEST_CHECK(math::is_close(samples[3].t, 5.0));
    TEST_CHECK(samples[1].blocked == true);

    const float* v0 = samples[0].value.as<float>();
    const float* v2 = samples[2].value.as<float>();
    const float* v3 = samples[3].value.as<float>();
    TEST_CHECK(v0 != nullptr);
    TEST_CHECK(v2 != nullptr);
    TEST_CHECK(v3 != nullptr);
    if (v0) TEST_CHECK(math::is_close(*v0, 10.0f));
    if (v2) TEST_CHECK(math::is_close(*v2, 40.0f));
    if (v3) TEST_CHECK(math::is_close(*v3, 50.0f));

    TEST_CHECK(ts.has_sample_at(4.0));
    value::TimeSamples::Sample* sample = nullptr;
    TEST_CHECK(ts.get_sample_at(4.0, &sample));
    TEST_CHECK(sample != nullptr);
    if (sample) {
      const float* sample_value = sample->value.as<float>();
      TEST_CHECK(sample_value != nullptr);
      if (sample_value) TEST_CHECK(math::is_close(*sample_value, 40.0f));
    }

    auto value_opt = ts.get_value(2);
    TEST_CHECK(value_opt.has_value());
    if (value_opt.has_value()) {
      const float* sample_value = value_opt.value().as<float>();
      TEST_CHECK(sample_value != nullptr);
      if (sample_value) TEST_CHECK(math::is_close(*sample_value, 40.0f));
    }

    // Reconstruct once, append again, and verify the cache is invalidated.
    TEST_CHECK(ts.add_sample<float>(2.0, 20.0f));
    const auto& updated = ts.get_samples();
    TEST_CHECK(updated.size() == 5);
    TEST_CHECK(math::is_close(updated[0].t, 1.0));
    TEST_CHECK(math::is_close(updated[1].t, 2.0));
    const float* v1 = updated[1].value.as<float>();
    TEST_CHECK(v1 != nullptr);
    if (v1) TEST_CHECK(math::is_close(*v1, 20.0f));
  }

  // Blocked-only generic samples should allow deferred type initialization.
  {
    value::TimeSamples ts;
    std::string err;

    TEST_CHECK(ts.add_blocked_sample(5.0, value::Value(), &err));
    TEST_CHECK(ts.type_id() == 0);
    TEST_CHECK(ts.add_sample(10.0, value::Value(42.0f), &err));
    TEST_CHECK(ts.type_id() == value::TypeTraits<float>::type_id());

    const auto &samples = ts.get_samples();
    TEST_CHECK(samples.size() == 2);
    if (samples.size() == 2) {
      TEST_CHECK(samples[0].blocked == true);
      TEST_CHECK(samples[1].blocked == false);
      const float *v = samples[1].value.as<float>();
      TEST_CHECK(v != nullptr);
      if (v) TEST_CHECK(math::is_close(*v, 42.0f));
    }
  }

  // Scalar and array unified backends must not be mixed.
  {
    value::TimeSamples ts;
    std::string err;

    TEST_CHECK(ts.add_sample<float>(0.0, 1.0f, &err));
    TEST_CHECK(!ts.add_array_sample<float>(1.0, std::vector<float>{1.0f, 2.0f}, &err));
    TEST_CHECK(!err.empty());
  }

  // Array layout mismatches must fail instead of silently reinterpreting storage.
  {
    value::TimeSamples ts;
    std::string err;

    TEST_CHECK(ts.add_array_sample<float>(0.0, std::vector<float>{1.0f, 2.0f}, &err));
    TypedArray<float> typed = {3.0f, 4.0f};
    TEST_CHECK(!ts.add_array_sample<float>(1.0, typed, &err));
    TEST_CHECK(!err.empty());
  }

  // Variable-sized unified arrays should preserve per-sample element counts.
  {
    value::TimeSamples ts;

    TEST_CHECK(ts.add_array_sample<float>(5.0, std::vector<float>{5.0f, 6.0f, 7.0f, 8.0f}));
    TEST_CHECK(ts.add_array_sample<float>(1.0, std::vector<float>{1.0f, 2.0f}));
    TEST_CHECK(ts.add_array_sample<float>(3.0, std::vector<float>{3.0f, 4.0f, 5.0f}));

    (void)ts.get_time(0);

    TEST_CHECK(ts.get_array_size() == 0);
    TEST_CHECK(ts.get_array_count(0) == 2);
    TEST_CHECK(ts.get_array_count(1) == 3);
    TEST_CHECK(ts.get_array_count(2) == 4);

    std::vector<float> out;
    TEST_CHECK(ts.get_vector_at_time<float>(1.0, &out));
    TEST_CHECK(out.size() == 2);
    if (out.size() == 2) {
      TEST_CHECK(math::is_close(out[0], 1.0f));
      TEST_CHECK(math::is_close(out[1], 2.0f));
    }

    TEST_CHECK(ts.get_vector_at_time<float>(3.0, &out));
    TEST_CHECK(out.size() == 3);
    if (out.size() == 3) {
      TEST_CHECK(math::is_close(out[0], 3.0f));
      TEST_CHECK(math::is_close(out[2], 5.0f));
    }

    TEST_CHECK(ts.get_vector_at_time<float>(5.0, &out));
    TEST_CHECK(out.size() == 4);
    if (out.size() == 4) {
      TEST_CHECK(math::is_close(out[0], 5.0f));
      TEST_CHECK(math::is_close(out[3], 8.0f));
    }

    const auto& samples = ts.get_samples();
    TEST_CHECK(samples.size() == 3);
    if (samples.size() == 3) {
      const auto* first = samples[0].value.as<std::vector<float>>();
      const auto* last = samples[2].value.as<std::vector<float>>();
      TEST_CHECK(first != nullptr);
      TEST_CHECK(last != nullptr);
      if (first) {
        TEST_CHECK(first->size() == 2);
        if (first->size() == 2) {
          TEST_CHECK(math::is_close((*first)[0], 1.0f));
          TEST_CHECK(math::is_close((*first)[1], 2.0f));
        }
      }
      if (last) {
        TEST_CHECK(last->size() == 4);
        if (last->size() == 4) {
          TEST_CHECK(math::is_close((*last)[0], 5.0f));
          TEST_CHECK(math::is_close((*last)[3], 8.0f));
        }
      }
    }

    auto value_opt = ts.get_value(1);
    TEST_CHECK(value_opt.has_value());
    if (value_opt.has_value()) {
      const auto* middle = value_opt.value().as<std::vector<float>>();
      TEST_CHECK(middle != nullptr);
      if (middle) {
        TEST_CHECK(middle->size() == 3);
        if (middle->size() == 3) {
          TEST_CHECK(math::is_close((*middle)[0], 3.0f));
          TEST_CHECK(math::is_close((*middle)[2], 5.0f));
        }
      }
    }
  }

  // Out-of-order deduplicated arrays must preserve remapped counts after sorting.
  {
    value::TimeSamples ts;

    TEST_CHECK(ts.add_array_sample<float>(5.0, std::vector<float>{5.0f, 6.0f, 7.0f}));
    TEST_CHECK(ts.add_array_sample<float>(1.0, std::vector<float>{1.0f}));
    TEST_CHECK(ts.add_dedup_array_sample<float>(3.0, 0));

    const auto &samples = ts.get_samples();
    TEST_CHECK(samples.size() == 3);
    TEST_CHECK(ts.get_array_count(0) == 1);
    TEST_CHECK(ts.get_array_count(1) == 3);
    TEST_CHECK(ts.get_array_count(2) == 3);

    std::vector<float> out;
    TEST_CHECK(ts.get_vector_at_time<float>(3.0, &out));
    TEST_CHECK(out.size() == 3);
    if (out.size() == 3) {
      TEST_CHECK(math::is_close(out[0], 5.0f));
      TEST_CHECK(math::is_close(out[2], 7.0f));
    }
  }

  // Generic blocked and non-blocked array samples should preserve array values and block state.
  {
    value::TimeSamples ts;
    std::string err;

    TEST_CHECK(ts.add_sample(3.0, value::Value(std::vector<float>{3.0f, 4.0f}), &err));
    TEST_CHECK(ts.add_blocked_sample(1.0, value::Value(std::vector<float>{}), &err));
    TEST_CHECK(ts.add_sample(5.0, value::Value(std::vector<float>{5.0f, 6.0f, 7.0f}), &err));

    const auto &samples = ts.get_samples();
    TEST_CHECK(samples.size() == 3);
    if (samples.size() == 3) {
      TEST_CHECK(samples[0].blocked == true);
      TEST_CHECK(samples[1].blocked == false);
      TEST_CHECK(samples[2].blocked == false);
    }

    std::vector<float> out;
    bool blocked = false;
    TEST_CHECK(!ts.get_vector_at<float>(0, &out, &blocked));
    TEST_CHECK(blocked == true);
    TEST_CHECK(ts.get_vector_at<float>(1, &out, &blocked));
    TEST_CHECK(blocked == false);
    TEST_CHECK(out.size() == 2);
  }

  // TypedArray-backed unified storage should keep the TypedArray type id and counts.
  {
    value::TimeSamples ts;

    TypedArray<float> late = {7.0f, 8.0f};
    TypedArray<float> early = {1.0f, 2.0f, 3.0f};

    TEST_CHECK(ts.add_array_sample<float>(5.0, late));
    TEST_CHECK(ts.add_array_sample<float>(1.0, early));

    TEST_CHECK(ts.is_typed_array());
    TEST_CHECK(!ts.is_stl_array());
    TEST_CHECK(ts.type_id() == value::TypeTraits<TypedArray<float>>::type_id());

    TypedArray<float> out;
    TEST_CHECK(ts.get_typed_array_at_time<float>(1.0, &out));
    TEST_CHECK(out.size() == 3);
    if (out.size() == 3) {
      TEST_CHECK(math::is_close(out[0], 1.0f));
      TEST_CHECK(math::is_close(out[2], 3.0f));
    }

    auto view = ts.get_typed_array_view_at<float>(1);
    TEST_CHECK(view.size() == 2);
    if (view.size() == 2) {
      TEST_CHECK(math::is_close(view[0], 7.0f));
      TEST_CHECK(math::is_close(view[1], 8.0f));
    }

    const auto& samples = ts.get_samples();
    TEST_CHECK(samples.size() == 2);
    if (samples.size() == 2) {
      const auto* first = samples[0].value.as<TypedArray<float>>();
      const auto* second = samples[1].value.as<TypedArray<float>>();
      TEST_CHECK(first != nullptr);
      TEST_CHECK(second != nullptr);
      if (first) {
        TEST_CHECK(first->size() == 3);
        if (first->size() == 3) {
          TEST_CHECK(math::is_close((*first)[0], 1.0f));
          TEST_CHECK(math::is_close((*first)[2], 3.0f));
        }
      }
      if (second) {
        TEST_CHECK(second->size() == 2);
        if (second->size() == 2) {
          TEST_CHECK(math::is_close((*second)[0], 7.0f));
          TEST_CHECK(math::is_close((*second)[1], 8.0f));
        }
      }
    }

    auto value_opt = ts.get_value(0);
    TEST_CHECK(value_opt.has_value());
    if (value_opt.has_value()) {
      const auto* first = value_opt.value().as<TypedArray<float>>();
      TEST_CHECK(first != nullptr);
      if (first) {
        TEST_CHECK(first->size() == 3);
      }
    }
  }

  // Empty TypedArray samples should round-trip through unified reconstruction.
  {
    value::TimeSamples ts;

    TypedArray<float> empty;
    TEST_CHECK(ts.add_array_sample<float>(2.0, empty));
    TEST_CHECK(ts.is_typed_array());
    TEST_CHECK(ts.size() == 1);
    TEST_CHECK(ts.get_array_count(0) == 0);

    TypedArray<float> out;
    bool blocked = true;
    TEST_CHECK(ts.get_typed_array_at_time<float>(2.0, &out, &blocked));
    TEST_CHECK(blocked == false);
    TEST_CHECK(out.size() == 0);

    auto view = ts.get_typed_array_view_at<float>(0);
    TEST_CHECK(view.size() == 0);

    auto value_opt = ts.get_value(0);
    TEST_CHECK(value_opt.has_value());
    if (value_opt.has_value()) {
      const auto *typed = value_opt.value().as<TypedArray<float>>();
      TEST_CHECK(typed != nullptr);
      if (typed) {
        TEST_CHECK(typed->size() == 0);
      }
    }
  }

  // Bool scalars should stay on the generic Value path instead of binary storage.
  {
    value::TimeSamples ts;
    std::string err;

    TEST_CHECK(ts.add_sample<bool>(3.0, true, &err));
    TEST_CHECK(ts.add_blocked_sample<bool>(1.0, &err));
    TEST_CHECK(!ts.is_using_binary_storage());
    TEST_CHECK(ts.type_id() == value::TypeTraits<bool>::type_id());

    bool out = false;
    TEST_CHECK(ts.get(&out, 3.0, value::TimeSampleInterpolationType::Held));
    TEST_CHECK(out == true);

    const auto& samples = ts.get_samples();
    TEST_CHECK(samples.size() == 2);
    if (samples.size() == 2) {
      TEST_CHECK(samples[0].blocked == true);
      TEST_CHECK(samples[1].blocked == false);
      const bool* bool_value = samples[1].value.as<bool>();
      TEST_CHECK(bool_value != nullptr);
      if (bool_value) {
        TEST_CHECK(*bool_value == true);
      }
    }
  }

  // Bool arrays should use the generic Value path and generic dedup.
  {
    value::TimeSamples ts;
    std::string err;
    std::vector<bool> authored = {true, false, true};

    TEST_CHECK(ts.add_array_sample<bool>(1.0, authored, &err));
    TEST_CHECK(ts.add_dedup_sample(3.0, 0, &err));
    TEST_CHECK(!ts.is_using_binary_storage());
    TEST_CHECK(ts.type_id() == value::TypeTraits<std::vector<bool>>::type_id());

    std::vector<bool> out;
    bool blocked = true;
    TEST_CHECK(ts.get_vector_at_time<bool>(1.0, &out, &blocked));
    TEST_CHECK(blocked == false);
    TEST_CHECK(out.size() == 3);
    if (out.size() == 3) {
      TEST_CHECK(out[0] == true);
      TEST_CHECK(out[1] == false);
      TEST_CHECK(out[2] == true);
    }

    TEST_CHECK(ts.get_vector_at_time<bool>(3.0, &out, &blocked));
    TEST_CHECK(blocked == false);
    TEST_CHECK(out.size() == 3);
    if (out.size() == 3) {
      TEST_CHECK(out[0] == true);
      TEST_CHECK(out[1] == false);
      TEST_CHECK(out[2] == true);
    }
  }

  // Value-array storage should sort times and keep per-sample array sizes aligned.
  {
    value::TimeSamples ts;

    TEST_CHECK(ts.add_value_array_sample(5.0, value::Value(std::vector<float>{5.0f, 6.0f, 7.0f, 8.0f})));
    TEST_CHECK(ts.add_value_array_sample(1.0, value::Value(std::vector<float>{1.0f, 2.0f})));
    TEST_CHECK(ts.add_dedup_sample(3.0, 1));

    (void)ts.get_samples();

    TEST_CHECK(ts.get_array_count(0) == 2);
    TEST_CHECK(ts.get_array_count(1) == 2);
    TEST_CHECK(ts.get_array_count(2) == 4);

    std::vector<float> out;
    TEST_CHECK(ts.get_vector_at_time<float>(1.0, &out));
    TEST_CHECK(out.size() == 2);
    TEST_CHECK(ts.get_vector_at_time<float>(3.0, &out));
    TEST_CHECK(out.size() == 2);
    if (out.size() == 2) {
      TEST_CHECK(math::is_close(out[0], 1.0f));
      TEST_CHECK(math::is_close(out[1], 2.0f));
    }
    TEST_CHECK(ts.get_vector_at_time<float>(5.0, &out));
    TEST_CHECK(out.size() == 4);

    const auto& samples = ts.get_samples();
    TEST_CHECK(samples.size() == 3);
    TEST_CHECK(math::is_close(samples[0].t, 1.0));
    TEST_CHECK(math::is_close(samples[1].t, 3.0));
    TEST_CHECK(math::is_close(samples[2].t, 5.0));
  }

  // Direct TypedTimeSamples held lookup should return the previous sample and hold after the end.
  {
    TypedTimeSamples<float> ts;
    ts.add_sample(10.0, 3.0f);
    ts.add_sample(0.0, 1.0f);

    float out = 0.0f;
    TEST_CHECK(ts.get(&out, 5.0, value::TimeSampleInterpolationType::Held));
    TEST_CHECK(math::is_close(out, 1.0f));

    TEST_CHECK(ts.get(&out, 50.0, value::TimeSampleInterpolationType::Held));
    TEST_CHECK(math::is_close(out, 3.0f));
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
