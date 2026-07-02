// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Value Parser
// Parses USD values from tokenized input

#pragma once

#include "../types/value.hh"
#include <memory>
#include <string>

namespace tinyusdz {
namespace next {

class Lexer;
struct USDAParseProfile;

/// Result of a parse operation
struct ParseResult {
  bool success = false;
  Value value;
  std::string error;

  /// Construct success result
  static ParseResult Ok(Value v) {
    ParseResult r;
    r.success = true;
    r.value = std::move(v);
    return r;
  }

  /// Construct error result
  static ParseResult Error(const std::string& msg) {
    ParseResult r;
    r.success = false;
    r.error = msg;
    return r;
  }
};

/// Parse a value of the expected type from the lexer
/// The lexer should be positioned at the start of the value
ParseResult ParseValue(Lexer& lexer, TypeId expected_type);

/// Parse an array value of the expected element type
/// The lexer should be positioned at the opening bracket
ParseResult ParseArrayValue(Lexer& lexer, TypeId element_type);

/// True when ParseCapturedArrayValue can parse this array element type.
bool CanParseCapturedArrayValue(TypeId element_type);

/// Parse a bracketed array span previously captured by Lexer::capture_bracketed_literal.
/// The span must include the outer '[' and ']'. `comma_count` is the capture's
/// comma tally (0 = unknown); for a simple array it predicts the scalar count
/// (scalars = commas + 1) and lets the parser pre-size its output vector.
ParseResult ParseCapturedArrayValue(const char* data, size_t len, bool simple,
                                    size_t comma_count, TypeId element_type,
                                    int num_threads, USDAParseProfile* profile);

/// Storage mapping for a deferrable numeric array element type: the flat
/// payload scalar kind and scalars-per-element. Returns false for types the
/// deferred path does not handle (bool/token/string/matrix/...).
bool GetDeferredArrayInfo(TypeId element_type, Value::ArrayScalarKind* out_kind,
                          uint32_t* out_comps);

#if defined(TINYUSDZ_ENABLE_THREAD)

/// One captured simple numeric array awaiting deferred (worker-pool) parsing.
/// `data` points into the parser's input buffer (alive until the parse's drain
/// barrier); `fill` is the pre-committed Value's payload (see
/// Value::MakeDeferredArray).
struct DeferredArrayItem {
  const char* data = nullptr;     // span incl. the outer '[' and ']'
  uint32_t len = 0;
  uint32_t expected_scalars = 0;  // capture comma count + 1 (exact when valid)
  TypeId type_id = TypeId::Invalid;
  Value::DeferredArrayFill fill;
};

/// Batches deferred array parses onto the shared USDA value worker pool so
/// numeric conversion overlaps the main thread's lexing. Per-array tasks are
/// far too fine (millions of ~3KB arrays); items are accumulated into
/// multi-hundred-KB batches before submission. Single-threaded producer
/// (the parser main thread); Drain() is the join barrier.
class DeferredArrayScheduler {
 public:
  /// Returns nullptr when the worker pool is unavailable (num_threads == 1 or
  /// no hardware parallelism).
  static std::unique_ptr<DeferredArrayScheduler> Create(int num_threads);
  ~DeferredArrayScheduler();

  DeferredArrayScheduler(const DeferredArrayScheduler&) = delete;
  DeferredArrayScheduler& operator=(const DeferredArrayScheduler&) = delete;

  void Enqueue(DeferredArrayItem item);

  /// Submit any partial batch and wait for all in-flight batches to finish.
  /// Returns false (with *error set) if any deferred array failed to parse.
  /// Idempotent.
  bool Drain(std::string* error);

  /// Cheap failure probe so the producer can stop deferring early.
  bool failed() const;

 private:
  explicit DeferredArrayScheduler(int num_threads);
  void SubmitBatch();

  struct Shared;
  std::shared_ptr<Shared> shared_;
  int num_threads_ = 0;
  size_t max_inflight_ = 0;
  std::vector<DeferredArrayItem> current_;
  size_t current_bytes_ = 0;
};

#endif  // TINYUSDZ_ENABLE_THREAD

/// Parse a generic value, inferring the type from syntax
/// Returns the value and sets type_id to the inferred type
ParseResult ParseGenericValue(Lexer& lexer, TypeId& out_type);

/// Parse a USD dictionary `{ [type] key = value ... }` into a Dictionary Value.
/// The lexer should be positioned at the opening brace. Nested dictionaries and
/// array-valued entries are supported.
ParseResult ParseDict(Lexer& lexer);

/// Get TypeId from a USD type name string
/// Handles both simple types ("float") and array types ("float[]")
TypeId ParseTypeName(const std::string& type_name, bool& is_array);

}  // namespace next
}  // namespace tinyusdz
