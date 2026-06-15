// SPDX-License-Identifier: Apache 2.0
// Copyright 2025-Present Light Transport Entertainment, Inc.
//
// tusddiff — USD Layer Diff Tool
//
// Usage:
//   tusddiff file1.usd file2.usd
//   tusddiff --json file1.usd file2.usd
//   tusddiff --help
//
// Exit codes:
//   0 = no differences found
//   1 = differences found
//   2 = error (file not found, parse failure, etc.)
//

#include <algorithm>
#include <iostream>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tinyusdz.hh"
#include "layer.hh"
#include "tydra/diff-and-compare.hh"

namespace {

// ============================================================================
// --fast: a structural BYTE-LEVEL diff that never parses values.
//
// Both files are memory-mapped (page cache, ~no anonymous RSS), scanned into a
// tree of prim byte-ranges by INDENTATION (machine-written USDA puts each prim's
// `{`/`}` on its own line at the prim's indent, and children are deeper-indented,
// so the first `<indent>}` after a prim's `<indent>{` is unambiguously its
// close). Byte-identical prim subtrees are skipped with a single memcmp — so on
// two mostly-identical flattens the bulk is never touched. Only the prim paths
// that actually differ are reported (with a line-level diff of the prim's own
// content). Fast + low-memory; text-level (no ULP / no instance canonicalization).
// ============================================================================

struct MMapFile {
  const char *p = nullptr;
  size_t n = 0;
  int fd = -1;
};

bool mmap_open(const std::string &path, MMapFile &m) {
  m.fd = ::open(path.c_str(), O_RDONLY);
  if (m.fd < 0) return false;
  struct stat st {};
  if (::fstat(m.fd, &st) != 0 || st.st_size < 0) {
    ::close(m.fd);
    m.fd = -1;
    return false;
  }
  m.n = static_cast<size_t>(st.st_size);
  if (m.n == 0) {
    m.p = "";
    return true;
  }
  void *a = ::mmap(nullptr, m.n, PROT_READ, MAP_PRIVATE, m.fd, 0);
  if (a == MAP_FAILED) {
    ::close(m.fd);
    m.fd = -1;
    return false;
  }
  ::madvise(a, m.n, MADV_SEQUENTIAL);
  m.p = static_cast<const char *>(a);
  return true;
}

void mmap_close(MMapFile &m) {
  if (m.p && m.n) ::munmap(const_cast<char *>(m.p), m.n);
  if (m.fd >= 0) ::close(m.fd);
  m.p = nullptr;
  m.n = 0;
  m.fd = -1;
}

struct FBlock {
  std::string name;          // prim name (the quoted element name)
  size_t blk0 = 0, blk1 = 0; // whole block bytes [header line start, after `}` line)
  size_t open1 = 0, close0 = 0; // body content range [after `{` line, before `}` line)
  std::vector<FBlock> kids;
};

// A def/over/class prim header line (after the leading indent).
bool is_prim_kw(const char *p, size_t s, size_t le) {
  auto kw = [&](const char *k) {
    size_t n = std::strlen(k);
    return (s + n <= le) && std::memcmp(p + s, k, n) == 0;
  };
  return kw("def ") || kw("over ") || kw("class ");
}

// The element name = the last quoted "..." on the header text [s, le).
std::string extract_name(const char *p, size_t s, size_t le) {
  size_t q1 = std::string::npos, q0 = std::string::npos;
  for (size_t i = s; i < le; ++i) {
    if (p[i] == '"') {
      if (q0 == std::string::npos || q1 != std::string::npos) {
        q0 = i;
        q1 = std::string::npos;
      } else {
        q1 = i;
      }
    }
  }
  if (q0 != std::string::npos && q1 != std::string::npos && q1 > q0 + 1)
    return std::string(p + q0 + 1, q1 - q0 - 1);
  return std::string();
}

// True if line [off,le) is exactly `indent` spaces followed by `c` (+ optional
// trailing spaces / CR).
bool is_brace_line(const char *p, size_t off, size_t le, int indent, char c) {
  if (static_cast<size_t>(indent) >= le - off) return false;
  for (int i = 0; i < indent; ++i)
    if (p[off + i] != ' ') return false;
  size_t i = off + indent;
  if (i >= le || p[i] != c) return false;
  for (++i; i < le; ++i)
    if (p[i] != ' ' && p[i] != '\r') return false;
  return true;
}

// Scan child prims at `indent` within [begin,end). `*ok=false` on a structural
// surprise (caller falls back to the semantic diff).
std::vector<FBlock> scan_prims(const char *p, size_t begin, size_t end,
                               int indent, bool *ok) {
  std::vector<FBlock> out;
  size_t off = begin;
  while (off < end) {
    size_t le = off;
    while (le < end && p[le] != '\n') ++le;
    size_t next = (le < end) ? le + 1 : end;
    int ind = 0;
    size_t s = off;
    while (s < le && p[s] == ' ') { ++s; ++ind; }
    if (ind != indent || !is_prim_kw(p, s, le)) { off = next; continue; }

    FBlock b;
    b.name = extract_name(p, s, le);
    b.blk0 = off;
    // Find this prim's body-open `<indent>{` (skipping any `( ... )` metadata).
    size_t l = next, openEnd = std::string::npos;
    while (l < end) {
      size_t l2 = l;
      while (l2 < end && p[l2] != '\n') ++l2;
      if (is_brace_line(p, l, l2, indent, '{')) {
        openEnd = (l2 < end) ? l2 + 1 : end;
        break;
      }
      // Another prim or a dedent before `{` => malformed for our purposes.
      int ind2 = 0; size_t s2 = l;
      while (s2 < l2 && p[s2] == ' ') { ++s2; ++ind2; }
      if (s2 < l2 && ind2 <= indent && is_prim_kw(p, s2, l2)) { if (ok) *ok = false; return out; }
      l = (l2 < end) ? l2 + 1 : end;
    }
    if (openEnd == std::string::npos) { if (ok) *ok = false; return out; }
    b.open1 = openEnd;
    // Find body-close `<indent>}` (children are deeper-indented; first match wins).
    size_t c = openEnd, closeStart = std::string::npos, closeEnd = std::string::npos;
    while (c < end) {
      size_t c2 = c;
      while (c2 < end && p[c2] != '\n') ++c2;
      if (is_brace_line(p, c, c2, indent, '}')) {
        closeStart = c;
        closeEnd = (c2 < end) ? c2 + 1 : end;
        break;
      }
      c = (c2 < end) ? c2 + 1 : end;
    }
    if (closeStart == std::string::npos) { if (ok) *ok = false; return out; }
    b.close0 = closeStart;
    b.blk1 = closeEnd;
    b.kids = scan_prims(p, openEnd, closeStart, indent + 4, ok);
    if (ok && !*ok) return out;
    out.push_back(std::move(b));
    off = closeEnd;
  }
  return out;
}

bool bytes_equal(const char *a, size_t a0, size_t a1, const char *b, size_t b0,
                 size_t b1) {
  return (a1 - a0) == (b1 - b0) && std::memcmp(a + a0, b + b0, a1 - a0) == 0;
}

// Lines of a prim block EXCLUDING its child sub-blocks (the prim's own content:
// header + property lines).
std::vector<std::string> own_lines(const char *p, const FBlock &b) {
  std::vector<std::string> out;
  size_t off = b.blk0;
  size_t ki = 0;
  while (off < b.blk1) {
    if (ki < b.kids.size() && off == b.kids[ki].blk0) {
      off = b.kids[ki].blk1;
      ++ki;
      continue;
    }
    size_t le = off;
    while (le < b.blk1 && p[le] != '\n') ++le;
    out.emplace_back(p + off, le - off);
    off = (le < b.blk1) ? le + 1 : b.blk1;
  }
  return out;
}

// Recursively diff two matched prim blocks (same path). Returns true if any
// difference was reported under `path`.
bool diff_block(const char *pa, const FBlock &a, const char *pb, const FBlock &b,
                const std::string &path, std::ostream &os) {
  if (bytes_equal(pa, a.blk0, a.blk1, pb, b.blk0, b.blk1)) return false;

  // Children by name.
  std::vector<const FBlock *> amiss, bmiss;
  std::vector<std::pair<const FBlock *, const FBlock *>> both;
  std::vector<bool> bused(b.kids.size(), false);
  for (const auto &ak : a.kids) {
    const FBlock *m = nullptr;
    for (size_t j = 0; j < b.kids.size(); ++j)
      if (!bused[j] && b.kids[j].name == ak.name) { m = &b.kids[j]; bused[j] = true; break; }
    if (m) both.push_back({&ak, m}); else amiss.push_back(&ak);
  }
  for (size_t j = 0; j < b.kids.size(); ++j)
    if (!bused[j]) bmiss.push_back(&b.kids[j]);

  bool any = false;
  // The prim's OWN content (excluding children).
  auto la = own_lines(pa, a);
  auto lb = own_lines(pb, b);
  if (la != lb) {
    any = true;
    os << "~ " << path << " (modified)\n";
    if (la.size() == lb.size()) {
      // Same line count (the common case: one value changed) -> positional,
      // centered on the differing region so huge arrays do not flood output.
      for (size_t i = 0; i < la.size(); ++i) {
        if (la[i] == lb[i]) continue;
        auto pr = tinyusdz::tydra::CenterValuePairForDiff(la[i], lb[i]);
        os << "  - " << pr.first << "\n  + " << pr.second << "\n";
      }
    } else {
      std::vector<std::string> sb(lb.begin(), lb.end());
      for (const auto &x : la)
        if (std::find(sb.begin(), sb.end(), x) == sb.end()) os << "  - " << x << "\n";
      std::vector<std::string> sa(la.begin(), la.end());
      for (const auto &x : lb)
        if (std::find(sa.begin(), sa.end(), x) == sa.end()) os << "  + " << x << "\n";
    }
  }
  for (const auto *d : amiss) { os << "- " << path << "/" << d->name << " (deleted)\n"; any = true; }
  for (const auto *d : bmiss) { os << "+ " << path << "/" << d->name << " (added)\n"; any = true; }
  for (const auto &pr : both)
    any |= diff_block(pa, *pr.first, pb, *pr.second, path + "/" + pr.first->name, os);
  return any;
}

// Returns: 0 no diffs, 1 diffs, -1 fall back to the semantic path.
int fast_diff(const std::string &f1, const std::string &f2, std::ostream &os) {
  MMapFile m1, m2;
  if (!mmap_open(f1, m1) || !mmap_open(f2, m2)) {
    mmap_close(m1);
    mmap_close(m2);
    return -1;
  }
  int rc = 1;
  if (bytes_equal(m1.p, 0, m1.n, m2.p, 0, m2.n)) {
    rc = 0;
  } else {
    bool ok = true;
    auto t1 = scan_prims(m1.p, 0, m1.n, 0, &ok);
    auto t2 = ok ? scan_prims(m2.p, 0, m2.n, 0, &ok) : std::vector<FBlock>();
    if (!ok) {
      rc = -1;  // structural surprise -> semantic fallback
    } else {
      os << "--- " << f1 << "\n+++ " << f2 << "\n";
      bool any = false;
      std::vector<bool> used(t2.size(), false);
      for (const auto &a : t1) {
        const FBlock *m = nullptr;
        for (size_t j = 0; j < t2.size(); ++j)
          if (!used[j] && t2[j].name == a.name) { m = &t2[j]; used[j] = true; break; }
        if (m) any |= diff_block(m1.p, a, m2.p, *m, "/" + a.name, os);
        else { os << "- /" << a.name << " (deleted)\n"; any = true; }
      }
      for (size_t j = 0; j < t2.size(); ++j)
        if (!used[j]) { os << "+ /" << t2[j].name << " (added)\n"; any = true; }
      rc = any ? 1 : 0;
    }
  }
  mmap_close(m1);
  mmap_close(m2);
  return rc;
}

void print_usage() {
  std::cout << "tusddiff — USD Layer Diff Tool\n";
  std::cout << "\n";
  std::cout << "USAGE:\n";
  std::cout << "  tusddiff [OPTIONS] <file1> <file2>\n";
  std::cout << "\n";
  std::cout << "OPTIONS:\n";
  std::cout << "  --json      Output diff in JSON format\n";
  std::cout << "  --quiet     Suppress diff output, exit code only\n";
  std::cout << "  --ulps N    Floating-point tolerance in ULPs (default 1).\n";
  std::cout << "              Absorbs ~1 ULP rounding (e.g. pxr quaternion/xform).\n";
  std::cout << "              Use 0 for bitwise-exact comparison.\n";
  std::cout << "  --eps F     Absolute floating-point tolerance (overrides/ORs ULP).\n";
  std::cout << "  --no-meta   Do not compare metadata (attr/prim/layer).\n";
  std::cout << "  --no-canonicalize-instances\n";
  std::cout << "              Disable content-matching of flatten prototypes.\n";
  std::cout << "              (Default ON: /Flattened_Prototype_N prims are matched\n";
  std::cout << "              by content, so non-deterministic numbering does not\n";
  std::cout << "              produce spurious added/deleted/modified diffs.)\n";
  std::cout << "  --low-mem   Strip large arrays to content fingerprints before\n";
  std::cout << "              diffing: each file is processed before the next is\n";
  std::cout << "              loaded, so peak memory ~halves and big-array compares\n";
  std::cout << "              are faster. Large arrays compare EXACTLY (no ULP);\n";
  std::cout << "              scalars / small vectors keep ULP tolerance.\n";
  std::cout << "  --fast      Structural byte-level diff: memory-map both files,\n";
  std::cout << "              skip byte-identical prim subtrees, report only the\n";
  std::cout << "              prim paths that differ (with a line-level diff). No\n";
  std::cout << "              value parsing -> fastest + lowest memory, but\n";
  std::cout << "              text-level (no ULP tolerance, no instance\n";
  std::cout << "              canonicalization). Best for same-tool/same-format\n";
  std::cout << "              comparisons; falls back to the full diff if the USDA\n";
  std::cout << "              structure is unexpected.\n";
  std::cout << "  --help      Show this help message\n";
  std::cout << "  -h          Show this help message\n";
  std::cout << "\n";
  std::cout << "EXIT CODES:\n";
  std::cout << "  0  No differences found\n";
  std::cout << "  1  Differences found\n";
  std::cout << "  2  Error (file not found, parse failure, etc.)\n";
  std::cout << "\n";
  std::cout << "EXAMPLES:\n";
  std::cout << "  tusddiff old.usd new.usd\n";
  std::cout << "  tusddiff --json scene1.usda scene2.usda\n";
  std::cout << "  tusddiff --quiet model.usda model.usdc\n";
  std::cout << "\n";
  std::cout << "SUPPORTED FORMATS:\n";
  std::cout << "  .usd, .usda, .usdc, .usdz\n";
}

} // namespace

int main(int argc, char **argv) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) {
    args.push_back(std::string(argv[i]));
  }

  bool json_output = false;
  bool quiet = false;
  // Match flatten prototypes by CONTENT (default on) so non-deterministic
  // /Flattened_Prototype_N numbering does not produce spurious diffs.
  bool canonicalize_instances = true;
  // Strip large arrays to content fingerprints (lower peak RSS + faster). Big
  // arrays then compare EXACTLY (no ULP), so it is opt-in.
  bool low_mem = false;
  // Structural byte-level diff (mmap, no value parsing): fast + low memory.
  // Text-level (no ULP, no instance canonicalization).
  bool fast = false;
  std::string file1, file2;
  tinyusdz::tydra::DiffOptions diff_opts;

  // Parse command line arguments
  for (size_t i = 0; i < args.size(); i++) {
    if (args[i] == "--help" || args[i] == "-h") {
      print_usage();
      return 0;
    } else if (args[i] == "--json") {
      json_output = true;
    } else if (args[i] == "--quiet") {
      quiet = true;
    } else if (args[i] == "--no-meta") {
      diff_opts.compareMetadata = false;
    } else if (args[i] == "--canonicalize-instances") {
      canonicalize_instances = true;
    } else if (args[i] == "--no-canonicalize-instances") {
      canonicalize_instances = false;
    } else if (args[i] == "--low-mem") {
      low_mem = true;
    } else if (args[i] == "--fast") {
      fast = true;
    } else if (args[i] == "--ulps") {
      if (i + 1 >= args.size()) {
        std::cerr << "Error: --ulps requires a value\n";
        return 2;
      }
      try {
        unsigned long u = std::stoul(args[++i]);
        diff_opts.floatUlps = static_cast<uint32_t>(u);
        diff_opts.doubleUlps = static_cast<uint64_t>(u);
      } catch (...) {
        std::cerr << "Error: --ulps value must be a non-negative integer\n";
        return 2;
      }
    } else if (args[i] == "--eps") {
      if (i + 1 >= args.size()) {
        std::cerr << "Error: --eps requires a value\n";
        return 2;
      }
      try {
        diff_opts.absEps = std::stod(args[++i]);
      } catch (...) {
        std::cerr << "Error: --eps value must be a number\n";
        return 2;
      }
    } else if (file1.empty()) {
      file1 = args[i];
    } else if (file2.empty()) {
      file2 = args[i];
    } else {
      std::cerr << "Error: Too many arguments\n";
      print_usage();
      return 2;
    }
  }

  if (file1.empty() || file2.empty()) {
    std::cerr << "Error: Please specify two USD files to compare\n";
    print_usage();
    return 2;
  }

  // --fast: structural byte-level diff (no value parsing). Falls back to the
  // full semantic diff on a structural surprise / unreadable file.
  if (fast) {
    std::string out;
    {
      std::ostringstream ss;
      int rc = fast_diff(file1, file2, ss);
      if (rc >= 0) {
        if (!quiet) {
          if (rc == 1) std::cout << ss.str();
          else std::cout << "No differences found." << std::endl;
        }
        return rc;
      }
    }
    std::cerr << "[tusddiff] --fast inconclusive (structural surprise); "
                 "using full diff.\n";
  }

  // Load both USD files as Layers (preserves full PrimSpec tree). Each file is
  // canonicalized (and, in --low-mem, its big arrays stripped to fingerprints)
  // right after load, BEFORE the next file is loaded — so the two full layers
  // never coexist and peak RSS roughly halves.
  tinyusdz::Layer layer1, layer2;
  std::string warn, err;

  auto prepare = [&](tinyusdz::Layer &layer) {
    // Canonicalize instance-flatten prototypes by content so non-deterministic
    // /Flattened_Prototype_N numbering does not show up as spurious diffs.
    if (canonicalize_instances) tinyusdz::tydra::CanonicalizeInstances(layer);
    if (low_mem) tinyusdz::tydra::StripLargeArrays(layer);
  };

  if (!tinyusdz::LoadLayerFromFile(file1, &layer1, &warn, &err)) {
    std::cerr << "Error loading " << file1 << ": " << err << std::endl;
    return 2;
  }
  if (!warn.empty()) {
    std::cerr << "Warning loading " << file1 << ": " << warn << std::endl;
  }
  prepare(layer1);

  warn.clear();
  err.clear();

  if (!tinyusdz::LoadLayerFromFile(file2, &layer2, &warn, &err)) {
    std::cerr << "Error loading " << file2 << ": " << err << std::endl;
    return 2;
  }
  if (!warn.empty()) {
    std::cerr << "Warning loading " << file2 << ": " << warn << std::endl;
  }
  prepare(layer2);

  // Perform diff
  tinyusdz::HashMap<std::string, tinyusdz::tydra::PrimSpecDiff> psDiffs;
  tinyusdz::HashMap<std::string, tinyusdz::tydra::PropDiff> propDiffs;
  tinyusdz::tydra::LayerMetaDiff layerMetaDiff;
  tinyusdz::tydra::Diff(layer1, layer2, psDiffs, propDiffs, diff_opts,
                        &layerMetaDiff);

  bool has_diffs =
      !psDiffs.empty() || !propDiffs.empty() || layerMetaDiff.changed();

  if (!quiet) {
    if (json_output) {
      std::string jsonDiff =
          tinyusdz::tydra::DiffToJSON(layer1, layer2, file1, file2, diff_opts);
      std::cout << jsonDiff;
    } else {
      if (has_diffs) {
        std::string textDiff =
            tinyusdz::tydra::DiffToText(layer1, layer2, file1, file2, diff_opts);
        std::cout << textDiff;
      } else {
        std::cout << "No differences found." << std::endl;
      }
    }
  }

  return has_diffs ? 1 : 0;
}
