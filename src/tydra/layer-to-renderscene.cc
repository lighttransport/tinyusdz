#include "layer-to-renderscene.hh"
#include "common-utils.hh"
#include "common-types.hh"

#include <algorithm>
#include <unordered_map>
#include <cstring>
#include <vector>

#include "../prim-types.hh"
#include "../layer.hh"
#include "../usdGeom.hh"
#include "../value-types.hh"
#include "../tinyusdz.hh"
#include "../prim-reconstruct.hh"
#include "../str-util.hh"
#include "../pprinter.hh"

namespace tinyusdz {
namespace tydra {

namespace {

constexpr const char* kInPlaceConversionDisabledMessage =
    "In-place LayerToRenderScene conversion is temporarily disabled because "
    "destructive source transfer is not implemented safely yet. Use "
    "ConvertLayer or ConvertPrimSpec instead.";

#if 0
template<typename T>
void MoveVector(std::vector<T>& src, std::vector<T>& dst) {
  dst = std::move(src);
  src.clear();
  src.shrink_to_fit();
}
#endif

#if 0
template<typename T>
void ExtractAndClearAnimatable(Animatable<T>& src, T* default_val, TypedTimeSamples<T>* ts) {
  if (src.has_value() && default_val) {
    src.get_scalar(default_val);
  }
  
  if (src.has_timesamples() && ts) {
    *ts = std::move(const_cast<TypedTimeSamples<T>&>(src.get_timesamples()));
  }
  
  src.clear_scalar();
  src.clear_timesamples();
}
#endif

// TODO: Fix these functions once Property API is clarified
// bool ConvertPrimvarToVertexAttribute(
//     const Animatable<std::vector<value::float3>>& primvar,
//     VertexAttribute* vertex_attr,
//     bool free_source) {
//   
//   if (!vertex_attr) return false;
//   
//   vertex_attr->format = VertexAttributeFormat::Vec3;
//   vertex_attr->variability = VertexVariability::Vertex;
//   
//   if (primvar.has_value()) {
//     std::vector<value::float3> data;
//     primvar.get_scalar(&data);
//     
//     vertex_attr->data.resize(data.size() * sizeof(float) * 3);
//     memcpy(vertex_attr->data.data(), data.data(), vertex_attr->data.size());
//     
//     if (free_source) {
//       const_cast<Animatable<std::vector<value::float3>>&>(primvar).clear_scalar();
//     }
//   }
//   
//   return true;
// }
// 
// bool ConvertPrimvarToVertexAttribute(
//     const Animatable<std::vector<value::float2>>& primvar,
//     VertexAttribute* vertex_attr,
//     bool free_source) {
//   
//   if (!vertex_attr) return false;
//   
//   vertex_attr->format = VertexAttributeFormat::Vec2;
//   vertex_attr->variability = VertexVariability::Vertex;
//   
//   if (primvar.has_value()) {
//     std::vector<value::float2> data;
//     primvar.get_scalar(&data);
//     
//     vertex_attr->data.resize(data.size() * sizeof(float) * 2);
//     memcpy(vertex_attr->data.data(), data.data(), vertex_attr->data.size());
//     
//     if (free_source) {
//       const_cast<Animatable<std::vector<value::float2>>&>(primvar).clear_scalar();
//     }
//   }
//   
//   return true;
// }

}  // anonymous namespace

struct LayerToRenderSceneConverter::Impl {
  std::unordered_map<std::string, int> material_path_to_id;
  std::unordered_map<std::string, int> mesh_path_to_id;
  
  void ClearCaches() {
    material_path_to_id.clear();
    mesh_path_to_id.clear();
  }
};

LayerToRenderSceneConverter::LayerToRenderSceneConverter() 
    : impl_(std::make_unique<Impl>()) {
}

LayerToRenderSceneConverter::~LayerToRenderSceneConverter() = default;

bool LayerToRenderSceneConverter::ConvertLayer(
    Layer* layer, 
    RenderScene* render_scene,
    std::string* warn,
    std::string* err) {

  (void)warn;
  
  if (!layer || !render_scene) {
    if (err) {
      *err = "Invalid input: layer or render_scene is null";
    }
    return false;
  }
  
  impl_->ClearCaches();
  
  if (config_.progress_callback) {
    config_.progress_callback("Starting Layer to RenderScene conversion");
  }
  
  const auto& primspecs = layer->primspecs();
  
  for (const auto& item : primspecs) {
    const std::string& path = item.first;
    const PrimSpec& primspec = item.second;
    if (config_.progress_callback) {
      config_.progress_callback("Processing: " + path);
    }
    
    // Skip invalid primspecs if needed
    
    const std::string& typeName = primspec.typeName();
    
    if (typeName == "Mesh") {
      RenderMesh mesh;
      if (ConvertGeomMeshPrimSpec(&primspec, &mesh, false)) {
        mesh.prim_name = primspec.name();
        mesh.abs_path = path;
        
        int mesh_id = static_cast<int>(render_scene->meshes.size());
        render_scene->meshes.push_back(std::move(mesh));
        impl_->mesh_path_to_id[path] = mesh_id;
      }
    } else if (typeName == "Material") {
      RenderMaterial material;
      if (ConvertMaterialPrimSpec(&primspec, &material, false)) {
        material.name = primspec.name();
        material.abs_path = path;
        
        int material_id = static_cast<int>(render_scene->materials.size());
        render_scene->materials.push_back(std::move(material));
        impl_->material_path_to_id[path] = material_id;
      }
    } else if (typeName == "Xform" || typeName == "Scope") {
      Node node;
      if (ConvertXformPrimSpec(&primspec, &node, false)) {
        node.prim_name = primspec.name();
        node.abs_path = path;
        render_scene->nodes.push_back(std::move(node));
      }
    }
  }
  
  if (config_.progress_callback) {
    config_.progress_callback("Conversion complete");
  }
  
  return true;
}

bool LayerToRenderSceneConverter::ConvertLayerInPlace(
    std::unique_ptr<Layer> layer,
    RenderScene* render_scene,
    std::string* warn,
    std::string* err) {

  (void)warn;
  if (!layer || !render_scene) {
    if (err) {
      *err = "Invalid input: layer or render_scene is null";
    }
    return false;
  }
  
  impl_->ClearCaches();
  
  if (config_.progress_callback) {
    config_.progress_callback("Starting in-place Layer to RenderScene conversion");
  }

  if (err) {
    *err = kInPlaceConversionDisabledMessage;
  }

  if (config_.progress_callback) {
    config_.progress_callback("In-place conversion aborted: unsafe transfer path is disabled");
  }

  return false;
}

bool LayerToRenderSceneConverter::ConvertPrimSpec(
    PrimSpec* prim_spec,
    RenderMesh* render_mesh,
    std::string* warn,
    std::string* err) {

  (void)warn;
  
  if (!prim_spec || !render_mesh) {
    if (err) {
      *err = "Invalid input: prim_spec or render_mesh is null";
    }
    return false;
  }
  
  return ConvertGeomMeshPrimSpec(prim_spec, render_mesh, false);
}

bool LayerToRenderSceneConverter::ConvertPrimSpecInPlace(
    std::unique_ptr<PrimSpec> prim_spec,
    RenderMesh* render_mesh,
    std::string* warn,
    std::string* err) {

  (void)warn;

  if (!prim_spec || !render_mesh) {
    if (err) {
      *err = "Invalid input: prim_spec or render_mesh is null";
    }
    return false;
  }

  if (err) {
    *err = kInPlaceConversionDisabledMessage;
  }

  if (config_.progress_callback) {
    config_.progress_callback("In-place PrimSpec conversion aborted: unsafe transfer path is disabled");
  }

  return false;
}

bool LayerToRenderSceneConverter::ConvertGeomMeshPrimSpec(
    const PrimSpec* prim_spec,
    RenderMesh* render_mesh,
    bool free_source) {

  (void)free_source;
  
  if (!prim_spec || !render_mesh) {
    return false;
  }
  
  render_mesh->prim_name = prim_spec->name();
  
  const auto& props = prim_spec->props();
  
  auto points_it = props.find("points");
  if (points_it != props.end()) {
    if (points_it->second.is_attribute()) {
      // const auto* attr_prop = &points_it->second;
      // TODO: Fix template syntax for get_animatable
      // if (attr_prop->is_animatable_typeName("float3[]")) {
      //   auto typed_attr = attr_prop->get_animatable<Animatable<std::vector<value::float3>>>();
      //   if (typed_attr && typed_attr->has_value()) {
      //     std::vector<value::float3> points_data;
      //     typed_attr->get(&points_data);
      //     
      //     render_mesh->points.reserve(points_data.size());
      //     for (const auto& p : points_data) {
      //       render_mesh->points.push_back(vec3{p[0], p[1], p[2]});
      //     }
      //     
      //     if (free_source) {
      //       typed_attr->clear_scalar();
      //       typed_attr->clear_timesamples();
      //     }
      //   }
      // }
    }
  }
  
  auto face_indices_it = props.find("faceVertexIndices");
  if (face_indices_it != props.end()) {
    if (face_indices_it->second.is_attribute()) {
      // TODO: Fix Property API usage
      // const auto* attr_prop = &face_indices_it->second;
      // if (attr_prop->is_value_typeName("int[]")) {
      //   auto typed_attr = attr_prop->get_value<std::vector<int>>();
      //   if (typed_attr) {
      //     render_mesh->usdFaceVertexIndices.reserve(typed_attr->size());
      //     for (int idx : *typed_attr) {
      //       render_mesh->usdFaceVertexIndices.push_back(static_cast<uint32_t>(idx));
      //     }
      //     
      //     if (free_source) {
      //       const_cast<std::vector<int>&>(*typed_attr).clear();
      //       const_cast<std::vector<int>&>(*typed_attr).shrink_to_fit();
      //     }
      //   }
      // }
    }
  }
  
  auto face_counts_it = props.find("faceVertexCounts");
  if (face_counts_it != props.end()) {
    if (face_counts_it->second.is_attribute()) {
      // TODO: Fix Property API usage
      // const auto* attr_prop = &face_counts_it->second;
      // if (attr_prop->is_value_typeName("int[]")) {
      //   auto typed_attr = attr_prop->get_value<std::vector<int>>();
      //   if (typed_attr) {
      //     render_mesh->usdFaceVertexCounts.reserve(typed_attr->size());
      //     for (int count : *typed_attr) {
      //       render_mesh->usdFaceVertexCounts.push_back(static_cast<uint32_t>(count));
      //     }
      //     
      //     if (free_source) {
      //       const_cast<std::vector<int>&>(*typed_attr).clear();
      //       const_cast<std::vector<int>&>(*typed_attr).shrink_to_fit();
      //     }
      //   }
      // }
    }
  }
  
  if (config_.triangulate) {
    size_t num_faces = render_mesh->usdFaceVertexCounts.size();
    size_t num_triangles = 0;
    
    for (uint32_t count : render_mesh->usdFaceVertexCounts) {
      if (count >= 3) {
        num_triangles += count - 2;
      }
    }
    
    render_mesh->triangulatedFaceVertexIndices.reserve(num_triangles * 3);
    render_mesh->triangulatedFaceVertexCounts.resize(num_triangles, 3);
    
    size_t src_idx = 0;
    for (size_t face_idx = 0; face_idx < num_faces; ++face_idx) {
      uint32_t num_verts = render_mesh->usdFaceVertexCounts[face_idx];
      
      if (num_verts < 3) {
        src_idx += num_verts;
        continue;
      }
      
      uint32_t v0 = render_mesh->usdFaceVertexIndices[src_idx];
      for (uint32_t i = 1; i < num_verts - 1; ++i) {
        uint32_t v1 = render_mesh->usdFaceVertexIndices[src_idx + i];
        uint32_t v2 = render_mesh->usdFaceVertexIndices[src_idx + i + 1];
        
        render_mesh->triangulatedFaceVertexIndices.push_back(v0);
        render_mesh->triangulatedFaceVertexIndices.push_back(v1);
        render_mesh->triangulatedFaceVertexIndices.push_back(v2);
      }
      
      src_idx += num_verts;
    }
  }
  
  auto normals_it = props.find("normals");
  if (normals_it != props.end()) {
    if (normals_it->second.is_attribute()) {
      // const auto* attr_prop = &normals_it->second;
      // TODO: Fix template syntax for get_animatable
      // if (attr_prop->is_animatable_typeName("float3[]")) {
      //   auto typed_attr = attr_prop->get_animatable<Animatable<std::vector<value::float3>>>();
      //   if (typed_attr) {
      //     ConvertPrimvarToVertexAttribute(*typed_attr, &render_mesh->normals, free_source);
      //   }
      // }
    }
  }
  
  auto doubleSided_it = props.find("doubleSided");
  if (doubleSided_it != props.end()) {
    if (doubleSided_it->second.is_attribute()) {
      // TODO: Fix Property API usage
      // const auto* attr_prop = &doubleSided_it->second;
      // if (attr_prop->is_value_typeName("bool")) {
      //   auto typed_attr = attr_prop->get_value<bool>();
      //   if (typed_attr) {
      //     render_mesh->doubleSided = *typed_attr;
      //   }
      // }
    }
  }
  
  size_t estimated_size = render_mesh->estimate_memory_usage();
  TrackMemoryUsage(estimated_size, 0);
  
  return true;
}

bool LayerToRenderSceneConverter::ConvertMaterialPrimSpec(
    const PrimSpec* prim_spec,
    RenderMaterial* render_material,
    bool free_source) {
  
  (void)free_source;
  if (!prim_spec || !render_material) {
    return false;
  }
  
  render_material->name = prim_spec->name();
  
  return true;
}

bool LayerToRenderSceneConverter::ConvertXformPrimSpec(
    const PrimSpec* prim_spec,
    Node* node,
    bool free_source) {
  
  (void)free_source;
  if (!prim_spec || !node) {
    return false;
  }
  
  node->prim_name = prim_spec->name();
  node->nodeType = NodeType::Xform;
  
  const auto& props = prim_spec->props();
  
  auto xform_it = props.find("xformOp:transform");
  if (xform_it != props.end()) {
    if (xform_it->second.is_attribute()) {
      // TODO: Fix Property API usage
      // const auto* attr_prop = &xform_it->second;
      // if (attr_prop->is_value_typeName("matrix4d")) {
      //   auto typed_attr = attr_prop->get_value<value::matrix4d>();
      //   if (typed_attr) {
      //     node->local_matrix = *typed_attr;
      //     node->global_matrix = *typed_attr;
      //   }
      // }
    }
  }
  
  return true;
}

void LayerToRenderSceneConverter::TrackMemoryUsage(size_t bytes_allocated, size_t bytes_freed) {
  current_memory_usage_ += bytes_allocated;
  current_memory_usage_ -= bytes_freed;
  
  if (current_memory_usage_ > peak_memory_usage_) {
    peak_memory_usage_ = current_memory_usage_;
  }
  
  if (current_memory_usage_ > config_.max_memory_limit_mb * 1024 * 1024) {
    if (config_.progress_callback) {
      config_.progress_callback("Warning: Memory limit exceeded");
    }
  }
}

}  // namespace tydra
}  // namespace tinyusdz
