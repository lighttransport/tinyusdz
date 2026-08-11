// Fuzz the host-only next Stage -> Tydra conversion boundary. GPU execution is
// deliberately excluded; this covers traversal, triangulation, packing, and
// allocation planning with deterministic limits.

#include <cstddef>
#include <cstdint>
#include <string>

#include "next/tinyusdz-next.hh"
#include "tydra/next/render-converter.hh"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  constexpr size_t kMaxInputBytes = 1024u * 1024u;
  if (!data || size == 0 || size > kMaxInputBytes) return 0;

  std::string source("#usda 1.0\n");
  source.append(reinterpret_cast<const char*>(data), size);
  tinyusdz::next::LoadResult loaded =
      tinyusdz::next::LoadUSDAFromString(source);
  if (!loaded.success) return 0;

  tinyusdz::tydra::next::ConverterConfig config;
  config.max_render_depth = 64;
  config.max_render_records = 4096;
  config.mesh.sphere_subdivisions = 1;
  config.mesh.compute_tangents = false;
  config.material.load_textures = false;
  config.animation.enabled = false;
  config.animation.bake_value_clips = false;
  tinyusdz::tydra::next::RenderSceneConverter converter(config);
  (void)converter.Convert(loaded.stage);
  return 0;
}
