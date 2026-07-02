// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - private USDA ASCII parser implementation details.

#pragma once

#include "ascii-parser.hh"
#include "lexer.hh"
#include "value-parser.hh"
#include "../layer/layer.hh"

#include <memory>
#include <string>
#include <vector>
#if defined(TINYUSDZ_ENABLE_THREAD)
#include <condition_variable>
#include <mutex>
#endif

namespace tinyusdz {
namespace next {

#if defined(TINYUSDZ_ENABLE_THREAD)
/// One dispatched prim subtree, parsed by a worker into its own Layer
/// fragment (exactly one root prim) and stitched into the main layer after
/// the join. Object address is stable (owned via unique_ptr in the state's
/// fragment list) so workers write through a raw pointer.
struct SubtreeFragment {
  std::unique_ptr<Layer> layer;
};

/// Shared state of the parallel prim-subtree parse; owned by the root parser.
/// `fragments` is mutated by the main thread only (dispatch order = authored
/// order = placeholder resolution order); workers touch only their own
/// SubtreeFragment plus the counters under `mu`.
struct SubtreeParseState {
  std::mutex mu;
  std::condition_variable cv;
  size_t inflight = 0;
  size_t max_inflight = 0;
  bool failed = false;
  ParseError first_error;
  USDAParseProfile merged_profile;  // workers accumulate under mu
  std::vector<std::unique_ptr<SubtreeFragment>> fragments;
};
#endif

class AsciiParser::Impl {
public:
  explicit Impl(const ParseOptions& options) : options_(options) {}

  bool Parse(const char* data, size_t length);
  bool ParseFile(const char* filename);

  Stage TakeStage() { return std::move(stage_); }
  const std::vector<ParseError>& GetErrors() const { return errors_; }
  bool HasErrors() const { return !errors_.empty(); }
  const std::vector<std::string>& GetWarnings() const { return warnings_; }

private:
  ParseOptions options_;
  Stage stage_;
  std::vector<ParseError> errors_;
  std::vector<std::string> warnings_;

  // Parsing state
  std::unique_ptr<Lexer> lexer_;
  std::unique_ptr<Layer> layer_;
  std::unique_ptr<LayerBuilder> builder_;
  size_t depth_ = 0;
  // Nonzero while parsing prims inside a variant block (builder_ is swapped to
  // a variant-content builder there — subtree dispatch must stay off).
  size_t variant_depth_ = 0;
#if defined(TINYUSDZ_ENABLE_THREAD)
  // Active only with ParseOptions::async_arrays: batches captured simple
  // numeric arrays onto the value worker pool; drained (join barrier) before
  // finalize and on every Parse() exit path (the spans die with the input).
  // Shared: parallel prim-subtree sub-parsers enqueue into the root's
  // scheduler (Enqueue is thread-safe).
  std::shared_ptr<DeferredArrayScheduler> deferred_arrays_;
  // Active only with ParseOptions::parallel_prims on the ROOT parser: prim
  // blocks in a size window are captured and parsed by pool workers into
  // Layer fragments, stitched after JoinSubtreeTasks(). Null in sub-parsers.
  std::shared_ptr<SubtreeParseState> subtree_state_;
  // Parse one captured prim block into `fragment` (worker or inline).
  static void RunSubtreeParse(const ParseOptions& base_options,
                              std::shared_ptr<DeferredArrayScheduler> arrays,
                              SubtreeParseState* st, const char* data,
                              size_t len, size_t start_line, size_t base_depth,
                              const std::string& parent_path,
                              SubtreeFragment* fragment);
  // Wait for all dispatched subtrees, merge profiles/errors, stitch fragments.
  bool JoinSubtreeTasks();
#endif
  // ParsePrim, or capture the block and dispatch it to a worker when the
  // parallel subtree parse is active and the block is in the size window.
  bool ParsePrimMaybeParallel();

  // Parsing methods
  bool ParseStageMetadata();
  bool ParsePrim();
  bool ParsePrimContents();
  bool ParseAttribute();
  bool ParseRelationship(PrimSpec::RelationshipListOp op =
                         PrimSpec::RelationshipListOp::Append);
  bool ParseMetadataBlock();
  bool ParseTimeSamples(const std::string& prop_name, TypeId type_id,
                        bool is_array);
  // Array-value parse for attribute defaults and timeSamples: captures the
  // bracketed span and either hands it to the deferred-array scheduler
  // (placeholder Value committed now, payload filled by a worker before the
  // drain barrier; *out_deferred=true) or parses it synchronously.
  ParseResult ParseArrayValueMaybeDeferred(TypeId type_id, bool* out_deferred);
  bool ParseVariantSetBody(const std::string& variant_set_name);
  bool ParseVariantSetBodyInto(const std::string& variant_set_name,
                               std::vector<VariantSetData>& target, int depth);
  bool ParseVariantOption(VariantData* out, int depth);
  // Read one composition-arc reference into canonical "@asset@</prim>" /
  // "</prim>" form (with an optional `?layerOffset=off:scale` suffix). Shared by
  // prim-metadata arcs and variant-option arcs. Returns false if no arc token.
  bool ReadArcRef(std::string* out);
  bool ParseNamespacedName(std::string* out, const char* what);
  bool SkipBalancedBlock(TokenType open, TokenType close, size_t depth = 0);
  bool SkipValueLike();
  void SkipPropertyMetadata();
  void ParsePropertyMetadata(const std::string& prop_name);

  void AddError(const std::string& message);
  void AddWarning(const std::string& message);

  // Token helpers
  bool Match(TokenType type);
  bool Check(TokenType type);
  bool AtEnd();
};

bool IsNameToken(const Token& tok);

}  // namespace next
}  // namespace tinyusdz
