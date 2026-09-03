// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// LightUSD Next - asset-path anchors.

#include "asset-anchor.hh"

#include <deque>
#include <mutex>
#include <unordered_map>

#if defined(_WIN32)
#include <direct.h>
#elif !defined(__EMSCRIPTEN__)
#include <unistd.h>
#endif

namespace lightusd {
namespace next {

namespace {

bool IsAbsoluteDir(const std::string& p) {
  if (p.empty()) return false;
  if (p[0] == '/' || p[0] == '\\') return true;
  // Windows drive letter ("C:\...").
  return p.size() >= 2 && p[1] == ':';
}

// An anchor must be unambiguous: consumers re-anchor a path they consider
// relative against their OWN base (tusdview's texture decoder uses the scene
// file's directory), so handing them a CWD-relative anchor would double-prefix
// it. Absolutize once, here, and every consumer gets a path it can just open.
std::string AbsolutizeDir(const std::string& dir) {
  if (dir.empty() || IsAbsoluteDir(dir)) return dir;
#if defined(__EMSCRIPTEN__)
  return dir;  // no filesystem; getcwd aborts under -sFILESYSTEM=0
#else
  char buf[4096];
#if defined(_WIN32)
  if (!_getcwd(buf, sizeof(buf))) return dir;
#else
  if (!getcwd(buf, sizeof(buf))) return dir;
#endif
  std::string cwd(buf);
  if (cwd.empty() || cwd == ".") return dir;
  if (cwd.back() == '/' || cwd.back() == '\\') cwd.pop_back();
  return cwd + "/" + dir;
#endif
}

struct AnchorTable {
  std::mutex mu;
  // deque, not vector: AssetAnchorPath hands out a reference to an element and
  // another thread may intern concurrently. deque::push_back keeps references
  // to existing elements valid; vector reallocation would dangle them.
  std::deque<std::string> paths{std::string()};  // index 0 == "no anchor"
  std::unordered_map<std::string, uint32_t> by_path;
};

AnchorTable& Table() {
  static AnchorTable* t = new AnchorTable();  // leaked on purpose: ids outlive
  return *t;                                  // any stage that holds them
}

}  // namespace

uint32_t InternAssetAnchor(const std::string& dir) {
  if (dir.empty()) return 0;

  const std::string abs = AbsolutizeDir(dir);

  AnchorTable& t = Table();
  std::lock_guard<std::mutex> lk(t.mu);
  auto it = t.by_path.find(abs);
  if (it != t.by_path.end()) return it->second;

  const uint32_t id = static_cast<uint32_t>(t.paths.size());
  t.paths.push_back(abs);
  t.by_path.emplace(abs, id);
  return id;
}

const std::string& AssetAnchorPath(uint32_t id) {
  AnchorTable& t = Table();
  std::lock_guard<std::mutex> lk(t.mu);
  if (id == 0 || id >= t.paths.size()) return t.paths[0];  // == ""
  return t.paths[id];
}

}  // namespace next
}  // namespace lightusd
