// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Light Transport Entertainment, Inc.
//
// Test optimized array parsing performance with synthetic data

#include <iostream>
#include <sstream>
#include <chrono>
#include <random>
#include <iomanip>
#include <string>
#include <vector>

#include "tinyusdz.hh"
#include "ascii-parser.hh"
#include "stream-reader.hh"
#include "value-types.hh"

using namespace tinyusdz;

enum class BenchmarkProfile {
  Full,
  Quick,
};

// Generate random float arrays
std::string generate_float_array(size_t count) {
  std::stringstream ss;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<float> dis(-10000.0f, 10000.0f);

  ss << "[";
  for (size_t i = 0; i < count; i++) {
    ss << dis(gen);
    if (i < count - 1) ss << ", ";
  }
  ss << "]";

  return ss.str();
}

// Generate float2 arrays: [(x, y), ...]
std::string generate_float2_array(size_t count) {
  std::stringstream ss;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<float> dis(-10000.0f, 10000.0f);

  ss << "[";
  for (size_t i = 0; i < count; i++) {
    ss << "(" << dis(gen) << ", " << dis(gen) << ")";
    if (i < count - 1) ss << ", ";
  }
  ss << "]";

  return ss.str();
}

// Generate float3 arrays: [(x, y, z), ...]
std::string generate_float3_array(size_t count) {
  std::stringstream ss;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<float> dis(-10000.0f, 10000.0f);

  ss << "[";
  for (size_t i = 0; i < count; i++) {
    ss << "(" << dis(gen) << ", " << dis(gen) << ", " << dis(gen) << ")";
    if (i < count - 1) ss << ", ";
  }
  ss << "]";

  return ss.str();
}

// Generate float4 arrays: [(x, y, z, w), ...]
std::string generate_float4_array(size_t count) {
  std::stringstream ss;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<float> dis(-10000.0f, 10000.0f);

  ss << "[";
  for (size_t i = 0; i < count; i++) {
    ss << "(" << dis(gen) << ", " << dis(gen) << ", " << dis(gen) << ", " << dis(gen) << ")";
    if (i < count - 1) ss << ", ";
  }
  ss << "]";

  return ss.str();
}

// Generate double arrays
std::string generate_double_array(size_t count) {
  std::stringstream ss;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<double> dis(-10000.0, 10000.0);

  ss << "[";
  for (size_t i = 0; i < count; i++) {
    ss << std::setprecision(15) << dis(gen);
    if (i < count - 1) ss << ", ";
  }
  ss << "]";

  return ss.str();
}

// Generate matrix4d arrays: [( e0, e1, ..., e15 ), ...]
std::string generate_matrix4d_array(size_t count) {
  std::stringstream ss;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<double> dis(-10000.0, 10000.0);

  ss << "[";
  for (size_t i = 0; i < count; i++) {
    ss << "(";
    for (int j = 0; j < 16; j++) {
      ss << std::setprecision(15) << dis(gen);
      if (j < 15) ss << ", ";
    }
    ss << ")";
    if (i < count - 1) ss << ", ";
  }
  ss << "]";

  return ss.str();
}

// Generate timeSamples with float arrays
std::string generate_timesample_float_arrays(size_t num_samples, size_t array_size) {
  std::stringstream ss;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<float> dis(-10000.0f, 10000.0f);

  ss << "{\n";
  for (size_t t = 0; t < num_samples; t++) {
    double time = static_cast<double>(t);
    ss << "  " << time << ": [";
    for (size_t i = 0; i < array_size; i++) {
      ss << dis(gen);
      if (i < array_size - 1) ss << ", ";
    }
    ss << "]";
    if (t < num_samples - 1) ss << ",";
    ss << "\n";
  }
  ss << "}";

  return ss.str();
}

// Generate timeSamples with float3 arrays
std::string generate_timesample_float3_arrays(size_t num_samples, size_t array_size) {
  std::stringstream ss;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<float> dis(-10000.0f, 10000.0f);

  ss << "{\n";
  for (size_t t = 0; t < num_samples; t++) {
    double time = static_cast<double>(t);
    ss << "  " << time << ": [";
    for (size_t i = 0; i < array_size; i++) {
      ss << "(" << dis(gen) << ", " << dis(gen) << ", " << dis(gen) << ")";
      if (i < array_size - 1) ss << ", ";
    }
    ss << "]";
    if (t < num_samples - 1) ss << ",";
    ss << "\n";
  }
  ss << "}";

  return ss.str();
}

// Benchmark helper
template<typename Func>
double benchmark(const std::string& name, Func func, int iterations = 1) {
  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < iterations; i++) {
    func();
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  double avg_ms = static_cast<double>(duration.count()) / iterations;
  std::cout << "  " << name << ": " << avg_ms << " ms";
  if (iterations > 1) {
    std::cout << " (avg of " << iterations << " runs)";
  }
  std::cout << std::endl;

  return avg_ms;
}

// Test parsing float arrays
void test_float_array_parsing(BenchmarkProfile profile) {
  std::cout << "\n=== Float Array Parsing ===" << std::endl;

  const std::vector<size_t> sizes =
      (profile == BenchmarkProfile::Quick)
          ? std::vector<size_t>{10000, 100000, 500000}
          : std::vector<size_t>{10000, 100000, 1000000, 5000000, 10000000};

  for (size_t size : sizes) {
    std::cout << "\nArray size: " << size << " elements" << std::endl;

    std::string data = generate_float_array(size);
    std::cout << "  Generated data size: " << data.size() << " bytes" << std::endl;

    benchmark("Parse time", [&]() {
      StreamReader sr(reinterpret_cast<const uint8_t*>(data.data()), data.size(), false);
      ascii::AsciiParser parser(&sr);
      std::vector<float> result;
      parser.ParseBasicTypeArray(&result);
    });
  }
}

// Test parsing float3 arrays
void test_float3_array_parsing(BenchmarkProfile profile) {
  std::cout << "\n=== Float3 Array Parsing ===" << std::endl;

  const std::vector<size_t> sizes =
      (profile == BenchmarkProfile::Quick)
          ? std::vector<size_t>{10000, 50000, 100000}
          : std::vector<size_t>{10000, 100000, 500000, 1000000};

  for (size_t size : sizes) {
    std::cout << "\nArray size: " << size << " float3 vectors" << std::endl;

    std::string data = generate_float3_array(size);
    std::cout << "  Generated data size: " << data.size() << " bytes" << std::endl;

    benchmark("Parse time", [&]() {
      StreamReader sr(reinterpret_cast<const uint8_t*>(data.data()), data.size(), false);
      ascii::AsciiParser parser(&sr);
      std::vector<value::float3> result;
      parser.ParseBasicTypeArray(&result);
    });
  }
}

// Test parsing double arrays
void test_double_array_parsing(BenchmarkProfile profile) {
  std::cout << "\n=== Double Array Parsing ===" << std::endl;

  const std::vector<size_t> sizes =
      (profile == BenchmarkProfile::Quick)
          ? std::vector<size_t>{10000, 50000, 100000}
          : std::vector<size_t>{10000, 100000, 1000000, 5000000};

  for (size_t size : sizes) {
    std::cout << "\nArray size: " << size << " elements" << std::endl;

    std::string data = generate_double_array(size);
    std::cout << "  Generated data size: " << data.size() << " bytes" << std::endl;

    benchmark("Parse time", [&]() {
      StreamReader sr(reinterpret_cast<const uint8_t*>(data.data()), data.size(), false);
      ascii::AsciiParser parser(&sr);
      std::vector<double> result;
      parser.ParseBasicTypeArray(&result);
    });
  }
}

// Test parsing matrix4d arrays
void test_matrix4d_array_parsing(BenchmarkProfile profile) {
  std::cout << "\n=== Matrix4d Array Parsing ===" << std::endl;

  const std::vector<size_t> sizes =
      (profile == BenchmarkProfile::Quick)
          ? std::vector<size_t>{1000, 5000, 10000}
          : std::vector<size_t>{1000, 10000, 50000, 100000};

  for (size_t size : sizes) {
    std::cout << "\nArray size: " << size << " matrix4d matrices" << std::endl;

    std::string data = generate_matrix4d_array(size);
    std::cout << "  Generated data size: " << data.size() << " bytes" << std::endl;

    benchmark("Parse time", [&]() {
      StreamReader sr(reinterpret_cast<const uint8_t*>(data.data()), data.size(), false);
      ascii::AsciiParser parser(&sr);
      std::vector<value::matrix4d> result;
      parser.ParseBasicTypeArray(&result);
    });
  }
}

// Test parsing timeSamples with float arrays
void test_timesample_float_arrays(BenchmarkProfile profile) {
  std::cout << "\n=== TimeSamples with Float Arrays ===" << std::endl;

  struct TestCase {
    size_t num_samples;
    size_t array_size;
  };

  const std::vector<TestCase> cases =
      (profile == BenchmarkProfile::Quick)
          ? std::vector<TestCase>{{25, 5000}, {50, 5000}, {25, 20000}}
          : std::vector<TestCase>{{100, 10000},
                                  {500, 10000},
                                  {100, 100000},
                                  {500, 100000}};

  for (const auto& tc : cases) {
    std::cout << "\nTimeSamples: " << tc.num_samples << " frames × "
              << tc.array_size << " floats/frame" << std::endl;

    std::string data = generate_timesample_float_arrays(tc.num_samples, tc.array_size);
    std::cout << "  Generated data size: " << data.size() << " bytes" << std::endl;

    benchmark("Parse time", [&]() {
      StreamReader sr(reinterpret_cast<const uint8_t*>(data.data()), data.size(), false);
      ascii::AsciiParser parser(&sr);
      value::TimeSamples ts;
      parser.ParseTimeSamples("float[]", &ts);
    });
  }
}

// Test parsing timeSamples with float3 arrays (e.g., points)
void test_timesample_float3_arrays(BenchmarkProfile profile) {
  std::cout << "\n=== TimeSamples with Float3 Arrays (e.g., points) ===" << std::endl;

  struct TestCase {
    size_t num_samples;
    size_t array_size;
  };

  const std::vector<TestCase> cases =
      (profile == BenchmarkProfile::Quick)
          ? std::vector<TestCase>{{25, 5000}, {50, 5000}, {25, 10000}}
          : std::vector<TestCase>{{100, 10000},
                                  {500, 10000},
                                  {100, 50000},
                                  {500, 50000}};

  for (const auto& tc : cases) {
    std::cout << "\nTimeSamples: " << tc.num_samples << " frames × "
              << tc.array_size << " points/frame" << std::endl;

    std::string data = generate_timesample_float3_arrays(tc.num_samples, tc.array_size);
    std::cout << "  Generated data size: " << data.size() << " bytes" << std::endl;

    benchmark("Parse time", [&]() {
      StreamReader sr(reinterpret_cast<const uint8_t*>(data.data()), data.size(), false);
      ascii::AsciiParser parser(&sr);
      value::TimeSamples ts;
      parser.ParseTimeSamples("float3[]", &ts);
    });
  }
}

// Test complete USDA snippet with arrays
void test_complete_usda_snippet(BenchmarkProfile profile) {
  std::cout << "\n=== Complete USDA Snippet Parsing ===" << std::endl;

  const size_t points_count =
      (profile == BenchmarkProfile::Quick) ? 25000 : 100000;
  const size_t face_vertex_counts_count =
      (profile == BenchmarkProfile::Quick) ? 7500 : 30000;
  const size_t face_vertex_indices_count =
      (profile == BenchmarkProfile::Quick) ? 22500 : 90000;

  std::stringstream ss;
  ss << "#usda 1.0\n";
  ss << "(\n";
  ss << "  defaultPrim = \"TestMesh\"\n";
  ss << ")\n\n";
  ss << "def Mesh \"TestMesh\" {\n";
  ss << "  float3[] points = " << generate_float3_array(points_count) << "\n";
  ss << "  int[] faceVertexCounts = "
     << generate_float_array(face_vertex_counts_count) << "\n";
  ss << "  int[] faceVertexIndices = "
     << generate_float_array(face_vertex_indices_count) << "\n";
  ss << "}\n";

  std::string data = ss.str();
  std::cout << "USDA data size: " << data.size() << " bytes" << std::endl;

  benchmark("Full USDA parse", [&]() {
    Stage stage;
    std::string warn, err;
    bool ret = LoadUSDFromMemory(reinterpret_cast<const uint8_t*>(data.data()),
                                   data.size(), "test.usda", &stage, &warn, &err);
    if (!ret) {
      std::cerr << "Parse error: " << err << std::endl;
    }
  });
}

BenchmarkProfile ParseProfile(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    const std::string arg(argv[i]);
    if (arg == "--quick") {
      return BenchmarkProfile::Quick;
    }
  }

  return BenchmarkProfile::Full;
}

int main(int argc, char** argv) {
  const BenchmarkProfile profile = ParseProfile(argc, argv);

  std::cout << "========================================" << std::endl;
  std::cout << "TinyUSDZ Array Parsing Benchmark" << std::endl;
  std::cout << "Profile: "
            << ((profile == BenchmarkProfile::Quick) ? "quick" : "full")
            << std::endl;
  std::cout << "========================================" << std::endl;

  // Run all benchmarks
  test_float_array_parsing(profile);
  test_float3_array_parsing(profile);
  test_double_array_parsing(profile);
  test_matrix4d_array_parsing(profile);
  test_timesample_float_arrays(profile);
  test_timesample_float3_arrays(profile);
  test_complete_usda_snippet(profile);

  std::cout << "\n========================================" << std::endl;
  std::cout << "Benchmark Complete" << std::endl;
  std::cout << "========================================" << std::endl;

  return 0;
}
