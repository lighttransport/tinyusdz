// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Memory-rooted layer loading + flatten tests for src/next:
// pcp::LoadLayerFromMemory[Owned] content dispatch (USDA / USDC / USDZ),
// AssetResolver::ReadAsset + pipeline::MakeResolverLayerLoader (no
// filesystem), and pipeline::FlattenUSDMemoryToUSDCOwned for USDA roots with
// USDA dependency layers.

#include "next/pcp/layer-registry.hh"
#include "next/pipeline/flatten.hh"
#include "next/reader/usda-reader.hh"
#include "next/reader/usdc-reader.hh"
#include "next/resolver/asset-resolver.hh"
#include "next/writer/usdc-writer.hh"
#include "next/writer/usdz-writer.hh"

#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace tinyusdz::next;

static int g_fail = 0;
#define CHECK(cond, msg)                                                    \
  do {                                                                      \
    if (!(cond)) { std::cerr << "  FAIL: " << msg << "\n"; ++g_fail; }      \
    else { std::cout << "  ok: " << msg << "\n"; }                          \
  } while (0)

static const char* kBaseUSDA = R"(#usda 1.0

def Xform "Base"
{
    int imported = 42

    def Mesh "Geo"
    {
        int n = 3
    }
}
)";

static const char* kRootUSDA = R"(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root" (
    references = @dep.usda@</Base>
)
{
    double localOnly = 1.0
}
)";

static std::vector<uint8_t> MakeCrateBytes(const char* usda_text) {
  LoadResult r = LoadUSDAFromString(usda_text, std::strlen(usda_text));
  if (!r.success) return {};
  std::vector<uint8_t> buf;
  USDCWriteResult wr = WriteUSDCToMemory(buf, r.stage);
  if (!wr.success) return {};
  return buf;
}

// LoadLayerFromMemory dispatches USDA text / crate magic / ZIP header.
static void test_load_layer_from_memory_dispatch() {
  std::cout << "[LoadLayerFromMemory dispatch]\n";
  std::string warn, err;

  // USDA text
  {
    std::shared_ptr<Layer> l = pcp::LoadLayerFromMemory(
        "mem.usda", reinterpret_cast<const uint8_t*>(kBaseUSDA),
        std::strlen(kBaseUSDA), &warn, &err);
    CHECK(l != nullptr, "USDA text parses");
    if (l) {
      l->build_path_index();
      CHECK(l->prim_at_path("/Base/Geo") != nullptr, "USDA layer has prims");
    }
  }

  // Crate bytes
  {
    std::vector<uint8_t> crate = MakeCrateBytes(kBaseUSDA);
    CHECK(!crate.empty(), "crate fixture built");
    err.clear();
    std::shared_ptr<Layer> l = pcp::LoadLayerFromMemory(
        "mem.usdc", crate.data(), crate.size(), &warn, &err);
    CHECK(l != nullptr, "crate bytes parse");
    if (l) {
      l->build_path_index();
      CHECK(l->prim_at_path("/Base") != nullptr, "crate layer has prims");
    }
  }

  // USDZ bytes (crate entry inside zip)
  {
    std::vector<uint8_t> crate = MakeCrateBytes(kBaseUSDA);
    std::vector<uint8_t> zip;
    USDZWriteResult zr =
        WriteUSDZFromUSDCToMemory(zip, crate.data(), crate.size());
    CHECK(zr.success, "usdz fixture built");
    err.clear();
    std::shared_ptr<Layer> l = pcp::LoadLayerFromMemory(
        "mem.usdz", zip.data(), zip.size(), &warn, &err);
    CHECK(l != nullptr, "usdz bytes parse");
    if (l) {
      l->build_path_index();
      CHECK(l->prim_at_path("/Base") != nullptr, "usdz layer has prims");
    }
  }

  // Owned variant (USDA)
  {
    err.clear();
    std::shared_ptr<Layer> l = pcp::LoadLayerFromMemoryOwned(
        "mem.usda", std::string(kBaseUSDA), &warn, &err);
    CHECK(l != nullptr, "owned USDA parses");
  }

  // Empty buffer fails cleanly
  {
    err.clear();
    std::shared_ptr<Layer> l =
        pcp::LoadLayerFromMemory("empty", nullptr, 0, &warn, &err);
    CHECK(l == nullptr && !err.empty(), "empty buffer rejected with error");
  }
}

// A resolver whose bytes come from an in-memory map (no filesystem), driving
// a flatten of a USDA root with a USDA dependency — the wasm/HTTP shape.
static void test_resolver_layer_loader_usda_dep() {
  std::cout << "[MakeResolverLayerLoader + USDA dependency]\n";

  std::map<std::string, std::string> assets;
  assets["dep.usda"] = kBaseUSDA;

  AssetResolver resolver;
  resolver.SetCustomResolver(
      [](const std::string& asset_path, const std::string& anchor) {
        (void)anchor;
        return asset_path;  // identity: keys are asset paths
      });
  resolver.SetAssetReader([&assets](const std::string& resolved_path,
                                    std::vector<uint8_t>* out,
                                    std::string* err) {
    auto it = assets.find(resolved_path);
    if (it == assets.end()) {
      if (err) *err += "no such asset: " + resolved_path + "\n";
      return false;
    }
    out->assign(it->second.begin(), it->second.end());
    return true;
  });

  pipeline::FlattenOptions opts;
  opts.resolver = &resolver;
  opts.layer_loader = pipeline::MakeResolverLayerLoader(&resolver);
  opts.fail_on_composition_error = true;

  std::vector<uint8_t> out;
  pipeline::FlattenStats stats;
  std::string err;
  bool ok = pipeline::FlattenUSDMemoryToUSDCOwned(
      "root.usda", std::string(kRootUSDA), out, opts, &stats, &err);
  CHECK(ok, ("flatten with USDA dep succeeds: " + err).c_str());
  CHECK(!out.empty(), "flatten produced crate bytes");
  CHECK(stats.composition_errors.empty(), "no composition errors");

  if (ok && !out.empty()) {
    USDCLoadResult rr = LoadUSDCFromMemory(out.data(), out.size());
    CHECK(rr.success, "flattened crate parses back");
    if (rr.success) {
      std::shared_ptr<Layer> l = rr.stage.ReleaseRootLayer();
      l->build_path_index();
      const PrimSpec* root = l->prim_at_path("/Root");
      CHECK(root != nullptr, "composed /Root exists");
      CHECK(root && root->property_value("imported") != nullptr,
            "referenced USDA opinion composed onto /Root");
      CHECK(root && root->property_value("localOnly") != nullptr,
            "local opinion preserved");
      CHECK(l->prim_at_path("/Root/Geo") != nullptr,
            "referenced USDA subtree grafted");
    }
  }
}

// Crate roots keep the lazy passthrough path.
static void test_memory_flatten_crate_root() {
  std::cout << "[FlattenUSDMemoryToUSDCOwned crate root]\n";
  std::vector<uint8_t> crate = MakeCrateBytes(kBaseUSDA);
  CHECK(!crate.empty(), "crate fixture built");

  std::vector<uint8_t> out;
  std::string err;
  bool ok = pipeline::FlattenUSDMemoryToUSDCOwned(
      "root.usdc",
      std::string(reinterpret_cast<const char*>(crate.data()), crate.size()),
      out, {}, nullptr, &err);
  CHECK(ok, ("crate root flatten succeeds: " + err).c_str());
  if (ok) {
    USDCLoadResult rr = LoadUSDCFromMemory(out.data(), out.size());
    CHECK(rr.success, "crate root output parses back");
    if (rr.success) {
      std::shared_ptr<Layer> l = rr.stage.ReleaseRootLayer();
      l->build_path_index();
      CHECK(l->prim_at_path("/Base/Geo") != nullptr,
            "crate root prims survive");
    }
  }
}

// Missing dependency must fail (not silently emit partial output) when
// fail_on_composition_error is set — the resumable/need-layer contract.
static void test_missing_dep_fails() {
  std::cout << "[missing dependency fails hard]\n";

  AssetResolver resolver;
  resolver.SetCustomResolver(
      [](const std::string& asset_path, const std::string&) {
        return asset_path;
      });
  resolver.SetAssetReader([](const std::string& resolved_path,
                             std::vector<uint8_t>*, std::string* err) {
    if (err) *err += "NEED_LAYER:" + resolved_path + "\n";
    return false;
  });

  pipeline::FlattenOptions opts;
  opts.resolver = &resolver;
  opts.layer_loader = pipeline::MakeResolverLayerLoader(&resolver);
  opts.fail_on_composition_error = true;

  std::vector<uint8_t> out;
  std::string err;
  bool ok = pipeline::FlattenUSDMemoryToUSDCOwned(
      "root.usda", std::string(kRootUSDA), out, opts, nullptr, &err);
  CHECK(!ok, "flatten fails when dependency bytes are unavailable");
  CHECK(err.find("dep.usda") != std::string::npos ||
            err.find("NEED_LAYER") != std::string::npos,
        "error names the missing layer");
}

int main() {
  test_load_layer_from_memory_dispatch();
  test_resolver_layer_loader_usda_dep();
  test_memory_flatten_crate_root();
  test_missing_dep_fails();

  if (g_fail) {
    std::cerr << "\n" << g_fail << " memory-flatten check(s) FAILED\n";
    return 1;
  }
  std::cout << "\nAll memory-flatten checks passed.\n";
  return 0;
}
