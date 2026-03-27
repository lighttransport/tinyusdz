// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Experimental USDC (Crate) File Writer
// Bare framework for writing USD Layer/PrimSpec to binary Crate format
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <fstream>

// TinyUSDZ crate format definitions
#include "crate-format.hh"
#include "stage.hh"
#include "usdGeom.hh"
#include "usdShade.hh"
#include "usdSkel.hh"
#include "usdLux.hh"

// Path sorting and encoding library
#include "crate-path-utils/path_interface.hh"
#include "crate-path-utils/path_sort.hh"
#include "crate-path-utils/tree_encode.hh"

namespace tinyusdz {
namespace experimental {

///
/// ErrorContextStack - Tracks error context during crate writing operations
///
/// Provides detailed error messages with full operation history and path context
///
class ErrorContextStack {
 public:
  ErrorContextStack(int max_depth = 10) : max_depth_(max_depth) {}

  /// Push an operation context onto the stack
  void Push(const std::string& operation) {
    if (static_cast<int>(stack_.size()) < max_depth_) {
      stack_.push_back(operation);
    }
  }

  /// Pop an operation context from the stack
  void Pop() {
    if (!stack_.empty()) {
      stack_.pop_back();
    }
  }

  /// Get the current full context path
  std::string GetContextPath() const {
    if (stack_.empty()) return "";

    std::string result;
    for (size_t i = 0; i < stack_.size(); ++i) {
      if (i > 0) result += " -> ";
      result += stack_[i];
    }
    return result;
  }

  /// Get detailed error message with context
  std::string GetDetailedErrorMessage(const std::string& base_error) const {
    if (stack_.empty()) {
      return base_error;
    }

    std::string detailed = base_error + "\nContext Stack:\n";
    for (size_t i = 0; i < stack_.size(); ++i) {
      detailed += "  [" + std::to_string(i) + "] " + stack_[i] + "\n";
    }
    return detailed;
  }

  /// Check if stack is at maximum depth
  bool IsMaxDepth() const {
    return static_cast<int>(stack_.size()) >= max_depth_;
  }

  /// Clear the entire stack
  void Clear() { stack_.clear(); }

  /// Get stack size
  size_t Size() const { return stack_.size(); }

 private:
  std::vector<std::string> stack_;
  int max_depth_;
};

///
/// IOutputStream - Abstract output stream for CrateWriter I/O
///
/// Allows CrateWriter to write to files or memory buffers interchangeably.
///
class IOutputStream {
public:
  virtual ~IOutputStream();
  virtual bool Open(std::string* err) = 0;
  virtual void Close() = 0;
  virtual bool IsOpen() const = 0;
  virtual int64_t Tell() = 0;
  virtual bool Seek(int64_t pos) = 0;
  virtual bool Write(const void* data, size_t size) = 0;
  virtual bool Flush() = 0;
};

///
/// MemoryOutputStream - Writes to an in-memory buffer
///
/// Used by SaveAsUSDCToMemory to avoid temp-file round-trips.
///
class MemoryOutputStream : public IOutputStream {
public:
  MemoryOutputStream() = default;
  ~MemoryOutputStream() override;
  bool Open(std::string*) override { pos_ = 0; buffer_.clear(); return true; }
  void Close() override {}
  bool IsOpen() const override { return true; }
  int64_t Tell() override { return static_cast<int64_t>(pos_); }
  bool Seek(int64_t pos) override {
    if (pos < 0) return false;
    size_t p = static_cast<size_t>(pos);
    if (p > buffer_.size()) buffer_.resize(p, 0);
    pos_ = p;
    return true;
  }
  bool Write(const void* data, size_t size) override {
    if (pos_ + size > buffer_.size()) buffer_.resize(pos_ + size);
    std::memcpy(buffer_.data() + pos_, data, size);
    pos_ += size;
    return true;
  }
  bool Flush() override { return true; }

  std::vector<uint8_t> TakeBuffer() { return std::move(buffer_); }
  const std::vector<uint8_t>& GetBuffer() const { return buffer_; }
private:
  std::vector<uint8_t> buffer_;
  size_t pos_ = 0;
};

///
/// CrateWriter - Experimental framework for writing USDC binary files
///
/// This is a bare-bones implementation focusing on core structure.
/// Implements Crate format version 0.8.0 (stable, production-ready)
///
/// Key features:
/// - Bootstrap header with magic "PXR-USDC"
/// - Table of Contents (TOC) structure
/// - Structural sections: TOKENS, STRINGS, FIELDS, FIELDSETS, PATHS, SPECS
/// - Token/String/Path/Value deduplication
/// - Integration with path sorting/encoding library
///
/// Current limitations (experimental):
/// - No compression (will add in future)
/// - Limited type support (basic types only)
/// - No async I/O
/// - No zero-copy optimization
///
class CrateWriter {
public:
  /// Create a new crate writer for the given file path
  explicit CrateWriter(const std::string& filepath);

  /// Create a new crate writer with a custom output stream
  explicit CrateWriter(std::unique_ptr<IOutputStream> stream);

  ~CrateWriter();

  ///
  /// Initialize the writer and prepare for writing
  ///
  bool Open(std::string* err = nullptr);

  ///
  /// Add a spec (Prim, Property, etc.) to the file
  ///
  /// @param path Path for this spec (e.g., "/Root/Geo/Mesh")
  /// @param spec_type Type of spec (Prim, Attribute, etc.)
  /// @param fields Map of field name -> value for this spec
  ///
  bool AddSpec(const Path& path,
               SpecType spec_type,
               const crate::FieldValuePairVector& fields,
               std::string* err = nullptr);

  ///
  /// Finalize and write the file
  ///
  /// This performs:
  /// 1. Sort all paths
  /// 2. Encode path tree
  /// 3. Write all sections
  /// 4. Write TOC
  /// 5. Write bootstrap header
  ///
  bool Finalize(std::string* err = nullptr);

  ///
  /// Close the file (called automatically by destructor)
  ///
  void Close();

  ///
  /// Convert a TinyUSDZ Stage to Crate specs and add them to the file
  ///
  /// This traverses the stage hierarchy and converts all prims and
  /// their properties to Crate format specs.
  ///
  /// @param stage The TinyUSDZ Stage to convert
  /// @param err Optional error message output
  /// @return true on success, false on failure
  ///
  bool ConvertStageToSpecs(const Stage& stage, std::string* err = nullptr);

  ///
  /// Convert a TinyUSDZ Layer to Crate specs and add them to the file
  ///
  /// This traverses the Layer's PrimSpecs and converts them along with
  /// their properties, relationships, and connections to Crate format.
  ///
  /// @param layer The TinyUSDZ Layer to convert
  /// @param err Optional error message output
  /// @return true on success, false on failure
  ///
  bool ConvertLayerToSpecs(const Layer& layer, std::string* err = nullptr);

  // Resource tracking and limit enforcement
  ///
  /// Get current file size in bytes
  ///
  int64_t GetBytesWritten() const { return bytes_written_; }

  ///
  /// Get estimated memory usage
  ///
  int64_t GetMemoryUsageEstimate() const { return memory_used_estimate_; }

  ///
  /// Check if file size limit would be exceeded
  ///
  bool WouldExceedFileSizeLimit(int64_t additional_bytes) const {
    return (bytes_written_ + additional_bytes) > options_.max_file_size_bytes;
  }

  ///
  /// Check if memory limit would be exceeded
  ///
  bool WouldExceedMemoryLimit(int64_t additional_bytes) const {
    return (memory_used_estimate_ + additional_bytes) > options_.max_memory_bytes;
  }

  // Error context tracking
  ///
  /// Get the current error context path
  ///
  std::string GetErrorContextPath() const {
    return error_context_.GetContextPath();
  }

  ///
  /// Get detailed error message with full context
  ///
  std::string GetDetailedErrorMessage(const std::string& error) const {
    return error_context_.GetDetailedErrorMessage(error);
  }

  ///
  /// Clear the error context stack
  ///
  void ClearErrorContext() {
    error_context_.Clear();
  }

  // Validation methods
  ///
  /// Validate a Stage before writing
  /// Checks for common issues like invalid paths, missing prims, cycles, etc.
  ///
  /// @param stage The Stage to validate
  /// @param err Optional error message output
  /// @return true if valid, false if validation fails
  ///
  bool ValidateStage(const Stage& stage, std::string* err = nullptr);

  ///
  /// Validate a Layer before writing
  /// Checks for common issues in PrimSpecs and properties
  ///
  /// @param layer The Layer to validate
  /// @param err Optional error message output
  /// @return true if valid, false if validation fails
  ///
  bool ValidateLayer(const Layer& layer, std::string* err = nullptr);

  ///
  /// Get validation results summary
  ///
  std::string GetValidationSummary() const;

  // Configuration options
  struct Options {
    uint8_t version_major = 0;
    uint8_t version_minor = 8;  // Default to 0.8.0 (stable)
    uint8_t version_patch = 0;

    bool enable_compression = true;   // Phase 4: LZ4 compression enabled by default
    bool enable_deduplication = true; // Deduplicate tokens/strings/paths/values
    bool enable_validation = true;    // Phase 5: Pre-write validation enabled by default

    // Memory and file size limits (with WASM-specific defaults)
    // These prevent resource exhaustion when processing untrusted USD files
#ifdef __wasm__
    // WASM environment has stricter limits
    int64_t max_memory_bytes = 256 * 1024 * 1024;      // 256 MB default for WASM
    int64_t max_file_size_bytes = 100 * 1024 * 1024;   // 100 MB default for WASM
#else
    // Desktop/server environment limits
    int64_t max_memory_bytes = 32 * 1024 * 1024 * 1024LL;  // 32 GB default
    int64_t max_file_size_bytes = 1024 * 1024 * 1024LL;    // 1 GB default
#endif

    // Error context depth (how many stack frames to include in error messages)
    int error_context_depth = 5;
  };

  void SetOptions(const Options& opts) { options_ = opts; }

private:
  // ======================================================================
  // Internal structures
  // ======================================================================

  /// Bootstrap header (64 bytes, at file offset 0)
  struct BootStrap {
    char ident[8];       // "PXR-USDC"
    uint8_t version[8];  // [major, minor, patch, 0, 0, 0, 0, 0]
    int64_t toc_offset;  // File offset to table of contents
    int64_t reserved[6]; // Reserved for future use
  };

  /// Internal spec representation before writing
  struct SpecData {
    Path path;
    SpecType spec_type;  // Store the spec type for later use
    crate::Spec spec;
    crate::FieldValuePairVector fields;
  };

  // ======================================================================
  // Section writing
  // ======================================================================

  /// Write the TOKENS section (token string pool)
  bool WriteTokensSection(std::string* err);

  /// Write the STRINGS section (string -> token index mapping)
  bool WriteStringsSection(std::string* err);

  /// Write the FIELDS section (field name + value pairs)
  bool WriteFieldsSection(std::string* err);

  /// Write the FIELDSETS section (lists of field indices)
  bool WriteFieldSetsSection(std::string* err);

  /// Write the PATHS section (compressed path tree)
  bool WritePathsSection(std::string* err);

  /// Write the SPECS section (spec data)
  bool WriteSpecsSection(std::string* err);

  /// Write the Table of Contents
  bool WriteTableOfContents(std::string* err);

  /// Write the Bootstrap header
  bool WriteBootStrap(std::string* err);

  // ======================================================================
  // Stage conversion helpers
  // ======================================================================

  /// Convert a Prim and its children recursively
  /// Convert a single prim (no children) and add its specs.
  /// Returns true on success. Does not recurse into children.
  bool ConvertSinglePrim(const Prim& prim, const Path& parent_path, std::string* err);

  /// Iterative depth-first conversion of a prim tree.
  bool ConvertPrimIterative(const Prim& root_prim, const Path& parent_path, std::string* err);

  /// Extract properties from a Prim and add as fields
  bool ExtractPrimProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract type-specific properties based on prim type
  bool ExtractTypeSpecificProperties(const Prim& prim, const Path& prim_path, const std::string& type_name,
                                     crate::FieldValuePairVector& fields, std::string* err);

  /// Extract Xform-specific properties (xformOps)
  bool ExtractXformProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract Mesh-specific properties (points, normals, etc.)
  bool ExtractMeshProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract Cube-specific properties (size, extent)
  bool ExtractCubeProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract Sphere-specific properties (radius)
  bool ExtractSphereProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract Cylinder-specific properties (radius, height)
  bool ExtractCylinderProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract Cone-specific properties (radius, height, axis)
  bool ExtractConeProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract Capsule-specific properties (radius, height, axis)
  bool ExtractCapsuleProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract Points-specific properties (points, widths, ids, normals, velocities, accelerations)
  bool ExtractPointsProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract Camera-specific properties (focal length, aperture, clipping range, etc.)
  bool ExtractCameraProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract BasisCurves-specific properties (type, basis, wrap, points, widths, etc.)
  bool ExtractBasisCurvesProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract NurbsCurves-specific properties (order, knots, ranges, pointWeights, points, etc.)
  bool ExtractNurbsCurvesProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract PointInstancer-specific properties (protoIndices, positions, orientations, scales, velocities, etc.)
  bool ExtractPointInstancerProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract GeomSubset-specific properties (elementType, familyName, indices)
  bool ExtractGeomSubsetProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract BlendShape properties (offsets, normalOffsets, pointIndices)
  bool ExtractBlendShapeProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract SphereLight properties (radius, color, intensity, exposure, shaping)
  bool ExtractSphereLightProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract RectLight properties (width, height, color, intensity, exposure, shaping)
  bool ExtractRectLightProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract DiskLight properties (radius, color, intensity, exposure, shaping)
  bool ExtractDiskLightProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract CylinderLight properties (radius, length, color, intensity, exposure, shaping)
  bool ExtractCylinderLightProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract DistantLight properties (angle, color, intensity, exposure)
  bool ExtractDistantLightProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract DomeLight properties (texture path, color, intensity, exposure)
  bool ExtractDomeLightProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract GeometryLight properties (geometry relationship, color, intensity, exposure)
  bool ExtractGeometryLightProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract PortalLight properties (geometry relationship, color, intensity, exposure)
  bool ExtractPortalLightProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract Skeleton properties (jointNames, joints, bindTransforms, restTransforms)
  bool ExtractSkeletonProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract SkelAnimation properties (joints, rotations, translations, scales, blendShapeWeights)
  bool ExtractSkelAnimationProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract SkelRoot properties (visibility, purpose, extent)
  bool ExtractSkelRootProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract PrimMeta fields (kind, active, hidden, customData, apiSchemas, references, payload, inherits, specializes, etc.)
  void ExtractPrimMeta(const PrimMeta& metas, crate::FieldValuePairVector& fields);

  /// Extract common GPrim properties (visibility, purpose, etc.)
  bool ExtractGPrimProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract Material properties (outputs: surface, displacement, volume)
  bool ExtractMaterialProperties(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Add Material output specs as separate attribute specs (called after Material prim spec is added)
  bool AddMaterialOutputSpecs(const Material* material, const Path& prim_path, std::string* err);

  /// Extract Shader properties (info:id, inputs, outputs)
  bool ExtractShaderProperties(const Prim& prim, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract NodeGraph properties (shader network container)
  bool ExtractNodeGraphProperties(const Prim& prim, crate::FieldValuePairVector& fields, std::string* err);

  /// Add UsdPreviewSurface shader input specs as separate attribute specs (called after Shader prim spec is added)
  bool AddUsdPreviewSurfaceInputSpecs(const UsdPreviewSurface* preview_surface, const Path& prim_path, std::string* err);

  /// Add UsdUVTexture shader input specs as separate attribute specs (called after Shader prim spec is added)
  bool AddUsdUVTextureInputSpecs(const UsdUVTexture* uv_texture, const Path& prim_path, std::string* err);

  /// Add UsdPrimvarReader shader input specs as separate attribute specs (called after Shader prim spec is added)
  /// This works with all UsdPrimvarReader_* variants (float, float2, float3, etc.)
  bool AddUsdPrimvarReaderInputSpecs(const value::Value& shader_value, const std::string& reader_type, const Path& prim_path, std::string* err);

  /// Add UsdTransform2d shader input specs as separate attribute specs (called after Shader prim spec is added)
  bool AddUsdTransform2dInputSpecs(const UsdTransform2d* transform2d, const Path& prim_path, std::string* err);

  /// Add material binding relationships as separate relationship specs (called after Prim spec is added)
  /// Handles material:binding, material:binding:preview, and material:binding:full relationships
  bool AddMaterialBindingSpecs(const Prim& prim, const Path& prim_path, std::string* err);

  /// Add light filter relationships as separate relationship specs (called after Light prim spec is added)
  /// Handles light:filters relationships for SphereLight, RectLight, DiskLight, CylinderLight, DistantLight, DomeLight
  bool AddLightFilterSpecs(const Prim& prim, const Path& prim_path, std::string* err);

  /// Add PointInstancer prototypes relationship as separate relationship spec
  bool AddPointInstancerPrototypesSpec(const Prim& prim, const Path& prim_path, std::string* err);

  /// Extract xformOps from Xformable (GPrim or Xform)
  /// Creates separate Attribute specs for each xformOp property
  bool ExtractXformOpsFromXformable(const Prim& prim, const Path& prim_path, crate::FieldValuePairVector& fields, std::string* err);

  /// Convert TinyUSDZ value to CrateValue
  bool ConvertValue(const value::Value& val, crate::CrateValue& out, std::string* err);

  /// Convert a value::Value to CrateValue and append to fields.
  bool AddArrayAttribute(const std::string& attr_name, const value::Value& val,
                         crate::FieldValuePairVector& fields, std::string* err);

  /// Convert an enum string to a token and append to fields.
  void AddEnumAttribute(const std::string& attr_name, const std::string& enum_val,
                        crate::FieldValuePairVector& fields);

  /// Convert a typed optional attribute to CrateValue and append to fields.
  /// Silently skips unsupported types rather than failing.
  template<typename T>
  bool AddTypedArrayAttribute(const char* name, const T& typed_attr,
                              crate::FieldValuePairVector& fields);

  /// Extract default value (and time samples if present) from an Animatable<T>
  /// into FieldValuePairVector.  Reduces the 10-line per-attribute boilerplate
  /// in Extract*Properties helpers to a single call.
  template<typename T>
  bool ExtractAnimatableDefault(const Animatable<T>& anim, const char* name,
                                crate::FieldValuePairVector& fields, std::string* err);

  // ======================================================================
  // Layer/PrimSpec conversion helpers
  // ======================================================================

  /// Convert a PrimSpec and its children recursively
  /// Convert a single PrimSpec (no children) and add its specs.
  bool ConvertSinglePrimSpec(const PrimSpec& primspec, const Path& parent_path, std::string* err);

  /// Iterative depth-first conversion of a PrimSpec tree.
  bool ConvertPrimSpecIterative(const PrimSpec& primspec, const Path& parent_path, std::string* err);

  /// Convert a Property to Fields (handles Attribute, Relationship, Connection)
  bool ConvertPropertyToFields(const std::string& prop_name, const Property& prop,
                               const Path& parent_path, crate::FieldValuePairVector& fields,
                               std::string* err);

  /// Convert an Attribute to separate spec (proper USD format)
  /// Creates a spec with SpecType::Attribute at parent_path.AppendProperty(attr_name)
  bool ConvertAttributeToFields(const std::string& attr_name, const Attribute& attr,
                                const Path& parent_path, bool is_custom, std::string* err);

  /// Convert a Relationship to separate spec (proper USD format)
  /// Creates a spec with SpecType::Relationship at parent_path.AppendProperty(rel_name)
  bool ConvertRelationshipToFields(const std::string& rel_name, const Relationship& rel,
                                   const Path& parent_path, std::string* err);

  /// Convert an Attribute Connection to separate spec (proper USD format)
  /// Creates a spec with SpecType::Connection
  bool ConvertConnectionToFields(const std::string& conn_name, const Attribute& attr,
                                 const Path& parent_path, std::string* err);

  /// Convert a VariantSet to separate specs (proper USD format)
  /// Creates specs with SpecType::VariantSet for variant selection metadata
  bool ConvertVariantSetToFields(const std::string& variantset_name,
                                 const VariantSet& variantset,
                                 const Path& parent_path, std::string* err);

  /// Convert a Variant to separate spec (proper USD format)
  /// Creates a spec with SpecType::Variant containing variant properties and children
  bool ConvertVariantToFields(const std::string& variant_name,
                              const Variant& variant,
                              const Path& variantset_path,
                              const std::string& variantset_name, std::string* err);

  // ======================================================================
  // Value encoding
  // ======================================================================

  /// Pack a CrateValue into ValueRep
  /// Returns the ValueRep and may write out-of-line data to file
  crate::ValueRep PackValue(const crate::CrateValue& value, std::string* err);

  /// Write a value to the value data section
  /// Returns the file offset where the value was written
  int64_t WriteValueData(const crate::CrateValue& value, bool* is_compressed,
                         std::string* err);

  /// Try to inline a value in ValueRep payload (optimization)
  bool TryInlineValue(const crate::CrateValue& value, crate::ValueRep* rep);

  // ======================================================================
  // Deduplication
  // ======================================================================

  /// Generic get-or-create for deduplication tables
  template<typename KeyType, typename IndexType, typename MapType, typename VecType>
  IndexType GetOrCreateImpl(const KeyType& key, MapType& map, VecType& vec) {
    auto it = map.find(key);
    if (it != map.end()) return it->second;
    IndexType idx(static_cast<uint32_t>(vec.size()));
    vec.push_back(key);
    map[key] = idx;
    return idx;
  }

  /// Get or create token index for a token
  crate::TokenIndex GetOrCreateToken(const std::string& token);

  /// Get or create string index for a string
  crate::StringIndex GetOrCreateString(const std::string& str);

  /// Get or create path index for a path
  crate::PathIndex GetOrCreatePath(const Path& path);

  /// Get or create field index for a field
  crate::FieldIndex GetOrCreateField(const crate::Field& field);

  /// Get or create fieldset index for a fieldset
  crate::FieldSetIndex GetOrCreateFieldSet(const std::vector<crate::FieldIndex>& fieldset);

  // ======================================================================
  // Compression (Phase 4)
  // ======================================================================

  /// Compress data using LZ4
  /// Returns true if compression succeeded, false otherwise
  /// If compression fails or expands data, original data is kept
  bool CompressData(const char* input, size_t inputSize,
                    std::vector<char>* compressed, std::string* err);

  /// Write a compressed integer array (32-bit) using Usd_IntegerCompression.
  /// If compression fails or count < 16, writes uncompressed.
  /// Sets `is_compressed` to match the bytes actually written.
  /// The data pointer must point to count uint32_t values.
  /// Returns -1 on I/O error.
  int64_t WriteCompressedArray32(const uint32_t* data, uint64_t count,
                                 const char* typeName, bool* is_compressed,
                                 std::string* err);

  /// Write a compressed integer array (64-bit) using Usd_IntegerCompression64.
  /// If compression fails or count < 16, writes uncompressed.
  /// Sets `is_compressed` to match the bytes actually written.
  /// The data pointer must point to count uint64_t values.
  /// Returns -1 on I/O error.
  int64_t WriteCompressedArray64(const uint64_t* data, uint64_t count,
                                 const char* typeName, bool* is_compressed,
                                 std::string* err);

  // ======================================================================
  // I/O utilities
  // ======================================================================

  /// Get current file position
  int64_t Tell();

  /// Seek to file position
  bool Seek(int64_t pos);

  /// Write raw bytes
  bool WriteBytes(const void* data, size_t size);

  /// Write typed value
  template<typename T>
  bool Write(const T& value) {
    return WriteBytes(&value, sizeof(T));
  }

  // ======================================================================
  // Member variables
  // ======================================================================

  std::string filepath_;
  std::unique_ptr<IOutputStream> stream_;
  Options options_;

  bool is_open_ = false;
  bool is_finalized_ = false;

  // Memory and file size tracking for resource limits
  int64_t bytes_written_ = 0;           // Current file size in bytes
  int64_t memory_used_estimate_ = 0;    // Estimated memory usage (tokens, strings, paths, specs, etc.)

  // Error context stack for detailed error messages
  ErrorContextStack error_context_{options_.error_context_depth};

  // Validation tracking
  size_t validation_prim_count_ = 0;           // Number of prims validated
  size_t validation_property_count_ = 0;       // Number of properties validated
  size_t validation_warnings_count_ = 0;       // Number of validation warnings
  std::vector<std::string> validation_warnings_;  // Collected validation warnings

  // Deduplication tables
  std::unordered_map<std::string, crate::TokenIndex> token_to_index_;
  std::vector<std::string> tokens_;  // Index -> token string
  std::map<int32_t, uint32_t> path_tree_token_remap_;  // Maps path tree token index -> our token index

  std::unordered_map<std::string, crate::StringIndex> string_to_index_;
  std::vector<std::string> strings_;  // Index -> string

  std::unordered_map<Path, crate::PathIndex, crate::PathHasher, crate::PathKeyEqual> path_to_index_;
  std::vector<Path> paths_;  // Index -> path

  std::unordered_map<crate::Field, crate::FieldIndex, crate::FieldHasher, crate::FieldKeyEqual> field_to_index_;
  std::vector<crate::Field> fields_;  // Index -> field

  std::unordered_map<std::vector<crate::FieldIndex>, crate::FieldSetIndex, crate::FieldSetHasher> fieldset_to_index_;
  std::vector<std::vector<crate::FieldIndex>> fieldsets_;  // Index -> fieldset

  // Spec data (accumulated before writing)
  std::vector<SpecData> spec_data_;

  // Table of contents (filled during writing)
  crate::TableOfContents toc_;

  // Value data offset tracking
  int64_t value_data_start_offset_ = 0;
  int64_t value_data_end_offset_ = 0;

  // Phase 5: TimeSamples value deduplication with NaN-aware hashing.
  // Follows OpenUSD TfHash pattern: +0.0 and -0.0 hash identically;
  // all other values hash by bit pattern.
  struct NanAwareHash {
    static size_t hash_float(float v) {
      uint32_t bits = 0;
      if (v != 0.0f) { std::memcpy(&bits, &v, sizeof(v)); }
      return std::hash<uint32_t>{}(bits);
    }
    static size_t hash_double(double v) {
      uint64_t bits = 0;
      if (v != 0.0) { std::memcpy(&bits, &v, sizeof(v)); }
      return std::hash<uint64_t>{}(bits);
    }
    static size_t combine(size_t seed, size_t h) {
      return seed ^ (h + 0x9e3779b9 + (seed << 6) + (seed >> 2));
    }
    // Hash a buffer with NaN-aware float/double handling.
    // element_size: sizeof(float) or sizeof(double) when is_float is true.
    static size_t hash_buffer(const void *data, size_t byte_count,
                              size_t element_size, bool is_float);
    // NaN-aware buffer equality (canonicalizes +0/-0 before comparison).
    static bool buffers_equal(const void *a, const void *b, size_t byte_count,
                              size_t element_size, bool is_float);
  };
  struct ValueDedupEntry {
    std::vector<char> bytes;
    size_t element_size;
    bool is_float;
    int64_t offset;
  };
  std::unordered_multimap<size_t, ValueDedupEntry> value_dedup_map_;
};

} // namespace experimental
} // namespace tinyusdz
