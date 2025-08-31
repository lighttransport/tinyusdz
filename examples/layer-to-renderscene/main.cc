#include <iostream>
#include <memory>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <sstream>

#include "../../src/tinyusdz.hh"
#include "../../src/tydra/layer-to-renderscene.hh"
#include "../../src/tydra/render-data.hh"
#include "../../src/usda-reader.hh"
#include "../../src/usdc-reader.hh"
#include "../../src/crate-reader.hh"
#include "../../src/io-util.hh"

using namespace tinyusdz;
using namespace tinyusdz::tydra;
using namespace tinyusdz::usda;
using namespace tinyusdz::crate;

class MemoryTracker {
 public:
  void ReportFreed(size_t bytes) {
    total_freed_ += bytes;
    std::cout << "  [Memory] Freed: " << FormatBytes(bytes) 
              << " (Total freed: " << FormatBytes(total_freed_) << ")\n";
  }
  
  void ReportPeak(size_t bytes) {
    std::cout << "  [Memory] Peak usage: " << FormatBytes(bytes) << "\n";
  }
  
 private:
  size_t total_freed_{0};
  
  std::string FormatBytes(size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int unit_idx = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024.0 && unit_idx < 3) {
      size /= 1024.0;
      unit_idx++;
    }
    
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << size << " " << units[unit_idx];
    return ss.str();
  }
};

void PrintRenderSceneStats(const RenderScene& scene) {
  std::cout << "\nRenderScene Statistics:\n";
  std::cout << "  Meshes: " << scene.meshes.size() << "\n";
  std::cout << "  Materials: " << scene.materials.size() << "\n";
  std::cout << "  Nodes: " << scene.nodes.size() << "\n";
  std::cout << "  Textures: " << scene.textures.size() << "\n";
  
  size_t total_vertices = 0;
  size_t total_triangles = 0;
  
  for (const auto& mesh : scene.meshes) {
    total_vertices += mesh.points.size();
    if (mesh.is_triangulated()) {
      total_triangles += mesh.triangulatedFaceVertexIndices.size() / 3;
    } else {
      for (uint32_t count : mesh.usdFaceVertexCounts) {
        if (count >= 3) {
          total_triangles += count - 2;
        }
      }
    }
  }
  
  std::cout << "  Total vertices: " << total_vertices << "\n";
  std::cout << "  Total triangles: " << total_triangles << "\n";
  
  size_t estimated_memory = 0;
  for (const auto& mesh : scene.meshes) {
    estimated_memory += mesh.estimate_memory_usage();
  }
  
  std::cout << "  Estimated RenderScene memory: " 
            << (estimated_memory / (1024.0 * 1024.0)) << " MB\n";
}

bool LoadLayerDirectly(const std::string& filename, std::unique_ptr<Layer>& layer) {
  std::string warn, err;
  
  // Simply use the tinyusdz loader
  Layer loaded_layer;
  if (!LoadLayerFromFile(filename, &loaded_layer, &warn, &err)) {
    std::cerr << "Failed to load file: " << err << std::endl;
    return false;
  }
  
  layer = std::make_unique<Layer>(std::move(loaded_layer));
  return true;
}

void TestNormalConversion(const std::string& filename) {
  std::cout << "\n=== Normal Conversion (Stage -> RenderScene) ===\n";
  
  auto start = std::chrono::high_resolution_clock::now();
  
  tinyusdz::Stage stage;
  std::string warn, err;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &stage, &warn, &err);
  
  if (!ret) {
    std::cerr << "Failed to load USD file: " << err << std::endl;
    return;
  }
  
  auto load_end = std::chrono::high_resolution_clock::now();
  
  RenderScene render_scene;
  RenderSceneConverter converter;
  
  RenderSceneConverterEnv env(stage);
  converter.ConvertToRenderScene(env, &render_scene);
  
  auto convert_end = std::chrono::high_resolution_clock::now();
  
  auto load_time = std::chrono::duration_cast<std::chrono::milliseconds>(load_end - start);
  auto convert_time = std::chrono::duration_cast<std::chrono::milliseconds>(convert_end - load_end);
  
  std::cout << "Load time: " << load_time.count() << " ms\n";
  std::cout << "Conversion time: " << convert_time.count() << " ms\n";
  std::cout << "Total time: " << (load_time.count() + convert_time.count()) << " ms\n";
  
  PrintRenderSceneStats(render_scene);
}

void TestDirectConversion(const std::string& filename, bool in_place) {
  std::cout << "\n=== Direct Conversion (Layer -> RenderScene, " 
            << (in_place ? "in-place" : "normal") << ") ===\n";
  
  auto start = std::chrono::high_resolution_clock::now();
  
  std::unique_ptr<Layer> layer;
  if (!LoadLayerDirectly(filename, layer)) {
    return;
  }
  
  auto load_end = std::chrono::high_resolution_clock::now();
  
  RenderScene render_scene;
  LayerToRenderSceneConverter converter;
  
  DirectConversionConfig config;
  config.triangulate = true;
  config.build_vertex_indices = true;
  config.enable_inplace_conversion = in_place;
  
  MemoryTracker tracker;
  
  config.progress_callback = [](const std::string& msg) {
    std::cout << "  [Progress] " << msg << "\n";
  };
  
  config.memory_freed_callback = [&tracker](size_t bytes) {
    tracker.ReportFreed(bytes);
  };
  
  converter.SetConfig(config);
  
  std::string warn, err;
  bool ret = false;
  
  if (in_place) {
    ret = converter.ConvertLayerInPlace(std::move(layer), &render_scene, &warn, &err);
  } else {
    ret = converter.ConvertLayer(layer.get(), &render_scene, &warn, &err);
  }
  
  auto convert_end = std::chrono::high_resolution_clock::now();
  
  if (!ret) {
    std::cerr << "Conversion failed: " << err << std::endl;
    return;
  }
  
  auto load_time = std::chrono::duration_cast<std::chrono::milliseconds>(load_end - start);
  auto convert_time = std::chrono::duration_cast<std::chrono::milliseconds>(convert_end - load_end);
  
  std::cout << "Load time: " << load_time.count() << " ms\n";
  std::cout << "Conversion time: " << convert_time.count() << " ms\n";
  std::cout << "Total time: " << (load_time.count() + convert_time.count()) << " ms\n";
  
  tracker.ReportPeak(converter.GetPeakMemoryUsage());
  
  PrintRenderSceneStats(render_scene);
}

void TestSinglePrimSpecConversion(const std::string& filename) {
  std::cout << "\n=== Single PrimSpec Conversion ===\n";
  
  std::unique_ptr<Layer> layer;
  if (!LoadLayerDirectly(filename, layer)) {
    return;
  }
  
  const auto& primspecs = layer->primspecs();
  
  for (const auto& item : primspecs) {
    const std::string& path = item.first;
    const PrimSpec& primspec = item.second;
    
    if (primspec.typeName() == "Mesh") {
      std::cout << "Converting mesh: " << path << "\n";
      
      RenderMesh mesh;
      LayerToRenderSceneConverter converter;
      
      DirectConversionConfig config;
      config.triangulate = true;
      converter.SetConfig(config);
      
      std::string warn, err;
      
      auto primspec_copy = std::make_unique<PrimSpec>(primspec);
      
      bool ret = converter.ConvertPrimSpecInPlace(std::move(primspec_copy), &mesh, &warn, &err);
      
      if (ret) {
        std::cout << "  Mesh converted successfully:\n";
        std::cout << "    Points: " << mesh.points.size() << "\n";
        std::cout << "    Faces: " << mesh.faceVertexCounts().size() << "\n";
        std::cout << "    Triangulated: " << (mesh.is_triangulated() ? "yes" : "no") << "\n";
        std::cout << "    Memory usage: " << (mesh.estimate_memory_usage() / 1024.0) << " KB\n";
      } else {
        std::cerr << "  Conversion failed: " << err << std::endl;
      }
      
      break;
    }
  }
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cout << "Usage: " << argv[0] << " <usd_file> [mode]\n";
    std::cout << "Modes:\n";
    std::cout << "  normal    - Traditional Stage->RenderScene conversion\n";
    std::cout << "  direct    - Direct Layer->RenderScene conversion\n";
    std::cout << "  inplace   - In-place Layer->RenderScene conversion\n";
    std::cout << "  primspec  - Single PrimSpec conversion example\n";
    std::cout << "  all       - Run all modes (default)\n";
    return 1;
  }
  
  std::string filename = argv[1];
  std::string mode = (argc > 2) ? argv[2] : "all";
  
  std::cout << "Processing file: " << filename << "\n";
  
  if (mode == "normal" || mode == "all") {
    TestNormalConversion(filename);
  }
  
  if (mode == "direct" || mode == "all") {
    TestDirectConversion(filename, false);
  }
  
  if (mode == "inplace" || mode == "all") {
    TestDirectConversion(filename, true);
  }
  
  if (mode == "primspec" || mode == "all") {
    TestSinglePrimSpecConversion(filename);
  }
  
  return 0;
}