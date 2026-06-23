// SPDX-License-Identifier: Apache-2.0
// tusdrender — shared scene/render data types (textures, lights/IBL, camera,
// options, direct-primitive + BVH/instance records, volumes, mesh/curve jobs).
// Includes tusdr_math.hh; the RenderContext aggregator lives in tusdr_context.hh.
#pragma once

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "tusdr_math.hh"
#include "tinyusdz.hh"
#include "value-types.hh"
#include "xform.hh"
#include "mmap-array-ref.hh"
#include "tydra/render-data.hh"
#include "next/prim/path.hh"
#include "next/schema/geom-mesh.hh"
#include "next/reader/usdz-reader.hh"
#include "next/stage/stage.hh"
#include "next/tinyusdz-next.hh"
#include "next/types/value.hh"
#include "tydra/next/scene-access.hh"
extern "C" {
#include "lightrt_c_tri.h"
}

namespace tusdr {

using tinyusdz::value::color3f;
using tinyusdz::value::float3;
using tinyusdz::value::matrix4d;
using tinyusdz::tydra::Node;
using tinyusdz::tydra::NodeType;
using tinyusdz::tydra::RenderCamera;
using tinyusdz::tydra::RenderLight;
using tinyusdz::tydra::RenderMaterial;
using tinyusdz::tydra::RenderMesh;
using tinyusdz::tydra::RenderScene;

enum class WrapMode { Repeat, Clamp, Mirror, Black };

struct Texture {
  int width{0}, height{0}, channels{0};
  std::vector<uint8_t> pixels;  // mip 0: width*height*channels, top-left
  // Box-filtered mip chain (levels 1..N); level k halves the previous. Built on
  // load so ray-differential LOD can avoid texture minification aliasing.
  struct Mip {
    int w, h;
    std::vector<uint8_t> data;
  };
  std::vector<Mip> mips;
  WrapMode wrap_s{WrapMode::Repeat};
  WrapMode wrap_t{WrapMode::Repeat};
  bool srgb{true};  // sourceColorSpace: decode sRGB->linear when sampled
  // UsdUVTexture inputs:scale / inputs:bias (applied post-sample by the caller,
  // e.g. (2,2,2)/(-1,-1,-1) to unpack a [0,1] normal map to [-1,1]).
  Vec3 scale{1.0f, 1.0f, 1.0f};
  Vec3 bias{0.0f, 0.0f, 0.0f};

  static float ApplyWrap(float x, WrapMode m, bool *in_bounds) {
    *in_bounds = true;
    switch (m) {
      case WrapMode::Repeat:
        return x - std::floor(x);
      case WrapMode::Clamp:
        return std::min(1.0f, std::max(0.0f, x));
      case WrapMode::Mirror: {
        float t = std::fabs(x);
        int k = int(std::floor(t));
        float f = t - float(k);
        return (k & 1) ? (1.0f - f) : f;
      }
      case WrapMode::Black:
        if (x < 0.0f || x > 1.0f) *in_bounds = false;
        return std::min(1.0f, std::max(0.0f, x));
    }
    return x - std::floor(x);
  }
  static float SrgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f
                         : std::pow((c + 0.055f) / 1.055f, 2.4f);
  }

  // Box-filter mip 0 down to 1x1 (filtered in the stored space).
  void build_mips() {
    mips.clear();
    int sw = width, sh = height;
    const uint8_t *src = pixels.data();
    while (sw > 1 || sh > 1) {
      int dw = std::max(1, sw / 2), dh = std::max(1, sh / 2);
      Mip m;
      m.w = dw;
      m.h = dh;
      m.data.resize(size_t(dw) * size_t(dh) * size_t(channels));
      for (int y = 0; y < dh; ++y) {
        int y0 = std::min(2 * y, sh - 1), y1 = std::min(2 * y + 1, sh - 1);
        for (int x = 0; x < dw; ++x) {
          int x0 = std::min(2 * x, sw - 1), x1 = std::min(2 * x + 1, sw - 1);
          for (int c = 0; c < channels; ++c) {
            int s = src[(size_t(y0) * sw + x0) * channels + c] +
                    src[(size_t(y0) * sw + x1) * channels + c] +
                    src[(size_t(y1) * sw + x0) * channels + c] +
                    src[(size_t(y1) * sw + x1) * channels + c];
            m.data[(size_t(y) * dw + x) * channels + c] = uint8_t((s + 2) / 4);
          }
        }
      }
      mips.push_back(std::move(m));
      sw = dw;
      sh = dh;
      src = mips.back().data.data();
    }
  }

  // Bilinear lookup in a single level (raw, no sRGB), at pre-wrapped (wu,wv).
  Vec3 bilinear_level(int lvl, float wu, float wv) const {
    int w, h;
    const uint8_t *d;
    if (lvl <= 0 || mips.empty()) {
      w = width;
      h = height;
      d = pixels.data();
    } else {
      const Mip &m = mips[std::min(size_t(lvl) - 1, mips.size() - 1)];
      w = m.w;
      h = m.h;
      d = m.data.data();
    }
    float fu = wu * float(w) - 0.5f, fv = wv * float(h) - 0.5f;
    int x0 = int(std::floor(fu)), y0 = int(std::floor(fv));
    float tx = fu - float(x0), ty = fv - float(y0);
    auto texel = [&](int x, int y) -> Vec3 {
      x = ((x % w) + w) % w;
      y = ((y % h) + h) % h;
      const uint8_t *p = &d[(size_t(y) * w + x) * size_t(channels)];
      return Vec3{float(p[0]) / 255.0f,
                  float(channels > 1 ? p[1] : p[0]) / 255.0f,
                  float(channels > 2 ? p[2] : p[0]) / 255.0f};
    };
    auto lerp = [](const Vec3 &a, const Vec3 &b, float t) {
      return Vec3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t};
    };
    Vec3 c00 = texel(x0, y0), c10 = texel(x0 + 1, y0);
    Vec3 c01 = texel(x0, y0 + 1), c11 = texel(x0 + 1, y0 + 1);
    return lerp(lerp(c00, c10, tx), lerp(c01, c11, tx), ty);
  }

  // Trilinear sample at mip level `lod` (lod<=0 = full res); wrap + sRGB applied.
  // (USD UV origin is bottom-left, so v is flipped.)
  Vec3 sample(float u, float v, float lod = 0.0f) const {
    if (width <= 0 || height <= 0 || pixels.empty()) {
      return Vec3{0.5f, 0.5f, 0.5f};
    }
    bool su = true, sv = true;
    float wu = ApplyWrap(u, wrap_s, &su);
    float wv = ApplyWrap(1.0f - v, wrap_t, &sv);
    if (!su || !sv) return Vec3{0.0f, 0.0f, 0.0f};  // Black wrap, out of bounds
    const float maxlvl = float(mips.size());
    const float L = std::max(0.0f, std::min(lod, maxlvl));
    const int l0 = int(std::floor(L));
    const float f = L - float(l0);
    Vec3 c = bilinear_level(l0, wu, wv);
    if (f > 0.0f && float(l0) < maxlvl) {
      Vec3 c1 = bilinear_level(l0 + 1, wu, wv);
      c = Vec3{c.x + (c1.x - c.x) * f, c.y + (c1.y - c.y) * f,
               c.z + (c1.z - c.z) * f};
    }
    if (srgb) c = Vec3{SrgbToLinear(c.x), SrgbToLinear(c.y), SrgbToLinear(c.z)};
    return c;
  }

  // Anisotropic sample: the UV footprint is a parallelogram whose axes are the
  // per-screen-axis UV derivatives. Pick the mip from the MINOR axis (so the
  // result stays sharp across the narrow direction) and average up to
  // `max_aniso` trilinear taps stepped along the MAJOR axis (anti-aliasing the
  // long direction). Reduces to plain trilinear when the footprint is isotropic.
  Vec3 sample_aniso(float u, float v, float dudx, float dvdx, float dudy,
                    float dvdy, int max_aniso) const {
    const float W = float(width), H = float(height);
    const float lx = std::sqrt(dudx * dudx * W * W + dvdx * dvdx * H * H);
    const float ly = std::sqrt(dudy * dudy * W * W + dvdy * dvdy * H * H);
    float Pmax, Pmin, mdu, mdv;  // major-axis length (texels) + UV step
    if (lx >= ly) {
      Pmax = lx; Pmin = ly; mdu = dudx; mdv = dvdx;
    } else {
      Pmax = ly; Pmin = lx; mdu = dudy; mdv = dvdy;
    }
    if (Pmax < 1.0e-8f) return sample(u, v, 0.0f);
    int n = int(std::ceil(Pmax / std::max(Pmin, 1.0e-8f)));
    n = std::min(std::max(n, 1), std::max(1, max_aniso));
    const float lod = std::log2(std::max(Pmax / float(n), 1.0e-8f));
    if (n <= 1) return sample(u, v, lod);
    Vec3 acc{0.0f, 0.0f, 0.0f};
    for (int i = 0; i < n; ++i) {
      const float t = (float(i) + 0.5f) / float(n) - 0.5f;  // [-0.5, 0.5)
      Vec3 s = sample(u + t * mdu, v + t * mdv, lod);
      acc.x += s.x;
      acc.y += s.y;
      acc.z += s.z;
    }
    const float inv = 1.0f / float(n);
    return Vec3{acc.x * inv, acc.y * inv, acc.z * inv};
  }

  // Bilinear lookup of a SINGLE channel `ch` (0=r,1=g,2=b,3=a) in one level, raw.
  // Unlike bilinear_level (which packs only RGB into a Vec3), this reaches the
  // alpha channel -- needed for scalar inputs connected to a UsdUVTexture's
  // outputs:a (e.g. opacity from a diffuse map's alpha). A channel beyond the
  // image's component count returns the UsdUVTexture fallback (1 for alpha, else
  // 0). Same lerp order as bilinear_level, so r/g/b stay bit-identical.
  float bilinear_channel(int lvl, float wu, float wv, int ch) const {
    int w, h;
    const uint8_t *d;
    if (lvl <= 0 || mips.empty()) {
      w = width; h = height; d = pixels.data();
    } else {
      const Mip &m = mips[std::min(size_t(lvl) - 1, mips.size() - 1)];
      w = m.w; h = m.h; d = m.data.data();
    }
    if (ch >= channels) return ch == 3 ? 1.0f : 0.0f;
    float fu = wu * float(w) - 0.5f, fv = wv * float(h) - 0.5f;
    int x0 = int(std::floor(fu)), y0 = int(std::floor(fv));
    float tx = fu - float(x0), ty = fv - float(y0);
    auto texel = [&](int x, int y) -> float {
      x = ((x % w) + w) % w;
      y = ((y % h) + h) % h;
      return float(d[(size_t(y) * w + x) * size_t(channels) + size_t(ch)]) /
             255.0f;
    };
    float c00 = texel(x0, y0), c10 = texel(x0 + 1, y0);
    float c01 = texel(x0, y0 + 1), c11 = texel(x0 + 1, y0 + 1);
    float a = c00 + (c10 - c00) * tx;
    float b = c01 + (c11 - c01) * tx;
    return a + (b - a) * ty;
  }

  // Trilinear single-channel sample (incl. alpha). Raw (scalar inputs are always
  // sourceColorSpace=raw), so no sRGB. Matches ChannelOf(sample(...)) bit-for-bit
  // for r/g/b on a raw texture; additionally exposes alpha (ch=3).
  float sample_channel(float u, float v, float lod, int ch) const {
    if (width <= 0 || height <= 0 || pixels.empty()) return 0.5f;
    bool su = true, sv = true;
    float wu = ApplyWrap(u, wrap_s, &su);
    float wv = ApplyWrap(1.0f - v, wrap_t, &sv);
    if (!su || !sv) return 0.0f;  // Black wrap, out of bounds
    const float maxlvl = float(mips.size());
    const float L = std::max(0.0f, std::min(lod, maxlvl));
    const int l0 = int(std::floor(L));
    const float f = L - float(l0);
    float c = bilinear_channel(l0, wu, wv, ch);
    if (f > 0.0f && float(l0) < maxlvl) {
      float c1 = bilinear_channel(l0 + 1, wu, wv, ch);
      c = c + (c1 - c) * f;
    }
    return c;
  }

  // Anisotropic single-channel sample: mirrors sample_aniso's footprint/tap logic
  // exactly (so r/g/b match ChannelOf(sample_aniso(...)) bit-for-bit) on one
  // channel, including alpha.
  float sample_channel_aniso(float u, float v, float dudx, float dvdx, float dudy,
                             float dvdy, int max_aniso, int ch) const {
    const float W = float(width), H = float(height);
    const float lx = std::sqrt(dudx * dudx * W * W + dvdx * dvdx * H * H);
    const float ly = std::sqrt(dudy * dudy * W * W + dvdy * dvdy * H * H);
    float Pmax, Pmin, mdu, mdv;
    if (lx >= ly) {
      Pmax = lx; Pmin = ly; mdu = dudx; mdv = dvdx;
    } else {
      Pmax = ly; Pmin = lx; mdu = dudy; mdv = dvdy;
    }
    if (Pmax < 1.0e-8f) return sample_channel(u, v, 0.0f, ch);
    int n = int(std::ceil(Pmax / std::max(Pmin, 1.0e-8f)));
    n = std::min(std::max(n, 1), std::max(1, max_aniso));
    const float lod = std::log2(std::max(Pmax / float(n), 1.0e-8f));
    if (n <= 1) return sample_channel(u, v, lod, ch);
    float acc = 0.0f;
    for (int i = 0; i < n; ++i) {
      const float t = (float(i) + 0.5f) / float(n) - 0.5f;
      acc += sample_channel(u + t * mdu, v + t * mdv, lod, ch);
    }
    return acc / float(n);
  }
};

// 2D UV transform (UsdTransform2d): out = Rotate(rotation) * (st * scale) +
// translation. Baked into tri_uvs at build time (no per-hit cost).
struct UvXform {
  bool identity{true};
  float rc{1.0f}, rs{0.0f};  // cos/sin(rotation)
  float sx{1.0f}, sy{1.0f}, tx{0.0f}, ty{0.0f};
  void apply(float *u, float *v) const {
    if (identity) return;
    float a = *u * sx, b = *v * sy;
    *u = a * rc - b * rs + tx;
    *v = a * rs + b * rc + ty;
  }
};

struct PreviewLight {
  enum class Kind { Point, Distant, Sphere, Rect, Disk, Cylinder, Mesh, Dome };
  Kind kind{Kind::Point};
  Vec3 position{0.0f, 0.0f, 0.0f};
  Vec3 direction{0.0f, -1.0f, 0.0f};
  Vec3 radiance{1.0f, 1.0f, 1.0f};
  Vec3 normal{0.0f, 1.0f, 0.0f};
  float radius{0.0f};
  float width{1.0f};
  float height{1.0f};
  float area{0.0f};
  float power{0.0f};
  float cdf{0.0f};
  int tri_id{-1};
  int texture_id{-1};
  std::string texture_file;
};

struct LightCache {
  std::vector<PreviewLight> finite;
  std::vector<PreviewLight> mesh;
  std::vector<float> finite_cdf;
  std::vector<float> mesh_cdf;
  std::vector<float> env_cdf;
  bool has_dome{false};
  PreviewLight dome;
  Vec3 env_color{0.0f, 0.0f, 0.0f};
};

struct EnvImage {
  int width{0};
  int height{0};
  std::vector<Vec3> pixels;
};

struct IblCache {
  bool valid{false};
  EnvImage env;
  EnvImage diffuse;
  std::vector<EnvImage> prefiltered;
  int brdf_size{0};
  std::vector<float> brdf_lut;
  // DomeLight orientation: when the dome prim carries a non-identity world
  // rotation, these are its local axes in world space (the rows of the world
  // rotation). A world direction is mapped into the dome's local frame by
  // projecting onto them (d_local = {d.rx, d.ry, d.rz}) before the lat-long
  // lookup, so the environment rotates with the dome. Identity when !rotated
  // (--env override and untransformed domes stay byte-identical).
  bool rotated{false};
  Vec3 rx{1.0f, 0.0f, 0.0f};
  Vec3 ry{0.0f, 1.0f, 0.0f};
  Vec3 rz{0.0f, 0.0f, 1.0f};
};

struct CameraFrame {
  Vec3 origin;
  Vec3 right{1.0f, 0.0f, 0.0f};
  Vec3 up{0.0f, 1.0f, 0.0f};
  Vec3 forward{0.0f, 0.0f, -1.0f};
  float yfov{45.0f * 3.14159265358979323846f / 180.0f};
  float xmag{1.0f};
  float ymag{1.0f};
  float znear{0.001f};
  float zfar{1.0e30f};
  bool ortho{false};
};

struct Options {
  std::string input;
  std::string output;
  std::string camera;
  int width{960};
  int height{0};
  float fit_scale{1.8f};
  Vec3 view_dir{0.0f, 0.0f, 0.0f};
  bool has_view_dir{false};
  uint32_t purpose_mask{kPurposeDefaultMask};
  double timecode{tinyusdz::value::TimeCode::Default()};
  int samples{1};
  Vec3 bg{0.0f, 0.0f, 0.0f};
  float ambient{0.05f};
  bool shadows{true};
  bool no_assetresolver{false};
  bool stats{false};
  bool direct_prims{true};
  bool rt_preview{false};
  bool legacy_load{false};  // use the legacy eager loader instead of `next`
  bool smooth{false};       // interpolate authored normals (smooth shading)
  bool progress{false};
  lrt_tri_quality quality{LRT_TRI_BUILD_FAST};
  int threads{0};
  int subdivision_level{0};
  bool autoframe{false};  // OpenUSD usdrecord-style auto camera framing
  std::string js_script;  // -js <file>: drive rendering from a JS script
  bool mcp{false};        // -mcp: run an MCP stdio control server
  std::vector<std::string> mask;  // -mask: restrict to these prim subtrees
  std::string frames;             // -frames FRAMESPEC: render an animation
  bool default_time{false};       // -defaultTime: evaluate at the default time
  double max_mem_gib{0.0};        // -maxMem <GiB>: 0 = auto min(32, 0.5*avail)
  std::map<std::string, std::string> variant_overrides;  // --variant set=selection
  bool vulkan{false};              // -vk: use Vulkan backend
  bool vulkan_rt{false};           // -vkr: use Vulkan ray tracing backend
  std::string env_file;            // --env <hdr>: IBL environment map override
};

struct DirectShape {
  enum class Type { Cylinder, Cone, Capsule };
  Type type{Type::Cylinder};
  matrix4d world{matrix4d::identity()};
  matrix4d inv_world{matrix4d::identity()};
  double radius{1.0};
  double height{2.0};
  tinyusdz::Axis axis{tinyusdz::Axis::Z};
  Vec3 base_color{0.18f, 0.18f, 0.18f};
  Vec3 emission{0.0f, 0.0f, 0.0f};
};

struct DirectHit {
  float t{std::numeric_limits<float>::max()};
  Vec3 n{0.0f, 1.0f, 0.0f};
  Vec3 base_color{0.18f, 0.18f, 0.18f};
  Vec3 emission{0.0f, 0.0f, 0.0f};
  bool hit{false};
};

struct TetPrim {
  Vec3 p[4];
  Vec3 base_color{0.56f, 0.36f, 0.64f};
  Vec3 emission{0.0f, 0.0f, 0.0f};
};

struct DirectScene {
  std::unique_ptr<lrt_tri_scene, void (*)(lrt_tri_scene *)> spheres{nullptr,
                                                                    lrt_tri_scene_free};
  std::unique_ptr<lrt_tri_scene, void (*)(lrt_tri_scene *)> round_curves{nullptr,
                                                                        lrt_tri_scene_free};
  std::unique_ptr<lrt_tri_scene, void (*)(lrt_tri_scene *)> flat_curves{nullptr,
                                                                       lrt_tri_scene_free};
  std::unique_ptr<lrt_tri_scene, void (*)(lrt_tri_scene *)> points{nullptr,
                                                                   lrt_tri_scene_free};
  std::unique_ptr<lrt_tri_scene, void (*)(lrt_tri_scene *)> bez_curves{nullptr,
                                                                       lrt_tri_scene_free};
  std::unique_ptr<lrt_tri_scene, void (*)(lrt_tri_scene *)> tets{nullptr,
                                                                 lrt_tri_scene_free};
  std::vector<TriInfo> sphere_info;
  std::vector<TriInfo> round_curve_info;
  std::vector<TriInfo> flat_curve_info;
  std::vector<TriInfo> bez_curve_info;
  std::vector<TriInfo> point_info;
  std::vector<TetPrim> tet_prims;
  std::vector<DirectShape> shapes;
  std::unordered_set<std::string> direct_paths;
};

struct RTPreviewStats {
  struct MeshGeometry {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<uint32_t> indices;
  };
  size_t meshes{0};
  size_t meshes_with_mmap_points{0};
  size_t meshes_with_owned_points{0};
  size_t skipped_meshes{0};
  size_t triangles{0};
  uint64_t mmap_deferred_bytes{0};
  uint64_t copied_point_bytes{0};
  uint64_t copied_topology_bytes{0};
  uint64_t packed_triangle_bytes{0};
  uint64_t purpose_default_triangles{0};
  uint64_t purpose_render_triangles{0};
  uint64_t purpose_proxy_triangles{0};
  uint64_t purpose_guide_triangles{0};
  size_t point_instancers{0};  // UsdGeomPointInstancer prims expanded
  size_t point_instances{0};   // visible instances they emitted (TLAS placements)
  size_t nested_instances{0};  // extra placements from flattening nested instancing
  size_t curve_strands{0};     // top-level BasisCurves/NurbsCurves prims
  size_t curve_instances{0};   // instanced curve-prototype placements (TLAS)
  double build_seconds{0.0};
};

template <typename T>
struct BorrowedArrayView {
  const T *data{nullptr};
  const uint8_t *bytes{nullptr};
  size_t count{0};
  bool mmap{false};
  std::vector<T> owned;
};

struct MeshJob {
  const tinyusdz::GeomMesh *mesh{nullptr};
  matrix4d world{matrix4d::identity()};
  tinyusdz::Purpose purpose{tinyusdz::Purpose::Default};
  std::string prim_path;
};

struct PurposeFilter {
  const std::vector<FlatTri> *tris{nullptr};
  uint32_t mask{kPurposeDefaultMask};
};

struct Blas {
  FloatVec vertices;  // packed local-space triangle positions (soup; empty when indexed)
  // Indexed geometry (Phase 2b): when `indices` is non-empty the BLAS streamed
  // 1x unique verts (uverts, 3 floats each) + 3 indices/tri and is built via
  // lrt_tri_scene_build_indexed. `vertices` (soup) stays empty in that case.
  // Both are freed after the BVH build (positions live in the de-indexed leaf).
  FloatVec uverts;
  IdxVec indices;
  TriStoreVec tris;   // local p0/p1/p2/n/purpose + mat_id (into mat_table)
  std::vector<TriMat> mat_table;  // one entry per source mesh-job
  FloatVec tri_uvs;   // 6 floats/tri (parallel to tris) or empty
  ByteVec tri_colors;   // 12 bytes/tri (per-corner RGBA8, prim_id order) or empty
  // Phase 5: per-corner colors reordered into BVH leaf-slot order (12 bytes/slot)
  // for cache-coherent hit reads -- adjacent leaf triangles -> adjacent entries.
  // When non-empty, tri_colors is freed and the hit indexes this by lrt_tri_get_slot.
  ByteVec tri_colors_slot;
  FloatVec tri_normals; // 9 floats/tri (per-corner authored normals) or empty
  // Curve BLAS (is_curve): the scene is a LightRT round-hair scene. Per segment
  // we keep only the local-space endpoints (curve_seg, 6 floats: p0 p1) + a
  // material id into mat_table (curve_seg_mat) -- the material is shared by all of
  // a curve job's segments, so storing one TriMat instead of a 120 B TriInfo per
  // segment cuts a 3M-segment hair BLAS ~360 -> ~84 MB. p2 (a normal helper) is
  // synthetic (p0 + +Y) and the geometric normal is recomputed at hit, matching
  // the old full-TriInfo path byte-for-byte.
  bool is_curve{false};
  std::vector<float> curve_seg;         // 6 floats/segment: p0.xyz p1.xyz
  std::vector<uint32_t> curve_seg_mat;  // mat_id into mat_table, per segment
  lrt_tri_scene *scene{nullptr};

  Blas() = default;
  Blas(Blas &&o) noexcept { *this = std::move(o); }
  Blas &operator=(Blas &&o) noexcept {
    if (this != &o) {
      vertices = std::move(o.vertices);
      uverts = std::move(o.uverts);
      indices = std::move(o.indices);
      tris = std::move(o.tris);
      mat_table = std::move(o.mat_table);
      tri_uvs = std::move(o.tri_uvs);
      tri_colors = std::move(o.tri_colors);
      tri_colors_slot = std::move(o.tri_colors_slot);
      tri_normals = std::move(o.tri_normals);
      is_curve = o.is_curve;
      curve_seg = std::move(o.curve_seg);
      curve_seg_mat = std::move(o.curve_seg_mat);
      if (scene) lrt_tri_scene_free(scene);
      scene = o.scene;
      o.scene = nullptr;
    }
    return *this;
  }
  Blas(const Blas &) = delete;
  Blas &operator=(const Blas &) = delete;
  ~Blas() {
    if (scene) lrt_tri_scene_free(scene);
  }
};

struct InstanceRT {
  uint32_t blas_id{0};
  float o2w[12]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
};

struct RayDiff {
  Vec3 ox, dx;  // ray for pixel (x+1, y)
  Vec3 oy, dy;  // ray for pixel (x, y+1)
  bool valid{false};
};

struct VolumeData {
  std::vector<float> density;  // dense grid, x-contiguous
  int dim[3] = {0, 0, 0};
  Vec3 bmin{0.0f, 0.0f, 0.0f};  // object-space AABB
  Vec3 bmax{0.0f, 0.0f, 0.0f};
  matrix4d inv_world{matrix4d::identity()};  // world -> object
  float density_scale = 1.0f;
  Vec3 albedo{0.6f, 0.6f, 0.65f};
  Vec3 emission{0.0f, 0.0f, 0.0f};
  float background = 0.0f;
};

struct MeshJobNext {
  tinyusdz::next::UsdPrim prim;
  matrix4d world{matrix4d::identity()};
  tinyusdz::Purpose purpose{tinyusdz::Purpose::Default};
  Vec3 base_color{0.55f, 0.55f, 0.55f};  // resolved diffuse constant
  int32_t tex_id{-1};                    // resolved diffuse texture, or -1
  float roughness{0.55f};                // resolved inputs:roughness
  float metallic{0.0f};                  // resolved inputs:metallic
  int32_t normal_tex_id{-1};             // resolved tangent-space normal map
  ScalarTex rough_tex;                   // roughness texture + channel
  ScalarTex metal_tex;                   // metallic texture + channel
  Vec3 emission{0.0f, 0.0f, 0.0f};       // resolved inputs:emissiveColor
  int32_t emission_tex_id{-1};           // emissive color texture
  float occlusion{1.0f};                 // resolved inputs:occlusion
  ScalarTex occ_tex;                     // occlusion texture + channel
  UvXform uv_xform;                      // UsdTransform2d on the st chain
  float opacity{1.0f};                   // displayOpacity / inputs:opacity constant
  ScalarTex opacity_tex;                 // UsdPreviewSurface inputs:opacity texture
  float opacity_threshold{0.0f};         // inputs:opacityThreshold (alpha cutout)
  float clearcoat{0.0f};                 // inputs:clearcoat weight
  float clearcoat_roughness{0.01f};      // inputs:clearcoatRoughness
  ScalarTex clearcoat_tex;               // clearcoat-weight texture + channel
  ScalarTex clearcoat_rough_tex;         // clearcoat-roughness texture + channel
  Vec3 specular_color{0.0f, 0.0f, 0.0f}; // inputs:specularColor
  int32_t specular_tex_id{-1};           // specularColor texture
  float ior{1.5f};                       // inputs:ior
  uint8_t use_specular_workflow{0};      // inputs:useSpecularWorkflow
  bool vertex_color{false};              // displayColor/Opacity is per-vertex
};

struct TextureCache {
  std::vector<Texture> *textures{nullptr};
  std::unordered_map<std::string, int32_t> by_key;
  std::string base_dir;  // directory of the input file, for relative paths
  const tinyusdz::next::USDZReader *usdz{nullptr};
};

struct ResolvedMat {
  Vec3 base_color{0.55f, 0.55f, 0.55f};
  int32_t tex_id{-1};
  float roughness{0.55f};
  float metallic{0.0f};
  int32_t normal_tex_id{-1};
  UvXform uv_xform;
  ScalarTex rough_tex;
  ScalarTex metal_tex;
  Vec3 emission{0.0f, 0.0f, 0.0f};
  int32_t emission_tex_id{-1};
  float occlusion{1.0f};
  ScalarTex occ_tex;
  float opacity{1.0f};
  ScalarTex opacity_tex;
  float opacity_threshold{0.0f};
  float clearcoat{0.0f};
  float clearcoat_roughness{0.01f};
  ScalarTex clearcoat_tex;
  ScalarTex clearcoat_rough_tex;
  Vec3 specular_color{0.0f, 0.0f, 0.0f};
  int32_t specular_tex_id{-1};
  float ior{1.5f};
  uint8_t use_specular_workflow{0};
  bool vertex_color{false};
};

struct ProtoBuildReq {
  std::string path;
  tinyusdz::Purpose purpose{tinyusdz::Purpose::Default};
  uint32_t blas_id{0};  // index into RenderContext::blas
};

struct CurveJobNext {
  tinyusdz::next::UsdPrim prim;
  matrix4d world{matrix4d::identity()};
  tinyusdz::Purpose purpose{tinyusdz::Purpose::Default};
};

// One placement of a curve prototype's curve BLAS (deduped curve geometry stored
// once and instanced through the TLAS, like mesh prototypes).
struct CurveInstanceRT {
  uint32_t curve_proto_idx{0};  // index into CurveProtoCollect::protos
  float o2w[12]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
};

// Curve prototypes referenced by PointInstancers + their placements, so a
// heavily-instanced curve prototype stores its hair geometry once (a curve BLAS)
// instead of baking world-space copies per instance.
struct CurveProtoCollect {
  std::vector<ProtoBuildReq> protos;             // curve prototype paths
  std::unordered_map<std::string, uint32_t> ids;  // dedup key -> protos index
  std::vector<CurveInstanceRT> instances;
};

}  // namespace tusdr
