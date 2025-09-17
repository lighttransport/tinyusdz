// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.

#include "type-registry.hh"
#include "usdGeom.hh"
#include "usdLux.hh"
#include "usdShade.hh"
#include "usdSkel.hh"

namespace tinyusdz {

void TypeRegistry::InitializeBuiltinTypes() {
  // Register all built-in USD prim types
  
  // Core types
  RegisterType<Model>("Model", "Model", value::TYPE_ID_MODEL);
  RegisterType<Scope>("Scope", "Scope", value::TYPE_ID_SCOPE);
  RegisterType<Xform>("Xform", "Xform", value::TYPE_ID_GEOM_XFORM);
  
  // Geometry types
  RegisterType<GeomMesh>("GeomMesh", "Mesh", value::TYPE_ID_GEOM_MESH);
  RegisterType<GeomPoints>("GeomPoints", "Points", value::TYPE_ID_GEOM_POINTS);
  RegisterType<GeomCube>("GeomCube", "Cube", value::TYPE_ID_GEOM_CUBE);
  RegisterType<GeomSphere>("GeomSphere", "Sphere", value::TYPE_ID_GEOM_SPHERE);
  RegisterType<GeomCylinder>("GeomCylinder", "Cylinder", value::TYPE_ID_GEOM_CYLINDER);
  RegisterType<GeomCone>("GeomCone", "Cone", value::TYPE_ID_GEOM_CONE);
  RegisterType<GeomCapsule>("GeomCapsule", "Capsule", value::TYPE_ID_GEOM_CAPSULE);
  RegisterType<GeomSubset>("GeomSubset", "GeomSubset", value::TYPE_ID_GEOM_GEOMSUBSET);
  RegisterType<GeomCamera>("GeomCamera", "Camera", value::TYPE_ID_GEOM_CAMERA);
  RegisterType<GeomBasisCurves>("GeomBasisCurves", "BasisCurves", value::TYPE_ID_GEOM_BASIS_CURVES);
  RegisterType<GeomNurbsCurves>("GeomNurbsCurves", "NurbsCurves", value::TYPE_ID_GEOM_NURBS_CURVES);
  RegisterType<GeomPointInstancer>("GeomPointInstancer", "PointInstancer", value::TYPE_ID_GEOM_POINT_INSTANCER);
  
  // Light types
  RegisterType<DomeLight>("DomeLight", "DomeLight", value::TYPE_ID_LUX_DOME);
  RegisterType<SphereLight>("SphereLight", "SphereLight", value::TYPE_ID_LUX_SPHERE);
  RegisterType<CylinderLight>("CylinderLight", "CylinderLight", value::TYPE_ID_LUX_CYLINDER);
  RegisterType<DiskLight>("DiskLight", "DiskLight", value::TYPE_ID_LUX_DISK);
  RegisterType<DistantLight>("DistantLight", "DistantLight", value::TYPE_ID_LUX_DISTANT);
  RegisterType<RectLight>("RectLight", "RectLight", value::TYPE_ID_LUX_RECT);
  RegisterType<GeometryLight>("GeometryLight", "GeometryLight", value::TYPE_ID_LUX_GEOMETRY);
  RegisterType<PortalLight>("PortalLight", "PortalLight", value::TYPE_ID_LUX_PORTAL);
  RegisterType<PluginLight>("PluginLight", "PluginLight", value::TYPE_ID_LUX_PLUGIN);
  
  // Shading types
  RegisterType<Material>("Material", "Material", value::TYPE_ID_MATERIAL);
  RegisterType<Shader>("Shader", "Shader", value::TYPE_ID_SHADER);
  RegisterType<NodeGraph>("NodeGraph", "NodeGraph", value::TYPE_ID_NODEGRAPH);
  
  // Skeletal animation types
  RegisterType<SkelRoot>("SkelRoot", "SkelRoot", value::TYPE_ID_SKEL_ROOT);
  RegisterType<Skeleton>("Skeleton", "Skeleton", value::TYPE_ID_SKELETON);
  RegisterType<SkelAnimation>("SkelAnimation", "SkelAnimation", value::TYPE_ID_SKELANIMATION);
  RegisterType<BlendShape>("BlendShape", "BlendShape", value::TYPE_ID_BLENDSHAPE);
}

const TypeInfo* TypeRegistry::GetTypeInfo(const std::type_info &type) const {
  return GetTypeInfo(std::type_index(type));
}

const TypeInfo* TypeRegistry::GetTypeInfo(std::type_index idx) const {
  auto it = type_map_.find(idx);
  if (it != type_map_.end()) {
    return it->second.get();
  }
  return nullptr;
}

const TypeInfo* TypeRegistry::GetTypeInfoByName(const std::string &name) const {
  auto it = name_map_.find(name);
  if (it != name_map_.end()) {
    return it->second;
  }
  return nullptr;
}

const TypeInfo* TypeRegistry::GetTypeInfoByUsdName(const std::string &usd_name) const {
  auto it = usd_name_map_.find(usd_name);
  if (it != usd_name_map_.end()) {
    return it->second;
  }
  return nullptr;
}

const TypeInfo* TypeRegistry::GetTypeInfoById(uint32_t type_id) const {
  auto it = id_map_.find(type_id);
  if (it != id_map_.end()) {
    return it->second;
  }
  return nullptr;
}

std::vector<const TypeInfo*> TypeRegistry::GetAllTypes() const {
  std::vector<const TypeInfo*> result;
  for (const auto &pair : type_map_) {
    result.push_back(pair.second.get());
  }
  return result;
}

PrimMeta* TypeRegistry::GetPrimMeta(value::Value &v) const {
  // Try each registered type
  for (const auto &pair : type_map_) {
    if (auto meta = pair.second->get_prim_meta(v)) {
      return meta;
    }
  }
  return nullptr;
}

const PrimMeta* TypeRegistry::GetPrimMeta(const value::Value &v) const {
  // Try each registered type
  for (const auto &pair : type_map_) {
    if (auto meta = pair.second->get_prim_meta_const(v)) {
      return meta;
    }
  }
  return nullptr;
}

std::map<std::string, Property>* TypeRegistry::GetProperties(value::Value &v) const {
  // Try each registered type
  for (const auto &pair : type_map_) {
    if (auto props = pair.second->get_properties(v)) {
      return props;
    }
  }
  return nullptr;
}

const std::map<std::string, Property>* TypeRegistry::GetProperties(const value::Value &v) const {
  // Try each registered type
  for (const auto &pair : type_map_) {
    if (auto props = pair.second->get_properties_const(v)) {
      return props;
    }
  }
  return nullptr;
}

void TypeRegistry::AcceptVisitor(value::Value &v, TypeVisitor &visitor) {
  // Dispatch to appropriate visitor method based on type
  
  // Try Model
  if (auto ptr = v.as<Model>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try Scope
  if (auto ptr = v.as<Scope>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try Xform
  if (auto ptr = v.as<Xform>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try GeomMesh
  if (auto ptr = v.as<GeomMesh>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try GeomPoints
  if (auto ptr = v.as<GeomPoints>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try GeomCube
  if (auto ptr = v.as<GeomCube>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try GeomSphere
  if (auto ptr = v.as<GeomSphere>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try GeomCylinder
  if (auto ptr = v.as<GeomCylinder>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try GeomCone
  if (auto ptr = v.as<GeomCone>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try GeomCapsule
  if (auto ptr = v.as<GeomCapsule>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try GeomSubset
  if (auto ptr = v.as<GeomSubset>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try GeomCamera
  if (auto ptr = v.as<GeomCamera>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try GeomBasisCurves
  if (auto ptr = v.as<GeomBasisCurves>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try DomeLight
  if (auto ptr = v.as<DomeLight>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try SphereLight
  if (auto ptr = v.as<SphereLight>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try CylinderLight
  if (auto ptr = v.as<CylinderLight>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try DiskLight
  if (auto ptr = v.as<DiskLight>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try DistantLight
  if (auto ptr = v.as<DistantLight>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try RectLight
  if (auto ptr = v.as<RectLight>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try Material
  if (auto ptr = v.as<Material>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try Shader
  if (auto ptr = v.as<Shader>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try SkelRoot
  if (auto ptr = v.as<SkelRoot>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try Skeleton
  if (auto ptr = v.as<Skeleton>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try SkelAnimation
  if (auto ptr = v.as<SkelAnimation>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try BlendShape
  if (auto ptr = v.as<BlendShape>()) {
    visitor.Visit(*ptr);
    return;
  }
}

void TypeRegistry::AcceptVisitor(const value::Value &v, TypeVisitor &visitor) {
  // Dispatch to appropriate visitor method based on type (const version)
  
  // Try Model
  if (auto ptr = v.as<Model>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try Scope
  if (auto ptr = v.as<Scope>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try Xform
  if (auto ptr = v.as<Xform>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try GeomMesh
  if (auto ptr = v.as<GeomMesh>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try GeomPoints
  if (auto ptr = v.as<GeomPoints>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try GeomCube
  if (auto ptr = v.as<GeomCube>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try GeomSphere
  if (auto ptr = v.as<GeomSphere>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try GeomCylinder
  if (auto ptr = v.as<GeomCylinder>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try GeomCone
  if (auto ptr = v.as<GeomCone>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try GeomCapsule
  if (auto ptr = v.as<GeomCapsule>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try GeomSubset
  if (auto ptr = v.as<GeomSubset>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try GeomCamera
  if (auto ptr = v.as<GeomCamera>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try GeomBasisCurves
  if (auto ptr = v.as<GeomBasisCurves>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try DomeLight
  if (auto ptr = v.as<DomeLight>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try SphereLight
  if (auto ptr = v.as<SphereLight>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try CylinderLight
  if (auto ptr = v.as<CylinderLight>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try DiskLight
  if (auto ptr = v.as<DiskLight>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try DistantLight
  if (auto ptr = v.as<DistantLight>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try RectLight
  if (auto ptr = v.as<RectLight>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try Material
  if (auto ptr = v.as<Material>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try Shader
  if (auto ptr = v.as<Shader>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try SkelRoot
  if (auto ptr = v.as<SkelRoot>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try Skeleton
  if (auto ptr = v.as<Skeleton>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try SkelAnimation
  if (auto ptr = v.as<SkelAnimation>()) {
    visitor.Visit(*ptr);
    return;
  }
  
  // Try BlendShape
  if (auto ptr = v.as<BlendShape>()) {
    visitor.Visit(*ptr);
    return;
  }
}

} // namespace tinyusdz