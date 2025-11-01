// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Experimental USDC (Crate) File Writer
// Bare framework for writing USD Layer/PrimSpec to binary Crate format
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <fstream>

// TinyUSDZ crate format definitions
#include "../../../src/crate-format.hh"

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
  std::ofstream file_;
  Options options_;

  bool is_open_ = false;
  bool is_finalized_ = false;

  // Deduplication tables
  std::unordered_map<std::string, crate::TokenIndex> token_to_index_;
  std::vector<std::string> tokens_;  // Index -> token string

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

  // Phase 5: TimeSamples array deduplication
  // Maps array content hash to file offset where it was written
  // Only used for numeric arrays in TimeSamples
  struct ArrayHash {
    std::size_t operator()(const std::vector<char>& v) const {
      std::size_t hash = 0;
      for (char c : v) {
        hash = hash * 31 + static_cast<std::size_t>(c);
      }
      return hash;
    }
  };
  std::unordered_map<std::vector<char>, int64_t, ArrayHash> array_dedup_map_;
};

} // namespace experimental
} // namespace tinyusdz
