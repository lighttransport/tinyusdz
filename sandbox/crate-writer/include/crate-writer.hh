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
#include "../../../src/crate-format.hh"
#include "../../../src/stage.hh"
#include "../../../src/usdGeom.hh"
#include "../../../src/usdShade.hh"
#include "../../../src/usdSkel.hh"
#include "../../../src/usdLux.hh"

// Path sorting and encoding library
#include "../../../sandbox/path-sort-and-encode-crate/include/crate/path_interface.hh"
#include "../../../sandbox/path-sort-and-encode-crate/include/crate/path_sort.hh"
#include "../../../sandbox/path-sort-and-encode-crate/include/crate/tree_encode.hh"

namespace tinyusdz {
namespace experimental {

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

  // Configuration options
  struct Options {
    uint8_t version_major = 0;
    uint8_t version_minor = 8;  // Default to 0.8.0 (stable)
    uint8_t version_patch = 0;

    bool enable_compression = true;   // Phase 4: LZ4 compression enabled by default
    bool enable_deduplication = true; // Deduplicate tokens/strings/paths/values
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
  bool ConvertPrimRecursive(const Prim& prim, const Path& parent_path, std::string* err);

  /// Extract properties from a Prim and add as fields
  bool ExtractPrimProperties(const Prim& prim, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract type-specific properties based on prim type
  bool ExtractTypeSpecificProperties(const Prim& prim, const std::string& type_name,
                                     crate::FieldValuePairVector& fields, std::string* err);

  /// Extract Xform-specific properties (xformOps)
  bool ExtractXformProperties(const Prim& prim, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract Mesh-specific properties (points, normals, etc.)
  bool ExtractMeshProperties(const Prim& prim, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract Cube-specific properties (size, extent)
  bool ExtractCubeProperties(const Prim& prim, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract Sphere-specific properties (radius)
  bool ExtractSphereProperties(const Prim& prim, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract Cylinder-specific properties (radius, height)
  bool ExtractCylinderProperties(const Prim& prim, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract common GPrim properties (visibility, purpose, etc.)
  bool ExtractGPrimProperties(const Prim& prim, crate::FieldValuePairVector& fields, std::string* err);

  /// Extract xformOps from Xformable (GPrim or Xform)
  bool ExtractXformOpsFromXformable(const Prim& prim, crate::FieldValuePairVector& fields, std::string* err);

  /// Convert TinyUSDZ value to CrateValue
  bool ConvertValue(const value::Value& val, crate::CrateValue& out, std::string* err);

  // ======================================================================
  // Layer/PrimSpec conversion helpers
  // ======================================================================

  /// Convert a PrimSpec and its children recursively
  bool ConvertPrimSpecRecursive(const PrimSpec& primspec, const Path& parent_path, std::string* err);

  /// Convert a Property to Fields (handles Attribute, Relationship, Connection)
  bool ConvertPropertyToFields(const std::string& prop_name, const Property& prop,
                               crate::FieldValuePairVector& fields, std::string* err);

  /// Convert an Attribute to Fields
  bool ConvertAttributeToFields(const std::string& attr_name, const Attribute& attr,
                                crate::FieldValuePairVector& fields, std::string* err);

  /// Convert a Relationship to Fields
  bool ConvertRelationshipToFields(const std::string& rel_name, const Relationship& rel,
                                   crate::FieldValuePairVector& fields, std::string* err);

  /// Convert an Attribute Connection to Fields
  bool ConvertConnectionToFields(const std::string& conn_name, const Attribute& attr,
                                 crate::FieldValuePairVector& fields, std::string* err);

  // ======================================================================
  // Value encoding
  // ======================================================================

  /// Pack a CrateValue into ValueRep
  /// Returns the ValueRep and may write out-of-line data to file
  crate::ValueRep PackValue(const crate::CrateValue& value, std::string* err);

  /// Write a value to the value data section
  /// Returns the file offset where the value was written
  int64_t WriteValueData(const crate::CrateValue& value, std::string* err);

  /// Try to inline a value in ValueRep payload (optimization)
  bool TryInlineValue(const crate::CrateValue& value, crate::ValueRep* rep);

  // ======================================================================
  // Deduplication
  // ======================================================================

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
  std::fstream file_;  // Changed from ofstream to support read+write for bootstrap
  Options options_;

  bool is_open_ = false;
  bool is_finalized_ = false;

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
