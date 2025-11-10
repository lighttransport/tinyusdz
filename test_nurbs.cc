// Test NURBS curves to mesh conversion
#include "src/tydra/curves-to-mesh.hh"
#include "src/tinyusdz.hh"
#include <iostream>

using namespace tinyusdz;
using namespace tinyusdz::tydra;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <nurbs-curves.usda>\n";
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

  // Setup tessellation options
  CurveTessellationOptions options;
  options.mode = CurveTessellationMode::Cylinder;
  options.radial_subdivisions = 8;
  options.segments_per_span = 8;
  options.adaptive = false; // NURBS uses uniform sampling for now

  std::vector<value::float3> points;
  std::vector<int> faceVertexCounts;
  std::vector<int> faceVertexIndices;
  std::vector<value::float3> normals;
  std::vector<value::float2> uvs;

  // Find first NurbsCurves prim
  const auto& root_prims = stage.root_prims();
  for (const auto& root : root_prims) {
    if (root.is<GeomNurbsCurves>()) {
      const auto* curves = root.as<GeomNurbsCurves>();

      std::cout << "Converting NurbsCurves prim to mesh...\n";
      ret = NurbsCurvesToMesh(*curves, options, points, faceVertexCounts,
                              faceVertexIndices, normals, uvs);

      if (ret) {
        std::cout << "Success! Generated mesh:\n";
        std::cout << "  Points: " << points.size() << "\n";
        std::cout << "  Faces: " << faceVertexCounts.size() << "\n";
        std::cout << "  Normals: " << normals.size() << "\n";

        // Print first few evaluated points
        std::cout << "\nFirst 5 evaluated points:\n";
        for (size_t i = 0; i < std::min(size_t(5), points.size()); i++) {
          std::cout << "  [" << i << "] ("
                    << points[i][0] << ", "
                    << points[i][1] << ", "
                    << points[i][2] << ")\n";
        }

        return 0;
      } else {
        std::cerr << "Failed to convert NURBS curves to mesh\n";
        return 1;
      }
    }
  }

  std::cerr << "No NurbsCurves prim found in file\n";
  return 1;
}
