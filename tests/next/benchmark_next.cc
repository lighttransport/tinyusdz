// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Performance Benchmarks

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "next/tinyusdz-next.hh"
#include "next/layer/layer.hh"

using namespace tinyusdz::next;

// Timer helper
class Timer {
public:
  void start() {
    start_ = std::chrono::high_resolution_clock::now();
  }

  double elapsed_ms() const {
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start_).count();
  }

  double elapsed_us() const {
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::micro>(end - start_).count();
  }

private:
  std::chrono::high_resolution_clock::time_point start_;
};

// ============================================================
// Benchmark: Value creation and access
// ============================================================

void benchmark_value_creation(int iterations) {
  printf("\n=== Value Creation Benchmark ===\n");

  Timer timer;

  // Scalar creation
  timer.start();
  for (int i = 0; i < iterations; ++i) {
    Value v(static_cast<float>(i));
    (void)v;
  }
  printf("Float scalar: %.2f us/iter (%.0f ops/sec)\n",
         timer.elapsed_us() / iterations,
         iterations / timer.elapsed_ms() * 1000);

  // Float3 creation
  timer.start();
  for (int i = 0; i < iterations; ++i) {
    Value v = Value::MakeFloat3(1.0f, 2.0f, 3.0f);
    (void)v;
  }
  printf("Float3: %.2f us/iter (%.0f ops/sec)\n",
         timer.elapsed_us() / iterations,
         iterations / timer.elapsed_ms() * 1000);

  // Matrix4f creation
  timer.start();
  float mat[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  for (int i = 0; i < iterations; ++i) {
    Value v = Value::MakeMatrix4f(mat);
    (void)v;
  }
  printf("Matrix4f: %.2f us/iter (%.0f ops/sec)\n",
         timer.elapsed_us() / iterations,
         iterations / timer.elapsed_ms() * 1000);

  // String creation
  timer.start();
  for (int i = 0; i < iterations; ++i) {
    Value v(std::string("test_string_value"));
    (void)v;
  }
  printf("String: %.2f us/iter (%.0f ops/sec)\n",
         timer.elapsed_us() / iterations,
         iterations / timer.elapsed_ms() * 1000);
}

// ============================================================
// Benchmark: Property lookup
// ============================================================

void benchmark_property_lookup(int iterations) {
  printf("\n=== Property Lookup Benchmark ===\n");

  // Create a PrimSpec with many properties
  PrimSpec prim("TestPrim", "Mesh");

  // Add properties
  for (int i = 0; i < 100; ++i) {
    std::string name = "property_" + std::to_string(i);
    prim.add_property(name, Value(static_cast<float>(i)));
  }
  prim.finalize_properties();

  Timer timer;

  // Lookup by string (first time - interning)
  timer.start();
  for (int i = 0; i < iterations; ++i) {
    int idx = i % 100;
    std::string name = "property_" + std::to_string(idx);
    const Value* v = prim.property_value(name);
    (void)v;
  }
  printf("String lookup: %.2f us/iter (%.0f ops/sec)\n",
         timer.elapsed_us() / iterations,
         iterations / timer.elapsed_ms() * 1000);

  // Lookup by pre-interned ID
  PropNameId id = GetPropNameTable().find("property_50");
  timer.start();
  for (int i = 0; i < iterations; ++i) {
    const Value* v = prim.property_value(id);
    (void)v;
  }
  printf("ID lookup: %.2f us/iter (%.0f ops/sec)\n",
         timer.elapsed_us() / iterations,
         iterations / timer.elapsed_ms() * 1000);
}

// ============================================================
// Benchmark: Layer building
// ============================================================

void benchmark_layer_building(int num_prims) {
  printf("\n=== Layer Building Benchmark ===\n");

  Timer timer;
  timer.start();

  Layer layer;
  LayerBuilder builder(layer);

  // Build points array
  std::vector<float> points = {0,0,0, 1,0,0, 1,1,0, 0,1,0};
  std::vector<int32_t> faceVertexCounts = {4};
  std::vector<int32_t> faceVertexIndices = {0,1,2,3};

  for (int i = 0; i < num_prims; ++i) {
    std::string name = "Prim_" + std::to_string(i);
    builder.begin_prim(name, "Mesh");

    // Add some properties
    builder.add_property("points", Value::MakeFloat3Array(points));
    builder.add_property("faceVertexCounts", Value::MakeIntArray(faceVertexCounts));
    builder.add_property("faceVertexIndices", Value::MakeIntArray(faceVertexIndices));

    builder.end_prim();
  }

  builder.finalize();

  double elapsed = timer.elapsed_ms();
  printf("Built %d prims in %.2f ms (%.0f prims/sec)\n",
         num_prims, elapsed, num_prims / elapsed * 1000);

  auto stats = layer.stats();
  printf("  Total prims: %zu\n", stats.prim_count);
  printf("  Total properties: %zu\n", stats.total_properties);
  printf("  Memory usage: %.2f KB\n", stats.memory_bytes / 1024.0);
}

// ============================================================
// Benchmark: Stage traversal
// ============================================================

void benchmark_traversal(int num_prims) {
  printf("\n=== Stage Traversal Benchmark ===\n");

  // Build a stage with prims
  Layer layer;
  LayerBuilder builder(layer);
  for (int i = 0; i < num_prims; ++i) {
    std::string name = "Prim_" + std::to_string(i);
    builder.begin_prim(name, "Xform");
    builder.end_prim();
  }
  builder.finalize();

  Stage stage;
  stage.SetRootLayer(std::move(layer));

  Timer timer;

  // Count traversal
  timer.start();
  int count = 0;
  stage.Traverse([&count](const UsdPrim& /* prim */) {
    ++count;
    return true;
  });
  double elapsed = timer.elapsed_ms();
  printf("Traversed %d prims in %.2f ms (%.0f prims/sec)\n",
         count, elapsed, count / elapsed * 1000);

  // Type-filtered traversal
  timer.start();
  auto xforms = stage.GetPrimsOfType("Xform");
  elapsed = timer.elapsed_ms();
  printf("GetPrimsOfType found %zu in %.2f ms\n", xforms.size(), elapsed);
}

// ============================================================
// Benchmark: Time sample interpolation
// ============================================================

void benchmark_time_samples(int num_samples) {
  printf("\n=== Time Sample Benchmark ===\n");

  Layer layer;
  LayerBuilder builder(layer);
  builder.begin_prim("AnimatedCube", "Mesh");

  // Add time samples
  for (int i = 0; i < num_samples; ++i) {
    double time = static_cast<double>(i);
    builder.add_time_sample("xformOp:translate",
                            time,
                            Value::MakeFloat3(static_cast<float>(i), 0, 0));
  }

  builder.end_prim();
  builder.finalize();

  Stage stage;
  stage.SetRootLayer(std::move(layer));

  UsdPrim prim = stage.GetPrimAtPath("/AnimatedCube");

  Timer timer;
  int iterations = 10000;

  // Time sample query
  timer.start();
  for (int i = 0; i < iterations; ++i) {
    double t = static_cast<double>(i % num_samples);
    const Value* v = prim.GetValueAtTime("xformOp:translate", t);
    (void)v;
  }
  printf("GetValueAtTime: %.2f us/iter (%.0f ops/sec)\n",
         timer.elapsed_us() / iterations,
         iterations / timer.elapsed_ms() * 1000);

  // Interpolated query
  timer.start();
  for (int i = 0; i < iterations; ++i) {
    double t = static_cast<double>(i % num_samples) + 0.5;
    Value v = prim.GetInterpolatedValue("xformOp:translate", t);
    (void)v;
  }
  printf("GetInterpolatedValue: %.2f us/iter (%.0f ops/sec)\n",
         timer.elapsed_us() / iterations,
         iterations / timer.elapsed_ms() * 1000);
}

// ============================================================
// Benchmark: USDA parsing (synthetic)
// ============================================================

void benchmark_usda_parsing() {
  printf("\n=== USDA Parsing Benchmark ===\n");

  // Generate a synthetic USDA
  std::string usda = "#usda 1.0\n(\n    defaultPrim = \"World\"\n)\n\n";

  const int num_prims = 1000;
  for (int i = 0; i < num_prims; ++i) {
    usda += "def Mesh \"Mesh_" + std::to_string(i) + "\"\n{\n";
    usda += "    float3[] points = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]\n";
    usda += "    int[] faceVertexCounts = [4]\n";
    usda += "    int[] faceVertexIndices = [0, 1, 2, 3]\n";
    usda += "}\n\n";
  }

  printf("USDA size: %.2f KB\n", usda.size() / 1024.0);

  Timer timer;
  timer.start();

  LoadResult result = LoadUSDAFromString(usda);

  double elapsed = timer.elapsed_ms();

  if (result.success) {
    printf("Parsed %d prims in %.2f ms (%.0f prims/sec)\n",
           num_prims, elapsed, num_prims / elapsed * 1000);
    printf("  Throughput: %.2f MB/sec\n",
           (usda.size() / 1024.0 / 1024.0) / (elapsed / 1000.0));
  } else {
    printf("Parse failed: %s\n", result.error_summary.c_str());
  }
}

// ============================================================
// Benchmark: USDA writing
// ============================================================

void benchmark_usda_writing(int num_prims) {
  printf("\n=== USDA Writing Benchmark ===\n");

  // Build a stage
  std::vector<float> points = {0,0,0, 1,0,0, 1,1,0, 0,1,0};
  std::vector<int32_t> faceVertexCounts = {4};
  std::vector<int32_t> faceVertexIndices = {0,1,2,3};

  Layer layer;
  LayerBuilder builder(layer);
  for (int i = 0; i < num_prims; ++i) {
    std::string name = "Mesh_" + std::to_string(i);
    builder.begin_prim(name, "Mesh");
    builder.add_property("points", Value::MakeFloat3Array(points));
    builder.add_property("faceVertexCounts", Value::MakeIntArray(faceVertexCounts));
    builder.add_property("faceVertexIndices", Value::MakeIntArray(faceVertexIndices));
    builder.end_prim();
  }
  builder.finalize();

  Stage stage;
  stage.SetRootLayer(std::move(layer));

  Timer timer;
  timer.start();

  std::string usda = WriteUSDAToString(stage);

  double elapsed = timer.elapsed_ms();
  printf("Wrote %d prims in %.2f ms (%.0f prims/sec)\n",
         num_prims, elapsed, num_prims / elapsed * 1000);
  printf("  Output size: %.2f KB\n", usda.size() / 1024.0);
  printf("  Throughput: %.2f MB/sec\n",
         (usda.size() / 1024.0 / 1024.0) / (elapsed / 1000.0));
}

// ============================================================
// Main
// ============================================================

int main(int argc, char** argv) {
  int scale = 1;
  if (argc > 1) {
    scale = std::atoi(argv[1]);
    if (scale < 1) scale = 1;
  }

  printf("TinyUSDZ Next - Performance Benchmarks (scale=%d)\n", scale);
  printf("================================================\n");

  benchmark_value_creation(100000 * scale);
  benchmark_property_lookup(100000 * scale);
  benchmark_layer_building(1000 * scale);
  benchmark_traversal(10000 * scale);
  benchmark_time_samples(100 * scale);
  benchmark_usda_parsing();
  benchmark_usda_writing(1000 * scale);

  printf("\n=== Summary ===\n");
  printf("All benchmarks completed.\n");

  return 0;
}
