// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Tydra Next - Render Scene Converter Implementation

#include "render-converter.hh"
#include <cmath>
#include <algorithm>
#include <unordered_set>

namespace tinyusdz {
namespace tydra {
namespace next {

using ::tinyusdz::next::Stage;
using ::tinyusdz::next::UsdPrim;
using ::tinyusdz::next::Value;

//
// Constructor / Destructor
//

RenderSceneConverter::RenderSceneConverter(const ConverterConfig& config)
    : config_(config) {}

RenderSceneConverter::~RenderSceneConverter() = default;

//
// Main conversion
//

ConvertResult RenderSceneConverter::Convert(const Stage& stage) {
  ConvertResult result;
  warnings_.clear();

  try {
    // Report progress
    if (config_.progress_callback) {
      config_.progress_callback(0.0f, "Starting conversion...");
    }

    // Set scene metadata
    auto meta = stage.GetMeta();
    result.scene.name = meta.defaultPrim;
    result.scene.default_prim = meta.defaultPrim;
    result.scene.meters_per_unit = static_cast<float>(meta.metersPerUnit);
    result.scene.up_axis = (meta.upAxis == "Z") ?
                           RenderScene::UpAxis::Z : RenderScene::UpAxis::Y;
    result.scene.start_time = meta.startTimeCode;
    result.scene.end_time = meta.endTimeCode;
    result.scene.frames_per_second = meta.timeCodesPerSecond;

    // Build node hierarchy first
    if (config_.progress_callback) {
      config_.progress_callback(0.1f, "Building node hierarchy...");
    }
    BuildNodeHierarchy(stage, &result.scene);

    // Convert meshes
    auto meshes = FindMeshes(stage);
    float mesh_progress_start = 0.2f;
    float mesh_progress_end = 0.5f;

    for (size_t i = 0; i < meshes.size(); ++i) {
      if (config_.progress_callback) {
        float p = mesh_progress_start +
                  (mesh_progress_end - mesh_progress_start) * i / meshes.size();
        config_.progress_callback(p, "Converting mesh: " + meshes[i].GetName());
      }

      RenderMesh mesh;
      if (ConvertMesh(meshes[i], &mesh)) {
        int32_t mesh_id = static_cast<int32_t>(result.scene.meshes.size());
        result.scene.mesh_by_path[mesh.prim_path] = mesh_id;
        result.scene.meshes.push_back(std::move(mesh));
      } else {
        warnings_.push_back("Failed to convert mesh: " + meshes[i].GetPath().str());
      }
    }

    // Convert materials
    auto materials = FindMaterials(stage);
    float mat_progress_start = 0.5f;
    float mat_progress_end = 0.7f;

    for (size_t i = 0; i < materials.size(); ++i) {
      if (config_.progress_callback) {
        float p = mat_progress_start +
                  (mat_progress_end - mat_progress_start) * i / materials.size();
        config_.progress_callback(p, "Converting material: " + materials[i].GetName());
      }

      RenderMaterial material;
      if (ConvertMaterial(stage, materials[i], &material)) {
        int32_t mat_id = static_cast<int32_t>(result.scene.materials.size());
        result.scene.material_by_path[material.prim_path] = mat_id;
        result.scene.materials.push_back(std::move(material));
      } else {
        warnings_.push_back("Failed to convert material: " + materials[i].GetPath().str());
      }
    }

    // Convert lights
    auto lights = FindLights(stage);
    for (const auto& light_prim : lights) {
      RenderLight light;
      if (ConvertLight(light_prim, &light)) {
        result.scene.lights.push_back(std::move(light));
      }
    }

    // Convert cameras
    auto cameras = FindCameras(stage);
    for (const auto& cam_prim : cameras) {
      RenderCamera camera;
      if (ConvertCamera(cam_prim, &camera)) {
        result.scene.cameras.push_back(std::move(camera));
      }
    }

    // Convert skeletons
    auto skeletons = FindSkeletons(stage);
    for (const auto& skel_prim : skeletons) {
      Skeleton skeleton;
      if (ConvertSkeleton(skel_prim, &skeleton)) {
        result.scene.skeletons.push_back(std::move(skeleton));
      }
    }

    if (config_.progress_callback) {
      config_.progress_callback(1.0f, "Conversion complete");
    }

    result.success = true;
    result.warnings = std::move(warnings_);

  } catch (const std::exception& e) {
    result.success = false;
    result.error = std::string("Exception during conversion: ") + e.what();
  }

  return result;
}

//
// Node hierarchy
//

void RenderSceneConverter::BuildNodeHierarchy(const Stage& stage, RenderScene* scene) {
  std::unordered_map<std::string, int32_t> path_to_node;

  stage.Traverse([&](const UsdPrim& prim) {
    SceneNode node;
    node.name = prim.GetName();
    node.prim_path = prim.GetPath().str();

    // Determine node type
    const std::string& type = prim.GetTypeName();
    if (type == "Mesh") node.type = NodeType::Mesh;
    else if (type == "Xform") node.type = NodeType::Xform;
    else if (type == "Camera") node.type = NodeType::Camera;
    else if (type == "Skeleton") node.type = NodeType::Skeleton;
    else if (IsLight(prim)) {
      LightKind kind = GetLightKind(prim);
      switch (kind) {
        case LightKind::DistantLight: node.type = NodeType::DirectionalLight; break;
        case LightKind::DomeLight: node.type = NodeType::DomeLight; break;
        case LightKind::RectLight: node.type = NodeType::RectLight; break;
        case LightKind::DiskLight: node.type = NodeType::DiskLight; break;
        case LightKind::SphereLight: node.type = NodeType::SphereLight; break;
        case LightKind::PointLight: node.type = NodeType::PointLight; break;
        default: node.type = NodeType::PointLight; break;
      }
    }

    // Compute transforms
    ComputeLocalTransform(prim, node.local_transform.m, config_.time_code);
    ComputeWorldTransform(stage, prim, node.world_transform.m, config_.time_code);

    // Get visibility
    bool visible = true;
    GetToken(prim, "visibility", nullptr);  // TODO: proper visibility handling
    node.visible = visible;

    int32_t node_id = static_cast<int32_t>(scene->nodes.size());
    path_to_node[node.prim_path] = node_id;
    scene->node_by_path[node.prim_path] = node_id;

    // Set parent
    std::string parent_path = GetParentPath(node.prim_path);
    if (!parent_path.empty() && parent_path != "/") {
      auto it = path_to_node.find(parent_path);
      if (it != path_to_node.end()) {
        node.parent_id = it->second;
        scene->nodes[it->second].children.push_back(node_id);
      }
    } else {
      scene->root_nodes.push_back(node_id);
    }

    scene->nodes.push_back(std::move(node));
    return true;
  });
}

//
// Mesh conversion
//

bool RenderSceneConverter::ConvertMesh(const UsdPrim& prim, RenderMesh* out) {
  if (!out || !IsMesh(prim)) {
    last_error_ = "Invalid mesh prim";
    return false;
  }

  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();

  // Extract topology
  if (!ExtractMeshTopology(prim, out)) {
    return false;
  }

  // Extract geometry
  if (!ExtractMeshGeometry(prim, out)) {
    return false;
  }

  // Extract primvars (UVs, colors, etc.)
  ExtractMeshPrimvars(prim, out);

  // Triangulate if requested
  if (config_.mesh.triangulate && !out->is_triangulated) {
    TriangulateMesh(out);
  }

  // Compute normals if needed
  if (config_.mesh.compute_normals && out->normals.empty()) {
    ComputeVertexNormals(out);
  }

  return true;
}

bool RenderSceneConverter::ExtractMeshTopology(const UsdPrim& prim, RenderMesh* mesh) {
  // Get face vertex counts
  std::vector<int32_t> face_counts = GetIntArray(prim, "faceVertexCounts");
  if (face_counts.empty()) {
    last_error_ = "Mesh has no faceVertexCounts";
    return false;
  }

  mesh->face_vertex_counts.reserve(face_counts.size());
  for (int32_t c : face_counts) {
    mesh->face_vertex_counts.push_back(static_cast<uint32_t>(c));
  }

  // Get face vertex indices
  std::vector<int32_t> indices = GetIntArray(prim, "faceVertexIndices");
  if (indices.empty()) {
    last_error_ = "Mesh has no faceVertexIndices";
    return false;
  }

  mesh->face_vertex_indices.reserve(indices.size());
  for (int32_t i : indices) {
    mesh->face_vertex_indices.push_back(static_cast<uint32_t>(i));
  }

  return true;
}

bool RenderSceneConverter::ExtractMeshGeometry(const UsdPrim& prim, RenderMesh* mesh) {
  // Get points directly into chunked array
  const Value* points_val = GetAttribute(prim, "points");
  if (!points_val) {
    last_error_ = "Mesh has no points";
    return false;
  }

  const std::vector<float>* points = points_val->as_float_array();
  if (!points || points->empty()) {
    last_error_ = "Invalid points data";
    return false;
  }

  // Copy directly to chunked array
  mesh->points.append(points->data(), points->size());

  // Compute bounding box
  size_t num_points = mesh->point_count();
  if (num_points > 0) {
    mesh->bbox_min = Float3(1e30f, 1e30f, 1e30f);
    mesh->bbox_max = Float3(-1e30f, -1e30f, -1e30f);

    for (size_t i = 0; i < num_points; ++i) {
      float x = mesh->points[i * 3 + 0];
      float y = mesh->points[i * 3 + 1];
      float z = mesh->points[i * 3 + 2];

      mesh->bbox_min.x = std::min(mesh->bbox_min.x, x);
      mesh->bbox_min.y = std::min(mesh->bbox_min.y, y);
      mesh->bbox_min.z = std::min(mesh->bbox_min.z, z);
      mesh->bbox_max.x = std::max(mesh->bbox_max.x, x);
      mesh->bbox_max.y = std::max(mesh->bbox_max.y, y);
      mesh->bbox_max.z = std::max(mesh->bbox_max.z, z);
    }
    mesh->has_bbox = true;
  }

  // Get normals if present
  const Value* normals_val = GetAttribute(prim, "normals");
  if (normals_val) {
    const std::vector<float>* normals = normals_val->as_float_array();
    if (normals && !normals->empty()) {
      mesh->normals.append(normals->data(), normals->size());

      // Determine interpolation
      std::string interp;
      GetToken(prim, "normals:interpolation", &interp);
      if (interp == "faceVarying") {
        mesh->normals_interp = Interpolation::FaceVarying;
      } else {
        mesh->normals_interp = Interpolation::Vertex;
      }
    }
  }

  return true;
}

bool RenderSceneConverter::ExtractMeshPrimvars(const UsdPrim& prim, RenderMesh* mesh) {
  // Get UV coordinates
  std::string uv_name = "primvars:" + config_.mesh.default_uv_primvar;
  const Value* uv_val = GetAttribute(prim, uv_name);
  if (uv_val) {
    const std::vector<float>* uvs = uv_val->as_float_array();
    if (uvs && !uvs->empty()) {
      mesh->texcoords_0.append(uvs->data(), uvs->size());

      std::string interp;
      GetToken(prim, uv_name + ":interpolation", &interp);
      if (interp == "faceVarying") {
        mesh->texcoords_0_interp = Interpolation::FaceVarying;
      } else {
        mesh->texcoords_0_interp = Interpolation::Vertex;
      }
    }
  }

  // Get vertex colors
  const Value* colors_val = GetAttribute(prim, "primvars:displayColor");
  if (colors_val) {
    const std::vector<float>* colors = colors_val->as_float_array();
    if (colors && !colors->empty()) {
      mesh->colors.append(colors->data(), colors->size());
    }
  }

  return true;
}

//
// Triangulation
//

bool RenderSceneConverter::TriangulateMesh(RenderMesh* mesh) {
  if (mesh->face_vertex_counts.empty()) return false;

  // Check if already triangulated
  bool all_triangles = true;
  for (size_t i = 0; i < mesh->face_vertex_counts.size(); ++i) {
    if (mesh->face_vertex_counts[i] != 3) {
      all_triangles = false;
      break;
    }
  }

  if (all_triangles) {
    // Just copy indices
    mesh->triangulated_indices.resize(mesh->face_vertex_indices.size());
    for (size_t i = 0; i < mesh->face_vertex_indices.size(); ++i) {
      mesh->triangulated_indices[i] = mesh->face_vertex_indices[i];
    }
    mesh->is_triangulated = true;
    return true;
  }

  // Use triangle fan method
  std::vector<uint32_t> face_counts_vec = mesh->face_vertex_counts.flatten();
  std::vector<uint32_t> indices_vec = mesh->face_vertex_indices.flatten();

  return TriangulateFan(face_counts_vec.data(), face_counts_vec.size(),
                        indices_vec.data(), indices_vec.size(),
                        &mesh->triangulated_indices);
}

bool RenderSceneConverter::TriangulateFan(
    const uint32_t* face_vertex_counts, size_t face_count,
    const uint32_t* indices, size_t index_count,
    UInt32Chunked* out_indices) {

  // Count triangles
  size_t tri_count = 0;
  for (size_t i = 0; i < face_count; ++i) {
    uint32_t nverts = face_vertex_counts[i];
    if (nverts >= 3) {
      tri_count += nverts - 2;
    }
  }

  out_indices->reserve(tri_count * 3);

  size_t idx_offset = 0;
  for (size_t f = 0; f < face_count; ++f) {
    uint32_t nverts = face_vertex_counts[f];
    if (nverts < 3) {
      idx_offset += nverts;
      continue;
    }

    // Triangle fan: v0, v1, v2; v0, v2, v3; v0, v3, v4; ...
    uint32_t v0 = indices[idx_offset];
    for (uint32_t i = 1; i < nverts - 1; ++i) {
      out_indices->push_back(v0);
      out_indices->push_back(indices[idx_offset + i]);
      out_indices->push_back(indices[idx_offset + i + 1]);
    }

    idx_offset += nverts;
  }

  return true;
}

//
// Normal computation
//

bool RenderSceneConverter::ComputeVertexNormals(RenderMesh* mesh) {
  if (mesh->points.empty() || !mesh->is_triangulated) {
    // Need triangulated mesh for normal computation
    if (!mesh->is_triangulated) {
      TriangulateMesh(mesh);
    }
    if (!mesh->is_triangulated) return false;
  }

  size_t num_points = mesh->point_count();
  size_t num_tris = mesh->triangulated_indices.size() / 3;

  // Initialize normals to zero
  mesh->normals.resize(num_points * 3, 0.0f);

  // Accumulate face normals at each vertex
  for (size_t t = 0; t < num_tris; ++t) {
    uint32_t i0 = mesh->triangulated_indices[t * 3 + 0];
    uint32_t i1 = mesh->triangulated_indices[t * 3 + 1];
    uint32_t i2 = mesh->triangulated_indices[t * 3 + 2];

    float p0[3] = {mesh->points[i0*3], mesh->points[i0*3+1], mesh->points[i0*3+2]};
    float p1[3] = {mesh->points[i1*3], mesh->points[i1*3+1], mesh->points[i1*3+2]};
    float p2[3] = {mesh->points[i2*3], mesh->points[i2*3+1], mesh->points[i2*3+2]};

    // Edge vectors
    float e1[3] = {p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2]};
    float e2[3] = {p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2]};

    // Cross product
    float n[3] = {
      e1[1]*e2[2] - e1[2]*e2[1],
      e1[2]*e2[0] - e1[0]*e2[2],
      e1[0]*e2[1] - e1[1]*e2[0]
    };

    // Add to each vertex
    mesh->normals[i0*3+0] += n[0]; mesh->normals[i0*3+1] += n[1]; mesh->normals[i0*3+2] += n[2];
    mesh->normals[i1*3+0] += n[0]; mesh->normals[i1*3+1] += n[1]; mesh->normals[i1*3+2] += n[2];
    mesh->normals[i2*3+0] += n[0]; mesh->normals[i2*3+1] += n[1]; mesh->normals[i2*3+2] += n[2];
  }

  // Normalize
  for (size_t v = 0; v < num_points; ++v) {
    float nx = mesh->normals[v*3+0];
    float ny = mesh->normals[v*3+1];
    float nz = mesh->normals[v*3+2];
    float len = std::sqrt(nx*nx + ny*ny + nz*nz);
    if (len > 1e-8f) {
      mesh->normals[v*3+0] = nx / len;
      mesh->normals[v*3+1] = ny / len;
      mesh->normals[v*3+2] = nz / len;
    } else {
      mesh->normals[v*3+0] = 0.0f;
      mesh->normals[v*3+1] = 1.0f;
      mesh->normals[v*3+2] = 0.0f;
    }
  }

  mesh->normals_interp = Interpolation::Vertex;
  return true;
}

//
// Material conversion
//

bool RenderSceneConverter::ConvertMaterial(const Stage& stage, const UsdPrim& prim, RenderMaterial* out) {
  if (!out || !IsMaterial(prim)) {
    last_error_ = "Invalid material prim";
    return false;
  }

  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();

  // Find shader(s) in material
  bool found_shader = false;

  auto children = prim.GetChildren();
  for (const auto& child : children) {
    if (IsShader(child)) {
      std::string shader_id;
      GetToken(child, "info:id", &shader_id);

      if (shader_id == "UsdPreviewSurface") {
        out->shader_type = RenderMaterial::ShaderType::PreviewSurface;
        out->preview_surface = std::make_unique<PreviewSurfaceShader>();
        // TODO: Extract shader parameters
        found_shader = true;
      } else if (shader_id == "ND_open_pbr_surface_surfaceshader") {
        out->shader_type = RenderMaterial::ShaderType::OpenPBR;
        out->openpbr = std::make_unique<OpenPBRSurfaceShader>();
        // TODO: Extract OpenPBR parameters
        found_shader = true;
      }
    }
  }

  (void)stage;  // Suppress unused parameter warning
  return found_shader;
}

//
// Light conversion
//

bool RenderSceneConverter::ConvertLight(const UsdPrim& prim, RenderLight* out) {
  if (!out || !IsLight(prim)) {
    last_error_ = "Invalid light prim";
    return false;
  }

  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();

  // Determine light type
  LightKind kind = GetLightKind(prim);
  switch (kind) {
    case LightKind::DistantLight: out->type = LightType::Directional; break;
    case LightKind::DomeLight: out->type = LightType::Dome; break;
    case LightKind::RectLight: out->type = LightType::Rect; break;
    case LightKind::DiskLight: out->type = LightType::Disk; break;
    case LightKind::SphereLight: out->type = LightType::Sphere; break;
    case LightKind::CylinderLight: out->type = LightType::Cylinder; break;
    default: out->type = LightType::Point; break;
  }

  // Common properties
  GetFloat3(prim, "inputs:color", &out->color.x, &out->color.y, &out->color.z);
  GetFloat(prim, "inputs:intensity", &out->intensity);
  GetFloat(prim, "inputs:exposure", &out->exposure);
  GetBool(prim, "inputs:normalize", &out->normalize);

  // Type-specific properties
  switch (out->type) {
    case LightType::Sphere:
      GetFloat(prim, "inputs:radius", &out->params.sphere.radius);
      break;
    case LightType::Rect:
      GetFloat(prim, "inputs:width", &out->params.rect.width);
      GetFloat(prim, "inputs:height", &out->params.rect.height);
      break;
    case LightType::Disk:
      GetFloat(prim, "inputs:radius", &out->params.disk.radius);
      break;
    default:
      break;
  }

  // Shadow settings
  GetBool(prim, "inputs:enableShadows", &out->enable_shadow);

  return true;
}

//
// Camera conversion
//

bool RenderSceneConverter::ConvertCamera(const UsdPrim& prim, RenderCamera* out) {
  if (!out || !IsCamera(prim)) {
    last_error_ = "Invalid camera prim";
    return false;
  }

  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();

  // Projection type
  std::string projection;
  GetToken(prim, "projection", &projection);
  out->type = (projection == "orthographic") ?
              CameraType::Orthographic : CameraType::Perspective;

  // Lens parameters
  GetFloat(prim, "focalLength", &out->focal_length);
  GetFloat(prim, "horizontalAperture", &out->horizontal_aperture);
  GetFloat(prim, "verticalAperture", &out->vertical_aperture);

  // Clipping
  float clip_range[2] = {0.1f, 10000.0f};
  const Value* clip_val = GetAttribute(prim, "clippingRange");
  if (clip_val) {
    const float* cr = clip_val->as_float2();
    if (cr) {
      clip_range[0] = cr[0];
      clip_range[1] = cr[1];
    }
  }
  out->near_clip = clip_range[0];
  out->far_clip = clip_range[1];

  return true;
}

//
// Skeleton conversion
//

bool RenderSceneConverter::ConvertSkeleton(const UsdPrim& prim, Skeleton* out) {
  if (!out || !IsSkeleton(prim)) {
    last_error_ = "Invalid skeleton prim";
    return false;
  }

  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();

  // TODO: Extract joints, bind transforms, rest transforms

  return true;
}

//
// Animation conversion
//

bool RenderSceneConverter::ConvertAnimation(const UsdPrim& prim, AnimationClip* out) {
  // TODO: Implement animation extraction
  return false;
}

//
// Texture loading
//

bool RenderSceneConverter::LoadTexture(const std::string& asset_path, TextureImage* out) {
  if (!out) return false;

  // Use custom loader if provided
  if (config_.material.custom_texture_loader) {
    return config_.material.custom_texture_loader(asset_path, out);
  }

  // TODO: Implement default texture loading
  // For now, just store the path
  out->resolved_path = asset_path;

  return true;
}

//
// Utility functions
//

void ComputeTriangleNormal(const float* p0, const float* p1, const float* p2, float* normal) {
  float e1[3] = {p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2]};
  float e2[3] = {p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2]};

  normal[0] = e1[1]*e2[2] - e1[2]*e2[1];
  normal[1] = e1[2]*e2[0] - e1[0]*e2[2];
  normal[2] = e1[0]*e2[1] - e1[1]*e2[0];

  float len = std::sqrt(normal[0]*normal[0] + normal[1]*normal[1] + normal[2]*normal[2]);
  if (len > 1e-8f) {
    normal[0] /= len;
    normal[1] /= len;
    normal[2] /= len;
  }
}

}  // namespace next
}  // namespace tydra
}  // namespace tinyusdz
