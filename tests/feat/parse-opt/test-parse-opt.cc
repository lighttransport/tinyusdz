// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Light Transport Entertainment, Inc.
//
// Synthetic USDA numeric parser benchmark.

#include <chrono>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "ascii-parser.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"
#include "value-types.hh"

namespace {

enum class BenchmarkProfile {
  Full,
  Quick,
};

struct BenchmarkConfig {
  size_t scalar_count;
  size_t tuple_count;
  size_t quat_count;
  size_t matrix_count;
  size_t usda_count;
  int iterations;
};

struct Options {
  BenchmarkProfile profile{BenchmarkProfile::Full};
  bool direct_only{false};
  bool usda_only{false};
};

BenchmarkConfig ConfigFor(BenchmarkProfile profile) {
  if (profile == BenchmarkProfile::Quick) {
    return BenchmarkConfig{/*scalar_count=*/2048,
                           /*tuple_count=*/1024,
                           /*quat_count=*/512,
                           /*matrix_count=*/64,
                           /*usda_count=*/256,
                           /*iterations=*/1};
  }

  return BenchmarkConfig{/*scalar_count=*/500000,
                         /*tuple_count=*/100000,
                         /*quat_count=*/100000,
                         /*matrix_count=*/10000,
                         /*usda_count=*/10000,
                         /*iterations=*/3};
}

void PrintUsage(const char *argv0) {
  std::cout << "Usage: " << argv0
            << " [--quick] [--direct-only] [--usda-only]\n";
}

Options ParseOptions(int argc, char **argv) {
  Options opts;
  for (int i = 1; i < argc; i++) {
    const std::string arg(argv[i]);
    if (arg == "--quick") {
      opts.profile = BenchmarkProfile::Quick;
    } else if (arg == "--direct-only") {
      opts.direct_only = true;
    } else if (arg == "--usda-only") {
      opts.usda_only = true;
    } else if (arg == "--help" || arg == "-h") {
      PrintUsage(argv[0]);
      std::exit(0);
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      PrintUsage(argv[0]);
      std::exit(1);
    }
  }

  if (opts.direct_only && opts.usda_only) {
    std::cerr << "--direct-only and --usda-only are mutually exclusive.\n";
    std::exit(1);
  }

  return opts;
}

void AppendDoubleLiteral(std::string *out, double value, int precision) {
  char buf[64];
  const int n = std::snprintf(buf, sizeof(buf), "%.*g", precision, value);
  if (n > 0) {
    out->append(buf, static_cast<size_t>(n));
  }
}

double FloatValue(size_t i) {
  const int base = static_cast<int>(i % 20001) - 10000;
  return double(base) * 0.125 + double(i % 7) * 0.001;
}

double DoubleValue(size_t i) {
  const int base = static_cast<int>(i % 20001) - 10000;
  return double(base) * 0.0009765625 + double((i * 17) % 101) * 1.0e-9;
}

double HalfValue(size_t i) {
  const int base = static_cast<int>(i % 1025) - 512;
  return double(base) * 0.125;
}

int32_t IntValue(size_t i) {
  return static_cast<int32_t>(static_cast<int64_t>((i * 37) % 200000) -
                              100000);
}

uint32_t UIntValue(size_t i) {
  return static_cast<uint32_t>((i * 65537u) % 4000000000u);
}

int64_t Int64Value(size_t i) {
  return static_cast<int64_t>(i) * 2147483647ll - 900000000000ll;
}

uint64_t UInt64Value(size_t i) {
  return 100000000000ull + static_cast<uint64_t>(i) * 2654435761ull;
}

template <typename AppendValue>
std::string MakeScalarArray(size_t count, AppendValue append_value) {
  std::string out;
  out.reserve(count * 16 + 2);
  out.push_back('[');
  for (size_t i = 0; i < count; i++) {
    append_value(&out, i);
    if (i + 1 < count) {
      out.append(", ");
    }
  }
  out.push_back(']');
  return out;
}

template <typename AppendValue>
std::string MakeTupleArray(size_t count, size_t arity, AppendValue append_value) {
  std::string out;
  out.reserve(count * arity * 16 + count * 4 + 2);
  out.push_back('[');
  for (size_t i = 0; i < count; i++) {
    out.push_back('(');
    for (size_t j = 0; j < arity; j++) {
      append_value(&out, i, j);
      if (j + 1 < arity) {
        out.append(", ");
      }
    }
    out.push_back(')');
    if (i + 1 < count) {
      out.append(", ");
    }
  }
  out.push_back(']');
  return out;
}

std::string MakeMatrix4dArray(size_t count) {
  std::string out;
  out.reserve(count * 280 + 2);
  out.push_back('[');
  for (size_t i = 0; i < count; i++) {
    out.append("( ");
    for (size_t row = 0; row < 4; row++) {
      out.push_back('(');
      for (size_t col = 0; col < 4; col++) {
        const size_t idx = i * 16 + row * 4 + col;
        const double value = (row == col) ? 1.0 : DoubleValue(idx);
        AppendDoubleLiteral(&out, value, 17);
        if (col + 1 < 4) {
          out.append(", ");
        }
      }
      out.push_back(')');
      if (row + 1 < 4) {
        out.append(", ");
      }
    }
    out.append(" )");
    if (i + 1 < count) {
      out.append(", ");
    }
  }
  out.push_back(']');
  return out;
}

std::string MakeIntArray(size_t count) {
  return MakeScalarArray(count, [](std::string *out, size_t i) {
    out->append(std::to_string(IntValue(i)));
  });
}

std::string MakeUIntArray(size_t count) {
  return MakeScalarArray(count, [](std::string *out, size_t i) {
    out->append(std::to_string(UIntValue(i)));
  });
}

std::string MakeInt64Array(size_t count) {
  return MakeScalarArray(count, [](std::string *out, size_t i) {
    out->append(std::to_string(Int64Value(i)));
  });
}

std::string MakeUInt64Array(size_t count) {
  return MakeScalarArray(count, [](std::string *out, size_t i) {
    out->append(std::to_string(UInt64Value(i)));
  });
}

std::string MakeHalfArray(size_t count) {
  return MakeScalarArray(count, [](std::string *out, size_t i) {
    AppendDoubleLiteral(out, HalfValue(i), 9);
  });
}

std::string MakeFloatArray(size_t count) {
  return MakeScalarArray(count, [](std::string *out, size_t i) {
    AppendDoubleLiteral(out, FloatValue(i), 9);
  });
}

std::string MakeDoubleArray(size_t count) {
  return MakeScalarArray(count, [](std::string *out, size_t i) {
    AppendDoubleLiteral(out, DoubleValue(i), 17);
  });
}

std::string MakeHalf3Array(size_t count) {
  return MakeTupleArray(count, 3, [](std::string *out, size_t i, size_t j) {
    AppendDoubleLiteral(out, HalfValue(i * 3 + j), 9);
  });
}

std::string MakeFloat3Array(size_t count) {
  return MakeTupleArray(count, 3, [](std::string *out, size_t i, size_t j) {
    AppendDoubleLiteral(out, FloatValue(i * 3 + j), 9);
  });
}

std::string MakeDouble3Array(size_t count) {
  return MakeTupleArray(count, 3, [](std::string *out, size_t i, size_t j) {
    AppendDoubleLiteral(out, DoubleValue(i * 3 + j), 17);
  });
}

std::string MakeQuatHalfArray(size_t count) {
  return MakeTupleArray(count, 4, [](std::string *out, size_t i, size_t j) {
    const double value = (j == 0) ? 1.0 : HalfValue(i * 3 + j);
    AppendDoubleLiteral(out, value, 9);
  });
}

std::string MakeQuatFloatArray(size_t count) {
  return MakeTupleArray(count, 4, [](std::string *out, size_t i, size_t j) {
    const double value = (j == 0) ? 1.0 : FloatValue(i * 3 + j) * 0.001;
    AppendDoubleLiteral(out, value, 9);
  });
}

std::string MakeQuatDoubleArray(size_t count) {
  return MakeTupleArray(count, 4, [](std::string *out, size_t i, size_t j) {
    const double value = (j == 0) ? 1.0 : DoubleValue(i * 3 + j);
    AppendDoubleLiteral(out, value, 17);
  });
}

template <typename Func>
bool Benchmark(const std::string &name, size_t bytes, int iterations, Func func) {
  const auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < iterations; i++) {
    if (!func()) {
      std::cerr << "Benchmark failed: " << name << "\n";
      return false;
    }
  }
  const auto end = std::chrono::high_resolution_clock::now();
  const double us =
      double(std::chrono::duration_cast<std::chrono::microseconds>(end - start)
                 .count());
  const double avg_ms = us / 1000.0 / double(iterations);
  const double mib = double(bytes) / (1024.0 * 1024.0);
  const double mib_per_s = (avg_ms > 0.0) ? (mib / (avg_ms / 1000.0)) : 0.0;

  std::cout << "  " << std::left << std::setw(14) << name << std::right
            << " bytes=" << std::setw(10) << bytes
            << " avg_ms=" << std::setw(9) << std::fixed << std::setprecision(3)
            << avg_ms << " MiB/s=" << std::setw(9) << std::setprecision(1)
            << mib_per_s;
  if (iterations > 1) {
    std::cout << " runs=" << iterations;
  }
  std::cout << "\n";
  return true;
}

template <typename T>
bool RunArrayCase(const std::string &name, const std::string &data,
                  size_t expected_count, int iterations) {
  return Benchmark(name, data.size(), iterations, [&]() {
    tinyusdz::StreamReader sr(reinterpret_cast<const uint8_t *>(data.data()),
                              data.size(), false);
    tinyusdz::ascii::AsciiParser parser(&sr);
    std::vector<T> result;
    if (!parser.ParseBasicTypeArray(&result)) {
      std::cerr << "Parse error for " << name << ": " << parser.GetError()
                << "\n";
      return false;
    }
    if (result.size() != expected_count) {
      std::cerr << "Unexpected element count for " << name << ": got "
                << result.size() << ", expected " << expected_count << "\n";
      return false;
    }
    return true;
  });
}

std::string MakeNumericUsda(size_t count, size_t matrix_count) {
  std::string out;
  out.reserve(count * 900 + matrix_count * 300);
  out.append("#usda 1.0\n\n");
  out.append("def Scope \"NumericBench\" {\n");
  out.append("  custom int[] intValues = ");
  out.append(MakeIntArray(count));
  out.append("\n  custom uint[] uintValues = ");
  out.append(MakeUIntArray(count));
  out.append("\n  custom int64[] int64Values = ");
  out.append(MakeInt64Array(count));
  out.append("\n  custom uint64[] uint64Values = ");
  out.append(MakeUInt64Array(count));
  out.append("\n  custom half[] halfValues = ");
  out.append(MakeHalfArray(count));
  out.append("\n  custom half3[] half3Values = ");
  out.append(MakeHalf3Array(count));
  out.append("\n  custom float[] floatValues = ");
  out.append(MakeFloatArray(count));
  out.append("\n  custom float3[] float3Values = ");
  out.append(MakeFloat3Array(count));
  out.append("\n  custom double[] doubleValues = ");
  out.append(MakeDoubleArray(count));
  out.append("\n  custom double3[] double3Values = ");
  out.append(MakeDouble3Array(count));
  out.append("\n  custom quath[] quathValues = ");
  out.append(MakeQuatHalfArray(count));
  out.append("\n  custom quatf[] quatfValues = ");
  out.append(MakeQuatFloatArray(count));
  out.append("\n  custom quatd[] quatdValues = ");
  out.append(MakeQuatDoubleArray(count));
  out.append("\n  custom matrix4d[] matrixValues = ");
  out.append(MakeMatrix4dArray(matrix_count));
  out.append("\n}\n");
  return out;
}

bool RunDirectArrayBenchmarks(const BenchmarkConfig &config) {
  std::cout << "\n=== Direct Array Literal Parsing ===\n";
  bool ok = true;
  ok &= RunArrayCase<int32_t>("int[]", MakeIntArray(config.scalar_count),
                              config.scalar_count, config.iterations);
  ok &= RunArrayCase<uint32_t>("uint[]", MakeUIntArray(config.scalar_count),
                               config.scalar_count, config.iterations);
  ok &= RunArrayCase<int64_t>("int64[]", MakeInt64Array(config.scalar_count),
                              config.scalar_count, config.iterations);
  ok &= RunArrayCase<uint64_t>("uint64[]", MakeUInt64Array(config.scalar_count),
                               config.scalar_count, config.iterations);
  ok &= RunArrayCase<tinyusdz::value::half>(
      "half[]", MakeHalfArray(config.scalar_count), config.scalar_count,
      config.iterations);
  ok &= RunArrayCase<float>("float[]", MakeFloatArray(config.scalar_count),
                            config.scalar_count, config.iterations);
  ok &= RunArrayCase<double>("double[]", MakeDoubleArray(config.scalar_count),
                             config.scalar_count, config.iterations);
  ok &= RunArrayCase<tinyusdz::value::half3>(
      "half3[]", MakeHalf3Array(config.tuple_count), config.tuple_count,
      config.iterations);
  ok &= RunArrayCase<tinyusdz::value::float3>(
      "float3[]", MakeFloat3Array(config.tuple_count), config.tuple_count,
      config.iterations);
  ok &= RunArrayCase<tinyusdz::value::double3>(
      "double3[]", MakeDouble3Array(config.tuple_count), config.tuple_count,
      config.iterations);
  ok &= RunArrayCase<tinyusdz::value::quath>(
      "quath[]", MakeQuatHalfArray(config.quat_count), config.quat_count,
      config.iterations);
  ok &= RunArrayCase<tinyusdz::value::quatf>(
      "quatf[]", MakeQuatFloatArray(config.quat_count), config.quat_count,
      config.iterations);
  ok &= RunArrayCase<tinyusdz::value::quatd>(
      "quatd[]", MakeQuatDoubleArray(config.quat_count), config.quat_count,
      config.iterations);
  ok &= RunArrayCase<tinyusdz::value::matrix4d>(
      "matrix4d[]", MakeMatrix4dArray(config.matrix_count),
      config.matrix_count, config.iterations);
  return ok;
}

bool RunUsdaBenchmark(const BenchmarkConfig &config) {
  std::cout << "\n=== Synthetic USDA Parsing ===\n";
  const std::string usda = MakeNumericUsda(config.usda_count,
                                           (std::max)(size_t(1), config.usda_count / 16));
  std::cout << "  attributes=14 elements_per_numeric_attr=" << config.usda_count
            << " bytes=" << usda.size() << "\n";

  return Benchmark("LoadUSDA", usda.size(), config.iterations, [&]() {
    tinyusdz::Stage stage;
    std::string warn;
    std::string err;
    const bool ret = tinyusdz::LoadUSDFromMemory(
        reinterpret_cast<const uint8_t *>(usda.data()), usda.size(),
        "synthetic-numeric.usda", &stage, &warn, &err);
    if (!ret) {
      std::cerr << "LoadUSDFromMemory failed: " << err << "\n";
      return false;
    }
    return true;
  });
}

}  // namespace

int main(int argc, char **argv) {
  const Options opts = ParseOptions(argc, argv);
  const BenchmarkConfig config = ConfigFor(opts.profile);

  std::cout << "========================================\n";
  std::cout << "TinyUSDZ Synthetic Numeric USDA Benchmark\n";
  std::cout << "Profile: "
            << ((opts.profile == BenchmarkProfile::Quick) ? "quick" : "full")
            << "\n";
  std::cout << "========================================\n";

  bool ok = true;
  if (!opts.usda_only) {
    ok &= RunDirectArrayBenchmarks(config);
  }
  if (!opts.direct_only) {
    ok &= RunUsdaBenchmark(config);
  }

  std::cout << "\n========================================\n";
  std::cout << (ok ? "Benchmark Complete" : "Benchmark Failed") << "\n";
  std::cout << "========================================\n";

  return ok ? 0 : 1;
}
