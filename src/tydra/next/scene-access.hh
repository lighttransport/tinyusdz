// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Tydra Next - Scene Access API
//
// Provides traversal and query functions for next::Stage

#pragma once

#include <string>
#include <vector>
#include <functional>

#include "next/stage/stage.hh"
#include "next/types/value.hh"

namespace tinyusdz {
namespace tydra {
namespace next {

using ::tinyusdz::next::Stage;
using ::tinyusdz::next::UsdPrim;
using ::tinyusdz::next::Value;
using ::tinyusdz::next::TypeId;

//
// Prim type checking
//

bool IsMesh(const UsdPrim& prim);
bool IsXform(const UsdPrim& prim);
bool IsCamera(const UsdPrim& prim);
bool IsMaterial(const UsdPrim& prim);
bool IsShader(const UsdPrim& prim);
bool IsLight(const UsdPrim& prim);
bool IsSkeleton(const UsdPrim& prim);
bool IsSkelRoot(const UsdPrim& prim);
bool IsGeomSubset(const UsdPrim& prim);
bool IsScope(const UsdPrim& prim);

// Get specific light type
enum class LightKind {
  Unknown,
  DistantLight,
  DomeLight,
  RectLight,
  DiskLight,
  SphereLight,
  CylinderLight,
  PointLight,  // Non-standard but common
};
LightKind GetLightKind(const UsdPrim& prim);

//
// Find prims by type
//

std::vector<UsdPrim> FindMeshes(const Stage& stage);
std::vector<UsdPrim> FindXforms(const Stage& stage);
std::vector<UsdPrim> FindCameras(const Stage& stage);
std::vector<UsdPrim> FindMaterials(const Stage& stage);
std::vector<UsdPrim> FindLights(const Stage& stage);
std::vector<UsdPrim> FindSkeletons(const Stage& stage);

// Generic find by type name
std::vector<UsdPrim> FindPrimsByType(const Stage& stage, const std::string& type_name);

// Find with predicate
using PrimPredicate = std::function<bool(const UsdPrim&)>;
std::vector<UsdPrim> FindPrims(const Stage& stage, PrimPredicate pred);

//
// Attribute access helpers
//

// Get attribute value with type checking
// Returns nullptr if attribute doesn't exist or wrong type
const Value* GetAttribute(const UsdPrim& prim, const std::string& name);

// Get attribute with time sample support
const Value* GetAttributeAtTime(const UsdPrim& prim, const std::string& name, double time);

// Type-safe attribute getters (return false if not found or wrong type)
bool GetFloat(const UsdPrim& prim, const std::string& name, float* out);
bool GetFloat3(const UsdPrim& prim, const std::string& name, float* x, float* y, float* z);
bool GetDouble(const UsdPrim& prim, const std::string& name, double* out);
bool GetDouble3(const UsdPrim& prim, const std::string& name, double* x, double* y, double* z);
bool GetInt(const UsdPrim& prim, const std::string& name, int* out);
bool GetBool(const UsdPrim& prim, const std::string& name, bool* out);
bool GetString(const UsdPrim& prim, const std::string& name, std::string* out);
bool GetToken(const UsdPrim& prim, const std::string& name, std::string* out);
bool GetMatrix4(const UsdPrim& prim, const std::string& name, float* matrix16);
bool GetMatrix4d(const UsdPrim& prim, const std::string& name, double* matrix16);

// Get array attributes (returns empty vector if not found)
std::vector<float> GetFloatArray(const UsdPrim& prim, const std::string& name);
std::vector<int32_t> GetIntArray(const UsdPrim& prim, const std::string& name);

//
// Relationship access
//

// Get relationship target paths
std::vector<std::string> GetRelationshipTargets(const UsdPrim& prim, const std::string& rel_name);

// Get material binding
std::string GetBoundMaterial(const UsdPrim& prim);

// Get skeleton binding
std::string GetBoundSkeleton(const UsdPrim& prim);

//
// Transform operations
//

// Compute local transform matrix from xformOps
bool ComputeLocalTransform(const UsdPrim& prim, float* matrix16, double time = 0.0);
bool ComputeLocalTransform(const UsdPrim& prim, double* matrix16, double time = 0.0);

// Compute world transform (including all parent transforms)
bool ComputeWorldTransform(const Stage& stage, const UsdPrim& prim, float* matrix16, double time = 0.0);
bool ComputeWorldTransform(const Stage& stage, const UsdPrim& prim, double* matrix16, double time = 0.0);

// Check if prim has resetXformStack
bool HasResetXformStack(const UsdPrim& prim);

//
// Hierarchy access
//

// Get parent prim
UsdPrim GetParent(const Stage& stage, const UsdPrim& prim);

// Get children prims
std::vector<UsdPrim> GetChildren(const UsdPrim& prim);

// Get all descendants
std::vector<UsdPrim> GetDescendants(const UsdPrim& prim);

// Get path components
std::string GetPrimName(const UsdPrim& prim);
std::string GetParentPath(const std::string& path);

//
// GeomSubset access
//

struct GeomSubset {
  std::string name;
  std::string path;
  std::string family_name;
  std::vector<int32_t> indices;
  std::string material_path;
};

std::vector<GeomSubset> GetGeomSubsets(const UsdPrim& mesh_prim);

//
// Primvar access
//

struct Primvar {
  std::string name;
  TypeId type_id;
  std::string interpolation;  // "constant", "uniform", "vertex", "faceVarying"
  const Value* value;
  std::vector<int32_t> indices;  // Optional indices for indexed primvars
};

std::vector<Primvar> GetPrimvars(const UsdPrim& prim);
Primvar GetPrimvar(const UsdPrim& prim, const std::string& name);

//
// Blend shape access
//

struct BlendShapeInfo {
  std::string name;
  std::string path;
  std::vector<float> point_offsets;  // xyz interleaved
  std::vector<float> normal_offsets; // xyz interleaved (optional)
  std::vector<uint32_t> point_indices; // Which points are affected
};

std::vector<BlendShapeInfo> GetBlendShapes(const UsdPrim& mesh_prim);

//
// Skeleton access
//

struct JointInfo {
  std::string name;
  std::string path;
  int32_t parent_index;
  // bind_transform and rest_transform as 16-float arrays
  float bind_transform[16];
  float rest_transform[16];
};

struct SkeletonInfo {
  std::string name;
  std::string path;
  std::vector<JointInfo> joints;
  std::vector<std::string> joint_order;
};

bool GetSkeletonInfo(const UsdPrim& skel_prim, SkeletonInfo* out);

//
// Skin binding access
//

struct SkinBindingInfo {
  std::string skeleton_path;
  std::vector<int32_t> joint_indices;  // Per-influence joint indices
  std::vector<float> joint_weights;    // Per-influence weights
  int32_t influences_per_vertex;       // Typically 4
  float geom_bind_transform[16];
};

bool GetSkinBinding(const UsdPrim& mesh_prim, SkinBindingInfo* out);

//
// Connection following
//

// Follow a connection chain to get the final value
// Returns nullptr if no value found
const Value* ResolveConnection(const Stage& stage, const UsdPrim& prim, const std::string& attr_name);

// Get connected prim path (for input:xxx.connect)
std::string GetConnectionPath(const UsdPrim& prim, const std::string& attr_name);

}  // namespace next
}  // namespace tydra
}  // namespace tinyusdz
