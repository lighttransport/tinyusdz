// Regression test: a tstring_view's backing storage survives WASM linear-memory
// growth.
//
// tinyusdz::Path stores its parts as tstring_view slices into a single owning
// std::string buffer. Under Emscripten with -sALLOW_MEMORY_GROWTH=1 the heap can
// grow at any allocation. wasm32 `memory.grow` is APPEND-ONLY: it extends the
// single linear memory and never relocates existing allocations, so a
// tstring_view's `const char*` (a linear-memory offset into a std::string)
// stays valid across growth. This test forces a large growth while holding views
// and verifies they still read the original bytes.
//
// Build & run: web/tests/run-wasm-stringview-heapgrowth.sh
// (compiles with em++ -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=16MB and runs in
// node; exits non-zero on failure.)

#include "tiny-string.hh"

#include <cstdio>
#include <string>
#include <vector>

using namespace tinyusdz;

int main() {
  // Backing std::string, long enough to be heap-allocated (not SSO).
  std::string backing =
      std::string("/Model/Very/Long/Path/Exceeding/SSO/Buffer/Mesh") + ".points";
  const size_t dot = backing.find('.');
  const tstring_view prim(backing.data(), dot);
  const tstring_view prop(backing.data() + dot + 1, backing.size() - dot - 1);

  const char *prim_ptr_before = prim.c_str();
  const std::string prim_expected = backing.substr(0, dot);

  // Force linear-memory growth: allocate & touch ~512 MB in big blocks.
  std::vector<std::vector<char>> blocks;
  size_t grew = 0;
  for (int i = 0; i < 64; i++) {
    blocks.emplace_back(size_t(8) * 1024 * 1024, char('A' + (i % 26)));
    blocks.back()[0] = 'Z';  // touch first/last pages
    blocks.back().back() = 'Y';
    grew += blocks.back().size();
  }

  // After growth: the backing string was NOT moved, so the views still read the
  // original bytes, the pointer is stable, and size-aware ops are correct.
  const bool ptr_stable = (prim.c_str() == prim_ptr_before);
  const bool prim_ok = (prim == prim_expected);
  const bool prop_ok = (prop == "points");
  const bool starts_ok = prim.starts_with("/Model");
  const bool contains_ok = prim.contains("/SSO/");

  std::printf(
      "grew=%zuMB ptr_stable=%d prim_ok=%d prop_ok=%d starts=%d contains=%d\n",
      grew / (1024 * 1024), ptr_stable, prim_ok, prop_ok, starts_ok,
      contains_ok);
  std::printf("prim='%.*s' prop='%.*s'\n", (int)prim.size(), prim.c_str(),
              (int)prop.size(), prop.c_str());

  const bool ok = ptr_stable && prim_ok && prop_ok && starts_ok && contains_ok;
  std::printf("%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
