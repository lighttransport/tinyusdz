// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDA ASCII Parser implementation

#include "ascii-parser-internal.hh"
#include "../strfmt.hh"
#include "value-parser.hh"
#include "../../external/fast_float/include/fast_float/fast_float.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <system_error>
#if defined(TINYUSDZ_ENABLE_THREAD)
#include <thread>
#endif

#if defined(_WIN32)
#define TINYUSDZ_NEXT_HAVE_MMAP 0
#else
#define TINYUSDZ_NEXT_HAVE_MMAP 1
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace tinyusdz {
namespace next {

namespace {

using ProfileClock = std::chrono::steady_clock;

double ProfileMs(ProfileClock::duration d) {
  return std::chrono::duration<double, std::milli>(d).count();
}

class MappedFile {
 public:
  MappedFile() = default;
  ~MappedFile() { Close(); }
  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;

  bool Open(const char* filename, size_t max_file_size, std::string* err) {
#if TINYUSDZ_NEXT_HAVE_MMAP
    int fd = ::open(filename, O_RDONLY);
    if (fd < 0) {
      if (err) *err = "Failed to open file";
      return false;
    }
    struct stat st;
    if (::fstat(fd, &st) != 0) {
      ::close(fd);
      if (err) *err = "Failed to stat file";
      return false;
    }
    if (st.st_size < 0) {
      ::close(fd);
      if (err) *err = "Invalid file size";
      return false;
    }
    const size_t n = static_cast<size_t>(st.st_size);
    if (max_file_size > 0 && n > max_file_size) {
      ::close(fd);
      if (err) *err = "File size exceeds maximum allowed";
      return false;
    }
    if (n == 0) {
      fd_ = fd;
      size_ = 0;
      data_ = nullptr;
      return true;
    }
    // Plain demand paging: MAP_POPULATE was measured SLOWER here (the upfront
    // populate serializes on the main thread, while demand minor-faults spread
    // across the parse and overlap the array worker pool).
    void* p = ::mmap(nullptr, n, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED) {
      ::close(fd);
      if (err) *err = "Failed to mmap file";
      return false;
    }
    fd_ = fd;
    data_ = static_cast<const char*>(p);
    size_ = n;
    return true;
#else
    (void)filename;
    (void)max_file_size;
    if (err) *err = "mmap unavailable";
    return false;
#endif
  }

  void Close() {
#if TINYUSDZ_NEXT_HAVE_MMAP
    if (data_ && size_) {
      ::munmap(const_cast<char*>(data_), size_);
    }
    if (fd_ >= 0) {
      ::close(fd_);
    }
#endif
    data_ = nullptr;
    size_ = 0;
    fd_ = -1;
  }

  const char* data() const { return data_; }
  size_t size() const { return size_; }

 private:
  const char* data_ = nullptr;
  size_t size_ = 0;
  int fd_ = -1;
};

}  // namespace

bool IsNameToken(const Token& tok) {
  switch (tok.type) {
    case TokenType::Identifier:
    case TokenType::Def:
    case TokenType::Over:
    case TokenType::Class:
    case TokenType::True:
    case TokenType::False:
    case TokenType::None:
    case TokenType::TimeSamples:
    case TokenType::Custom:
    case TokenType::Uniform:
    case TokenType::Varying:
    case TokenType::Prepend:
    case TokenType::Append:
    case TokenType::Delete:
    case TokenType::Add:
    case TokenType::Reorder:
    case TokenType::Rel:
      return !tok.text.empty();
    default:
      return false;
  }
}

#if defined(TINYUSDZ_ENABLE_THREAD)
namespace {
// Parallel prim-subtree window. Blocks below the floor are cheaper to parse
// inline than to dispatch (fragment + task + stitch overhead); blocks above
// the ceiling are descended inline so their CHILDREN are dispatched
// individually (keeps worker granularity bounded and the drain tail short).
constexpr size_t kSubtreeMinBytes = size_t(16) << 10;   // 16 KiB
constexpr size_t kSubtreeMaxBytes = size_t(64) << 20;   // 64 MiB
}  // namespace

void AsciiParser::Impl::RunSubtreeParse(
    const ParseOptions& base_options,
    std::shared_ptr<DeferredArrayScheduler> arrays, SubtreeParseState* st,
    const char* data, size_t len, size_t start_line, size_t base_depth,
    const std::string& parent_path, SubtreeFragment* fragment) {
  USDAParseProfile local_profile;
  ParseOptions opts = base_options;
  opts.profile = base_options.profile ? &local_profile : nullptr;

  Impl sub(opts);
  sub.layer_ = std::make_unique<Layer>();
  sub.builder_ = std::make_unique<LayerBuilder>(*sub.layer_);
  if (!parent_path.empty()) {
    sub.builder_->set_path_prefix(parent_path);
  }
  sub.lexer_ = std::make_unique<Lexer>(data, len);
  sub.lexer_->num_threads = opts.num_threads;
  sub.lexer_->profile = opts.profile;
  sub.lexer_->set_source_location(start_line, 1);
  sub.deferred_arrays_ = std::move(arrays);
  sub.depth_ = base_depth;

  const bool ok = sub.ParsePrim();
  if (!ok || sub.HasErrors()) {
    std::lock_guard<std::mutex> lock(st->mu);
    if (!st->failed) {
      st->failed = true;
      if (!sub.errors_.empty()) {
        st->first_error = sub.errors_.front();
      } else {
        st->first_error =
            ParseError{start_line, 1, "Failed to parse prim subtree"};
      }
    }
  } else {
    fragment->layer = std::move(sub.layer_);
  }

  if (opts.profile) {
    const USDAParseProfile& p = local_profile;
    std::lock_guard<std::mutex> lock(st->mu);
    USDAParseProfile& m = st->merged_profile;
    m.prims += p.prims;
    m.properties += p.properties;
    m.time_samples += p.time_samples;
    m.arrays += p.arrays;
    m.array_bytes += p.array_bytes;
    m.simple_arrays += p.simple_arrays;
    m.numeric_arrays += p.numeric_arrays;
    m.numeric_array_bytes += p.numeric_array_bytes;
    m.numeric_array_scalars += p.numeric_array_scalars;
    m.parallel_arrays += p.parallel_arrays;
    m.parallel_array_bytes += p.parallel_array_bytes;
    m.parallel_array_scalars += p.parallel_array_scalars;
    m.parallel_array_fallbacks += p.parallel_array_fallbacks;
    m.deferred_arrays += p.deferred_arrays;
    m.deferred_array_bytes += p.deferred_array_bytes;
  }
}

bool AsciiParser::Impl::JoinSubtreeTasks() {
  if (!subtree_state_) return true;
  SubtreeParseState* st = subtree_state_.get();
  {
    std::unique_lock<std::mutex> lock(st->mu);
    st->cv.wait(lock, [st]() { return st->inflight == 0; });
  }
  if (options_.profile) {
    const USDAParseProfile& m = st->merged_profile;
    USDAParseProfile& p = *options_.profile;
    p.prims += m.prims;
    p.properties += m.properties;
    p.time_samples += m.time_samples;
    p.arrays += m.arrays;
    p.array_bytes += m.array_bytes;
    p.simple_arrays += m.simple_arrays;
    p.numeric_arrays += m.numeric_arrays;
    p.numeric_array_bytes += m.numeric_array_bytes;
    p.numeric_array_scalars += m.numeric_array_scalars;
    p.parallel_arrays += m.parallel_arrays;
    p.parallel_array_bytes += m.parallel_array_bytes;
    p.parallel_array_scalars += m.parallel_array_scalars;
    p.parallel_array_fallbacks += m.parallel_array_fallbacks;
    p.deferred_arrays += m.deferred_arrays;
    p.deferred_array_bytes += m.deferred_array_bytes;
    // Merge exactly once (JoinSubtreeTasks runs again on error paths).
    st->merged_profile = USDAParseProfile{};
  }
  if (st->failed) {
    errors_.push_back(st->first_error);
    st->failed = false;  // reported once
    return false;
  }
  // Stitch fragments in dispatch (= authored) order, then resolve the
  // placeholder child/root indices in one pass. Reserve the exact total once
  // so adoption never reallocates the (PrimSpec-move-expensive) main vector.
  size_t total_prims = layer_->prim_count();
  for (const auto& f : st->fragments) {
    if (f->layer) total_prims += f->layer->prim_count();
  }
  layer_->reserve(total_prims);
  std::vector<uint32_t> resolved(st->fragments.size(), UINT32_MAX);
  for (size_t i = 0; i < st->fragments.size(); i++) {
    if (st->fragments[i]->layer) {
      resolved[i] = layer_->adopt_fragment(std::move(*st->fragments[i]->layer));
      st->fragments[i]->layer.reset();
    }
  }
  layer_->resolve_pending_indices(resolved);
  st->fragments.clear();
  return true;
}

namespace {
// Deferral window. Below the floor, per-item bookkeeping beats the parse cost.
// Huge arrays are deferred too — one worker parses the whole array serially
// into the exactly-pre-sized payload while the main thread keeps lexing. That
// beats the synchronous intra-array parallel path (which blocks the main
// thread in a per-array join and pays worker-local growth plus a full
// concatenate copy) on scenes dominated by multi-MB arrays: on flattened
// Island (9.3GB, 73% of numeric bytes in >=4MiB arrays) it is the difference
// between the main thread waiting on joins and streaming ahead. The ceiling
// only bounds the drain-tail: a single array bigger than this would leave one
// worker parsing alone after lexing finishes, so it stays on the synchronous
// split path.
constexpr size_t kDeferredArrayMinBytes = 256;
constexpr size_t kDeferredArrayMaxBytes = size_t(2) << 30;  // 2 GiB
}  // namespace
#endif

bool AsciiParser::Impl::ParsePrimMaybeParallel() {
#if defined(TINYUSDZ_ENABLE_THREAD)
  // Variant content builds into a swapped-in variant builder; its prims must
  // not be dispatched (fragment stitching targets the main layer).
  if (subtree_state_ && variant_depth_ == 0) {
    SubtreeParseState* st = subtree_state_.get();
    bool already_failed;
    {
      std::lock_guard<std::mutex> lock(st->mu);
      already_failed = st->failed;
    }
    const char* block = nullptr;
    size_t len = 0;
    size_t line = 0;
    bool too_big = false;
    if (!already_failed &&
        lexer_->capture_prim_block(kSubtreeMinBytes, kSubtreeMaxBytes, &block,
                                   &len, &line, &too_big)) {
      const uint32_t frag_id = static_cast<uint32_t>(st->fragments.size());
      st->fragments.push_back(std::make_unique<SubtreeFragment>());
      SubtreeFragment* frag = st->fragments.back().get();

      // Placeholder in authored position (resolved after the join).
      std::string parent_path;
      if (PrimSpec* parent = builder_->current()) {
        parent->mutable_child_indices().push_back(Layer::kPendingIndexBit |
                                                  frag_id);
        parent_path = parent->path().str();
      } else {
        layer_->add_root_pending(frag_id);
      }

      bool run_inline = false;
      {
        std::lock_guard<std::mutex> lock(st->mu);
        if (st->inflight >= st->max_inflight) {
          run_inline = true;  // backpressure: bound pending fragment memory
        } else {
          st->inflight++;
        }
      }
      if (run_inline) {
        RunSubtreeParse(options_, deferred_arrays_, st, block, len, line,
                        depth_, parent_path, frag);
      } else {
        const ParseOptions opts = options_;
        std::shared_ptr<SubtreeParseState> stp = subtree_state_;
        std::shared_ptr<DeferredArrayScheduler> arrays = deferred_arrays_;
        const size_t base_depth = depth_;
        const bool submitted = SubmitPoolTask(
            options_.num_threads,
            [opts, arrays, stp, block, len, line, base_depth, parent_path,
             frag]() {
              RunSubtreeParse(opts, arrays, stp.get(), block, len, line,
                              base_depth, parent_path, frag);
              std::lock_guard<std::mutex> lock(stp->mu);
              stp->inflight--;
              if (stp->inflight == 0) stp->cv.notify_all();
            });
        if (!submitted) {
          RunSubtreeParse(options_, deferred_arrays_, st, block, len, line,
                          depth_, parent_path, frag);
          std::lock_guard<std::mutex> lock(st->mu);
          st->inflight--;
          if (st->inflight == 0) st->cv.notify_all();
        }
      }
      // Worker errors surface at the join barrier; keep the main thread
      // streaming (matches the sync parser's fail-at-that-point semantics
      // closely enough: the load still fails with the worker's error).
      return true;
    }
    // Not captured (tiny / too big / malformed): parse inline. too_big blocks
    // recurse here for their children.
  }
#endif
  return ParsePrim();
}

ParseResult AsciiParser::Impl::ParseArrayValueMaybeDeferred(TypeId type_id,
                                                            bool* out_deferred) {
  if (out_deferred) *out_deferred = false;
#if defined(TINYUSDZ_ENABLE_THREAD)
  if (deferred_arrays_ && !deferred_arrays_->failed() &&
      CanParseCapturedArrayValue(type_id) &&
      lexer_->peek().type == TokenType::OpenBracket) {
    const char* data = nullptr;
    size_t len = 0;
    bool simple = false;
    size_t commas = 0;
    if (!lexer_->capture_bracketed_literal(&data, &len, &simple, &commas)) {
      return ParseResult::Error(lexer_->error());
    }

    Value::ArrayScalarKind kind{};
    uint32_t comps = 0;
    // `commas >= 1` guarantees at least two scalars, which makes the
    // comma-count scalar prediction exact for every well-formed input (an
    // empty or single-element array can't be distinguished from whitespace by
    // counts alone — those parse synchronously, and are tiny anyway).
    if (simple && commas >= 1 && len >= kDeferredArrayMinBytes &&
        len < kDeferredArrayMaxBytes &&
        GetDeferredArrayInfo(type_id, &kind, &comps)) {
      const uint64_t scalars = static_cast<uint64_t>(commas) + 1;
      if ((scalars % comps) == 0 && scalars <= (uint64_t(1) << 30)) {
        Value::DeferredArrayFill fill;
        Value v = Value::MakeDeferredArray(
            type_id, kind, static_cast<uint32_t>(scalars / comps), &fill);
        DeferredArrayItem item;
        item.data = data;
        item.len = static_cast<uint32_t>(len);
        item.expected_scalars = static_cast<uint32_t>(scalars);
        item.type_id = type_id;
        item.fill = std::move(fill);
        deferred_arrays_->Enqueue(std::move(item));
        if (USDAParseProfile* profile = options_.profile) {
          profile->arrays++;
          profile->array_bytes += len;
          profile->simple_arrays++;
          profile->numeric_arrays++;
          profile->numeric_array_bytes += len;
          profile->numeric_array_scalars += scalars;
          profile->deferred_arrays++;
          profile->deferred_array_bytes += len;
        }
        if (out_deferred) *out_deferred = true;
        return ParseResult::Ok(std::move(v));
      }
    }
    // Captured but not deferrable: parse the span synchronously.
    return ParseCapturedArrayValue(data, len, simple, commas, type_id,
                                   lexer_->num_threads, options_.profile);
  }
#endif
  return ParseArrayValue(*lexer_, type_id);
}

bool AsciiParser::Impl::Parse(const char* data, size_t length) {
  const auto t_parse_start = ProfileClock::now();
  errors_.clear();
  warnings_.clear();
  depth_ = 0;
  if (options_.profile) {
    options_.profile->input_bytes += length;
  }

  if (options_.max_file_size > 0 && length > options_.max_file_size) {
    AddError("File size exceeds maximum allowed");
    return false;
  }

  // Create fresh layer and builder
  layer_ = std::make_unique<Layer>();
  builder_ = std::make_unique<LayerBuilder>(*layer_);

  lexer_ = std::make_unique<Lexer>(data, length);
  lexer_->num_threads = options_.num_threads;
  lexer_->profile = options_.profile;

#if defined(TINYUSDZ_ENABLE_THREAD)
  if (options_.async_arrays && options_.num_threads != 1) {
    deferred_arrays_ = DeferredArrayScheduler::Create(options_.num_threads);
  }
  // Deferred workers hold spans into `data` and write into committed payloads:
  // every exit from Parse (including early error returns) MUST join them
  // before the input buffer and the layer can die. The destructor's Drain is
  // idempotent, so the explicit success-path Drain below is unaffected.
  // Guard ORDER matters: subtree workers Enqueue into the array scheduler and
  // reference the input buffer, so the join guard (declared LAST, destroyed
  // FIRST) must run before the drain guard releases the scheduler.
  struct DrainGuard {
    std::shared_ptr<DeferredArrayScheduler>& scheduler;
    ~DrainGuard() { scheduler.reset(); }  // ~DeferredArrayScheduler drains
  } drain_guard{deferred_arrays_};

  if (options_.parallel_prims && options_.num_threads != 1) {
    // Requires the worker pool; probe with a no-op-free check by creating the
    // state only when the array scheduler (same pool) could be created, or
    // the pool answers a direct submit probe below on first dispatch.
    subtree_state_ = std::make_shared<SubtreeParseState>();
    int nt = options_.num_threads;
    if (nt <= 0) {
      nt = static_cast<int>(std::thread::hardware_concurrency());
      if (nt < 1) nt = 1;
      nt = std::min(nt, 8);
    }
    if (nt <= 1) {
      subtree_state_.reset();
    } else {
      subtree_state_->max_inflight = static_cast<size_t>(4 * nt);
    }
  }
  struct SubtreeJoinGuard {
    Impl* impl;
    ~SubtreeJoinGuard() {
      // Wait out in-flight subtree workers on EVERY exit path (they reference
      // the input buffer and fragment slots), then release the state.
      if (impl->subtree_state_) {
        SubtreeParseState* st = impl->subtree_state_.get();
        std::unique_lock<std::mutex> lock(st->mu);
        st->cv.wait(lock, [st]() { return st->inflight == 0; });
        lock.unlock();
        impl->subtree_state_.reset();
      }
    }
  } subtree_join_guard{this};
#endif

  // Parse stage metadata (header block)
  const auto t_meta_start = ProfileClock::now();
  if (!ParseStageMetadata()) {
    return false;
  }
  const auto t_meta_end = ProfileClock::now();

  // Parse root prims
  const auto t_prims_start = ProfileClock::now();
  while (!AtEnd()) {
    if (!ParsePrimMaybeParallel()) {
      return false;
    }
  }

#if defined(TINYUSDZ_ENABLE_THREAD)
  // Join + stitch parallel prim subtrees (placeholder indices resolved) before
  // anything reads the tree. A worker-side parse failure fails the load.
  if (!JoinSubtreeTasks()) {
    return false;
  }
#endif
  const auto t_prims_end = ProfileClock::now();

#if defined(TINYUSDZ_ENABLE_THREAD)
  // Join barrier: all deferred array payloads must be filled before the layer
  // is finalized/consumed. A worker-side parse failure fails the load, same as
  // the synchronous path.
  if (deferred_arrays_) {
    std::string deferred_error;
    if (!deferred_arrays_->Drain(&deferred_error)) {
      AddError(deferred_error);
      return false;
    }
  }
#endif

  // Finalize the layer
  const auto t_finalize_start = ProfileClock::now();
  builder_->finalize();
  const auto t_finalize_end = ProfileClock::now();

  // Create stage from layer
  stage_ = Stage();
  stage_.SetRootLayer(std::move(*layer_));

  lexer_.reset();
  builder_.reset();
  layer_.reset();

  if (options_.profile) {
    options_.profile->stage_metadata_ms += ProfileMs(t_meta_end - t_meta_start);
    options_.profile->prims_ms += ProfileMs(t_prims_end - t_prims_start);
    options_.profile->finalize_ms += ProfileMs(t_finalize_end - t_finalize_start);
    options_.profile->parser_ms += ProfileMs(ProfileClock::now() - t_parse_start);
  }

  return errors_.empty();
}

bool AsciiParser::Impl::ParseFile(const char* filename) {
  const auto t_open_start = ProfileClock::now();
  MappedFile mapped;
  std::string map_error;
  if (mapped.Open(filename, options_.max_file_size, &map_error)) {
    if (options_.profile) {
      options_.profile->file_open_ms += ProfileMs(ProfileClock::now() - t_open_start);
      options_.profile->used_mmap = true;
    }
    const char empty = '\0';
    return Parse(mapped.size() ? mapped.data() : &empty, mapped.size());
  }

  std::ifstream file(filename, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    AddError(std::string("Failed to open file: ") + filename);
    return false;
  }
  if (options_.profile) {
    options_.profile->file_open_ms += ProfileMs(ProfileClock::now() - t_open_start);
  }

  size_t size = static_cast<size_t>(file.tellg());
  file.seekg(0, std::ios::beg);
  if (options_.max_file_size > 0 && size > options_.max_file_size) {
    AddError("File size exceeds maximum allowed");
    return false;
  }

  // Default-init (NOT value-init) the buffer: `new char[]` leaves the bytes
  // uninitialized, so we skip zero-filling hundreds of MB we immediately
  // overwrite with file.read (the zero-fill was ~8% of a big-file parse).
  std::unique_ptr<char[]> content(new char[size ? size : 1]);
  const auto t_read_start = ProfileClock::now();
  if (size && !file.read(content.get(), static_cast<std::streamsize>(size))) {
    AddError("Failed to read file contents");
    return false;
  }
  if (options_.profile) {
    options_.profile->file_read_ms += ProfileMs(ProfileClock::now() - t_read_start);
  }

  return Parse(content.get(), size);
}

// Read one composition-arc reference into canonical "@asset@</prim>" /
// "</prim>" form (the lexer yields @asset@ as a String without '@' and
// </prim> as a PathRef without '<>'). Peeks before consuming so a missing
// optional token does not eat the next one. Returns false if no arc token.
bool AsciiParser::Impl::ReadArcRef(std::string* out) {
  std::string ref;
  if (Check(TokenType::String)) {
    std::string asset;
    lexer_->expect(TokenType::String, asset);
    ref = "@" + asset + "@";
    if (Check(TokenType::PathRef)) {
      std::string pr;
      lexer_->expect(TokenType::PathRef, pr);
      ref += "<" + pr + ">";
    }
  } else if (Check(TokenType::PathRef)) {
    std::string pr;
    lexer_->expect(TokenType::PathRef, pr);
    ref = "<" + pr + ">";
  } else {
    return false;
  }
  // Optional per-arc layer offset: `(offset = N; scale = M)`. Encoded into the
  // canonical ref as `?layerOffset=offset:scale` (Compositor::ParseReference
  // decodes it); composition composes it through the arc chain and bakes it
  // into time-sample times.
  if (Check(TokenType::OpenParen)) {
    Match(TokenType::OpenParen);
    double off = 0.0, scl = 1.0;
    while (!Check(TokenType::CloseParen) && !AtEnd()) {
      if (Check(TokenType::Identifier)) {
        std::string k;
        lexer_->expect(TokenType::Identifier, k);
        Match(TokenType::Equals);
        if (Check(TokenType::Number)) {
          std::string num;
          lexer_->expect(TokenType::Number, num);
          // Freestanding double parse (fast_float; no libc strtod).
          double v = 0.0;
          {
            const char* b = num.c_str();
            const char* e = b + num.size();
            auto r = fast_float::from_chars(b, e, v);
            if (!(r.ec == std::errc{} && r.ptr == e) && b < e && *b == '+') {
              fast_float::from_chars(b + 1, e, v);
            }
          }
          if (k == "offset") off = v;
          else if (k == "scale") scl = v;
        }
      } else {
        lexer_->consume();  // skip unexpected token (avoid spinning)
      }
      Match(TokenType::Semicolon);
    }
    Match(TokenType::CloseParen);
    if (off != 0.0 || scl != 1.0) {
      ref += "?layerOffset=" + std::to_string(off) + ":" + std::to_string(scl);
    }
  }
  *out = ref;
  return true;
}

bool AsciiParser::Impl::ParseMetadataBlock() {
  if (!Match(TokenType::OpenParen)) {
    return false;
  }

  PrimSpec* prim = builder_->current();
  if (!prim) {
    AddError("No current prim for metadata");
    return false;
  }

  // Read an arc value that may be a bracketed list or a single value.
  // List-op qualifier applied to an arc list (prepend/append/delete/explicit).
  enum class ArcQual { Explicit, Prepend, Append, Delete, Reorder };

  enum class ArcField { References, Payloads, Inherits, Specializes };
  auto SelectArc = [](PrimSpecMeta& meta,
                      ArcField f) -> std::vector<std::string>& {
    switch (f) {
      case ArcField::References: return meta.references;
      case ArcField::Payloads: return meta.payloads;
      case ArcField::Inherits: return meta.inherits;
      default: return meta.specializes;
    }
  };
  auto SelectEdit = [](ArcListOpEdits& e, ArcField f) -> ArcEdit& {
    switch (f) {
      case ArcField::References: return e.references;
      case ArcField::Payloads: return e.payloads;
      case ArcField::Inherits: return e.inherits;
      default: return e.specializes;
    }
  };

  // Read the bracketed (or single) arc references, then merge into the inline
  // arc vector honoring the list-op qualifier (explicit/bare replaces, prepend
  // front, append/reorder back, delete removes) -- the within-spec effective
  // list. ALSO record the raw qualifier into the spec's ArcEdit (Phase 7 S5),
  // which cross-layer composition (apply_list_ops) and the writer consume. Even
  // a bare empty list (`references = []`) is authored and must replace weaker
  // opinions.
  auto ReadArcList = [this, &SelectArc, &SelectEdit](
                         PrimSpecMeta& meta, ArcField field, ArcQual qual) {
    std::vector<std::string> items;
    if (Match(TokenType::OpenBracket)) {
      while (!Check(TokenType::CloseBracket) && !AtEnd()) {
        std::string ref;
        if (!ReadArcRef(&ref)) break;
        items.push_back(ref);
        Match(TokenType::Comma);
      }
      Match(TokenType::CloseBracket);
    } else {
      std::string ref;
      if (ReadArcRef(&ref)) items.push_back(ref);
    }

    // Record the list-op edit first (copies items for non-bare qualifiers).
    ArcEdit& e = SelectEdit(meta.ensure_arc_edits(), field);
    switch (qual) {
      case ArcQual::Explicit:
        e = ArcEdit();  // explicit replaces: is_explicit=true, lists cleared
        e.authored = true;
        break;
      case ArcQual::Prepend:
        e.authored = true;
        e.is_explicit = false;
        e.prepended.insert(e.prepended.end(), items.begin(), items.end());
        break;
      case ArcQual::Append:
        e.authored = true;
        e.is_explicit = false;
        e.appended.insert(e.appended.end(), items.begin(), items.end());
        break;
      case ArcQual::Delete:
        e.authored = true;
        e.is_explicit = false;
        e.deleted.insert(e.deleted.end(), items.begin(), items.end());
        break;
      case ArcQual::Reorder:
        e.authored = true;
        e.is_explicit = false;
        e.ordered.insert(e.ordered.end(), items.begin(), items.end());
        break;
    }

    std::vector<std::string>* target = &SelectArc(meta, field);
    switch (qual) {
      case ArcQual::Explicit:
        *target = std::move(items);
        break;
      case ArcQual::Prepend:
        target->insert(target->begin(), items.begin(), items.end());
        break;
      case ArcQual::Append:
      case ArcQual::Reorder:
        target->insert(target->end(), items.begin(), items.end());
        break;
      case ArcQual::Delete:
        for (const std::string& d : items) {
          target->erase(std::remove(target->begin(), target->end(), d),
                        target->end());
        }
        break;
    }
  };

  while (!Check(TokenType::CloseParen) && !AtEnd()) {
    // A bare (often triple-quoted) string is the prim documentation —
    // USD shorthand for `doc = "..."`.
    if (Check(TokenType::String)) {
      std::string d;
      lexer_->expect(TokenType::String, d);
      prim->meta().doc() = d;
      continue;
    }
    // Optional list-op qualifier keyword (prepend/append/delete/reorder)
    // precedes the real key, e.g. `prepend references = [...]`.
    ArcQual arc_qual = ArcQual::Explicit;
    if (Match(TokenType::Prepend)) {
      arc_qual = ArcQual::Prepend;
    } else if (Match(TokenType::Append) || Match(TokenType::Add)) {
      // `add` is USD's legacy list-op; treat it as append.
      arc_qual = ArcQual::Append;
    } else if (Match(TokenType::Delete)) {
      arc_qual = ArcQual::Delete;
    } else if (Match(TokenType::Reorder)) {
      arc_qual = ArcQual::Reorder;
    }

    std::string key;
    if (!lexer_->expect(TokenType::Identifier, key)) {
      AddError("Expected metadata key");
      return false;
    }

    if (!Match(TokenType::Equals)) {
      AddError("Expected '=' after metadata key");
      return false;
    }

    // Handle known prim metadata
    if (key == "active") {
      ParseResult result = ParseValue(*lexer_, TypeId::Bool);
      if (result.success && result.value.as_bool()) {
        builder_->set_active(*result.value.as_bool());
      }
    } else if (key == "hidden") {
      ParseResult result = ParseValue(*lexer_, TypeId::Bool);
      if (result.success && result.value.as_bool()) {
        builder_->set_hidden(*result.value.as_bool());
      }
    } else if (key == "doc" || key == "documentation") {
      std::string doc;
      if (lexer_->expect(TokenType::String, doc)) {
        prim->meta().doc() = doc;
      }
    } else if (key == "comment") {
      std::string v;
      if (lexer_->expect(TokenType::String, v)) {
        prim->meta().comment() = v;
      }
    } else if (key == "kind") {
      std::string v;
      if (lexer_->expect(TokenType::String, v)) {
        prim->meta().kind() = v;
      }
    } else if (key == "displayName") {
      std::string v;
      if (lexer_->expect(TokenType::String, v)) {
        prim->meta().displayName() = v;
      }
    } else if (key == "instanceable") {
      ParseResult result = ParseValue(*lexer_, TypeId::Bool);
      if (result.success && result.value.as_bool()) {
        prim->meta().instanceable = *result.value.as_bool();
      }
    } else if (key == "apiSchemas") {
      if (Match(TokenType::OpenBracket)) {
        while (!Check(TokenType::CloseBracket) && !AtEnd()) {
          // Schema names are authored as quoted strings (`"PhysicsRigidBodyAPI"`);
          // accept bare identifiers too. expect() consumes even on mismatch, so
          // Check the token type first.
          std::string schema;
          if (Check(TokenType::String)
                  ? lexer_->expect(TokenType::String, schema)
                  : lexer_->expect(TokenType::Identifier, schema)) {
            prim->meta().apiSchemas().push_back(schema);
          }
          Match(TokenType::Comma);
        }
        Match(TokenType::CloseBracket);
      }
    } else if (key == "references") {
      ReadArcList(prim->meta(), ArcField::References, arc_qual);
    } else if (key == "payload") {
      ReadArcList(prim->meta(), ArcField::Payloads, arc_qual);
    } else if (key == "inherits") {
      ReadArcList(prim->meta(), ArcField::Inherits, arc_qual);
    } else if (key == "specializes") {
      ReadArcList(prim->meta(), ArcField::Specializes, arc_qual);
    } else if (key == "customData") {
      ParseResult r = ParseDict(*lexer_);
      if (r.success) prim->meta().customData() = std::move(r.value);
    } else if (key == "assetInfo") {
      ParseResult r = ParseDict(*lexer_);
      if (r.success) prim->meta().assetInfo() = std::move(r.value);
    } else if (key == "sdrMetadata") {
      ParseResult r = ParseDict(*lexer_);
      if (r.success) prim->meta().sdrMetadata() = std::move(r.value);
    } else if (key == "clips") {
      ParseResult r = ParseDict(*lexer_);
      if (r.success) prim->meta().clips() = std::move(r.value);
    } else if (key == "variantSets") {
      // variantSets = ["setName1", "setName2"]  OR a single bare string
      // (`add variantSets = "shadingVariant"`). Declarations only; no body here.
      auto add_vs = [&](const std::string& vs_name) {
        for (const auto& vs : prim->meta().variantSets()) {
          if (vs.name == vs_name) return;  // already declared
        }
        prim->meta().variantSets().emplace_back();
        prim->meta().variantSets().back().name = vs_name;
      };
      if (Match(TokenType::OpenBracket)) {
        while (!Check(TokenType::CloseBracket) && !AtEnd()) {
          // expect() consumes even on mismatch, so dispatch on the peeked type
          // (the old `expect(String) || expect(Identifier)` dropped identifier
          // names by consuming them in the failed String branch).
          std::string vs_name;
          if (Check(TokenType::String)
                  ? lexer_->expect(TokenType::String, vs_name)
                  : lexer_->expect(TokenType::Identifier, vs_name)) {
            add_vs(vs_name);
          }
          Match(TokenType::Comma);
        }
        Match(TokenType::CloseBracket);
      } else {
        std::string vs_name;
        if (Check(TokenType::String)
                ? lexer_->expect(TokenType::String, vs_name)
                : lexer_->expect(TokenType::Identifier, vs_name)) {
          add_vs(vs_name);
        }
      }
    } else if (key == "variants" || key == "variantSelection") {
      // variants = { string set = "selection"  string set2 = "sel2" }. USD
      // supports a selection per set; record them all in variantSelections()
      // (keeping the legacy single field set to the first for back-compat).
      if (Match(TokenType::OpenBrace)) {
        while (!Check(TokenType::CloseBrace) && !AtEnd()) {
          // Optional leading type name ("string"); the key may be a quoted
          // string or a bare identifier.
          std::string set_name;
          if (Check(TokenType::String)) {
            lexer_->expect(TokenType::String, set_name);
          } else if (Check(TokenType::Identifier)) {
            std::string first;
            lexer_->expect(TokenType::Identifier, first);
            // `string set = ...` -> first is the type name, read the real key.
            if (!Check(TokenType::Equals) &&
                (Check(TokenType::Identifier) || Check(TokenType::String))) {
              if (Check(TokenType::String)) {
                lexer_->expect(TokenType::String, set_name);
              } else {
                lexer_->expect(TokenType::Identifier, set_name);
              }
            } else {
              set_name = first;
            }
          } else {
            break;
          }
          if (!Match(TokenType::Equals)) break;
          std::string sel_name;
          if (!lexer_->expect(TokenType::String, sel_name)) break;
          prim->meta().variantSelections().emplace_back(set_name, sel_name);
          if (prim->meta().variantSelection.empty()) {
            prim->meta().variantSelection = set_name + "=" + sel_name;
          }
          Match(TokenType::Comma);
        }
        Match(TokenType::CloseBrace);
      }
    } else {
      // Generic metadata may be a dictionary/list; skip it structurally.
      SkipValueLike();
      AddWarning("Unknown prim metadata: " + key);
    }
  }

  return Match(TokenType::CloseParen);
}

// ============================================================
// AsciiParser public interface
// ============================================================

AsciiParser::AsciiParser(const ParseOptions& options)
    : impl_(std::make_unique<Impl>(options)) {}

AsciiParser::~AsciiParser() = default;

AsciiParser::AsciiParser(AsciiParser&&) noexcept = default;
AsciiParser& AsciiParser::operator=(AsciiParser&&) noexcept = default;

bool AsciiParser::Parse(const char* data, size_t length) {
  return impl_->Parse(data, length);
}

bool AsciiParser::ParseFile(const char* filename) {
  return impl_->ParseFile(filename);
}

Stage AsciiParser::TakeStage() {
  return impl_->TakeStage();
}

const std::vector<ParseError>& AsciiParser::GetErrors() const {
  return impl_->GetErrors();
}

bool AsciiParser::HasErrors() const {
  return impl_->HasErrors();
}

const std::vector<std::string>& AsciiParser::GetWarnings() const {
  return impl_->GetWarnings();
}

}  // namespace next
}  // namespace tinyusdz
