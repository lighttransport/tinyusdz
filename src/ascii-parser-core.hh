// SPDX-License-Identifier: Apache 2.0
// Copyright 2021-2025 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Refactored core USD ASCII parser with modular design
#pragma once

#include <memory>
#include <functional>
#include <string>
#include <vector>

#include "stream-reader.hh"
#include "prim-types.hh"
#include "composition.hh"
#include "tinyusdz.hh"

// Modular components
#include "ascii-lexer.hh"
#include "ascii-expression-parser.hh"
#include "ascii-type-parser.hh"
#include "ascii-property-parser.hh"
#include "ascii-error-handling.hh"

namespace tinyusdz {
namespace ascii {

///
/// Progress callback function type.
///
using ProgressCallback = std::function<bool(float progress, void *userptr)>;

///
/// Parser configuration options.
///
struct AsciiParserConfig {
  bool allow_unknown_prim{true};
  bool allow_unknown_apiSchema{true};
  bool strict_allowedToken_check{false};
  size_t max_memory_limit{2ULL * 1024 * 1024 * 1024};  // 2GB default
  size_t max_errors{100};
  bool enable_error_recovery{true};
};

///
/// Refactored USD ASCII parser with modular architecture.
/// This parser coordinates multiple specialized modules for different parsing tasks.
///
class AsciiParserCore {
 public:
  // Stage metadata structure (temporary, should match existing code)
  struct StageMetas {
    std::vector<value::AssetPath> subLayers;
    value::token defaultPrim;
    value::StringData doc;
    nonstd::optional<Axis> upAxis;
    nonstd::optional<double> metersPerUnit;
    nonstd::optional<double> kilogramsPerUnit;
    nonstd::optional<double> timeCodesPerSecond;
    nonstd::optional<double> startTimeCode;
    nonstd::optional<double> endTimeCode;
    nonstd::optional<double> framesPerSecond;
    nonstd::optional<bool> autoPlay;
    nonstd::optional<value::token> playbackMode;
    std::map<std::string, MetaVariable> customLayerData;
    value::StringData comment;
  };

  AsciiParserCore(StreamReader* sr, const AsciiParserConfig& config = AsciiParserConfig());
  ~AsciiParserCore();

  // Main parsing interface
  bool CheckHeader();
  bool Parse(const uint32_t load_states, tinyusdz::Layer* layer);
  
  // Progress monitoring
  void SetProgressCallback(ProgressCallback callback, void* userptr = nullptr);
  
  // Error handling
  std::string GetError() const;
  std::string GetWarning() const;
  bool HasErrors() const;
  bool HasWarnings() const;
  
  // Memory usage
  size_t GetMemoryUsage() const;
  
  // Parse specific elements (for testing/debugging)
  bool ParseBlock(const Specifier spec, const int64_t primIdx,
                 const int64_t parentPrimIdx, int level,
                 bool is_root, const std::string& prim_name,
                 const PrimType prim_type);
  
  bool ParseVariantSet(const std::string& variantSetName,
                      const std::vector<std::string>& variantNames,
                      std::map<std::string, std::vector<std::pair<ListEditQual, PrimSpec>>>* variantPrimSpecDict);
  
  bool ParseMagicHeader();
  bool ParseStageMetas();

  // Access to modules (for advanced usage)
  AsciiLexer* GetLexer() { return lexer_.get(); }
  AsciiTypeParser* GetTypeParser() { return type_parser_.get(); }
  AsciiExpressionParser* GetExpressionParser() { return expr_parser_.get(); }
  AsciiPropertyParser* GetPropertyParser() { return prop_parser_.get(); }
  AsciiErrorHandler* GetErrorHandler() { return error_handler_.get(); }

 private:
  // Modular components
  std::unique_ptr<AsciiLexer> lexer_;
  std::unique_ptr<AsciiTypeParser> type_parser_;
  std::unique_ptr<AsciiExpressionParser> expr_parser_;
  std::unique_ptr<AsciiPropertyParser> prop_parser_;
  std::unique_ptr<AsciiErrorHandler> error_handler_;
  
  // Core data
  StreamReader* _sr;
  AsciiParserConfig config_;
  
  // Progress tracking
  ProgressCallback progress_callback_;
  void* progress_userptr_{nullptr};
  
  // Parsing state
  std::vector<PrimSpec> prim_specs_;
  StageMetas stage_metas_;
  std::map<std::string, int64_t> prim_name_to_index_;
  
  // Memory tracking
  size_t memory_usage_{0};
  
  // Private implementation
  class Impl;
  std::unique_ptr<Impl> impl_;
  
  // Module initialization
  void InitializeModules();
  
  // Coordination methods
  bool ParsePrimSpec(PrimSpec* spec);
  bool ParsePrimBody(const std::string& prim_name, PrimSpec* spec);
  bool ReportProgress(float progress);
  
  // Error coordination
  void PropagateErrors();
  bool CheckErrorLimit();
};

// Backward compatibility typedef
using AsciiParser = AsciiParserCore;

}  // namespace ascii
}  // namespace tinyusdz