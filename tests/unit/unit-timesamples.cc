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

}