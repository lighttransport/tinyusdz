// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Schema Registry
// Manages built-in and user-supplied prim schemas

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "lightusd/result.hh"
#include "lightusd/schema.hh"

namespace lightusd {
namespace v1 {

// Forward declarations
class Prim;
class Stage;

// ============================================================================
// Schema Registry
// ============================================================================

/// Central registry for prim schemas
/// Manages both built-in USD schemas and user-defined custom schemas
class SchemaRegistry {
public:
    /// Get the singleton instance
    static SchemaRegistry& instance();

    // === Schema Registration ===

    /// Register a schema from JSON string
    /// @param json JSON schema definition
    /// @param source Optional source identifier (file path, "builtin", etc.)
    /// @return Success or error
    Result<void> register_schema(std::string_view json, const std::string& source = "");

    /// Register a schema object directly
    Result<void> register_schema(PrimSchema schema);

    /// Register multiple schemas from a JSON array string
    Result<void> register_schemas(std::string_view json_array, const std::string& source = "");

    /// Unregister a schema by type name
    bool unregister_schema(const std::string& type_name);

    /// Clear all schemas (including built-ins)
    void clear();

    /// Reset to default built-in schemas only
    void reset_to_defaults();

    // === Schema Lookup ===

    /// Get schema for a prim type
    /// Returns nullptr if not found
    const PrimSchema* get_schema(std::string_view type_name) const;

    /// Check if schema exists for type
    bool has_schema(std::string_view type_name) const;

    /// Get all registered type names
    std::vector<std::string> registered_types() const;

    /// Get all schemas
    const std::map<std::string, PrimSchema>& all_schemas() const { return schemas_; }

    // === Validation ===

    /// Validate a prim against its schema
    /// Uses the prim's type name to find the schema
    ValidationResult validate(const Prim& prim) const;

    /// Validate a prim with explicit schema
    ValidationResult validate(const Prim& prim, const PrimSchema& schema) const;

    /// Validate all prims in a stage
    std::map<std::string, ValidationResult> validate_stage(const Stage& stage) const;

    /// Validate all prims recursively from root
    std::map<std::string, ValidationResult> validate_prim_tree(const Prim& root) const;

    // === Schema Inheritance ===

    /// Get resolved schema with inheritance applied
    /// Merges all parent schemas into a single schema
    Result<PrimSchema> get_resolved_schema(std::string_view type_name) const;

    // === Built-in Schema Access ===

    /// Get JSON string for a built-in schema
    static const char* get_builtin_schema_json(std::string_view type_name);

    /// Get all built-in schema type names
    static std::vector<std::string> builtin_schema_types();

private:
    SchemaRegistry();
    ~SchemaRegistry() = default;

    // Non-copyable
    SchemaRegistry(const SchemaRegistry&) = delete;
    SchemaRegistry& operator=(const SchemaRegistry&) = delete;

    /// Load built-in schemas
    void load_builtin_schemas();

    /// Resolve schema inheritance chain
    Result<void> resolve_inheritance(PrimSchema& schema) const;

    std::map<std::string, PrimSchema> schemas_;
    bool builtins_loaded_ = false;
};

// ============================================================================
// Convenience functions
// ============================================================================

/// Validate a prim using the global registry
inline ValidationResult validate_prim(const Prim& prim) {
    return SchemaRegistry::instance().validate(prim);
}

/// Validate a stage using the global registry
inline std::map<std::string, ValidationResult> validate_stage(const Stage& stage) {
    return SchemaRegistry::instance().validate_stage(stage);
}

/// Register a custom schema
inline Result<void> register_schema(std::string_view json, const std::string& source = "") {
    return SchemaRegistry::instance().register_schema(json, source);
}

/// Get schema for a type
inline const PrimSchema* get_schema(std::string_view type_name) {
    return SchemaRegistry::instance().get_schema(type_name);
}

// ============================================================================
// Built-in Schema JSON Strings (embedded)
// ============================================================================

namespace builtin_schemas {

/// Mesh prim schema
extern const char* const kMeshSchema;

/// Xform prim schema
extern const char* const kXformSchema;

/// Scope prim schema
extern const char* const kScopeSchema;

/// Material prim schema
extern const char* const kMaterialSchema;

/// Shader prim schema
extern const char* const kShaderSchema;

/// NodeGraph prim schema
extern const char* const kNodeGraphSchema;

/// MaterialXConfigAPI schema
extern const char* const kMaterialXConfigAPISchema;

/// Camera prim schema
extern const char* const kCameraSchema;

/// SphereLight prim schema
extern const char* const kSphereLightSchema;

/// DistantLight prim schema
extern const char* const kDistantLightSchema;

/// DomeLight prim schema
extern const char* const kDomeLightSchema;

/// RectLight prim schema
extern const char* const kRectLightSchema;

/// CylinderLight prim schema (light)
extern const char* const kCylinderLightSchema;

/// DiskLight prim schema
extern const char* const kDiskLightSchema;

/// PortalLight prim schema
extern const char* const kPortalLightSchema;

/// MeshLight prim schema
extern const char* const kMeshLightSchema;

/// CollectionAPI schema
extern const char* const kCollectionAPISchema;

/// SkelBindingAPI schema
extern const char* const kSkelBindingAPISchema;

/// SkelRoot prim schema
extern const char* const kSkelRootSchema;

/// Skeleton prim schema
extern const char* const kSkeletonSchema;

/// SkelAnimation prim schema
extern const char* const kSkelAnimationSchema;

/// BlendShape prim schema
extern const char* const kBlendShapeSchema;

/// Points prim schema
extern const char* const kPointsSchema;

/// BasisCurves prim schema
extern const char* const kBasisCurvesSchema;

/// Cube prim schema
extern const char* const kCubeSchema;

/// Sphere prim schema (geometry)
extern const char* const kSphereSchema;

/// Cylinder prim schema
extern const char* const kCylinderSchema;

/// Cone prim schema
extern const char* const kConeSchema;

/// Capsule prim schema
extern const char* const kCapsuleSchema;

} // namespace builtin_schemas

} // namespace v1
} // namespace lightusd
