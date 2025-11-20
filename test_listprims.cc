#include <iostream>
#include "tinyusdz.hh"
#include "tydra/scene-access.hh"

int main() {
  std::string warn, err;
  tinyusdz::Stage stage;
  
  // Load a simple USD file
  if (!tinyusdz::LoadUSDFromFile("models/cube.usdc", &stage, &warn, &err)) {
    std::cerr << "Failed to load USD: " << err << "\n";
    return 1;
  }
  
  if (!warn.empty()) {
    std::cout << "WARN: " << warn << "\n";
  }
  
  // Test ListPrims
  using MeshMap = std::map<std::string, const tinyusdz::GeomMesh *>;
  MeshMap meshes;
  tinyusdz::tydra::ListPrims(stage, meshes);
  
  std::cout << "Found " << meshes.size() << " meshes\n";
  
  for (const auto &mesh : meshes) {
    const tinyusdz::Prim *prim_ptr = reinterpret_cast<const tinyusdz::Prim*>(mesh.second);
    
    // Try to get prim from mesh.second by finding its containing Prim
    // This is tricky - mesh.second is a pointer to GeomMesh inside a Prim
    // We need to find that Prim in the stage
    
    std::cout << "\nMesh path from map key: " << mesh.first << "\n";
    
    // Try to get the Prim at this path from the stage
    auto prim_result = stage.GetPrimAtPath(tinyusdz::Path(mesh.first, ""));
    if (prim_result) {
      const tinyusdz::Prim *prim = prim_result.value();
      std::cout << "  element_name: " << prim->element_name() << "\n";
      std::cout << "  absolute_path: " << prim->absolute_path().full_path_name() << "\n";
      std::cout << "  local_path: " << prim->local_path().full_path_name() << "\n";
      std::cout << "  element_path: " << prim->element_path().full_path_name() << "\n";
    } else {
      std::cerr << "  ERROR: Could not find Prim at path: " << prim_result.error() << "\n";
    }
  }
  
  return 0;
}
