// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Prim reconstruction utilities for USDC reader
// Extracted from usdc-reader.cc

#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include "value-types.hh"
#include "prim-types.hh"
#include "crate-reader.hh"
#include "nonstd/expected.hpp"

namespace tinyusdz {
namespace usdc {

// Forward declarations
class Path;
class Prim;
class PrimSpec;
class Stage;
class Layer;
class Property;
struct AttrMeta;
struct RelMeta;
struct PrimMeta;

// Property map for reconstruction
using PropertyMap = std::map<std::string, Property>;
using ReferenceList = std::vector<Reference>;

// Prim reconstruction configuration
struct PrimReconstructConfig {
  bool strict_mode = false;
  bool allow_custom_prims = true;
  bool validate_schema = false;
  size_t max_depth = 1024;
  size_t max_prims = 1000000;  // 1M prims max by default
};

// Base prim reconstructor interface
class PrimReconstructor {
public:
  virtual ~PrimReconstructor() = default;
  
  // Main reconstruction method
  virtual bool Reconstruct(
      const Specifier &spec,
      const PropertyMap &properties,
      const ReferenceList &references,
      const crate::CrateReader::Node &node,
      Prim &prim,
      std::string *err) = 0;
  
  // Get supported type name
  virtual std::string GetTypeName() const = 0;
  
  // Validate reconstructed prim
  virtual bool Validate(const Prim &prim, std::string *warn) const {
    return true;
  }
};

// Template for typed prim reconstructors
template<typename T>
class TypedPrimReconstructor : public PrimReconstructor {
public:
  bool Reconstruct(
      const Specifier &spec,
      const PropertyMap &properties,
      const ReferenceList &references,
      const crate::CrateReader::Node &node,
      Prim &prim,
      std::string *err) override;
  
  std::string GetTypeName() const override;
  
protected:
  virtual bool ReconstructTyped(
      const Specifier &spec,
      const PropertyMap &properties,
      const ReferenceList &references,
      T &typed_prim,
      std::string *err) = 0;
};

// Concrete reconstructors for each prim type
class XformReconstructor : public TypedPrimReconstructor<Xform> {
protected:
  bool ReconstructTyped(const Specifier &spec, const PropertyMap &props,
                       const ReferenceList &refs, Xform &xform,
                       std::string *err) override;
};

class GeomMeshReconstructor : public TypedPrimReconstructor<GeomMesh> {
protected:
  bool ReconstructTyped(const Specifier &spec, const PropertyMap &props,
                       const ReferenceList &refs, GeomMesh &mesh,
                       std::string *err) override;
};

class GeomCameraReconstructor : public TypedPrimReconstructor<GeomCamera> {
protected:
  bool ReconstructTyped(const Specifier &spec, const PropertyMap &props,
                       const ReferenceList &refs, GeomCamera &camera,
                       std::string *err) override;
};

class MaterialReconstructor : public TypedPrimReconstructor<Material> {
protected:
  bool ReconstructTyped(const Specifier &spec, const PropertyMap &props,
                       const ReferenceList &refs, Material &material,
                       std::string *err) override;
};

class ShaderReconstructor : public TypedPrimReconstructor<Shader> {
protected:
  bool ReconstructTyped(const Specifier &spec, const PropertyMap &props,
                       const ReferenceList &refs, Shader &shader,
                       std::string *err) override;
};

// Light reconstructors
class SphereLightReconstructor : public TypedPrimReconstructor<SphereLight> {
protected:
  bool ReconstructTyped(const Specifier &spec, const PropertyMap &props,
                       const ReferenceList &refs, SphereLight &light,
                       std::string *err) override;
};

class DomeLightReconstructor : public TypedPrimReconstructor<DomeLight> {
protected:
  bool ReconstructTyped(const Specifier &spec, const PropertyMap &props,
                       const ReferenceList &refs, DomeLight &light,
                       std::string *err) override;
};

// Skeletal animation reconstructors
class SkelRootReconstructor : public TypedPrimReconstructor<SkelRoot> {
protected:
  bool ReconstructTyped(const Specifier &spec, const PropertyMap &props,
                       const ReferenceList &refs, SkelRoot &skel,
                       std::string *err) override;
};

class SkeletonReconstructor : public TypedPrimReconstructor<Skeleton> {
protected:
  bool ReconstructTyped(const Specifier &spec, const PropertyMap &props,
                       const ReferenceList &refs, Skeleton &skeleton,
                       std::string *err) override;
};

// Factory for creating reconstructors
class PrimReconstructorFactory {
public:
  static PrimReconstructorFactory& GetInstance();
  
  // Register a reconstructor for a type
  void RegisterReconstructor(
      const std::string &typeName,
      std::shared_ptr<PrimReconstructor> reconstructor);
  
  // Get reconstructor for type
  std::shared_ptr<PrimReconstructor> GetReconstructor(
      const std::string &typeName) const;
  
  // Check if type is registered
  bool HasReconstructor(const std::string &typeName) const;
  
  // Get all registered type names
  std::vector<std::string> GetRegisteredTypes() const;
  
  // Initialize with default reconstructors
  void InitializeDefaults();
  
private:
  PrimReconstructorFactory() = default;
  std::map<std::string, std::shared_ptr<PrimReconstructor>> reconstructors_;
};

// Utility functions for reconstruction
namespace reconstruct_utils {

// Property extraction helpers
bool ExtractXformOps(const PropertyMap &props, Xform &xform);
bool ExtractMeshTopology(const PropertyMap &props, GeomMesh &mesh);
bool ExtractMaterialBindings(const PropertyMap &props, Prim &prim);
bool ExtractDisplayColor(const PropertyMap &props, value::float3 &color);
bool ExtractVisibility(const PropertyMap &props, Visibility &vis);

// Metadata extraction
bool ExtractPrimMeta(const crate::FieldValuePairVector &fvs, PrimMeta &meta);
bool ExtractAttrMeta(const crate::FieldValuePairVector &fvs, AttrMeta &meta);
bool ExtractRelMeta(const crate::FieldValuePairVector &fvs, RelMeta &meta);

// Schema validation
bool ValidateSchema(const std::string &typeName, const PropertyMap &props,
                   std::string *err);

// API schemas handling
struct APISchemas {
  std::vector<std::string> schemas;
  std::map<std::string, value::Value> schema_data;
};

nonstd::expected<APISchemas, std::string> ParseAPISchemas(
    const value::Value &listOpValue);

bool ApplyAPISchemas(const APISchemas &schemas, Prim &prim);

// Property type registration
void RegisterPrimAttrTypes(std::set<std::string> &types);
bool IsRegisteredValueType(const std::string &typeName);

// Variant handling
struct VariantInfo {
  std::string variantSetName;
  std::string variantName;
  std::map<std::string, Prim> variants;
};

bool ExtractVariantSets(const PropertyMap &props,
                        std::map<std::string, VariantInfo> &variantSets);

// Reference and payload handling
bool ResolveReferences(const ReferenceList &refs, Prim &prim,
                      const std::string &baseDir);

// Composition arc handling
bool ApplyCompositionArcs(const PropertyMap &props, Prim &prim);

} // namespace reconstruct_utils

} // namespace usdc
} // namespace tinyusdz