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
constexpr size_t kDeferredArrayMaxBytes = size_t(1) << 30;  // 1 GiB
}  // namespace
#endif

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
  struct DrainGuard {
    std::unique_ptr<DeferredArrayScheduler>& scheduler;
    ~DrainGuard() { scheduler.reset(); }  // ~DeferredArrayScheduler drains
  } drain_guard{deferred_arrays_};
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
    if (!ParsePrim()) {
      return false;
    }
  }
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
