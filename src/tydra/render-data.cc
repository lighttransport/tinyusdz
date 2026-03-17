// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// TODO:
//   - [ ] Subdivision surface to polygon mesh conversion.
//     - [ ] Correctly handle primvar with 'vertex' interpolation(Use the basis
//     function of subd surface)
//   - [ ] Support Inbetween BlendShape
//   - [ ] Support material binding collection(Collection API)
//   - [ ] Support multiple skel animation
//   https://github.com/PixarAnimationStudios/OpenUSD/issues/2246
//   - [ ] Adjust normal vector computation with handness?
//   - [ ] Node xform animation
//   - [ ] Better build of index buffer
//     - [ ] Preserve the order of 'points' variable(mesh.points, Skin
//     indices/weights, BlendShape points, ...) as much as possible.
//     - Implement spatial hash
//
#include <numeric>
#include <set>

#include "common-utils.hh"
#include "common-types.hh"
#include "image-loader.hh"
#include "image-util.hh"
#include "image-types.hh"
#include "linear-algebra.hh"
#include "math-util.inc"
#include "pprinter.hh"
#include "prim-types.hh"
#include "str-util.hh"
#include "tiny-format.hh"
#include "tinyusdz.hh"
#include "usdGeom.hh"
#include "usdShade.hh"
#include "usdLux.hh"
#include "usdMtlx.hh"
#include "value-pprint.hh"
#include "logger.hh"
#include "bone-util.hh"
#include "shape-to-mesh.hh"
#include "materialx-to-json.hh"
#include "mmap-array-ref.hh"
#include "shape-to-mesh.hh"

//
#include "common-macros.inc"
#include "math-util.inc"


//
#include "tydra/attribute-eval.hh"
#include "tydra/render-data.hh"
#include "tydra/render-data-internal.hh"
#include "tydra/scene-access.hh"
#include "tydra/shader-network.hh"

namespace tinyusdz {

namespace tydra {

//
// Convert GeomCube to RenderMesh by generating tessellated geometry
//
bool RenderSceneConverter::ConvertCube(
    const RenderSceneConverterEnv &env, const Path &abs_prim_path,
    const GeomCube &cube, const MaterialPath &material_path,
    const std::map<std::string, MaterialPath> &subset_material_path_map,
    const StringAndIdMap &rmaterial_map,
    const std::vector<const tinyusdz::GeomSubset *> &material_subsets,
    const std::vector<std::pair<std::string, const tinyusdz::BlendShape *>> &blendshapes,
    RenderMesh *dstMesh) {

  // Extract cube size
  double size;
  if (!cube.size.get_value().get_scalar(&size)) {
    size = 2.0;  // Use default value if not available
  }

  // Generate cube mesh geometry
  std::vector<value::float3> points_f3;
  std::vector<int> faceVertexCounts;
  std::vector<int> faceVertexIndices;
  std::vector<value::float3> normals_f3;
  std::vector<value::float2> uvs_f2;

  GenerateCubeMesh(size, points_f3, faceVertexCounts, faceVertexIndices, normals_f3, uvs_f2);

  // Create temporary GeomMesh with generated data
  GeomMesh temp_mesh;

  // Convert points from float3 to point3f
  std::vector<value::point3f> points;
  for (const auto &p : points_f3) {
    points.push_back(value::point3f{p[0], p[1], p[2]});
  }
  temp_mesh.points.set_value(points);
  temp_mesh.faceVertexCounts.set_value(faceVertexCounts);
  temp_mesh.faceVertexIndices.set_value(faceVertexIndices);

  // Copy properties from cube
  temp_mesh.orientation = cube.orientation;
  temp_mesh.doubleSided = cube.doubleSided;

  // Set normals as face-varying primvar
  {
    std::vector<value::normal3f> normal3f_data;
    for (const auto &n : normals_f3) {
      normal3f_data.push_back(value::normal3f{n[0], n[1], n[2]});
    }
    temp_mesh.normals.set_value(normal3f_data);
    temp_mesh.normals.metas().set_interpolation_enum(Interpolation::FaceVarying);
  }

  // Set UVs as st primvar (face-varying)
  {
    GeomPrimvar primvar;
    primvar.set_name("st");
    primvar.set_interpolation(Interpolation::FaceVarying);
    std::vector<value::texcoord2f> uv_data;
    for (const auto &uv : uvs_f2) {
      uv_data.push_back(value::texcoord2f{uv[0], uv[1]});
    }
    primvar.set_value(uv_data);
    temp_mesh.set_primvar(primvar);
  }

  // Forward to ConvertMesh
  return ConvertMesh(env, abs_prim_path, temp_mesh, material_path,
                     subset_material_path_map, rmaterial_map,
                     material_subsets, blendshapes, dstMesh);
}

//
// Convert GeomSphere to RenderMesh by generating tessellated geometry
//
bool RenderSceneConverter::ConvertSphere(
    const RenderSceneConverterEnv &env, const Path &abs_prim_path,
    const GeomSphere &sphere, const MaterialPath &material_path,
    const std::map<std::string, MaterialPath> &subset_material_path_map,
    const StringAndIdMap &rmaterial_map,
    const std::vector<const tinyusdz::GeomSubset *> &material_subsets,
    const std::vector<std::pair<std::string, const tinyusdz::BlendShape *>> &blendshapes,
    RenderMesh *dstMesh) {

  // Extract sphere radius
  double radius;
  if (!sphere.radius.get_value().get_scalar(&radius)) {
    radius = 2.0;  // Use default value if not available
  }

  // Generate sphere mesh geometry
  // Default to icosphere with 2 subdivisions (4 divisions as per user request seems to mean subdivisions)
  std::vector<value::float3> points_f3;
  std::vector<int> faceVertexCounts;
  std::vector<int> faceVertexIndices;
  std::vector<value::float3> normals_f3;
  std::vector<value::float2> uvs_f2;

  int subdivisions = env.mesh_config.sphere_subdivisions;
  if (env.mesh_config.sphere_tessellation == SphereTessellation::UV) {
    GenerateUVSphereMesh(radius, subdivisions, points_f3, faceVertexCounts, faceVertexIndices, normals_f3, uvs_f2);
  } else {
    GenerateIcosphereMesh(radius, subdivisions, points_f3, faceVertexCounts, faceVertexIndices, normals_f3, uvs_f2);
  }

  // Create temporary GeomMesh with generated data
  GeomMesh temp_mesh;

  // Convert points from float3 to point3f
  std::vector<value::point3f> points;
  for (const auto &p : points_f3) {
    points.push_back(value::point3f{p[0], p[1], p[2]});
  }
  temp_mesh.points.set_value(points);
  temp_mesh.faceVertexCounts.set_value(faceVertexCounts);
  temp_mesh.faceVertexIndices.set_value(faceVertexIndices);

  // Copy properties from sphere
  temp_mesh.orientation = sphere.orientation;
  temp_mesh.doubleSided = sphere.doubleSided;

  // Set normals as face-varying primvar
  {
    std::vector<value::normal3f> normal3f_data;
    for (const auto &n : normals_f3) {
      normal3f_data.push_back(value::normal3f{n[0], n[1], n[2]});
    }
    temp_mesh.normals.set_value(normal3f_data);
    temp_mesh.normals.metas().set_interpolation_enum(Interpolation::FaceVarying);
  }

  // Set UVs as st primvar (face-varying)
  {
    GeomPrimvar primvar;
    primvar.set_name("st");
    primvar.set_interpolation(Interpolation::FaceVarying);
    std::vector<value::texcoord2f> uv_data;
    for (const auto &uv : uvs_f2) {
      uv_data.push_back(value::texcoord2f{uv[0], uv[1]});
    }
    primvar.set_value(uv_data);
    temp_mesh.set_primvar(primvar);
  }

  // Forward to ConvertMesh
  return ConvertMesh(env, abs_prim_path, temp_mesh, material_path,
                     subset_material_path_map, rmaterial_map,
                     material_subsets, blendshapes, dstMesh);
}

// Helper to get NodeCategory from NodeType
static NodeCategory GetNodeCategoryFromType(NodeType nodeType) {
  switch (nodeType) {
    case NodeType::Xform:
      return NodeCategory::Group;
    case NodeType::Mesh:
      return NodeCategory::Geom;
    case NodeType::Camera:
      return NodeCategory::Camera;
    case NodeType::SkelRoot:
    case NodeType::Skeleton:
      return NodeCategory::Skeleton;
    case NodeType::PointLight:
    case NodeType::DirectionalLight:
    case NodeType::EnvmapLight:
    case NodeType::RectLight:
    case NodeType::DiskLight:
    case NodeType::CylinderLight:
    case NodeType::GeometryLight:
      return NodeCategory::Light;
  }
  return NodeCategory::Group;  // Default
}

bool RenderSceneConverter::BuildNodeHierarchyImpl(
    const RenderSceneConverterEnv &env, const std::string &parentPrimPath,
    const XformNode &node, Node &out_rnode) {
  Node rnode;

  std::string primPath;
  if (parentPrimPath.empty()) {
    primPath = "/" + node.element_name;
  } else {
    primPath = parentPrimPath + "/" + node.element_name;
  }

  const tinyusdz::Prim *prim = node.prim;
  if (prim) {
    rnode.prim_name = prim->element_name();
    rnode.abs_path = primPath;
    rnode.display_name = prim->metas().has_displayName() ? prim->metas().get_displayName() : "";

    DCOUT("rnode.prim_name " << rnode.prim_name);
    DCOUT("node.local_mat " << node.get_local_matrix());
    DCOUT("node.has_resetXform " << node.has_resetXformStack());
    DCOUT("prim.type_name " << prim->type_name());
    DCOUT("prim.type_id " << prim->type_id());
    DCOUT("xform " << value::TYPE_ID_GEOM_XFORM);

    if (prim->type_id() == value::TYPE_ID_GEOM_MESH) {
      // GeomMesh(GPrim) also has xform.
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.nodeType = NodeType::Mesh;
      rnode.has_resetXform = node.has_resetXformStack();

      if (auto mesh_it = meshMap.find(primPath); mesh_it != meshMap.s_end()) {
        rnode.id = int32_t(mesh_it->second);
      } else {
        rnode.id = -1;
      }

      // Note: MeshLightAPI is now handled in ConvertMesh, which sets
      // mesh.is_area_light = true and stores light properties directly in RenderMesh
    } else if (prim->type_id() == value::TYPE_ID_GEOM_CAMERA) {
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Camera;

      const GeomCamera *geomCamera = prim->as<GeomCamera>();
      if (geomCamera) {
        RenderCamera rcam;
        rcam.name = prim->element_name();
        rcam.abs_path = primPath;
        rcam.display_name = prim->metas().has_displayName() ? prim->metas().get_displayName() : "";

        // Extract lens properties
        float val_f;
        if (geomCamera->focalLength.get_value().get_scalar(&val_f)) {
          rcam.focalLength = val_f;
        }
        if (geomCamera->verticalAperture.get_value().get_scalar(&val_f)) {
          rcam.verticalAperture = val_f;
        }
        if (geomCamera->horizontalAperture.get_value().get_scalar(&val_f)) {
          rcam.horizontalAperture = val_f;
        }

        value::float2 range_val;
        if (geomCamera->clippingRange.get_value().get_scalar(&range_val)) {
          rcam.znear = range_val[0];
          rcam.zfar = range_val[1];
        }

        GeomCamera::Projection proj_val;
        if (geomCamera->projection.get_value().get_scalar(&proj_val)) {
          rcam.projection = proj_val;
        }

        size_t cam_id = cameras.size();
        cameraMap.add(primPath, cam_id);
        cameras.push_back(std::move(rcam));
        rnode.id = int32_t(cam_id);
      } else {
        rnode.id = -1;
      }
    } else if (prim->type_id() == value::TYPE_ID_GEOM_XFORM) {
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      DCOUT("rnode.local_matrix " << rnode.local_matrix);
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Xform;
    } else if (prim->type_id() == value::TYPE_ID_SCOPE) {
      // NOTE: get_local_matrix() should return identity matrix.
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Xform;
    } else if (prim->type_id() == value::TYPE_ID_MODEL) {
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Xform;
    } else if (prim->type_id() == value::TYPE_ID_GEOM_CUBE || prim->type_id() == value::TYPE_ID_GEOM_SPHERE) {
      // GeomCube and GeomSphere are converted to meshes
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.nodeType = NodeType::Mesh;
      rnode.has_resetXform = node.has_resetXformStack();

      if (auto mesh_it = meshMap.find(primPath); mesh_it != meshMap.s_end()) {
        rnode.id = int32_t(mesh_it->second);
      } else {
        rnode.id = -1;
      }
    } else if ((prim->type_id() > value::TYPE_ID_MODEL_BEGIN) && (prim->type_id() < value::TYPE_ID_GEOM_END)) {
      // Other Geom prims (e.g. GeomCone, GeomCylinder) - not yet converted to meshes
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Xform;
    } else if (IsLightPrim(*prim)) {
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();

      // Convert USD light to RenderLight and add to scene
      RenderLight rlight;
      bool light_converted = false;
      std::string light_abs_path = primPath;
      Path lightPath(light_abs_path, /* prop_part */ "");

      if (prim->type_id() == value::TYPE_ID_LUX_SPHERE) {
        const SphereLight *sphereLight = prim->as<SphereLight>();
        if (sphereLight) {
          if (!ConvertSphereLight(env, lightPath, *sphereLight, &rlight)) {
            return false;
          }
          rnode.nodeType = NodeType::PointLight;
          light_converted = true;
        }
      } else if (prim->type_id() == value::TYPE_ID_LUX_DISTANT) {
        const DistantLight *distantLight = prim->as<DistantLight>();
        if (distantLight) {
          if (!ConvertDistantLight(env, lightPath, *distantLight, &rlight)) {
            return false;
          }
          rnode.nodeType = NodeType::DirectionalLight;
          light_converted = true;
        }
      } else if (prim->type_id() == value::TYPE_ID_LUX_DOME) {
        const DomeLight *domeLight = prim->as<DomeLight>();
        if (domeLight) {
          if (!ConvertDomeLight(env, lightPath, *domeLight, &rlight)) {
            return false;
          }
          rnode.nodeType = NodeType::EnvmapLight;
          light_converted = true;
        }
      } else if (prim->type_id() == value::TYPE_ID_LUX_RECT) {
        const RectLight *rectLight = prim->as<RectLight>();
        if (rectLight) {
          if (!ConvertRectLight(env, lightPath, *rectLight, &rlight)) {
            return false;
          }
          rnode.nodeType = NodeType::RectLight;
          light_converted = true;
        }
      } else if (prim->type_id() == value::TYPE_ID_LUX_DISK) {
        const DiskLight *diskLight = prim->as<DiskLight>();
        if (diskLight) {
          if (!ConvertDiskLight(env, lightPath, *diskLight, &rlight)) {
            return false;
          }
          rnode.nodeType = NodeType::DiskLight;
          light_converted = true;
        }
      } else if (prim->type_id() == value::TYPE_ID_LUX_CYLINDER) {
        const CylinderLight *cylinderLight = prim->as<CylinderLight>();
        if (cylinderLight) {
          if (!ConvertCylinderLight(env, lightPath, *cylinderLight, &rlight)) {
            return false;
          }
          rnode.nodeType = NodeType::CylinderLight;
          light_converted = true;
        }
      } else if (prim->type_id() == value::TYPE_ID_LUX_GEOMETRY) {
        const GeometryLight *geometryLight = prim->as<GeometryLight>();
        if (geometryLight) {
          if (!ConvertGeometryLight(env, lightPath, *geometryLight, &rlight)) {
            return false;
          }
          rnode.nodeType = NodeType::GeometryLight;
          light_converted = true;
        }
      } else {
        // Unsupported light type
        DCOUT("Unsupported light type: " << prim->type_name());
        rnode.nodeType = NodeType::Xform;
      }

      if (light_converted) {
        // Copy world transform to the light
        // rnode.global_matrix is a matrix4d, rlight.transform is mat4 (float)
        const auto &m = rnode.global_matrix;
        rlight.transform.m[0][0] = float(m.m[0][0]);
        rlight.transform.m[0][1] = float(m.m[0][1]);
        rlight.transform.m[0][2] = float(m.m[0][2]);
        rlight.transform.m[0][3] = float(m.m[0][3]);
        rlight.transform.m[1][0] = float(m.m[1][0]);
        rlight.transform.m[1][1] = float(m.m[1][1]);
        rlight.transform.m[1][2] = float(m.m[1][2]);
        rlight.transform.m[1][3] = float(m.m[1][3]);
        rlight.transform.m[2][0] = float(m.m[2][0]);
        rlight.transform.m[2][1] = float(m.m[2][1]);
        rlight.transform.m[2][2] = float(m.m[2][2]);
        rlight.transform.m[2][3] = float(m.m[2][3]);
        rlight.transform.m[3][0] = float(m.m[3][0]);
        rlight.transform.m[3][1] = float(m.m[3][1]);
        rlight.transform.m[3][2] = float(m.m[3][2]);
        rlight.transform.m[3][3] = float(m.m[3][3]);

        // Extract position from transform (translation column)
        rlight.position[0] = float(m.m[3][0]);
        rlight.position[1] = float(m.m[3][1]);
        rlight.position[2] = float(m.m[3][2]);

        // Extract direction from transform (light faces -Z in local space)
        // Direction is the negative of the Z column (third column) of the rotation part
        rlight.direction[0] = -float(m.m[2][0]);
        rlight.direction[1] = -float(m.m[2][1]);
        rlight.direction[2] = -float(m.m[2][2]);

        // Add light to the lights array
        size_t light_id = lights.size();
        lightMap.add(light_abs_path, light_id);
        lights.push_back(std::move(rlight));
        rnode.id = int32_t(light_id);
      } else {
        rnode.id = -1;
      }
    } else if (prim->type_id() == value::TYPE_ID_SKEL_ROOT) {
      // UsdSkelRoot: encapsulation prim for skinned subtree.
      // SkelRoot is Xformable and its world transform (skelLocalToWorld)
      // positions the skinned result in world space.
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::SkelRoot;
    } else if (prim->type_id() == value::TYPE_ID_SKELETON) {
      // UsdSkeleton: joint hierarchy with bindTransforms and restTransforms.
      // Skeleton is Xformable; its world transform contributes to
      // skelLocalToWorld for positioning skinned results.
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Skeleton;
    } else {
      // ignore other node types.
      DCOUT("Unknown/Unsupported prim. " << prim->type_name());

      // Setup as xform for now.
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Xform;
    }

    // Set category based on nodeType
    rnode.category = GetNodeCategoryFromType(rnode.nodeType);
  }

  for (const auto &child : node.children) {
    Node child_rnode;
    if (!BuildNodeHierarchyImpl(env, primPath, child, child_rnode)) {
      return false;
    }

    rnode.children.emplace_back(std::move(child_rnode));
  }

  out_rnode = std::move(rnode);

  return true;
}

//

bool RenderSceneConverter::BuildNodeHierarchy(
    const RenderSceneConverterEnv &env, const XformNode &root) {
  std::string defaultRootNode = env.stage.metas().defaultPrim.str();

  default_node = -1;

  for (const auto &rootNode : root.children) {
    Node root_node;
    if (!BuildNodeHierarchyImpl(env, /* root */ "", rootNode, root_node)) {
      return false;
    }

    if (defaultRootNode == rootNode.element_name) {
      default_node = int(root_nodes.size());
    }

    root_nodeMap.add("/" + rootNode.element_name, root_nodes.size());
    root_nodes.push_back(root_node);
  }

  return true;
}

bool RenderSceneConverter::GetBoundMaterialCached(
    const Stage &stage, const Path &abs_path,
    const std::string &purpose, Path *materialPath,
    const Material **material, std::string *err) {
  // Build cache key: "prim_path\0purpose"
  std::string key = abs_path.full_path_name();
  key.push_back('\0');
  key += purpose;

  auto it = _materialBindingCache.find(key);
  if (it != _materialBindingCache.end()) {
    if (!it->second.error.empty()) {
      if (err) {
        (*err) += it->second.error;
      }
      return false;
    }

    if (it->second.found) {
      *materialPath = it->second.materialPath;
      *material = it->second.material;
    }
    return it->second.found;
  }

  std::string local_err;
  bool found = GetBoundMaterial(stage, abs_path, purpose,
                                materialPath, material, &local_err);

  MaterialBindingCacheEntry entry;
  entry.found = found;
  if (found) {
    entry.materialPath = *materialPath;
    entry.material = *material;
  }
  entry.error = local_err;
  _materialBindingCache[key] = entry;

  if (!local_err.empty() && err) {
    (*err) += local_err;
  }

  return found;
}

bool RenderSceneConverter::ConvertToRenderScene(
    const RenderSceneConverterEnv &env, RenderScene *scene) {
  if (!scene) {
    PUSH_ERROR_AND_RETURN("nullptr for RenderScene argument.");
  }

  // Reset progress state
  _progress_info = DetailedProgressInfo{};

  // Clear lookup caches from previous conversion
  _skelPathToIndex.clear();
  _animPathToIndex.clear();
  _skelNameToIndexCache.clear();
  _skelRootToSkeleton.clear();
  _uvNameCache.clear();
  _materialBindingCache.clear();
  ResetConnectionResolveCache(env.stage);

  // Report initial progress
  if (!CallProgressCallback(0.0f)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  // Count meshes and materials before conversion for accurate progress reporting
  // Single-pass traversal: walk the stage tree once and classify prims by type_id
  DCOUT("[Tydra] Counting primitives...");
  PathPrimMap<GeomMesh> meshPrimMap;
  PathPrimMap<GeomCube> cubePrimMap;
  PathPrimMap<GeomSphere> spherePrimMap;
  PathPrimMap<Material> materialPrimMap;
  PathPrimMap<Skeleton> allSkeletons;
  PathPrimMap<SkelRoot> allSkelRoots;
  PathPrimMap<SkelAnimation> allAnimations;

  {
    // Iterative stack-based traversal visiting each prim exactly once
    struct StackEntry {
      const Prim *parent;
      size_t child_idx;
      size_t parent_path_len;
    };
    std::vector<StackEntry> stack;
    stack.reserve(64);
    std::string path_buf;
    path_buf.reserve(256);

    auto classifyPrim = [&](const Prim &prim) {
      switch (prim.type_id()) {
        case value::TYPE_ID_GEOM_MESH:
          if (const auto *p = prim.as<GeomMesh>()) meshPrimMap[path_buf] = p;
          break;
        case value::TYPE_ID_GEOM_CUBE:
          if (const auto *p = prim.as<GeomCube>()) cubePrimMap[path_buf] = p;
          break;
        case value::TYPE_ID_GEOM_SPHERE:
          if (const auto *p = prim.as<GeomSphere>()) spherePrimMap[path_buf] = p;
          break;
        case value::TYPE_ID_MATERIAL:
          if (const auto *p = prim.as<Material>()) materialPrimMap[path_buf] = p;
          break;
        case value::TYPE_ID_SKELETON:
          if (const auto *p = prim.as<Skeleton>()) allSkeletons[path_buf] = p;
          break;
        case value::TYPE_ID_SKEL_ROOT:
          if (const auto *p = prim.as<SkelRoot>()) allSkelRoots[path_buf] = p;
          break;
        case value::TYPE_ID_SKELANIMATION:
          if (const auto *p = prim.as<SkelAnimation>()) allAnimations[path_buf] = p;
          break;
        default:
          break;
      }
    };

    for (const auto &root_prim : env.stage.root_prims()) {
      path_buf = "/" + root_prim.local_path().full_path_name();
      classifyPrim(root_prim);

      if (!root_prim.children().empty()) {
        stack.push_back({&root_prim, 0, 0});
      }

      size_t iter = 0;
      while (!stack.empty()) {
        if (iter++ >= kMaxDefaultTraversalLimit) {
          PUSH_WARN("Prim traversal exceeded max iteration limit during pre-processing.");
          break;
        }
        auto &top = stack.back();
        if (top.child_idx >= top.parent->children().size()) {
          path_buf.resize(top.parent_path_len);
          stack.pop_back();
          continue;
        }

        const Prim &child = top.parent->children()[top.child_idx];
        ++top.child_idx;

        size_t cur_len = path_buf.size();
        path_buf += "/";
        path_buf += child.local_path().full_path_name();

        classifyPrim(child);

        if (!child.children().empty()) {
          stack.push_back({&child, 0, cur_len});
        } else {
          path_buf.resize(cur_len);
        }
      }
    }
  }
  DCOUT("[Tydra] Pre-discovered " << allSkeletons.size() << " skeletons, "
        << allSkelRoots.size() << " skelroots, " << allAnimations.size() << " animations");

  SkelRootSkeletonResolver::BuildMap(allSkeletons, allSkelRoots,
                                     &_skelRootToSkeleton);
  DCOUT("Precomputed SkelRoot->Skeleton entries: " << _skelRootToSkeleton.size());

  // Total meshes includes GeomMesh, GeomCube, and GeomSphere (all converted to meshes)
  const size_t total_meshes = meshPrimMap.size() + cubePrimMap.size() + spherePrimMap.size();
  const size_t total_materials = materialPrimMap.size();
  DCOUT("[Tydra] Found " << total_meshes << " meshes ("
        << meshPrimMap.size() << " mesh, " << cubePrimMap.size() << " cube, "
        << spherePrimMap.size() << " sphere), " << total_materials << " materials");

  // Report counting complete via detailed progress
  _progress_info.stage = DetailedProgressInfo::Stage::CountingPrims;
  _progress_info.meshes_total = total_meshes;
  _progress_info.materials_total = total_materials;
  _progress_info.message = "Counted " + std::to_string(total_meshes) + " meshes, " +
                           std::to_string(total_materials) + " materials";
  if (!CallDetailedProgressCallback(_progress_info)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  // 1. Convert Xform
  // 2. Convert Material/Texture
  // 3. Convert Mesh/SkinWeights/BlendShapes
  // 4. Convert Skeleton(bones)
  // 5. Build node hierarchy (includes lights and cameras)

  //
  // 1. Build Xform at specified time.
  //    Each Prim in Stage is converted to XformNode.
  //
  _progress_info.stage = DetailedProgressInfo::Stage::ConvertingXforms;
  _progress_info.progress = 0.1f;
  _progress_info.message = "Building xform hierarchy";
  if (!CallDetailedProgressCallback(_progress_info)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  XformNode xform_node;
  if (!BuildXformNodeFromStage(env.stage, &xform_node, env.timecode)) {
    PUSH_ERROR_AND_RETURN("Failed to build Xform node hierarchy.\n");
  }

  // Report progress after xform building (20%)
  if (!CallProgressCallback(0.2f)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  std::string err;

  //
  // 2. Convert Material/Texture
  // 3. Convert Mesh/SkinWeights/BlendShapes
  // 4. Convert Skeleton(bones) and SkelAnimation
  //
  // Material conversion will be done in MeshVisitor.
  //
  _progress_info.stage = DetailedProgressInfo::Stage::ConvertingMeshes;
  _progress_info.progress = 0.2f;
  _progress_info.message = "Converting meshes and materials";
  if (!CallDetailedProgressCallback(_progress_info)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  MeshVisitorEnv menv;
  menv.env = &env;
  menv.converter = this;
  menv.meshes_total = total_meshes;
  menv.materials_total = total_materials;
  menv.allSkeletons = &allSkeletons;
  menv.allSkelRoots = &allSkelRoots;
  menv.allAnimations = &allAnimations;

  // Store pre-discovered maps in converter for use by ConvertMesh
  _allSkeletons = &allSkeletons;
  _allSkelRoots = &allSkelRoots;
  _allAnimations = &allAnimations;

  bool ret = tydra::VisitPrims(env.stage, MeshVisitor, &menv, &err);

  if (!ret) {
    PUSH_ERROR_AND_RETURN(err);
  }

  // Add standalone skeletons (not referenced by any mesh) to the render scene.
  // This ensures skeletons with SkelAnimations but no bound meshes are still
  // available for visualization (e.g. bone hierarchy display).
  for (const auto &skelEntry : allSkeletons) {
    const std::string &skelPathStr = skelEntry.first;
    if (_skelPathToIndex.find(skelPathStr) != _skelPathToIndex.end()) {
      continue;  // Already added by a mesh binding
    }
    const Skeleton *skelPtr = skelEntry.second;
    if (!skelPtr) continue;

    int32_t skel_id = int32_t(skeletons.size());
    SkelHierarchy skel;

    std::string primName = skelPathStr;
    size_t lastSlash = primName.rfind('/');
    if (lastSlash != std::string::npos) {
      primName = primName.substr(lastSlash + 1);
    }
    if (!ConvertSkeletonFromPtr(env, Path(skelPathStr, ""), *skelPtr, primName, &skel)) {
      PushError(fmt::format("Failed to convert standalone skeleton: {}\n",
                            skelPathStr));
      return false;
    }

    _skelPathToIndex[skelPathStr] = skel_id;
    skeletons.emplace_back(std::move(skel));
    DCOUT("Added standalone skeleton: " << skelPathStr);
  }

  // Convert all SkelAnimation prims now that all skeletons have been discovered.
  // This supports multiple animations per skeleton (when animationSource is a pathvector).
  DCOUT("Converting all SkelAnimation prims...");
  if (!ConvertAllSkelAnimations(env)) {
    PUSH_ERROR_AND_RETURN("Failed to convert SkelAnimation prims");
  }
  DCOUT("SkelAnimation conversion complete");

  // Clear temporary pointers
  _allSkeletons = nullptr;
  _allSkelRoots = nullptr;
  _allAnimations = nullptr;
  _skelRootToSkeleton.clear();
  _materialBindingCache.clear();

  // Report progress after mesh/material conversion (70%)
  _progress_info.stage = DetailedProgressInfo::Stage::BuildingHierarchy;
  _progress_info.progress = 0.7f;
  _progress_info.meshes_processed = menv.meshes_processed;
  _progress_info.message = "Mesh conversion complete (" +
      std::to_string(menv.meshes_processed) + " meshes)";
  if (!CallDetailedProgressCallback(_progress_info)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  if (!CallProgressCallback(0.7f)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  //
  // 5. Build node hierarchy from XformNode and meshes, materials, skeletons,
  // etc.
  //
  _progress_info.message = "Building node hierarchy";
  if (!CallDetailedProgressCallback(_progress_info)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  if (!BuildNodeHierarchy(env, xform_node)) {
    return false;
  }

  // Report progress after node hierarchy building (85%)
  _progress_info.stage = DetailedProgressInfo::Stage::ExtractingAnimations;
  _progress_info.progress = 0.85f;
  _progress_info.message = "Hierarchy complete, extracting animations";
  if (!CallDetailedProgressCallback(_progress_info)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  if (!CallProgressCallback(0.85f)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  //
  // 6. Extract xformOp animations from nodes with time-sampled transforms
  //
  {
    // Single-pass depth-first traversal with stable node indices.
    // This avoids repeatedly counting subtree sizes.
    std::function<void(const XformNode&, int32_t&, int32_t)> extractAnimationsFromNode;
    extractAnimationsFromNode = [&](const XformNode& node, int32_t& next_node_index, int32_t depth) {
      if (size_t(depth) >= kMaxDefaultTraversalLimit) return;
      const int32_t node_index = next_node_index++;

      // Check if this node has a prim with xformOps
      if (node.prim && IsXformablePrim(*node.prim)) {
        const Xformable *xformable = nullptr;
        if (CastToXformable(*node.prim, &xformable) && xformable) {
          // Check if xformable has time-sampled transforms
          if (xformable->has_timesamples()) {
            AnimationClip anim;
            // node.absolute_path is already a Path object
            const Path &prim_path = node.absolute_path;

            // Extract xformOp animation
            if (ExtractXformOpAnimation(env, prim_path, node.element_name,
                                       *xformable, node_index, &anim)) {
              // Check if animation with this path already exists via O(1) lookup
              const auto &anim_abs_path = anim.abs_path;
              if (_animPathToIndex.find(anim_abs_path) == _animPathToIndex.end()) {
                DCOUT("Extracted xformOp animation from: " << anim_abs_path);
                _animPathToIndex[anim_abs_path] = int32_t(animations.size());
                animations.emplace_back(std::move(anim));
              }
            }
          }
        }
      }

      for (const auto& child : node.children) {
        extractAnimationsFromNode(child, next_node_index, depth + 1);
      }
    };

    int32_t current_node_index = 0;
    for (const auto& root : xform_node.children) {
      extractAnimationsFromNode(root, current_node_index, 0);
    }
  }

  // Report progress after animation extraction (90%)
  if (!CallProgressCallback(0.9f)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  //
  // 7. Merge meshes with same material (optional optimization)
  //
  if (env.scene_config.merge_meshes) {
    if (!MergeMeshesImpl(env)) {
      PushWarn("Mesh merging encountered issues, but conversion continues.\n");
    }
  }

  // Report progress after mesh merging (95%)
  if (!CallProgressCallback(0.95f)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  // render_scene.meshMap = std::move(meshMap);
  // render_scene.materialMap = std::move(materialMap);
  // render_scene.textureMap = std::move(textureMap);
  // render_scene.imageMap = std::move(imageMap);
  // render_scene.bufferMap = std::move(bufferMap);

  RenderScene render_scene;
  render_scene.usd_filename = env.usd_filename;
  render_scene.default_root_node = 0;
  if (default_node > -1) {
    if (size_t(default_node) >= root_nodes.size()) {
      PushWarn("Invalid default_node id. Use 0 for default_node id.");
    } else {
      render_scene.default_root_node = uint32_t(default_node);
    }
  }

  render_scene.nodes = std::move(root_nodes);
  render_scene.meshes = std::move(meshes);
  render_scene.textures = std::move(textures);
  render_scene.images = std::move(images);
  render_scene.buffers = std::move(buffers);
  render_scene.materials = std::move(materials);
  render_scene.cameras = std::move(cameras);
  render_scene.lights = std::move(lights);
  render_scene.skeletons = std::move(skeletons);
  render_scene.animations = std::move(animations);

  // Populate scene metadata from Stage
  {
    const auto &stage_metas = env.stage.metas();

    // upAxis
    if (stage_metas.upAxis.authored()) {
      render_scene.meta.upAxis = to_string(stage_metas.upAxis.get_value());
    }

    // metersPerUnit
    if (stage_metas.metersPerUnit.authored()) {
      render_scene.meta.metersPerUnit = stage_metas.metersPerUnit.get_value();
    }

    // framesPerSecond
    if (stage_metas.framesPerSecond.authored()) {
      render_scene.meta.framesPerSecond = stage_metas.framesPerSecond.get_value();
    }

    // timeCodesPerSecond
    if (stage_metas.timeCodesPerSecond.authored()) {
      render_scene.meta.timeCodesPerSecond = stage_metas.timeCodesPerSecond.get_value();
    }

    // startTimeCode
    if (stage_metas.startTimeCode.authored()) {
      render_scene.meta.startTimeCode = stage_metas.startTimeCode.get_value();
    }

    // endTimeCode
    if (stage_metas.endTimeCode.authored()) {
      render_scene.meta.endTimeCode = stage_metas.endTimeCode.get_value();
    }

    // autoPlay
    if (stage_metas.autoPlay.authored()) {
      render_scene.meta.autoPlay = stage_metas.autoPlay.get_value();
    }

    // comment
    if (!stage_metas.comment.value.empty()) {
      render_scene.meta.comment = stage_metas.comment.value;
    }

    // copyright - Check if customLayerData contains copyright info
    auto it = stage_metas.customLayerData.find("copyright");
    if (it != stage_metas.customLayerData.end()) {
      // Try to extract string value from MetaVariable
      auto copyright_val = it->second.get_value<std::string>();
      if (copyright_val) {
        render_scene.meta.copyright = copyright_val.value();
      }
    }
  }

  (*scene) = std::move(render_scene);

  // Report completion (100%)
  _progress_info.stage = DetailedProgressInfo::Stage::Complete;
  _progress_info.progress = 1.0f;
  _progress_info.message = "Conversion complete";
  if (!CallDetailedProgressCallback(_progress_info)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }
  CallProgressCallback(1.0f);

  DCOUT("[Tydra] Conversion complete: " << scene->meshes.size() << " meshes, "
        << scene->materials.size() << " materials, " << scene->textures.size() << " textures");

  return true;
}

bool DefaultTextureImageLoaderFunction(
    const value::AssetPath &assetPath, const AssetInfo &assetInfo,
    const AssetResolutionResolver &assetResolver, TextureImage *texImageOut,
    std::vector<uint8_t> *imageData, void *userdata, std::string *warn,
    std::string *err) {
  if (!texImageOut) {
    if (err) {
      (*err) = "`imageOut` argument is nullptr\n";
    }
    return false;
  }

  if (!imageData) {
    if (err) {
      (*err) = "`imageData` argument is nullptr\n";
    }
    return false;
  }

  // TODO: assetInfo
  (void)assetInfo;
  (void)userdata;
  (void)warn;

  std::string resolvedPath = assetResolver.resolve(assetPath.GetAssetPath());

  if (resolvedPath.empty()) {
    if (err) {
      (*err) += fmt::format("Failed to resolve asset path: {}\n",
                            assetPath.GetAssetPath());
    }
    return false;
  }

  Asset asset;
  bool ret = assetResolver.open_asset(resolvedPath, assetPath.GetAssetPath(),
                                      &asset, warn, err);
  if (!ret) {
    if (err) {
      (*err) += fmt::format("Failed to open asset: {}", resolvedPath);
    }
    return false;
  }

  DCOUT("Resolved asset path = " << resolvedPath);

  // TODO: user-defined image loader handler.
  auto result = tinyusdz::image::LoadImageFromMemory(asset.data(), asset.size(),
                                                     resolvedPath);
  if (!result) {
    if (err) {
      (*err) += "Failed to load image file: " + result.error() + "\n";
    }
    return false;
  }

  TextureImage texImage;

  texImage.asset_identifier = resolvedPath;
  texImage.channels = result.value().image.channels;

  const auto &imgret = result.value();

  if (imgret.image.bpp == 8) {
    // assume uint8
    texImage.assetTexelComponentType = ComponentType::UInt8;
  } else if (imgret.image.bpp == 16) {
    if (imgret.image.format == Image::PixelFormat::UInt) {
      texImage.assetTexelComponentType = ComponentType::UInt16;
    } else if (imgret.image.format == Image::PixelFormat::Int) {
      texImage.assetTexelComponentType = ComponentType::Int16;
    } else if (imgret.image.format == Image::PixelFormat::Float) {
      texImage.assetTexelComponentType = ComponentType::Half;
    } else {
      if (err) {
        (*err) += "Invalid image.pixelformat: " + tinyusdz::to_string(imgret.image.format) + "\n";
      }
      return false;
    }

  } else if (imgret.image.bpp == 32) {
    if (imgret.image.format == Image::PixelFormat::UInt) {
      texImage.assetTexelComponentType = ComponentType::UInt32;
    } else if (imgret.image.format == Image::PixelFormat::Int) {
      texImage.assetTexelComponentType = ComponentType::Int32;
    } else if (imgret.image.format == Image::PixelFormat::Float) {
      texImage.assetTexelComponentType = ComponentType::Float;
    } else {
      if (err) {
        (*err) += "Invalid image.pixelformat: " + tinyusdz::to_string(imgret.image.format) + "\n";
      }
      return false;
    }
  } else {
    DCOUT("TODO: bpp = " << result.value().image.bpp);
    if (err) {
      (*err) += "TODO or unsupported bpp: " +
               std::to_string(result.value().image.bpp) + "\n";
    }
    return false;
  }

  texImage.channels = result.value().image.channels;
  texImage.width = result.value().image.width;
  texImage.height = result.value().image.height;

  (*texImageOut) = texImage;

  // raw image data
  (*imageData) = result.value().image.data;

  return true;
}

bool InferColorSpace(const value::token &tok, ColorSpace *cty) {
  if (!cty) {
    return false;
  }

  if (tok.str() == "raw") {
    (*cty) = ColorSpace::Raw;
  } else if (tok.str() == "Raw") {
    (*cty) = ColorSpace::Raw;
  } else if (tok.str() == "srgb") {
    (*cty) = ColorSpace::sRGB;
  } else if (tok.str() == "sRGB") {
    (*cty) = ColorSpace::sRGB;
  } else if (tok.str() == "srgb_texture") {  // MaterialX texture colorspace
    (*cty) = ColorSpace::sRGB_Texture;
  } else if (tok.str() == "linear") { // guess linear_srgb
    (*cty) = ColorSpace::Lin_sRGB;
  } else if (tok.str() == "lin_srgb") {
    (*cty) = ColorSpace::Lin_sRGB;
  } else if (tok.str() == "rec709") {
    (*cty) = ColorSpace::Rec709;
  } else if (tok.str() == "lin_rec709") {  // MaterialX linear Rec.709
    (*cty) = ColorSpace::Lin_Rec709;
  } else if (tok.str() == "g22_rec709") {  // MaterialX gamma 2.2 Rec.709
    (*cty) = ColorSpace::g22_Rec709;
  } else if (tok.str() == "g18_rec709") {  // MaterialX gamma 1.8 Rec.709
    (*cty) = ColorSpace::g18_Rec709;
  } else if (tok.str() == "lin_rec2020") {  // Linear Rec.2020
    (*cty) = ColorSpace::Lin_Rec2020;
  } else if (tok.str() == "acescg") {  // Alternative ACES CG naming
    (*cty) = ColorSpace::Lin_ACEScg;
  } else if (tok.str() == "lin_ap1") {  // Linear AP1 (same as ACEScg)
    (*cty) = ColorSpace::Lin_ACEScg;
  } else if (tok.str() == "aces2065-1") {  // ACES 2065-1
    (*cty) = ColorSpace::ACES2065_1;
  } else if (tok.str() == "ocio") {
    (*cty) = ColorSpace::OCIO;
  } else if (tok.str() == "lin_displayp3") {
    (*cty) = ColorSpace::Lin_DisplayP3;
  } else if (tok.str() == "srgb_displayp3") {
    (*cty) = ColorSpace::sRGB_DisplayP3;

    //
    // seen in Apple's USDZ model(or OCIO?)
    //

  } else if (tok.str() == "ACES - ACEScg") {
    (*cty) = ColorSpace::Lin_ACEScg;
  } else if (tok.str() == "Input - Texture - sRGB - Display P3") {
    (*cty) = ColorSpace::sRGB_DisplayP3;
  } else if (tok.str() == "Input - Texture - sRGB - sRGB") {
    (*cty) = ColorSpace::sRGB;
  } else if (tok.str() == "custom") {
    (*cty) = ColorSpace::Custom;
  } else {
    return false;
  }

  return true;
}


// Memory usage estimation implementations

size_t RenderMesh::estimate_memory_usage() const {
  size_t total = sizeof(RenderMesh);

  // String storage
  total += prim_name.capacity();
  total += abs_path.capacity();
  total += display_name.capacity();

  // Vertex data
  total += points.capacity() * sizeof(vec3);

  // Index data
  total += usdFaceVertexIndices.capacity() * sizeof(uint32_t);
  total += usdFaceVertexCounts.capacity() * sizeof(uint32_t);
  total += triangulatedFaceVertexIndices.capacity() * sizeof(uint32_t);
  total += triangulatedFaceVertexCounts.capacity() * sizeof(uint32_t);
  total += triangulatedToOrigFaceVertexIndexMap.capacity() * sizeof(uint32_t);
  total += triangulatedFaceCounts.capacity() * sizeof(uint32_t);

  // Vertex attributes helper
  auto estimate_vertex_attr = [](const VertexAttribute& attr) -> size_t {
    size_t size = sizeof(VertexAttribute);
    size += attr.name.capacity();
    size += attr.data.capacity();
    size += attr.indices.capacity() * sizeof(uint32_t);
    return size;
  };

  total += estimate_vertex_attr(normals);
  total += estimate_vertex_attr(tangents);
  total += estimate_vertex_attr(binormals);
  total += estimate_vertex_attr(vertex_colors);
  total += estimate_vertex_attr(vertex_opacities);

  // Texcoords map
  for (const auto& texcoord_pair : texcoords) {
    total += sizeof(uint32_t) + estimate_vertex_attr(texcoord_pair.second);
  }

  // StringAndIdMap for texcoords
  total += texcoordSlotIdMap.size() * (sizeof(uint64_t) + sizeof(std::string));
  for (auto it = texcoordSlotIdMap.s_begin(); it != texcoordSlotIdMap.s_end(); ++it) {
    total += it->first.capacity();
  }

  // Joint and weights
  total += sizeof(JointAndWeight);
  total += joint_and_weights.jointIndices.capacity() * sizeof(int);
  total += joint_and_weights.jointWeights.capacity() * sizeof(float);

  // Blend shapes
  for (const auto& blend_shape_pair : targets) {
    total += blend_shape_pair.first.capacity() + sizeof(ShapeTarget);
    const auto& st = blend_shape_pair.second;
    total += st.prim_name.capacity();
    total += st.abs_path.capacity();
    total += st.display_name.capacity();
    total += st.pointIndices.capacity() * sizeof(uint32_t);
    total += st.pointOffsets.capacity() * sizeof(vec3);
    total += st.normalOffsets.capacity() * sizeof(vec3);
    for (const auto& ib_pair : st.inbetweens) {
      total += sizeof(float) + sizeof(InbetweenShapeTarget);
      total += ib_pair.second.pointOffsets.capacity() * sizeof(vec3);
      total += ib_pair.second.normalOffsets.capacity() * sizeof(vec3);
    }
  }

  // Material subset map
  for (const auto& subset_pair : material_subsetMap) {
    total += subset_pair.first.capacity() + sizeof(MaterialSubset);
    const auto& ms = subset_pair.second;
    total += ms.prim_name.capacity();
    total += ms.abs_path.capacity();
    total += ms.display_name.capacity();
    total += ms.usdIndices.capacity() * sizeof(int);
    total += ms.triangulatedIndices.capacity() * sizeof(int);
  }

  return total;
}

// Helper to estimate Node tree memory recursively.
static size_t EstimateNodeMemory(const Node& node) {
  size_t total = sizeof(Node);
  total += node.prim_name.capacity();
  total += node.abs_path.capacity();
  total += node.display_name.capacity();
  total += node.children.capacity() * sizeof(Node);
  for (const auto& child : node.children) {
    total += EstimateNodeMemory(child) - sizeof(Node); // avoid double-counting
  }
  return total;
}

// Helper to estimate SkelNode tree memory recursively.
static size_t EstimateSkelNodeMemory(const SkelNode& node) {
  size_t total = sizeof(SkelNode);
  total += node.joint_path.capacity();
  total += node.joint_name.capacity();
  total += node.children.capacity() * sizeof(SkelNode);
  for (const auto& child : node.children) {
    total += EstimateSkelNodeMemory(child) - sizeof(SkelNode);
  }
  return total;
}

size_t RenderScene::estimate_memory_usage() const {
  size_t total = sizeof(RenderScene);

  // Scene metadata and filename
  total += usd_filename.capacity();
  total += sizeof(SceneMetadata);

  // Nodes (recursive tree)
  total += nodes.capacity() * sizeof(Node);
  for (const auto& node : nodes) {
    total += EstimateNodeMemory(node) - sizeof(Node);
  }

  // Texture images
  total += images.capacity() * sizeof(TextureImage);
  for (const auto& img : images) {
    total += img.asset_identifier.capacity();
  }

  // Materials
  total += materials.capacity() * sizeof(RenderMaterial);
  for (const auto& mat : materials) {
    total += mat.name.capacity();
    total += mat.abs_path.capacity();
    total += mat.display_name.capacity();
    total += mat.displacement_shader_path.capacity();
    total += mat.volume_shader_path.capacity();
    // Spectral data vectors (if present)
    if (mat.surfaceShader.has_value()) {
      const auto& s = *mat.surfaceShader;
      if (s.spd_reflectance.has_value()) {
        total += s.spd_reflectance->samples.capacity() * sizeof(vec2);
      }
      if (s.spd_ior.has_value()) {
        total += s.spd_ior->samples.capacity() * sizeof(vec2);
      }
    }
  }

  total += cameras.capacity() * sizeof(RenderCamera);
  total += lights.capacity() * sizeof(RenderLight);

  total += textures.capacity() * sizeof(UVTexture);
  for (const auto& texture : textures) {
    total += texture.prim_name.capacity();
    total += texture.abs_path.capacity();
    total += texture.display_name.capacity();
  }

  // Meshes - use the detailed estimation
  total += meshes.capacity() * sizeof(RenderMesh);
  for (const auto& mesh : meshes) {
    total += mesh.estimate_memory_usage() - sizeof(RenderMesh);
  }

  // Animations
  total += animations.capacity() * sizeof(AnimationClip);
  for (const auto& clip : animations) {
    total += clip.name.capacity();
    total += clip.prim_name.capacity();
    total += clip.abs_path.capacity();
    total += clip.display_name.capacity();
    total += clip.samplers.capacity() * sizeof(KeyframeSampler);
    for (const auto& sampler : clip.samplers) {
      total += sampler.times.capacity() * sizeof(float);
      total += sampler.values.capacity() * sizeof(float);
    }
    total += clip.channels.capacity() * sizeof(AnimationChannel);
  }

  // Skeletons
  total += skeletons.capacity() * sizeof(SkelHierarchy);
  for (const auto& skel : skeletons) {
    total += skel.prim_name.capacity();
    total += skel.abs_path.capacity();
    total += skel.display_name.capacity();
    total += EstimateSkelNodeMemory(skel.root_node) - sizeof(SkelNode);
    total += skel.anim_ids.capacity() * sizeof(int);
    total += skel.parent_joint_indices.capacity() * sizeof(int);
    total += skel.bind_transforms.capacity() * sizeof(value::matrix4d);
    total += skel.rest_transforms.capacity() * sizeof(value::matrix4d);
  }

  total += buffers.capacity() * sizeof(BufferData);
  for (const auto& buffer : buffers) {
    total += buffer.data.capacity();
  }

  return total;
}

void RenderSceneConverter::SetProgressCallback(ProgressCallback callback, void *userptr) {
  _progress_callback = callback;
  _progress_userptr = userptr;
}

void RenderSceneConverter::SetDetailedProgressCallback(DetailedProgressCallback callback, void *userptr) {
  _detailed_progress_callback = callback;
  _detailed_progress_userptr = userptr;
}

bool RenderSceneConverter::CallProgressCallback(float progress) {
  if (_progress_callback) {
    return _progress_callback(progress, _progress_userptr);
  }
  return true; // Continue if no callback set
}

bool RenderSceneConverter::CallDetailedProgressCallback(const DetailedProgressInfo &info) {
  if (_detailed_progress_callback) {
    return _detailed_progress_callback(info, _detailed_progress_userptr);
  }
  return true; // Continue if no callback set
}

bool RenderSceneConverter::ReportMeshProgress(size_t meshes_processed, size_t meshes_total,
                                               const std::string& mesh_name, const std::string& message) {
  _progress_info.stage = DetailedProgressInfo::Stage::ConvertingMeshes;
  _progress_info.meshes_processed = meshes_processed;
  _progress_info.meshes_total = meshes_total;
  _progress_info.current_mesh_name = mesh_name;
  _progress_info.message = message;

  // Calculate progress: meshes are 20%-70% of total progress (50% range)
  float mesh_progress = 0.2f + (0.5f * float(meshes_processed) / float(std::max(size_t(1), meshes_total)));
  _progress_info.progress = mesh_progress;

  return CallDetailedProgressCallback(_progress_info);
}

bool RenderSceneConverter::IsMeshMergeable(const RenderMesh &mesh) const {
  // Mesh cannot be merged if:
  // 1. Has skeletal animation
  if (mesh.skel_id >= 0) {
    return false;
  }

  // 2. Has blend shapes
  if (!mesh.targets.empty()) {
    return false;
  }

  // 3. Has per-face materials (GeomSubset)
  if (!mesh.material_subsetMap.empty()) {
    return false;
  }

  // 4. Is an area light (special rendering)
  if (mesh.is_area_light) {
    return false;
  }

  return true;
}

// Helper function to transform a vec3 point by a matrix4d
static vec3 TransformPoint(const value::matrix4d &m, const vec3 &p) {
  // Apply full 4x4 transform (position)
  double x = m.m[0][0] * double(p[0]) + m.m[1][0] * double(p[1]) + m.m[2][0] * double(p[2]) + m.m[3][0];
  double y = m.m[0][1] * double(p[0]) + m.m[1][1] * double(p[1]) + m.m[2][1] * double(p[2]) + m.m[3][1];
  double z = m.m[0][2] * double(p[0]) + m.m[1][2] * double(p[1]) + m.m[2][2] * double(p[2]) + m.m[3][2];
  double w = m.m[0][3] * double(p[0]) + m.m[1][3] * double(p[1]) + m.m[2][3] * double(p[2]) + m.m[3][3];

  if (std::abs(w) > 1e-10) {
    x /= w;
    y /= w;
    z /= w;
  }

  return vec3{float(x), float(y), float(z)};
}

// Helper function to transform a vec3 direction (normal) by a matrix4d
// Uses the upper-left 3x3 of the inverse-transpose for correct normal transformation
static vec3 TransformNormal(const value::matrix4d &m, const vec3 &n) {
  // For normals, we need the inverse transpose of the upper-left 3x3
  // For now, we use the upper-left 3x3 directly (correct for uniform scale and rotation only)
  // TODO: Proper inverse-transpose for non-uniform scale
  double x = m.m[0][0] * double(n[0]) + m.m[1][0] * double(n[1]) + m.m[2][0] * double(n[2]);
  double y = m.m[0][1] * double(n[0]) + m.m[1][1] * double(n[1]) + m.m[2][1] * double(n[2]);
  double z = m.m[0][2] * double(n[0]) + m.m[1][2] * double(n[1]) + m.m[2][2] * double(n[2]);

  // Normalize the result
  double len = std::sqrt(x*x + y*y + z*z);
  if (len > 1e-10) {
    x /= len;
    y /= len;
    z /= len;
  }

  return vec3{float(x), float(y), float(z)};
}

bool RenderSceneConverter::MergeMeshData(const RenderMesh &src,
                                         const value::matrix4d &src_transform,
                                         RenderMesh &dst,
                                         std::string *err) {
  auto set_merge_error = [&](const std::string &msg) {
    if (err) {
      *err = msg;
    }
  };

  // Check if transform is identity using tinyusdz::is_identity function
  bool transform_is_identity = tinyusdz::is_identity(src_transform);

  // Get the vertex offset for index adjustment
  uint32_t vertex_offset = static_cast<uint32_t>(dst.points.size());

  // Merge points (with transform if needed)
  if (transform_is_identity) {
    dst.points.insert(dst.points.end(), src.points.begin(), src.points.end());
  } else {
    for (const auto &p : src.points) {
      dst.points.push_back(TransformPoint(src_transform, p));
    }
  }

  // Merge face vertex indices (adjust by vertex offset)
  for (uint32_t idx : src.usdFaceVertexIndices) {
    dst.usdFaceVertexIndices.push_back(idx + vertex_offset);
  }

  // Merge face vertex counts
  dst.usdFaceVertexCounts.insert(dst.usdFaceVertexCounts.end(),
                                  src.usdFaceVertexCounts.begin(),
                                  src.usdFaceVertexCounts.end());

  // Merge triangulated indices if present
  if (!src.triangulatedFaceVertexIndices.empty()) {
    for (uint32_t idx : src.triangulatedFaceVertexIndices) {
      dst.triangulatedFaceVertexIndices.push_back(idx + vertex_offset);
    }
    dst.triangulatedFaceVertexCounts.insert(dst.triangulatedFaceVertexCounts.end(),
                                             src.triangulatedFaceVertexCounts.begin(),
                                             src.triangulatedFaceVertexCounts.end());
  }

  // Merge normals (transform direction if needed)
  if (!src.normals.empty()) {
    size_t src_normal_count = src.normals.vertex_count();

    // Ensure dst normals has same format
    if (dst.normals.empty()) {
      dst.normals = src.normals;
      if (!transform_is_identity) {
        // Transform the normals we just copied
        vec3 *normals_data = reinterpret_cast<vec3*>(dst.normals.data.data());
        for (size_t i = 0; i < src_normal_count; i++) {
          normals_data[i] = TransformNormal(src_transform, normals_data[i]);
        }
      }
    } else {
      if (dst.normals.format != src.normals.format ||
          dst.normals.stride_bytes() != src.normals.stride_bytes()) {
        set_merge_error("Cannot merge normals: incompatible format or stride.");
        return false;
      }
      // Append normals
      size_t old_size = dst.normals.data.size();
      dst.normals.data.resize(old_size + src.normals.data.size());

      if (transform_is_identity) {
        memcpy(dst.normals.data.data() + old_size, src.normals.data.data(), src.normals.data.size());
      } else {
        const vec3 *src_normals = reinterpret_cast<const vec3*>(src.normals.data.data());
        vec3 *dst_normals = reinterpret_cast<vec3*>(dst.normals.data.data() + old_size);
        for (size_t i = 0; i < src_normal_count; i++) {
          dst_normals[i] = TransformNormal(src_transform, src_normals[i]);
        }
      }
    }
  }

  // Merge texcoords (no transform needed)
  for (const auto &src_tc : src.texcoords) {
    uint32_t slot = src_tc.first;
    const auto &src_attr = src_tc.second;

    auto dst_tc_it = dst.texcoords.find(slot);
    if (dst_tc_it == dst.texcoords.end()) {
      dst.texcoords.emplace(slot, src_attr);
    } else {
      auto &dst_attr = dst_tc_it->second;
      if (dst_attr.format != src_attr.format ||
          dst_attr.stride_bytes() != src_attr.stride_bytes()) {
        set_merge_error("Cannot merge texcoords slot " + std::to_string(slot) +
                        ": incompatible format or stride.");
        return false;
      }
      size_t old_size = dst_attr.data.size();
      dst_attr.data.resize(old_size + src_attr.data.size());
      memcpy(dst_attr.data.data() + old_size, src_attr.data.data(), src_attr.data.size());
    }
  }

  // Merge tangents (transform direction if needed)
  if (!src.tangents.empty()) {
    if (dst.tangents.empty()) {
      dst.tangents = src.tangents;
      if (!transform_is_identity) {
        vec3 *tangents_data = reinterpret_cast<vec3*>(dst.tangents.data.data());
        size_t count = dst.tangents.vertex_count();
        for (size_t i = 0; i < count; i++) {
          tangents_data[i] = TransformNormal(src_transform, tangents_data[i]);
        }
      }
    } else {
      if (dst.tangents.format != src.tangents.format ||
          dst.tangents.stride_bytes() != src.tangents.stride_bytes()) {
        set_merge_error(
            "Cannot merge tangents: incompatible format or stride.");
        return false;
      }
      size_t old_size = dst.tangents.data.size();
      size_t src_count = src.tangents.vertex_count();
      dst.tangents.data.resize(old_size + src.tangents.data.size());

      if (transform_is_identity) {
        memcpy(dst.tangents.data.data() + old_size, src.tangents.data.data(), src.tangents.data.size());
      } else {
        const vec3 *src_tangents = reinterpret_cast<const vec3*>(src.tangents.data.data());
        vec3 *dst_tangents = reinterpret_cast<vec3*>(dst.tangents.data.data() + old_size);
        for (size_t i = 0; i < src_count; i++) {
          dst_tangents[i] = TransformNormal(src_transform, src_tangents[i]);
        }
      }
    }
  }

  // Merge binormals (transform direction if needed)
  if (!src.binormals.empty()) {
    if (dst.binormals.empty()) {
      dst.binormals = src.binormals;
      if (!transform_is_identity) {
        vec3 *binormals_data = reinterpret_cast<vec3*>(dst.binormals.data.data());
        size_t count = dst.binormals.vertex_count();
        for (size_t i = 0; i < count; i++) {
          binormals_data[i] = TransformNormal(src_transform, binormals_data[i]);
        }
      }
    } else {
      if (dst.binormals.format != src.binormals.format ||
          dst.binormals.stride_bytes() != src.binormals.stride_bytes()) {
        set_merge_error(
            "Cannot merge binormals: incompatible format or stride.");
        return false;
      }
      size_t old_size = dst.binormals.data.size();
      size_t src_count = src.binormals.vertex_count();
      dst.binormals.data.resize(old_size + src.binormals.data.size());

      if (transform_is_identity) {
        memcpy(dst.binormals.data.data() + old_size, src.binormals.data.data(), src.binormals.data.size());
      } else {
        const vec3 *src_binormals = reinterpret_cast<const vec3*>(src.binormals.data.data());
        vec3 *dst_binormals = reinterpret_cast<vec3*>(dst.binormals.data.data() + old_size);
        for (size_t i = 0; i < src_count; i++) {
          dst_binormals[i] = TransformNormal(src_transform, src_binormals[i]);
        }
      }
    }
  }

  // Merge vertex colors
  if (!src.vertex_colors.empty()) {
    if (dst.vertex_colors.empty()) {
      dst.vertex_colors = src.vertex_colors;
    } else {
      if (dst.vertex_colors.format != src.vertex_colors.format ||
          dst.vertex_colors.stride_bytes() != src.vertex_colors.stride_bytes()) {
        set_merge_error(
            "Cannot merge vertex_colors: incompatible format or stride.");
        return false;
      }
      size_t old_size = dst.vertex_colors.data.size();
      dst.vertex_colors.data.resize(old_size + src.vertex_colors.data.size());
      memcpy(dst.vertex_colors.data.data() + old_size, src.vertex_colors.data.data(), src.vertex_colors.data.size());
    }
  }

  // Merge vertex opacities
  if (!src.vertex_opacities.empty()) {
    if (dst.vertex_opacities.empty()) {
      dst.vertex_opacities = src.vertex_opacities;
    } else {
      if (dst.vertex_opacities.format != src.vertex_opacities.format ||
          dst.vertex_opacities.stride_bytes() != src.vertex_opacities.stride_bytes()) {
        set_merge_error(
            "Cannot merge vertex_opacities: incompatible format or stride.");
        return false;
      }
      size_t old_size = dst.vertex_opacities.data.size();
      dst.vertex_opacities.data.resize(old_size + src.vertex_opacities.data.size());
      memcpy(dst.vertex_opacities.data.data() + old_size, src.vertex_opacities.data.data(), src.vertex_opacities.data.size());
    }
  }

  return true;
}

bool RenderSceneConverter::MergeMeshesImpl(const RenderSceneConverterEnv &env) {
  if (!env.scene_config.merge_meshes) {
    return true;  // Merging disabled, nothing to do
  }

  DCOUT("MergeMeshesImpl: Starting mesh merge...");

  // Build a map from mesh to its node and global transform
  // Structure: mesh_index -> (node_ptr, global_matrix)
  struct MeshNodeInfo {
    Node *node{nullptr};
    value::matrix4d global_matrix;
    size_t mesh_index{0};
  };

  std::vector<MeshNodeInfo> mesh_node_infos;
  mesh_node_infos.resize(meshes.size());
  std::vector<std::vector<Node *>> mesh_nodes_by_id(meshes.size());

  // Helper to traverse nodes and collect mesh info
  std::function<void(Node &, int32_t)> collectMeshNodes = [&](Node &node, int32_t depth) {
    if (size_t(depth) >= kMaxDefaultTraversalLimit) return;
    if (node.nodeType == NodeType::Mesh && node.id >= 0 &&
        size_t(node.id) < meshes.size()) {
      mesh_node_infos[size_t(node.id)].node = &node;
      mesh_node_infos[size_t(node.id)].global_matrix = node.global_matrix;
      mesh_node_infos[size_t(node.id)].mesh_index = size_t(node.id);
      mesh_nodes_by_id[size_t(node.id)].push_back(&node);
    }
    for (auto &child : node.children) {
      collectMeshNodes(child, depth + 1);
    }
  };

  for (auto &root : root_nodes) {
    collectMeshNodes(root, 0);
  }

  // Group meshes by material_id
  // Only include meshes that are mergeable
  std::unordered_map<int, std::vector<size_t>> material_to_meshes;
  material_to_meshes.reserve(meshes.size());

  for (size_t i = 0; i < meshes.size(); i++) {
    const auto &mesh = meshes[i];
    if (!IsMeshMergeable(mesh)) {
      continue;
    }

    // Skip meshes that don't have a node (shouldn't happen but be safe)
    if (!mesh_node_infos[i].node) {
      continue;
    }

    material_to_meshes[mesh.material_id].push_back(i);
  }

  // For each material group with 2+ meshes, merge them
  std::vector<RenderMesh> merged_meshes;
  std::vector<std::pair<int32_t, std::vector<size_t>>> merged_groups;
  [[maybe_unused]] size_t merged_source_mesh_count{0};

  // Keep deterministic output order by processing material IDs in ascending order.
  std::vector<int> sorted_material_ids;
  sorted_material_ids.reserve(material_to_meshes.size());
  for (const auto &kv : material_to_meshes) {
    sorted_material_ids.push_back(kv.first);
  }
  std::sort(sorted_material_ids.begin(), sorted_material_ids.end());

  for (int material_id : sorted_material_ids) {
    auto group_it = material_to_meshes.find(material_id);
    if (group_it == material_to_meshes.end()) {
      continue;
    }
    auto &mesh_indices = group_it->second;

    if (mesh_indices.size() < 2) {
      // Only one mesh with this material, no merging needed
      continue;
    }

    DCOUT("Merging " << mesh_indices.size() << " meshes with material_id=" << material_id);

    // Check if all meshes have the same global transform (when bake_transform is false)
    bool can_merge = true;
    if (!env.scene_config.merge_meshes_bake_transform) {
      const auto &first_matrix = mesh_node_infos[mesh_indices[0]].global_matrix;
      for (size_t i = 1; i < mesh_indices.size(); i++) {
        const auto &matrix = mesh_node_infos[mesh_indices[i]].global_matrix;
        // Compare matrices (with epsilon)
        bool same_transform = true;
        for (int r = 0; r < 4 && same_transform; r++) {
          for (int c = 0; c < 4 && same_transform; c++) {
            if (std::abs(first_matrix.m[r][c] - matrix.m[r][c]) > 1e-6) {
              same_transform = false;
            }
          }
        }
        if (!same_transform) {
          can_merge = false;
          break;
        }
      }
    }

    if (!can_merge) {
      DCOUT("Cannot merge meshes with material_id=" << material_id << " - different transforms");
      continue;
    }

    // Create merged mesh
    RenderMesh merged;
    merged.prim_name = "merged_material_" + std::to_string(material_id);
    merged.abs_path = "/merged/" + merged.prim_name;
    merged.display_name = "Merged mesh (material " + std::to_string(material_id) + ")";
    merged.material_id = material_id;

    // Copy properties from first mesh
    const auto &first_mesh = meshes[mesh_indices[0]];
    merged.doubleSided = first_mesh.doubleSided;
    merged.displayColor = first_mesh.displayColor;
    merged.displayOpacity = first_mesh.displayOpacity;
    merged.is_rightHanded = first_mesh.is_rightHanded;

    // If baking transforms, we transform all vertices to world space
    // The merged mesh will have identity transform

    std::vector<size_t> merged_sources;
    merged_sources.reserve(mesh_indices.size());

    for (size_t idx : mesh_indices) {
      const auto &src_mesh = meshes[idx];
      const auto &node_info = mesh_node_infos[idx];

      value::matrix4d relative_transform;
      if (env.scene_config.merge_meshes_bake_transform) {
        // Use world space transform
        relative_transform = node_info.global_matrix;
      } else {
        // All transforms should be the same (checked above)
        relative_transform = value::matrix4d::identity();
      }

      std::string merge_err;
      if (!MergeMeshData(src_mesh, relative_transform, merged, &merge_err)) {
        PushInfo("Skipping mesh merge for " + src_mesh.abs_path +
                 (merge_err.empty() ? std::string()
                                    : std::string(": ") + merge_err));
        continue;
      }

      merged_sources.push_back(idx);
    }

    if (merged_sources.size() < 2) {
      // Nothing useful to merge for this material group.
      continue;
    }

    merged_source_mesh_count += merged_sources.size();

    // The merged mesh is either in world space (if bake_transform) or
    // shares the transform of the first mesh
    merged.is_single_indexable = first_mesh.is_single_indexable;

    // Add merged mesh
    size_t new_mesh_index = meshes.size() + merged_meshes.size();
    merged_meshes.push_back(std::move(merged));

    merged_groups.emplace_back(static_cast<int32_t>(new_mesh_index),
                               std::move(merged_sources));
  }

  if (merged_meshes.empty()) {
    DCOUT("No meshes were merged");
    return true;
  }

  DCOUT("Created " << merged_meshes.size() << " merged meshes from "
                   << merged_source_mesh_count << " source meshes");

  // Add merged meshes to the mesh array
  for (auto &mm : merged_meshes) {
    meshes.push_back(std::move(mm));
  }

  // Update node references for merged sources only.
  // Keep only one node per merged mesh and invalidate the rest.
  for (const auto &group : merged_groups) {
    int32_t new_id = group.first;
    const auto &source_ids = group.second;
    bool first_assigned = false;

    for (size_t old_id : source_ids) {
      if (old_id >= mesh_nodes_by_id.size()) {
        continue;
      }

      for (Node *node_ptr : mesh_nodes_by_id[old_id]) {
        if (!node_ptr) {
          continue;
        }

        if (!first_assigned) {
          node_ptr->id = new_id;
          first_assigned = true;

          // If we baked transforms, reset the node's transform to identity
          if (env.scene_config.merge_meshes_bake_transform) {
            node_ptr->local_matrix = value::matrix4d::identity();
            node_ptr->global_matrix = value::matrix4d::identity();
          }
        } else {
          node_ptr->id = -1;
        }
      }
    }
  }

  return true;
}

}  // namespace tydra
}  // namespace tinyusdz
