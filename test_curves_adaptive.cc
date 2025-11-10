// Simple test to verify curves-to-mesh.hh compiles with adaptive tessellation
#include "src/tydra/curves-to-mesh.hh"
#include "src/tinyusdz.hh"
#include <iostream>

using namespace tinyusdz;
using namespace tinyusdz::tydra;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <curves.usda>\n";
    return 1;
  }

  // Load USD file
  Stage stage;
  std::string warn, err;
  bool ret = LoadUSDFromFile(argv[1], &stage, &warn, &err);

  if (!ret) {
    std::cerr << "Failed to load USD: " << err << "\n";
    return 1;
  }

  if (!warn.empty()) {
    std::cout << "Warning: " << warn << "\n";
  }

  std::cout << "Loaded USD file successfully\n";

  // Test with adaptive tessellation
  CurveTessellationOptions options;
  options.mode = CurveTessellationMode::Cylinder;
  options.radial_subdivisions = 8;
  options.segments_per_span = 4;
  options.adaptive = true;
  options.max_edge_length = 0.1f;

  std::vector<value::float3> points;
  std::vector<int> faceVertexCounts;
  std::vector<int> faceVertexIndices;
  std::vector<value::float3> normals;
  std::vector<value::float2> uvs;

  // Find first BasisCurves prim
  const auto& root_prims = stage.root_prims();
  for (const auto& root : root_prims) {
    if (root.is<GeomBasisCurves>()) {
      const auto* curves = root.as<GeomBasisCurves>();

      std::cout << "Converting BasisCurves prim with adaptive tessellation...\n";
      ret = BasisCurvesToMesh(*curves, options, points, faceVertexCounts,
                              faceVertexIndices, normals, uvs);

      if (ret) {
        std::cout << "Success! Generated mesh:\n";
        std::cout << "  Points: " << points.size() << "\n";
        std::cout << "  Faces: " << faceVertexCounts.size() << "\n";
        std::cout << "  Normals: " << normals.size() << "\n";
        return 0;
      } else {
        std::cerr << "Failed to convert curves to mesh\n";
        return 1;
      }
    }
  }

  std::cerr << "No BasisCurves prim found in file\n";
  return 1;
}
