// SPDX-License-Identifier: Apache-2.0
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "next/lightusd-next.hh"
#include "preview_cache.hh"

namespace fs = std::filesystem;

int main() {
  const fs::path base = fs::temp_directory_path() / "tusdview-preview-cache-test";
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base, ec);
  assert(!ec);
  const fs::path root = base / "root.usda";
  const fs::path dependency = base / "dependency.usda";
  {
    std::ofstream output(root);
    output << R"(#usda 1.0
def Cube "Bound" {
  float3[] extent = [(-1, -1, -1), (1, 1, 1)]
}
)";
  }
  {
    std::ofstream output(dependency);
    output << "#usda 1.0\ndef Scope \"Dependency\" {}\n";
  }

  lightusd::next::Stage preview;
  std::string warn, err;
  assert(lightusd::next::LoadUSDA(root.string(), &preview, &warn, &err));

  tusdview::PreviewCacheOptions options;
  options.mode = tusdview::PreviewCacheMode::Auto;
  options.directory = (base / "cache").string();
  options.maxBytes = size_t(16) << 20;
  const std::string fingerprint = "payload=all;time=default";
  const std::vector<std::string> dependencies = {
      root.string(), dependency.string()};
  std::string reason;
  assert(tusdview::StorePreviewCache(options, root.string(), fingerprint,
                                     preview, dependencies, &reason));

  tusdview::PreviewCacheLookup hit = tusdview::LoadPreviewCache(
      options, root.string(), fingerprint);
  assert(hit.hit);
  assert(hit.stage.GetPrimAtPath("/Bound").IsValid());

  tusdview::PreviewCacheLookup different = tusdview::LoadPreviewCache(
      options, root.string(), fingerprint + ";variant=high");
  assert(!different.hit);

  options.mode = tusdview::PreviewCacheMode::Refresh;
  tusdview::PreviewCacheLookup refresh = tusdview::LoadPreviewCache(
      options, root.string(), fingerprint);
  assert(!refresh.hit && refresh.reason == "refresh requested");
  options.mode = tusdview::PreviewCacheMode::Auto;

  // Force metadata invalidation without relying on filesystem timestamp
  // resolution or changing the fixture's semantic shape.
  const auto old_time = fs::last_write_time(dependency, ec);
  assert(!ec);
  fs::last_write_time(dependency, old_time + std::chrono::seconds(2), ec);
  assert(!ec);
  tusdview::PreviewCacheLookup stale = tusdview::LoadPreviewCache(
      options, root.string(), fingerprint);
  assert(!stale.hit);
  assert(stale.reason.find("dependency changed") != std::string::npos);

  const std::string key =
      tusdview::PreviewCacheFingerprint(root.string(), fingerprint);
  {
    std::ofstream output(fs::path(options.directory) / (key + ".json"),
                         std::ios::trunc);
    output << "{";
  }
  tusdview::PreviewCacheLookup malformed = tusdview::LoadPreviewCache(
      options, root.string(), fingerprint);
  assert(!malformed.hit && malformed.reason == "manifest malformed");

  fs::remove_all(base, ec);
  std::cout << "PASS: preview cache hit and invalidation\n";
  return 0;
}
