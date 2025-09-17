// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Centralized type registry system
// Replaces macro-based type registration

#pragma once

#include <string>
#include <typeinfo>
#include <typeindex>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "value-types.hh"
#include "prim-types.hh"

namespace tinyusdz {

// Forward declarations
struct PrimMeta;
class Property;

// Type visitor interface
class TypeVisitor {
 public:
  virtual ~TypeVisitor() = default;
  
  // Visit methods for each type
  virtual void Visit(Model &obj) {}
  virtual void Visit(const Model &obj) {}
  virtual void Visit(Scope &obj) {}
  virtual void Visit(const Scope &obj) {}
  virtual void Visit(Xform &obj) {}
  virtual void Visit(const Xform &obj) {}
  virtual void Visit(GeomMesh &obj) {}
  virtual void Visit(const GeomMesh &obj) {}
  virtual void Visit(GeomPoints &obj) {}
  virtual void Visit(const GeomPoints &obj) {}
  virtual void Visit(GeomCube &obj) {}
  virtual void Visit(const GeomCube &obj) {}
  virtual void Visit(GeomSphere &obj) {}
  virtual void Visit(const GeomSphere &obj) {}
  virtual void Visit(GeomCylinder &obj) {}
  virtual void Visit(const GeomCylinder &obj) {}
  virtual void Visit(GeomCone &obj) {}
  virtual void Visit(const GeomCone &obj) {}
  virtual void Visit(GeomCapsule &obj) {}
  virtual void Visit(const GeomCapsule &obj) {}
  virtual void Visit(GeomSubset &obj) {}
  virtual void Visit(const GeomSubset &obj) {}
  virtual void Visit(GeomCamera &obj) {}
  virtual void Visit(const GeomCamera &obj) {}
  virtual void Visit(GeomBasisCurves &obj) {}
  virtual void Visit(const GeomBasisCurves &obj) {}
  virtual void Visit(DomeLight &obj) {}
  virtual void Visit(const DomeLight &obj) {}
  virtual void Visit(SphereLight &obj) {}
  virtual void Visit(const SphereLight &obj) {}
  virtual void Visit(CylinderLight &obj) {}
  virtual void Visit(const CylinderLight &obj) {}
  virtual void Visit(DiskLight &obj) {}
  virtual void Visit(const DiskLight &obj) {}
  virtual void Visit(DistantLight &obj) {}
  virtual void Visit(const DistantLight &obj) {}
  virtual void Visit(RectLight &obj) {}
  virtual void Visit(const RectLight &obj) {}
  virtual void Visit(Material &obj) {}
  virtual void Visit(const Material &obj) {}
  virtual void Visit(Shader &obj) {}
  virtual void Visit(const Shader &obj) {}
  virtual void Visit(SkelRoot &obj) {}
  virtual void Visit(const SkelRoot &obj) {}
  virtual void Visit(Skeleton &obj) {}
  virtual void Visit(const Skeleton &obj) {}
  virtual void Visit(SkelAnimation &obj) {}
  virtual void Visit(const SkelAnimation &obj) {}
  virtual void Visit(BlendShape &obj) {}
  virtual void Visit(const BlendShape &obj) {}
};

// Type information structure
struct TypeInfo {
  std::string name;
  std::string usd_type_name;
  std::type_index type_index;
  uint32_t type_id;
  size_t size;
  
  // Function pointers for type operations
  std::function<PrimMeta*(value::Value&)> get_prim_meta;
  std::function<const PrimMeta*(const value::Value&)> get_prim_meta_const;
  std::function<std::map<std::string, Property>*(value::Value&)> get_properties;
  std::function<const std::map<std::string, Property>*(const value::Value&)> get_properties_const;
  std::function<void*(value::Value&)> get_raw_ptr;
  std::function<const void*(const value::Value&)> get_raw_ptr_const;
  
  TypeInfo(const std::type_info &ti) : type_index(ti) {}
};

// Type registry singleton
class TypeRegistry {
 public:
  static TypeRegistry& GetInstance() {
    static TypeRegistry instance;
    return instance;
  }
  
  // Register a type
  template <typename T>
  void RegisterType(const std::string &name,
                   const std::string &usd_name,
                   uint32_t type_id);
  
  // Get type information
  const TypeInfo* GetTypeInfo(const std::type_info &type) const;
  const TypeInfo* GetTypeInfo(std::type_index idx) const;
  const TypeInfo* GetTypeInfoByName(const std::string &name) const;
  const TypeInfo* GetTypeInfoByUsdName(const std::string &usd_name) const;
  const TypeInfo* GetTypeInfoById(uint32_t type_id) const;
  
  // Get all registered types
  std::vector<const TypeInfo*> GetAllTypes() const;
  
  // Type operations using registry
  PrimMeta* GetPrimMeta(value::Value &v) const;
  const PrimMeta* GetPrimMeta(const value::Value &v) const;
  
  std::map<std::string, Property>* GetProperties(value::Value &v) const;
  const std::map<std::string, Property>* GetProperties(const value::Value &v) const;
  
  // Apply visitor pattern
  void AcceptVisitor(value::Value &v, TypeVisitor &visitor);
  void AcceptVisitor(const value::Value &v, TypeVisitor &visitor);
  
 private:
  TypeRegistry() { InitializeBuiltinTypes(); }
  ~TypeRegistry() = default;
  
  TypeRegistry(const TypeRegistry&) = delete;
  TypeRegistry& operator=(const TypeRegistry&) = delete;
  
  void InitializeBuiltinTypes();
  
  // Type maps for fast lookup
  std::unordered_map<std::type_index, std::unique_ptr<TypeInfo>> type_map_;
  std::unordered_map<std::string, TypeInfo*> name_map_;
  std::unordered_map<std::string, TypeInfo*> usd_name_map_;
  std::unordered_map<uint32_t, TypeInfo*> id_map_;
};

// Template implementation
template <typename T>
void TypeRegistry::RegisterType(const std::string &name,
                               const std::string &usd_name,
                               uint32_t type_id) {
  auto info = std::make_unique<TypeInfo>(typeid(T));
  info->name = name;
  info->usd_type_name = usd_name;
  info->type_id = type_id;
  info->size = sizeof(T);
  
  // Set up function pointers
  info->get_prim_meta = [](value::Value &v) -> PrimMeta* {
    if (auto ptr = v.as<T>()) {
      return &(ptr->meta);
    }
    return nullptr;
  };
  
  info->get_prim_meta_const = [](const value::Value &v) -> const PrimMeta* {
    if (auto ptr = v.as<T>()) {
      return &(ptr->meta);
    }
    return nullptr;
  };
  
  info->get_properties = [](value::Value &v) -> std::map<std::string, Property>* {
    if (auto ptr = v.as<T>()) {
      return &(ptr->props);
    }
    return nullptr;
  };
  
  info->get_properties_const = [](const value::Value &v) -> const std::map<std::string, Property>* {
    if (auto ptr = v.as<T>()) {
      return &(ptr->props);
    }
    return nullptr;
  };
  
  info->get_raw_ptr = [](value::Value &v) -> void* {
    return v.as<T>();
  };
  
  info->get_raw_ptr_const = [](const value::Value &v) -> const void* {
    return v.as<T>();
  };
  
  // Store in maps
  TypeInfo* info_ptr = info.get();
  type_map_[info->type_index] = std::move(info);
  name_map_[name] = info_ptr;
  usd_name_map_[usd_name] = info_ptr;
  id_map_[type_id] = info_ptr;
}

// Helper macros for registration (much simpler than before)
#define REGISTER_PRIM_TYPE(TypeName, UsdName, TypeId) \
  TypeRegistry::GetInstance().RegisterType<TypeName>(#TypeName, UsdName, TypeId)

// Visitor-based type dispatcher
template <typename Visitor>
class TypeDispatcher {
 public:
  explicit TypeDispatcher(Visitor &visitor) : visitor_(visitor) {}
  
  template <typename T>
  void Dispatch(T &obj) {
    visitor_.Visit(obj);
  }
  
  template <typename T>
  void Dispatch(const T &obj) {
    visitor_.Visit(obj);
  }
  
  void DispatchValue(value::Value &v) {
    TypeRegistry::GetInstance().AcceptVisitor(v, visitor_);
  }
  
  void DispatchValue(const value::Value &v) {
    TypeRegistry::GetInstance().AcceptVisitor(v, visitor_);
  }
  
 private:
  Visitor &visitor_;
};

// Convenience functions using the registry
inline PrimMeta* GetPrimMeta(value::Value &v) {
  return TypeRegistry::GetInstance().GetPrimMeta(v);
}

inline const PrimMeta* GetPrimMeta(const value::Value &v) {
  return TypeRegistry::GetInstance().GetPrimMeta(v);
}

inline std::map<std::string, Property>* GetProperties(value::Value &v) {
  return TypeRegistry::GetInstance().GetProperties(v);
}

inline const std::map<std::string, Property>* GetProperties(const value::Value &v) {
  return TypeRegistry::GetInstance().GetProperties(v);
}

} // namespace tinyusdz