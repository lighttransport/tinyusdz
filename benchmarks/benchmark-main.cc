#include <unistd.h>
#include "ubench.h"

#include "value-type.hh"

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
    linb::any a;
    a = double(i);
  }
}

UBENCH(perf, thelink2012_any_float_10M)
{
  constexpr size_t niter = 10 * 10000;
  for (size_t i = 0; i < niter; i++) {
    linb::any a;
    a = float(i);
  }
}

UBENCH(perf, thelink2012_any_double_10M)
{
  constexpr size_t niter = 10 * 10000;

  std::vector<linb::any> v;

  for (size_t i = 0; i < niter; i++) {
    v.push_back(double(i));
  }
}

#if 0
UBENCH(perf, any_value_100M)
{
  constexpr size_t niter = 100 * 10000;
  for (size_t i = 0; i < niter; i++) {
    tinyusdz::value::any_value a;
    a = i;
  }
}
#endif

UBENCH(perf, timesamples_double_10M)
{
  constexpr size_t ns = 10 * 10000;

  tinyusdz::value::TimeSamples ts;

  for (size_t i = 0; i < ns; i++) {
    ts.times.push_back(double(i));
    ts.values.push_back(double(i));
  }
}

//int main(int argc, char **argv)
//{
//  benchmark_any_type();
//
//  return 0;
//}

UBENCH_MAIN();
