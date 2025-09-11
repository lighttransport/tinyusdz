#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>

#include "../prim-types.hh"
#include "common-types.hh"
#include "render-data.hh"

namespace tinyusdz {

class Layer;
class PrimSpec;

namespace tydra {

struct DirectConversionConfig {
  bool triangulate{true};
  bool build_vertex_indices{true};
  bool compute_tangents_and_binormals{false};
  bool flatten_primvars{true};
  
  size_t max_memory_limit_mb{16384}; 
  
  bool enable_inplace_conversion{true};
  
  std::function<void(const std::string&)> progress_callback;
  std::function<void(size_t bytes_freed)> memory_freed_callback;
};

class LayerToRenderSceneConverter {
 public:
  LayerToRenderSceneConverter();
  ~LayerToRenderSceneConverter();

  void SetConfig(const DirectConversionConfig& config) {
    config_ = config;
  }

  bool ConvertLayer(Layer* layer, RenderScene* render_scene, std::string* warn, std::string* err);
  
  bool ConvertLayerInPlace(std::unique_ptr<Layer> layer, RenderScene* render_scene, std::string* warn, std::string* err);

  bool ConvertPrimSpec(PrimSpec* prim_spec, RenderMesh* render_mesh, std::string* warn, std::string* err);
  
  bool ConvertPrimSpecInPlace(std::unique_ptr<PrimSpec> prim_spec, RenderMesh* render_mesh, std::string* warn, std::string* err);

  size_t GetPeakMemoryUsage() const { return peak_memory_usage_; }
  size_t GetCurrentMemoryUsage() const { return current_memory_usage_; }
  
 private:
  bool ConvertGeomMeshPrimSpec(const PrimSpec* prim_spec, RenderMesh* render_mesh, bool free_source);
  bool ConvertMaterialPrimSpec(const PrimSpec* prim_spec, RenderMaterial* render_material, bool free_source);
  bool ConvertXformPrimSpec(const PrimSpec* prim_spec, Node* node, bool free_source);
  
  template<typename T>
  bool ExtractAndConvertAttribute(const PrimSpec* prim_spec, const std::string& attr_name, 
                                   VertexAttribute* vertex_attr, bool free_source);
  
  template<typename T>
  void FreeAttributeMemory(TypedAttribute<T>* attr);
  
  void TrackMemoryUsage(size_t bytes_allocated, size_t bytes_freed);
  
  DirectConversionConfig config_;
  size_t peak_memory_usage_{0};
  size_t current_memory_usage_{0};
  
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

template<typename T>
class InPlaceDataExtractor {
 public:
  static bool ExtractAndFree(T* source, std::vector<typename T::value_type>* dest) {
    if (!source || !dest) return false;
    
    *dest = std::move(source->data);
    
    source->data.clear();
    source->data.shrink_to_fit();
    
    return true;
  }
  
  template<typename U>
  static bool ExtractAnimatable(Animatable<U>* source, U* dest_default, 
                                TypedTimeSamples<U>* dest_samples, bool free_source) {
    if (!source) return false;
    
    if (source->has_value() && dest_default) {
      source->get_scalar(dest_default);
    }
    
    if (source->has_timesamples() && dest_samples) {
      *dest_samples = std::move(const_cast<TypedTimeSamples<U>&>(source->get_timesamples()));
    }
    
    if (free_source) {
      source->clear_scalar();
      source->clear_timesamples();
    }
    
    return true;
  }
};

template<typename T>
size_t EstimateMemorySize(const T& data);

template<>
inline size_t EstimateMemorySize(const std::vector<float>& data) {
  return data.capacity() * sizeof(float);
}

template<>
inline size_t EstimateMemorySize(const std::vector<vec3>& data) {
  return data.capacity() * sizeof(vec3);
}

template<>
inline size_t EstimateMemorySize(const std::vector<uint32_t>& data) {
  return data.capacity() * sizeof(uint32_t);
}

}  // namespace tydra
}  // namespace tinyusdz

