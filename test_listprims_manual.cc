#include <iostream>
#include "tinyusdz.hh"
#include "usdGeom.hh"
#include "tydra/scene-access.hh"

int main() {
  tinyusdz::Stage stage;
  
  // Manually create a Stage with a mesh
  tinyusdz::Xform xform;
  xform.name = "root";
  xform.spec = tinyusdz::Specifier::Def;
  
  tinyusdz::GeomMesh mesh;
  mesh.name = "mesh0";
  mesh.spec = tinyusdz::Specifier::Def;
  
  tinyusdz::Prim mesh_prim("mesh0", mesh);
  tinyusdz::Prim root_prim("root", xform);
  root_prim.children().push_back(mesh_prim);
  
  stage.metas().primChildren.push_back(tinyusdz::value::token("root"));
  stage.root_prims().push_back(root_prim);
  
  // Test ListPrims WITHOUT calling compute_absolute_prim_path_and_assign_prim_id
  std::cout << "=== Before compute_absolute_prim_path_and_assign_prim_id ===\n";
  {
    using MeshMap = std::map<std::string, const tinyusdz::GeomMesh *>;
    MeshMap meshes;
    tinyusdz::tydra::ListPrims(stage, meshes);
    
    std::cout << "Found " << meshes.size() << " meshes\n";
    
    for (const auto &mesh_item : meshes) {
      std::cout << "\nMesh path from map key: " << mesh_item.first << "\n";
      
      // Try to get the Prim at this path from the stage
      auto prim_result = stage.GetPrimAtPath(tinyusdz::Path(mesh_item.first, ""));
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
  }
  
  // Now call compute_absolute_prim_path_and_assign_prim_id
  std::cout << "\n=== After compute_absolute_prim_path_and_assign_prim_id ===\n";
  if (!stage.compute_absolute_prim_path_and_assign_prim_id()) {
    std::cerr << "compute_absolute_prim_path_and_assign_prim_id failed\n";
  }
  
  // Check the stage root prims directly
  std::cout << "Stage has " << stage.root_prims().size() << " root prims\n";
  for (const auto &root : stage.root_prims()) {
    std::cout << "  Root prim: " << root.element_name() 
              << " abs_path=" << root.absolute_path().full_path_name() << "\n";
    for (const auto &child : root.children()) {
      std::cout << "    Child prim: " << child.element_name() 
                << " abs_path=" << child.absolute_path().full_path_name() << "\n";
    }
  }
  
  {
    using MeshMap = std::map<std::string, const tinyusdz::GeomMesh *>;
    MeshMap meshes;
    tinyusdz::tydra::ListPrims(stage, meshes);
    
    std::cout << "Found " << meshes.size() << " meshes\n";
    
    for (const auto &mesh_item : meshes) {
      std::cout << "\nMesh path from map key: " << mesh_item.first << "\n";
      
      // Try to get the Prim at this path from the stage
      auto prim_result = stage.GetPrimAtPath(tinyusdz::Path(mesh_item.first, ""));
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
  }
  
  return 0;
}
