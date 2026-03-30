#include <unistd.h>
#include "ubench.h"

#include "value-types.hh"
#include "core/prim.hh"
#include "timesamples.hh"
#include "usdGeom.hh"

using namespace tinyusdz;

namespace {

value::TimeSamples BuildDescendingSmallScalarSamples(size_t sample_count) {
  value::TimeSamples ts;
  for (size_t i = 0; i < sample_count; ++i) {
    const double t = static_cast<double>(sample_count - i);
    ts.add_sample<float>(t, static_cast<float>(t));
  }
  return ts;
}

value::TimeSamples BuildVariableArraySamples(size_t sample_count) {
  value::TimeSamples ts;
  for (size_t i = 0; i < sample_count; ++i) {
    const size_t width = 1 + (i % 8);
    std::vector<float> values(width);
    for (size_t j = 0; j < width; ++j) {
      values[j] = static_cast<float>((i * 10) + j);
    }
    ts.add_array_sample<float>(static_cast<double>(i), values);
  }
  return ts;
}

value::TimeSamples BuildTypedArraySamples(size_t sample_count, size_t width) {
  value::TimeSamples ts;
  for (size_t i = 0; i < sample_count; ++i) {
    TypedArray<float> values;
    values.resize(width);
    for (size_t j = 0; j < width; ++j) {
      values[j] = static_cast<float>((i * width) + j);
    }
    ts.add_array_sample<float>(static_cast<double>(i), values);
  }
  return ts;
}

const value::TimeSamples &GetVariableArrayFixture() {
  static const value::TimeSamples fixture = BuildVariableArraySamples(1024);
  return fixture;
}

const value::TimeSamples &GetTypedArrayFixture() {
  static const value::TimeSamples fixture = BuildTypedArraySamples(512, 32);
  return fixture;
}

}  // namespace

UBENCH(perf, vector_double_push_back_10M)
{
  std::vector<double> v;
  constexpr size_t niter = 10 * 10000;
  for (size_t i = 0; i < niter; i++) {
    v.push_back(double(i));
  }
}

UBENCH(perf, any_value_double_10M)
{
  constexpr size_t niter = 10 * 10000;
  for (size_t i = 0; i < niter; i++) {
    value::any_value a;
    a = double(i);
  }
}

UBENCH(perf, thelink2012_any_float_10M)
{
  constexpr size_t niter = 10 * 10000;
  for (size_t i = 0; i < niter; i++) {
    value::any_value a;
    a = float(i);
  }
}

UBENCH(perf, thelink2012_any_double_10M)
{
  constexpr size_t niter = 10 * 10000;

  std::vector<value::any_value> v;

  for (size_t i = 0; i < niter; i++) {
    v.push_back(double(i));
  }
}

UBENCH(perf, any_value_100M)
{
  constexpr size_t niter = 100 * 10000;
  for (size_t i = 0; i < niter; i++) {
    tinyusdz::value::Value a;
    a = i;
  }
}

UBENCH(perf, timesamples_double_10M)
{
  constexpr size_t ns = 10 * 10000;

  tinyusdz::TypedTimeSamples<double> ts;

  for (size_t i = 0; i < ns; i++) {
    ts.add_sample(double(i), double(i));
  }
}

UBENCH(timesamples, descending_small_scalar_sort_4k)
{
  auto ts = BuildDescendingSmallScalarSamples(4096);
  const auto &samples = ts.get_samples();
  UBENCH_DO_NOTHING((void *)samples.data());
}

UBENCH(timesamples, variable_array_lookup_1k)
{
  const auto &ts = GetVariableArrayFixture();
  std::vector<float> out;

  for (size_t i = 0; i < 1024; i += 7) {
    ts.get_vector_at_time<float>(static_cast<double>(i), &out);
    UBENCH_DO_NOTHING(out.empty() ? nullptr : static_cast<void *>(out.data()));
  }
}

UBENCH(timesamples, typed_array_reconstruct_512)
{
  auto ts = GetTypedArrayFixture();
  const auto &samples = ts.get_samples();

  for (const auto &sample : samples) {
    const auto *typed = sample.value.as<TypedArray<float>>();
    UBENCH_DO_NOTHING(typed ? const_cast<float *>(typed->data()) : nullptr);
  }
}

UBENCH(perf, gprim_10M)
{
  constexpr size_t niter = 10 * 10000;
  std::vector<value::Value> prims;

  tinyusdz::Xform xform;
  for (size_t i = 0; i < niter; i++) {
    prims.emplace_back(xform);
  }

}

// Its rougly 3.5x slower compared to `string_vector_10M` in single-threaded run on Threadripper 1950X
// (even not using thread_safe_databse(no mutex))
UBENCH(perf, token_vector_10M)
{
  constexpr size_t niter = 10 * 10000;
  std::vector<value::token> v;

  for (size_t i = 0; i < niter; i++) {
    value::token tok(std::to_string(i));
    v.emplace_back(tok);
  }

}

UBENCH(perf, string_vector_10M)
{
  constexpr size_t niter = 10 * 10000;
  std::vector<std::string> v;

  for (size_t i = 0; i < niter; i++) {
    std::string s(std::to_string(i));
    v.emplace_back(s);
  }

}

//int main(int argc, char **argv)
//{
//  benchmark_any_type();
//
//  return 0;
//}

#include "mandelbulb-mesh.cc"

UBENCH_MAIN();
