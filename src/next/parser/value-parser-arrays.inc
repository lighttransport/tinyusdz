// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - array value parsing helpers and optimized bracket-literal parser.
// Included inside namespace tinyusdz::next.

// Helper to check if type stores float3-like data
bool IsFloat3Like(TypeId type) {
  return type == TypeId::Float3 || type == TypeId::Half3 ||
         type == TypeId::Point3h || type == TypeId::Point3f ||
         type == TypeId::Vector3h || type == TypeId::Vector3f ||
         type == TypeId::Normal3h || type == TypeId::Normal3f ||
         type == TypeId::Color3h || type == TypeId::Color3f ||
         type == TypeId::Texcoord3h || type == TypeId::Texcoord3f;
}
// Helper to check if type stores float2-like data
bool IsFloat2Like(TypeId type) {
  return type == TypeId::Float2 || type == TypeId::Half2 ||
         type == TypeId::Texcoord2h || type == TypeId::Texcoord2f;
}

// Helper to check if type stores float4-like data
bool IsFloat4Like(TypeId type) {
  return type == TypeId::Float4 || type == TypeId::Half4 ||
         type == TypeId::Quath || type == TypeId::Quatf ||
         type == TypeId::Color4h || type == TypeId::Color4f;
}

bool IsDouble2Like(TypeId type) {
  return type == TypeId::Double2 || type == TypeId::Texcoord2d;
}

bool IsDouble3Like(TypeId type) {
  return type == TypeId::Double3 || type == TypeId::Point3d ||
         type == TypeId::Vector3d || type == TypeId::Normal3d ||
         type == TypeId::Color3d || type == TypeId::Texcoord3d;
}

bool IsDouble4Like(TypeId type) {
  return type == TypeId::Double4 || type == TypeId::Quatd ||
         type == TypeId::Color4d;
}

bool SliceIsIdentifierStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool SliceIsIdentifierContinue(char c) {
  return SliceIsIdentifierStart(c) || (c >= '0' && c <= '9');
}

struct SliceParser {
  const char* p{nullptr};
  const char* end{nullptr};

  void skip_ws() {
    while (p < end) {
      if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        ++p;
      } else if (*p == '#') {
        while (p < end && *p != '\n') ++p;
      } else {
        break;
      }
    }
  }

  bool consume(char c) {
    skip_ws();
    if (p >= end || *p != c) return false;
    ++p;
    return true;
  }

  bool maybe_consume(char c) {
    skip_ws();
    if (p < end && *p == c) {
      ++p;
      return true;
    }
    return false;
  }

  bool at_array_end() {
    skip_ws();
    return p < end && *p == ']';
  }

  bool finish_array() {
    if (!consume(']')) return false;
    skip_ws();
    return p == end;
  }

  bool parse_float(float* out) {
    skip_ws();
    auto r = fast_float::from_chars(p, end, *out);
    if (r.ec == std::errc{} && r.ptr != p) { p = r.ptr; return true; }
    // Accept a leading '+' (fast_float's `general` format rejects it), to match
    // the scalar FastFloatParse path. The surrounding array grammar validates
    // that the returned pointer lands before a legal separator, so prefix-only
    // input such as `1foo` is still rejected without an extra hot-path branch.
    if (p < end && *p == '+') {
      r = fast_float::from_chars(p + 1, end, *out);
      if (r.ec == std::errc{} && r.ptr != p + 1) { p = r.ptr; return true; }
    }
    return false;
  }

  bool parse_double(double* out) {
    skip_ws();
    auto r = fast_float::from_chars(p, end, *out);
    if (r.ec == std::errc{} && r.ptr != p) { p = r.ptr; return true; }
    if (p < end && *p == '+') {
      r = fast_float::from_chars(p + 1, end, *out);
      if (r.ec == std::errc{} && r.ptr != p + 1) { p = r.ptr; return true; }
    }
    return false;
  }

  // Decimal `[+-]?digits` integer parse with in-loop overflow detection. USD
  // integer literals are decimal; this is our own atoi (modeled on
  // src/tiny-string.cc str::parse_int64) so the parser depends on neither libc
  // strtoll (locale-aware, base-0 octal/hex, and the hottest symbol on mesh
  // scenes) nor std stdio — important for WASM/WASI. Overflow returns false,
  // matching strtoll's ERANGE rejection.
  bool parse_i64(int64_t* out) {
    skip_ws();
    const char* q = p;
    if (q >= end) return false;
    bool neg = false;
    if (*q == '+' || *q == '-') { neg = (*q == '-'); ++q; }
    if (q >= end || *q < '0' || *q > '9') return false;
    // Largest magnitude: INT64_MAX, or INT64_MAX+1 for a negative value.
    const uint64_t limit = neg ? (static_cast<uint64_t>(INT64_MAX) + 1u)
                               : static_cast<uint64_t>(INT64_MAX);
    uint64_t val = 0;
    while (q < end && *q >= '0' && *q <= '9') {
      const uint64_t d = static_cast<uint64_t>(*q - '0');
      if (val > (limit - d) / 10u) return false;  // would overflow
      val = val * 10u + d;
      ++q;
    }
    // 0u - val gives the two's-complement negation, correct even at INT64_MIN.
    *out = neg ? static_cast<int64_t>(0u - val) : static_cast<int64_t>(val);
    p = q;
    return true;
  }

  bool parse_i32(int32_t* out) {
    skip_ws();
    const char* q = p;
    if (q >= end) return false;
    bool neg = false;
    if (*q == '+' || *q == '-') { neg = (*q == '-'); ++q; }
    if (q >= end) return false;
    unsigned d = static_cast<unsigned>(*q - '0');
    if (d > 9u) return false;
    const uint32_t limit = neg ? 2147483648u : 2147483647u;
    uint32_t val = 0;
    do {
      if (val > (limit - d) / 10u) return false;
      val = val * 10u + d;
      ++q;
      if (q >= end) break;
      d = static_cast<unsigned>(*q - '0');
    } while (d <= 9u);
    *out = neg ? static_cast<int32_t>(0u - val) : static_cast<int32_t>(val);
    p = q;
    return true;
  }

  bool parse_u64(uint64_t* out) {
    skip_ws();
    const char* q = p;
    if (q >= end || *q == '-') return false;
    if (*q == '+') ++q;
    if (q >= end || *q < '0' || *q > '9') return false;
    uint64_t val = 0;
    while (q < end && *q >= '0' && *q <= '9') {
      const uint64_t d = static_cast<uint64_t>(*q - '0');
      if (val > (UINT64_MAX - d) / 10u) return false;  // would overflow
      val = val * 10u + d;
      ++q;
    }
    *out = val;
    p = q;
    return true;
  }

  bool parse_u32(uint32_t* out) {
    skip_ws();
    const char* q = p;
    if (q >= end || *q == '-') return false;
    if (*q == '+') ++q;
    if (q >= end) return false;
    unsigned d = static_cast<unsigned>(*q - '0');
    if (d > 9u) return false;
    uint32_t val = 0;
    do {
      if (val > (UINT32_MAX - d) / 10u) return false;
      val = val * 10u + d;
      ++q;
      if (q >= end) break;
      d = static_cast<unsigned>(*q - '0');
    } while (d <= 9u);
    *out = val;
    p = q;
    return true;
  }

  bool parse_bool(bool* out) {
    skip_ws();
    if (p + 4 <= end && std::memcmp(p, "true", 4) == 0 &&
        (p + 4 == end || !SliceIsIdentifierContinue(*(p + 4)))) {
      p += 4;
      *out = true;
      return true;
    }
    if (p + 5 <= end && std::memcmp(p, "false", 5) == 0 &&
        (p + 5 == end || !SliceIsIdentifierContinue(*(p + 5)))) {
      p += 5;
      *out = false;
      return true;
    }
    double v = 0.0;
    if (!parse_double(&v)) return false;
    *out = (v != 0.0);
    return true;
  }

  bool parse_string(std::string* out) {
    skip_ws();
    if (p >= end || (*p != '"' && *p != '\'')) return false;
    const char quote = *p;
    ++p;
    bool triple = false;
    if (p + 1 < end && p[0] == quote && p[1] == quote) {
      triple = true;
      p += 2;
    }
    out->clear();
    while (p < end) {
      char c = *p++;
      if (triple) {
        if (c == quote && p + 1 < end && p[0] == quote && p[1] == quote) {
          p += 2;
          return true;
        }
      } else if (c == quote) {
        return true;
      }
      if (c == '\\' && p < end) {
        char esc = *p++;
        switch (esc) {
          case 'n': out->push_back('\n'); break;
          case 'r': out->push_back('\r'); break;
          case 't': out->push_back('\t'); break;
          case '"': out->push_back('"'); break;
          case '\'': out->push_back('\''); break;
          case '\\': out->push_back('\\'); break;
          default: out->push_back(esc); break;
        }
      } else {
        out->push_back(c);
      }
    }
    return false;
  }

  bool parse_identifier(std::string* out) {
    skip_ws();
    if (p >= end || !SliceIsIdentifierStart(*p)) return false;
    const char* start = p;
    ++p;
    while (p < end && SliceIsIdentifierContinue(*p)) ++p;
    out->assign(start, size_t(p - start));
    return true;
  }

  bool parse_token(std::string* out) {
    return parse_string(out) || parse_identifier(out);
  }
};

// Defense-in-depth ceiling on the number of scalars accumulated for one array
// (parity with the mature ASCII parser's kMaxArrayElements). USDA arrays are
// otherwise only input-length-bounded; this caps a single pathological array so
// it can't drive a multi-GB vector even from a large/streamed input.
constexpr size_t kMaxArrayScalars = size_t(1) << 30;  // ~1.07e9 scalars

// ============================================================
// Element-parse functors for the numeric array paths
// ============================================================
//
// Namespace-scope functor types (not per-call-site lambdas) so every caller —
// the sync parse, the intra-array parallel workers, and the deferred-array
// worker fill — instantiates the SAME ParseChunkFlat/ParseSimpleArraySerial
// specializations. Duplicate instantiations of these big loops blow GCC's
// unit-growth inline budget and fast_float::from_chars stops inlining into the
// hot loops (measured ~10% Caldera regression from one extra lambda type).

// Comment-aware SliceParser element parsers (generic/fallback paths).
struct ParseFloatSliceFn {
  bool operator()(SliceParser* s, float* v) const { return s->parse_float(v); }
};
struct ParseDoubleSliceFn {
  bool operator()(SliceParser* s, double* v) const { return s->parse_double(v); }
};
struct ParseI64SliceFn {
  bool operator()(SliceParser* s, int64_t* v) const { return s->parse_i64(v); }
};
struct ParseU64SliceFn {
  bool operator()(SliceParser* s, uint64_t* v) const { return s->parse_u64(v); }
};

// Raw chunk-path float parsers: leading ws is guaranteed consumed by
// ParseChunkFlat (which skips ws before every element) and `simple` arrays
// contain no comments, so skip SliceParser::skip_ws and go straight to
// fast_float. Same accept/reject semantics as parse_float/parse_double
// (including the leading-'+' fallback).
struct ParseFloatRawFn {
  bool operator()(SliceParser* s, float* v) const {
    auto r = fast_float::from_chars(s->p, s->end, *v);
    if (r.ec == std::errc{} && r.ptr != s->p) { s->p = r.ptr; return true; }
    if (s->p < s->end && *s->p == '+') {
      r = fast_float::from_chars(s->p + 1, s->end, *v);
      if (r.ec == std::errc{} && r.ptr != s->p + 1) { s->p = r.ptr; return true; }
    }
    return false;
  }
};
struct ParseDoubleRawFn {
  bool operator()(SliceParser* s, double* v) const {
    auto r = fast_float::from_chars(s->p, s->end, *v);
    if (r.ec == std::errc{} && r.ptr != s->p) { s->p = r.ptr; return true; }
    if (s->p < s->end && *s->p == '+') {
      r = fast_float::from_chars(s->p + 1, s->end, *v);
      if (r.ec == std::errc{} && r.ptr != s->p + 1) { s->p = r.ptr; return true; }
    }
    return false;
  }
};

template <class T, class ParseOne>
bool ParseScalarArray(SliceParser* sp, std::vector<T>* out, ParseOne parse_one) {
  if (!sp->consume('[')) return false;
  out->clear();
  if (sp->at_array_end()) return sp->finish_array();
  while (true) {
    T v{};
    if (!parse_one(sp, &v)) return false;
    out->push_back(v);
    if (out->size() > kMaxArrayScalars) return false;
    if (sp->maybe_consume(',')) continue;
    return sp->finish_array();
  }
}

template <class ScalarT, uint32_t N, class ParseOne>
bool ParseTupleArray(SliceParser* sp, std::vector<ScalarT>* out,
                     ParseOne parse_one) {
  if (!sp->consume('[')) return false;
  out->clear();
  if (sp->at_array_end()) return sp->finish_array();
  while (true) {
    if (!sp->consume('(')) return false;
    for (uint32_t i = 0; i < N; i++) {
      ScalarT v{};
      if (!parse_one(sp, &v)) return false;
      out->push_back(v);
      if (i + 1 < N && !sp->consume(',')) return false;
    }
    if (!sp->consume(')')) return false;
    if (out->size() > kMaxArrayScalars) return false;
    if (sp->maybe_consume(',')) continue;
    return sp->finish_array();
  }
}

template <class ScalarT, uint32_t N, class ParseOne>
bool ParseMatrixArray(SliceParser* sp, std::vector<ScalarT>* out,
                      ParseOne parse_one) {
  if (!sp->consume('[')) return false;
  out->clear();
  if (sp->at_array_end()) return sp->finish_array();
  while (true) {
    if (!sp->consume('(')) return false;
    for (uint32_t r = 0; r < N; r++) {
      if (!sp->consume('(')) return false;
      for (uint32_t c = 0; c < N; c++) {
        ScalarT v{};
        if (!parse_one(sp, &v)) return false;
        out->push_back(v);
        if (c + 1 < N && !sp->consume(',')) return false;
      }
      if (!sp->consume(')')) return false;
      if (r + 1 < N && !sp->consume(',')) return false;
    }
    if (!sp->consume(')')) return false;
    if (out->size() > kMaxArrayScalars) return false;
    if (sp->maybe_consume(',')) continue;
    return sp->finish_array();
  }
}

// ============================================================
// Parallel numeric array parse (large "simple" arrays)
// ============================================================
//
// Distributes the per-element fast_float work across worker threads for big
// numeric arrays. Each worker parses a byte sub-range that contains WHOLE
// elements (boundaries snapped to a top-level separator comma), into a private
// vector; the main thread concatenates them in order. The result is byte-
// identical to the serial parse and independent of the thread count: every token
// is produced by the same fast_float, and element order is preserved. Only used
// when the captured array is "simple" (no comments/strings/asset-refs/nested
// brackets), so every comma/paren is a pure separator. On any worker failure
// (a boundary edge case), the caller falls back to a serial parse — so the
// parallel path can only ever be an optimization, never a correctness risk.
// Min array text size to bother spawning threads, and min bytes per worker chunk.
// Conservative: only clearly-large arrays parallelize (the gain is memory-bandwidth
// bound, so a lower threshold only adds thread-spawn overhead on medium arrays for
// no extra throughput).
constexpr size_t kParallelArrayMinBytes = size_t(4) << 20;  // 4 MiB
constexpr size_t kParallelChunkMinBytes = size_t(1) << 20;  // 1 MiB

// Parse one byte range [b,e) of comma-separated elements (N scalars each; N>1 are
// paren-wrapped vector tuples) into `local`. Returns false on any malformed token.
// A non-final chunk ends right after a boundary separator comma (expected); the
// final chunk (`is_last`) ends at the ']' and must reject a trailing comma, to
// stay byte-for-byte equivalent to the serial parse on malformed input.
template <class T, uint32_t N, class ParseOne>
bool ParseChunkFlat(const char* b, const char* e, bool is_last,
                    std::vector<T>* local, ParseOne parse_one) {
  SliceParser sp{b, e};
  // Inline whitespace skip. `simple` arrays (the only ones reaching here) contain
  // no `#` comments, so this matches SliceParser::skip_ws on this input. Hoisting
  // it out of parse_one/maybe_consume collapses the 3 skip_ws + the maybe_consume
  // call per element down to one ws scan per separator — the per-element overhead
  // dominates for short-token arrays (indices, 0/1 values) where each fast_float
  // call is tiny.
  const char* const end = sp.end;
  auto skipws = [&] {
    const char* p = sp.p;
    while (p < end) {
      const char c = *p;
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++p;
      else break;
    }
    sp.p = p;
  };
  skipws();
  if (sp.p >= end) return true;  // empty chunk
  while (true) {
    if (N == 1) {
      T v{};
      // Leading ws already consumed (loop start / post-separator skipws).
      if (!parse_one(&sp, &v)) return false;
      local->push_back(v);
    } else {
      if (sp.p >= end || *sp.p != '(') return false;
      ++sp.p;
      for (uint32_t i = 0; i < N; i++) {
        T v{};
        skipws();  // ws after '(' or after the intra-tuple comma
        if (!parse_one(&sp, &v)) return false;
        local->push_back(v);
        skipws();
        if (i + 1 < N) {
          if (sp.p >= end || *sp.p != ',') return false;
          ++sp.p;
        }
      }
      if (sp.p >= end || *sp.p != ')') return false;
      ++sp.p;
    }
    skipws();
    if (sp.p >= end) return true;     // last element of chunk, no trailing comma
    if (*sp.p != ',') return false;   // adjacent elements with no separator
    ++sp.p;                            // consume the separator comma
    skipws();
    if (sp.p >= end) {
      // Trailing separator: the normal inter-chunk boundary, but on the FINAL
      // chunk it is a trailing comma before ']' which the serial parser rejects.
      return is_last ? false : true;
    }
  }
}

inline void SkipSimpleArrayWs(const char*& p, const char* end) {
  while (p < end) {
    const char c = *p;
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++p;
    else break;
  }
}

inline bool ParseI32TokenSimple(const char*& p, const char* end,
                                int32_t* out) {
  bool neg = false;
  if (p < end && (*p == '+' || *p == '-')) {
    neg = (*p == '-');
    ++p;
  }
  if (p >= end) return false;
  unsigned d = static_cast<unsigned>(*p - '0');
  if (d > 9u) return false;
  const uint32_t limit = neg ? 2147483648u : 2147483647u;
  uint32_t val = 0;
  do {
    if (val > (limit - d) / 10u) return false;
    val = val * 10u + d;
    ++p;
    if (p >= end) break;
    d = static_cast<unsigned>(*p - '0');
  } while (d <= 9u);
  *out = neg ? static_cast<int32_t>(0u - val) : static_cast<int32_t>(val);
  return true;
}

inline bool ParseU32TokenSimple(const char*& p, const char* end,
                                uint32_t* out) {
  if (p < end && *p == '-') return false;
  if (p < end && *p == '+') ++p;
  if (p >= end) return false;
  unsigned d = static_cast<unsigned>(*p - '0');
  if (d > 9u) return false;
  uint32_t val = 0;
  do {
    if (val > (UINT32_MAX - d) / 10u) return false;
    val = val * 10u + d;
    ++p;
    if (p >= end) break;
    d = static_cast<unsigned>(*p - '0');
  } while (d <= 9u);
  *out = val;
  return true;
}

// Token-style int parsers for the flat int chunk path (no ws handling; the
// chunk loop owns separators). Namespace-scope for single-instantiation — see
// the functor block above.
struct ParseI32TokenFn {
  bool operator()(const char*& p, const char* e, int32_t* v) const {
    return ParseI32TokenSimple(p, e, v);
  }
};
struct ParseU32TokenFn {
  bool operator()(const char*& p, const char* e, uint32_t* v) const {
    return ParseU32TokenSimple(p, e, v);
  }
};

template <class T, class ParseToken>
bool ParseScalarIntChunkFlat(const char* b, const char* e, bool is_last,
                             std::vector<T>* local,
                             ParseToken parse_token) {
  const char* p = b;
  SkipSimpleArrayWs(p, e);
  if (p >= e) return true;
  while (true) {
    T v{};
    if (!parse_token(p, e, &v)) return false;
    local->push_back(v);
    if (local->size() > kMaxArrayScalars) return false;
    SkipSimpleArrayWs(p, e);
    if (p >= e) return true;
    if (*p != ',') return false;
    ++p;
    SkipSimpleArrayWs(p, e);
    if (p >= e) return is_last ? false : true;
  }
}

// Snap a target offset forward to a top-level element-separator boundary, returning
// the position just AFTER the separator comma (or `end` if none remains).
template <uint32_t N>
const char* SnapElementBoundary(const char* t, const char* end) {
  if (N > 1) {
    // Vector tuple: the first ')' at/after t closes a tuple (vectors don't nest),
    // after which the next ',' is a top-level separator.
    const char* rp = static_cast<const char*>(std::memchr(t, ')', size_t(end - t)));
    if (!rp) return end;
    t = rp + 1;
  }
  const char* c = static_cast<const char*>(std::memchr(t, ',', size_t(end - t)));
  return c ? c + 1 : end;
}

#if defined(TINYUSDZ_ENABLE_THREAD)

// Resolve the worker count from a ParseOptions::num_threads hint (forwarded via
// Lexer::num_threads): 1 = serial, >1 = that many, <=0 = auto (min(hw, 8)). No
// process-environment input -- the caller passes the hint explicitly.
static int ResolveParseThreads(int requested) {
  if (requested == 1) return 1;
  if (requested > 1) return requested;
  int hw = static_cast<int>(std::thread::hardware_concurrency());
  if (hw < 1) hw = 1;
  return std::min(hw, 8);
}

// Returns true and fills *out on success; false => caller does a serial fallback.
template <class T, uint32_t N, class ParseOne>
bool ParseNumericArrayParallel(const char* data, size_t len, int nt_hint,
                               std::vector<T>* out, ParseOne parse_one,
                               USDAParseProfile* profile) {
  // Captured span includes the outer '[' .. ']'.
  if (len < 2 || data[0] != '[' || data[len - 1] != ']') return false;
  const char* content_begin = data + 1;
  const char* content_end = data + len - 1;
  const size_t content_len = size_t(content_end - content_begin);
  if (content_len < kParallelArrayMinBytes) return false;

  const int nt = ResolveParseThreads(nt_hint);
  if (nt <= 1) return false;
  size_t nchunks = std::min<size_t>(size_t(nt), content_len / kParallelChunkMinBytes);
  if (nchunks < 2) return false;

  std::vector<const char*> cut(nchunks + 1);
  cut[0] = content_begin;
  cut[nchunks] = content_end;
  for (size_t k = 1; k < nchunks; k++) {
    const char* target = content_begin + (content_len * k) / nchunks;
    const char* c = SnapElementBoundary<N>(target, content_end);
    if (c < cut[k - 1]) c = cut[k - 1];  // keep monotonic
    cut[k] = c;
  }

  std::vector<std::vector<T>> locals(nchunks);
  std::vector<char> ok(nchunks, 1);
  std::vector<std::thread> threads;
  threads.reserve(nchunks);
  for (size_t k = 0; k < nchunks; k++) {
    const char* b = cut[k];
    const char* e = cut[k + 1];
    if (b >= e) continue;  // empty chunk
    const bool is_last = (e == content_end);
    threads.emplace_back([b, e, is_last, k, &locals, &ok, parse_one]() {
      ok[k] = ParseChunkFlat<T, N>(b, e, is_last, &locals[k], parse_one) ? 1 : 0;
    });
  }
  for (auto& th : threads) th.join();
  for (char c : ok) {
    if (!c) {
      if (profile) profile->parallel_array_fallbacks++;
      return false;  // boundary edge -> serial fallback
    }
  }

  size_t total = 0;
  for (const auto& l : locals) total += l.size();
  out->clear();
  out->reserve(total);
  for (auto& l : locals) out->insert(out->end(), l.begin(), l.end());
  if (profile) {
    profile->parallel_arrays++;
    profile->parallel_array_bytes += len;
    profile->parallel_array_scalars += total;
  }
  return true;
}

#endif  // TINYUSDZ_ENABLE_THREAD

template <class T, class ParseToken>
bool ParseSimpleScalarIntArraySerial(const char* data, size_t len,
                                     size_t expected_scalars,
                                     std::vector<T>* out,
                                     ParseToken parse_token) {
  if (len < 2 || data[0] != '[' || data[len - 1] != ']') return false;
  out->clear();
  // Exact-capacity reserve from the capture-time comma count (scalars =
  // commas + 1 for a well-formed simple array): removes all growth
  // reallocation/copying. Reserve + push_back, deliberately NOT a pointer-fill
  // loop: adding fill-variant instantiations of the chunk parsers broke
  // fast_float inlining (unit-growth budget; 12.2s -> 13.5s on Caldera), and
  // with __attribute__((flatten)) to restore inlining the fill loop still only
  // matched reserve+push_back (resize's zero-fill offsets the bookkeeping win).
  if (expected_scalars > 0 && expected_scalars <= kMaxArrayScalars) {
    out->reserve(expected_scalars);
  }
  return ParseScalarIntChunkFlat<T>(data + 1, data + len - 1,
                                    /*is_last=*/true, out, parse_token);
}

#if defined(TINYUSDZ_ENABLE_THREAD)
template <class T, class ParseToken>
bool ParseScalarIntArrayParallel(const char* data, size_t len, int nt_hint,
                                 std::vector<T>* out, ParseToken parse_token,
                                 USDAParseProfile* profile) {
  if (len < 2 || data[0] != '[' || data[len - 1] != ']') return false;
  const char* content_begin = data + 1;
  const char* content_end = data + len - 1;
  const size_t content_len = size_t(content_end - content_begin);
  if (content_len < kParallelArrayMinBytes) return false;

  const int nt = ResolveParseThreads(nt_hint);
  if (nt <= 1) return false;
  const size_t nchunks =
      std::min<size_t>(size_t(nt), content_len / kParallelChunkMinBytes);
  if (nchunks < 2) return false;

  std::vector<const char*> cut(nchunks + 1);
  cut[0] = content_begin;
  cut[nchunks] = content_end;
  for (size_t k = 1; k < nchunks; k++) {
    const char* target = content_begin + (content_len * k) / nchunks;
    const char* c = SnapElementBoundary<1>(target, content_end);
    if (c < cut[k - 1]) c = cut[k - 1];
    cut[k] = c;
  }

  std::vector<std::vector<T>> locals(nchunks);
  std::vector<char> ok(nchunks, 1);
  std::vector<std::thread> threads;
  threads.reserve(nchunks);
  for (size_t k = 0; k < nchunks; k++) {
    const char* b = cut[k];
    const char* e = cut[k + 1];
    if (b >= e) continue;
    const bool is_last = (e == content_end);
    threads.emplace_back([b, e, is_last, k, &locals, &ok, parse_token]() {
      ok[k] = ParseScalarIntChunkFlat<T>(b, e, is_last, &locals[k],
                                         parse_token) ? 1 : 0;
    });
  }
  for (auto& th : threads) th.join();
  for (char c : ok) {
    if (!c) {
      if (profile) profile->parallel_array_fallbacks++;
      return false;
    }
  }

  size_t total = 0;
  for (const auto& l : locals) total += l.size();
  if (total > kMaxArrayScalars) return false;
  out->clear();
  out->reserve(total);
  for (auto& l : locals) out->insert(out->end(), l.begin(), l.end());
  if (profile) {
    profile->parallel_arrays++;
    profile->parallel_array_bytes += len;
    profile->parallel_array_scalars += total;
  }
  return true;
}
#endif

template <class T, class ParseToken>
bool ParseScalarIntArrayMaybeParallel(const char* data, size_t len, bool simple,
                                      size_t expected_scalars, int nt_hint,
                                      std::vector<T>* out,
                                      ParseToken parse_token,
                                      USDAParseProfile* profile) {
#if defined(TINYUSDZ_ENABLE_THREAD)
  if (simple &&
      ParseScalarIntArrayParallel<T>(data, len, nt_hint, out, parse_token,
                                     profile)) {
    return true;
  }
#else
  (void)nt_hint;
  (void)profile;
#endif
  if (simple && ParseSimpleScalarIntArraySerial<T>(data, len, expected_scalars,
                                                   out, parse_token)) {
    return true;
  }
  return false;
}

// Parse a scalar numeric array, using the parallel path for large simple arrays
// and falling back to the serial SliceParser otherwise.
// Serial parse of a whole simple array span ([ .. ]) through the tight
// ParseChunkFlat inner loop — the same low-per-element-overhead path the parallel
// workers use, so sub-threshold (and threads-off) arrays get the same speedup.
template <class T, uint32_t N, class ParseOne>
bool ParseSimpleArraySerial(const char* data, size_t len,
                            size_t expected_scalars, std::vector<T>* out,
                            ParseOne parse_one) {
  if (len < 2 || data[0] != '[' || data[len - 1] != ']') return false;
  out->clear();
  // Exact-capacity reserve from the capture-time comma count (see the int
  // variant above for why reserve-not-fill).
  if (expected_scalars > 0 && expected_scalars <= kMaxArrayScalars) {
    out->reserve(expected_scalars);
  }
  return ParseChunkFlat<T, N>(data + 1, data + len - 1, /*is_last=*/true, out,
                              parse_one);
}

// `parse_one_raw` is the chunk-path element parser: it may assume leading ws is
// already consumed (ParseChunkFlat skips ws before every element) and that the
// input is a `simple` array (no comments), so it can go straight to the numeric
// conversion. `parse_one` is the comment-aware generic parser used by the
// SliceParser fallback (non-simple arrays). Keeping the raw functor as the ONLY
// ParseChunkFlat instantiation per (T,N) — instead of adding a second lambda
// type — is deliberate: extra instantiations of these big loops blow the inline
// budget and un-inline fast_float (measured 10% regression).
template <class T, class ParseOneRaw, class ParseOne>
bool ParseScalarArrayMaybeParallel(const char* data, size_t len, bool simple,
                                   size_t expected_scalars, int nt_hint,
                                   std::vector<T>* out, ParseOneRaw parse_one_raw,
                                   ParseOne parse_one,
                                   USDAParseProfile* profile) {
#if defined(TINYUSDZ_ENABLE_THREAD)
  if (simple &&
      ParseNumericArrayParallel<T, 1>(data, len, nt_hint, out, parse_one_raw,
                                      profile)) {
    return true;
  }
#else
  (void)nt_hint;
  (void)profile;
#endif
  if (simple && ParseSimpleArraySerial<T, 1>(data, len, expected_scalars, out,
                                             parse_one_raw)) {
    return true;
  }
  SliceParser sp{data, data + len};
  return ParseScalarArray<T>(&sp, out, parse_one);
}

// Same, for paren-wrapped single-level vector tuples (float3, double2, ...).
template <class T, uint32_t N, class ParseOneRaw, class ParseOne>
bool ParseTupleArrayMaybeParallel(const char* data, size_t len, bool simple,
                                  size_t expected_scalars, int nt_hint,
                                  std::vector<T>* out, ParseOneRaw parse_one_raw,
                                  ParseOne parse_one,
                                  USDAParseProfile* profile) {
#if defined(TINYUSDZ_ENABLE_THREAD)
  if (simple &&
      ParseNumericArrayParallel<T, N>(data, len, nt_hint, out, parse_one_raw,
                                      profile)) {
    return true;
  }
#else
  (void)nt_hint;
  (void)profile;
#endif
  if (simple && ParseSimpleArraySerial<T, N>(data, len, expected_scalars, out,
                                             parse_one_raw)) {
    return true;
  }
  SliceParser sp{data, data + len};
  return ParseTupleArray<T, N>(&sp, out, parse_one);
}

bool CanParseCapturedArrayValue(TypeId element_type) {
  switch (element_type) {
    case TypeId::Float:
    case TypeId::Double:
    case TypeId::Int:
    case TypeId::UInt:
    case TypeId::Int64:
    case TypeId::UInt64:
    case TypeId::Bool:
    case TypeId::Half:
    case TypeId::Half2:
    case TypeId::Half3:
    case TypeId::Half4:
    case TypeId::Float2:
    case TypeId::Float3:
    case TypeId::Float4:
    case TypeId::Point3h:
    case TypeId::Point3f:
    case TypeId::Vector3h:
    case TypeId::Vector3f:
    case TypeId::Normal3h:
    case TypeId::Normal3f:
    case TypeId::Color3h:
    case TypeId::Color3f:
    case TypeId::Color4h:
    case TypeId::Color4f:
    case TypeId::Texcoord2h:
    case TypeId::Texcoord2f:
    case TypeId::Texcoord3h:
    case TypeId::Texcoord3f:
    case TypeId::Quath:
    case TypeId::Quatf:
    case TypeId::Double2:
    case TypeId::Double3:
    case TypeId::Double4:
    case TypeId::Point3d:
    case TypeId::Vector3d:
    case TypeId::Normal3d:
    case TypeId::Color3d:
    case TypeId::Color4d:
    case TypeId::Texcoord2d:
    case TypeId::Texcoord3d:
    case TypeId::Quatd:
    case TypeId::Matrix2f:
    case TypeId::Matrix3f:
    case TypeId::Matrix4f:
    case TypeId::Matrix2d:
    case TypeId::Matrix3d:
    case TypeId::Matrix4d:
    case TypeId::Token:
    case TypeId::String:
      return true;
    default:
      return false;
  }
}

ParseResult ParseCapturedArrayValue(const char* data, size_t len, bool simple,
                                    size_t comma_count, TypeId element_type,
                                    int num_threads,
                                    USDAParseProfile* profile) {
  SliceParser sp{data, data + len};
  // Scalar-count prediction from the capture's comma tally: any well-formed
  // simple array (bare scalars or paren tuples) has exactly one comma between
  // adjacent scalars, so scalars = commas + 1. Overpredicts by 1 only for an
  // empty array (handled by the fill path's resize-down). 0 = unknown, which
  // keeps the push_back growth path.
  const size_t expected_scalars = simple ? comma_count + 1 : 0;
  auto profile_array = [&](bool numeric, size_t scalars) {
    if (!profile) return;
    profile->arrays++;
    profile->array_bytes += len;
    if (simple) profile->simple_arrays++;
    if (numeric) {
      profile->numeric_arrays++;
      profile->numeric_array_bytes += len;
      profile->numeric_array_scalars += scalars;
    }
  };

  // Shared functor types (single instantiation per chunk template — see the
  // functor block above).
  const ParseFloatSliceFn parse_float{};
  const ParseDoubleSliceFn parse_double{};
  const ParseFloatRawFn parse_float_raw{};
  const ParseDoubleRawFn parse_double_raw{};

  if (element_type == TypeId::Float) {
    std::vector<float> values;
    if (!ParseScalarArrayMaybeParallel<float>(data, len, simple, expected_scalars, num_threads, &values, parse_float_raw, parse_float, profile)) {
      return ParseResult::Error("Failed to parse float array");
    }
    profile_array(true, values.size());
    return ParseResult::Ok(Value::MakeFloatArray(std::move(values)));
  }
  if (element_type == TypeId::Double) {
    std::vector<double> values;
    if (!ParseScalarArrayMaybeParallel<double>(data, len, simple, expected_scalars, num_threads, &values, parse_double_raw, parse_double, profile)) {
      return ParseResult::Error("Failed to parse double array");
    }
    profile_array(true, values.size());
    return ParseResult::Ok(Value::MakeDoubleArray(std::move(values)));
  }
  if (element_type == TypeId::Half) {
    std::vector<float> values;
    if (!ParseScalarArrayMaybeParallel<float>(data, len, simple, expected_scalars, num_threads, &values, parse_float_raw, parse_float, profile)) {
      return ParseResult::Error("Failed to parse half array");
    }
    profile_array(true, values.size());
    return ParseResult::Ok(Value::MakeFloatCompArray(std::move(values), element_type, 1));
  }
  if (element_type == TypeId::Bool) {
    std::vector<bool> values;
    auto parse_bool = [](SliceParser* s, bool* v) { return s->parse_bool(v); };
    if (!ParseScalarArray<bool>(&sp, &values, parse_bool)) {
      return ParseResult::Error("Failed to parse bool array");
    }
    profile_array(false, values.size());
    return ParseResult::Ok(Value::MakeBoolArray(values));
  }
  if (element_type == TypeId::Int) {
    std::vector<int32_t> values;
    const ParseI32TokenFn parse_i32{};
    if (!ParseScalarIntArrayMaybeParallel<int32_t>(data, len, simple, expected_scalars, num_threads, &values, parse_i32, profile)) {
      SliceParser fallback{data, data + len};
      auto parse_i32_generic = [](SliceParser* s, int32_t* v) { return s->parse_i32(v); };
      if (!ParseScalarArray<int32_t>(&fallback, &values, parse_i32_generic)) {
        return ParseResult::Error("Failed to parse int array");
      }
    }
    if (values.size() > kMaxArrayScalars) {
      return ParseResult::Error("Failed to parse int array");
    }
    profile_array(true, values.size());
    return ParseResult::Ok(Value::MakeIntArray(std::move(values)));
  }
  if (element_type == TypeId::UInt) {
    std::vector<uint32_t> values;
    const ParseU32TokenFn parse_u32{};
    if (!ParseScalarIntArrayMaybeParallel<uint32_t>(data, len, simple, expected_scalars, num_threads, &values, parse_u32, profile)) {
      SliceParser fallback{data, data + len};
      auto parse_u32_generic = [](SliceParser* s, uint32_t* v) { return s->parse_u32(v); };
      if (!ParseScalarArray<uint32_t>(&fallback, &values, parse_u32_generic)) {
        return ParseResult::Error("Failed to parse uint array");
      }
    }
    if (values.size() > kMaxArrayScalars) {
      return ParseResult::Error("Failed to parse uint array");
    }
    profile_array(true, values.size());
    return ParseResult::Ok(Value::MakeUIntArray(std::move(values)));
  }
  if (element_type == TypeId::Int64) {
    std::vector<int64_t> values;
    const ParseI64SliceFn parse_i64{};
    if (!ParseScalarArrayMaybeParallel<int64_t>(data, len, simple, expected_scalars, num_threads, &values, parse_i64, parse_i64, profile)) {
      return ParseResult::Error("Failed to parse int64 array");
    }
    profile_array(true, values.size());
    return ParseResult::Ok(Value::MakeInt64Array(std::move(values)));
  }
  if (element_type == TypeId::UInt64) {
    std::vector<uint64_t> values;
    const ParseU64SliceFn parse_u64{};
    if (!ParseScalarArrayMaybeParallel<uint64_t>(data, len, simple, expected_scalars, num_threads, &values, parse_u64, parse_u64, profile)) {
      return ParseResult::Error("Failed to parse uint64 array");
    }
    profile_array(true, values.size());
    return ParseResult::Ok(Value::MakeUInt64Array(std::move(values)));
  }
  if (element_type == TypeId::Token) {
    // Intern each token as it is parsed, straight into a vector<TfToken> — no
    // intermediate std::string vector (the token-array storage is interned).
    std::vector<TfToken> toks;
    if (!ParseScalarArray<TfToken>(&sp, &toks, [](SliceParser* s, TfToken* v) {
          std::string tmp;
          if (!s->parse_token(&tmp)) return false;
          *v = TfToken(tmp);
          return true;
        })) {
      return ParseResult::Error("Failed to parse token array");
    }
    profile_array(false, toks.size());
    return ParseResult::Ok(Value::MakeTokenArray(std::move(toks)));
  }
  if (element_type == TypeId::String) {
    std::vector<std::string> values;
    auto parse_string = [](SliceParser* s, std::string* v) {
      return s->parse_string(v);
    };
    using ParseStringFn = bool (*)(SliceParser*, std::string*);
    if (!ParseScalarArray<std::string>(&sp, &values,
                                       static_cast<ParseStringFn>(parse_string))) {
      return ParseResult::Error("Failed to parse string array");
    }
    profile_array(false, values.size());
    return ParseResult::Ok(Value::MakeTokenArray(std::move(values)));
  }

  if (IsFloat2Like(element_type)) {
    std::vector<float> values;
    if (!ParseTupleArrayMaybeParallel<float, 2>(data, len, simple, expected_scalars, num_threads, &values, parse_float_raw, parse_float, profile)) {
      return ParseResult::Error("Failed to parse float2 array");
    }
    profile_array(true, values.size());
    return ParseResult::Ok(Value::MakeFloatCompArray(std::move(values), element_type, 2));
  }
  if (IsFloat3Like(element_type)) {
    std::vector<float> values;
    if (!ParseTupleArrayMaybeParallel<float, 3>(data, len, simple, expected_scalars, num_threads, &values, parse_float_raw, parse_float, profile)) {
      return ParseResult::Error("Failed to parse float3 array");
    }
    profile_array(true, values.size());
    return ParseResult::Ok(Value::MakeFloatCompArray(std::move(values), element_type, 3));
  }
  if (IsFloat4Like(element_type)) {
    std::vector<float> values;
    if (!ParseTupleArrayMaybeParallel<float, 4>(data, len, simple, expected_scalars, num_threads, &values, parse_float_raw, parse_float, profile)) {
      return ParseResult::Error("Failed to parse float4 array");
    }
    profile_array(true, values.size());
    return ParseResult::Ok(Value::MakeFloatCompArray(std::move(values), element_type, 4));
  }
  if (IsDouble2Like(element_type)) {
    std::vector<double> values;
    if (!ParseTupleArrayMaybeParallel<double, 2>(data, len, simple, expected_scalars, num_threads, &values, parse_double_raw, parse_double, profile)) {
      return ParseResult::Error("Failed to parse double2 array");
    }
    profile_array(true, values.size());
    return ParseResult::Ok(Value::MakeDoubleCompArray(std::move(values), element_type, 2));
  }
  if (IsDouble3Like(element_type)) {
    std::vector<double> values;
    if (!ParseTupleArrayMaybeParallel<double, 3>(data, len, simple, expected_scalars, num_threads, &values, parse_double_raw, parse_double, profile)) {
      return ParseResult::Error("Failed to parse double3 array");
    }
    profile_array(true, values.size());
    return ParseResult::Ok(Value::MakeDoubleCompArray(std::move(values), element_type, 3));
  }
  if (IsDouble4Like(element_type)) {
    std::vector<double> values;
    if (!ParseTupleArrayMaybeParallel<double, 4>(data, len, simple, expected_scalars, num_threads, &values, parse_double_raw, parse_double, profile)) {
      return ParseResult::Error("Failed to parse double4 array");
    }
    profile_array(true, values.size());
    return ParseResult::Ok(Value::MakeDoubleCompArray(std::move(values), element_type, 4));
  }
  if (element_type == TypeId::Matrix2f) {
    std::vector<float> values;
    if (!ParseMatrixArray<float, 2>(&sp, &values, parse_float)) {
      return ParseResult::Error("Failed to parse matrix2f array");
    }
    profile_array(true, values.size());
    return ParseResult::Ok(Value::MakeFloatCompArray(std::move(values), element_type, 4));
  }
  if (element_type == TypeId::Matrix3f) {
    std::vector<float> values;
    if (!ParseMatrixArray<float, 3>(&sp, &values, parse_float)) {
      return ParseResult::Error("Failed to parse matrix3f array");
    }
    profile_array(true, values.size());
    return ParseResult::Ok(Value::MakeFloatCompArray(std::move(values), element_type, 9));
  }
  if (element_type == TypeId::Matrix4f) {
    std::vector<float> values;
    if (!ParseMatrixArray<float, 4>(&sp, &values, parse_float)) {
      return ParseResult::Error("Failed to parse matrix4f array");
    }
    profile_array(true, values.size());
    return ParseResult::Ok(Value::MakeFloatCompArray(std::move(values), element_type, 16));
  }
  if (element_type == TypeId::Matrix2d) {
    std::vector<double> values;
    if (!ParseMatrixArray<double, 2>(&sp, &values, parse_double)) {
      return ParseResult::Error("Failed to parse matrix2d array");
    }
    profile_array(true, values.size());
    return ParseResult::Ok(Value::MakeDoubleCompArray(std::move(values), element_type, 4));
  }
  if (element_type == TypeId::Matrix3d) {
    std::vector<double> values;
    if (!ParseMatrixArray<double, 3>(&sp, &values, parse_double)) {
      return ParseResult::Error("Failed to parse matrix3d array");
    }
    profile_array(true, values.size());
    return ParseResult::Ok(Value::MakeDoubleCompArray(std::move(values), element_type, 9));
  }
  if (element_type == TypeId::Matrix4d) {
    std::vector<double> values;
    if (!ParseMatrixArray<double, 4>(&sp, &values, parse_double)) {
      return ParseResult::Error("Failed to parse matrix4d array");
    }
    profile_array(true, values.size());
    return ParseResult::Ok(Value::MakeDoubleCompArray(std::move(values), element_type, 16));
  }

  return ParseResult::Error("No optimized array parser for type");
}

bool GetDeferredArrayInfo(TypeId element_type, Value::ArrayScalarKind* out_kind,
                          uint32_t* out_comps) {
  using K = Value::ArrayScalarKind;
  if (element_type == TypeId::Float || element_type == TypeId::Half) {
    *out_kind = K::Float; *out_comps = 1; return true;
  }
  if (IsFloat2Like(element_type)) { *out_kind = K::Float; *out_comps = 2; return true; }
  if (IsFloat3Like(element_type)) { *out_kind = K::Float; *out_comps = 3; return true; }
  if (IsFloat4Like(element_type)) { *out_kind = K::Float; *out_comps = 4; return true; }
  if (element_type == TypeId::Double) { *out_kind = K::Double; *out_comps = 1; return true; }
  if (IsDouble2Like(element_type)) { *out_kind = K::Double; *out_comps = 2; return true; }
  if (IsDouble3Like(element_type)) { *out_kind = K::Double; *out_comps = 3; return true; }
  if (IsDouble4Like(element_type)) { *out_kind = K::Double; *out_comps = 4; return true; }
  if (element_type == TypeId::Int) { *out_kind = K::Int32; *out_comps = 1; return true; }
  if (element_type == TypeId::UInt) { *out_kind = K::UInt32; *out_comps = 1; return true; }
  if (element_type == TypeId::Int64) { *out_kind = K::Int64; *out_comps = 1; return true; }
  if (element_type == TypeId::UInt64) { *out_kind = K::UInt64; *out_comps = 1; return true; }
  // bool/token/string/matrix arrays stay on the synchronous path.
  return false;
}

#if defined(TINYUSDZ_ENABLE_THREAD)

// Worker-side deferred parse: convert one captured span into the pre-committed
// Value's payload vector (item.fill.vec). Uses the exact same serial parse
// paths (and functor types — see the single-instantiation note above) as the
// synchronous parser, so the scalars are bit-identical. Returns false on
// malformed input; the scheduler propagates that as a parse failure.
bool FillDeferredArrayValue(const DeferredArrayItem& item) {
  const char* data = item.data;
  const size_t len = item.len;
  const size_t expected = item.expected_scalars;
  const TypeId t = item.type_id;
  void* vec = item.fill.vec;
  if (t == TypeId::Float || t == TypeId::Half) {
    return ParseSimpleArraySerial<float, 1>(
        data, len, expected, static_cast<std::vector<float>*>(vec),
        ParseFloatRawFn{});
  }
  if (IsFloat2Like(t)) {
    return ParseSimpleArraySerial<float, 2>(
        data, len, expected, static_cast<std::vector<float>*>(vec),
        ParseFloatRawFn{});
  }
  if (IsFloat3Like(t)) {
    return ParseSimpleArraySerial<float, 3>(
        data, len, expected, static_cast<std::vector<float>*>(vec),
        ParseFloatRawFn{});
  }
  if (IsFloat4Like(t)) {
    return ParseSimpleArraySerial<float, 4>(
        data, len, expected, static_cast<std::vector<float>*>(vec),
        ParseFloatRawFn{});
  }
  if (t == TypeId::Double) {
    return ParseSimpleArraySerial<double, 1>(
        data, len, expected, static_cast<std::vector<double>*>(vec),
        ParseDoubleRawFn{});
  }
  if (IsDouble2Like(t)) {
    return ParseSimpleArraySerial<double, 2>(
        data, len, expected, static_cast<std::vector<double>*>(vec),
        ParseDoubleRawFn{});
  }
  if (IsDouble3Like(t)) {
    return ParseSimpleArraySerial<double, 3>(
        data, len, expected, static_cast<std::vector<double>*>(vec),
        ParseDoubleRawFn{});
  }
  if (IsDouble4Like(t)) {
    return ParseSimpleArraySerial<double, 4>(
        data, len, expected, static_cast<std::vector<double>*>(vec),
        ParseDoubleRawFn{});
  }
  if (t == TypeId::Int) {
    return ParseSimpleScalarIntArraySerial<int32_t>(
        data, len, expected, static_cast<std::vector<int32_t>*>(vec),
        ParseI32TokenFn{});
  }
  if (t == TypeId::UInt) {
    return ParseSimpleScalarIntArraySerial<uint32_t>(
        data, len, expected, static_cast<std::vector<uint32_t>*>(vec),
        ParseU32TokenFn{});
  }
  if (t == TypeId::Int64) {
    return ParseSimpleArraySerial<int64_t, 1>(
        data, len, expected, static_cast<std::vector<int64_t>*>(vec),
        ParseI64SliceFn{});
  }
  if (t == TypeId::UInt64) {
    return ParseSimpleArraySerial<uint64_t, 1>(
        data, len, expected, static_cast<std::vector<uint64_t>*>(vec),
        ParseU64SliceFn{});
  }
  return false;
}

#endif  // TINYUSDZ_ENABLE_THREAD

ParseResult ParseArrayValueOptimized(Lexer& lexer, TypeId element_type) {
  const char* data = nullptr;
  size_t len = 0;
  bool simple = false;
  size_t commas = 0;
  if (!lexer.capture_bracketed_literal(&data, &len, &simple, &commas)) {
    return ParseResult::Error(lexer.error());
  }
  return ParseCapturedArrayValue(data, len, simple, commas, element_type,
                                 lexer.num_threads, lexer.profile);
}

ParseResult ParseArrayValue(Lexer& lexer, TypeId element_type) {
  // USD allows array-valued attributes to author `None` as a blocked value.
  // Preserve it as a block (not an empty placeholder) so the writer re-emits
  // `= None` rather than collapsing it to a declared-only attribute.
  if (lexer.peek().type == TokenType::None) {
    lexer.consume();
    return ParseResult::Ok(Value::MakeBlock());
  }

  switch (element_type) {
    case TypeId::Float:
    case TypeId::Double:
    case TypeId::Int:
    case TypeId::UInt:
    case TypeId::Int64:
    case TypeId::UInt64:
    case TypeId::Bool:
    case TypeId::Half:
    case TypeId::Half2:
    case TypeId::Half3:
    case TypeId::Half4:
    case TypeId::Float2:
    case TypeId::Float3:
    case TypeId::Float4:
    case TypeId::Point3h:
    case TypeId::Point3f:
    case TypeId::Vector3h:
    case TypeId::Vector3f:
    case TypeId::Normal3h:
    case TypeId::Normal3f:
    case TypeId::Color3h:
    case TypeId::Color3f:
    case TypeId::Color4h:
    case TypeId::Color4f:
    case TypeId::Texcoord2h:
    case TypeId::Texcoord2f:
    case TypeId::Texcoord3h:
    case TypeId::Texcoord3f:
    case TypeId::Quath:
    case TypeId::Quatf:
    case TypeId::Double2:
    case TypeId::Double3:
    case TypeId::Double4:
    case TypeId::Point3d:
    case TypeId::Vector3d:
    case TypeId::Normal3d:
    case TypeId::Color3d:
    case TypeId::Color4d:
    case TypeId::Texcoord2d:
    case TypeId::Texcoord3d:
    case TypeId::Quatd:
    case TypeId::Matrix2f:
    case TypeId::Matrix3f:
    case TypeId::Matrix4f:
    case TypeId::Matrix2d:
    case TypeId::Matrix3d:
    case TypeId::Matrix4d:
    // Token/String arrays use the same bracket-literal fast path; the slice
    // parser handles quotes, triple-quotes and escapes and returns a real
    // token array (see ParseArrayValueOptimized).
    case TypeId::Token:
    case TypeId::String:
      return ParseArrayValueOptimized(lexer, element_type);
    default:
      break;
  }

  if (!lexer.expect(TokenType::OpenBracket)) {
    return ParseResult::Error(lexer.error());
  }

  // Handle empty array
  if (lexer.peek().type == TokenType::CloseBracket) {
    lexer.consume();
    // Return appropriate empty array based on element type
    if (element_type == TypeId::Float || IsFloat3Like(element_type) ||
        IsFloat2Like(element_type) || IsFloat4Like(element_type)) {
      return ParseResult::Ok(Value::MakeFloatArray(std::vector<float>{}));
    } else if (element_type == TypeId::Int) {
      return ParseResult::Ok(Value::MakeIntArray(std::vector<int32_t>{}));
    } else if (element_type == TypeId::Token || element_type == TypeId::String) {
      return ParseResult::Ok(Value::MakeTokenArray(std::vector<std::string>{}));
    }
    return ParseResult::Ok(Value::MakeFloatArray(std::vector<float>{}));
  }

  // Parse elements
  std::vector<float> float_data;
  std::vector<int32_t> int_data;
  std::vector<std::string> string_data;

  while (true) {
    ParseResult elem = ParseValue(lexer, element_type);
    if (!elem.success) {
      return elem;
    }

    // Collect data based on type
    if (element_type == TypeId::Float) {
      if (const float* f = elem.value.as_float()) {
        float_data.push_back(*f);
      }
    } else if (element_type == TypeId::Int) {
      if (const int32_t* i = elem.value.as_int()) {
        int_data.push_back(*i);
      }
    } else if (IsFloat3Like(element_type)) {
      if (const float* f = elem.value.as_float3()) {
        float_data.push_back(f[0]);
        float_data.push_back(f[1]);
        float_data.push_back(f[2]);
      }
    } else if (IsFloat2Like(element_type)) {
      if (const float* f = elem.value.as_float2()) {
        float_data.push_back(f[0]);
        float_data.push_back(f[1]);
      }
    } else if (IsFloat4Like(element_type)) {
      if (const float* f = elem.value.as_float4()) {
        float_data.push_back(f[0]);
        float_data.push_back(f[1]);
        float_data.push_back(f[2]);
        float_data.push_back(f[3]);
      }
    } else if (element_type == TypeId::Token) {
      if (const std::string* s = elem.value.as_token()) {
        string_data.push_back(*s);
      }
    } else if (element_type == TypeId::String) {
      if (const std::string* s = elem.value.as_string()) {
        string_data.push_back(*s);
      }
    }

    // Defense-in-depth: cap a single pathological array's accumulated scalars.
    if (float_data.size() > kMaxArrayScalars ||
        int_data.size() > kMaxArrayScalars ||
        string_data.size() > kMaxArrayScalars) {
      return ParseResult::Error("Array element count exceeds limit");
    }

    // Check for comma or end
    if (lexer.peek().type == TokenType::Comma) {
      lexer.consume();
    } else if (lexer.peek().type == TokenType::CloseBracket) {
      lexer.consume();
      break;
    } else {
      return ParseResult::Error("Expected ',' or ']' in array");
    }
  }

  // Return appropriate array type
  if (element_type == TypeId::Float) {
    return ParseResult::Ok(Value::MakeFloatArray(std::move(float_data)));
  } else if (element_type == TypeId::Int) {
    return ParseResult::Ok(Value::MakeIntArray(std::move(int_data)));
  } else if (IsFloat3Like(element_type)) {
    return ParseResult::Ok(Value::MakeFloat3Array(std::move(float_data)));
  } else if (IsFloat2Like(element_type) || IsFloat4Like(element_type)) {
    return ParseResult::Ok(Value::MakeFloatArray(std::move(float_data)));
  } else if (element_type == TypeId::Token || element_type == TypeId::String) {
    return ParseResult::Ok(Value::MakeTokenArray(std::move(string_data)));
  }

  // Default: try to return float array
  return ParseResult::Ok(Value::MakeFloatArray(std::move(float_data)));
}
