// SPDX-License-Identifier: Apache 2.0
// Copyright 2026 - Present, Light Transport Entertainment, Inc.
//
// tusdrender: CPU preview raytrace renderer for USD scenes.
//
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <new>
#include <unistd.h>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "asset-resolution.hh"
#include "image-loader.hh"
#include "image-writer.hh"
#include "io-util.hh"
#include "mmap-array-ref.hh"
#include "tinyusdz.hh"
#include "tsd/tinysubdiv.hh"
#include "tydra/attribute-eval.hh"
#include "tydra/render-data.hh"
#include "usdGeom.hh"
#include "value-types.hh"
#include "xform.hh"

// Experimental `next` lazy loader: fast, low-memory USDC parse used as the
// default backend for the RT preview path. tydra_next provides bit-exact
// world transforms (see src/tydra/next/scene-access.cc).
#include "next/layer/prim-spec.hh"
#include "next/pcp/prim-index.hh"
#include "next/prim/path.hh"
#include "next/reader/usdz-reader.hh"
#include "next/schema/geom-mesh.hh"
#include "next/stage/stage.hh"
#include "next/tinyusdz-next.hh"
#include "next/types/value.hh"
#include "tydra/next/scene-access.hh"



extern "C" {
#include "lightrt_c_tri.h"
}

#ifdef TINYUSDZ_WITH_QJS
#include <fstream>
#include <sstream>

#include "external/jsonhpp/nlohmann/json.hpp"
#include "tydra/js-script.hh"
extern "C" {
#include "external/quickjs-ng/quickjs.h"
}
#endif

namespace {

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

// ===========================================================================
// Memory budget / manager.
//
// A process-wide cap that keeps tusdrender from being OOM-killed on huge scenes
// (e.g. fully instance-expanded Caldera maps). The cap defaults to
// min(32 GiB, 0.5 * system MemAvailable) and is overridable with -maxMem <GiB>.
// It is enforced two ways:
//   1. Phase guards (GuardPhase) check the live process RSS + an estimate of the
//      next phase's allocation and abort cleanly BEFORE the allocation that would
//      bust the cap (covers composition + LightRT BVH, which allocate outside our
//      allocator).
//   2. The pool allocator (PoolAlloc, below) accounts every render-buffer byte
//      into `tracked_` and throws std::bad_alloc when our allocations would push
//      RSS past the cap (covers the triangle stream precisely, mid-flight).
// ===========================================================================
class MemBudget {
 public:
  static constexpr size_t kDefaultCapBytes = size_t(32) << 30;  // 32 GiB

  static MemBudget &Get() {
    static MemBudget inst;
    return inst;
  }

  // cap_override_gib <= 0 -> auto: min(32 GiB, 0.5 * MemAvailable).
  void Init(double cap_override_gib) {
    if (cap_override_gib > 0.0) {
      cap_ = size_t(cap_override_gib * double(size_t(1) << 30));
    } else {
      size_t avail = AvailableSystemMemory();
      size_t half = avail ? avail / 2 : 0;
      cap_ = half ? std::min(kDefaultCapBytes, half) : kDefaultCapBytes;
    }
    base_.store(0);
    tracked_.store(0);
    peak_tracked_.store(0);
  }

  size_t Cap() const { return cap_; }
  size_t Tracked() const { return tracked_.load(std::memory_order_relaxed); }
  size_t PeakTracked() const {
    return peak_tracked_.load(std::memory_order_relaxed);
  }

  // Snapshot the non-tracked RSS (everything except our render buffers), so the
  // pool allocator can bound OUR allocations to cap_ - base_ with pure atomics
  // (no /proc read per allocation). Call at the start of a streaming phase.
  void SnapshotBase() {
    size_t rss = ProcessRSS();
    size_t tr = tracked_.load(std::memory_order_relaxed);
    base_.store(rss > tr ? rss - tr : 0, std::memory_order_relaxed);
  }

  // Atomically reserve `bytes` of tracked allocation; false if it would exceed
  // the cap (cap_ - base_). Used by PoolAlloc::allocate.
  bool TryAdd(size_t bytes) {
    if (!cap_) {  // no limit configured
      size_t v = tracked_.fetch_add(bytes, std::memory_order_relaxed) + bytes;
      BumpPeak(v);
      return true;
    }
    size_t base = base_.load(std::memory_order_relaxed);
    size_t limit = cap_ > base ? cap_ - base : 0;
    size_t prev = tracked_.fetch_add(bytes, std::memory_order_relaxed);
    if (prev + bytes > limit) {
      tracked_.fetch_sub(bytes, std::memory_order_relaxed);
      return false;
    }
    BumpPeak(prev + bytes);
    return true;
  }
  void Sub(size_t bytes) {
    tracked_.fetch_sub(bytes, std::memory_order_relaxed);
  }

  // Phase guard: would the process RSS plus `extra_estimate` untracked bytes bust
  // the cap right now? `phase`/extra are for the diagnostic message.
  bool WouldExceed(size_t extra_estimate, std::string *why = nullptr) const {
    if (!cap_) return false;
    size_t rss = ProcessRSS();
    if (rss + extra_estimate <= cap_) return false;
    if (why) {
      *why = "memory cap " + GiB(cap_) + " would be exceeded (current RSS " +
             GiB(rss) + " + estimated " + GiB(extra_estimate) + ")";
    }
    return true;
  }

  // Linux RSS via /proc/self/statm (pages); 0 if unavailable.
  static size_t ProcessRSS() {
    std::ifstream f("/proc/self/statm");
    if (!f) return 0;
    size_t total_pages = 0, rss_pages = 0;
    f >> total_pages >> rss_pages;
    if (!f) return 0;
    long pg = sysconf(_SC_PAGESIZE);
    return rss_pages * size_t(pg > 0 ? pg : 4096);
  }

  // Linux MemAvailable via /proc/meminfo (bytes); 0 if unavailable.
  static size_t AvailableSystemMemory() {
    std::ifstream f("/proc/meminfo");
    if (!f) return 0;
    std::string key;
    while (f >> key) {
      if (key == "MemAvailable:") {
        size_t kib = 0;
        f >> kib;
        return kib * size_t(1024);
      }
      std::string rest;
      std::getline(f, rest);
    }
    return 0;
  }

  static std::string GiB(size_t bytes) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f GiB",
                  double(bytes) / double(size_t(1) << 30));
    return std::string(buf);
  }

 private:
  MemBudget() = default;
  void BumpPeak(size_t v) {
    size_t p = peak_tracked_.load(std::memory_order_relaxed);
    while (v > p &&
           !peak_tracked_.compare_exchange_weak(p, v, std::memory_order_relaxed)) {
    }
  }
  size_t cap_{kDefaultCapBytes};
  std::atomic<size_t> base_{0};
  std::atomic<size_t> tracked_{0};
  std::atomic<size_t> peak_tracked_{0};
};

// Conservative estimate of the extra memory LightRT's BVH build adds per
// triangle (the re-swizzled vertex copy ~36 B + wide-BVH nodes/leaves). Used by
// the pre-build RSS guards; intentionally an over-estimate so the guard trips
// before an OOM rather than after.
static constexpr size_t kBvhBytesPerTri = 80;

// Max anisotropic taps for diffuse texture sampling (GPU-style; 1 = isotropic
// trilinear). Each tap is a trilinear lookup, so this bounds the per-hit cost.
static constexpr int kMaxAniso = 8;

// Thread-safe size-bucketed free-list pool. Cuts malloc/free traffic for the
// render buffers (which are freed + reallocated across animation frames / re-
// renders) and recycles whole bucket-sized blocks so reuse is always safe.
// Oversize requests bypass the pool. A retained-free-bytes cap prevents hoarding.
class MemPool {
 public:
  static MemPool &Get() {
    static MemPool p;
    return p;
  }
  void *Alloc(size_t bytes) {
    int b = Bucket(bytes);
    if (b < 0) return std::malloc(bytes);  // oversize: exact size
    {
      std::lock_guard<std::mutex> lk(mu_[b]);
      if (!free_[b].empty()) {
        void *p = free_[b].back();
        free_[b].pop_back();
        pooled_.fetch_sub(BucketBytes(b), std::memory_order_relaxed);
        return p;
      }
    }
    return std::malloc(BucketBytes(b));  // full bucket size -> safe to recycle
  }
  void Free(void *p, size_t bytes) {
    if (!p) return;
    int b = Bucket(bytes);
    if (b >= 0 &&
        pooled_.load(std::memory_order_relaxed) + BucketBytes(b) <= kMaxPooled) {
      std::lock_guard<std::mutex> lk(mu_[b]);
      free_[b].push_back(p);
      pooled_.fetch_add(BucketBytes(b), std::memory_order_relaxed);
      return;
    }
    std::free(p);
  }

 private:
  static constexpr int kMinShift = 6;   // 64 B
  static constexpr int kMaxShift = 20;  // 1 MiB
  static constexpr int kNumBuckets = kMaxShift - kMinShift + 1;
  static constexpr size_t kMaxPooled = size_t(256) << 20;  // retain <=256 MiB
  static int Bucket(size_t bytes) {
    if (bytes == 0) bytes = 1;
    int s = kMinShift;
    while ((size_t(1) << s) < bytes) ++s;
    if (s > kMaxShift) return -1;
    return s - kMinShift;
  }
  static size_t BucketBytes(int b) { return size_t(1) << (b + kMinShift); }
  std::mutex mu_[kNumBuckets];
  std::vector<void *> free_[kNumBuckets];
  std::atomic<size_t> pooled_{0};
};

// Allocator that routes std::vector storage through MemPool and accounts every
// byte into MemBudget — so the triangle buffers are tracked precisely and a
// stream that would bust the cap throws std::bad_alloc mid-flight (caught by the
// stream workers -> clean abort) instead of OOM-killing the process.
template <class T>
struct PoolAlloc {
  using value_type = T;
  PoolAlloc() noexcept = default;
  template <class U>
  PoolAlloc(const PoolAlloc<U> &) noexcept {}
  T *allocate(std::size_t n) {
    std::size_t bytes = n * sizeof(T);
    if (!MemBudget::Get().TryAdd(bytes)) throw std::bad_alloc();
    void *p = MemPool::Get().Alloc(bytes);
    if (!p) {
      MemBudget::Get().Sub(bytes);
      throw std::bad_alloc();
    }
    return static_cast<T *>(p);
  }
  void deallocate(T *p, std::size_t n) noexcept {
    std::size_t bytes = n * sizeof(T);
    MemPool::Get().Free(p, bytes);
    MemBudget::Get().Sub(bytes);
  }
  template <class U>
  bool operator==(const PoolAlloc<U> &) const noexcept {
    return true;
  }
  template <class U>
  bool operator!=(const PoolAlloc<U> &) const noexcept {
    return false;
  }
};

struct Vec3 {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
};

struct Bounds {
  Vec3 lo{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
          std::numeric_limits<float>::max()};
  Vec3 hi{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
          -std::numeric_limits<float>::max()};
  bool valid{false};
};

static constexpr uint32_t kPurposeDefaultBit = 1u << 0u;
static constexpr uint32_t kPurposeRenderBit = 1u << 1u;
static constexpr uint32_t kPurposeProxyBit = 1u << 2u;
static constexpr uint32_t kPurposeGuideBit = 1u << 3u;
static constexpr uint32_t kPurposeDefaultMask =
    kPurposeDefaultBit | kPurposeRenderBit | kPurposeProxyBit;

struct TriInfo {
  Vec3 p0;
  Vec3 p1;
  Vec3 p2;
  Vec3 n;
  Vec3 base_color{0.18f, 0.18f, 0.18f};
  Vec3 emission{0.0f, 0.0f, 0.0f};
  float roughness{0.55f};
  float metallic{0.0f};
  uint32_t purpose_bit{kPurposeDefaultBit};
  int32_t tex_id{-1};  // diffuse texture index, or -1 for a flat base_color
  int32_t normal_tex_id{-1};  // tangent-space normal map, or -1
  int32_t rough_tex_id{-1};   // roughness texture, or -1
  int32_t metal_tex_id{-1};   // metallic texture, or -1
  int32_t emission_tex_id{-1};  // emissive color texture (sRGB), or -1
  int32_t occ_tex_id{-1};       // occlusion (AO) texture, or -1
  float occlusion{1.0f};        // ambient-occlusion scalar
  uint8_t rough_ch{0};          // source channel (0=r,1=g,2=b,3=a)
  uint8_t metal_ch{0};
  uint8_t occ_ch{0};
  float opacity{1.0f};          // primvars:displayOpacity (constant); <1 = blend
};

// Per-material shading parameters, factored out of the per-triangle record. A
// scene with many meshes sharing few materials (e.g. Island's coral) stores one
// of these per mesh-job instead of replicating ~64 B of material data on every
// triangle. The instanced (TLAS) BLAS arrays store the slim TriStore below + a
// side table of TriMat, cutting per-triangle memory ~2× on geometry-heavy scenes.
struct TriMat {
  Vec3 base_color{0.18f, 0.18f, 0.18f};
  Vec3 emission{0.0f, 0.0f, 0.0f};
  float roughness{0.55f};
  float metallic{0.0f};
  int32_t tex_id{-1};
  int32_t normal_tex_id{-1};
  int32_t rough_tex_id{-1};
  int32_t metal_tex_id{-1};
  int32_t emission_tex_id{-1};
  int32_t occ_tex_id{-1};
  float occlusion{1.0f};
  float opacity{1.0f};  // primvars:displayOpacity (constant); <1 = see-through
  uint8_t rough_ch{0};
  uint8_t metal_ch{0};
  uint8_t occ_ch{0};
};

// Slim per-triangle record for instanced BLAS storage: just a material id into
// the BLAS's TriMat table. Triangle positions are read from the BLAS's vertex
// soup (`Blas::vertices`, which LightRT aliases so it stays resident) at hit
// time, and the geometric normal is recomputed there — so the instanced
// per-triangle record is only 4 bytes (down from a 124 B TriInfo).
struct TriStore {
  uint32_t mat_id{0};
};

// Build a full TriInfo from a material record (positions/normal are filled by the
// caller from the vertex soup). Used by ResolveTLASHit for instanced mesh hits.
inline TriInfo CombineTriMat(const TriMat &m) {
  TriInfo t;
  t.base_color = m.base_color;
  t.emission = m.emission;
  t.roughness = m.roughness;
  t.metallic = m.metallic;
  t.tex_id = m.tex_id;
  t.normal_tex_id = m.normal_tex_id;
  t.rough_tex_id = m.rough_tex_id;
  t.metal_tex_id = m.metal_tex_id;
  t.emission_tex_id = m.emission_tex_id;
  t.occ_tex_id = m.occ_tex_id;
  t.occlusion = m.occlusion;
  t.opacity = m.opacity;
  t.rough_ch = m.rough_ch;
  t.metal_ch = m.metal_ch;
  t.occ_ch = m.occ_ch;
  return t;
}

// A scalar texture binding: texture index + source channel (UsdUVTexture
// outputs:r/g/b/a; for ORM packing roughness=g, metallic=b in one texture).
struct ScalarTex {
  int32_t id{-1};
  uint8_t ch{0};
};

// Budget-tracked, pooled vectors for the big render buffers (triangle positions,
// TriInfo, UVs). All other vectors keep the default allocator.
using FloatVec = std::vector<float, PoolAlloc<float>>;
using TriVec = std::vector<TriInfo, PoolAlloc<TriInfo>>;
using TriStoreVec = std::vector<TriStore, PoolAlloc<TriStore>>;

// Decoded RGB(A) texture (8-bit) sampled by the diffuse texture pipeline.
// UsdUVTexture wrap mode (inputs:wrapS / inputs:wrapT).
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
  lrt_tri_quality quality{LRT_TRI_BUILD_DEFAULT};
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

float Dot(const Vec3 &a, const Vec3 &b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 Cross(const Vec3 &a, const Vec3 &b) {
  return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
              a.x * b.y - a.y * b.x};
}

Vec3 Add(const Vec3 &a, const Vec3 &b) {
  return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 Sub(const Vec3 &a, const Vec3 &b) {
  return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 Mul(const Vec3 &a, float s) { return Vec3{a.x * s, a.y * s, a.z * s}; }

Vec3 Mul(const Vec3 &a, const Vec3 &b) {
  return Vec3{a.x * b.x, a.y * b.y, a.z * b.z};
}

Vec3 Div(const Vec3 &a, float s) {
  if (std::abs(s) <= 1.0e-20f) return Vec3{0.0f, 0.0f, 0.0f};
  return Vec3{a.x / s, a.y / s, a.z / s};
}

float Length(const Vec3 &v) { return std::sqrt(std::max(0.0f, Dot(v, v))); }

float Luminance(const Vec3 &v) {
  return 0.2126f * v.x + 0.7152f * v.y + 0.0722f * v.z;
}

Vec3 Normalize(const Vec3 &v) {
  float len = Length(v);
  if (len <= 1.0e-20f) {
    return Vec3{0.0f, 0.0f, 0.0f};
  }
  return Mul(v, 1.0f / len);
}

Vec3 Clamp01(const Vec3 &v) {
  return Vec3{std::max(0.0f, std::min(1.0f, v.x)),
              std::max(0.0f, std::min(1.0f, v.y)),
              std::max(0.0f, std::min(1.0f, v.z))};
}

Vec3 Lerp(const Vec3 &a, const Vec3 &b, float t) {
  return Add(Mul(a, 1.0f - t), Mul(b, t));
}

Vec3 Reflect(const Vec3 &v, const Vec3 &n) {
  return Sub(v, Mul(n, 2.0f * Dot(v, n)));
}

float ClampFloat(float v, float lo, float hi) {
  return std::max(lo, std::min(hi, v));
}

Vec3 FromFloat3(const float3 &v) { return Vec3{v[0], v[1], v[2]}; }
Vec3 FromPoint3(const tinyusdz::value::point3f &v) {
  return Vec3{v[0], v[1], v[2]};
}
Vec3 FromVector3(const tinyusdz::value::vector3f &v) {
  return Vec3{v[0], v[1], v[2]};
}

Vec3 TransformPoint(const matrix4d &m, const Vec3 &p) {
  double x = m.m[0][0] * double(p.x) + m.m[1][0] * double(p.y) +
             m.m[2][0] * double(p.z) + m.m[3][0];
  double y = m.m[0][1] * double(p.x) + m.m[1][1] * double(p.y) +
             m.m[2][1] * double(p.z) + m.m[3][1];
  double z = m.m[0][2] * double(p.x) + m.m[1][2] * double(p.y) +
             m.m[2][2] * double(p.z) + m.m[3][2];
  double w = m.m[0][3] * double(p.x) + m.m[1][3] * double(p.y) +
             m.m[2][3] * double(p.z) + m.m[3][3];
  if (std::abs(w) > 1.0e-20) {
    x /= w;
    y /= w;
    z /= w;
  }
  return Vec3{float(x), float(y), float(z)};
}

Vec3 TransformVector(const matrix4d &m, const Vec3 &v) {
  return Vec3{
      float(m.m[0][0] * double(v.x) + m.m[1][0] * double(v.y) +
            m.m[2][0] * double(v.z)),
      float(m.m[0][1] * double(v.x) + m.m[1][1] * double(v.y) +
            m.m[2][1] * double(v.z)),
      float(m.m[0][2] * double(v.x) + m.m[1][2] * double(v.y) +
            m.m[2][2] * double(v.z))};
}

void Expand(Bounds *b, const Vec3 &p) {
  if (!b) return;
  b->lo.x = std::min(b->lo.x, p.x);
  b->lo.y = std::min(b->lo.y, p.y);
  b->lo.z = std::min(b->lo.z, p.z);
  b->hi.x = std::max(b->hi.x, p.x);
  b->hi.y = std::max(b->hi.y, p.y);
  b->hi.z = std::max(b->hi.z, p.z);
  b->valid = true;
}

bool ParseIntStrict(const std::string &s, int *out) {
  if (!out || s.empty()) return false;
  errno = 0;
  char *end = nullptr;
  long v = std::strtol(s.c_str(), &end, 10);
  if (errno == ERANGE || end != s.c_str() + s.size()) return false;
  if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max()) {
    return false;
  }
  *out = int(v);
  return true;
}

bool ParseFloatStrict(const std::string &s, float *out) {
  if (!out || s.empty()) return false;
  errno = 0;
  char *end = nullptr;
  float v = std::strtof(s.c_str(), &end);
  if (errno == ERANGE || end != s.c_str() + s.size() || !std::isfinite(v)) {
    return false;
  }
  *out = v;
  return true;
}

bool ParseDoubleStrict(const std::string &s, double *out) {
  if (!out || s.empty()) return false;
  errno = 0;
  char *end = nullptr;
  double v = std::strtod(s.c_str(), &end);
  if (errno == ERANGE || end != s.c_str() + s.size() || !std::isfinite(v)) {
    return false;
  }
  *out = v;
  return true;
}

bool ParseColor(const std::string &s, Vec3 *out) {
  if (!out) return false;
  size_t p0 = s.find(',');
  size_t p1 = (p0 == std::string::npos) ? std::string::npos : s.find(',', p0 + 1);
  if (p0 == std::string::npos || p1 == std::string::npos ||
      s.find(',', p1 + 1) != std::string::npos) {
    return false;
  }
  Vec3 v;
  if (!ParseFloatStrict(s.substr(0, p0), &v.x) ||
      !ParseFloatStrict(s.substr(p0 + 1, p1 - p0 - 1), &v.y) ||
      !ParseFloatStrict(s.substr(p1 + 1), &v.z)) {
    return false;
  }
  *out = v;
  return true;
}

void PrintUsage(const char *prog) {
  std::cout
      << "tusdrender - CPU preview raytrace renderer for USD\n\n"
      << "Usage:\n"
      << "  " << prog << " input.usd output.png [options]\n\n"
      << "Options:\n"
      << "  -h, -help              Show this help.\n"
      << "  -w, -width <N>         Output width (default 960).\n"
      << "  -height <N>            Output height (default from aspect or 540).\n"
      << "  -camera <path|name>    Camera absolute path or camera name.\n"
      << "  -fitScale <value>      Auto-fit camera distance multiplier (default 1.8).\n"
      << "  -viewDir <x,y,z>       Auto-fit camera direction from target to eye.\n"
      << "  -purpose <list>        Visible USD purposes: default,render,proxy,guide.\n"
      << "  -showGuide             Include purpose=guide geometry.\n"
      << "  -hideProxy             Hide purpose=proxy geometry.\n"
      << "  -hideRender            Hide purpose=render geometry.\n"
      << "  -hideDefault           Hide default-purpose geometry.\n"
      << "  -timecode <value>      USD timecode to evaluate.\n"
      << "  -samples <N>           Deterministic supersamples per pixel (default 1).\n"
      << "  -bg <r,g,b>            Background color in linear RGB (default 0,0,0).\n"
      << "  -ambient <value>       Ambient diffuse term (default 0.05).\n"
      << "  -noShadows             Disable hard shadow rays.\n"
      << "  -smooth                Interpolate authored normals (smooth shading)\n"
      << "                         instead of per-face geometric normals.\n"
      << "  -rtPreview             Use mmap zero-copy mesh preview path for large USDC.\n"
      << "  -progress              Print long-running load/build progress.\n"
      << "  -quality <fast|default|hq>\n"
      << "                         LightRT BVH build quality (default default).\n"
      << "  -threads <N>           LightRT build threads (0 = backend default).\n"
      << "  -subdiv <N>            Subdivision level for Mesh subdivisionScheme\n"
      << "                         catmullClark/loop/bilinear (default 0).\n"
      << "  -complexity <low|medium|high|veryhigh>\n"
      << "                         usdrecord refinement preset -> subdiv 0/1/2/3.\n"
      << "  -autoframe             usdrecord-style auto camera framing.\n"
      << "  -timecode <t>          Evaluate animation at time code t.\n"
      << "  -defaultTime           Evaluate at the default (non-animated) time.\n"
      << "  -frames <FRAMESPEC>    Render an animation; one image per time code.\n"
      << "                         FRAMESPEC: t | start:end | start:end x stride,\n"
      << "                         comma-separated. Output path uses # for the\n"
      << "                         frame number (e.g. frame.####.png).\n"
      << "  -mask <PATH[,PATH...]> Restrict rendering to these prim subtrees.\n"
      << "  -variant <SET=SEL>     Override variant selection (e.g.\n"
      << "                         --variant districtLod=full). Repeatable.\n"
      << "  -legacyLoad            Use the legacy eager loader (next is default).\n"
#ifdef TINYUSDZ_WITH_QJS
      << "  -js <file.js>          Drive rendering from a JavaScript script.\n"
      << "                         Scene + BVH stay resident across renders\n"
      << "                         (memory-persistent, e.g. camera animation).\n"
      << "                         API: tusdrender.{setCamera,orbit,setResolution,\n"
      << "                         setAmbient,setBackground,setShadows,setSamples,\n"
      << "                         autoframe,bounds,stats,render}.\n"
      << "  -mcp                   Run an MCP stdio control server over the\n"
      << "                         resident scene (tools: eval,set_camera,orbit,\n"
      << "                         set_resolution,render,bounds,stats).\n"
#endif
      << "  -vk                   Use the Vulkan rasterizer backend.\n"
      << "  -vkr                  Use the Vulkan ray-tracing backend.\n"
      << "  -noDirectPrims         Tessellate USD shapes/curves/NURBS instead of\n"
      << "                         using tusdrender direct primitive paths.\n"
      << "  -stats                 Print scene/BVH stats.\n"
      << "  -noar                  Disable external asset resolution.\n";
}

bool ParseArgs(int argc, char **argv, Options *opt) {
  if (!opt) return false;
  if (argc <= 1) {
    PrintUsage(argv[0]);
    return false;
  }
  std::vector<std::string> positional;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto need_value = [&](const char *name) -> const char * {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for " << name << "\n";
        return nullptr;
      }
      return argv[++i];
    };
    if (a == "-h" || a == "-help" || a == "--help") {
      PrintUsage(argv[0]);
      std::exit(EXIT_SUCCESS);
    } else if (a == "-w" || a == "-width" || a == "--width") {
      const char *v = need_value(a.c_str());
      if (!v || !ParseIntStrict(v, &opt->width) || opt->width <= 0) {
        std::cerr << "Invalid width.\n";
        return false;
      }
    } else if (a == "-height" || a == "--height") {
      const char *v = need_value(a.c_str());
      if (!v || !ParseIntStrict(v, &opt->height) || opt->height <= 0) {
        std::cerr << "Invalid height.\n";
        return false;
      }
    } else if (a == "-camera" || a == "--camera") {
      const char *v = need_value(a.c_str());
      if (!v) return false;
      opt->camera = v;
    } else if (a == "-fitScale" || a == "--fitScale") {
      const char *v = need_value(a.c_str());
      if (!v) return false;
      opt->fit_scale = std::max(0.05f, std::stof(v));
    } else if (a == "-viewDir" || a == "--viewDir") {
      const char *v = need_value(a.c_str());
      if (!v || !ParseColor(v, &opt->view_dir) || Length(opt->view_dir) < 1.0e-6f) {
        std::cerr << "Invalid viewDir.\n";
        return false;
      }
      opt->has_view_dir = true;
    } else if (a == "-purpose" || a == "--purpose") {
      const char *v = need_value(a.c_str());
      if (!v) return false;
      uint32_t mask = 0;
      std::string s(v);
      size_t pos = 0;
      while (pos <= s.size()) {
        size_t comma = s.find(',', pos);
        std::string tok = s.substr(pos, comma == std::string::npos
                                            ? std::string::npos
                                            : comma - pos);
        if (tok == "default") {
          mask |= kPurposeDefaultBit;
        } else if (tok == "render") {
          mask |= kPurposeRenderBit;
        } else if (tok == "proxy") {
          mask |= kPurposeProxyBit;
        } else if (tok == "guide") {
          mask |= kPurposeGuideBit;
        } else if (!tok.empty()) {
          std::cerr << "Invalid purpose token: " << tok << "\n";
          return false;
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
      }
      opt->purpose_mask = mask;
    } else if (a == "-showGuide" || a == "--showGuide") {
      opt->purpose_mask |= kPurposeGuideBit;
    } else if (a == "-hideProxy" || a == "--hideProxy") {
      opt->purpose_mask &= ~kPurposeProxyBit;
    } else if (a == "-hideRender" || a == "--hideRender") {
      opt->purpose_mask &= ~kPurposeRenderBit;
    } else if (a == "-hideDefault" || a == "--hideDefault") {
      opt->purpose_mask &= ~kPurposeDefaultBit;
    } else if (a == "-timecode" || a == "--timecode") {
      const char *v = need_value(a.c_str());
      if (!v || !ParseDoubleStrict(v, &opt->timecode)) {
        std::cerr << "Invalid timecode.\n";
        return false;
      }
    } else if (a == "-samples" || a == "--samples") {
      const char *v = need_value(a.c_str());
      if (!v || !ParseIntStrict(v, &opt->samples) || opt->samples <= 0) {
        std::cerr << "Invalid samples.\n";
        return false;
      }
    } else if (a == "-bg" || a == "--bg") {
      const char *v = need_value(a.c_str());
      if (!v || !ParseColor(v, &opt->bg)) {
        std::cerr << "Invalid background color. Expected r,g,b.\n";
        return false;
      }
    } else if (a == "-ambient" || a == "--ambient") {
      const char *v = need_value(a.c_str());
      if (!v || !ParseFloatStrict(v, &opt->ambient) || opt->ambient < 0.0f) {
        std::cerr << "Invalid ambient value.\n";
        return false;
      }
    } else if (a == "-smooth" || a == "--smooth") {
      opt->smooth = true;
    } else if (a == "-noShadows" || a == "--noShadows") {
      opt->shadows = false;
    } else if (a == "-rtPreview" || a == "--rtPreview" ||
               a == "-mmapRt" || a == "--mmapRt") {
      opt->rt_preview = true;
      opt->direct_prims = false;
    } else if (a == "-legacyLoad" || a == "--legacyLoad") {
      opt->legacy_load = true;
    } else if (a == "-progress" || a == "--progress") {
      opt->progress = true;
    } else if (a == "-quality" || a == "--quality") {
      const char *v = need_value(a.c_str());
      if (!v) return false;
      std::string q = v;
      if (q == "fast") {
        opt->quality = LRT_TRI_BUILD_FAST;
      } else if (q == "default") {
        opt->quality = LRT_TRI_BUILD_DEFAULT;
      } else if (q == "hq") {
        opt->quality = LRT_TRI_BUILD_HQ;
      } else {
        std::cerr << "Invalid quality. Expected fast, default, or hq.\n";
        return false;
      }
    } else if (a == "-threads" || a == "--threads") {
      const char *v = need_value(a.c_str());
      if (!v || !ParseIntStrict(v, &opt->threads) || opt->threads < 0) {
        std::cerr << "Invalid thread count.\n";
        return false;
      }
    } else if (a == "-maxMem" || a == "--maxMem") {
      const char *v = need_value(a.c_str());
      char *end = nullptr;
      double g = v ? std::strtod(v, &end) : 0.0;
      if (!v || end == v || g < 0.0) {
        std::cerr << "Invalid -maxMem (expected GiB, e.g. -maxMem 24).\n";
        return false;
      }
      opt->max_mem_gib = g;
    } else if (a == "-env" || a == "--env") {
      const char *v = need_value(a.c_str());
      if (!v) {
        std::cerr << "Invalid -env (expected an environment-map path).\n";
        return false;
      }
      opt->env_file = v;
    } else if (a == "-subdiv" || a == "--subdiv" ||
               a == "-subdivLevel" || a == "--subdivLevel" ||
               a == "-subdivisionLevel" || a == "--subdivisionLevel") {
      const char *v = need_value(a.c_str());
      if (!v || !ParseIntStrict(v, &opt->subdivision_level) ||
          opt->subdivision_level < 0 ||
          opt->subdivision_level > tinyusdz::tsd::kMaxLevel) {
        std::cerr << "Invalid subdivision level. Expected 0.."
                  << tinyusdz::tsd::kMaxLevel << ".\n";
        return false;
      }
    } else if (a == "-complexity" || a == "--complexity") {
      // OpenUSD usdrecord refinement presets -> subdivision level
      // (low=1.0, medium=1.1, high=1.2, veryhigh=1.3 -> refine 0/1/2/3).
      const char *v = need_value(a.c_str());
      const std::string s = v ? v : "";
      if (s == "low") {
        opt->subdivision_level = 0;
      } else if (s == "medium") {
        opt->subdivision_level = 1;
      } else if (s == "high") {
        opt->subdivision_level = 2;
      } else if (s == "veryhigh") {
        opt->subdivision_level = 3;
      } else {
        std::cerr << "Invalid -complexity. Expected low|medium|high|veryhigh.\n";
        return false;
      }
    } else if (a == "-autoframe" || a == "--autoframe") {
      opt->autoframe = true;
    } else if (a == "-mask" || a == "--mask") {
      const char *v = need_value(a.c_str());
      if (!v) {
        std::cerr << "-mask requires PRIMPATH[,PRIMPATH...].\n";
        return false;
      }
      // Comma- and/or space-separated absolute prim paths.
      std::string s = v;
      for (char &ch : s) {
        if (ch == ',') ch = ' ';
      }
      std::istringstream iss(s);
      std::string p;
      while (iss >> p) {
        if (!p.empty()) opt->mask.push_back(p);
      }
    } else if (a == "-frames" || a == "--frames" || a == "-f") {
      const char *v = need_value(a.c_str());
      if (!v) {
        std::cerr << "-frames requires a FRAMESPEC.\n";
        return false;
      }
      opt->frames = v;
    } else if (a == "-defaultTime" || a == "--defaultTime") {
      opt->default_time = true;
    } else if (a == "-variant" || a == "--variant") {
      const char *v = need_value(a.c_str());
      if (!v) return false;
      std::string s = v;
      size_t eq = s.find('=');
      if (eq == std::string::npos || eq == 0 || eq + 1 >= s.size()) {
        std::cerr << "Invalid --variant. Expected SET=SELECTION (e.g. "
                     "--variant districtLod=full).\n";
        return false;
      }
      opt->variant_overrides[s.substr(0, eq)] = s.substr(eq + 1);
    } else if (a == "-vk" || a == "--vk") {
      opt->vulkan = true;
    } else if (a == "-vkr" || a == "--vkr") {
      opt->vulkan = true;
      opt->vulkan_rt = true;
    } else if (a == "-js" || a == "--js") {
      const char *v = need_value(a.c_str());
      if (!v) {
        std::cerr << "-js requires a script file path.\n";
        return false;
      }
      opt->js_script = v;
      opt->rt_preview = true;
      opt->direct_prims = false;
    } else if (a == "-mcp" || a == "--mcp") {
      opt->mcp = true;
      opt->rt_preview = true;
      opt->direct_prims = false;
    } else if (a == "-noDirectPrims" || a == "--noDirectPrims") {
      opt->direct_prims = false;
    } else if (a == "-stats" || a == "--stats") {
      opt->stats = true;
    } else if (a == "-noar" || a == "--noar") {
      opt->no_assetresolver = true;
    } else if (!a.empty() && a[0] == '-') {
      std::cerr << "Unknown option: " << a << "\n";
      return false;
    } else {
      positional.push_back(a);
    }
  }
  // -js / -mcp drive output paths from the script / MCP calls, so only the
  // input is required there; an output positional is optional.
  const bool output_optional = opt->mcp || !opt->js_script.empty();
  if (positional.empty() || positional.size() > 2 ||
      (!output_optional && positional.size() != 2)) {
    PrintUsage(argv[0]);
    return false;
  }
  opt->input = positional[0];
  opt->output = positional.size() > 1 ? positional[1] : std::string();
  return true;
}

int NullARResolve(const char *, const std::vector<std::string> &, std::string *,
                  std::string *, void *) {
  return -1;
}

int NullARSize(const char *, uint64_t *, std::string *, void *) { return -1; }

int NullARRead(const char *, uint64_t, uint8_t *, uint64_t *, std::string *,
               void *) {
  return -1;
}

void SetupNullAssetResolution(tinyusdz::AssetResolutionResolver *resolver) {
  if (!resolver) return;
  tinyusdz::AssetResolutionHandler handler;
  handler.resolve_fun = NullARResolve;
  handler.size_fun = NullARSize;
  handler.read_fun = NullARRead;
  handler.write_fun = nullptr;
  handler.userdata = nullptr;
  resolver->register_wildcard_asset_resolution_handler(handler);
}

Vec3 MaterialColor(const RenderScene &scene, const RenderMesh &mesh,
                   int material_id) {
  if (material_id >= 0 && size_t(material_id) < scene.materials.size()) {
    const RenderMaterial &mat = scene.materials[size_t(material_id)];
    if (mat.openPBRShader.has_value()) {
      return Clamp01(FromFloat3(mat.openPBRShader->base_color.value));
    }
    if (mat.surfaceShader.has_value()) {
      return Clamp01(FromFloat3(mat.surfaceShader->diffuseColor.value));
    }
  }
  color3f c = mesh.displayColor;
  return Clamp01(Vec3{c[0], c[1], c[2]});
}

Vec3 MaterialEmission(const RenderScene &scene, int material_id) {
  if (material_id >= 0 && size_t(material_id) < scene.materials.size()) {
    const RenderMaterial &mat = scene.materials[size_t(material_id)];
    if (mat.surfaceShader.has_value()) {
      return FromFloat3(mat.surfaceShader->emissiveColor.value);
    }
  }
  return Vec3{0.0f, 0.0f, 0.0f};
}

float MaterialRoughness(const RenderScene &scene, int material_id) {
  if (material_id >= 0 && size_t(material_id) < scene.materials.size()) {
    const RenderMaterial &mat = scene.materials[size_t(material_id)];
    if (mat.openPBRShader.has_value()) {
      return ClampFloat(mat.openPBRShader->base_roughness.value, 0.02f, 1.0f);
    }
    if (mat.surfaceShader.has_value()) {
      return ClampFloat(mat.surfaceShader->roughness.value, 0.02f, 1.0f);
    }
  }
  return 0.55f;
}

float MaterialMetallic(const RenderScene &scene, int material_id) {
  if (material_id >= 0 && size_t(material_id) < scene.materials.size()) {
    const RenderMaterial &mat = scene.materials[size_t(material_id)];
    if (mat.openPBRShader.has_value()) {
      return ClampFloat(mat.openPBRShader->base_metalness.value, 0.0f, 1.0f);
    }
    if (mat.surfaceShader.has_value()) {
      return ClampFloat(mat.surfaceShader->metallic.value, 0.0f, 1.0f);
    }
  }
  return 0.0f;
}

Vec3 MeshLightEmission(const RenderScene &scene, const RenderMesh &mesh,
                       int material_id, float total_area) {
  if (!mesh.is_area_light) return Vec3{0.0f, 0.0f, 0.0f};
  auto light_color = mesh.get_effective_light_color();
  Vec3 effective{light_color[0], light_color[1], light_color[2]};
  Vec3 material_emission = MaterialEmission(scene, material_id);
  Vec3 result = effective;
  if (mesh.light_material_sync_mode == "independent") {
    result = Add(effective, material_emission);
  } else if (mesh.light_material_sync_mode != "noMaterialResponse") {
    Vec3 tint = material_emission;
    if (Luminance(tint) <= 1.0e-6f) {
      tint = MaterialColor(scene, mesh, material_id);
    }
    result = Mul(effective, tint);
  }
  if (mesh.light_normalize && total_area > 1.0e-8f) {
    result = Mul(result, 1.0f / total_area);
  }
  return result;
}

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

bool BuildNodeMatrixMap(const Node &node,
                        std::unordered_map<std::string, matrix4d> *map) {
  if (!map) return false;
  if (!node.abs_path.empty()) {
    (*map)[node.abs_path] = node.global_matrix;
  }
  for (const Node &child : node.children) {
    BuildNodeMatrixMap(child, map);
  }
  return true;
}

std::unordered_map<std::string, matrix4d> BuildNodeMatrixMap(
    const RenderScene &scene) {
  std::unordered_map<std::string, matrix4d> map;
  for (const Node &root : scene.nodes) {
    BuildNodeMatrixMap(root, &map);
  }
  return map;
}

matrix4d MatrixForPath(const std::unordered_map<std::string, matrix4d> &map,
                       const std::string &path) {
  auto it = map.find(path);
  return (it == map.end()) ? matrix4d::identity() : it->second;
}

Vec3 TransformNormal(const matrix4d &inv_world, const Vec3 &n) {
  return Normalize(Vec3{
      float(inv_world.m[0][0] * double(n.x) + inv_world.m[0][1] * double(n.y) +
            inv_world.m[0][2] * double(n.z)),
      float(inv_world.m[1][0] * double(n.x) + inv_world.m[1][1] * double(n.y) +
            inv_world.m[1][2] * double(n.z)),
      float(inv_world.m[2][0] * double(n.x) + inv_world.m[2][1] * double(n.y) +
            inv_world.m[2][2] * double(n.z))});
}

int AxisIndex(tinyusdz::Axis axis) {
  switch (axis) {
    case tinyusdz::Axis::X: return 0;
    case tinyusdz::Axis::Y: return 1;
    case tinyusdz::Axis::Z: return 2;
  }
  return 2;
}

Vec3 AxisVec(tinyusdz::Axis axis) {
  switch (axis) {
    case tinyusdz::Axis::X: return Vec3{1.0f, 0.0f, 0.0f};
    case tinyusdz::Axis::Y: return Vec3{0.0f, 1.0f, 0.0f};
    case tinyusdz::Axis::Z: return Vec3{0.0f, 0.0f, 1.0f};
  }
  return Vec3{0.0f, 0.0f, 1.0f};
}

float Coord(const Vec3 &v, int axis) {
  return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}

Vec3 WithCoord(Vec3 v, int axis, float c) {
  if (axis == 0) v.x = c;
  if (axis == 1) v.y = c;
  if (axis == 2) v.z = c;
  return v;
}

Vec3 RadialPart(Vec3 v, int axis) {
  return WithCoord(v, axis, 0.0f);
}

bool SolveQuadratic(float a, float b, float c, float *t0, float *t1) {
  if (std::abs(a) < 1.0e-12f) {
    if (std::abs(b) < 1.0e-12f) return false;
    *t0 = *t1 = -c / b;
    return true;
  }
  float disc = b * b - 4.0f * a * c;
  if (disc < 0.0f) return false;
  float s = std::sqrt(disc);
  float q = -0.5f * (b + std::copysign(s, b));
  *t0 = q / a;
  *t1 = (std::abs(q) > 1.0e-20f) ? c / q : (-b + s) / (2.0f * a);
  if (*t0 > *t1) std::swap(*t0, *t1);
  return true;
}

bool IntersectTriangleMT(const Vec3 &o, const Vec3 &d, const Vec3 &a,
                         const Vec3 &b, const Vec3 &c, float tmin,
                         float tmax, float *t) {
  Vec3 e1 = Sub(b, a);
  Vec3 e2 = Sub(c, a);
  Vec3 p = Cross(d, e2);
  float det = Dot(e1, p);
  if (std::abs(det) < 1.0e-12f) return false;
  float inv_det = 1.0f / det;
  Vec3 s = Sub(o, a);
  float u = inv_det * Dot(s, p);
  if (u < 0.0f || u > 1.0f) return false;
  Vec3 q = Cross(s, e1);
  float v = inv_det * Dot(d, q);
  if (v < 0.0f || u + v > 1.0f) return false;
  float tt = inv_det * Dot(e2, q);
  if (tt < tmin || tt > tmax) return false;
  if (t) *t = tt;
  return true;
}

bool IntersectTetPrim(const TetPrim &tet, const Vec3 &o, const Vec3 &d,
                      float tmin, float tmax, float *best_t, Vec3 *normal) {
  const int faces[4][4] = {
      {0, 2, 1, 3}, {0, 1, 3, 2}, {0, 3, 2, 1}, {1, 2, 3, 0},
  };
  bool hit = false;
  float best = tmax;
  Vec3 best_n{0.0f, 1.0f, 0.0f};
  for (const auto &f : faces) {
    const Vec3 &a = tet.p[f[0]];
    const Vec3 &b = tet.p[f[1]];
    const Vec3 &c = tet.p[f[2]];
    const Vec3 &opp = tet.p[f[3]];
    Vec3 n = Cross(Sub(b, a), Sub(c, a));
    if (Dot(n, Sub(opp, a)) > 0.0f) n = Mul(n, -1.0f);
    n = Normalize(n);
    float t = 0.0f;
    if (IntersectTriangleMT(o, d, a, b, c, tmin, best, &t)) {
      best = t;
      best_n = (Dot(n, d) > 0.0f) ? Mul(n, -1.0f) : n;
      hit = true;
    }
  }
  if (!hit) return false;
  if (best_t) *best_t = best;
  if (normal) *normal = best_n;
  return true;
}

int TetUserIntersect(const lrt_ray *ray, uint32_t prim_id, void *user,
                     float *t, float *u, float *v) {
  const std::vector<TetPrim> *tets =
      reinterpret_cast<const std::vector<TetPrim> *>(user);
  if (!ray || !tets || prim_id >= tets->size()) return 0;
  Vec3 o{ray->org[0], ray->org[1], ray->org[2]};
  Vec3 d{ray->dir[0], ray->dir[1], ray->dir[2]};
  float tt = ray->tmax;
  Vec3 n;
  if (!IntersectTetPrim((*tets)[prim_id], o, d, ray->tmin, ray->tmax, &tt, &n)) {
    return 0;
  }
  if (t) *t = tt;
  if (u) *u = 0.0f;
  if (v) *v = 0.0f;
  return 1;
}

int TetUserOccluded(const lrt_ray *ray, uint32_t prim_id, void *user) {
  return TetUserIntersect(ray, prim_id, user, nullptr, nullptr, nullptr);
}

bool AcceptT(float t, float tmin, float tmax, float *best) {
  if (t >= tmin && t <= tmax && t < *best) {
    *best = t;
    return true;
  }
  return false;
}

bool IntersectDirectShape(const DirectShape &shape, const Vec3 &ray_org,
                          const Vec3 &ray_dir, float tmin, float tmax,
                          DirectHit *hit) {
  Vec3 o = TransformPoint(shape.inv_world, ray_org);
  Vec3 d = TransformVector(shape.inv_world, ray_dir);
  const int ax = AxisIndex(shape.axis);
  const float half_h = float(std::max(0.0, shape.height) * 0.5);
  const float radius = float(std::max(0.0, shape.radius));
  float best = tmax;
  Vec3 nlocal{0.0f, 1.0f, 0.0f};
  bool found = false;

  if (shape.type == DirectShape::Type::Cylinder) {
    Vec3 ro = RadialPart(o, ax);
    Vec3 rd = RadialPart(d, ax);
    float t0, t1;
    if (SolveQuadratic(Dot(rd, rd), 2.0f * Dot(ro, rd),
                       Dot(ro, ro) - radius * radius, &t0, &t1)) {
      for (float t : {t0, t1}) {
        float y = Coord(o, ax) + t * Coord(d, ax);
        if (y >= -half_h && y <= half_h && AcceptT(t, tmin, best, &best)) {
          nlocal = Normalize(RadialPart(Add(o, Mul(d, t)), ax));
          found = true;
        }
      }
    }
    for (float cap : {-half_h, half_h}) {
      float denom = Coord(d, ax);
      if (std::abs(denom) < 1.0e-12f) continue;
      float t = (cap - Coord(o, ax)) / denom;
      Vec3 p = Add(o, Mul(d, t));
      if (Dot(RadialPart(p, ax), RadialPart(p, ax)) <= radius * radius &&
          AcceptT(t, tmin, best, &best)) {
        nlocal = Mul(AxisVec(shape.axis), cap < 0.0f ? -1.0f : 1.0f);
        found = true;
      }
    }
  } else if (shape.type == DirectShape::Type::Cone) {
    const float apex = half_h;
    const float base = -half_h;
    const float k = (half_h > 0.0f) ? radius / (2.0f * half_h) : 0.0f;
    const float oy = Coord(o, ax);
    const float dy = Coord(d, ax);
    Vec3 ro = RadialPart(o, ax);
    Vec3 rd = RadialPart(d, ax);
    float t0, t1;
    if (SolveQuadratic(Dot(rd, rd) - k * k * dy * dy,
                       2.0f * (Dot(ro, rd) - k * k * (oy - apex) * dy),
                       Dot(ro, ro) - k * k * (oy - apex) * (oy - apex),
                       &t0, &t1)) {
      for (float t : {t0, t1}) {
        float y = oy + t * dy;
        if (y >= base && y <= apex && AcceptT(t, tmin, best, &best)) {
          Vec3 p = Add(o, Mul(d, t));
          Vec3 radial = RadialPart(p, ax);
          nlocal = Normalize(Add(radial, Mul(AxisVec(shape.axis), k * Length(radial))));
          found = true;
        }
      }
    }
    if (std::abs(dy) > 1.0e-12f) {
      float t = (base - oy) / dy;
      Vec3 p = Add(o, Mul(d, t));
      if (Dot(RadialPart(p, ax), RadialPart(p, ax)) <= radius * radius &&
          AcceptT(t, tmin, best, &best)) {
        nlocal = Mul(AxisVec(shape.axis), -1.0f);
        found = true;
      }
    }
  } else {
    Vec3 a = Mul(AxisVec(shape.axis), -half_h);
    Vec3 b = Mul(AxisVec(shape.axis), half_h);
    Vec3 ba = Sub(b, a);
    Vec3 oa = Sub(o, a);
    float baba = Dot(ba, ba);
    float bard = Dot(ba, d);
    float baoa = Dot(ba, oa);
    float rdoa = Dot(d, oa);
    float oaoa = Dot(oa, oa);
    float A = baba - bard * bard;
    float B = baba * rdoa - baoa * bard;
    float C = baba * oaoa - baoa * baoa - radius * radius * baba;
    float h = B * B - A * C;
    if (h >= 0.0f && std::abs(A) > 1.0e-12f) {
      float t = (-B - std::sqrt(h)) / A;
      float y = baoa + t * bard;
      if (y > 0.0f && y < baba && AcceptT(t, tmin, best, &best)) {
        Vec3 p = Add(oa, Mul(d, t));
        nlocal = Normalize(Sub(p, Mul(ba, y / baba)));
        found = true;
      }
    }
    for (Vec3 c : {a, b}) {
      Vec3 oc = Sub(o, c);
      float t0, t1;
      if (SolveQuadratic(Dot(d, d), 2.0f * Dot(oc, d),
                         Dot(oc, oc) - radius * radius, &t0, &t1)) {
        for (float t : {t0, t1}) {
          if (AcceptT(t, tmin, best, &best)) {
            nlocal = Normalize(Sub(Add(o, Mul(d, t)), c));
            found = true;
          }
        }
      }
    }
  }

  if (!found || !hit) return false;
  hit->t = best;
  hit->n = TransformNormal(shape.inv_world, nlocal);
  hit->base_color = shape.base_color;
  hit->emission = shape.emission;
  hit->hit = true;
  return true;
}

float TriangleArea(const Vec3 &p0, const Vec3 &p1, const Vec3 &p2) {
  return 0.5f * Length(Cross(Sub(p1, p0), Sub(p2, p0)));
}

std::vector<int> FaceMaterialIds(const RenderMesh &mesh) {
  const std::vector<uint32_t> &counts = mesh.faceVertexCounts();
  std::vector<int> ids(counts.size(), mesh.material_id);
  for (const auto &kv : mesh.material_subsetMap) {
    const tinyusdz::tydra::MaterialSubset &subset = kv.second;
    const std::vector<int> &faces = subset.indices();
    for (int f : faces) {
      if (f >= 0 && size_t(f) < ids.size()) {
        ids[size_t(f)] = subset.material_id;
      }
    }
  }
  return ids;
}

void AddMeshTriangles(const RenderScene &scene, const RenderMesh &mesh,
                      const matrix4d &world, std::vector<float> *vertices,
                      std::vector<TriInfo> *tris, Bounds *bounds,
                      LightCache *lights = nullptr) {
  if (!vertices || !tris || !bounds) return;
  const std::vector<uint32_t> &indices = mesh.faceVertexIndices();
  const std::vector<uint32_t> &counts = mesh.faceVertexCounts();
  std::vector<int> material_ids = FaceMaterialIds(mesh);
  float mesh_area = 0.0f;
  if (mesh.is_area_light) {
    size_t area_cursor = 0;
    for (size_t face = 0; face < counts.size(); face++) {
      uint32_t nverts = counts[face];
      if (nverts < 3 || area_cursor + nverts > indices.size()) {
        area_cursor += nverts;
        continue;
      }
      for (uint32_t k = 1; k + 1 < nverts; k++) {
        uint32_t i0 = indices[area_cursor + 0];
        uint32_t i1 = indices[area_cursor + k];
        uint32_t i2 = indices[area_cursor + k + 1];
        if (i0 >= mesh.points.size() || i1 >= mesh.points.size() ||
            i2 >= mesh.points.size()) {
          continue;
        }
        mesh_area += TriangleArea(TransformPoint(world, FromFloat3(mesh.points[i0])),
                                  TransformPoint(world, FromFloat3(mesh.points[i1])),
                                  TransformPoint(world, FromFloat3(mesh.points[i2])));
      }
      area_cursor += nverts;
    }
  }
  size_t cursor = 0;
  for (size_t face = 0; face < counts.size(); face++) {
    uint32_t nverts = counts[face];
    if (nverts < 3 || cursor + nverts > indices.size()) {
      cursor += nverts;
      continue;
    }
    int mat_id = (face < material_ids.size()) ? material_ids[face] : mesh.material_id;
    for (uint32_t k = 1; k + 1 < nverts; k++) {
      uint32_t i0 = indices[cursor + 0];
      uint32_t i1 = indices[cursor + k];
      uint32_t i2 = indices[cursor + k + 1];
      if (i0 >= mesh.points.size() || i1 >= mesh.points.size() ||
          i2 >= mesh.points.size()) {
        continue;
      }
      Vec3 p0 = TransformPoint(world, FromFloat3(mesh.points[i0]));
      Vec3 p1 = TransformPoint(world, FromFloat3(mesh.points[i1]));
      Vec3 p2 = TransformPoint(world, FromFloat3(mesh.points[i2]));
      Vec3 n = Normalize(Cross(Sub(p1, p0), Sub(p2, p0)));
      if (!mesh.is_rightHanded) {
        n = Mul(n, -1.0f);
      }
      TriInfo tri;
      tri.p0 = p0;
      tri.p1 = p1;
      tri.p2 = p2;
      tri.n = n;
      tri.base_color = MaterialColor(scene, mesh, mat_id);
      tri.emission = MaterialEmission(scene, mat_id);
      tri.roughness = MaterialRoughness(scene, mat_id);
      tri.metallic = MaterialMetallic(scene, mat_id);
      if (mesh.is_area_light) {
        tri.emission = MeshLightEmission(scene, mesh, mat_id, mesh_area);
      }
      vertices->push_back(p0.x);
      vertices->push_back(p0.y);
      vertices->push_back(p0.z);
      vertices->push_back(p1.x);
      vertices->push_back(p1.y);
      vertices->push_back(p1.z);
      vertices->push_back(p2.x);
      vertices->push_back(p2.y);
      vertices->push_back(p2.z);
      int tri_id = int(tris->size());
      tris->push_back(tri);
      if (lights && mesh.is_area_light && Luminance(tri.emission) > 1.0e-6f) {
        float area = TriangleArea(p0, p1, p2);
        if (area > 1.0e-10f) {
          PreviewLight ml;
          ml.kind = PreviewLight::Kind::Mesh;
          ml.position = Mul(Add(Add(p0, p1), p2), 1.0f / 3.0f);
          ml.normal = n;
          ml.direction = Mul(n, -1.0f);
          ml.radiance = tri.emission;
          ml.area = area;
          ml.power = std::max(0.0f, Luminance(ml.radiance) * area);
          ml.tri_id = tri_id;
          lights->mesh.push_back(ml);
        }
      }
      Expand(bounds, p0);
      Expand(bounds, p1);
      Expand(bounds, p2);
    }
    cursor += nverts;
  }
}

void CollectGeometry(const RenderScene &scene, const Node &node,
                     std::vector<float> *vertices, std::vector<TriInfo> *tris,
                     Bounds *bounds,
                     const std::unordered_set<std::string> *skip_paths,
                     LightCache *lights) {
  if (node.nodeType == NodeType::Mesh && node.id >= 0 &&
      size_t(node.id) < scene.meshes.size()) {
    const RenderMesh &mesh = scene.meshes[size_t(node.id)];
    if (!skip_paths || !skip_paths->count(mesh.abs_path)) {
      AddMeshTriangles(scene, mesh, node.global_matrix, vertices, tris, bounds,
                       lights);
    }
  }
  for (const Node &child : node.children) {
    CollectGeometry(scene, child, vertices, tris, bounds, skip_paths, lights);
  }
}

void CollectAllGeometry(const RenderScene &scene, std::vector<float> *vertices,
                        std::vector<TriInfo> *tris, Bounds *bounds,
                        const std::unordered_set<std::string> *skip_paths,
                        LightCache *lights) {
  for (const Node &root : scene.nodes) {
    CollectGeometry(scene, root, vertices, tris, bounds, skip_paths, lights);
  }
  for (const tinyusdz::tydra::RenderInstance &inst : scene.instances) {
    if (inst.mesh_id >= 0 && size_t(inst.mesh_id) < scene.meshes.size() &&
        inst.visible) {
      const RenderMesh &mesh = scene.meshes[size_t(inst.mesh_id)];
      AddMeshTriangles(scene, mesh, inst.global_matrix, vertices, tris, bounds,
                       lights);
    }
  }
}

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

template <typename T>
bool BorrowMMapArray(const tinyusdz::Stage &stage, const std::string &prim_path,
                     const std::string &attr_name, BorrowedArrayView<T> *out) {
  if (!out || !stage.has_mmap_zero_copy()) return false;
  const tinyusdz::MMapArrayRef *ref =
      stage.mmap_table()->find_compatible(prim_path, attr_name);
  if (!ref) return false;
  const tinyusdz::MMapDataSource *source = stage.mmap_source();
  if (!source || !source->is_valid()) return false;
  if (ref->element_size != sizeof(T)) return false;
  if (ref->element_count > (UINT64_MAX / sizeof(T))) return false;
  uint64_t byte_count = ref->element_count * sizeof(T);
  if (ref->byte_offset > source->size()) return false;
  if (byte_count > (source->size() - ref->byte_offset)) return false;
  const uint8_t *bytes = source->addr() + ref->byte_offset;
  const T *ptr = nullptr;
  if (reinterpret_cast<uintptr_t>(bytes) % alignof(T) == 0) {
    ptr = reinterpret_cast<const T *>(bytes);
  }
  if (ref->element_count > uint64_t((std::numeric_limits<size_t>::max)())) {
    return false;
  }
  out->data = ptr;
  out->bytes = bytes;
  out->count = static_cast<size_t>(ref->element_count);
  out->mmap = true;
  out->owned.clear();
  return true;
}

template <typename T>
T ReadBorrowedArrayValue(const BorrowedArrayView<T> &view, size_t index) {
  if (view.data) return view.data[index];
  T value{};
  std::memcpy(&value, view.bytes + index * sizeof(T), sizeof(T));
  return value;
}

// Zero-copy const-ref access to an in-memory array attribute. Returns the
// underlying vector without copying when the attribute holds a static (non
// time-sampled, non-connected, non-blocked) default value. Returns nullptr for
// time-sampled/connected/blocked attributes (the caller should fall back to the
// copying EvalAnim() path) or for deferred mmap arrays (empty vector in
// mmap_zero_copy mode; the caller should try BorrowMMapArray() first).
template <typename T>
const std::vector<T> *BorrowScalarVector(
    const tinyusdz::TypedAttribute<tinyusdz::Animatable<std::vector<T>>> &attr) {
  if (attr.is_blocked() || attr.has_connections()) return nullptr;
  const auto &opt = attr.get_value_ref();
  if (!opt) return nullptr;
  const tinyusdz::Animatable<std::vector<T>> &anim = opt.value();
  if (anim.is_scalar() && anim.has_default()) {
    return &anim.get_scalar_ref();
  }
  return nullptr;
}

// Number of worker threads to use for the embarrassingly-parallel mesh-stream
// and render passes. `requested` is the user's -threads value (<=0 means auto).
unsigned WorkerThreadCount(int requested) {
  if (requested > 0) return unsigned(requested);
  unsigned hw = std::thread::hardware_concurrency();
  return hw > 0 ? hw : 1u;
}

const tinyusdz::Xformable *AsPreviewXformable(const tinyusdz::Prim &prim) {
  if (const tinyusdz::Xform *x = prim.as<tinyusdz::Xform>()) return x;
  if (const tinyusdz::GeomMesh *m = prim.as<tinyusdz::GeomMesh>()) return m;
  if (const tinyusdz::GeomCamera *c = prim.as<tinyusdz::GeomCamera>()) return c;
  return nullptr;
}

const tinyusdz::GPrim *AsPreviewGPrim(const tinyusdz::Prim &prim) {
  if (const tinyusdz::Xform *p = prim.as<tinyusdz::Xform>()) return p;
  if (const tinyusdz::GeomMesh *p = prim.as<tinyusdz::GeomMesh>()) return p;
  if (const tinyusdz::GeomCamera *p = prim.as<tinyusdz::GeomCamera>()) return p;
  if (const tinyusdz::GeomCube *p = prim.as<tinyusdz::GeomCube>()) return p;
  if (const tinyusdz::GeomSphere *p = prim.as<tinyusdz::GeomSphere>()) return p;
  if (const tinyusdz::GeomCone *p = prim.as<tinyusdz::GeomCone>()) return p;
  if (const tinyusdz::GeomCylinder *p = prim.as<tinyusdz::GeomCylinder>()) return p;
  if (const tinyusdz::GeomCapsule *p = prim.as<tinyusdz::GeomCapsule>()) return p;
  if (const tinyusdz::GeomPlane *p = prim.as<tinyusdz::GeomPlane>()) return p;
  if (const tinyusdz::GeomTetMesh *p = prim.as<tinyusdz::GeomTetMesh>()) return p;
  if (const tinyusdz::GeomNurbsPatch *p = prim.as<tinyusdz::GeomNurbsPatch>()) {
    return p;
  }
  if (const tinyusdz::GeomBasisCurves *p = prim.as<tinyusdz::GeomBasisCurves>()) {
    return p;
  }
  if (const tinyusdz::GeomHermiteCurves *p =
          prim.as<tinyusdz::GeomHermiteCurves>()) {
    return p;
  }
  if (const tinyusdz::GeomNurbsCurves *p = prim.as<tinyusdz::GeomNurbsCurves>()) {
    return p;
  }
  if (const tinyusdz::GeomPoints *p = prim.as<tinyusdz::GeomPoints>()) return p;
  if (const tinyusdz::GeomPointInstancer *p =
          prim.as<tinyusdz::GeomPointInstancer>()) {
    return p;
  }
  return nullptr;
}

matrix4d LocalMatrixOrIdentity(const tinyusdz::Xformable *xformable, double time,
                               bool *reset) {
  if (reset) *reset = false;
  if (!xformable) return matrix4d::identity();
  bool local_reset = false;
  auto ret = xformable->GetLocalMatrix(
      time, tinyusdz::value::TimeSampleInterpolationType::Linear, &local_reset);
  if (reset) *reset = local_reset;
  if (!ret) return matrix4d::identity();
  return ret.value();
}

template <typename T>
bool EvalAnim(const tinyusdz::Stage &stage,
              const tinyusdz::TypedAttribute<tinyusdz::Animatable<T>> &attr,
              const std::string &name, double time, T *out) {
  std::string err;
  return tinyusdz::tydra::EvaluateTypedAnimatableAttribute(
      stage, attr, name, out, &err, time);
}

template <typename T>
bool EvalAnimFallback(
    const tinyusdz::Stage &stage,
    const tinyusdz::TypedAttributeWithFallback<tinyusdz::Animatable<T>> &attr,
    const std::string &name, double time, T *out) {
  std::string err;
  return tinyusdz::tydra::EvaluateTypedAnimatableAttribute(
      stage, attr, name, out, &err, time);
}

uint32_t PurposeBit(tinyusdz::Purpose purpose) {
  switch (purpose) {
    case tinyusdz::Purpose::Render:
      return kPurposeRenderBit;
    case tinyusdz::Purpose::Proxy:
      return kPurposeProxyBit;
    case tinyusdz::Purpose::Guide:
      return kPurposeGuideBit;
    case tinyusdz::Purpose::Default:
    default:
      return kPurposeDefaultBit;
  }
}

tinyusdz::Purpose ResolvePurpose(const tinyusdz::Prim &prim,
                                 tinyusdz::Purpose inherited) {
  if (const tinyusdz::GPrim *gprim = AsPreviewGPrim(prim)) {
    tinyusdz::Purpose purpose = gprim->purpose.get_value();
    if (purpose != tinyusdz::Purpose::Default) return purpose;
  }
  return inherited;
}

bool PurposeVisible(uint32_t purpose_bit, uint32_t purpose_mask) {
  return (purpose_bit & purpose_mask) != 0;
}

bool AddRTPreviewMesh(const tinyusdz::Stage &stage, const std::string &prim_path,
                      const tinyusdz::GeomMesh &mesh, const matrix4d &world,
                      double time, tinyusdz::Purpose purpose,
                      uint32_t purpose_mask,
                      std::vector<float> *vertices, std::vector<TriInfo> *tris,
                      Bounds *bounds, RTPreviewStats *stats) {
  if (!vertices || !tris || !bounds || !stats) return false;
  BorrowedArrayView<tinyusdz::value::point3f> points;
  if (BorrowMMapArray(stage, prim_path, "points", &points)) {
    stats->meshes_with_mmap_points++;
  } else if (const std::vector<tinyusdz::value::point3f> *pv =
                 BorrowScalarVector(mesh.points)) {
    // Zero-copy: in-memory (materialized) points vector.
    points.data = pv->data();
    points.bytes = reinterpret_cast<const uint8_t *>(pv->data());
    points.count = pv->size();
    points.mmap = false;
    stats->meshes_with_owned_points++;
  } else {
    // Fallback (time-sampled/connected): copy via attribute evaluation.
    if (!EvalAnim(stage, mesh.points, "points", time, &points.owned)) {
      stats->skipped_meshes++;
      return false;
    }
    points.data = points.owned.data();
    points.bytes = reinterpret_cast<const uint8_t *>(points.owned.data());
    points.count = points.owned.size();
    points.mmap = false;
    stats->meshes_with_owned_points++;
    stats->copied_point_bytes +=
        uint64_t(points.count) * sizeof(tinyusdz::value::point3f);
  }

  // Topology: prefer zero-copy const-ref to the in-memory vectors; fall back to
  // a copy only for time-sampled/connected attributes.
  std::vector<int32_t> counts_owned;
  std::vector<int32_t> indices_owned;
  const std::vector<int32_t> *counts_ptr =
      BorrowScalarVector(mesh.faceVertexCounts);
  if (!counts_ptr) {
    if (!EvalAnim(stage, mesh.faceVertexCounts, "faceVertexCounts", time,
                  &counts_owned)) {
      stats->skipped_meshes++;
      return false;
    }
    counts_ptr = &counts_owned;
    stats->copied_topology_bytes += uint64_t(counts_owned.size()) * sizeof(int32_t);
  }
  const std::vector<int32_t> *indices_ptr =
      BorrowScalarVector(mesh.faceVertexIndices);
  if (!indices_ptr) {
    if (!EvalAnim(stage, mesh.faceVertexIndices, "faceVertexIndices", time,
                  &indices_owned)) {
      stats->skipped_meshes++;
      return false;
    }
    indices_ptr = &indices_owned;
    stats->copied_topology_bytes += uint64_t(indices_owned.size()) * sizeof(int32_t);
  }
  const std::vector<int32_t> &counts = *counts_ptr;
  const std::vector<int32_t> &indices = *indices_ptr;
  if ((!points.data && !points.bytes) || points.count == 0 || counts.empty() ||
      indices.empty()) {
    stats->skipped_meshes++;
    return false;
  }

  // Reserve output buffers up-front from the exact triangle-fan estimate to
  // avoid repeated reallocation while appending.
  size_t tri_estimate = 0;
  for (int32_t c : counts) {
    if (c >= 3) tri_estimate += size_t(c - 2);
  }
  if (tri_estimate) {
    vertices->reserve(vertices->size() + tri_estimate * 9);
    tris->reserve(tris->size() + tri_estimate);
  }

  size_t cursor = 0;
  for (int32_t c : counts) {
    if (c < 3 || cursor + size_t(c) > indices.size()) {
      cursor += size_t(std::max<int32_t>(0, c));
      continue;
    }
    for (int32_t k = 1; k + 1 < c; k++) {
      int32_t i0 = indices[cursor + 0];
      int32_t i1 = indices[cursor + size_t(k)];
      int32_t i2 = indices[cursor + size_t(k + 1)];
      if (i0 < 0 || i1 < 0 || i2 < 0 || size_t(i0) >= points.count ||
          size_t(i1) >= points.count || size_t(i2) >= points.count) {
        continue;
      }
      Vec3 p0 = TransformPoint(
          world, FromPoint3(ReadBorrowedArrayValue(points, size_t(i0))));
      Vec3 p1 = TransformPoint(
          world, FromPoint3(ReadBorrowedArrayValue(points, size_t(i1))));
      Vec3 p2 = TransformPoint(
          world, FromPoint3(ReadBorrowedArrayValue(points, size_t(i2))));
      Vec3 n = Normalize(Cross(Sub(p1, p0), Sub(p2, p0)));
      if (Length(n) < 1.0e-12f) continue;

      TriInfo tri;
      tri.p0 = p0;
      tri.p1 = p1;
      tri.p2 = p2;
      tri.n = n;
      tri.base_color = Vec3{0.55f, 0.55f, 0.55f};
      tri.purpose_bit = PurposeBit(purpose);
      if (tri.purpose_bit == kPurposeRenderBit) {
        stats->purpose_render_triangles++;
      } else if (tri.purpose_bit == kPurposeProxyBit) {
        stats->purpose_proxy_triangles++;
      } else if (tri.purpose_bit == kPurposeGuideBit) {
        stats->purpose_guide_triangles++;
      } else {
        stats->purpose_default_triangles++;
      }
      const bool visible_for_fit = PurposeVisible(tri.purpose_bit, purpose_mask);
      vertices->insert(vertices->end(),
                       {p0.x, p0.y, p0.z, p1.x, p1.y, p1.z, p2.x, p2.y, p2.z});
      tris->push_back(tri);
      stats->triangles++;
      if (visible_for_fit) {
        Expand(bounds, p0);
        Expand(bounds, p1);
        Expand(bounds, p2);
      }
    }
    cursor += size_t(c);
  }
  return true;
}

// A single mesh-extraction work item produced by the (serial) tree walk and
// consumed by the parallel mesh-stream workers.
struct MeshJob {
  const tinyusdz::GeomMesh *mesh{nullptr};
  matrix4d world{matrix4d::identity()};
  tinyusdz::Purpose purpose{tinyusdz::Purpose::Default};
  std::string prim_path;
};

// Serial: resolve world matrices / purpose (parent-dependent) and flatten the
// renderable GeomMesh prims into `jobs`. The per-triangle work happens later in
// parallel; this walk only does cheap per-prim xform/purpose evaluation.
void CollectRTPreviewMeshes(const tinyusdz::Prim &prim,
                            const matrix4d &parent_world, double time,
                            tinyusdz::Purpose inherited_purpose,
                            std::vector<MeshJob> *jobs) {
  bool reset = false;
  const matrix4d local =
      LocalMatrixOrIdentity(AsPreviewXformable(prim), time, &reset);
  const matrix4d world = reset ? local : (local * parent_world);
  const tinyusdz::Purpose purpose = ResolvePurpose(prim, inherited_purpose);
  if (const tinyusdz::GeomMesh *mesh = prim.as<tinyusdz::GeomMesh>()) {
    MeshJob job;
    job.mesh = mesh;
    job.world = world;
    job.purpose = purpose;
    job.prim_path = prim.absolute_path().full_path_name();
    jobs->push_back(std::move(job));
  }
  for (const tinyusdz::Prim &child : prim.children()) {
    CollectRTPreviewMeshes(child, world, time, purpose, jobs);
  }
}

void MergeStats(RTPreviewStats *dst, const RTPreviewStats &src) {
  dst->meshes_with_mmap_points += src.meshes_with_mmap_points;
  dst->meshes_with_owned_points += src.meshes_with_owned_points;
  dst->skipped_meshes += src.skipped_meshes;
  dst->triangles += src.triangles;
  dst->copied_point_bytes += src.copied_point_bytes;
  dst->copied_topology_bytes += src.copied_topology_bytes;
  dst->purpose_default_triangles += src.purpose_default_triangles;
  dst->purpose_render_triangles += src.purpose_render_triangles;
  dst->purpose_proxy_triangles += src.purpose_proxy_triangles;
  dst->purpose_guide_triangles += src.purpose_guide_triangles;
}

void MergeBounds(Bounds *dst, const Bounds &src) {
  if (!src.valid) return;
  Expand(dst, src.lo);
  Expand(dst, src.hi);
}

bool BuildRTPreviewScene(const tinyusdz::Stage &stage, const Options &opt,
                         std::vector<float> *vertices,
                         std::vector<TriInfo> *tris, Bounds *bounds,
                         RTPreviewStats *stats, std::string *err) {
  if (!vertices || !tris || !bounds || !stats) return false;
  vertices->clear();
  tris->clear();
  *bounds = Bounds();
  *stats = RTPreviewStats();
  if (stage.has_mmap_zero_copy()) {
    stats->mmap_deferred_bytes = stage.mmap_table()->total_deferred_bytes();
  }
  const auto t0 = std::chrono::steady_clock::now();

  // Pass A (serial, cheap): flatten the prim tree into per-mesh jobs.
  std::vector<MeshJob> jobs;
  for (const tinyusdz::Prim &root : stage.root_prims()) {
    CollectRTPreviewMeshes(root, matrix4d::identity(), opt.timecode,
                           tinyusdz::Purpose::Default, &jobs);
  }
  stats->meshes = jobs.size();

  // Pass B (parallel): extract + triangulate each mesh into its own result
  // buffer (disjoint writes, no locking). Work-stealing via an atomic cursor
  // balances the highly non-uniform per-mesh cost.
  struct MeshResult {
    std::vector<float> vertices;
    std::vector<TriInfo> tris;
    Bounds bounds;
    RTPreviewStats stats;
  };
  std::vector<MeshResult> results(jobs.size());
  const unsigned nthreads =
      std::min<unsigned>(WorkerThreadCount(opt.threads),
                         jobs.empty() ? 1u : unsigned(jobs.size()));
  std::atomic<size_t> cursor{0};
  auto worker = [&]() {
    for (;;) {
      const size_t i = cursor.fetch_add(1, std::memory_order_relaxed);
      if (i >= jobs.size()) break;
      const MeshJob &job = jobs[i];
      MeshResult &r = results[i];
      AddRTPreviewMesh(stage, job.prim_path, *job.mesh, job.world, opt.timecode,
                       job.purpose, opt.purpose_mask, &r.vertices, &r.tris,
                       &r.bounds, &r.stats);
    }
  };
  if (nthreads <= 1) {
    worker();
  } else {
    std::vector<std::thread> pool;
    pool.reserve(nthreads);
    for (unsigned t = 0; t < nthreads; ++t) pool.emplace_back(worker);
    for (std::thread &th : pool) th.join();
  }

  // Pass C (serial merge): concatenate in job order for deterministic output,
  // freeing each chunk as we go to bound peak memory.
  size_t total_floats = 0;
  size_t total_tris = 0;
  for (const MeshResult &r : results) {
    total_floats += r.vertices.size();
    total_tris += r.tris.size();
  }
  vertices->reserve(total_floats);
  tris->reserve(total_tris);
  for (MeshResult &r : results) {
    vertices->insert(vertices->end(), r.vertices.begin(), r.vertices.end());
    tris->insert(tris->end(), r.tris.begin(), r.tris.end());
    MergeBounds(bounds, r.bounds);
    MergeStats(stats, r.stats);
    std::vector<float>().swap(r.vertices);
    std::vector<TriInfo>().swap(r.tris);
  }

  const auto t1 = std::chrono::steady_clock::now();
  stats->build_seconds = std::chrono::duration<double>(t1 - t0).count();
  stats->packed_triangle_bytes = uint64_t(vertices->size()) * sizeof(float);
  if (tris->empty()) {
    if (err) *err = "RT preview found no renderable Mesh triangles.";
    return false;
  }
  return true;
}

bool MatchPrimNameOrPath(const tinyusdz::Prim &prim, const std::string &query) {
  if (query.empty()) return true;
  const std::string path = prim.absolute_path().full_path_name();
  return path == query || prim.element_name() == query;
}

bool CameraFrameFromGeomCamera(const tinyusdz::Stage &stage,
                               const tinyusdz::GeomCamera &cam,
                               const matrix4d &world, double time,
                               CameraFrame *frame) {
  if (!frame) return false;
  float focal_length = 50.0f;
  float vertical_aperture = 15.2908f;
  float horizontal_aperture = 20.955f;
  tinyusdz::value::float2 clipping_range{0.1f, 1000000.0f};
  tinyusdz::GeomCamera::Projection projection =
      tinyusdz::GeomCamera::Projection::Perspective;
  EvalAnimFallback(stage, cam.focalLength, "focalLength", time, &focal_length);
  EvalAnimFallback(stage, cam.verticalAperture, "verticalAperture", time,
                   &vertical_aperture);
  EvalAnimFallback(stage, cam.horizontalAperture, "horizontalAperture", time,
                   &horizontal_aperture);
  EvalAnimFallback(stage, cam.clippingRange, "clippingRange", time,
                   &clipping_range);
  cam.projection.get_value().get_scalar(&projection);

  frame->origin =
      Vec3{float(world.m[3][0]), float(world.m[3][1]), float(world.m[3][2])};
  frame->right = Normalize(TransformVector(world, Vec3{1.0f, 0.0f, 0.0f}));
  frame->up = Normalize(TransformVector(world, Vec3{0.0f, 1.0f, 0.0f}));
  frame->forward = Normalize(TransformVector(world, Vec3{0.0f, 0.0f, -1.0f}));
  frame->yfov = 2.0f * std::atan(0.5f * vertical_aperture /
                                 std::max(1.0e-6f, focal_length));
  frame->xmag = horizontal_aperture;
  frame->ymag = vertical_aperture;
  frame->znear = std::max(1.0e-5f, clipping_range[0]);
  frame->zfar = std::max(frame->znear, clipping_range[1]);
  frame->ortho = projection == tinyusdz::GeomCamera::Projection::Orthographic;
  return true;
}

bool FindStageCameraFrameRecursive(const tinyusdz::Stage &stage,
                                   const tinyusdz::Prim &prim,
                                   const std::string &query,
                                   const matrix4d &parent_world, double time,
                                   CameraFrame *frame) {
  bool reset = false;
  const matrix4d local =
      LocalMatrixOrIdentity(AsPreviewXformable(prim), time, &reset);
  const matrix4d world = reset ? local : (local * parent_world);
  if (const tinyusdz::GeomCamera *cam = prim.as<tinyusdz::GeomCamera>()) {
    if (MatchPrimNameOrPath(prim, query)) {
      return CameraFrameFromGeomCamera(stage, *cam, world, time, frame);
    }
  }
  for (const tinyusdz::Prim &child : prim.children()) {
    if (FindStageCameraFrameRecursive(stage, child, query, world, time, frame)) {
      return true;
    }
  }
  return false;
}

bool FindStageCameraFrame(const tinyusdz::Stage &stage, const std::string &query,
                          double time, CameraFrame *frame) {
  for (const tinyusdz::Prim &root : stage.root_prims()) {
    if (FindStageCameraFrameRecursive(stage, root, query, matrix4d::identity(),
                                      time, frame)) {
      return true;
    }
  }
  return false;
}

template <typename T>
bool EvalFallback(const tinyusdz::Stage &stage,
                  const tinyusdz::TypedAttributeWithFallback<T> &attr,
                  const std::string &name, T *out) {
  std::string err;
  return tinyusdz::tydra::EvaluateTypedAttribute(stage, attr, name, out, &err);
}

bool EvalAxis(const tinyusdz::TypedAttributeWithFallback<tinyusdz::Axis> &attr,
              tinyusdz::Axis *out) {
  if (!out) return false;
  *out = attr.get_value();
  return true;
}

std::string PrimPathString(const tinyusdz::Prim &prim) {
  return prim.absolute_path().full_path_name();
}

float ApproxScale(const matrix4d &m) {
  Vec3 sx = TransformVector(m, Vec3{1.0f, 0.0f, 0.0f});
  Vec3 sy = TransformVector(m, Vec3{0.0f, 1.0f, 0.0f});
  Vec3 sz = TransformVector(m, Vec3{0.0f, 0.0f, 1.0f});
  return std::max(1.0e-6f, (Length(sx) + Length(sy) + Length(sz)) / 3.0f);
}

void AddNurbsTriangle(const Vec3 &p0, const Vec3 &p1, const Vec3 &p2,
                      std::vector<float> *vertices, std::vector<TriInfo> *tris,
                      Bounds *bounds) {
  Vec3 n = Normalize(Cross(Sub(p1, p0), Sub(p2, p0)));
  TriInfo tri;
  tri.p0 = p0;
  tri.p1 = p1;
  tri.p2 = p2;
  tri.n = n;
  tri.base_color = Vec3{0.42f, 0.42f, 0.48f};
  vertices->insert(vertices->end(), {p0.x, p0.y, p0.z, p1.x, p1.y, p1.z,
                                     p2.x, p2.y, p2.z});
  tris->push_back(tri);
  Expand(bounds, p0);
  Expand(bounds, p1);
  Expand(bounds, p2);
}

void AddDirectTriangle(const Vec3 &p0, const Vec3 &p1, const Vec3 &p2,
                       const Vec3 &color, std::vector<float> *vertices,
                       std::vector<TriInfo> *tris, Bounds *bounds) {
  Vec3 n = Normalize(Cross(Sub(p1, p0), Sub(p2, p0)));
  TriInfo tri;
  tri.p0 = p0;
  tri.p1 = p1;
  tri.p2 = p2;
  tri.n = n;
  tri.base_color = color;
  vertices->insert(vertices->end(), {p0.x, p0.y, p0.z, p1.x, p1.y, p1.z,
                                     p2.x, p2.y, p2.z});
  tris->push_back(tri);
  Expand(bounds, p0);
  Expand(bounds, p1);
  Expand(bounds, p2);
}

void AddDirectCube(double size, const matrix4d &world, std::vector<float> *vertices,
                   std::vector<TriInfo> *tris, Bounds *bounds) {
  float h = float(size * 0.5);
  Vec3 p[8] = {
      TransformPoint(world, Vec3{-h, -h, -h}),
      TransformPoint(world, Vec3{ h, -h, -h}),
      TransformPoint(world, Vec3{ h,  h, -h}),
      TransformPoint(world, Vec3{-h,  h, -h}),
      TransformPoint(world, Vec3{-h, -h,  h}),
      TransformPoint(world, Vec3{ h, -h,  h}),
      TransformPoint(world, Vec3{ h,  h,  h}),
      TransformPoint(world, Vec3{-h,  h,  h}),
  };
  const int f[12][3] = {
      {0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7},
      {0, 1, 5}, {0, 5, 4}, {1, 2, 6}, {1, 6, 5},
      {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7},
  };
  for (const auto &tri : f) {
    AddDirectTriangle(p[tri[0]], p[tri[1]], p[tri[2]],
                      Vec3{0.46f, 0.50f, 0.56f}, vertices, tris, bounds);
  }
}

void AddDirectPlane(double width, double length, tinyusdz::Axis axis,
                    const matrix4d &world, std::vector<float> *vertices,
                    std::vector<TriInfo> *tris, Bounds *bounds) {
  float hw = float(width * 0.5);
  float hl = float(length * 0.5);
  Vec3 local[4];
  if (axis == tinyusdz::Axis::X) {
    local[0] = Vec3{0.0f, -hw, -hl};
    local[1] = Vec3{0.0f,  hw, -hl};
    local[2] = Vec3{0.0f,  hw,  hl};
    local[3] = Vec3{0.0f, -hw,  hl};
  } else if (axis == tinyusdz::Axis::Y) {
    local[0] = Vec3{-hw, 0.0f, -hl};
    local[1] = Vec3{ hw, 0.0f, -hl};
    local[2] = Vec3{ hw, 0.0f,  hl};
    local[3] = Vec3{-hw, 0.0f,  hl};
  } else {
    local[0] = Vec3{-hw, -hl, 0.0f};
    local[1] = Vec3{ hw, -hl, 0.0f};
    local[2] = Vec3{ hw,  hl, 0.0f};
    local[3] = Vec3{-hw,  hl, 0.0f};
  }
  Vec3 p0 = TransformPoint(world, local[0]);
  Vec3 p1 = TransformPoint(world, local[1]);
  Vec3 p2 = TransformPoint(world, local[2]);
  Vec3 p3 = TransformPoint(world, local[3]);
  AddDirectTriangle(p0, p1, p2, Vec3{0.38f, 0.55f, 0.44f}, vertices, tris, bounds);
  AddDirectTriangle(p0, p2, p3, Vec3{0.38f, 0.55f, 0.44f}, vertices, tris, bounds);
}

double BSplineBasis(int i, int degree, double u, const std::vector<double> &knots) {
  if (degree == 0) {
    const bool last = (i + 1 == int(knots.size()) - 1) && (u == knots.back());
    return ((knots[size_t(i)] <= u && u < knots[size_t(i + 1)]) || last) ? 1.0 : 0.0;
  }
  double left = 0.0;
  double denom_l = knots[size_t(i + degree)] - knots[size_t(i)];
  if (std::abs(denom_l) > 1.0e-14) {
    left = (u - knots[size_t(i)]) / denom_l *
           BSplineBasis(i, degree - 1, u, knots);
  }
  double right = 0.0;
  double denom_r = knots[size_t(i + degree + 1)] - knots[size_t(i + 1)];
  if (std::abs(denom_r) > 1.0e-14) {
    right = (knots[size_t(i + degree + 1)] - u) / denom_r *
            BSplineBasis(i + 1, degree - 1, u, knots);
  }
  return left + right;
}

Vec3 EvalNurbsPatchPoint(const std::vector<tinyusdz::value::point3f> &points,
                         const std::vector<double> &weights, int u_count,
                         int v_count, int u_order, int v_order,
                         const std::vector<double> &u_knots,
                         const std::vector<double> &v_knots, double u, double v) {
  Vec3 sum{0.0f, 0.0f, 0.0f};
  double wsum = 0.0;
  int u_degree = std::max(0, u_order - 1);
  int v_degree = std::max(0, v_order - 1);
  for (int j = 0; j < v_count; j++) {
    double bv = BSplineBasis(j, v_degree, v, v_knots);
    if (bv == 0.0) continue;
    for (int i = 0; i < u_count; i++) {
      double bu = BSplineBasis(i, u_degree, u, u_knots);
      if (bu == 0.0) continue;
      size_t idx = size_t(j) * size_t(u_count) + size_t(i);
      double w = idx < weights.size() ? weights[idx] : 1.0;
      double b = bu * bv * w;
      Vec3 p = FromPoint3(points[idx]);
      sum = Add(sum, Mul(p, float(b)));
      wsum += b;
    }
  }
  if (std::abs(wsum) > 1.0e-20) {
    sum = Mul(sum, float(1.0 / wsum));
  }
  return sum;
}

void AddNurbsPatchTriangles(const tinyusdz::Stage &stage,
                            const tinyusdz::GeomNurbsPatch &patch,
                            const matrix4d &world, double time,
                            std::vector<float> *vertices,
                            std::vector<TriInfo> *tris, Bounds *bounds) {
  std::vector<tinyusdz::value::point3f> points;
  int u_count = 0, v_count = 0, u_order = 0, v_order = 0;
  std::vector<double> u_knots, v_knots, weights;
  if (!EvalAnim(stage, patch.points, "points", time, &points) ||
      !EvalAnim(stage, patch.uVertexCount, "uVertexCount", time, &u_count) ||
      !EvalAnim(stage, patch.vVertexCount, "vVertexCount", time, &v_count) ||
      !EvalAnim(stage, patch.uOrder, "uOrder", time, &u_order) ||
      !EvalAnim(stage, patch.vOrder, "vOrder", time, &v_order) ||
      !EvalAnim(stage, patch.uKnots, "uKnots", time, &u_knots) ||
      !EvalAnim(stage, patch.vKnots, "vKnots", time, &v_knots)) {
    return;
  }
  EvalAnim(stage, patch.pointWeights, "pointWeights", time, &weights);
  if (u_count <= 0 || v_count <= 0 ||
      points.size() < size_t(u_count) * size_t(v_count)) {
    return;
  }
  double u0 = u_knots[size_t(std::max(0, u_order - 1))];
  double u1 = u_knots[u_knots.size() - size_t(std::max(1, u_order))];
  double v0 = v_knots[size_t(std::max(0, v_order - 1))];
  double v1 = v_knots[v_knots.size() - size_t(std::max(1, v_order))];
  tinyusdz::value::double2 range;
  if (EvalAnim(stage, patch.uRange, "uRange", time, &range)) {
    u0 = range[0];
    u1 = range[1];
  }
  if (EvalAnim(stage, patch.vRange, "vRange", time, &range)) {
    v0 = range[0];
    v1 = range[1];
  }
  constexpr int divs = 24;
  std::vector<Vec3> grid(size_t(divs + 1) * size_t(divs + 1));
  for (int y = 0; y <= divs; y++) {
    double v = v0 + (v1 - v0) * double(y) / double(divs);
    for (int x = 0; x <= divs; x++) {
      double u = u0 + (u1 - u0) * double(x) / double(divs);
      grid[size_t(y) * size_t(divs + 1) + size_t(x)] =
          TransformPoint(world, EvalNurbsPatchPoint(points, weights, u_count,
                                                    v_count, u_order, v_order,
                                                    u_knots, v_knots, u, v));
    }
  }
  for (int y = 0; y < divs; y++) {
    for (int x = 0; x < divs; x++) {
      Vec3 p00 = grid[size_t(y) * size_t(divs + 1) + size_t(x)];
      Vec3 p10 = grid[size_t(y) * size_t(divs + 1) + size_t(x + 1)];
      Vec3 p01 = grid[size_t(y + 1) * size_t(divs + 1) + size_t(x)];
      Vec3 p11 = grid[size_t(y + 1) * size_t(divs + 1) + size_t(x + 1)];
      AddNurbsTriangle(p00, p10, p11, vertices, tris, bounds);
      AddNurbsTriangle(p00, p11, p01, vertices, tris, bounds);
    }
  }
}

void AppendLinearCurveStrands(const std::vector<tinyusdz::value::point3f> &points,
                              const std::vector<int> &counts,
                              const std::vector<float> &widths,
                              const matrix4d &world,
                              std::vector<float> *curve_points,
                              std::vector<float> *curve_radii,
                              std::vector<uint32_t> *first,
                              std::vector<uint32_t> *count,
                              std::vector<TriInfo> *info,
                              Bounds *bounds) {
  size_t cursor = 0;
  for (int c : counts) {
    if (c < 2 || cursor + size_t(c) > points.size()) {
      cursor += size_t(std::max(0, c));
      continue;
    }
    first->push_back(uint32_t(curve_points->size() / 3));
    count->push_back(uint32_t(c));
    for (int i = 0; i < c; i++) {
      size_t idx = cursor + size_t(i);
      Vec3 p = TransformPoint(world, FromPoint3(points[idx]));
      float radius = 0.5f * ((idx < widths.size()) ? widths[idx] : 0.01f);
      curve_points->insert(curve_points->end(), {p.x, p.y, p.z});
      curve_radii->push_back(std::max(1.0e-5f, radius * ApproxScale(world)));
      Expand(bounds, p);
    }
    for (int i = 0; i + 1 < c; i++) {
      TriInfo ti;
      size_t point_base = size_t(first->back()) + size_t(i);
      Vec3 p0{(*curve_points)[point_base * 3 + 0],
              (*curve_points)[point_base * 3 + 1],
              (*curve_points)[point_base * 3 + 2]};
      Vec3 p1{(*curve_points)[(point_base + 1) * 3 + 0],
              (*curve_points)[(point_base + 1) * 3 + 1],
              (*curve_points)[(point_base + 1) * 3 + 2]};
      ti.p0 = p0;
      ti.p1 = p1;
      ti.p2 = Add(p0, Vec3{0.0f, 1.0f, 0.0f});
      ti.base_color = Vec3{0.62f, 0.50f, 0.34f};
      info->push_back(ti);
    }
    cursor += size_t(c);
  }
}

void TraverseDirectPrims(const tinyusdz::Stage &stage, const tinyusdz::Prim &prim,
                         const std::unordered_map<std::string, matrix4d> &matrices,
                         double time, DirectScene *direct,
                         std::vector<float> *vertices, std::vector<TriInfo> *tris,
                         Bounds *bounds, std::vector<float> *sphere_data,
                         std::vector<float> *round_points,
                         std::vector<float> *round_radii,
                         std::vector<uint32_t> *round_first,
                         std::vector<uint32_t> *round_count,
                         std::vector<float> *flat_points,
                         std::vector<float> *flat_radii,
                         std::vector<uint32_t> *flat_first,
                         std::vector<uint32_t> *flat_count,
                         std::vector<float> *point_centers,
                         std::vector<float> *point_radii,
                         std::vector<float> *bez_cps,
                         std::vector<float> *tet_aabbs) {
  const std::string path = PrimPathString(prim);
  const matrix4d world = MatrixForPath(matrices, path);
  matrix4d inv_world;
  bool has_inv = tinyusdz::inverse(world, inv_world, 1.0e-12);

  if (const tinyusdz::GeomSphere *sphere = prim.as<tinyusdz::GeomSphere>()) {
    double radius = 2.0;
    EvalAnimFallback(stage, sphere->radius, "radius", time, &radius);
    Vec3 c = TransformPoint(world, Vec3{0.0f, 0.0f, 0.0f});
    float r = float(radius) * ApproxScale(world);
    sphere_data->insert(sphere_data->end(), {c.x, c.y, c.z, r});
    TriInfo ti;
    ti.p0 = c;
    ti.base_color = Vec3{0.35f, 0.48f, 0.80f};
    direct->sphere_info.push_back(ti);
    direct->direct_paths.insert(path);
    Expand(bounds, Add(c, Vec3{r, r, r}));
    Expand(bounds, Sub(c, Vec3{r, r, r}));
  } else if (has_inv) {
    DirectShape shape;
    bool add_shape = false;
    if (const tinyusdz::GeomCylinder *cyl = prim.as<tinyusdz::GeomCylinder>()) {
      shape.type = DirectShape::Type::Cylinder;
      EvalAnimFallback(stage, cyl->radius, "radius", time, &shape.radius);
      EvalAnimFallback(stage, cyl->height, "height", time, &shape.height);
      EvalAxis(cyl->axis, &shape.axis);
      add_shape = true;
    } else if (const tinyusdz::GeomCone *cone = prim.as<tinyusdz::GeomCone>()) {
      shape.type = DirectShape::Type::Cone;
      EvalAnimFallback(stage, cone->radius, "radius", time, &shape.radius);
      EvalAnimFallback(stage, cone->height, "height", time, &shape.height);
      EvalAxis(cone->axis, &shape.axis);
      add_shape = true;
    } else if (const tinyusdz::GeomCapsule *cap = prim.as<tinyusdz::GeomCapsule>()) {
      shape.type = DirectShape::Type::Capsule;
      EvalAnimFallback(stage, cap->radius, "radius", time, &shape.radius);
      EvalAnimFallback(stage, cap->height, "height", time, &shape.height);
      EvalAxis(cap->axis, &shape.axis);
      add_shape = true;
    }
    if (add_shape) {
      shape.world = world;
      shape.inv_world = inv_world;
      direct->shapes.push_back(shape);
      direct->direct_paths.insert(path);
      float e = float(std::max(shape.height * 0.5 + shape.radius, shape.radius)) *
                ApproxScale(world);
      Vec3 c = TransformPoint(world, Vec3{0.0f, 0.0f, 0.0f});
      Expand(bounds, Add(c, Vec3{e, e, e}));
      Expand(bounds, Sub(c, Vec3{e, e, e}));
    }
  }

  if (const tinyusdz::GeomCube *cube = prim.as<tinyusdz::GeomCube>()) {
    double size = 2.0;
    EvalAnimFallback(stage, cube->size, "size", time, &size);
    AddDirectCube(size, world, vertices, tris, bounds);
    direct->direct_paths.insert(path);
  } else if (const tinyusdz::GeomPlane *plane = prim.as<tinyusdz::GeomPlane>()) {
    double width = 2.0;
    double length = 2.0;
    tinyusdz::Axis axis = tinyusdz::Axis::Z;
    EvalAnimFallback(stage, plane->width, "width", time, &width);
    EvalAnimFallback(stage, plane->length, "length", time, &length);
    EvalAxis(plane->axis, &axis);
    AddDirectPlane(width, length, axis, world, vertices, tris, bounds);
    direct->direct_paths.insert(path);
  } else if (const tinyusdz::GeomPoints *pts = prim.as<tinyusdz::GeomPoints>()) {
    std::vector<tinyusdz::value::point3f> points;
    std::vector<float> widths;
    if (EvalAnim(stage, pts->points, "points", time, &points)) {
      EvalAnim(stage, pts->widths, "widths", time, &widths);
      for (size_t i = 0; i < points.size(); i++) {
        Vec3 p = TransformPoint(world, FromPoint3(points[i]));
        float radius = 0.5f * ((i < widths.size()) ? widths[i] : 0.05f) *
                       ApproxScale(world);
        radius = std::max(1.0e-5f, radius);
        point_centers->insert(point_centers->end(), {p.x, p.y, p.z});
        point_radii->push_back(radius);
        TriInfo ti;
        ti.p0 = p;
        ti.base_color = Vec3{0.90f, 0.72f, 0.26f};
        direct->point_info.push_back(ti);
        Expand(bounds, Add(p, Vec3{radius, radius, radius}));
        Expand(bounds, Sub(p, Vec3{radius, radius, radius}));
      }
      direct->direct_paths.insert(path);
    }
  } else if (const tinyusdz::GeomTetMesh *tet = prim.as<tinyusdz::GeomTetMesh>()) {
    std::vector<tinyusdz::value::point3f> points;
    std::vector<tinyusdz::value::int4> indices;
    if (EvalAnim(stage, tet->points, "points", time, &points) &&
        EvalAnim(stage, tet->tetVertexIndices, "tetVertexIndices", time, &indices)) {
      for (const auto &idx : indices) {
        if (idx[0] < 0 || idx[1] < 0 || idx[2] < 0 || idx[3] < 0 ||
            size_t(idx[0]) >= points.size() || size_t(idx[1]) >= points.size() ||
            size_t(idx[2]) >= points.size() || size_t(idx[3]) >= points.size()) {
          continue;
        }
        TetPrim tp;
        tp.p[0] = TransformPoint(world, FromPoint3(points[size_t(idx[0])]));
        tp.p[1] = TransformPoint(world, FromPoint3(points[size_t(idx[1])]));
        tp.p[2] = TransformPoint(world, FromPoint3(points[size_t(idx[2])]));
        tp.p[3] = TransformPoint(world, FromPoint3(points[size_t(idx[3])]));
        Vec3 lo{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()};
        Vec3 hi{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                -std::numeric_limits<float>::max()};
        for (const Vec3 &p : tp.p) {
          lo.x = std::min(lo.x, p.x);
          lo.y = std::min(lo.y, p.y);
          lo.z = std::min(lo.z, p.z);
          hi.x = std::max(hi.x, p.x);
          hi.y = std::max(hi.y, p.y);
          hi.z = std::max(hi.z, p.z);
          Expand(bounds, p);
        }
        tet_aabbs->insert(tet_aabbs->end(), {lo.x, lo.y, lo.z, hi.x, hi.y, hi.z});
        direct->tet_prims.push_back(tp);
      }
      direct->direct_paths.insert(path);
    }
  }

  if (const tinyusdz::GeomBasisCurves *curves = prim.as<tinyusdz::GeomBasisCurves>()) {
    std::vector<tinyusdz::value::point3f> points;
    std::vector<int> counts;
    std::vector<float> widths;
    if (EvalAnim(stage, curves->points, "points", time, &points) &&
        EvalAnim(stage, curves->curveVertexCounts, "curveVertexCounts", time, &counts)) {
      EvalAnim(stage, curves->widths, "widths", time, &widths);
      if (curves->normals.authored()) {
        AppendLinearCurveStrands(points, counts, widths, world, flat_points,
                                 flat_radii, flat_first, flat_count,
                                 &direct->flat_curve_info, bounds);
      } else {
        AppendLinearCurveStrands(points, counts, widths, world, round_points,
                                 round_radii, round_first, round_count,
                                 &direct->round_curve_info, bounds);
      }
      direct->direct_paths.insert(path);
    }
  } else if (const tinyusdz::GeomNurbsCurves *curves = prim.as<tinyusdz::GeomNurbsCurves>()) {
    std::vector<tinyusdz::value::point3f> points;
    std::vector<int> counts;
    std::vector<float> widths;
    if (EvalAnim(stage, curves->points, "points", time, &points) &&
        EvalAnim(stage, curves->curveVertexCounts, "curveVertexCounts", time, &counts)) {
      EvalAnim(stage, curves->widths, "widths", time, &widths);
      AppendLinearCurveStrands(points, counts, widths, world, round_points,
                               round_radii, round_first, round_count,
                               &direct->round_curve_info, bounds);
      direct->direct_paths.insert(path);
    }
  } else if (const tinyusdz::GeomNurbsPatch *patch = prim.as<tinyusdz::GeomNurbsPatch>()) {
    AddNurbsPatchTriangles(stage, *patch, world, time, vertices, tris, bounds);
    direct->direct_paths.insert(path);
  } else if (const tinyusdz::GeomHermiteCurves *curves = prim.as<tinyusdz::GeomHermiteCurves>()) {
    std::vector<tinyusdz::value::point3f> points;
    std::vector<tinyusdz::value::vector3f> tangents;
    std::vector<int> counts;
    std::vector<float> widths;
    if (EvalAnim(stage, curves->points, "points", time, &points) &&
        EvalAnim(stage, curves->curveVertexCounts, "curveVertexCounts", time, &counts) &&
        EvalAnim(stage, curves->tangents, "tangents", time, &tangents)) {
      EvalAnim(stage, curves->widths, "widths", time, &widths);
      size_t cursor = 0;
      for (int c : counts) {
        if (c < 2 || cursor + size_t(c) > points.size() ||
            cursor + size_t(c) > tangents.size()) {
          cursor += size_t(std::max(0, c));
          continue;
        }
        for (int i = 0; i + 1 < c; i++) {
          size_t i0 = cursor + size_t(i);
          size_t i1 = i0 + 1;
          Vec3 p0 = TransformPoint(world, FromPoint3(points[i0]));
          Vec3 p1 = TransformPoint(world, FromPoint3(points[i1]));
          Vec3 t0 = TransformVector(world, FromVector3(tangents[i0]));
          Vec3 t1 = TransformVector(world, FromVector3(tangents[i1]));
          float r0 = 0.5f * ((i0 < widths.size()) ? widths[i0] : 0.01f) *
                     ApproxScale(world);
          float r1 = 0.5f * ((i1 < widths.size()) ? widths[i1] : 0.01f) *
                     ApproxScale(world);
          r0 = std::max(1.0e-5f, r0);
          r1 = std::max(1.0e-5f, r1);
          Vec3 b0 = p0;
          Vec3 b1 = Add(p0, Mul(t0, 1.0f / 3.0f));
          Vec3 b2 = Sub(p1, Mul(t1, 1.0f / 3.0f));
          Vec3 b3 = p1;
          bez_cps->insert(bez_cps->end(),
                          {b0.x, b0.y, b0.z, r0, b1.x, b1.y, b1.z, r0,
                           b2.x, b2.y, b2.z, r1, b3.x, b3.y, b3.z, r1});
          TriInfo ti;
          ti.p0 = p0;
          ti.p1 = p1;
          ti.p2 = Add(p0, Vec3{0.0f, 1.0f, 0.0f});
          ti.base_color = Vec3{0.72f, 0.45f, 0.28f};
          direct->bez_curve_info.push_back(ti);
          Expand(bounds, p0);
          Expand(bounds, p1);
        }
        cursor += size_t(c);
      }
      direct->direct_paths.insert(path);
    }
  }

  for (const tinyusdz::Prim &child : prim.children()) {
    TraverseDirectPrims(stage, child, matrices, time, direct, vertices, tris,
                        bounds, sphere_data, round_points, round_radii,
                        round_first, round_count, flat_points, flat_radii,
                        flat_first, flat_count, point_centers, point_radii,
                        bez_cps, tet_aabbs);
  }
}

bool BuildDirectScene(const tinyusdz::Stage &stage, const RenderScene &render_scene,
                      const Options &opt, std::vector<float> *vertices,
                      std::vector<TriInfo> *tris, Bounds *bounds,
                      DirectScene *direct, std::string *err) {
  if (!direct || !vertices || !tris || !bounds) return false;
  std::vector<float> sphere_data;
  std::vector<float> round_points, round_radii, flat_points, flat_radii;
  std::vector<float> point_centers, point_radii;
  std::vector<float> bez_cps;
  std::vector<float> tet_aabbs;
  std::vector<uint32_t> round_first, round_count, flat_first, flat_count;
  std::unordered_map<std::string, matrix4d> matrices = BuildNodeMatrixMap(render_scene);
  for (const tinyusdz::Prim &root : stage.root_prims()) {
    TraverseDirectPrims(stage, root, matrices, opt.timecode, direct, vertices,
                        tris, bounds, &sphere_data, &round_points, &round_radii,
                        &round_first, &round_count, &flat_points, &flat_radii,
                        &flat_first, &flat_count, &point_centers, &point_radii,
                        &bez_cps, &tet_aabbs);
  }

  lrt_tri_build_options build_opts;
  std::memset(&build_opts, 0, sizeof(build_opts));
  build_opts.quality = opt.quality;
  build_opts.layout = LRT_TRI_LAYOUT_AUTO;
  build_opts.num_threads = WorkerThreadCount(opt.threads);
  lrt_result lrt_err = LRT_RESULT_OK;
  if (!sphere_data.empty()) {
    direct->spheres.reset(
        lrt_sphere_scene_build(sphere_data.data(), sphere_data.size() / 4,
                               &build_opts, &lrt_err));
    if (!direct->spheres) {
      if (err) *err = "Failed to build LightRT sphere scene.";
      return false;
    }
  }
  if (!round_first.empty()) {
    lrt_hair_strands strands;
    std::memset(&strands, 0, sizeof(strands));
    strands.points = round_points.data();
    strands.radius = round_radii.data();
    strands.strand_first = round_first.data();
    strands.strand_count = round_count.data();
    strands.nstrands = round_first.size();
    strands.npoints = round_radii.size();
    direct->round_curves.reset(
        lrt_roundcurve_scene_build(&strands, &build_opts, &lrt_err));
    if (!direct->round_curves) {
      if (err) *err = "Failed to build LightRT round curve scene.";
      return false;
    }
  }
  if (!flat_first.empty()) {
    lrt_hair_strands strands;
    std::memset(&strands, 0, sizeof(strands));
    strands.points = flat_points.data();
    strands.radius = flat_radii.data();
    strands.strand_first = flat_first.data();
    strands.strand_count = flat_count.data();
    strands.nstrands = flat_first.size();
    strands.npoints = flat_radii.size();
    direct->flat_curves.reset(
        lrt_flatcurve_scene_build(&strands, &build_opts, &lrt_err));
    if (!direct->flat_curves) {
      if (err) *err = "Failed to build LightRT flat curve scene.";
      return false;
    }
  }
  if (!point_centers.empty()) {
    direct->points.reset(
        lrt_points_scene_build(point_centers.data(), point_radii.data(), nullptr,
                               LRT_POINT_SPHERE, point_radii.size(),
                               &build_opts, &lrt_err));
    if (!direct->points) {
      if (err) *err = "Failed to build LightRT points scene.";
      return false;
    }
  }
  if (!bez_cps.empty()) {
    direct->bez_curves.reset(
        lrt_bezcurve_scene_build(bez_cps.data(), bez_cps.size() / 16,
                                 &build_opts, &lrt_err));
    if (!direct->bez_curves) {
      if (err) *err = "Failed to build LightRT Hermite/Bezier curve scene.";
      return false;
    }
  }
  if (!tet_aabbs.empty()) {
    direct->tets.reset(lrt_user_scene_build(
        tet_aabbs.data(), direct->tet_prims.size(), TetUserIntersect,
        TetUserOccluded, &direct->tet_prims, &build_opts, &lrt_err));
    if (!direct->tets) {
      if (err) *err = "Failed to build LightRT TetMesh user scene.";
      return false;
    }
  }
  return true;
}

bool FindCameraNode(const RenderScene &scene, const Node &node,
                    const std::string &query, const Node **node_out) {
  if (node.nodeType == NodeType::Camera && node.id >= 0 &&
      size_t(node.id) < scene.cameras.size()) {
    const RenderCamera &cam = scene.cameras[size_t(node.id)];
    if (query.empty() || node.abs_path == query || cam.abs_path == query ||
        cam.name == query || node.prim_name == query) {
      *node_out = &node;
      return true;
    }
  }
  for (const Node &child : node.children) {
    if (FindCameraNode(scene, child, query, node_out)) return true;
  }
  return false;
}

const Node *FindCameraNode(const RenderScene &scene, const std::string &query) {
  const Node *result = nullptr;
  for (const Node &root : scene.nodes) {
    if (FindCameraNode(scene, root, query, &result)) return result;
  }
  return nullptr;
}

CameraFrame MakeCameraFrame(const RenderScene &scene, const Options &opt,
                            const Bounds &bounds, int height,
                            tinyusdz::Axis up_axis) {
  CameraFrame frame;
  const Node *cam_node = FindCameraNode(scene, opt.camera);
  if (!cam_node && !opt.camera.empty()) {
    std::cerr << "WARN: Camera not found: " << opt.camera
              << ". Using auto-fit camera.\n";
  }
  if (cam_node) {
    const RenderCamera &cam = scene.cameras[size_t(cam_node->id)];
    const matrix4d &m = cam_node->global_matrix;
    frame.origin = Vec3{float(m.m[3][0]), float(m.m[3][1]), float(m.m[3][2])};
    frame.right = Normalize(TransformVector(m, Vec3{1.0f, 0.0f, 0.0f}));
    frame.up = Normalize(TransformVector(m, Vec3{0.0f, 1.0f, 0.0f}));
    frame.forward = Normalize(TransformVector(m, Vec3{0.0f, 0.0f, -1.0f}));
    frame.yfov = 2.0f * std::atan(0.5f * cam.verticalAperture /
                                  std::max(1.0e-6f, cam.focalLength));
    frame.xmag = cam.xmag;
    frame.ymag = cam.ymag;
    frame.znear = std::max(1.0e-5f, cam.znear);
    frame.zfar = cam.zfar;
    frame.ortho = cam.projection == tinyusdz::GeomCamera::Projection::Orthographic;
    return frame;
  }

  Vec3 center{0.0f, 0.0f, 0.0f};
  float radius = 1.0f;
  if (bounds.valid) {
    center = Mul(Add(bounds.lo, bounds.hi), 0.5f);
    radius = std::max(0.001f, Length(Sub(bounds.hi, bounds.lo)) * 0.5f);
  }
  float aspect = (height > 0) ? float(opt.width) / float(height) : 16.0f / 9.0f;
  frame.yfov = 45.0f * 3.14159265358979323846f / 180.0f;
  float distance = radius / std::tan(frame.yfov * 0.5f);
  if (aspect < 1.0f) {
    distance /= aspect;
  }
  Vec3 up_axis_vec{0.0f, 1.0f, 0.0f};
  Vec3 view_dir{0.0f, 0.15f, 1.8f};
  if (up_axis == tinyusdz::Axis::Z) {
    up_axis_vec = Vec3{0.0f, 0.0f, 1.0f};
    view_dir = Normalize(Vec3{-0.95f, -1.15f, 0.62f});
  } else if (up_axis == tinyusdz::Axis::X) {
    up_axis_vec = Vec3{1.0f, 0.0f, 0.0f};
    view_dir = Normalize(Vec3{0.62f, -0.95f, -1.15f});
  }
  if (opt.has_view_dir) {
    view_dir = Normalize(opt.view_dir);
  }
  frame.origin = Add(center, Mul(view_dir, distance * opt.fit_scale));
  frame.forward = Normalize(Sub(center, frame.origin));
  frame.right = Normalize(Cross(frame.forward, up_axis_vec));
  if (Length(frame.right) < 1.0e-6f) {
    frame.right = Vec3{1.0f, 0.0f, 0.0f};
  }
  frame.up = Normalize(Cross(frame.right, frame.forward));
  frame.znear = std::max(1.0e-4f, distance * 0.001f);
  frame.zfar = std::max(1000.0f, distance * 10.0f);
  return frame;
}

void AppendPowerCdf(std::vector<PreviewLight> *lights, std::vector<float> *cdf) {
  if (!lights || !cdf) return;
  cdf->clear();
  float sum = 0.0f;
  for (PreviewLight &light : *lights) {
    sum += std::max(0.0f, light.power);
    light.cdf = sum;
    cdf->push_back(sum);
  }
  if (sum > 0.0f) {
    for (float &v : *cdf) {
      v /= sum;
    }
    for (PreviewLight &light : *lights) {
      light.cdf /= sum;
    }
  }
}

Vec3 DirectionFromLatlong(float u, float v) {
  constexpr float kPi = 3.14159265358979323846f;
  float phi = (u - 0.5f) * 2.0f * kPi;
  float theta = v * kPi;
  float st = std::sin(theta);
  return Normalize(Vec3{st * std::sin(phi), std::cos(theta), st * std::cos(phi)});
}

void LatlongUV(const Vec3 &dir, float *u, float *v) {
  constexpr float kPi = 3.14159265358979323846f;
  Vec3 d = Normalize(dir);
  float phi = std::atan2(d.x, d.z);
  float theta = std::acos(ClampFloat(d.y, -1.0f, 1.0f));
  if (u) *u = phi / (2.0f * kPi) + 0.5f;
  if (v) *v = theta / kPi;
}

Vec3 SampleEnvNearest(const EnvImage &img, float u, float v) {
  if (img.width <= 0 || img.height <= 0 || img.pixels.empty()) {
    return Vec3{0.0f, 0.0f, 0.0f};
  }
  u = u - std::floor(u);
  v = ClampFloat(v, 0.0f, 1.0f);
  int x = int(std::floor(u * float(img.width))) % img.width;
  int y = std::min(img.height - 1, int(std::floor(v * float(img.height))));
  return img.pixels[size_t(y) * size_t(img.width) + size_t(x)];
}

Vec3 SampleEnv(const EnvImage &img, const Vec3 &dir) {
  float u = 0.0f;
  float v = 0.0f;
  LatlongUV(dir, &u, &v);
  return SampleEnvNearest(img, u, v);
}

bool DecodeTextureToEnvImage(const RenderScene &scene, int texture_id,
                             EnvImage *out) {
  if (!out || texture_id < 0 || size_t(texture_id) >= scene.images.size()) {
    return false;
  }
  const tinyusdz::tydra::TextureImage &tex = scene.images[size_t(texture_id)];
  if (!tex.decoded || tex.width <= 0 || tex.height <= 0 || tex.channels <= 0 ||
      tex.buffer_id < 0 || size_t(tex.buffer_id) >= scene.buffers.size()) {
    return false;
  }
  const tinyusdz::tydra::BufferData &buf = scene.buffers[size_t(tex.buffer_id)];
  const size_t pixel_count = size_t(tex.width) * size_t(tex.height);
  const size_t channels = size_t(tex.channels);
  EnvImage img;
  img.width = tex.width;
  img.height = tex.height;
  img.pixels.resize(pixel_count);
  if (buf.componentType == tinyusdz::tydra::ComponentType::UInt8) {
    if (buf.data.size() < pixel_count * channels) return false;
    for (size_t i = 0; i < pixel_count; i++) {
      const uint8_t *p = buf.data.data() + i * channels;
      img.pixels[i] = Vec3{float(p[0]) / 255.0f,
                           float(p[std::min<size_t>(1, channels - 1)]) / 255.0f,
                           float(p[std::min<size_t>(2, channels - 1)]) / 255.0f};
    }
  } else if (buf.componentType == tinyusdz::tydra::ComponentType::Float) {
    if (buf.data.size() < pixel_count * channels * sizeof(float)) return false;
    const float *src = reinterpret_cast<const float *>(buf.data.data());
    for (size_t i = 0; i < pixel_count; i++) {
      const float *p = src + i * channels;
      img.pixels[i] = Vec3{p[0], p[std::min<size_t>(1, channels - 1)],
                           p[std::min<size_t>(2, channels - 1)]};
    }
  } else {
    return false;
  }
  *out = std::move(img);
  return true;
}

EnvImage ConvolveDiffuseEnv(const EnvImage &env, int width, int height) {
  constexpr float kPi = 3.14159265358979323846f;
  EnvImage out;
  out.width = width;
  out.height = height;
  out.pixels.resize(size_t(width) * size_t(height));
  const int theta_steps = 8;
  const int phi_steps = 16;
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      Vec3 n = DirectionFromLatlong((float(x) + 0.5f) / float(width),
                                    (float(y) + 0.5f) / float(height));
      Vec3 up = (std::abs(n.y) < 0.999f) ? Vec3{0.0f, 1.0f, 0.0f}
                                         : Vec3{1.0f, 0.0f, 0.0f};
      Vec3 tangent = Normalize(Cross(up, n));
      Vec3 bitangent = Cross(n, tangent);
      Vec3 sum{0.0f, 0.0f, 0.0f};
      float weight_sum = 0.0f;
      for (int ti = 0; ti < theta_steps; ti++) {
        float theta = (float(ti) + 0.5f) / float(theta_steps) * 0.5f * kPi;
        float st = std::sin(theta);
        float ct = std::cos(theta);
        for (int pi = 0; pi < phi_steps; pi++) {
          float phi = (float(pi) + 0.5f) / float(phi_steps) * 2.0f * kPi;
          Vec3 h = Add(Add(Mul(tangent, std::cos(phi) * st),
                           Mul(bitangent, std::sin(phi) * st)),
                       Mul(n, ct));
          float w = ct * st;
          sum = Add(sum, Mul(SampleEnv(env, h), w));
          weight_sum += w;
        }
      }
      out.pixels[size_t(y) * size_t(width) + size_t(x)] = Div(sum, weight_sum);
    }
  }
  return out;
}

EnvImage PrefilterEnvMip(const EnvImage &env, int width, int height,
                         float roughness) {
  EnvImage out;
  out.width = width;
  out.height = height;
  out.pixels.resize(size_t(width) * size_t(height));
  int radius = std::max(0, int(std::round(roughness * 8.0f)));
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      float u = (float(x) + 0.5f) / float(width);
      float v = (float(y) + 0.5f) / float(height);
      Vec3 sum{0.0f, 0.0f, 0.0f};
      float weight_sum = 0.0f;
      for (int yy = -radius; yy <= radius; yy++) {
        for (int xx = -radius; xx <= radius; xx++) {
          float du = float(xx) / float(std::max(1, width));
          float dv = float(yy) / float(std::max(1, height));
          float d2 = float(xx * xx + yy * yy);
          float sigma = std::max(1.0f, float(radius) * 0.5f);
          float w = radius == 0 ? 1.0f : std::exp(-d2 / (2.0f * sigma * sigma));
          sum = Add(sum, Mul(SampleEnvNearest(env, u + du, v + dv), w));
          weight_sum += w;
        }
      }
      out.pixels[size_t(y) * size_t(width) + size_t(x)] = Div(sum, weight_sum);
    }
  }
  return out;
}

float RadicalInverseVdc(uint32_t bits) {
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return float(bits) * 2.3283064365386963e-10f;
}

Vec3 ImportanceSampleGGX(float xi0, float xi1, float roughness, const Vec3 &n) {
  constexpr float kPi = 3.14159265358979323846f;
  float a = roughness * roughness;
  float phi = 2.0f * kPi * xi0;
  float cos_theta = std::sqrt((1.0f - xi1) / (1.0f + (a * a - 1.0f) * xi1));
  float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
  Vec3 h{std::cos(phi) * sin_theta, std::sin(phi) * sin_theta, cos_theta};
  Vec3 up = (std::abs(n.z) < 0.999f) ? Vec3{0.0f, 0.0f, 1.0f}
                                     : Vec3{1.0f, 0.0f, 0.0f};
  Vec3 tangent = Normalize(Cross(up, n));
  Vec3 bitangent = Cross(n, tangent);
  return Normalize(Add(Add(Mul(tangent, h.x), Mul(bitangent, h.y)), Mul(n, h.z)));
}

float GeometrySchlickGGX(float ndotv, float roughness) {
  float a = roughness;
  float k = (a * a) / 2.0f;
  return ndotv / std::max(1.0e-6f, ndotv * (1.0f - k) + k);
}

float GeometrySmith(float ndotv, float ndotl, float roughness) {
  return GeometrySchlickGGX(ndotv, roughness) *
         GeometrySchlickGGX(ndotl, roughness);
}

void BuildBrdfLut(int size, IblCache *ibl) {
  if (!ibl || size <= 0) return;
  ibl->brdf_size = size;
  ibl->brdf_lut.assign(size_t(size) * size_t(size) * 2, 0.0f);
  const uint32_t sample_count = 64;
  for (int y = 0; y < size; y++) {
    float roughness = (float(y) + 0.5f) / float(size);
    for (int x = 0; x < size; x++) {
      float ndotv = (float(x) + 0.5f) / float(size);
      Vec3 v{std::sqrt(std::max(0.0f, 1.0f - ndotv * ndotv)), 0.0f, ndotv};
      float a = 0.0f;
      float b = 0.0f;
      Vec3 n{0.0f, 0.0f, 1.0f};
      for (uint32_t i = 0; i < sample_count; i++) {
        Vec3 h = ImportanceSampleGGX(float(i) / float(sample_count),
                                     RadicalInverseVdc(i), roughness, n);
        Vec3 l = Normalize(Sub(Mul(h, 2.0f * Dot(v, h)), v));
        float ndotl = std::max(0.0f, l.z);
        float ndoth = std::max(0.0f, h.z);
        float vdoth = std::max(0.0f, Dot(v, h));
        if (ndotl > 0.0f) {
          float g = GeometrySmith(ndotv, ndotl, roughness);
          float g_vis = (g * vdoth) / std::max(1.0e-6f, ndoth * ndotv);
          float fc = std::pow(1.0f - vdoth, 5.0f);
          a += (1.0f - fc) * g_vis;
          b += fc * g_vis;
        }
      }
      size_t ofs = (size_t(y) * size_t(size) + size_t(x)) * 2;
      ibl->brdf_lut[ofs + 0] = a / float(sample_count);
      ibl->brdf_lut[ofs + 1] = b / float(sample_count);
    }
  }
}

// Build the IBL pyramids (diffuse irradiance + prefiltered specular mips + BRDF
// LUT) from a ready environment map. Shared by the legacy (tydra texture) and
// next (file / dome light) paths.
bool BuildIblFromEnv(EnvImage &&env, IblCache *ibl) {
  if (!ibl || env.width <= 0 || env.height <= 0 || env.pixels.empty()) {
    return false;
  }
  ibl->env = std::move(env);
  ibl->diffuse = ConvolveDiffuseEnv(ibl->env, 32, 16);
  ibl->prefiltered.clear();
  const int levels = 5;
  for (int level = 0; level < levels; level++) {
    int w = std::max(4, 64 >> level);
    int h = std::max(2, 32 >> level);
    float roughness = float(level) / float(levels - 1);
    ibl->prefiltered.push_back(PrefilterEnvMip(ibl->env, w, h, roughness));
  }
  BuildBrdfLut(64, ibl);
  ibl->valid = true;
  return true;
}

bool BuildIblCache(const RenderScene &scene, const LightCache &lights,
                   IblCache *ibl) {
  if (!ibl || !lights.has_dome) return false;
  EnvImage env;
  if (!DecodeTextureToEnvImage(scene, lights.dome.texture_id, &env)) {
    return false;
  }
  return BuildIblFromEnv(std::move(env), ibl);
}

// Load a lat-long environment map (HDR float, or 8-bit) from a file into an
// EnvImage, scaled by `scale` (dome intensity * color). 8-bit is treated as
// already-linear (matching DecodeTextureToEnvImage).
bool LoadEnvImageFromFile(const std::string &path, const Vec3 &scale,
                          EnvImage *out) {
  auto res = tinyusdz::image::LoadImageFromFile(path);
  if (!res) {
    std::cerr << "WARN: failed to load environment map: " << path << "\n";
    return false;
  }
  const tinyusdz::Image &img = res.value().image;
  if (img.width <= 0 || img.height <= 0 || img.channels <= 0 ||
      img.data.empty()) {
    return false;
  }
  const size_t n = size_t(img.width) * size_t(img.height);
  const size_t ch = size_t(img.channels);
  EnvImage e;
  e.width = img.width;
  e.height = img.height;
  e.pixels.resize(n);
  if (img.format == tinyusdz::Image::PixelFormat::Float && img.bpp == 32) {
    if (img.data.size() < n * ch * sizeof(float)) return false;
    const float *src = reinterpret_cast<const float *>(img.data.data());
    for (size_t i = 0; i < n; i++) {
      const float *p = src + i * ch;
      e.pixels[i] = Vec3{p[0] * scale.x, p[std::min<size_t>(1, ch - 1)] * scale.y,
                         p[std::min<size_t>(2, ch - 1)] * scale.z};
    }
  } else if (img.bpp == 8) {
    if (img.data.size() < n * ch) return false;
    for (size_t i = 0; i < n; i++) {
      const uint8_t *p = img.data.data() + i * ch;
      e.pixels[i] =
          Vec3{float(p[0]) / 255.0f * scale.x,
               float(p[std::min<size_t>(1, ch - 1)]) / 255.0f * scale.y,
               float(p[std::min<size_t>(2, ch - 1)]) / 255.0f * scale.z};
    }
  } else {
    std::cerr << "WARN: unsupported environment-map format: " << path << "\n";
    return false;
  }
  *out = std::move(e);
  return true;
}

Vec3 SampleIblMip(const std::vector<EnvImage> &mips, const Vec3 &dir,
                  float roughness) {
  if (mips.empty()) return Vec3{0.0f, 0.0f, 0.0f};
  float f = ClampFloat(roughness, 0.0f, 1.0f) * float(mips.size() - 1);
  size_t l0 = size_t(std::floor(f));
  size_t l1 = std::min(mips.size() - 1, l0 + 1);
  float t = f - float(l0);
  return Lerp(SampleEnv(mips[l0], dir), SampleEnv(mips[l1], dir), t);
}

void SampleBrdfLut(const IblCache &ibl, float ndotv, float roughness, float *a,
                   float *b) {
  if (!a || !b || ibl.brdf_size <= 0 || ibl.brdf_lut.empty()) {
    if (a) *a = 1.0f;
    if (b) *b = 0.0f;
    return;
  }
  int x = std::min(ibl.brdf_size - 1,
                   std::max(0, int(ClampFloat(ndotv, 0.0f, 1.0f) *
                                   float(ibl.brdf_size))));
  int y = std::min(ibl.brdf_size - 1,
                   std::max(0, int(ClampFloat(roughness, 0.0f, 1.0f) *
                                   float(ibl.brdf_size))));
  size_t ofs = (size_t(y) * size_t(ibl.brdf_size) + size_t(x)) * 2;
  *a = ibl.brdf_lut[ofs + 0];
  *b = ibl.brdf_lut[ofs + 1];
}

float RectArea(const RenderLight &light) {
  return std::max(0.0f, light.width) * std::max(0.0f, light.height);
}

float DiskArea(const RenderLight &light) {
  constexpr float kPi = 3.14159265358979323846f;
  return kPi * std::max(0.0f, light.radius) * std::max(0.0f, light.radius);
}

float SphereArea(const RenderLight &light) {
  constexpr float kPi = 3.14159265358979323846f;
  return 4.0f * kPi * std::max(0.0f, light.radius) *
         std::max(0.0f, light.radius);
}

float CylinderArea(const RenderLight &light) {
  constexpr float kPi = 3.14159265358979323846f;
  return 2.0f * kPi * std::max(0.0f, light.radius) *
         std::max(0.0f, light.length);
}

Vec3 LightColor(const RenderLight &light) {
  float scale = light.intensity * std::pow(2.0f, light.exposure);
  return Mul(FromFloat3(light.color), scale);
}

void AddFiniteLight(const RenderLight &light, PreviewLight::Kind kind,
                    LightCache *cache) {
  if (!cache) return;
  PreviewLight dst;
  dst.kind = kind;
  dst.position = FromFloat3(light.position);
  dst.direction = Normalize(FromFloat3(light.direction));
  if (Length(dst.direction) < 1.0e-6f) {
    dst.direction = Vec3{0.0f, -1.0f, 0.0f};
  }
  dst.normal = Mul(dst.direction, -1.0f);
  dst.radiance = LightColor(light);
  dst.radius = light.radius;
  dst.width = light.width;
  dst.height = light.height;
  if (kind == PreviewLight::Kind::Sphere) {
    dst.area = SphereArea(light);
  } else if (kind == PreviewLight::Kind::Rect) {
    dst.area = RectArea(light);
  } else if (kind == PreviewLight::Kind::Disk) {
    dst.area = DiskArea(light);
  } else if (kind == PreviewLight::Kind::Cylinder) {
    dst.area = CylinderArea(light);
  }
  dst.power = std::max(0.0f, Luminance(dst.radiance) *
                                 std::max(1.0f, dst.area));
  cache->finite.push_back(dst);
}

void CollectLights(const RenderScene &scene, LightCache *cache) {
  if (!cache) return;
  for (const RenderLight &light : scene.lights) {
    switch (light.type) {
      case RenderLight::Type::Distant:
        AddFiniteLight(light, PreviewLight::Kind::Distant, cache);
        break;
      case RenderLight::Type::Point:
        AddFiniteLight(light, PreviewLight::Kind::Point, cache);
        break;
      case RenderLight::Type::Sphere:
        AddFiniteLight(light, PreviewLight::Kind::Sphere, cache);
        break;
      case RenderLight::Type::Rect:
        AddFiniteLight(light, PreviewLight::Kind::Rect, cache);
        break;
      case RenderLight::Type::Disk:
        AddFiniteLight(light, PreviewLight::Kind::Disk, cache);
        break;
      case RenderLight::Type::Cylinder:
        AddFiniteLight(light, PreviewLight::Kind::Cylinder, cache);
        break;
      case RenderLight::Type::Geometry:
        AddFiniteLight(light, PreviewLight::Kind::Mesh, cache);
        break;
      case RenderLight::Type::Dome:
        cache->has_dome = true;
        cache->dome.kind = PreviewLight::Kind::Dome;
        cache->dome.radiance = LightColor(light);
        cache->dome.power = std::max(0.0f, Luminance(cache->dome.radiance));
        cache->dome.texture_id = light.envmap_texture_id;
        cache->dome.texture_file = light.textureFile;
        cache->env_color = Add(cache->env_color, cache->dome.radiance);
        cache->env_cdf.clear();
        break;
      case RenderLight::Type::Portal:
        std::cerr << "WARN: PortalLight ignored: " << light.name << "\n";
        break;
    }
  }
  AppendPowerCdf(&cache->finite, &cache->finite_cdf);
  AppendPowerCdf(&cache->mesh, &cache->mesh_cdf);
}

struct PurposeFilter {
  const std::vector<TriInfo> *tris{nullptr};
  uint32_t mask{kPurposeDefaultMask};
};

int PurposeAnyHitFilter(void *user, uint32_t prim_id, float, float, float) {
  const PurposeFilter *filter = reinterpret_cast<const PurposeFilter *>(user);
  if (!filter || !filter->tris || size_t(prim_id) >= filter->tris->size()) {
    return 0;
  }
  return PurposeVisible((*filter->tris)[size_t(prim_id)].purpose_bit,
                        filter->mask)
             ? 1
             : 0;
}

bool IntersectVisibleTriangles(lrt_tri_scene *scene,
                               const std::vector<TriInfo> &tris,
                               const lrt_ray &ray, uint32_t purpose_mask,
                               lrt_hit *hit) {
  if (!scene || !hit) return false;
  static constexpr size_t kMaxHits = 64;
  lrt_ray query = ray;
  for (int iter = 0; iter < 8; ++iter) {
    lrt_hit hits[kMaxHits];
    const size_t n = lrt_tri_intersect_n(scene, &query, hits, kMaxHits);
    if (n == 0) return false;
    for (size_t i = 0; i < n; ++i) {
      const uint32_t prim_id = hits[i].prim_id;
      if (prim_id == LRT_TRI_NO_HIT || size_t(prim_id) >= tris.size()) {
        continue;
      }
      if (PurposeVisible(tris[size_t(prim_id)].purpose_bit, purpose_mask)) {
        *hit = hits[i];
        return true;
      }
    }
    query.tmin = std::nextafter(hits[n - 1].t, query.tmax);
    if (!(query.tmin < query.tmax)) return false;
  }
  return false;
}

bool Occluded(lrt_tri_scene *scene, const std::vector<TriInfo> &tris,
              const Vec3 &p, const Vec3 &n, const Vec3 &l, float max_t,
              const DirectScene *direct, uint32_t purpose_mask) {
  // Self-intersection offset must scale with the surface point's magnitude: at
  // large world coordinates a fixed 1e-4 offset is below a float32 ULP (e.g.
  // ULP(40000) ~ 0.005), so `p + n*1e-4 == p` and the shadow ray would start
  // exactly on the surface -> self-shadowing (shadow acne). Use a relative
  // epsilon (~25 ULPs) so the origin clears the surface at any scale.
  const float mag = std::max(std::max(std::fabs(p.x), std::fabs(p.y)),
                             std::fabs(p.z));
  const float eps = std::max(1.0e-4f, mag * 3.0e-6f);
  Vec3 o = Add(p, Mul(n, eps));
  lrt_ray ray;
  ray.org[0] = o.x;
  ray.org[1] = o.y;
  ray.org[2] = o.z;
  ray.tmin = eps;
  ray.dir[0] = l.x;
  ray.dir[1] = l.y;
  ray.dir[2] = l.z;
  ray.tmax = max_t;
  if (scene) {
    PurposeFilter filter{&tris, purpose_mask};
    if (lrt_tri_occluded1_filtered(scene, &ray, PurposeAnyHitFilter, &filter)) {
      return true;
    }
  }
  if (direct) {
    if (direct->spheres && lrt_tri_occluded1(direct->spheres.get(), &ray)) return true;
    if (direct->round_curves &&
        lrt_tri_occluded1(direct->round_curves.get(), &ray)) return true;
    if (direct->flat_curves &&
        lrt_tri_occluded1(direct->flat_curves.get(), &ray)) return true;
    if (direct->points && lrt_tri_occluded1(direct->points.get(), &ray)) return true;
    if (direct->bez_curves &&
        lrt_tri_occluded1(direct->bez_curves.get(), &ray)) return true;
    if (direct->tets && lrt_tri_occluded1(direct->tets.get(), &ray)) return true;
    for (const DirectShape &shape : direct->shapes) {
      DirectHit dh;
      if (IntersectDirectShape(shape, o, l, ray.tmin, ray.tmax, &dh)) return true;
    }
  }
  return false;
}

// Shadow-ray occlusion against a TLAS (two-level/instanced path). The same
// magnitude-scaled self-intersection epsilon as Occluded() is used. No purpose
// filter: purpose-invisible triangles are already culled from the BLAS at build.
bool OccludedTLAS(const lrt_tlas *tlas, const Vec3 &p, const Vec3 &n,
                  const Vec3 &l, float max_t) {
  if (!tlas) return false;
  const float mag = std::max(std::max(std::fabs(p.x), std::fabs(p.y)),
                             std::fabs(p.z));
  const float eps = std::max(1.0e-4f, mag * 3.0e-6f);
  Vec3 o = Add(p, Mul(n, eps));
  lrt_ray ray;
  ray.org[0] = o.x;
  ray.org[1] = o.y;
  ray.org[2] = o.z;
  ray.tmin = eps;
  ray.dir[0] = l.x;
  ray.dir[1] = l.y;
  ray.dir[2] = l.z;
  ray.tmax = max_t;
  return lrt_tlas_occluded1(tlas, &ray, 0xffffffffu) != 0;
}

bool IntersectDirectScene(const DirectScene *direct, const Vec3 &ray_org,
                          const Vec3 &ray_dir, float tmin, float tmax,
                          DirectHit *best) {
  if (!direct || !best) return false;
  lrt_ray ray;
  ray.org[0] = ray_org.x;
  ray.org[1] = ray_org.y;
  ray.org[2] = ray_org.z;
  ray.tmin = tmin;
  ray.dir[0] = ray_dir.x;
  ray.dir[1] = ray_dir.y;
  ray.dir[2] = ray_dir.z;
  ray.tmax = tmax;
  auto test_scene = [&](lrt_tri_scene *scene, const std::vector<TriInfo> &info,
                        bool sphere) {
    if (!scene) return;
    lrt_hit h;
    if (!lrt_tri_intersect1(scene, &ray, &h) || h.prim_id == LRT_TRI_NO_HIT ||
        size_t(h.prim_id) >= info.size() || h.t >= best->t) {
      return;
    }
    const TriInfo &ti = info[size_t(h.prim_id)];
    Vec3 p = Add(ray_org, Mul(ray_dir, h.t));
    best->t = h.t;
    if (sphere) {
      Vec3 c = ti.p0;
      best->n = Normalize(Sub(p, c));
    } else {
      best->n = Normalize(Cross(Sub(ti.p1, ti.p0), Sub(ti.p2, ti.p0)));
      if (Length(best->n) < 1.0e-6f) {
        best->n = Normalize(Sub(ray_org, p));
      }
    }
    best->base_color = ti.base_color;
    best->emission = ti.emission;
    best->hit = true;
  };
  test_scene(direct->spheres.get(), direct->sphere_info, true);
  test_scene(direct->round_curves.get(), direct->round_curve_info, false);
  test_scene(direct->flat_curves.get(), direct->flat_curve_info, false);
  test_scene(direct->points.get(), direct->point_info, true);
  test_scene(direct->bez_curves.get(), direct->bez_curve_info, false);
  if (direct->tets) {
    lrt_hit h;
    if (lrt_tri_intersect1(direct->tets.get(), &ray, &h) &&
        h.prim_id != LRT_TRI_NO_HIT &&
        size_t(h.prim_id) < direct->tet_prims.size() && h.t < best->t) {
      const TetPrim &tet = direct->tet_prims[size_t(h.prim_id)];
      float t = h.t;
      Vec3 n;
      if (IntersectTetPrim(tet, ray_org, ray_dir, tmin, best->t, &t, &n)) {
        best->t = t;
        best->n = n;
        best->base_color = tet.base_color;
        best->emission = tet.emission;
        best->hit = true;
      }
    }
  }
  for (const DirectShape &shape : direct->shapes) {
    DirectHit h;
    if (IntersectDirectShape(shape, ray_org, ray_dir, tmin, best->t, &h) &&
        h.t < best->t) {
      *best = h;
    }
  }
  return best->hit;
}

// A bottom-level acceleration structure (BLAS): one prototype's (or the base
// scene's) geometry in its OWN local space, with the parallel per-triangle
// attributes. Instanced via a TLAS; the prototype geometry is stored once
// regardless of how many times it is placed.
struct Blas {
  FloatVec vertices;  // packed local-space triangle positions
  TriStoreVec tris;   // local p0/p1/p2/n/purpose + mat_id (into mat_table)
  std::vector<TriMat> mat_table;  // one entry per source mesh-job
  FloatVec tri_uvs;   // 6 floats/tri (parallel to tris) or empty
  FloatVec tri_colors;  // 12 floats/tri (per-corner RGBA) or empty
  FloatVec tri_normals; // 9 floats/tri (per-corner authored normals) or empty
  // Curve BLAS (is_curve): the scene is a LightRT round-hair scene and
  // curve_info holds one TriInfo per segment (local-space endpoints + color),
  // resolved at hit time exactly like the DirectScene curve path.
  bool is_curve{false};
  std::vector<TriInfo> curve_info;
  lrt_tri_scene *scene{nullptr};

  Blas() = default;
  Blas(Blas &&o) noexcept { *this = std::move(o); }
  Blas &operator=(Blas &&o) noexcept {
    if (this != &o) {
      vertices = std::move(o.vertices);
      tris = std::move(o.tris);
      mat_table = std::move(o.mat_table);
      tri_uvs = std::move(o.tri_uvs);
      tri_colors = std::move(o.tri_colors);
      tri_normals = std::move(o.tri_normals);
      is_curve = o.is_curve;
      curve_info = std::move(o.curve_info);
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

// 3x4 row-major object->world for LightRT (p' = L*p + t), derived from a
// row-vector matrix4d so it matches TransformPoint(m, p) exactly.
inline void Mat4ToObj2World(const matrix4d &m, float out[12]) {
  for (int k = 0; k < 3; ++k) {
    out[k * 4 + 0] = float(m.m[0][k]);
    out[k * 4 + 1] = float(m.m[1][k]);
    out[k * 4 + 2] = float(m.m[2][k]);
    out[k * 4 + 3] = float(m.m[3][k]);
  }
}

// Apply a 3x4 object->world (same layout as Mat4ToObj2World) to a point. Matches
// TransformPoint(matrix4d, p) for affine transforms (instances are affine), in
// float — the same precision LightRT uses for traversal.
inline Vec3 TransformPointO2W(const float o[12], const Vec3 &p) {
  return Vec3{o[0] * p.x + o[1] * p.y + o[2] * p.z + o[3],
              o[4] * p.x + o[5] * p.y + o[6] * p.z + o[7],
              o[8] * p.x + o[9] * p.y + o[10] * p.z + o[11]};
}

// Transform a direction (e.g. a normal) by the 3x3 part of a 3x4 object->world.
// Exact for rigid/uniform-scale instances (the common case); non-uniform scale
// is approximate (true normals need the inverse-transpose) — fine for preview.
inline Vec3 TransformDirO2W(const float o[12], const Vec3 &v) {
  return Vec3{o[0] * v.x + o[1] * v.y + o[2] * v.z,
              o[4] * v.x + o[5] * v.y + o[6] * v.z,
              o[8] * v.x + o[9] * v.y + o[10] * v.z};
}

// One placement of a BLAS, stored as a compact 3x4 float object->world (48 B vs a
// 128 B matrix4d) — instance arrays dominate footprint on heavily-instanced
// scenes (e.g. Island's 22 M instances). instances[0] is the base at identity.
struct InstanceRT {
  uint32_t blas_id{0};
  float o2w[12]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
};

// Resolve a primary TLAS hit into a world-space TriInfo (positions transformed,
// normal recomputed, diffuse texture sampled). Returns false if the hit indices
// are out of range. `textures` is the shared global texture table.
// CPU port of Storm's ComputeTBNMatrix/PerturbNormal
// (pxr/imaging/hdSt/shaders/surfaceHelpers.glslfx, "Surface Gradient-Based Bump
// Mapping Framework" 2020): the per-fragment screen-space derivatives dFdx/dFdy
// of position and st are replaced by this (planar) triangle's edge and UV deltas,
// which give the same constant gradient across the face. `N` is the geometric
// normal; `Nt` is the tangent-space normal already unpacked to [-1,1]. Returns
// the perturbed world normal, or `N` if the UV parameterization is degenerate.
Vec3 PerturbNormalStorm(const Vec3 &p0, const Vec3 &p1, const Vec3 &p2,
                        const Vec3 &N, float u0, float v0, float u1, float v1,
                        float u2, float v2, const Vec3 &Nt) {
  Vec3 dP1 = Sub(p1, p0), dP2 = Sub(p2, p0);
  Vec3 sigmaX = Sub(dP1, Mul(N, Dot(dP1, N)));
  Vec3 sigmaY = Sub(dP2, Mul(N, Dot(dP2, N)));
  float flipSign = Dot(dP2, Cross(N, dP1)) < 0.0f ? -1.0f : 1.0f;
  const float du1 = u1 - u0, dv1 = v1 - v0, du2 = u2 - u0, dv2 = v2 - v0;
  const float det = du1 * dv2 - dv1 * du2;
  const float signDet = det < 0.0f ? -1.0f : 1.0f;
  // First column of the inverse st matrix, scaled by sign (not divided by det).
  Vec3 T = Add(Mul(sigmaX, signDet * dv2), Mul(sigmaY, signDet * (-dv1)));
  if (std::fabs(det) <= 0.0f || Length(T) < 1.0e-12f) return N;
  T = Normalize(T);
  Vec3 B = Mul(Cross(N, T), signDet * flipSign);
  Vec3 pert = Add(Add(Mul(T, Nt.x), Mul(B, Nt.y)), Mul(N, Nt.z));
  float l = Length(pert);
  return l > 1.0e-12f ? Mul(pert, 1.0f / l) : N;
}

// Sample a normal map at (u,v,lod) and unpack to a tangent-space normal via the
// texture's scale/bias (UsdUVTexture convention).
Vec3 SampleTangentNormal(const Texture &nm, float u, float v, float lod) {
  Vec3 s = nm.sample(u, v, lod);  // raw [0,1] (normal maps are sourceColorSpace=raw)
  return Vec3{s.x * nm.scale.x + nm.bias.x, s.y * nm.scale.y + nm.bias.y,
              s.z * nm.scale.z + nm.bias.z};
}

inline float ChannelOf(const Vec3 &c, uint8_t ch) {
  return ch == 1 ? c.y : ch == 2 ? c.z : c.x;  // r/g/b (a not sampled -> r)
}

// Sample a scalar (roughness/metallic) texture's channel into [0,1].
inline float SampleScalarTex(const std::vector<Texture> &textures, int32_t id,
                             uint8_t ch, float u, float v, float lod) {
  if (id < 0 || size_t(id) >= textures.size()) return -1.0f;
  return ChannelOf(textures[size_t(id)].sample(u, v, lod), ch);
}

// Per-pixel ray differential: the camera rays for the +1 pixel neighbors in
// screen x and y (origin + direction each, to cover both pinhole and ortho).
struct RayDiff {
  Vec3 ox, dx;  // ray for pixel (x+1, y)
  Vec3 oy, dy;  // ray for pixel (x, y+1)
  bool valid{false};
};

// Barycentric (e1,e2 coords) of point Q on triangle p0,p1,p2 -> interpolated UV.
inline void TriPointUV(const Vec3 &Q, const Vec3 &p0, const Vec3 &e1,
                       const Vec3 &e2, float d00, float d01, float d11,
                       float invden, float u0, float v0, float du1, float dv1,
                       float du2, float dv2, float *u, float *v) {
  Vec3 q = Sub(Q, p0);
  float d20 = Dot(q, e1), d21 = Dot(q, e2);
  float a = (d11 * d20 - d01 * d21) * invden;
  float b = (d00 * d21 - d01 * d20) * invden;
  *u = u0 + a * du1 + b * du2;
  *v = v0 + a * dv1 + b * dv2;
}

// Mip LOD from the UV footprint (OpenGL isotropic formula): the larger of the
// two screen-axis texel spans. Returns 0 (full res) when textures aren't
// minified or differentials are unavailable.
float TextureLod(float dudx, float dvdx, float dudy, float dvdy, int w, int h) {
  float px = dudx * float(w), py = dvdx * float(h);
  float qx = dudy * float(w), qy = dvdy * float(h);
  float rho2 = std::max(px * px + py * py, qx * qx + qy * qy);
  if (!(rho2 > 1.0e-20f)) return 0.0f;
  return 0.5f * std::log2(rho2);
}

// Per-hit UV footprint via ray differentials: intersect the primary + neighbor
// rays with the hit triangle's plane and difference their UVs. Outputs the UV
// gradients per screen pixel. Returns false if the parameterization/plane is
// degenerate (caller then uses lod 0).
bool ComputeUVFootprint(const Vec3 &org, const Vec3 &dir, const RayDiff &rd,
                        const Vec3 &p0, const Vec3 &p1, const Vec3 &p2,
                        const Vec3 &N, float u0, float v0, float u1, float v1,
                        float u2, float v2, float *dudx, float *dvdx,
                        float *dudy, float *dvdy) {
  if (!rd.valid) return false;
  Vec3 e1 = Sub(p1, p0), e2 = Sub(p2, p0);
  float d00 = Dot(e1, e1), d01 = Dot(e1, e2), d11 = Dot(e2, e2);
  float den = d00 * d11 - d01 * d01;
  if (std::fabs(den) < 1.0e-20f) return false;
  float invden = 1.0f / den;
  float du1 = u1 - u0, dv1 = v1 - v0, du2 = u2 - u0, dv2 = v2 - v0;
  float pd = Dot(p0, N);
  auto hit_uv = [&](const Vec3 &o, const Vec3 &d, float *u, float *v) -> bool {
    float denom = Dot(d, N);
    if (std::fabs(denom) < 1.0e-12f) return false;
    float t = (pd - Dot(o, N)) / denom;
    if (t <= 0.0f) return false;
    TriPointUV(Add(o, Mul(d, t)), p0, e1, e2, d00, d01, d11, invden, u0, v0, du1,
               dv1, du2, dv2, u, v);
    return true;
  };
  float u, v, ux, vx, uy, vy;
  if (!hit_uv(org, dir, &u, &v) || !hit_uv(rd.ox, rd.dx, &ux, &vx) ||
      !hit_uv(rd.oy, rd.dy, &uy, &vy)) {
    return false;
  }
  *dudx = ux - u;
  *dvdx = vx - v;
  *dudy = uy - u;
  *dvdy = vy - v;
  return true;
}

bool ResolveTLASHit(const lrt_tlas_hit &th, const std::vector<Blas> &blas,
                    const std::vector<InstanceRT> &instances,
                    const std::vector<Texture> *textures, const Vec3 &ray_org,
                    const Vec3 &ray_dir, const RayDiff &rd, TriInfo *out) {
  if (size_t(th.inst_id) >= instances.size()) return false;
  const InstanceRT &inst = instances[size_t(th.inst_id)];
  if (size_t(inst.blas_id) >= blas.size()) return false;
  const Blas &b = blas[size_t(inst.blas_id)];
  // Curve BLAS: resolve the segment's local TriInfo, transform endpoints to world
  // (no UVs/textures for curves — matches the DirectScene curve path).
  if (b.is_curve) {
    if (size_t(th.prim_id) >= b.curve_info.size()) return false;
    const TriInfo &lt = b.curve_info[size_t(th.prim_id)];
    Vec3 wp0 = TransformPointO2W(inst.o2w, lt.p0);
    Vec3 wp1 = TransformPointO2W(inst.o2w, lt.p1);
    Vec3 wp2 = TransformPointO2W(inst.o2w, lt.p2);
    *out = lt;
    out->p0 = wp0;
    out->p1 = wp1;
    out->p2 = wp2;
    out->n = Normalize(Cross(Sub(wp1, wp0), Sub(wp2, wp0)));
    return true;
  }
  if (size_t(th.prim_id) >= b.tris.size()) return false;
  if (size_t(th.prim_id) * 9 + 8 >= b.vertices.size()) return false;
  const TriStore &ts = b.tris[size_t(th.prim_id)];
  TriInfo lt = CombineTriMat(size_t(ts.mat_id) < b.mat_table.size()
                                 ? b.mat_table[size_t(ts.mat_id)]
                                 : TriMat{});
  // Per-corner displayColor/displayOpacity (RGBA), barycentrically interpolated.
  if (size_t(th.prim_id) * 12 + 11 < b.tri_colors.size()) {
    const float *cc = &b.tri_colors[size_t(th.prim_id) * 12];
    const float w1 = th.u, w2 = th.v, w0 = 1.0f - w1 - w2;
    lt.base_color = Vec3{w0 * cc[0] + w1 * cc[4] + w2 * cc[8],
                         w0 * cc[1] + w1 * cc[5] + w2 * cc[9],
                         w0 * cc[2] + w1 * cc[6] + w2 * cc[10]};
    lt.opacity = w0 * cc[3] + w1 * cc[7] + w2 * cc[11];
  }
  // Local-space positions come from the vertex soup (LightRT aliases it).
  const float *v = &b.vertices[size_t(th.prim_id) * 9];
  Vec3 wp0 = TransformPointO2W(inst.o2w, Vec3{v[0], v[1], v[2]});
  Vec3 wp1 = TransformPointO2W(inst.o2w, Vec3{v[3], v[4], v[5]});
  Vec3 wp2 = TransformPointO2W(inst.o2w, Vec3{v[6], v[7], v[8]});
  *out = lt;
  out->p0 = wp0;
  out->p1 = wp1;
  out->p2 = wp2;
  out->n = Normalize(Cross(Sub(wp1, wp0), Sub(wp2, wp0)));
  // Smooth shading: interpolate per-corner authored normals (local) and transform
  // by the instance, falling back to the geometric normal above.
  if (size_t(th.prim_id) * 9 + 8 < b.tri_normals.size()) {
    const float *nn = &b.tri_normals[size_t(th.prim_id) * 9];
    const float w1 = th.u, w2 = th.v, w0 = 1.0f - w1 - w2;
    Vec3 ln{w0 * nn[0] + w1 * nn[3] + w2 * nn[6],
            w0 * nn[1] + w1 * nn[4] + w2 * nn[7],
            w0 * nn[2] + w1 * nn[5] + w2 * nn[8]};
    Vec3 wn = TransformDirO2W(inst.o2w, ln);
    if (Length(wn) > 1.0e-12f) out->n = Normalize(wn);
  }
  const bool has_tex = (lt.tex_id >= 0 || lt.normal_tex_id >= 0 ||
                        lt.rough_tex_id >= 0 || lt.metal_tex_id >= 0 ||
                        lt.emission_tex_id >= 0 || lt.occ_tex_id >= 0);
  if (has_tex && textures && !b.tri_uvs.empty()) {
    const size_t base = size_t(th.prim_id) * 6;
    if (base + 5 < b.tri_uvs.size()) {
      const float w1 = th.u, w2 = th.v, w0 = 1.0f - w1 - w2;
      const float *uv = &b.tri_uvs[base];
      float u = w0 * uv[0] + w1 * uv[2] + w2 * uv[4];
      float v = w0 * uv[1] + w1 * uv[3] + w2 * uv[5];
      // Ray-differential UV footprint (shared by all texture samples).
      float dudx = 0, dvdx = 0, dudy = 0, dvdy = 0;
      const bool have_fp =
          ComputeUVFootprint(ray_org, ray_dir, rd, wp0, wp1, wp2, out->n, uv[0],
                             uv[1], uv[2], uv[3], uv[4], uv[5], &dudx, &dvdx,
                             &dudy, &dvdy);
      auto scalar_lod = [&](const Texture &tx) {
        return have_fp ? TextureLod(dudx, dvdx, dudy, dvdy, tx.width, tx.height)
                       : 0.0f;
      };
      if (lt.tex_id >= 0 && size_t(lt.tex_id) < textures->size()) {
        const Texture &dt = (*textures)[size_t(lt.tex_id)];
        Vec3 t = have_fp ? dt.sample_aniso(u, v, dudx, dvdx, dudy, dvdy, kMaxAniso)
                         : dt.sample(u, v, 0.0f);
        t = Vec3{t.x * dt.scale.x + dt.bias.x, t.y * dt.scale.y + dt.bias.y,
                 t.z * dt.scale.z + dt.bias.z};
        out->base_color = Vec3{lt.base_color.x * t.x, lt.base_color.y * t.y,
                               lt.base_color.z * t.z};
      }
      if (lt.rough_tex_id >= 0) {
        float r = SampleScalarTex(*textures, lt.rough_tex_id, lt.rough_ch, u, v,
                                  scalar_lod((*textures)[size_t(lt.rough_tex_id)]));
        if (r >= 0.0f) out->roughness = r;
      }
      if (lt.metal_tex_id >= 0) {
        float m = SampleScalarTex(*textures, lt.metal_tex_id, lt.metal_ch, u, v,
                                  scalar_lod((*textures)[size_t(lt.metal_tex_id)]));
        if (m >= 0.0f) out->metallic = m;
      }
      if (lt.emission_tex_id >= 0 &&
          size_t(lt.emission_tex_id) < textures->size()) {
        const Texture &et = (*textures)[size_t(lt.emission_tex_id)];
        Vec3 e = have_fp ? et.sample_aniso(u, v, dudx, dvdx, dudy, dvdy, kMaxAniso)
                         : et.sample(u, v, 0.0f);
        out->emission = Vec3{lt.emission.x * e.x, lt.emission.y * e.y,
                             lt.emission.z * e.z};
      }
      if (lt.occ_tex_id >= 0) {
        float o = SampleScalarTex(*textures, lt.occ_tex_id, lt.occ_ch, u, v,
                                  scalar_lod((*textures)[size_t(lt.occ_tex_id)]));
        if (o >= 0.0f) out->occlusion = o;
      }
      if (lt.normal_tex_id >= 0 &&
          size_t(lt.normal_tex_id) < textures->size()) {
        const Texture &nt = (*textures)[size_t(lt.normal_tex_id)];
        Vec3 Nt = SampleTangentNormal(nt, u, v, scalar_lod(nt));
        out->n = PerturbNormalStorm(wp0, wp1, wp2, out->n, uv[0], uv[1], uv[2],
                                    uv[3], uv[4], uv[5], Nt);
      }
    }
  }
  return true;
}

Vec3 Shade(lrt_tri_scene *scene, const DirectScene *direct,
           const std::vector<TriInfo> &tris,
           const LightCache &lights, const IblCache *ibl,
           const CameraFrame &camera,
           const Options &opt, const Vec3 &ray_org, const Vec3 &ray_dir,
           const std::vector<Texture> *textures = nullptr,
           const std::vector<float> *tri_uvs = nullptr,
           const lrt_tlas *tlas = nullptr,
           const std::vector<Blas> *blas = nullptr,
           const std::vector<InstanceRT> *instances = nullptr,
           const RayDiff &rd = RayDiff{}, int depth = 0,
           const std::vector<float> *tri_colors = nullptr,
           const std::vector<float> *tri_normals = nullptr) {
  lrt_ray ray;
  ray.org[0] = ray_org.x;
  ray.org[1] = ray_org.y;
  ray.org[2] = ray_org.z;
  ray.tmin = camera.znear;
  ray.dir[0] = ray_dir.x;
  ray.dir[1] = ray_dir.y;
  ray.dir[2] = ray_dir.z;
  ray.tmax = camera.zfar;
  // Primary triangle hit. Two-level (TLAS/instanced) path resolves the hit into
  // a world-space TriInfo; the flat path indexes the shared tris[] directly.
  bool tri_hit = false;
  float tri_t = camera.zfar;
  TriInfo hit_tri;
  if (tlas) {
    lrt_tlas_hit th;
    if (lrt_tlas_intersect1(tlas, &ray, 0xffffffffu, &th) && blas && instances &&
        ResolveTLASHit(th, *blas, *instances, textures, ray_org, ray_dir, rd,
                       &hit_tri)) {
      tri_t = th.t;
      tri_hit = true;
    }
  } else {
    lrt_hit hit;
    if (IntersectVisibleTriangles(scene, tris, ray, opt.purpose_mask, &hit) &&
        hit.prim_id != LRT_TRI_NO_HIT && size_t(hit.prim_id) < tris.size()) {
      hit_tri = tris[size_t(hit.prim_id)];
      // Per-corner displayColor/displayOpacity (RGBA), barycentric-interpolated.
      if (tri_colors && size_t(hit.prim_id) * 12 + 11 < tri_colors->size()) {
        const float *cc = &(*tri_colors)[size_t(hit.prim_id) * 12];
        const float w1 = hit.u, w2 = hit.v, w0 = 1.0f - w1 - w2;
        hit_tri.base_color = Vec3{w0 * cc[0] + w1 * cc[4] + w2 * cc[8],
                                  w0 * cc[1] + w1 * cc[5] + w2 * cc[9],
                                  w0 * cc[2] + w1 * cc[6] + w2 * cc[10]};
        hit_tri.opacity = w0 * cc[3] + w1 * cc[7] + w2 * cc[11];
      }
      // Smooth shading: interpolate per-corner authored normals (world-space in
      // the flat path), falling back to the stored geometric normal.
      if (tri_normals && size_t(hit.prim_id) * 9 + 8 < tri_normals->size()) {
        const float *nn = &(*tri_normals)[size_t(hit.prim_id) * 9];
        const float w1 = hit.u, w2 = hit.v, w0 = 1.0f - w1 - w2;
        Vec3 sn{w0 * nn[0] + w1 * nn[3] + w2 * nn[6],
                w0 * nn[1] + w1 * nn[4] + w2 * nn[7],
                w0 * nn[2] + w1 * nn[5] + w2 * nn[8]};
        if (Length(sn) > 1.0e-12f) hit_tri.n = Normalize(sn);
      }
      // Diffuse/normal textures: interpolate per-vertex UV with the barycentric
      // hit weights (Moller-Trumbore: w0=1-u-v for p0, u for p1, v for p2).
      if ((hit_tri.tex_id >= 0 || hit_tri.normal_tex_id >= 0 ||
           hit_tri.rough_tex_id >= 0 || hit_tri.metal_tex_id >= 0 ||
           hit_tri.emission_tex_id >= 0 || hit_tri.occ_tex_id >= 0) &&
          textures && tri_uvs) {
        const size_t base = size_t(hit.prim_id) * 6;
        if (base + 5 < tri_uvs->size()) {
          const float w1 = hit.u, w2 = hit.v, w0 = 1.0f - w1 - w2;
          const float *uv = &(*tri_uvs)[base];
          float u = w0 * uv[0] + w1 * uv[2] + w2 * uv[4];
          float v = w0 * uv[1] + w1 * uv[3] + w2 * uv[5];
          float dudx = 0, dvdx = 0, dudy = 0, dvdy = 0;
          const bool have_fp = ComputeUVFootprint(
              ray_org, ray_dir, rd, hit_tri.p0, hit_tri.p1, hit_tri.p2,
              hit_tri.n, uv[0], uv[1], uv[2], uv[3], uv[4], uv[5], &dudx, &dvdx,
              &dudy, &dvdy);
          auto scalar_lod = [&](const Texture &tx) {
            return have_fp
                       ? TextureLod(dudx, dvdx, dudy, dvdy, tx.width, tx.height)
                       : 0.0f;
          };
          if (hit_tri.tex_id >= 0 && size_t(hit_tri.tex_id) < textures->size()) {
            const Texture &dt = (*textures)[size_t(hit_tri.tex_id)];
            Vec3 t = have_fp ? dt.sample_aniso(u, v, dudx, dvdx, dudy, dvdy,
                                               kMaxAniso)
                             : dt.sample(u, v, 0.0f);
            t = Vec3{t.x * dt.scale.x + dt.bias.x, t.y * dt.scale.y + dt.bias.y,
                     t.z * dt.scale.z + dt.bias.z};
            hit_tri.base_color = Vec3{hit_tri.base_color.x * t.x,
                                      hit_tri.base_color.y * t.y,
                                      hit_tri.base_color.z * t.z};
          }
          if (hit_tri.rough_tex_id >= 0) {
            float r = SampleScalarTex(
                *textures, hit_tri.rough_tex_id, hit_tri.rough_ch, u, v,
                scalar_lod((*textures)[size_t(hit_tri.rough_tex_id)]));
            if (r >= 0.0f) hit_tri.roughness = r;
          }
          if (hit_tri.metal_tex_id >= 0) {
            float m = SampleScalarTex(
                *textures, hit_tri.metal_tex_id, hit_tri.metal_ch, u, v,
                scalar_lod((*textures)[size_t(hit_tri.metal_tex_id)]));
            if (m >= 0.0f) hit_tri.metallic = m;
          }
          if (hit_tri.emission_tex_id >= 0 &&
              size_t(hit_tri.emission_tex_id) < textures->size()) {
            const Texture &et = (*textures)[size_t(hit_tri.emission_tex_id)];
            Vec3 e = have_fp ? et.sample_aniso(u, v, dudx, dvdx, dudy, dvdy,
                                               kMaxAniso)
                             : et.sample(u, v, 0.0f);
            hit_tri.emission = Vec3{hit_tri.emission.x * e.x,
                                    hit_tri.emission.y * e.y,
                                    hit_tri.emission.z * e.z};
          }
          if (hit_tri.occ_tex_id >= 0) {
            float o = SampleScalarTex(
                *textures, hit_tri.occ_tex_id, hit_tri.occ_ch, u, v,
                scalar_lod((*textures)[size_t(hit_tri.occ_tex_id)]));
            if (o >= 0.0f) hit_tri.occlusion = o;
          }
          if (hit_tri.normal_tex_id >= 0 &&
              size_t(hit_tri.normal_tex_id) < textures->size()) {
            const Texture &nt = (*textures)[size_t(hit_tri.normal_tex_id)];
            Vec3 Nt = SampleTangentNormal(nt, u, v, scalar_lod(nt));
            hit_tri.n = PerturbNormalStorm(hit_tri.p0, hit_tri.p1, hit_tri.p2,
                                           hit_tri.n, uv[0], uv[1], uv[2], uv[3],
                                           uv[4], uv[5], Nt);
          }
        }
      }
      tri_t = hit.t;
      tri_hit = true;
    }
  }
  float best_t = tri_hit ? tri_t : camera.zfar;
  DirectHit direct_hit;
  IntersectDirectScene(direct, ray_org, ray_dir, camera.znear, best_t, &direct_hit);
  if (!tri_hit && !direct_hit.hit) {
    if (ibl && ibl->valid) {
      return Add(opt.bg, SampleEnv(ibl->env, ray_dir));
    }
    return lights.has_dome ? Add(opt.bg, lights.env_color) : opt.bg;
  }
  TriInfo tri;
  float hit_t = best_t;
  if (direct_hit.hit) {
    hit_t = direct_hit.t;
    tri.n = direct_hit.n;
    tri.base_color = direct_hit.base_color;
    tri.emission = direct_hit.emission;
  } else {
    tri = hit_tri;
  }
  // Occlusion against whichever acceleration structure is active.
  auto occluded = [&](const Vec3 &op, const Vec3 &on, const Vec3 &ol,
                      float omax) -> bool {
    return tlas ? OccludedTLAS(tlas, op, on, ol, omax)
                : Occluded(scene, tris, op, on, ol, omax, direct,
                           opt.purpose_mask);
  };
  Vec3 p = Add(ray_org, Mul(ray_dir, hit_t));
  Vec3 n = tri.n;
  if (Dot(n, ray_dir) > 0.0f) {
    n = Mul(n, -1.0f);
  }
  // Occlusion (AO) modulates the indirect/ambient response, not self-emission.
  Vec3 c = Add(Mul(tri.base_color, opt.ambient * tri.occlusion), tri.emission);
  if (ibl && ibl->valid) {
    Vec3 view = Normalize(Mul(ray_dir, -1.0f));
    Vec3 diffuse = SampleEnv(ibl->diffuse, n);
    float ndotv = std::max(0.0f, Dot(n, view));
    Vec3 f0 = Lerp(Vec3{0.04f, 0.04f, 0.04f}, tri.base_color, tri.metallic);
    Vec3 refl = Reflect(Mul(view, -1.0f), n);
    Vec3 spec_env = SampleIblMip(ibl->prefiltered, refl, tri.roughness);
    float brdf_a = 1.0f;
    float brdf_b = 0.0f;
    SampleBrdfLut(*ibl, ndotv, tri.roughness, &brdf_a, &brdf_b);
    Vec3 spec = Mul(spec_env, Add(Mul(f0, brdf_a), Vec3{brdf_b, brdf_b, brdf_b}));
    Vec3 kd = Mul(Vec3{1.0f - f0.x, 1.0f - f0.y, 1.0f - f0.z},
                  1.0f - tri.metallic);
    Vec3 diff = Mul(Mul(Mul(tri.base_color, diffuse), kd), tri.occlusion);
    c = Add(c, Add(diff, spec));
  } else if (lights.has_dome) {
    c = Add(c, Mul(Mul(tri.base_color, lights.env_color), 0.25f));
  }
  // primvars:displayOpacity < 1: see-through. Blend the surface shade with what
  // lies behind it (continuation ray; bounded recursion). Opaque hits (default
  // opacity 1.0) return `col` unchanged, so opaque renders are byte-identical.
  auto apply_opacity = [&](const Vec3 &col) -> Vec3 {
    if (tri.opacity >= 0.999f || depth >= 4) return col;
    const float a = std::max(0.0f, tri.opacity);
    const Vec3 behind_org = Add(ray_org, Mul(ray_dir, hit_t + 0.01f));
    const Vec3 behind =
        Shade(scene, direct, tris, lights, ibl, camera, opt, behind_org, ray_dir,
              textures, tri_uvs, tlas, blas, instances, rd, depth + 1, tri_colors,
              tri_normals);
    return Add(Mul(col, a), Mul(behind, 1.0f - a));
  };
  if (lights.finite.empty() && lights.mesh.empty()) {
    Vec3 l = Normalize(Sub(camera.origin, p));
    float ndotl = std::max(0.0f, Dot(n, l));
    if (ndotl > 0.0f &&
        (!opt.shadows ||
         !occluded(p, n, l,
                   std::max(0.0f, Length(Sub(camera.origin, p)) - 1.0e-3f)))) {
      c = Add(c, Mul(tri.base_color, ndotl));
    }
    return apply_opacity(c);
  }
  auto eval_light = [&](const PreviewLight &light) {
    Vec3 l;
    float max_t = 1.0e30f;
    Vec3 radiance = light.radiance;
    if (light.kind == PreviewLight::Kind::Distant) {
      l = Normalize(Mul(light.direction, -1.0f));
    } else {
      Vec3 d = Sub(light.position, p);
      float dist = Length(d);
      if (dist <= 1.0e-6f) return;
      l = Mul(d, 1.0f / dist);
      max_t = std::max(0.0f, dist - 1.0e-3f);
      if (light.kind == PreviewLight::Kind::Mesh ||
          light.kind == PreviewLight::Kind::Rect ||
          light.kind == PreviewLight::Kind::Disk ||
          light.kind == PreviewLight::Kind::Cylinder) {
        float emit_cos = std::max(0.0f, Dot(light.normal, Mul(l, -1.0f)));
        if (emit_cos <= 0.0f) return;
        radiance = Mul(radiance, emit_cos * std::max(1.0f, light.area));
      }
      radiance = Mul(radiance, 1.0f / std::max(1.0e-4f, dist * dist));
    }
    float ndotl = std::max(0.0f, Dot(n, l));
    if (ndotl <= 0.0f) return;
    if (opt.shadows && occluded(p, n, l, max_t)) {
      return;
    }
    c = Add(c, Mul(Mul(tri.base_color, radiance), ndotl));
  };
  for (const PreviewLight &light : lights.finite) {
    eval_light(light);
  }
  for (const PreviewLight &light : lights.mesh) {
    eval_light(light);
  }
  // primvars:displayOpacity < 1: see-through. Blend the surface shade with what
  // lies behind it (continuation ray), bounded recursion. Opaque hits (the
  // default opacity 1.0) skip this entirely, so opaque renders are unchanged.
  return apply_opacity(c);
}

uint8_t ToSRGB8(float linear) {
  linear = std::max(0.0f, linear);
  float mapped = linear / (1.0f + linear);
  float srgb = (mapped <= 0.0031308f)
                   ? (12.92f * mapped)
                   : (1.055f * std::pow(mapped, 1.0f / 2.4f) - 0.055f);
  int v = int(std::round(std::max(0.0f, std::min(1.0f, srgb)) * 255.0f));
  return uint8_t(std::max(0, std::min(255, v)));
}

void MakeRay(const CameraFrame &camera, float aspect, float sx, float sy,
             Vec3 *org, Vec3 *dir) {
  float px = 2.0f * sx - 1.0f;
  float py = 1.0f - 2.0f * sy;
  if (camera.ortho) {
    float xmag = camera.xmag;
    float ymag = camera.ymag;
    if (xmag <= 0.0f) xmag = ymag * aspect;
    if (ymag <= 0.0f) ymag = xmag / std::max(1.0e-6f, aspect);
    *org = Add(camera.origin,
               Add(Mul(camera.right, px * xmag * 0.5f),
                   Mul(camera.up, py * ymag * 0.5f)));
    *dir = camera.forward;
    return;
  }
  float tan_y = std::tan(camera.yfov * 0.5f);
  Vec3 d = Add(camera.forward,
               Add(Mul(camera.right, px * aspect * tan_y),
                   Mul(camera.up, py * tan_y)));
  *org = camera.origin;
  *dir = Normalize(d);
}

tinyusdz::Image RenderImage(lrt_tri_scene *scene, const DirectScene *direct,
                            const std::vector<TriInfo> &tris,
                            const LightCache &lights, const IblCache *ibl,
                            const CameraFrame &camera, const Options &opt,
                            int height,
                            const std::vector<Texture> *textures = nullptr,
                            const std::vector<float> *tri_uvs = nullptr,
                            const lrt_tlas *tlas = nullptr,
                            const std::vector<Blas> *blas = nullptr,
                            const std::vector<InstanceRT> *instances = nullptr,
                            const std::vector<float> *tri_colors = nullptr,
                            const std::vector<float> *tri_normals = nullptr) {
  tinyusdz::Image img;
  img.width = opt.width;
  img.height = height;
  img.channels = 4;
  img.bpp = 8;
  img.format = tinyusdz::Image::PixelFormat::UInt;
  img.data.resize(size_t(img.width) * size_t(img.height) * 4);
  float aspect = float(img.width) / float(img.height);
  int spp_side = int(std::ceil(std::sqrt(float(opt.samples))));
  int spp = spp_side * spp_side;

  // Scanlines are independent and write to disjoint pixel ranges, so render
  // them in parallel. Result is deterministic regardless of thread scheduling.
  auto render_rows = [&](int y_begin, int y_end) {
    for (int y = y_begin; y < y_end; y++) {
      for (int x = 0; x < img.width; x++) {
        Vec3 color{0.0f, 0.0f, 0.0f};
        for (int sy = 0; sy < spp_side; sy++) {
          for (int sx = 0; sx < spp_side; sx++) {
            float fx = (float(x) + (float(sx) + 0.5f) / float(spp_side)) /
                       float(img.width);
            float fy = (float(y) + (float(sy) + 0.5f) / float(spp_side)) /
                       float(img.height);
            Vec3 org;
            Vec3 dir;
            MakeRay(camera, aspect, fx, fy, &org, &dir);
            // One-pixel ray differentials for texture footprint / mip LOD
            // (origin + dir cover both pinhole and ortho cameras).
            RayDiff rd;
            MakeRay(camera, aspect, fx + 1.0f / float(img.width), fy, &rd.ox,
                    &rd.dx);
            MakeRay(camera, aspect, fx, fy + 1.0f / float(img.height), &rd.oy,
                    &rd.dy);
            rd.valid = true;
            color = Add(color, Shade(scene, direct, tris, lights, ibl, camera,
                                     opt, org, dir, textures, tri_uvs, tlas,
                                     blas, instances, rd, 0, tri_colors,
                                     tri_normals));
          }
        }
        color = Mul(color, 1.0f / float(spp));
        size_t ofs = (size_t(y) * size_t(img.width) + size_t(x)) * 4;
        img.data[ofs + 0] = ToSRGB8(color.x);
        img.data[ofs + 1] = ToSRGB8(color.y);
        img.data[ofs + 2] = ToSRGB8(color.z);
        img.data[ofs + 3] = 255;
      }
    }
  };
  unsigned nthreads =
      std::min<unsigned>(WorkerThreadCount(opt.threads),
                         img.height > 0 ? unsigned(img.height) : 1u);
  if (nthreads <= 1) {
    render_rows(0, img.height);
  } else {
    std::vector<std::thread> pool;
    pool.reserve(nthreads);
    const int rows_per = (img.height + int(nthreads) - 1) / int(nthreads);
    for (unsigned t = 0; t < nthreads; ++t) {
      const int y0 = int(t) * rows_per;
      const int y1 = std::min(img.height, y0 + rows_per);
      if (y0 >= y1) break;
      pool.emplace_back(render_rows, y0, y1);
    }
    for (std::thread &th : pool) th.join();
  }
  return img;
}

bool LoadProgress(float progress, void *) {
  int percent = int(std::round(ClampFloat(progress, 0.0f, 1.0f) * 100.0f));
  std::cerr << "\rload: " << percent << "%" << std::flush;
  if (progress >= 1.0f) std::cerr << "\n";
  return true;
}

// ===========================================================================
// `next` lazy-loader RT preview backend (default for USDC inputs).
//
// Loads the USDC with the experimental `next` reader (fast, low-memory, lazy
// arrays) and streams triangles using tydra_next's bit-exact world transforms.
// Produces the byte-identical triangle stream of the legacy path (validated:
// matching per-purpose triangle counts on large scenes). Falls back to the
// legacy eager loader for non-USDC inputs or when -legacyLoad is given.
// ===========================================================================

matrix4d Mat4FromArray(const double d[16]) {
  matrix4d m;
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) m.m[i][j] = d[i * 4 + j];
  return m;
}

// Read an array attribute, decoding lazily-stored arrays into a throwaway temp
// (materialized_copy) so the `next` stage's Value stays lazy. This keeps the
// big geometry arrays (points/indices/normals) from being permanently
// materialized into the stage as we stream the scene, bounding peak memory.
// `time` is NaN for the default value, or a frame time for animated (time-
// sampled) arrays (held to the nearest authored sample). Decoding goes through
// materialized_copy so the stage's Value stays lazy.
std::vector<float> ReadFloatArrayLazy(const tinyusdz::next::UsdPrim &prim,
                                      const char *name, double time) {
  const tinyusdz::next::Value *v = std::isnan(time)
                                       ? prim.GetPropertyValue(name)
                                       : prim.GetValueAtTime(name, time);
  if (!v) return {};
  if (v->is_lazy()) {
    tinyusdz::next::Value tmp = v->materialized_copy();
    if (const std::vector<float> *a = tmp.as_float_array()) return *a;
    return {};
  }
  if (const std::vector<float> *a = v->as_float_array()) return *a;
  return {};
}

std::vector<int32_t> ReadIntArrayLazy(const tinyusdz::next::UsdPrim &prim,
                                      const char *name, double time) {
  const tinyusdz::next::Value *v = std::isnan(time)
                                       ? prim.GetPropertyValue(name)
                                       : prim.GetValueAtTime(name, time);
  if (!v) return {};
  if (v->is_lazy()) {
    tinyusdz::next::Value tmp = v->materialized_copy();
    if (const std::vector<int32_t> *a = tmp.as_int_array()) return *a;
    return {};
  }
  if (const std::vector<int32_t> *a = v->as_int_array()) return *a;
  return {};
}

std::vector<int64_t> ReadInt64ArrayLazy(const tinyusdz::next::UsdPrim &prim,
                                        const char *name, double time) {
  const tinyusdz::next::Value *v = std::isnan(time)
                                       ? prim.GetPropertyValue(name)
                                       : prim.GetValueAtTime(name, time);
  if (!v) return {};
  if (v->is_lazy()) {
    tinyusdz::next::Value tmp = v->materialized_copy();
    if (const std::vector<int64_t> *a = tmp.as_int64_array()) return *a;
    return {};
  }
  if (const std::vector<int64_t> *a = v->as_int64_array()) return *a;
  return {};
}

// Templated on the output buffer type so the flat path can stream into plain
// std::vector (ctx.tris) while the instanced path streams into the budget-tracked
// Blas FloatVec/TriVec (so the big instanced geometry is capped/pooled).
template <class FVec, class TVec>
void AddRTPreviewMeshNext(const tinyusdz::next::UsdPrim &prim,
                          const matrix4d &world, tinyusdz::Purpose purpose,
                          uint32_t purpose_mask, double time,
                          const Vec3 &base_color, int32_t tex_id,
                          int32_t normal_tex_id, float roughness, float metallic,
                          const ScalarTex &rough_tex, const ScalarTex &metal_tex,
                          const Vec3 &emission, int32_t emission_tex_id,
                          float occlusion, const ScalarTex &occ_tex,
                          const UvXform &uv_xform, bool want_uvs,
                          FVec *vertices, TVec *tris, FVec *tri_uvs,
                          Bounds *bounds, RTPreviewStats *stats,
                          bool purpose_cull = false,
                          TriMat *out_job_mat = nullptr, float opacity = 1.0f,
                          bool want_colors = false, FVec *tri_colors = nullptr,
                          bool want_normals = false, FVec *tri_normals = nullptr) {
  // When the output is the slim TriStore (instanced BLAS), the per-mesh material
  // is emitted once into out_job_mat and each triangle stores only its mat_id.
  if (out_job_mat) {
    out_job_mat->base_color = base_color;
    out_job_mat->emission = emission;
    out_job_mat->roughness = roughness;
    out_job_mat->metallic = metallic;
    out_job_mat->tex_id = tex_id;
    out_job_mat->normal_tex_id = normal_tex_id;
    out_job_mat->rough_tex_id = rough_tex.id;
    out_job_mat->metal_tex_id = metal_tex.id;
    out_job_mat->emission_tex_id = emission_tex_id;
    out_job_mat->occ_tex_id = occ_tex.id;
    out_job_mat->occlusion = occlusion;
    out_job_mat->opacity = opacity;
    out_job_mat->rough_ch = rough_tex.ch;
    out_job_mat->metal_ch = metal_tex.ch;
    out_job_mat->occ_ch = occ_tex.ch;
  }
  // Read geometry without permanently materializing it into the stage.
  const std::vector<float> points = ReadFloatArrayLazy(prim, "points", time);
  const std::vector<int32_t> counts =
      ReadIntArrayLazy(prim, "faceVertexCounts", time);
  const std::vector<int32_t> indices =
      ReadIntArrayLazy(prim, "faceVertexIndices", time);
  if (points.empty() || counts.empty() || indices.empty()) {
    stats->skipped_meshes++;
    return;
  }
  const size_t npts = points.size() / 3;

  // UV (primvars:st) for the diffuse texture. Stored flat [u0,v0,u1,v1,...].
  // Interpolation is inferred from sizes: per-point ("vertex"/"varying") when
  // the UV count matches the point count, otherwise per-face-vertex
  // ("faceVarying"). primvars:st:indices indirection is honored when present.
  std::vector<float> st;
  std::vector<int32_t> st_indices;
  bool st_facevarying = false;
  bool have_st = false;
  if (want_uvs && (tex_id >= 0 || normal_tex_id >= 0 || rough_tex.id >= 0 ||
                   metal_tex.id >= 0 || emission_tex_id >= 0 ||
                   occ_tex.id >= 0)) {
    st = ReadFloatArrayLazy(prim, "primvars:st", time);
    if (st.empty()) st = ReadFloatArrayLazy(prim, "primvars:UVMap", time);
    if (!st.empty()) {
      st_indices = ReadIntArrayLazy(prim, "primvars:st:indices", time);
      const size_t uv_count =
          st_indices.empty() ? (st.size() / 2) : st_indices.size();
      // Per-point if it matches the points; per-face-vertex if it matches the
      // face-vertex stream (== faceVertexIndices length).
      if (uv_count == npts) {
        st_facevarying = false;
        have_st = true;
      } else if (uv_count == indices.size()) {
        st_facevarying = true;
        have_st = true;
      }
    }
  }
  // Fetch the UV for face-vertex slot `fv` (position in the face-vertex stream)
  // whose underlying point index is `pi`. Returns {u,v}.
  auto uv_at = [&](size_t fv, int32_t pi) -> std::pair<float, float> {
    if (!have_st) return {0.0f, 0.0f};
    size_t s = st_facevarying ? fv : size_t(pi);
    if (!st_indices.empty()) {
      if (s >= st_indices.size()) return {0.0f, 0.0f};
      int32_t idx = st_indices[s];
      if (idx < 0) return {0.0f, 0.0f};
      s = size_t(idx);
    }
    if (s * 2 + 1 >= st.size()) return {0.0f, 0.0f};
    return {st[s * 2 + 0], st[s * 2 + 1]};
  };

  // Per-corner displayColor/displayOpacity (RGBA), when the scene has any
  // non-constant display primvar. Interpolation is inferred from array size:
  // vertex (== npoints), faceVarying (== face-vertex count), uniform (== nfaces);
  // a 1-element array (or absent) falls back to the constant base_color/opacity.
  std::vector<float> dcol, dopac;
  int dc_mode = 0, do_mode = 0;  // 0=const, 1=vertex, 2=faceVarying, 3=uniform
  if (want_colors) {
    dcol = ReadFloatArrayLazy(prim, "primvars:displayColor", time);
    const size_t nc = dcol.size() / 3;
    if (nc == npts) dc_mode = 1;
    else if (nc == indices.size()) dc_mode = 2;
    else if (nc == counts.size()) dc_mode = 3;
    dopac = ReadFloatArrayLazy(prim, "primvars:displayOpacity", time);
    const size_t no = dopac.size();
    if (no == npts) do_mode = 1;
    else if (no == indices.size()) do_mode = 2;
    else if (no == counts.size()) do_mode = 3;
  }
  // RGBA for face-vertex slot `fv` (point index `pi`, face `face`).
  auto col_at = [&](size_t fv, int32_t pi, size_t face, float out[4]) {
    out[0] = base_color.x; out[1] = base_color.y; out[2] = base_color.z;
    out[3] = opacity;
    if (dc_mode) {
      size_t ci = dc_mode == 1 ? size_t(pi) : dc_mode == 2 ? fv : face;
      if (ci * 3 + 2 < dcol.size()) {
        out[0] = dcol[ci * 3 + 0]; out[1] = dcol[ci * 3 + 1];
        out[2] = dcol[ci * 3 + 2];
      }
    }
    if (do_mode) {
      size_t oi = do_mode == 1 ? size_t(pi) : do_mode == 2 ? fv : face;
      if (oi < dopac.size()) out[3] = std::min(1.0f, std::max(0.0f, dopac[oi]));
    }
  };

  // Authored normals for smooth shading (`-smooth`). Stored per corner in the
  // job's frame (world for flat, prototype-local for instanced — `world` is
  // identity there), transformed at hit. Falls back to the geometric normal when
  // absent. Interpolation inferred from size (vertex / faceVarying).
  std::vector<float> nrm;
  int nrm_mode = 0;  // 0=none, 1=vertex, 2=faceVarying
  if (want_normals) {
    nrm = ReadFloatArrayLazy(prim, "normals", time);
    if (nrm.empty()) nrm = ReadFloatArrayLazy(prim, "primvars:normals", time);
    const size_t nn = nrm.size() / 3;
    if (nn == npts) nrm_mode = 1;
    else if (nn == indices.size()) nrm_mode = 2;
  }
  // World-space normal for face-vertex slot `fv` (point index `pi`); `geom` is the
  // face's geometric normal (already in the job frame) used as the fallback.
  auto norm_at = [&](size_t fv, int32_t pi, const Vec3 &geom) -> Vec3 {
    if (!nrm_mode) return geom;
    size_t ni = nrm_mode == 1 ? size_t(pi) : fv;
    if (ni * 3 + 2 >= nrm.size()) return geom;
    Vec3 ln{nrm[ni * 3 + 0], nrm[ni * 3 + 1], nrm[ni * 3 + 2]};
    Vec3 wn = TransformVector(world, ln);  // job-frame (world for flat path)
    float len = Length(wn);
    return len > 1.0e-12f ? Mul(wn, 1.0f / len) : geom;
  };

  // Reserve from the exact triangle-fan estimate. StreamMeshJobs gives each mesh
  // its OWN thread-local buffers (one mesh's worth), so a single up-front reserve
  // replaces the per-triangle geometric reallocations (the TriInfo realloc churn
  // perf flagged). (This is safe ONLY because the buffers are per-job now; the
  // old shared-buffer design would have reallocated multi-GB on every mesh.)
  size_t tri_estimate = 0;
  for (int32_t c : counts) {
    if (c >= 3) tri_estimate += size_t(c - 2);
  }
  if (tri_estimate) {
    vertices->reserve(vertices->size() + tri_estimate * 9);
    tris->reserve(tris->size() + tri_estimate);
    if (want_uvs) tri_uvs->reserve(tri_uvs->size() + tri_estimate * 6);
    if (want_colors) tri_colors->reserve(tri_colors->size() + tri_estimate * 12);
    if (want_normals) tri_normals->reserve(tri_normals->size() + tri_estimate * 9);
  }

  size_t cursor = 0;
  size_t face_idx = 0;
  for (int32_t c : counts) {
    const size_t face = face_idx++;
    if (c < 3 || cursor + size_t(c) > indices.size()) {
      cursor += size_t(std::max<int32_t>(0, c));
      continue;
    }
    for (int32_t k = 1; k + 1 < c; k++) {
      int32_t i0 = indices[cursor + 0];
      int32_t i1 = indices[cursor + size_t(k)];
      int32_t i2 = indices[cursor + size_t(k + 1)];
      if (i0 < 0 || i1 < 0 || i2 < 0 || size_t(i0) >= npts ||
          size_t(i1) >= npts || size_t(i2) >= npts) {
        continue;
      }
      Vec3 p0 = TransformPoint(
          world, Vec3{points[3 * i0], points[3 * i0 + 1], points[3 * i0 + 2]});
      Vec3 p1 = TransformPoint(
          world, Vec3{points[3 * i1], points[3 * i1 + 1], points[3 * i1 + 2]});
      Vec3 p2 = TransformPoint(
          world, Vec3{points[3 * i2], points[3 * i2 + 1], points[3 * i2 + 2]});
      Vec3 n = Normalize(Cross(Sub(p1, p0), Sub(p2, p0)));
      if (Length(n) < 1.0e-12f) continue;

      TriInfo tri;
      tri.p0 = p0;
      tri.p1 = p1;
      tri.p2 = p2;
      tri.n = n;
      tri.base_color = base_color;
      tri.tex_id = tex_id;
      tri.normal_tex_id = normal_tex_id;
      tri.roughness = roughness;
      tri.metallic = metallic;
      tri.rough_tex_id = rough_tex.id;
      tri.rough_ch = rough_tex.ch;
      tri.metal_tex_id = metal_tex.id;
      tri.metal_ch = metal_tex.ch;
      tri.emission = emission;
      tri.emission_tex_id = emission_tex_id;
      tri.occlusion = occlusion;
      tri.occ_tex_id = occ_tex.id;
      tri.occ_ch = occ_tex.ch;
      tri.opacity = opacity;
      tri.purpose_bit = PurposeBit(purpose);
      if (tri.purpose_bit == kPurposeRenderBit) {
        stats->purpose_render_triangles++;
      } else if (tri.purpose_bit == kPurposeProxyBit) {
        stats->purpose_proxy_triangles++;
      } else if (tri.purpose_bit == kPurposeGuideBit) {
        stats->purpose_guide_triangles++;
      } else {
        stats->purpose_default_triangles++;
      }
      const bool visible_for_fit = PurposeVisible(tri.purpose_bit, purpose_mask);
      // TLAS mode culls purpose-invisible triangles at build time (closest-hit
      // can't filter per-prim like the flat multi-hit path does).
      if (purpose_cull && !visible_for_fit) continue;
      vertices->insert(vertices->end(),
                       {p0.x, p0.y, p0.z, p1.x, p1.y, p1.z, p2.x, p2.y, p2.z});
      if constexpr (std::is_same<typename TVec::value_type, TriStore>::value) {
        // Slim store: only mat_id (positions are in `vertices` above; the global
        // mat_id is assigned by StreamMeshJobs when it concatenates jobs).
        TriStore ts;
        ts.mat_id = 0;
        tris->push_back(ts);
      } else {
        tris->push_back(tri);
      }
      if (want_uvs) {
        // Keep tri_uvs parallel to tris (6 floats/tri). uv0=vert0, uv1=vert(k),
        // uv2=vert(k+1) in fan order, matching i0/i1/i2 above.
        auto uv0 = uv_at(cursor + 0, i0);
        auto uv1 = uv_at(cursor + size_t(k), i1);
        auto uv2 = uv_at(cursor + size_t(k + 1), i2);
        // Bake the UsdTransform2d (if any) into the stored UVs.
        uv_xform.apply(&uv0.first, &uv0.second);
        uv_xform.apply(&uv1.first, &uv1.second);
        uv_xform.apply(&uv2.first, &uv2.second);
        tri_uvs->insert(tri_uvs->end(), {uv0.first, uv0.second, uv1.first,
                                         uv1.second, uv2.first, uv2.second});
      }
      if (want_colors) {
        // Per-corner RGBA parallel to tris (12 floats/tri), fan order matching
        // i0/i1/i2. Constant meshes replicate base_color/opacity at all corners.
        float c0[4], c1[4], c2[4];
        col_at(cursor + 0, i0, face, c0);
        col_at(cursor + size_t(k), i1, face, c1);
        col_at(cursor + size_t(k + 1), i2, face, c2);
        tri_colors->insert(tri_colors->end(),
                           {c0[0], c0[1], c0[2], c0[3], c1[0], c1[1], c1[2],
                            c1[3], c2[0], c2[1], c2[2], c2[3]});
      }
      if (want_normals) {
        // Per-corner normals (9 floats/tri), fan order matching i0/i1/i2.
        Vec3 n0 = norm_at(cursor + 0, i0, n);
        Vec3 n1 = norm_at(cursor + size_t(k), i1, n);
        Vec3 n2 = norm_at(cursor + size_t(k + 1), i2, n);
        tri_normals->insert(tri_normals->end(),
                            {n0.x, n0.y, n0.z, n1.x, n1.y, n1.z, n2.x, n2.y,
                             n2.z});
      }
      stats->triangles++;
      if (visible_for_fit) {
        Expand(bounds, p0);
        Expand(bounds, p1);
        Expand(bounds, p2);
      }
    }
    cursor += size_t(c);
  }
}

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
  float opacity{1.0f};                   // resolved primvars:displayOpacity
  bool vertex_color{false};              // displayColor/Opacity is per-vertex
};

// ---------------------------------------------------------------------------
// Material/texture resolution for the `next` render path.
//
// Resolves a Mesh's bound material (material:binding -> Material ->
// outputs:surface -> UsdPreviewSurface, including MaterialX's
// ND_UsdPreviewSurface_surfaceshader) into a flat diffuse base color and/or a
// diffuse (base color) texture sampled through inputs:diffuseColor ->
// UsdUVTexture(inputs:file). Done serially before the parallel triangle stream.
// ---------------------------------------------------------------------------

// Directory portion of a path (without trailing slash), or "" if none.
std::string DirName(const std::string &path) {
  size_t slash = path.find_last_of("/\\");
  if (slash == std::string::npos) return "";
  return path.substr(0, slash);
}

// Loaded RGB(A) textures + a key->index cache so each (file, wrap, colorspace)
// loads once. `usdz`, when set, is searched first so textures packed inside a
// .usdz archive resolve without touching the filesystem.
struct TextureCache {
  std::vector<Texture> *textures{nullptr};
  std::unordered_map<std::string, int32_t> by_key;
  std::string base_dir;  // directory of the input file, for relative paths
  const tinyusdz::next::USDZReader *usdz{nullptr};
};

// True if a usdz entry name refers to the same asset as `asset_path` (which may
// be relative, "./tex.png", or "tex.png"). Matches on full path or basename.
bool UsdzEntryMatches(const std::string &entry, const std::string &asset) {
  std::string a = asset;
  if (a.rfind("./", 0) == 0) a = a.substr(2);
  if (entry == a) return true;
  if (entry.size() > a.size() &&
      entry.compare(entry.size() - a.size(), a.size(), a) == 0 &&
      entry[entry.size() - a.size() - 1] == '/') {
    return true;
  }
  auto base = [](const std::string &s) {
    size_t p = s.find_last_of('/');
    return p == std::string::npos ? s : s.substr(p + 1);
  };
  return base(entry) == base(a);
}

int32_t LoadTextureCached(TextureCache &tc, const std::string &asset_path,
                          WrapMode ws, WrapMode wt, bool srgb,
                          const Vec3 &scale = Vec3{1.0f, 1.0f, 1.0f},
                          const Vec3 &bias = Vec3{0.0f, 0.0f, 0.0f}) {
  const std::string key = asset_path + "|" + std::to_string(int(ws)) + "," +
                          std::to_string(int(wt)) + (srgb ? "|s" : "|r") + "|" +
                          std::to_string(scale.x) + "," + std::to_string(bias.x);
  auto it = tc.by_key.find(key);
  if (it != tc.by_key.end()) return it->second;

  auto adopt = [&](const tinyusdz::Image &img) -> int32_t {
    if (img.width <= 0 || img.height <= 0 || img.bpp != 8 || img.data.empty()) {
      return -1;
    }
    Texture t;
    t.width = img.width;
    t.height = img.height;
    t.channels = img.channels;
    t.pixels = img.data;
    t.wrap_s = ws;
    t.wrap_t = wt;
    t.srgb = srgb;
    t.scale = scale;
    t.bias = bias;
    t.build_mips();
    int32_t id = int32_t(tc.textures->size());
    tc.textures->push_back(std::move(t));
    return id;
  };

  int32_t id = -1;
  // 1. usdz-embedded.
  if (tc.usdz) {
    for (size_t i = 0; i < tc.usdz->NumEntries(); ++i) {
      if (!UsdzEntryMatches(tc.usdz->EntryName(i), asset_path)) continue;
      auto res = tinyusdz::image::LoadImageFromMemory(
          tc.usdz->EntryData(i), tc.usdz->EntrySize(i), tc.usdz->EntryName(i));
      if (res) id = adopt(res.value().image);
      break;
    }
  }
  // 2. filesystem (anchored to the input dir).
  if (id < 0 && (!asset_path.empty())) {
    std::string path = asset_path;
    if (path[0] != '/' && !tc.base_dir.empty()) path = tc.base_dir + "/" + path;
    auto res = tinyusdz::image::LoadImageFromFile(path);
    if (res) id = adopt(res.value().image);
  }
  if (id < 0) std::cerr << "WARN: failed to load texture: " << asset_path << "\n";
  tc.by_key[key] = id;
  return id;
}

// Follow a connection on `prim` (e.g. "outputs:surface",
// "inputs:diffuseColor") to its target prim, or an invalid prim if unconnected.
tinyusdz::next::UsdPrim ConnectedPrimNext(const tinyusdz::next::Stage &stage,
                                          const tinyusdz::next::UsdPrim &prim,
                                          const std::string &prop) {
  const tinyusdz::next::PrimSpec *spec = prim.GetPrimSpec();
  if (!spec) return tinyusdz::next::UsdPrim();
  const std::vector<tinyusdz::next::Path> *c = spec->connection(prop);
  if (!c || c->empty()) return tinyusdz::next::UsdPrim();
  return stage.GetPrimAtPath((*c)[0].prim_path());
}

WrapMode ParseWrapMode(const std::string &s) {
  if (s == "clamp") return WrapMode::Clamp;
  if (s == "mirror") return WrapMode::Mirror;
  if (s == "black") return WrapMode::Black;
  return WrapMode::Repeat;  // "repeat"/"useMetadata"/default
}

// Resolve a scalar PBR input (inputs:roughness / inputs:metallic) that connects
// to a UsdUVTexture outputs:{r,g,b,a} (e.g. ORM packing). Loads the raw texture
// and records the source channel.
void ResolveScalarTextureNext(const tinyusdz::next::Stage &stage,
                              const tinyusdz::next::UsdPrim &surf,
                              const std::string &input, TextureCache &tc,
                              ScalarTex *out) {
  const tinyusdz::next::PrimSpec *spec = surf.GetPrimSpec();
  if (!spec) return;
  const std::vector<tinyusdz::next::Path> *c = spec->connection(input);
  if (!c || c->empty()) return;
  const tinyusdz::next::Path &target = (*c)[0];
  tinyusdz::next::UsdPrim tex = stage.GetPrimAtPath(target.prim_path());
  if (!tex.IsValid()) return;
  const tinyusdz::next::Value *fv = tex.GetPropertyValue("inputs:file");
  if (!fv) return;
  const std::string *ap = fv->as_asset_path();
  if (!ap) ap = fv->as_string();
  if (!ap || ap->empty()) return;
  WrapMode ws = WrapMode::Repeat, wt = WrapMode::Repeat;
  if (const tinyusdz::next::Value *v = tex.GetPropertyValue("inputs:wrapS"))
    if (const std::string *t = v->as_token()) ws = ParseWrapMode(*t);
  if (const tinyusdz::next::Value *v = tex.GetPropertyValue("inputs:wrapT"))
    if (const std::string *t = v->as_token()) wt = ParseWrapMode(*t);
  int32_t id = LoadTextureCached(tc, *ap, ws, wt, /*srgb=*/false);
  if (id < 0) return;
  out->id = id;
  const std::string prop = target.property_name();  // e.g. "outputs:g"
  if (!prop.empty()) {
    switch (prop.back()) {
      case 'g': out->ch = 1; break;
      case 'b': out->ch = 2; break;
      case 'a': out->ch = 3; break;
      default: out->ch = 0; break;  // r / rgb
    }
  }
}

// If a UsdUVTexture's inputs:st chain runs through a UsdTransform2d, read its
// rotation (deg, CCW) / scale / translation. Otherwise returns identity.
UvXform ResolveUvXform(const tinyusdz::next::Stage &stage,
                       const tinyusdz::next::UsdPrim &uvtex) {
  UvXform x;
  tinyusdz::next::UsdPrim st = ConnectedPrimNext(stage, uvtex, "inputs:st");
  if (!st.IsValid()) return x;
  const tinyusdz::next::Value *idv = st.GetPropertyValue("info:id");
  const std::string *id = idv ? idv->as_token() : nullptr;
  if (!id || *id != "UsdTransform2d") return x;
  float rot = 0.0f, sx = 1.0f, sy = 1.0f, tx = 0.0f, ty = 0.0f;
  if (const tinyusdz::next::Value *v = st.GetPropertyValue("inputs:rotation"))
    if (const float *f = v->as_float()) rot = *f;
  if (const tinyusdz::next::Value *v = st.GetPropertyValue("inputs:scale"))
    if (const float *f = v->as_float2()) { sx = f[0]; sy = f[1]; }
  if (const tinyusdz::next::Value *v = st.GetPropertyValue("inputs:translation"))
    if (const float *f = v->as_float2()) { tx = f[0]; ty = f[1]; }
  if (rot == 0.0f && sx == 1.0f && sy == 1.0f && tx == 0.0f && ty == 0.0f) {
    return x;  // identity
  }
  float rad = rot * 3.14159265358979f / 180.0f;
  x.rc = std::cos(rad);
  x.rs = std::sin(rad);
  x.sx = sx;
  x.sy = sy;
  x.tx = tx;
  x.ty = ty;
  x.identity = false;
  return x;
}

void ResolveMeshMaterialNext(const tinyusdz::next::Stage &stage,
                             const tinyusdz::next::UsdPrim &mesh,
                             TextureCache &tc, Vec3 *base_color, int32_t *tex_id,
                             float *roughness, float *metallic,
                             int32_t *normal_tex_id, UvXform *uv_xform,
                             ScalarTex *rough_tex, ScalarTex *metal_tex,
                             Vec3 *emission, int32_t *emission_tex_id,
                             float *occlusion, ScalarTex *occ_tex,
                             float *opacity = nullptr,
                             bool *vertex_color = nullptr) {
  // Geometry display primvars: the unmaterialed base color/opacity (e.g. ALab
  // geom-only meshes). The first value seeds the constant base; a >1-element
  // array is per-vertex/faceVarying/uniform and sets *vertex_color so the stream
  // stores per-corner colors (see AddRTPreviewMeshNext). A bound material below
  // overrides the constant color.
  if (const tinyusdz::next::Value *dcv =
          mesh.GetPropertyValue("primvars:displayColor")) {
    if (const std::vector<float> *dc = dcv->as_float_array()) {
      if (dc->size() >= 3) *base_color = Vec3{(*dc)[0], (*dc)[1], (*dc)[2]};
      if (vertex_color && dc->size() > 3) *vertex_color = true;
    }
  }
  if (opacity) {
    if (const tinyusdz::next::Value *dov =
            mesh.GetPropertyValue("primvars:displayOpacity")) {
      if (const std::vector<float> *od = dov->as_float_array()) {
        if (!od->empty()) *opacity = std::min(1.0f, std::max(0.0f, (*od)[0]));
        if (vertex_color && od->size() > 1) *vertex_color = true;
      }
    }
  }
  const std::vector<tinyusdz::next::Path> *bind =
      mesh.GetRelationship("material:binding");
  if (!bind || bind->empty()) return;
  tinyusdz::next::UsdPrim mat = stage.GetPrimAtPath((*bind)[0]);
  if (!mat.IsValid()) return;
  tinyusdz::next::UsdPrim surf = ConnectedPrimNext(stage, mat, "outputs:surface");
  if (!surf.IsValid()) {
    surf = ConnectedPrimNext(stage, mat, "outputs:mtlx:surface");
  }
  if (!surf.IsValid()) return;

  // Scalar PBR params (UsdPreviewSurface inputs:roughness / inputs:metallic).
  if (const tinyusdz::next::Value *r = surf.GetPropertyValue("inputs:roughness")) {
    if (const float *f = r->as_float()) *roughness = std::min(1.0f, std::max(0.0f, *f));
  }
  if (const tinyusdz::next::Value *m = surf.GetPropertyValue("inputs:metallic")) {
    if (const float *f = m->as_float()) *metallic = std::min(1.0f, std::max(0.0f, *f));
  }
  // Roughness/metallic textures (channel-aware; ORM packing).
  if (rough_tex)
    ResolveScalarTextureNext(stage, surf, "inputs:roughness", tc, rough_tex);
  if (metal_tex)
    ResolveScalarTextureNext(stage, surf, "inputs:metallic", tc, metal_tex);

  // Occlusion (AO) scalar + optional texture.
  if (const tinyusdz::next::Value *o = surf.GetPropertyValue("inputs:occlusion"))
    if (const float *f = o->as_float())
      if (occlusion) *occlusion = std::min(1.0f, std::max(0.0f, *f));
  if (occ_tex)
    ResolveScalarTextureNext(stage, surf, "inputs:occlusion", tc, occ_tex);

  // Emissive color: constant + optional UsdUVTexture (sRGB color).
  if (const tinyusdz::next::Value *e =
          surf.GetPropertyValue("inputs:emissiveColor"))
    if (const float *f = e->as_float3())
      if (emission) *emission = Vec3{f[0], f[1], f[2]};
  if (emission_tex_id) {
    tinyusdz::next::UsdPrim etex =
        ConnectedPrimNext(stage, surf, "inputs:emissiveColor");
    if (etex.IsValid()) {
      if (const tinyusdz::next::Value *fv =
              etex.GetPropertyValue("inputs:file")) {
        const std::string *ap = fv->as_asset_path();
        if (!ap) ap = fv->as_string();
        if (ap && !ap->empty()) {
          WrapMode ws = WrapMode::Repeat, wt = WrapMode::Repeat;
          if (const tinyusdz::next::Value *v = etex.GetPropertyValue("inputs:wrapS"))
            if (const std::string *t = v->as_token()) ws = ParseWrapMode(*t);
          if (const tinyusdz::next::Value *v = etex.GetPropertyValue("inputs:wrapT"))
            if (const std::string *t = v->as_token()) wt = ParseWrapMode(*t);
          int32_t id = LoadTextureCached(tc, *ap, ws, wt, /*srgb=*/true);
          if (id >= 0) {
            *emission_tex_id = id;
            if (emission) *emission = Vec3{1.0f, 1.0f, 1.0f};  // texture is the tint
          }
        }
      }
    }
  }

  // Constant diffuse color (also the texture's fallback tint).
  if (const tinyusdz::next::Value *dc =
          surf.GetPropertyValue("inputs:diffuseColor")) {
    if (const float *f = dc->as_float3()) {
      *base_color = Vec3{f[0], f[1], f[2]};
    }
  }
  // Diffuse texture: inputs:diffuseColor -> UsdUVTexture(inputs:file), honoring
  // its wrapS/wrapT and sourceColorSpace (sRGB by default for color).
  tinyusdz::next::UsdPrim tex =
      ConnectedPrimNext(stage, surf, "inputs:diffuseColor");
  if (tex.IsValid()) {
    if (const tinyusdz::next::Value *fv = tex.GetPropertyValue("inputs:file")) {
      const std::string *ap = fv->as_asset_path();
      if (!ap) ap = fv->as_string();
      if (ap && !ap->empty()) {
        WrapMode ws = WrapMode::Repeat, wt = WrapMode::Repeat;
        if (const tinyusdz::next::Value *v = tex.GetPropertyValue("inputs:wrapS"))
          if (const std::string *t = v->as_token()) ws = ParseWrapMode(*t);
        if (const tinyusdz::next::Value *v = tex.GetPropertyValue("inputs:wrapT"))
          if (const std::string *t = v->as_token()) wt = ParseWrapMode(*t);
        bool srgb = true;
        if (const tinyusdz::next::Value *v =
                tex.GetPropertyValue("inputs:sourceColorSpace")) {
          if (const std::string *t = v->as_token()) srgb = (*t != "raw");
        }
        // inputs:scale/bias tint the sampled color (default identity).
        Vec3 sc{1.0f, 1.0f, 1.0f}, bi{0.0f, 0.0f, 0.0f};
        if (const tinyusdz::next::Value *v = tex.GetPropertyValue("inputs:scale"))
          if (const float *f = v->as_float3()) sc = Vec3{f[0], f[1], f[2]};
        if (const tinyusdz::next::Value *v = tex.GetPropertyValue("inputs:bias"))
          if (const float *f = v->as_float3()) bi = Vec3{f[0], f[1], f[2]};
        int32_t id = LoadTextureCached(tc, *ap, ws, wt, srgb, sc, bi);
        if (id >= 0) {
          *tex_id = id;
          // A textured surface tints by the texture, not the (often unauthored)
          // diffuseColor constant. Use white so sampling shows true texels.
          *base_color = Vec3{1.0f, 1.0f, 1.0f};
          if (uv_xform) *uv_xform = ResolveUvXform(stage, tex);
        }
      }
    }
  }

  // Tangent-space normal map: inputs:normal -> UsdUVTexture(inputs:file). Always
  // raw (non-sRGB); scale/bias default to the UsdPreviewSurface convention
  // (2,-1) that unpacks a [0,1] texel to a [-1,1] tangent-space normal.
  tinyusdz::next::UsdPrim ntex = ConnectedPrimNext(stage, surf, "inputs:normal");
  if (ntex.IsValid()) {
    if (const tinyusdz::next::Value *fv = ntex.GetPropertyValue("inputs:file")) {
      const std::string *ap = fv->as_asset_path();
      if (!ap) ap = fv->as_string();
      if (ap && !ap->empty()) {
        WrapMode ws = WrapMode::Repeat, wt = WrapMode::Repeat;
        if (const tinyusdz::next::Value *v = ntex.GetPropertyValue("inputs:wrapS"))
          if (const std::string *t = v->as_token()) ws = ParseWrapMode(*t);
        if (const tinyusdz::next::Value *v = ntex.GetPropertyValue("inputs:wrapT"))
          if (const std::string *t = v->as_token()) wt = ParseWrapMode(*t);
        Vec3 scale{2.0f, 2.0f, 2.0f}, bias{-1.0f, -1.0f, -1.0f};
        if (const tinyusdz::next::Value *v = ntex.GetPropertyValue("inputs:scale"))
          if (const float *f = v->as_float3()) scale = Vec3{f[0], f[1], f[2]};
        if (const tinyusdz::next::Value *v = ntex.GetPropertyValue("inputs:bias"))
          if (const float *f = v->as_float3()) bias = Vec3{f[0], f[1], f[2]};
        int32_t id =
            LoadTextureCached(tc, *ap, ws, wt, /*srgb=*/false, scale, bias);
        if (id >= 0) {
          *normal_tex_id = id;
          if (uv_xform && uv_xform->identity)
            *uv_xform = ResolveUvXform(stage, ntex);
        }
      }
    }
  }
}

// The full resolved-material result for one bound material (everything
// ResolveMeshMaterialNext writes). Memoized by bound-material path so a scene
// with many meshes sharing few materials (e.g. Island's 605k coral meshes over a
// handful of coral materials) resolves each material — and its shader-graph walk
// + texture lookups — exactly once instead of per mesh.
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
  bool vertex_color{false};
};

// Resolve a mesh job's material with per-material memoization. The result is a
// pure function of the bound material (+ shared texture cache), so a cache hit
// skips the shader-graph walk entirely. Unbound meshes keep MeshJobNext defaults.
void ResolveMeshMaterialCached(
    const tinyusdz::next::Stage &stage, const tinyusdz::next::UsdPrim &mesh,
    TextureCache &tc, std::unordered_map<std::string, ResolvedMat> &cache,
    MeshJobNext *job) {
  const std::vector<tinyusdz::next::Path> *bind =
      mesh.GetRelationship("material:binding");
  const std::string key =
      (bind && !bind->empty()) ? (*bind)[0].str() : std::string();
  if (!key.empty()) {
    auto it = cache.find(key);
    if (it != cache.end()) {
      const ResolvedMat &r = it->second;
      job->base_color = r.base_color;
      job->tex_id = r.tex_id;
      job->roughness = r.roughness;
      job->metallic = r.metallic;
      job->normal_tex_id = r.normal_tex_id;
      job->uv_xform = r.uv_xform;
      job->rough_tex = r.rough_tex;
      job->metal_tex = r.metal_tex;
      job->emission = r.emission;
      job->emission_tex_id = r.emission_tex_id;
      job->occlusion = r.occlusion;
      job->occ_tex = r.occ_tex;
      job->opacity = r.opacity;
      job->vertex_color = r.vertex_color;
      return;
    }
  }
  ResolveMeshMaterialNext(stage, mesh, tc, &job->base_color, &job->tex_id,
                          &job->roughness, &job->metallic, &job->normal_tex_id,
                          &job->uv_xform, &job->rough_tex, &job->metal_tex,
                          &job->emission, &job->emission_tex_id, &job->occlusion,
                          &job->occ_tex, &job->opacity, &job->vertex_color);
  if (!key.empty()) {
    ResolvedMat r;
    r.base_color = job->base_color;
    r.tex_id = job->tex_id;
    r.roughness = job->roughness;
    r.metallic = job->metallic;
    r.normal_tex_id = job->normal_tex_id;
    r.uv_xform = job->uv_xform;
    r.rough_tex = job->rough_tex;
    r.metal_tex = job->metal_tex;
    r.emission = job->emission;
    r.emission_tex_id = job->emission_tex_id;
    r.occlusion = job->occlusion;
    r.occ_tex = job->occ_tex;
    r.opacity = job->opacity;
    r.vertex_color = job->vertex_color;
    cache.emplace(key, r);
  }
}

// True when `path` is one of the mask paths or a descendant of one. An empty
// mask matches everything.
bool PathMatchesMask(const std::string &path,
                     const std::vector<std::string> &mask) {
  if (mask.empty()) return true;
  for (const std::string &m : mask) {
    if (path == m) return true;
    if (path.size() > m.size() && path.compare(0, m.size(), m) == 0 &&
        path[m.size()] == '/') {
      return true;
    }
  }
  return false;
}

// True if any of the prim's authored xform ops are time-sampled (so its local
// transform varies with time).
bool PrimHasAnimatedXform(const tinyusdz::next::UsdPrim &prim) {
  const tinyusdz::next::Value *orderv = prim.GetPropertyValue("xformOpOrder");
  const std::vector<std::string> *order =
      orderv ? orderv->as_token_array() : nullptr;
  if (!order) return false;
  for (const std::string &raw : *order) {
    std::string op = raw;
    if (op.rfind("!invert!", 0) == 0) op = op.substr(8);
    if (op == "!resetXformStack!") continue;
    if (prim.HasTimeSamples(op)) return true;
  }
  return false;
}

// True if the mesh's own geometry (points/topology) is time-sampled.
bool MeshHasAnimatedGeom(const tinyusdz::next::UsdPrim &prim) {
  return prim.HasTimeSamples("points") ||
         prim.HasTimeSamples("faceVertexIndices") ||
         prim.HasTimeSamples("faceVertexCounts");
}

// True if the subtree contains a rendered (masked) Mesh whose world-space
// geometry varies with time: either the mesh's own points/topology are
// time-sampled, or some xform op on the path (this prim or an ancestor) is.
// Cameras and non-rendered prims are ignored, so camera-only animation does not
// flag the geometry as dynamic (the BVH can then be reused across frames).
bool SubtreeGeometryAnimated(const tinyusdz::next::UsdPrim &prim,
                             const std::vector<std::string> &mask,
                             bool ancestor_xform_animated) {
  const bool xform_anim =
      ancestor_xform_animated || PrimHasAnimatedXform(prim);
  if (prim.GetTypeName() == "Mesh" &&
      PathMatchesMask(prim.GetPath().str(), mask)) {
    if (xform_anim || MeshHasAnimatedGeom(prim)) return true;
  }
  for (const tinyusdz::next::UsdPrim &child : prim.GetChildren()) {
    if (SubtreeGeometryAnimated(child, mask, xform_anim)) return true;
  }
  return false;
}

bool SceneGeometryAnimated(const tinyusdz::next::Stage &stage,
                           const std::vector<std::string> &mask) {
  for (const tinyusdz::next::UsdPrim &root : stage.GetRootPrims()) {
    if (SubtreeGeometryAnimated(root, mask, false)) return true;
  }
  return false;
}

// Serial walk: resolve parent-dependent world matrices (at `time`) + purpose and
// flatten Mesh prims into jobs. `mask` (if non-empty) restricts emission to
// meshes at/under those prim paths (usdrecord --mask); the full tree is still
// walked so transform chains remain correct.
void CollectRTPreviewMeshesNext(const tinyusdz::next::Stage &stage,
                                const tinyusdz::next::UsdPrim &prim,
                                const matrix4d &parent_world,
                                tinyusdz::Purpose inherited_purpose, double time,
                                const std::vector<std::string> &mask,
                                std::vector<MeshJobNext> *jobs) {
  double dmat[16];
  tinyusdz::tydra::next::ComputeLocalTransform(prim, dmat, time);
  const matrix4d local = Mat4FromArray(dmat);
  const bool reset = tinyusdz::tydra::next::HasResetXformStack(prim);
  const matrix4d world = reset ? local : (local * parent_world);

  tinyusdz::Purpose purpose = inherited_purpose;
  if (const tinyusdz::next::Value *pv = prim.GetPropertyValue("purpose")) {
    if (const std::string *t = pv->as_token()) {
      if (*t == "render") {
        purpose = tinyusdz::Purpose::Render;
      } else if (*t == "proxy") {
        purpose = tinyusdz::Purpose::Proxy;
      } else if (*t == "guide") {
        purpose = tinyusdz::Purpose::Guide;
      }
      // "default"/unknown: keep the inherited purpose.
    }
  }

  if (prim.GetTypeName() == "Mesh" &&
      PathMatchesMask(prim.GetPath().str(), mask)) {
    MeshJobNext job;
    job.prim = prim;
    job.world = world;
    job.purpose = purpose;
    jobs->push_back(std::move(job));
  }
  for (const tinyusdz::next::UsdPrim &child : prim.GetChildren()) {
    CollectRTPreviewMeshesNext(stage, child, world, purpose, time, mask, jobs);
  }
}

// Read a UsdGeomCamera attribute (float, with double fallback).
float ReadCamFloatNext(const tinyusdz::next::UsdPrim &prim, const char *name,
                       float fallback) {
  if (const tinyusdz::next::Value *v = prim.GetPropertyValue(name)) {
    if (const float *f = v->as_float()) return *f;
    if (const double *d = v->as_double()) return float(*d);
  }
  return fallback;
}

// Find a (named) UsdGeomCamera in the next stage and build a CameraFrame plus
// its aperture aspect (horizontal/vertical). An empty query matches the first
// camera. Mirrors CameraFrameFromGeomCamera but on the next stage with
// bit-exact world transforms.
bool FindNextCameraFrameRecursive(const tinyusdz::next::Stage &stage,
                                  const tinyusdz::next::UsdPrim &prim,
                                  const matrix4d &parent_world,
                                  const std::string &query, double time,
                                  CameraFrame *frame, float *aspect) {
  double dmat[16];
  tinyusdz::tydra::next::ComputeLocalTransform(prim, dmat, time);
  const matrix4d local = Mat4FromArray(dmat);
  const bool reset = tinyusdz::tydra::next::HasResetXformStack(prim);
  const matrix4d world = reset ? local : (local * parent_world);

  if (prim.GetTypeName() == "Camera") {
    const std::string path = prim.GetPath().str();
    const std::string name = prim.GetName();
    const bool match =
        query.empty() || name == query || path == query ||
        (path.size() > query.size() &&
         path.compare(path.size() - query.size(), query.size(), query) == 0 &&
         path[path.size() - query.size() - 1] == '/');
    if (match) {
      const float focal = ReadCamFloatNext(prim, "focalLength", 50.0f);
      const float vap = ReadCamFloatNext(prim, "verticalAperture", 15.2908f);
      const float hap = ReadCamFloatNext(prim, "horizontalAperture", 20.955f);
      float znear = 0.1f, zfar = 1.0e6f;
      if (const tinyusdz::next::Value *v = prim.GetPropertyValue("clippingRange")) {
        if (const float *f = v->as_float2()) { znear = f[0]; zfar = f[1]; }
      }
      std::string proj = "perspective";
      if (const tinyusdz::next::Value *v = prim.GetPropertyValue("projection")) {
        if (const std::string *t = v->as_token()) proj = *t;
      }
      frame->origin = Vec3{float(world.m[3][0]), float(world.m[3][1]),
                           float(world.m[3][2])};
      frame->right = Normalize(TransformVector(world, Vec3{1.0f, 0.0f, 0.0f}));
      frame->up = Normalize(TransformVector(world, Vec3{0.0f, 1.0f, 0.0f}));
      frame->forward = Normalize(TransformVector(world, Vec3{0.0f, 0.0f, -1.0f}));
      frame->yfov = 2.0f * std::atan(0.5f * vap / std::max(1.0e-6f, focal));
      frame->xmag = hap;
      frame->ymag = vap;
      frame->znear = std::max(1.0e-5f, znear);
      frame->zfar = std::max(frame->znear, zfar);
      frame->ortho = (proj == "orthographic");
      if (aspect) *aspect = hap / std::max(1.0e-6f, vap);
      return true;
    }
  }
  for (const tinyusdz::next::UsdPrim &child : prim.GetChildren()) {
    if (FindNextCameraFrameRecursive(stage, child, world, query, time, frame,
                                     aspect)) {
      return true;
    }
  }
  return false;
}

bool FindNextCameraFrame(const tinyusdz::next::Stage &stage,
                         const std::string &query, double time,
                         CameraFrame *frame, float *aspect) {
  for (const tinyusdz::next::UsdPrim &root : stage.GetRootPrims()) {
    if (FindNextCameraFrameRecursive(stage, root, matrix4d::identity(), query,
                                     time, frame, aspect)) {
      return true;
    }
  }
  return false;
}

// OpenUSD usdrecord-style auto framing: replicates
// UsdAppUtilsFrameRecorder's default-GfCamera framing (focal 50mm, aperture
// 20.955 x 15.2908 -> horizontal FOV ~23.66 deg, aspect ~1.37). Positions a
// perspective camera on the depth axis so the bbox fits the horizontal FOV,
// front-on for Y-up and rotated for Z-up. Writes the aperture-derived image
// height (width / aspect) to `out_height`.
CameraFrame MakeUsdRecordCamera(const Bounds &bounds, tinyusdz::Axis up_axis,
                                int width, int *out_height) {
  constexpr float kPi = 3.14159265358979323846f;
  const float focal = 50.0f, hap = 20.955f, vap = 15.2908f;
  const float aspect = hap / vap;  // ~1.370
  const float half_hfov = std::atan(0.5f * hap / focal);
  if (out_height) {
    *out_height = std::max(1, int(std::lround(float(width) / aspect)));
  }

  Vec3 center{0.0f, 0.0f, 0.0f};
  Vec3 dim{2.0f, 2.0f, 2.0f};
  if (bounds.valid) {
    center = Mul(Add(bounds.lo, bounds.hi), 0.5f);
    dim = Sub(bounds.hi, bounds.lo);
  }

  CameraFrame frame;
  frame.yfov = 2.0f * std::atan(0.5f * vap / focal);
  frame.ortho = false;

  // Plane corner (half-extents in the focal plane) and the depth half-extent.
  float plane_x, plane_y, depth;
  Vec3 pos_dir, up_vec, fwd;
  if (up_axis == tinyusdz::Axis::Z) {
    plane_x = dim.x * 0.5f; plane_y = dim.z * 0.5f; depth = dim.y * 0.5f;
    pos_dir = Vec3{0.0f, -1.0f, 0.0f};  // back up along -Y
    fwd = Vec3{0.0f, 1.0f, 0.0f};
    up_vec = Vec3{0.0f, 0.0f, 1.0f};
  } else if (up_axis == tinyusdz::Axis::X) {
    plane_x = dim.y * 0.5f; plane_y = dim.z * 0.5f; depth = dim.x * 0.5f;
    pos_dir = Vec3{-1.0f, 0.0f, 0.0f};
    fwd = Vec3{1.0f, 0.0f, 0.0f};
    up_vec = Vec3{0.0f, 0.0f, 1.0f};
  } else {  // Y-up
    plane_x = dim.x * 0.5f; plane_y = dim.y * 0.5f; depth = dim.z * 0.5f;
    pos_dir = Vec3{0.0f, 0.0f, 1.0f};  // back up along +Z (look down -Z)
    fwd = Vec3{0.0f, 0.0f, -1.0f};
    up_vec = Vec3{0.0f, 1.0f, 0.0f};
  }
  const float plane_radius =
      std::sqrt(plane_x * plane_x + plane_y * plane_y);
  float distance = plane_radius / std::max(1.0e-6f, std::tan(half_hfov)) + depth;
  (void)kPi;

  frame.origin = Add(center, Mul(pos_dir, distance));
  frame.forward = Normalize(fwd);
  frame.right = Normalize(Cross(frame.forward, up_vec));
  if (Length(frame.right) < 1.0e-6f) frame.right = Vec3{1.0f, 0.0f, 0.0f};
  frame.up = Normalize(Cross(frame.right, frame.forward));
  const float diag = Length(dim);
  frame.znear = std::max(1.0e-4f, distance - diag);
  frame.zfar = distance + diag * 2.0f;
  return frame;
}

// Persistent render context: the loaded next stage, extracted geometry, and the
// built BVH are kept alive so the camera/render parameters can be changed and
// the scene re-rendered repeatedly without re-parsing or rebuilding the BVH
// (memory-persistent rendering, e.g. animation with a moving camera).
struct RenderContext {
  tinyusdz::next::Stage stage;  // keeps the lazy point/index arrays alive
  // Flat (no-instance) path buffers: default allocator so the shared
  // Shade/RenderImage signatures stay std::vector. The big, OOM-prone instanced
  // geometry lives in the budget-tracked Blas buffers below.
  std::vector<float> vertices;  // packed triangle positions (flat-path BVH input)
  std::vector<TriInfo> tris;
  std::vector<Texture> textures;  // diffuse textures referenced by tris[].tex_id
  std::vector<float> tri_uvs;  // 6 floats/tri (parallel to tris); empty if none
  std::vector<float> tri_colors;  // 12 floats/tri (per-corner RGBA); empty if none
  std::vector<float> tri_normals;  // 9 floats/tri (per-corner normals); empty if none
  Bounds bounds;
  RTPreviewStats stats;
  lrt_tri_scene *scene{nullptr};  // owned flat BVH (no-instance path)
  // Two-level (instanced) BVH path: built when the composed scene has native
  // instances. blas[0] is the base (non-instanced) geometry; blas[1..] are the
  // unique prototypes. instances[] place them; tlas is the top-level BVH.
  std::vector<Blas> blas;
  std::vector<InstanceRT> instances;
  lrt_tlas *tlas{nullptr};
  bool use_tlas{false};
  DirectScene direct;             // empty for the next path
  LightCache lights;              // empty -> camera-headlight fallback
  IblCache ibl;                   // image-based lighting (--env / DomeLight)
  tinyusdz::Axis up_axis{tinyusdz::Axis::Y};
  CameraFrame camera;
  Options opt;  // mutable render parameters (width/height/ambient/bg/...)
  int width{960};
  int height{540};
  // Time at which geometry + transforms are evaluated (NaN = default value).
  double frame_time{std::numeric_limits<double>::quiet_NaN()};
  double load_seconds{0.0}, stream_seconds{0.0}, bvh_seconds{0.0};

  // Free the TLAS before the BLAS scenes it references (blas[] destructs after
  // this body runs).
  ~RenderContext() {
    if (tlas) lrt_tlas_free(tlas);
    if (scene) lrt_tri_scene_free(scene);
  }
  RenderContext() = default;
  RenderContext(const RenderContext &) = delete;
  RenderContext &operator=(const RenderContext &) = delete;
};

// Resolve the camera (named / autoframe / auto-fit) into ctx.camera and the
// image height into ctx.height, from the current ctx.opt + ctx.bounds.
void ResolveCameraNext(RenderContext &ctx) {
  const Options &opt = ctx.opt;
  RenderScene empty_render_scene;
  int height = opt.height;
  if (!opt.camera.empty()) {
    float cam_aspect = 16.0f / 9.0f;
    if (FindNextCameraFrame(ctx.stage, opt.camera, ctx.frame_time, &ctx.camera,
                            &cam_aspect)) {
      if (height <= 0) {
        height = std::max(1, int(std::lround(float(ctx.width) / cam_aspect)));
      }
    } else {
      std::cerr << "WARN: camera not found: " << opt.camera
                << ". Using auto-fit.\n";
      if (height <= 0) height = 540;
      Options auto_opt = opt;
      auto_opt.camera.clear();
      auto_opt.width = ctx.width;
      ctx.camera = MakeCameraFrame(empty_render_scene, auto_opt, ctx.bounds,
                                   height, ctx.up_axis);
    }
  } else if (opt.autoframe) {
    ctx.camera = MakeUsdRecordCamera(ctx.bounds, ctx.up_axis, ctx.width, &height);
  } else {
    if (height <= 0) height = 540;
    Options auto_opt = opt;
    auto_opt.camera.clear();
    auto_opt.width = ctx.width;
    ctx.camera = MakeCameraFrame(empty_render_scene, auto_opt, ctx.bounds,
                                 height, ctx.up_axis);
  }
  ctx.height = height;
}

// Prototype BLAS to build: the holder prim's path + the inherited purpose
// context it was instanced under (part of the dedup key, so instances under a
// guide ancestor get a separate, purpose-culled BLAS).
struct ProtoBuildReq {
  std::string path;
  tinyusdz::Purpose purpose{tinyusdz::Purpose::Default};
  uint32_t blas_id{0};  // index into RenderContext::blas
};

// A curve prim (UsdGeomBasisCurves / NurbsCurves) to ray-trace as hair strands
// in the next path. `world` is the world transform; the linear-strand geometry is
// built into the RenderContext's DirectScene (shared by the flat and TLAS render
// paths) — see BuildNextCurves.
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

bool IsCurvePrimNext(const tinyusdz::next::UsdPrim &prim) {
  const std::string &t = prim.GetTypeName();
  return t == "BasisCurves" || t == "NurbsCurves";
}

// Read a curve prim's `points` into a point3f vector via the lazy float accessor.
std::vector<tinyusdz::value::point3f> ReadCurvePointsNext(
    const tinyusdz::next::UsdPrim &prim, double time) {
  const std::vector<float> pf = ReadFloatArrayLazy(prim, "points", time);
  std::vector<tinyusdz::value::point3f> pts(pf.size() / 3);
  for (size_t i = 0; i < pts.size(); ++i)
    pts[i] = {pf[3 * i + 0], pf[3 * i + 1], pf[3 * i + 2]};
  return pts;
}

// Build the collected curve jobs into the RenderContext's DirectScene as LightRT
// hair-strand scenes (round by default; flat/ribbon when the prim authors
// `normals`), reusing AppendLinearCurveStrands + the same intersectors the
// legacy direct path uses. Curve hits/occlusion are then traced by RenderImage's
// existing DirectScene path regardless of use_tlas. Returns false only on a
// LightRT build failure.
bool BuildNextCurves(RenderContext &ctx, const std::vector<CurveJobNext> &jobs,
                     double time) {
  if (jobs.empty()) return true;
  std::vector<float> round_points, round_radii, flat_points, flat_radii;
  std::vector<uint32_t> round_first, round_count, flat_first, flat_count;
  for (const CurveJobNext &job : jobs) {
    std::vector<tinyusdz::value::point3f> points =
        ReadCurvePointsNext(job.prim, time);
    std::vector<int32_t> counts32 =
        ReadIntArrayLazy(job.prim, "curveVertexCounts", time);
    if (points.empty() || counts32.empty()) continue;
    std::vector<int> counts(counts32.begin(), counts32.end());
    std::vector<float> widths = ReadFloatArrayLazy(job.prim, "widths", time);
    const bool flat = job.prim.GetPropertyValue("normals") != nullptr;
    if (flat) {
      AppendLinearCurveStrands(points, counts, widths, job.world, &flat_points,
                               &flat_radii, &flat_first, &flat_count,
                               &ctx.direct.flat_curve_info, &ctx.bounds);
    } else {
      AppendLinearCurveStrands(points, counts, widths, job.world, &round_points,
                               &round_radii, &round_first, &round_count,
                               &ctx.direct.round_curve_info, &ctx.bounds);
    }
  }

  lrt_tri_build_options build_opts;
  std::memset(&build_opts, 0, sizeof(build_opts));
  build_opts.quality = ctx.opt.quality;
  build_opts.layout = LRT_TRI_LAYOUT_AUTO;
  build_opts.num_threads = WorkerThreadCount(ctx.opt.threads);
  lrt_result lrt_err = LRT_RESULT_OK;
  auto make_strands = [](const std::vector<float> &pts,
                         const std::vector<float> &radii,
                         const std::vector<uint32_t> &first,
                         const std::vector<uint32_t> &count) {
    lrt_hair_strands s;
    std::memset(&s, 0, sizeof(s));
    s.points = pts.data();
    s.radius = radii.data();
    s.strand_first = first.data();
    s.strand_count = count.data();
    s.nstrands = first.size();
    s.npoints = radii.size();
    return s;
  };
  if (!round_first.empty()) {
    lrt_hair_strands s =
        make_strands(round_points, round_radii, round_first, round_count);
    ctx.direct.round_curves.reset(
        lrt_roundcurve_scene_build(&s, &build_opts, &lrt_err));
    if (!ctx.direct.round_curves) {
      std::cerr << "Failed to build LightRT round curve scene.\n";
      return false;
    }
  }
  if (!flat_first.empty()) {
    lrt_hair_strands s =
        make_strands(flat_points, flat_radii, flat_first, flat_count);
    ctx.direct.flat_curves.reset(
        lrt_flatcurve_scene_build(&s, &build_opts, &lrt_err));
    if (!ctx.direct.flat_curves) {
      std::cerr << "Failed to build LightRT flat curve scene.\n";
      return false;
    }
  }
  return true;
}

// Build a UsdGeomPointInstancer per-instance object->world matrix in the
// row-vector convention (p' = p * M): scale, then orient, then translate, all in
// the instancer's local space (USD's instance transform order). `quat_xyzw` is
// the orientation as stored by the next loader (imaginary x,y,z then real w);
// `scale3`/`pos` are per-axis scale and translation.
matrix4d InstanceTRS(const float *pos, const float *quat_xyzw,
                     const float *scale3) {
  tinyusdz::value::quatf q;
  q.imag[0] = quat_xyzw[0];
  q.imag[1] = quat_xyzw[1];
  q.imag[2] = quat_xyzw[2];
  q.real = quat_xyzw[3];
  // 3x3 rotation in the same convention as the rest of the xform stack.
  tinyusdz::value::matrix3d rot = tinyusdz::to_matrix3x3(q);
  // p * S * R with S diagonal scales row i of R by scale[i].
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) rot.m[i][j] *= double(scale3[i]);
  tinyusdz::value::double3 t{double(pos[0]), double(pos[1]), double(pos[2])};
  return tinyusdz::to_matrix(rot, t);  // translation into row 3
}

// Recursively collect curve prims under `prim`, accumulating world transforms in
// the row-vector convention. Used both at scene level and to gather a
// PointInstancer prototype's curves (relative to the prototype root).
void CollectCurvesNextRec(const tinyusdz::next::UsdPrim &prim,
                          const matrix4d &parent_world,
                          tinyusdz::Purpose inherited_purpose, double time,
                          std::vector<CurveJobNext> *out) {
  double dmat[16];
  tinyusdz::tydra::next::ComputeLocalTransform(prim, dmat, time);
  const matrix4d local = Mat4FromArray(dmat);
  const bool reset = tinyusdz::tydra::next::HasResetXformStack(prim);
  const matrix4d world = reset ? local : (local * parent_world);
  tinyusdz::Purpose purpose = inherited_purpose;
  if (const tinyusdz::next::Value *pv = prim.GetPropertyValue("purpose")) {
    if (const std::string *t = pv->as_token()) {
      if (*t == "render") purpose = tinyusdz::Purpose::Render;
      else if (*t == "proxy") purpose = tinyusdz::Purpose::Proxy;
      else if (*t == "guide") purpose = tinyusdz::Purpose::Guide;
    }
  }
  if (IsCurvePrimNext(prim)) {
    CurveJobNext cj;
    cj.prim = prim;
    cj.world = world;
    cj.purpose = purpose;
    out->push_back(std::move(cj));
  }
  for (const tinyusdz::next::UsdPrim &child : prim.GetChildren())
    CollectCurvesNextRec(child, world, purpose, time, out);
}

// Collect a PointInstancer prototype's curves with transforms relative to the
// prototype root (root at identity, replaced by the instance transform).
void CollectProtoCurves(const tinyusdz::next::Stage &stage,
                        const std::string &proto_path,
                        tinyusdz::Purpose start_purpose, double time,
                        std::vector<CurveJobNext> *out) {
  tinyusdz::next::UsdPrim proto = stage.GetPrimAtPath(proto_path);
  if (!proto.IsValid()) return;
  if (IsCurvePrimNext(proto)) {
    CurveJobNext cj;
    cj.prim = proto;
    cj.world = matrix4d::identity();
    cj.purpose = start_purpose;
    out->push_back(std::move(cj));
  }
  for (const tinyusdz::next::UsdPrim &child : proto.GetChildren())
    CollectCurvesNextRec(child, matrix4d::identity(), start_purpose, time, out);
}

// Reserve (deduped by path+purpose) a curve BLAS for a prototype's curves, shared
// by PointInstancer and native-instance placements. Returns its index in
// curve_inst->protos, or -1 if curve_inst is null or the prototype has no curves.
int32_t ReserveCurveProto(const tinyusdz::next::Stage &stage,
                          const std::string &proto_path,
                          tinyusdz::Purpose purpose, double time,
                          CurveProtoCollect *curve_inst) {
  if (!curve_inst) return -1;
  const std::string key =
      proto_path + "\x1f" + std::to_string(PurposeBit(purpose));
  auto it = curve_inst->ids.find(key);
  if (it != curve_inst->ids.end()) return int32_t(it->second);
  std::vector<CurveJobNext> probe;
  CollectProtoCurves(stage, proto_path, purpose, time, &probe);
  if (probe.empty()) return -1;
  const uint32_t idx = uint32_t(curve_inst->protos.size());
  curve_inst->ids[key] = idx;
  curve_inst->protos.push_back({proto_path, purpose, 0});
  return int32_t(idx);
}

// Expand a UsdGeomPointInstancer into TLAS placements: each prototype becomes a
// deduped BLAS (shared with the native-instance pool) and every visible instance
// becomes an InstanceRT placing that BLAS at scale*orient*translate composed with
// the instancer's world transform. Prototype paths come from the `prototypes`
// relationship; they are normally descendants of the instancer, so we resolve
// each target by leaf name among the instancer's children first (robust to
// whether composition re-rooted the authored target paths) and fall back to an
// absolute stage lookup. `invisibleIds` are skipped. The instancer's children
// are the prototypes, so the caller must NOT descend into it. Curve prototypes
// are deduped into a curve BLAS and instanced through the same TLAS as meshes.
void CollectPointInstancer(const tinyusdz::next::Stage &stage,
                           const tinyusdz::next::UsdPrim &instancer,
                           const matrix4d &instancer_world,
                           tinyusdz::Purpose purpose, double time,
                           const std::vector<std::string> &mask,
                           std::vector<InstanceRT> *instances,
                           std::unordered_map<std::string, uint32_t> *proto_ids,
                           std::vector<ProtoBuildReq> *protos,
                           CurveProtoCollect *curve_inst,
                           RTPreviewStats *stats) {
  if (!PathMatchesMask(instancer.GetPath().str(), mask)) return;
  const std::vector<tinyusdz::next::Path> *targets =
      instancer.GetRelationship("prototypes");
  if (!targets || targets->empty()) return;

  // Resolve each prototype target to a live stage prim and reserve its mesh BLAS
  // id (deduped by path + purpose, matching the native-instance path) and, if the
  // prototype has curves, a curve BLAS id (also deduped) — both stored once and
  // instanced via the TLAS rather than baked per instance.
  std::unordered_map<std::string, tinyusdz::next::UsdPrim> children_by_name;
  for (const tinyusdz::next::UsdPrim &c : instancer.GetChildren())
    children_by_name.emplace(c.GetName(), c);
  std::vector<int32_t> proto_blas(targets->size(), -1);
  std::vector<int32_t> proto_curve(targets->size(), -1);  // CurveProtoCollect idx
  for (size_t pi = 0; pi < targets->size(); ++pi) {
    const tinyusdz::next::Path &tp = (*targets)[pi];
    tinyusdz::next::UsdPrim proto;
    auto cit = children_by_name.find(tp.name());
    if (cit != children_by_name.end()) proto = cit->second;
    else proto = stage.GetPrimAtPath(tp.str());
    if (!proto.IsValid()) continue;
    const std::string proto_path = proto.GetPath().str();
    const std::string key =
        proto_path + "\x1f" + std::to_string(PurposeBit(purpose));
    auto it = proto_ids->find(key);
    if (it == proto_ids->end()) {
      const uint32_t blas_id = uint32_t(protos->size()) + 1;  // blas[0] = base
      (*proto_ids)[key] = blas_id;
      protos->push_back({proto_path, purpose, blas_id});
      proto_blas[pi] = int32_t(blas_id);
    } else {
      proto_blas[pi] = int32_t(it->second);
    }
    proto_curve[pi] =
        ReserveCurveProto(stage, proto_path, purpose, time, curve_inst);
  }

  // Per-instance arrays. `positions` drives the instance count; the rest default
  // (identity orientation, unit scale, proto 0) when absent or shorter.
  const std::vector<float> positions =
      ReadFloatArrayLazy(instancer, "positions", time);
  if (positions.empty()) return;
  const size_t n = positions.size() / 3;
  const std::vector<int32_t> proto_indices =
      ReadIntArrayLazy(instancer, "protoIndices", time);
  const std::vector<float> orientations =
      ReadFloatArrayLazy(instancer, "orientations", time);
  const std::vector<float> scales = ReadFloatArrayLazy(instancer, "scales", time);
  const std::vector<int64_t> invisible =
      ReadInt64ArrayLazy(instancer, "invisibleIds", time);
  const std::unordered_set<int64_t> invisible_set(invisible.begin(),
                                                  invisible.end());

  static const float kIdentQuat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  static const float kUnitScale[3] = {1.0f, 1.0f, 1.0f};
  size_t emitted = 0;
  for (size_t i = 0; i < n; ++i) {
    if (!invisible_set.empty() && invisible_set.count(int64_t(i))) continue;
    const int32_t pidx = (i < proto_indices.size()) ? proto_indices[i] : 0;
    if (pidx < 0 || size_t(pidx) >= proto_blas.size()) continue;
    const int32_t blas_id = proto_blas[size_t(pidx)];
    const int32_t curve_idx = proto_curve[size_t(pidx)];
    if (blas_id < 0 && curve_idx < 0) continue;
    const float *q = (orientations.size() >= (i + 1) * 4) ? &orientations[i * 4]
                                                          : kIdentQuat;
    const float *s =
        (scales.size() >= (i + 1) * 3) ? &scales[i * 3] : kUnitScale;
    const matrix4d inst_world =
        InstanceTRS(&positions[i * 3], q, s) * instancer_world;
    float o2w[12];
    Mat4ToObj2World(inst_world, o2w);
    if (blas_id >= 0) {
      InstanceRT inst;
      inst.blas_id = uint32_t(blas_id);
      std::memcpy(inst.o2w, o2w, sizeof(o2w));
      instances->push_back(inst);
    }
    if (curve_idx >= 0 && curve_inst) {
      CurveInstanceRT ci;
      ci.curve_proto_idx = uint32_t(curve_idx);
      std::memcpy(ci.o2w, o2w, sizeof(o2w));
      curve_inst->instances.push_back(ci);
    }
    emitted++;
  }
  if (stats) {
    stats->point_instancers++;
    stats->point_instances += emitted;
  }
}

// Walk the composed stage, splitting it into (a) base mesh jobs — geometry not
// under any native instance, emitted in world space — and (b) instance
// placements that reference a per-prototype BLAS. Native instances (prims with
// instance_prototype set) and UsdGeomPointInstancer prims are NOT descended
// into; instead each placement is recorded as an InstanceRT and its prototype
// (deduped by path+purpose) is queued for a BLAS build. This keeps each
// prototype's geometry stored once. Honors `mask`.
void CollectSceneSplit(const tinyusdz::next::Stage &stage,
                       const tinyusdz::next::UsdPrim &prim,
                       const matrix4d &parent_world,
                       tinyusdz::Purpose inherited_purpose, double time,
                       const std::vector<std::string> &mask,
                       std::vector<MeshJobNext> *base_jobs,
                       std::vector<InstanceRT> *instances,
                       std::unordered_map<std::string, uint32_t> *proto_ids,
                       std::vector<ProtoBuildReq> *protos,
                       std::vector<CurveJobNext> *curve_jobs,
                       CurveProtoCollect *curve_inst, RTPreviewStats *stats) {
  double dmat[16];
  tinyusdz::tydra::next::ComputeLocalTransform(prim, dmat, time);
  const matrix4d local = Mat4FromArray(dmat);
  const bool reset = tinyusdz::tydra::next::HasResetXformStack(prim);
  const matrix4d world = reset ? local : (local * parent_world);

  tinyusdz::Purpose purpose = inherited_purpose;
  if (const tinyusdz::next::Value *pv = prim.GetPropertyValue("purpose")) {
    if (const std::string *t = pv->as_token()) {
      if (*t == "render") purpose = tinyusdz::Purpose::Render;
      else if (*t == "proxy") purpose = tinyusdz::Purpose::Proxy;
      else if (*t == "guide") purpose = tinyusdz::Purpose::Guide;
    }
  }

  const tinyusdz::next::PrimSpec *spec = prim.GetPrimSpec();
  const std::string proto_path =
      spec ? spec->meta().instance_prototype() : std::string();
  if (!proto_path.empty()) {
    // Native instance: record placement + queue its prototype. Do not descend
    // (the instance proxy's children come from the prototype).
    if (PathMatchesMask(prim.GetPath().str(), mask)) {
      const std::string key =
          proto_path + "\x1f" + std::to_string(PurposeBit(purpose));
      auto it = proto_ids->find(key);
      uint32_t blas_id;
      if (it == proto_ids->end()) {
        blas_id = uint32_t(protos->size()) + 1;  // blas[0] is the base scene
        (*proto_ids)[key] = blas_id;
        protos->push_back({proto_path, purpose, blas_id});
      } else {
        blas_id = it->second;
      }
      float o2w[12];
      Mat4ToObj2World(world, o2w);
      InstanceRT inst;
      inst.blas_id = blas_id;
      std::memcpy(inst.o2w, o2w, sizeof(o2w));
      instances->push_back(inst);
      // Curves under the prototype: place them per native instance via a deduped
      // curve BLAS (the prototype's own copy is collected once as base geometry).
      const int32_t curve_idx =
          ReserveCurveProto(stage, proto_path, purpose, time, curve_inst);
      if (curve_idx >= 0 && curve_inst) {
        CurveInstanceRT ci;
        ci.curve_proto_idx = uint32_t(curve_idx);
        std::memcpy(ci.o2w, o2w, sizeof(o2w));
        curve_inst->instances.push_back(ci);
      }
    }
    return;
  }

  // UsdGeomPointInstancer: expand into TLAS placements (one BLAS per prototype,
  // shared with the native-instance pool) plus instanced curves. Its children are
  // the prototypes, so do not descend (that would emit each prototype once,
  // un-instanced).
  if (prim.GetTypeName() == "PointInstancer") {
    CollectPointInstancer(stage, prim, world, purpose, time, mask, instances,
                          proto_ids, protos, curve_inst, stats);
    return;
  }

  if (prim.GetTypeName() == "Mesh" &&
      PathMatchesMask(prim.GetPath().str(), mask)) {
    MeshJobNext job;
    job.prim = prim;
    job.world = world;
    job.purpose = purpose;
    base_jobs->push_back(std::move(job));
  } else if (curve_jobs && IsCurvePrimNext(prim) &&
             PathMatchesMask(prim.GetPath().str(), mask)) {
    CurveJobNext cj;
    cj.prim = prim;
    cj.world = world;
    cj.purpose = purpose;
    curve_jobs->push_back(std::move(cj));
  }
  for (const tinyusdz::next::UsdPrim &child : prim.GetChildren()) {
    CollectSceneSplit(stage, child, world, purpose, time, mask, base_jobs,
                      instances, proto_ids, protos, curve_jobs, curve_inst,
                      stats);
  }
}

// Collect a prototype's mesh jobs in prototype-LOCAL space (the holder prim at
// identity): traverse the holder's children with parent_world = identity, where
// GetChildren() transparently expands any nested instances inline (bounded —
// built once per unique prototype). The instance's world transform is applied
// later by the TLAS.
void CollectProtoJobs(const tinyusdz::next::Stage &stage,
                      const std::string &proto_path,
                      tinyusdz::Purpose start_purpose, double time,
                      std::vector<MeshJobNext> *jobs) {
  tinyusdz::next::UsdPrim proto = stage.GetPrimAtPath(proto_path);
  if (!proto.IsValid()) return;
  static const std::vector<std::string> kNoMask;
  // A prototype root is placed at identity (its own transform is replaced by the
  // instance transform), but if the prototype prim IS a Mesh (a PointInstancer
  // prototype can point straight at a Mesh) it must still be collected.
  if (proto.GetTypeName() == "Mesh") {
    MeshJobNext job;
    job.prim = proto;
    job.world = matrix4d::identity();
    job.purpose = start_purpose;
    jobs->push_back(std::move(job));
  }
  for (const tinyusdz::next::UsdPrim &child : proto.GetChildren()) {
    CollectRTPreviewMeshesNext(stage, child, matrix4d::identity(), start_purpose,
                               time, kNoMask, jobs);
  }
}

// Build one curve prototype into a curve BLAS (round hair) in prototype-LOCAL
// space, with one TriInfo per segment for hit resolution. The instance transform
// is applied later by the TLAS. Instanced curves are treated as round (the common
// XGen case; per-instance flat/ribbon curves are not separated). Returns false
// only on a LightRT build failure.
bool BuildCurveBlas(const tinyusdz::next::Stage &stage,
                    const std::string &proto_path, tinyusdz::Purpose purpose,
                    double time, const lrt_tri_build_options &build_opts,
                    Blas *out, Bounds *local) {
  std::vector<CurveJobNext> curves;
  CollectProtoCurves(stage, proto_path, purpose, time, &curves);
  if (curves.empty()) return true;
  std::vector<float> pts, radii;
  std::vector<uint32_t> first, count;
  for (const CurveJobNext &job : curves) {
    std::vector<tinyusdz::value::point3f> p = ReadCurvePointsNext(job.prim, time);
    std::vector<int32_t> c32 =
        ReadIntArrayLazy(job.prim, "curveVertexCounts", time);
    if (p.empty() || c32.empty()) continue;
    std::vector<int> c(c32.begin(), c32.end());
    std::vector<float> w = ReadFloatArrayLazy(job.prim, "widths", time);
    AppendLinearCurveStrands(p, c, w, job.world, &pts, &radii, &first, &count,
                             &out->curve_info, local);
  }
  if (first.empty()) return true;
  lrt_hair_strands s;
  std::memset(&s, 0, sizeof(s));
  s.points = pts.data();
  s.radius = radii.data();
  s.strand_first = first.data();
  s.strand_count = count.data();
  s.nstrands = first.size();
  s.npoints = radii.size();
  lrt_result e = LRT_RESULT_OK;
  out->scene = lrt_roundcurve_scene_build(&s, &build_opts, &e);
  out->is_curve = true;
  if (!out->scene) {
    std::cerr << "Failed to build curve BLAS (err=" << int(e) << ").\n";
    return false;
  }
  return true;
}

// Stream a list of (material-resolved) mesh jobs into packed triangle buffers +
// a bounds, in parallel, appending in job order (deterministic). Geometry is
// emitted in each job's `world` space. `purpose_cull` drops purpose-invisible
// triangles at build time (TLAS path).
// Returns false if a memory-cap allocation failure (std::bad_alloc from
// PoolAlloc) interrupted streaming — the caller then aborts the render cleanly
// instead of letting the process get OOM-killed.
template <class FVec, class TVec>
bool StreamMeshJobs(const std::vector<MeshJobNext> &jobs, uint32_t purpose_mask,
                    double time, bool want_uvs, bool purpose_cull, int threads,
                    FVec *out_vertices, TVec *out_tris, FVec *out_tri_uvs,
                    Bounds *out_bounds, RTPreviewStats *out_stats,
                    std::vector<TriMat> *out_mat_table = nullptr,
                    bool want_colors = false, FVec *out_tri_colors = nullptr,
                    bool want_normals = false, FVec *out_tri_normals = nullptr) {
  struct R {
    FVec v;
    TVec t;
    FVec uv;
    FVec col;  // per-corner RGBA (12 floats/tri) when want_colors
    FVec nrm;  // per-corner normals (9 floats/tri) when want_normals
    Bounds b;
    RTPreviewStats s;
    TriMat mat;  // this job's single material (slim TriStore path only)
  };
  std::vector<R> results(jobs.size());
  const unsigned nthreads =
      std::min<unsigned>(WorkerThreadCount(threads),
                         jobs.empty() ? 1u : unsigned(jobs.size()));
  std::atomic<size_t> cursor{0};
  std::atomic<bool> oom{false};
  // A bad_alloc must be caught INSIDE each worker thread (an exception escaping a
  // std::thread calls std::terminate); it signals the cap was hit so all workers
  // stop early.
  auto worker = [&]() {
    try {
      for (;;) {
        const size_t i = cursor.fetch_add(1, std::memory_order_relaxed);
        if (i >= jobs.size() || oom.load(std::memory_order_relaxed)) break;
        const MeshJobNext &job = jobs[i];
        R &r = results[i];
        AddRTPreviewMeshNext(job.prim, job.world, job.purpose, purpose_mask, time,
                             job.base_color, job.tex_id, job.normal_tex_id,
                             job.roughness, job.metallic, job.rough_tex,
                             job.metal_tex, job.emission, job.emission_tex_id,
                             job.occlusion, job.occ_tex, job.uv_xform, want_uvs,
                             &r.v, &r.t,
                             &r.uv, &r.b, &r.s, purpose_cull, &r.mat,
                             job.opacity, want_colors, &r.col, want_normals,
                             &r.nrm);
      }
    } catch (const std::bad_alloc &) {
      oom.store(true, std::memory_order_relaxed);
    }
  };
  if (nthreads <= 1) {
    worker();
  } else {
    std::vector<std::thread> pool;
    pool.reserve(nthreads);
    for (unsigned t = 0; t < nthreads; ++t) pool.emplace_back(worker);
    for (std::thread &th : pool) th.join();
  }
  if (oom.load(std::memory_order_relaxed)) return false;

  size_t tf = 0, tt = 0, tu = 0;
  for (const R &r : results) {
    tf += r.v.size();
    tt += r.t.size();
    tu += r.uv.size();
  }
  try {
    out_vertices->reserve(out_vertices->size() + tf);
    out_tris->reserve(out_tris->size() + tt);
    if (want_uvs) out_tri_uvs->reserve(out_tri_uvs->size() + tu);
    for (R &r : results) {
      out_vertices->insert(out_vertices->end(), r.v.begin(), r.v.end());
      // Slim-store path: assign each of this job's triangles a global material id
      // and append the job's material to the shared table (one entry per job).
      if constexpr (std::is_same<typename TVec::value_type, TriStore>::value) {
        if (out_mat_table) {
          const uint32_t mid = uint32_t(out_mat_table->size());
          out_mat_table->push_back(r.mat);
          for (auto &ts : r.t) ts.mat_id = mid;
        }
      }
      out_tris->insert(out_tris->end(), r.t.begin(), r.t.end());
      if (want_uvs)
        out_tri_uvs->insert(out_tri_uvs->end(), r.uv.begin(), r.uv.end());
      if (want_colors && out_tri_colors)
        out_tri_colors->insert(out_tri_colors->end(), r.col.begin(), r.col.end());
      if (want_normals && out_tri_normals)
        out_tri_normals->insert(out_tri_normals->end(), r.nrm.begin(),
                                r.nrm.end());
      MergeBounds(out_bounds, r.b);
      MergeStats(out_stats, r.s);
      FVec().swap(r.v);
      TVec().swap(r.t);
      FVec().swap(r.uv);
      FVec().swap(r.col);
      FVec().swap(r.nrm);
    }
  } catch (const std::bad_alloc &) {
    return false;
  }
  return true;
}

// Expand `g` by a local AABB transformed by a 3x4 object->world (8 corners).
void ExpandBoundsByTransformedO2W(Bounds *g, const Bounds &local,
                                  const float o2w[12]) {
  if (!local.valid) return;
  for (int c = 0; c < 8; ++c) {
    Vec3 corner{(c & 1) ? local.hi.x : local.lo.x,
                (c & 2) ? local.hi.y : local.lo.y,
                (c & 4) ? local.hi.z : local.lo.z};
    Expand(g, TransformPointO2W(o2w, corner));
  }
}

// (Re)stream triangles at `time` and (re)build the BVH. Safe to call repeatedly
// (e.g. once per animation frame): frees the previous BVH and clears the
// previous geometry first. Honors ctx.opt.mask. Geometry/transforms are
// evaluated at `time` (NaN = default value). When the composed scene has native
// instances, builds a two-level BVH (per-prototype BLAS + TLAS) so instanced
// geometry is stored once; otherwise builds a single flat scene (byte-identical
// to the historical path).
bool ExtractAndBuildBVH(RenderContext &ctx, double time) {
  const Options &opt = ctx.opt;
  ctx.frame_time = time;
  if (ctx.tlas) {
    lrt_tlas_free(ctx.tlas);
    ctx.tlas = nullptr;
  }
  if (ctx.scene) {
    lrt_tri_scene_free(ctx.scene);
    ctx.scene = nullptr;
  }
  ctx.vertices.clear();
  ctx.tris.clear();
  ctx.textures.clear();
  ctx.tri_uvs.clear();
  ctx.tri_colors.clear();
  ctx.tri_normals.clear();
  ctx.blas.clear();
  ctx.instances.clear();
  ctx.use_tlas = false;
  ctx.bounds = Bounds();
  ctx.stats = RTPreviewStats();

  const auto stream_t0 = std::chrono::steady_clock::now();
  std::vector<MeshJobNext> base_jobs;
  std::vector<InstanceRT> instances;
  std::unordered_map<std::string, uint32_t> proto_ids;
  std::vector<ProtoBuildReq> protos;
  std::vector<CurveJobNext> curve_jobs;
  CurveProtoCollect curve_inst;
  for (const tinyusdz::next::UsdPrim &root : ctx.stage.GetRootPrims()) {
    CollectSceneSplit(ctx.stage, root, matrix4d::identity(),
                      tinyusdz::Purpose::Default, time, opt.mask, &base_jobs,
                      &instances, &proto_ids, &protos, &curve_jobs, &curve_inst,
                      &ctx.stats);
  }
  // Curves (BasisCurves/NurbsCurves, plus any baked from curve-prototype
  // instancers) build into ctx.direct as LightRT hair scenes; RenderImage traces
  // them via the DirectScene path in both the flat and TLAS render modes.
  if (!BuildNextCurves(ctx, curve_jobs, time)) return false;
  ctx.stats.curve_strands = curve_jobs.size();

  lrt_tri_build_options build_opts;
  std::memset(&build_opts, 0, sizeof(build_opts));
  build_opts.quality = opt.quality;
  build_opts.layout = LRT_TRI_LAYOUT_AUTO;
  build_opts.max_leaf_size = 0;
  build_opts.num_threads = WorkerThreadCount(opt.threads);

  // Embedded textures: if the input is a .usdz package, open it so material
  // resolution can pull packed textures from the archive (else nullptr -> the
  // texture cache falls back to the filesystem).
  tinyusdz::next::USDZReader usdz_archive;
  const tinyusdz::next::USDZReader *usdz_ptr = nullptr;
  {
    const std::string &in = opt.input;
    if (in.size() >= 5 && in.compare(in.size() - 5, 5, ".usdz") == 0 &&
        usdz_archive.OpenFile(in)) {
      usdz_ptr = &usdz_archive;
    }
  }

  // -------------------------------------------------------------------------
  // Flat path: no native instances and no instanced curve prototypes. Identical
  // to the historical single-scene build (preserves byte-for-byte renders of
  // self-contained scenes).
  // -------------------------------------------------------------------------
  if (instances.empty() && curve_inst.instances.empty()) {
    ctx.stats.meshes = base_jobs.size();
    {
      TextureCache tc;
      tc.textures = &ctx.textures;
      tc.base_dir = DirName(opt.input);
      tc.usdz = usdz_ptr;
      std::unordered_map<std::string, ResolvedMat> mat_cache;
      for (MeshJobNext &job : base_jobs) {
        ResolveMeshMaterialCached(ctx.stage, job.prim, tc, mat_cache, &job);
      }
    }
    const bool want_uvs = !ctx.textures.empty();
    bool want_colors = false;
    for (const MeshJobNext &j : base_jobs)
      if (j.vertex_color) { want_colors = true; break; }
    if (!StreamMeshJobs(base_jobs, opt.purpose_mask, time, want_uvs,
                        /*purpose_cull=*/false, opt.threads, &ctx.vertices,
                        &ctx.tris, &ctx.tri_uvs, &ctx.bounds, &ctx.stats,
                        /*out_mat_table=*/nullptr, want_colors, &ctx.tri_colors,
                        opt.smooth, &ctx.tri_normals)) {
      std::cerr << "Aborting: triangle stream exceeded memory cap "
                << MemBudget::GiB(MemBudget::Get().Cap())
                << ".\n  Raise -maxMem, restrict with -mask, or lower -complexity.\n";
      return false;
    }
    const auto stream_t1 = std::chrono::steady_clock::now();
    ctx.stream_seconds =
        std::chrono::duration<double>(stream_t1 - stream_t0).count();
    ctx.stats.build_seconds = ctx.stream_seconds;
    ctx.stats.packed_triangle_bytes =
        uint64_t(ctx.vertices.size()) * sizeof(float);
    const bool have_curves = ctx.direct.round_curves || ctx.direct.flat_curves ||
                             ctx.direct.bez_curves;
    if (ctx.tris.empty()) {
      if (have_curves) return true;  // curves-only scene: traced via DirectScene
      std::cerr << "RT preview (next) found no renderable Mesh triangles.\n";
      return false;
    }
    // LightRT builds its BVH outside our allocator: it copies the triangle
    // vertices into its own layout (~36 B/tri) plus nodes (~kBvhBytesPerTri/tri).
    // Guard the process RSS against the cap before committing to the build.
    std::string why;
    if (MemBudget::Get().WouldExceed(ctx.tris.size() * kBvhBytesPerTri, &why)) {
      std::cerr << "Aborting BVH build: " << why
                << ".\n  Raise -maxMem, restrict with -mask, or lower -complexity.\n";
      return false;
    }
    const auto bvh_t0 = std::chrono::steady_clock::now();
    lrt_result lrt_err = LRT_RESULT_OK;
    ctx.scene = lrt_tri_scene_build(ctx.vertices.data(), ctx.tris.size(),
                                    &build_opts, &lrt_err);
    const auto bvh_t1 = std::chrono::steady_clock::now();
    if (!ctx.scene) {
      std::cerr << "Failed to build LightRT scene (err=" << int(lrt_err) << ").\n";
      return false;
    }
    ctx.bvh_seconds = std::chrono::duration<double>(bvh_t1 - bvh_t0).count();
    return true;
  }

  // -------------------------------------------------------------------------
  // Two-level (instanced) path: base BLAS + one BLAS per unique prototype,
  // placed by a TLAS. Geometry is stored once per prototype.
  // -------------------------------------------------------------------------
  ctx.use_tlas = true;
  std::vector<std::vector<MeshJobNext>> proto_jobs(protos.size());
  for (size_t i = 0; i < protos.size(); ++i) {
    CollectProtoJobs(ctx.stage, protos[i].path, protos[i].purpose, time,
                     &proto_jobs[i]);
  }
  // Material resolution over base + every prototype's meshes (shared cache).
  {
    TextureCache tc;
    tc.textures = &ctx.textures;
    tc.base_dir = DirName(opt.input);
    tc.usdz = usdz_ptr;
    std::unordered_map<std::string, ResolvedMat> mat_cache;
    for (MeshJobNext &job : base_jobs) {
      ResolveMeshMaterialCached(ctx.stage, job.prim, tc, mat_cache, &job);
    }
    for (std::vector<MeshJobNext> &pj : proto_jobs) {
      for (MeshJobNext &job : pj) {
        ResolveMeshMaterialCached(ctx.stage, job.prim, tc, mat_cache, &job);
      }
    }
  }
  const bool want_uvs = !ctx.textures.empty();
  bool want_colors = false;
  for (const MeshJobNext &j : base_jobs)
    if (j.vertex_color) { want_colors = true; break; }
  for (const std::vector<MeshJobNext> &pj : proto_jobs)
    for (const MeshJobNext &j : pj)
      if (j.vertex_color) { want_colors = true; break; }

  // blas layout: [0] base, [1 .. P] mesh prototypes, [P+1 ..] curve prototypes.
  const size_t curve_base = 1 + protos.size();
  ctx.blas.clear();
  ctx.blas.resize(curve_base + curve_inst.protos.size());
  std::vector<Bounds> local_bounds(ctx.blas.size());

  // Base geometry (world space) -> blas[0].
  bool stream_ok = StreamMeshJobs(
      base_jobs, opt.purpose_mask, time, want_uvs, /*purpose_cull=*/true,
      opt.threads, &ctx.blas[0].vertices, &ctx.blas[0].tris,
      &ctx.blas[0].tri_uvs, &local_bounds[0], &ctx.stats,
      &ctx.blas[0].mat_table, want_colors, &ctx.blas[0].tri_colors, opt.smooth,
      &ctx.blas[0].tri_normals);
  // Each mesh prototype (local space) -> blas[blas_id].
  for (size_t i = 0; stream_ok && i < protos.size(); ++i) {
    const uint32_t b = protos[i].blas_id;
    RTPreviewStats discard;
    stream_ok = StreamMeshJobs(proto_jobs[i], opt.purpose_mask, time, want_uvs,
                               /*purpose_cull=*/true, opt.threads,
                               &ctx.blas[b].vertices, &ctx.blas[b].tris,
                               &ctx.blas[b].tri_uvs, &local_bounds[b], &discard,
                               &ctx.blas[b].mat_table, want_colors,
                               &ctx.blas[b].tri_colors, opt.smooth,
                               &ctx.blas[b].tri_normals);
  }
  if (!stream_ok) {
    std::cerr << "Aborting: triangle stream exceeded memory cap "
              << MemBudget::GiB(MemBudget::Get().Cap())
              << ".\n  Raise -maxMem, restrict with -mask, or lower -complexity.\n";
    return false;
  }
  // Each curve prototype (local space) -> blas[curve_base + i] as a curve BLAS.
  for (size_t i = 0; i < curve_inst.protos.size(); ++i) {
    const size_t b = curve_base + i;
    if (!BuildCurveBlas(ctx.stage, curve_inst.protos[i].path,
                        curve_inst.protos[i].purpose, time, build_opts,
                        &ctx.blas[b], &local_bounds[b])) {
      return false;
    }
  }
  const auto stream_t1 = std::chrono::steady_clock::now();
  ctx.stream_seconds =
      std::chrono::duration<double>(stream_t1 - stream_t0).count();

  // Guard the BLAS builds (LightRT allocates outside our pool): the build adds
  // ~kBvhBytesPerTri per UNIQUE triangle (prototypes stored once).
  {
    size_t unique_tris = 0;
    for (const Blas &b : ctx.blas) unique_tris += b.tris.size();
    std::string why;
    if (MemBudget::Get().WouldExceed(unique_tris * kBvhBytesPerTri, &why)) {
      std::cerr << "Aborting BVH build: " << why
                << ".\n  Raise -maxMem, restrict with -mask, or lower -complexity.\n";
      return false;
    }
  }

  // Build the BLAS with a size-split strategy: LARGE prototypes are built one at
  // a time but each with full intra-build threading (LightRT only parallelizes a
  // single build at >=4096 tris), while the many SMALL prototypes are built in
  // one batch parallelized ACROSS scenes (each single-threaded). Using the batch
  // for everything would force big BLAS single-threaded and regress heavily
  // instanced scenes; the serial loop alone leaves the small-BLAS fleet building
  // one-at-a-time.
  const auto bvh_t0 = std::chrono::steady_clock::now();
  {
    const size_t nb = ctx.blas.size();
    const size_t kLargeTris = 32768;  // above this, intra-build threading wins
    // Pass 1: large BLAS, full internal threading.
    for (size_t b = 0; b < nb; ++b) {
      if (ctx.blas[b].tris.size() >= kLargeTris) {
        lrt_result e = LRT_RESULT_OK;
        ctx.blas[b].scene = lrt_tri_scene_build(
            ctx.blas[b].vertices.data(), ctx.blas[b].tris.size(), &build_opts, &e);
        if (!ctx.blas[b].scene) {
          std::cerr << "Failed to build BLAS (err=" << int(e) << ").\n";
          return false;
        }
      }
    }
    // Pass 2: small BLAS, batched across workers (bntris==0 skips large/empty).
    std::vector<const float *> bverts(nb, nullptr);
    std::vector<size_t> bntris(nb, 0);
    std::vector<lrt_tri_scene *> bscenes(nb, nullptr);
    std::vector<lrt_result> berrs(nb, LRT_RESULT_OK);
    for (size_t b = 0; b < nb; ++b) {
      const size_t nt = ctx.blas[b].tris.size();
      if (nt > 0 && nt < kLargeTris) {
        bverts[b] = ctx.blas[b].vertices.data();
        bntris[b] = nt;
      }
    }
    lrt_tri_scene_build_batch(bverts.data(), bntris.data(), nb, &build_opts,
                              bscenes.data(), berrs.data());
    for (size_t b = 0; b < nb; ++b) {
      if (bntris[b] > 0) {
        ctx.blas[b].scene = bscenes[b];
        if (!bscenes[b]) {
          std::cerr << "Failed to build BLAS (err=" << int(berrs[b]) << ").\n";
          return false;
        }
      }
    }
  }

  // LightRT tolerates NULL entries in the BLAS array (empty prototypes — e.g.
  // fully purpose-culled) as long as no instance references them, so the BLAS
  // index used by both the TLAS and shade-time ResolveTLASHit is just the
  // ctx.blas index — no compaction/remap needed.
  std::vector<lrt_tri_scene *> blas_ptrs(ctx.blas.size(), nullptr);
  for (size_t b = 0; b < ctx.blas.size(); ++b) blas_ptrs[b] = ctx.blas[b].scene;

  // Placements: instance 0 is the base scene at identity, then each native
  // instance whose prototype BLAS is non-empty. instance_id indexes
  // ctx.instances (resolved back to blas_id + transform at shade time).
  std::vector<lrt_instance> lrt_insts;
  auto add_instance = [&](uint32_t blas_id, const float o2w[12]) {
    if (blas_id >= ctx.blas.size() || !ctx.blas[blas_id].scene) return;
    const uint32_t id = uint32_t(ctx.instances.size());
    InstanceRT inst;
    inst.blas_id = blas_id;
    std::memcpy(inst.o2w, o2w, sizeof(inst.o2w));
    ctx.instances.push_back(inst);
    lrt_instance li;
    std::memset(&li, 0, sizeof(li));
    li.blas_id = blas_id;
    std::memcpy(li.obj2world, o2w, sizeof(li.obj2world));
    li.instance_id = id;
    li.mask = 0xffffffffu;
    lrt_insts.push_back(li);
    ExpandBoundsByTransformedO2W(&ctx.bounds, local_bounds[blas_id], o2w);
    ctx.stats.triangles += uint64_t(ctx.blas[blas_id].tris.size());
  };
  static const float kIdentO2W[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
  ctx.stats.triangles = 0;
  add_instance(0, kIdentO2W);  // base
  for (const InstanceRT &inst : instances) add_instance(inst.blas_id, inst.o2w);
  // Instanced curve prototypes: each placement references its curve BLAS, stored
  // once (no per-instance world-space copy).
  for (const CurveInstanceRT &ci : curve_inst.instances)
    add_instance(uint32_t(curve_base + ci.curve_proto_idx), ci.o2w);
  ctx.stats.curve_instances = curve_inst.instances.size();
  // The collection-side instance lists are now fully copied into ctx.instances +
  // lrt_insts; free them before lrt_tlas_build allocates the (peak) TLAS nodes.
  std::vector<InstanceRT>().swap(instances);
  std::vector<CurveInstanceRT>().swap(curve_inst.instances);

  if (lrt_insts.empty()) {
    std::cerr << "RT preview (next) found no renderable Mesh triangles.\n";
    return false;
  }
  ctx.stats.meshes = base_jobs.size() + instances.size();

  lrt_result terr = LRT_RESULT_OK;
  ctx.tlas = lrt_tlas_build(blas_ptrs.data(), blas_ptrs.size(),
                            lrt_insts.data(), lrt_insts.size(), &build_opts,
                            &terr);
  const auto bvh_t1 = std::chrono::steady_clock::now();
  if (!ctx.tlas) {
    std::cerr << "Failed to build LightRT TLAS (err=" << int(terr) << ").\n";
    return false;
  }
  ctx.bvh_seconds = std::chrono::duration<double>(bvh_t1 - bvh_t0).count();
  ctx.stats.build_seconds = ctx.stream_seconds;
  uint64_t blas_bytes = 0;
  for (const Blas &b : ctx.blas) blas_bytes += uint64_t(b.vertices.size()) * sizeof(float);
  ctx.stats.packed_triangle_bytes = blas_bytes;
  return true;
}

// Load the scene via next, then stream + build the BVH at the initial time.
// First UsdLuxDomeLight in the composed stage (depth-first), or an invalid prim.
tinyusdz::next::UsdPrim FindDomeLightRec(const tinyusdz::next::UsdPrim &prim) {
  if (prim.GetTypeName() == "DomeLight") return prim;
  for (const tinyusdz::next::UsdPrim &c : prim.GetChildren()) {
    tinyusdz::next::UsdPrim r = FindDomeLightRec(c);
    if (r.IsValid()) return r;
  }
  return tinyusdz::next::UsdPrim();
}

// Build the IBL cache from the --env override or a DomeLight; returns false (and
// leaves ibl invalid) if there is no env, so the renderer falls back to the
// headlight.
bool BuildNextIbl(const tinyusdz::next::Stage &stage, const Options &opt,
                  const std::string &base_dir, IblCache *ibl) {
  std::string env_path = opt.env_file;
  Vec3 scale{1.0f, 1.0f, 1.0f};
  if (env_path.empty()) {
    for (const tinyusdz::next::UsdPrim &root : stage.GetRootPrims()) {
      tinyusdz::next::UsdPrim dome = FindDomeLightRec(root);
      if (!dome.IsValid()) continue;
      if (const tinyusdz::next::Value *v =
              dome.GetPropertyValue("inputs:texture:file")) {
        const std::string *ap = v->as_asset_path();
        if (!ap) ap = v->as_string();
        if (ap) env_path = *ap;
      }
      float intensity = 1.0f;
      if (const tinyusdz::next::Value *v = dome.GetPropertyValue("inputs:intensity"))
        if (const float *f = v->as_float()) intensity = *f;
      Vec3 color{1.0f, 1.0f, 1.0f};
      if (const tinyusdz::next::Value *v = dome.GetPropertyValue("inputs:color"))
        if (const float *f = v->as_float3()) color = Vec3{f[0], f[1], f[2]};
      scale = Vec3{color.x * intensity, color.y * intensity, color.z * intensity};
      break;
    }
  }
  if (env_path.empty()) return false;
  std::string path = env_path;
  if (path[0] != '/' && !base_dir.empty()) path = base_dir + "/" + path;
  EnvImage env;
  if (!LoadEnvImageFromFile(path, scale, &env)) return false;
  return BuildIblFromEnv(std::move(env), ibl);
}

bool BuildRenderContext(const Options &opt, RenderContext &ctx) {
  ctx.opt = opt;
  ctx.width = opt.width > 0 ? opt.width : 960;

  const auto load_t0 = std::chrono::steady_clock::now();
  std::string warn, err;
  // LoadUSDComposed resolves references/payloads/sublayers in place (anchored to
  // the input dir), so tusdrender consumes raw reference-composed scenes (e.g.
  // Caldera prefab stubs) directly — no external usdcat --flatten step. Self-
  // contained / pre-flattened inputs skip composition (identical to LoadUSD).
  tinyusdz::next::pcp::CompositionOptions comp_opts;
  if (!opt.variant_overrides.empty())
    comp_opts.variant_overrides = opt.variant_overrides;
  if (!tinyusdz::next::LoadUSDComposed(opt.input, &ctx.stage, &warn, &err,
                                       &comp_opts)) {
    if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";
    std::cerr << "Failed to load USD (next): " << err << "\n";
    return false;
  }
  if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";
  const auto load_t1 = std::chrono::steady_clock::now();
  ctx.load_seconds = std::chrono::duration<double>(load_t1 - load_t0).count();

  // The composed stage is the memory baseline; everything our pool allocator
  // tracks (triangle buffers) must fit in cap - base. Abort now if compose alone
  // already blew the cap.
  std::string why;
  if (MemBudget::Get().WouldExceed(0, &why)) {
    std::cerr << "Aborting after load: " << why
              << ".\n  Raise -maxMem, restrict with -mask, or pre-flatten.\n";
    return false;
  }
  MemBudget::Get().SnapshotBase();

  ctx.up_axis = tinyusdz::Axis::Y;
  if (ctx.stage.GetUpAxis() == "X") ctx.up_axis = tinyusdz::Axis::X;
  else if (ctx.stage.GetUpAxis() == "Z") ctx.up_axis = tinyusdz::Axis::Z;

  // Initial time: default value unless -timecode was given. -defaultTime forces
  // the default (NaN) explicitly.
  const double init_time = opt.default_time
                               ? std::numeric_limits<double>::quiet_NaN()
                               : opt.timecode;
  if (!ExtractAndBuildBVH(ctx, init_time)) return false;
  ResolveCameraNext(ctx);

  // Image-based lighting: an explicit --env override wins, else the first
  // UsdLuxDomeLight's texture (scaled by intensity*color). Enables the glossy
  // BRDF (roughness/metallic) + env background; absent -> camera headlight.
  BuildNextIbl(ctx.stage, opt, DirName(opt.input), &ctx.ibl);
  if (opt.stats && ctx.ibl.valid) {
    std::cerr << "ibl: " << ctx.ibl.env.width << "x" << ctx.ibl.env.height
              << " (" << (opt.env_file.empty() ? "DomeLight" : "--env") << ")\n";
  }
  return true;
}

// Render the current camera/parameters of `ctx` and write to `path`.
// Reuses the persistent BVH (no rebuild). Returns the trace time in seconds
// (or a negative value on write failure).
double RenderFrameTo(RenderContext &ctx, const std::string &path) {
  ctx.opt.width = ctx.width;
  const auto t0 = std::chrono::steady_clock::now();
  tinyusdz::Image img = RenderImage(
      ctx.scene, &ctx.direct, ctx.tris, ctx.lights,
      ctx.ibl.valid ? &ctx.ibl : nullptr, ctx.camera, ctx.opt, ctx.height,
      ctx.textures.empty() ? nullptr : &ctx.textures,
      ctx.tri_uvs.empty() ? nullptr : &ctx.tri_uvs,
      ctx.use_tlas ? ctx.tlas : nullptr,
      ctx.use_tlas ? &ctx.blas : nullptr,
      ctx.use_tlas ? &ctx.instances : nullptr,
      ctx.tri_colors.empty() ? nullptr : &ctx.tri_colors,
      ctx.tri_normals.empty() ? nullptr : &ctx.tri_normals);
  const auto t1 = std::chrono::steady_clock::now();
  tinyusdz::image::WriteOption wopt;
  wopt.format = tinyusdz::image::WriteImageFormat::Autodetect;
  auto ret = tinyusdz::image::WriteImageToFile(path, img, wopt);
  if (!ret) {
    std::cerr << "Failed to write image: " << ret.error() << "\n";
    return -1.0;
  }
  return std::chrono::duration<double>(t1 - t0).count();
}

void PrintRTStats(const RenderContext &ctx) {
  std::cerr << "rt preview: 1\n";
  std::cerr << "rt loader: next\n";
  std::cerr << "rt meshes: " << ctx.stats.meshes << "\n";
  std::cerr << "rt skipped meshes: " << ctx.stats.skipped_meshes << "\n";
  std::cerr << "rt purpose default triangles: "
            << ctx.stats.purpose_default_triangles << "\n";
  std::cerr << "rt purpose guide triangles: "
            << ctx.stats.purpose_guide_triangles << "\n";
  if (ctx.stats.curve_strands > 0)
    std::cerr << "rt curve strands: " << ctx.stats.curve_strands << "\n";
  if (ctx.stats.curve_instances > 0)
    std::cerr << "rt curve instances: " << ctx.stats.curve_instances << "\n";
  std::cerr << "load seconds: " << ctx.load_seconds << "\n";
  std::cerr << "rt triangle stream seconds: " << ctx.stream_seconds << "\n";
  std::cerr << "rt bvh build seconds: " << ctx.bvh_seconds << "\n";
  std::cerr << "memory cap: " << MemBudget::GiB(MemBudget::Get().Cap()) << "\n";
  std::cerr << "tracked buffer peak: "
            << MemBudget::GiB(MemBudget::Get().PeakTracked()) << "\n";
  std::cerr << "process RSS: " << MemBudget::GiB(MemBudget::ProcessRSS()) << "\n";
  // ctx.stats.triangles is the (instance-expanded) renderable triangle count in
  // both paths; ctx.tris is empty in the two-level (TLAS) path.
  std::cerr << "triangles: " << ctx.stats.triangles << "\n";
  if (ctx.use_tlas) {
    size_t unique_tris = 0;
    for (const Blas &b : ctx.blas) unique_tris += b.tris.size();
    std::cerr << "rt instancing: tlas\n";
    std::cerr << "rt blas count: " << ctx.blas.size() << "\n";
    std::cerr << "rt instances: " << ctx.instances.size() << "\n";
    std::cerr << "rt point instancers: " << ctx.stats.point_instancers << "\n";
    std::cerr << "rt point instances: " << ctx.stats.point_instances << "\n";
    std::cerr << "rt unique triangles: " << unique_tris << "\n";
  } else {
    lrt_tri_stats st;
    std::memset(&st, 0, sizeof(st));
    lrt_tri_scene_stats(ctx.scene, &st);
    std::cerr << "lightrt: " << lrt_tri_kernel_name(ctx.scene) << "\n";
    std::cerr << "bvh nodes: " << st.node_count << ", leaves: " << st.leaf_count
              << ", memory: " << st.memory_bytes << " bytes\n";
  }
}

// Parse an OpenUSD usdrecord FRAMESPEC list into time codes. Each comma-
// separated spec is "t", "start:end", or "start:end x stride" (stride defaults
// to 1, sign inferred from start/end). Examples: "1", "1:10", "1:10x2",
// "10:1", "1:5,8,12:20x4".
bool ParseFrameSpec(const std::string &spec, std::vector<double> *times) {
  std::string s = spec;
  for (char &c : s) {
    if (c == ',') c = ' ';
  }
  std::istringstream iss(s);
  std::string tok;
  while (iss >> tok) {
    double start = 0, end = 0, stride = 1;
    const size_t colon = tok.find(':');
    if (colon == std::string::npos) {
      try {
        start = end = std::stod(tok);
      } catch (...) {
        return false;
      }
    } else {
      std::string a = tok.substr(0, colon);
      std::string rest = tok.substr(colon + 1);
      const size_t xpos = rest.find('x');
      std::string b = (xpos == std::string::npos) ? rest : rest.substr(0, xpos);
      try {
        start = std::stod(a);
        end = std::stod(b);
        if (xpos != std::string::npos) stride = std::stod(rest.substr(xpos + 1));
      } catch (...) {
        return false;
      }
    }
    if (stride == 0) stride = 1;
    stride = std::fabs(stride);
    if (start <= end) {
      for (double t = start; t <= end + 1e-9; t += stride) times->push_back(t);
    } else {
      for (double t = start; t >= end - 1e-9; t -= stride) times->push_back(t);
    }
  }
  return !times->empty();
}

// Substitute a frame number into an output path. Runs of '#' are replaced by the
// zero-padded frame number (width = number of '#'). If there is no '#', the
// frame number is inserted before the extension (.NNNN).
std::string SubstituteFrame(const std::string &path, long frame) {
  const size_t hpos = path.find('#');
  if (hpos != std::string::npos) {
    size_t hend = hpos;
    while (hend < path.size() && path[hend] == '#') ++hend;
    const int width = int(hend - hpos);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%0*ld", width, frame);
    return path.substr(0, hpos) + buf + path.substr(hend);
  }
  const size_t dot = path.find_last_of('.');
  char buf[32];
  std::snprintf(buf, sizeof(buf), ".%04ld", frame);
  if (dot == std::string::npos) return path + buf;
  return path.substr(0, dot) + buf + path.substr(dot);
}

int RunRTPreviewNext(const Options &opt) {
  RenderContext ctx;
  if (!BuildRenderContext(opt, ctx)) return EXIT_FAILURE;
  if (opt.stats) PrintRTStats(ctx);

  // Animation: -frames renders one image per time code, re-evaluating geometry,
  // transforms and any animated camera at that time. The scene is parsed once;
  // each frame re-streams + rebuilds the BVH (geometry may deform).
  if (!opt.frames.empty()) {
    std::vector<double> times;
    if (!ParseFrameSpec(opt.frames, &times)) {
      std::cerr << "Invalid -frames FRAMESPEC: " << opt.frames << "\n";
      return EXIT_FAILURE;
    }
    if (opt.output.empty()) {
      std::cerr << "-frames requires an output path (use # for the frame "
                   "number, e.g. frame.####.png).\n";
      return EXIT_FAILURE;
    }
    // If the rendered geometry is static across time (only the camera and/or
    // nothing animates), the BVH built in BuildRenderContext is valid for every
    // frame -- reuse it and only re-resolve the camera per frame. Otherwise
    // re-stream + rebuild the BVH at each time.
    const bool geom_animated = SceneGeometryAnimated(ctx.stage, opt.mask);
    if (opt.stats) {
      std::cerr << "rt frames: " << times.size()
                << ", geometry animated: " << (geom_animated ? 1 : 0)
                << " (BVH " << (geom_animated ? "rebuilt per frame" : "reused")
                << ")\n";
    }
    for (double t : times) {
      if (geom_animated) {
        if (!ExtractAndBuildBVH(ctx, t)) return EXIT_FAILURE;
      } else {
        ctx.frame_time = t;  // static geometry: keep BVH, animate camera only
      }
      ResolveCameraNext(ctx);
      const std::string out = SubstituteFrame(opt.output, std::lround(t));
      const double secs = RenderFrameTo(ctx, out);
      if (secs < 0.0) return EXIT_FAILURE;
      std::cerr << "frame " << t << " -> " << out << "  (" << secs << "s)\n";
    }
    return EXIT_SUCCESS;
  }

  const double secs = RenderFrameTo(ctx, opt.output);
  if (secs < 0.0) return EXIT_FAILURE;
  if (opt.stats) std::cerr << "render seconds: " << secs << "\n";
  return EXIT_SUCCESS;
}

#ifdef TINYUSDZ_WITH_QJS
// ===========================================================================
// JavaScript scripting + MCP control over a persistent RenderContext.
//
// The scene is loaded and the BVH built once; the `tusdrender.*` JS module then
// changes the camera/parameters and re-renders repeatedly without reloading or
// rebuilding (memory-persistent rendering -- e.g. animation with a moving
// camera). Both the -js script mode and the -mcp stdio server drive the same
// persistent QuickJS engine.
// ===========================================================================

namespace qjs {

constexpr double kPiD = 3.14159265358979323846;
RenderContext *g_render_ctx = nullptr;

double ArgD(JSContext *ctx, int argc, JSValueConst *argv, int i, double def) {
  if (i >= argc) return def;
  double d = def;
  if (JS_ToFloat64(ctx, &d, argv[i]) < 0) return def;
  return d;
}

JSValue Vec3JS(JSContext *ctx, const Vec3 &v) {
  JSValue a = JS_NewArray(ctx);
  JS_SetPropertyUint32(ctx, a, 0, JS_NewFloat64(ctx, v.x));
  JS_SetPropertyUint32(ctx, a, 1, JS_NewFloat64(ctx, v.y));
  JS_SetPropertyUint32(ctx, a, 2, JS_NewFloat64(ctx, v.z));
  return a;
}

void LookAt(CameraFrame &c, const Vec3 &eye, const Vec3 &target, const Vec3 &up,
            float fov_deg) {
  c.origin = eye;
  c.forward = Normalize(Sub(target, eye));
  c.right = Normalize(Cross(c.forward, up));
  if (Length(c.right) < 1.0e-6f) c.right = Vec3{1.0f, 0.0f, 0.0f};
  c.up = Normalize(Cross(c.right, c.forward));
  c.yfov = fov_deg * float(kPiD) / 180.0f;
  c.ortho = false;
  const float dist = Length(Sub(target, eye));
  c.znear = std::max(1.0e-4f, dist * 1.0e-4f);
  c.zfar = std::max(1000.0f, dist * 100.0f);
}

// tusdrender.setCamera(ex,ey,ez, tx,ty,tz, [fovDeg], [ux,uy,uz])
JSValue js_setCamera(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv,
                     int, JSValueConst *) {
  if (!g_render_ctx) return JS_ThrowTypeError(ctx, "no render context");
  Vec3 eye{float(ArgD(ctx, argc, argv, 0, 0)), float(ArgD(ctx, argc, argv, 1, 0)),
           float(ArgD(ctx, argc, argv, 2, 0))};
  Vec3 tgt{float(ArgD(ctx, argc, argv, 3, 0)), float(ArgD(ctx, argc, argv, 4, 0)),
           float(ArgD(ctx, argc, argv, 5, 0))};
  float fov = float(ArgD(ctx, argc, argv, 6,
                         double(g_render_ctx->camera.yfov) * 180.0 / kPiD));
  Vec3 up = AxisVec(g_render_ctx->up_axis);
  if (argc >= 10) {
    up = Vec3{float(ArgD(ctx, argc, argv, 7, up.x)),
              float(ArgD(ctx, argc, argv, 8, up.y)),
              float(ArgD(ctx, argc, argv, 9, up.z))};
  }
  LookAt(g_render_ctx->camera, eye, tgt, up, fov);
  return JS_UNDEFINED;
}

// tusdrender.orbit(azimuthDeg, elevationDeg, distanceScale) -- position the
// camera around the bounds center looking at it (animation-friendly).
JSValue js_orbit(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv, int,
                 JSValueConst *) {
  if (!g_render_ctx) return JS_ThrowTypeError(ctx, "no render context");
  RenderContext &rc = *g_render_ctx;
  const Vec3 center = Mul(Add(rc.bounds.lo, rc.bounds.hi), 0.5f);
  const float radius =
      std::max(1.0e-3f, Length(Sub(rc.bounds.hi, rc.bounds.lo)) * 0.5f);
  const double az = ArgD(ctx, argc, argv, 0, 0.0) * kPiD / 180.0;
  const double el = ArgD(ctx, argc, argv, 1, 20.0) * kPiD / 180.0;
  const double dist = radius * ArgD(ctx, argc, argv, 2, 2.5);
  const Vec3 up = AxisVec(rc.up_axis);
  const Vec3 ref = (std::fabs(up.y) < 0.9f) ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
  const Vec3 e0 = Normalize(Cross(up, ref));   // azimuth 0
  const Vec3 e1 = Normalize(Cross(up, e0));    // azimuth 90
  const float ce = float(std::cos(el)), se = float(std::sin(el));
  const Vec3 horiz = Add(Mul(e0, float(std::cos(az)) * ce),
                         Mul(e1, float(std::sin(az)) * ce));
  const Vec3 dir = Add(horiz, Mul(up, se));
  const Vec3 eye = Add(center, Mul(dir, float(dist)));
  LookAt(rc.camera, eye, center, up,
         float(double(rc.camera.yfov) * 180.0 / kPiD));
  rc.camera.znear = std::max(1.0e-4f, float(dist) * 1.0e-3f);
  rc.camera.zfar = float(dist) + radius * 4.0f;
  return JS_UNDEFINED;
}

JSValue js_setResolution(JSContext *ctx, JSValueConst, int argc,
                         JSValueConst *argv, int, JSValueConst *) {
  if (!g_render_ctx) return JS_ThrowTypeError(ctx, "no render context");
  g_render_ctx->width =
      std::max(1, int(ArgD(ctx, argc, argv, 0, g_render_ctx->width)));
  g_render_ctx->height =
      std::max(1, int(ArgD(ctx, argc, argv, 1, g_render_ctx->height)));
  return JS_UNDEFINED;
}
JSValue js_setAmbient(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv,
                      int, JSValueConst *) {
  if (g_render_ctx)
    g_render_ctx->opt.ambient =
        float(ArgD(ctx, argc, argv, 0, g_render_ctx->opt.ambient));
  return JS_UNDEFINED;
}
JSValue js_setBackground(JSContext *ctx, JSValueConst, int argc,
                         JSValueConst *argv, int, JSValueConst *) {
  if (g_render_ctx)
    g_render_ctx->opt.bg = Vec3{float(ArgD(ctx, argc, argv, 0, 0)),
                                float(ArgD(ctx, argc, argv, 1, 0)),
                                float(ArgD(ctx, argc, argv, 2, 0))};
  return JS_UNDEFINED;
}
JSValue js_setShadows(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv,
                      int, JSValueConst *) {
  if (g_render_ctx && argc > 0)
    g_render_ctx->opt.shadows = JS_ToBool(ctx, argv[0]) != 0;
  return JS_UNDEFINED;
}
JSValue js_setSamples(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv,
                      int, JSValueConst *) {
  if (g_render_ctx)
    g_render_ctx->opt.samples =
        std::max(1, int(ArgD(ctx, argc, argv, 0, g_render_ctx->opt.samples)));
  return JS_UNDEFINED;
}
JSValue js_autoframe(JSContext *ctx, JSValueConst, int, JSValueConst *, int,
                     JSValueConst *) {
  if (!g_render_ctx) return JS_ThrowTypeError(ctx, "no render context");
  g_render_ctx->opt.autoframe = true;
  g_render_ctx->opt.camera.clear();
  ResolveCameraNext(*g_render_ctx);
  return JS_UNDEFINED;
}
// tusdrender.setTime(t) -- re-evaluate geometry/transforms at time t and rebuild
// the BVH. Call with no/NaN arg for the default time. Returns {triangles}.
JSValue js_setTime(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv,
                   int, JSValueConst *) {
  if (!g_render_ctx) return JS_ThrowTypeError(ctx, "no render context");
  const double t =
      ArgD(ctx, argc, argv, 0, std::numeric_limits<double>::quiet_NaN());
  if (!ExtractAndBuildBVH(*g_render_ctx, t)) {
    return JS_ThrowInternalError(ctx, "failed to rebuild geometry at time");
  }
  ResolveCameraNext(*g_render_ctx);
  JSValue o = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, o, "time", std::isnan(t) ? JS_NULL : JS_NewFloat64(ctx, t));
  JS_SetPropertyStr(ctx, o, "triangles",
                    JS_NewInt64(ctx, int64_t(g_render_ctx->tris.size())));
  return o;
}
JSValue js_bounds(JSContext *ctx, JSValueConst, int, JSValueConst *, int,
                  JSValueConst *) {
  if (!g_render_ctx) return JS_ThrowTypeError(ctx, "no render context");
  RenderContext &rc = *g_render_ctx;
  const Vec3 center = Mul(Add(rc.bounds.lo, rc.bounds.hi), 0.5f);
  const float radius = Length(Sub(rc.bounds.hi, rc.bounds.lo)) * 0.5f;
  JSValue o = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, o, "lo", Vec3JS(ctx, rc.bounds.lo));
  JS_SetPropertyStr(ctx, o, "hi", Vec3JS(ctx, rc.bounds.hi));
  JS_SetPropertyStr(ctx, o, "center", Vec3JS(ctx, center));
  JS_SetPropertyStr(ctx, o, "radius", JS_NewFloat64(ctx, radius));
  return o;
}
JSValue js_stats(JSContext *ctx, JSValueConst, int, JSValueConst *, int,
                 JSValueConst *) {
  if (!g_render_ctx) return JS_ThrowTypeError(ctx, "no render context");
  RenderContext &rc = *g_render_ctx;
  lrt_tri_stats st;
  std::memset(&st, 0, sizeof(st));
  lrt_tri_scene_stats(rc.scene, &st);
  JSValue o = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, o, "triangles", JS_NewInt64(ctx, int64_t(rc.tris.size())));
  JS_SetPropertyStr(ctx, o, "meshes", JS_NewInt64(ctx, int64_t(rc.stats.meshes)));
  JS_SetPropertyStr(ctx, o, "bvhNodes", JS_NewInt64(ctx, int64_t(st.node_count)));
  JS_SetPropertyStr(ctx, o, "bvhBytes", JS_NewInt64(ctx, int64_t(st.memory_bytes)));
  JS_SetPropertyStr(ctx, o, "width", JS_NewInt32(ctx, rc.width));
  JS_SetPropertyStr(ctx, o, "height", JS_NewInt32(ctx, rc.height));
  return o;
}
JSValue js_render(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv,
                  int, JSValueConst *) {
  if (!g_render_ctx) return JS_ThrowTypeError(ctx, "no render context");
  if (argc < 1 || !JS_IsString(argv[0]))
    return JS_ThrowTypeError(ctx, "render(path) requires a path string");
  const char *p = JS_ToCString(ctx, argv[0]);
  const std::string path = p ? p : "";
  if (p) JS_FreeCString(ctx, p);
  const double secs = RenderFrameTo(*g_render_ctx, path);
  if (secs < 0.0) return JS_ThrowInternalError(ctx, "failed to write image");
  JSValue o = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, o, "path", JS_NewString(ctx, path.c_str()));
  JS_SetPropertyStr(ctx, o, "width", JS_NewInt32(ctx, g_render_ctx->width));
  JS_SetPropertyStr(ctx, o, "height", JS_NewInt32(ctx, g_render_ctx->height));
  JS_SetPropertyStr(ctx, o, "seconds", JS_NewFloat64(ctx, secs));
  return o;
}

void RegFn(JSContext *ctx, JSValue obj, const char *name, JSCFunctionData *fn,
           int len) {
  JS_SetPropertyStr(ctx, obj, name,
                    JS_NewCFunctionData(ctx, fn, len, 0, 0, nullptr));
}

void RegisterTusdrenderModule(tinyusdz::tydra::JSEngineState &engine,
                              RenderContext *rc) {
  JSContext *ctx = static_cast<JSContext *>(engine.context);
  g_render_ctx = rc;
  JSValue global = JS_GetGlobalObject(ctx);
  JSValue mod = JS_NewObject(ctx);
  RegFn(ctx, mod, "setCamera", js_setCamera, 10);
  RegFn(ctx, mod, "orbit", js_orbit, 3);
  RegFn(ctx, mod, "setResolution", js_setResolution, 2);
  RegFn(ctx, mod, "setAmbient", js_setAmbient, 1);
  RegFn(ctx, mod, "setBackground", js_setBackground, 3);
  RegFn(ctx, mod, "setShadows", js_setShadows, 1);
  RegFn(ctx, mod, "setSamples", js_setSamples, 1);
  RegFn(ctx, mod, "autoframe", js_autoframe, 0);
  RegFn(ctx, mod, "setTime", js_setTime, 1);
  RegFn(ctx, mod, "bounds", js_bounds, 0);
  RegFn(ctx, mod, "stats", js_stats, 0);
  RegFn(ctx, mod, "render", js_render, 1);
  JS_SetPropertyStr(ctx, global, "tusdrender", mod);
  JS_FreeValue(ctx, global);
}

std::string ReadFile(const std::string &path, bool *ok) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs) { *ok = false; return {}; }
  std::stringstream ss;
  ss << ifs.rdbuf();
  *ok = true;
  return ss.str();
}

}  // namespace qjs

int RunJSScriptMode(const Options &opt, const std::string &script_path) {
  RenderContext ctx;
  if (!BuildRenderContext(opt, ctx)) return EXIT_FAILURE;
  if (opt.stats) PrintRTStats(ctx);

  bool ok_read = false;
  const std::string code = qjs::ReadFile(script_path, &ok_read);
  if (!ok_read) {
    std::cerr << "Cannot open script: " << script_path << "\n";
    return EXIT_FAILURE;
  }

  tinyusdz::tydra::JSEngineState engine;
  std::string err;
  if (!tinyusdz::tydra::InitJSEngine(engine, err)) {
    std::cerr << "JS engine init failed: " << err << "\n";
    return EXIT_FAILURE;
  }
  qjs::RegisterTusdrenderModule(engine, &ctx);

  nlohmann::json ret;
  const bool ok = tinyusdz::tydra::RunJSScript(engine, code, ret, err);
  if (!ok) {
    std::cerr << "JS error: " << err << "\n";
  } else if (!ret.is_null()) {
    std::cerr << "JS result: " << ret.dump() << "\n";
  }
  tinyusdz::tydra::DestroyJSEngine(engine);
  qjs::g_render_ctx = nullptr;
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

// Build a tusdrender.* JS expression from an MCP tool call and evaluate it
// against the persistent engine, returning the JSON result.
bool McpCallTool(tinyusdz::tydra::JSEngineState &engine, const std::string &name,
                 const nlohmann::json &args, nlohmann::json &out,
                 std::string &err) {
  auto num = [&](const char *k, double d) -> double {
    return args.contains(k) && args[k].is_number() ? args[k].get<double>() : d;
  };
  auto arr3 = [&](const char *k, double a, double b, double c, double v[3]) {
    v[0] = a; v[1] = b; v[2] = c;
    if (args.contains(k) && args[k].is_array() && args[k].size() == 3) {
      for (int i = 0; i < 3; ++i)
        if (args[k][i].is_number()) v[i] = args[k][i].get<double>();
    }
  };
  std::string code;
  char buf[512];
  if (name == "eval") {
    if (!args.contains("script") || !args["script"].is_string()) {
      err = "eval requires a 'script' string";
      return false;
    }
    code = args["script"].get<std::string>();
  } else if (name == "set_camera") {
    double e[3], t[3];
    arr3("eye", 0, 0, 0, e);
    arr3("target", 0, 0, 0, t);
    const double fov = num("fov", 0.0);
    std::snprintf(buf, sizeof(buf),
                  "tusdrender.setCamera(%.9g,%.9g,%.9g,%.9g,%.9g,%.9g%s)", e[0],
                  e[1], e[2], t[0], t[1], t[2],
                  fov > 0.0 ? (std::string(",") + std::to_string(fov)).c_str()
                            : "");
    code = buf;
  } else if (name == "orbit") {
    std::snprintf(buf, sizeof(buf), "tusdrender.orbit(%.9g,%.9g,%.9g)",
                  num("azimuth", 0.0), num("elevation", 20.0),
                  num("distance", 2.5));
    code = buf;
  } else if (name == "set_time") {
    if (args.contains("time") && args["time"].is_number()) {
      std::snprintf(buf, sizeof(buf), "tusdrender.setTime(%.9g)",
                    args["time"].get<double>());
      code = buf;
    } else {
      code = "tusdrender.setTime()";  // default time
    }
  } else if (name == "set_resolution") {
    std::snprintf(buf, sizeof(buf), "tusdrender.setResolution(%d,%d)",
                  int(num("width", 960)), int(num("height", 540)));
    code = buf;
  } else if (name == "render") {
    const std::string path =
        args.contains("path") && args["path"].is_string()
            ? args["path"].get<std::string>()
            : "out.png";
    code = "tusdrender.render(" + nlohmann::json(path).dump() + ")";
  } else if (name == "bounds") {
    code = "tusdrender.bounds()";
  } else if (name == "stats") {
    code = "tusdrender.stats()";
  } else {
    err = "unknown tool: " + name;
    return false;
  }
  if (!tinyusdz::tydra::RunJSScript(engine, code, out, err)) {
    return false;  // err set
  }
  return true;
}

nlohmann::json McpToolList() {
  auto tool = [](const char *n, const char *desc, nlohmann::json props,
                 std::vector<std::string> req) {
    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = props;
    if (!req.empty()) schema["required"] = req;
    return nlohmann::json{{"name", n}, {"description", desc},
                          {"inputSchema", schema}};
  };
  auto vec3 = []() {
    return nlohmann::json{{"type", "array"},
                          {"items", {{"type", "number"}}},
                          {"minItems", 3},
                          {"maxItems", 3}};
  };
  auto num = []() { return nlohmann::json{{"type", "number"}}; };
  nlohmann::json tools = nlohmann::json::array();
  tools.push_back(tool("eval",
                       "Run JavaScript against the persistent render context "
                       "(tusdrender.* API). Returns the script value.",
                       {{"script", {{"type", "string"}}}}, {"script"}));
  tools.push_back(tool("set_camera",
                       "Position the camera with eye+target (+optional fov deg).",
                       {{"eye", vec3()}, {"target", vec3()}, {"fov", num()}},
                       {"eye", "target"}));
  tools.push_back(tool("orbit",
                       "Orbit the camera around the scene center.",
                       {{"azimuth", num()}, {"elevation", num()},
                        {"distance", num()}},
                       {}));
  tools.push_back(tool("set_time",
                       "Re-evaluate geometry/transforms at a time code (omit "
                       "for the default time) and rebuild the BVH.",
                       {{"time", num()}}, {}));
  tools.push_back(tool("set_resolution", "Set output image resolution.",
                       {{"width", num()}, {"height", num()}}, {}));
  tools.push_back(tool("render",
                       "Render the current camera to a PNG path (reuses the BVH).",
                       {{"path", {{"type", "string"}}}}, {"path"}));
  tools.push_back(tool("bounds", "Get the scene bounding box.",
                       nlohmann::json::object(), {}));
  tools.push_back(tool("stats", "Get triangle/BVH/resolution stats.",
                       nlohmann::json::object(), {}));
  return tools;
}

int RunMCPMode(const Options &opt) {
  RenderContext ctx;
  if (!BuildRenderContext(opt, ctx)) return EXIT_FAILURE;
  if (opt.stats) PrintRTStats(ctx);

  tinyusdz::tydra::JSEngineState engine;
  std::string err;
  if (!tinyusdz::tydra::InitJSEngine(engine, err)) {
    std::cerr << "JS engine init failed: " << err << "\n";
    return EXIT_FAILURE;
  }
  qjs::RegisterTusdrenderModule(engine, &ctx);
  std::cerr << "tusdrender MCP server ready (stdio JSON-RPC). Tools: eval, "
               "set_camera, orbit, set_resolution, render, bounds, stats.\n";

  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty()) continue;
    nlohmann::json req;
    try {
      req = nlohmann::json::parse(line);
    } catch (...) {
      continue;
    }
    const std::string method = req.value("method", std::string());
    const bool has_id = req.contains("id");
    nlohmann::json resp;
    resp["jsonrpc"] = "2.0";
    if (has_id) resp["id"] = req["id"];
    auto send = [&]() {
      std::cout << resp.dump() << "\n";
      std::cout.flush();
    };

    if (method == "initialize") {
      resp["result"] = {
          {"protocolVersion", "2024-11-05"},
          {"serverInfo", {{"name", "tusdrender"}, {"version", "1.0"}}},
          {"capabilities", {{"tools", nlohmann::json::object()}}}};
      send();
    } else if (method == "tools/list") {
      resp["result"] = {{"tools", McpToolList()}};
      send();
    } else if (method == "tools/call") {
      const auto params = req.value("params", nlohmann::json::object());
      const std::string tname = params.value("name", std::string());
      const auto targs = params.value("arguments", nlohmann::json::object());
      nlohmann::json out;
      std::string e2;
      const bool ok = McpCallTool(engine, tname, targs, out, e2);
      if (!ok) {
        resp["result"] = {
            {"isError", true},
            {"content", nlohmann::json::array(
                            {{{"type", "text"}, {"text", e2}}})}};
      } else {
        resp["result"] = {
            {"content", nlohmann::json::array(
                            {{{"type", "text"}, {"text", out.dump()}}})}};
      }
      send();
    } else if (has_id) {
      resp["error"] = {{"code", -32601}, {"message", "method not found: " + method}};
      send();
    }
    // notifications (no id) other than the above are ignored.
  }

  tinyusdz::tydra::DestroyJSEngine(engine);
  qjs::g_render_ctx = nullptr;
  return EXIT_SUCCESS;
}
#endif  // TINYUSDZ_WITH_QJS

}  // namespace

// ---------------------------------------------------------------------------
// LightRT Vulkan backend: uses the LightRT C API (lightrt_c_vk.h) for GPU
// BVH traversal. Builds the scene with the existing CPU builder, uploads to
// GPU, traces camera rays, then shades hits on the CPU.
// ---------------------------------------------------------------------------
#ifdef HAVE_VULKAN
#include "lightrt_c_vk.h"

static bool RunVulkanLightRT(const Options &opt,
                              const std::vector<Vec3> &base_colors,
                              const std::vector<RTPreviewStats::MeshGeometry> &geos,
                              const CameraFrame &camera, int height) {
  // Build an lrt_tri_scene from the geometry.
  // Flatten all meshes into a single vertex/index array that LightRT expects.
  std::vector<float> flat_verts;
  std::vector<uint32_t> flat_idx;
  std::vector<Vec3> mesh_base_colors;
  std::vector<Vec3> mesh_normals;  // per-triangle flat normals for shading
  uint32_t base_idx = 0;
  for (const auto &g : geos) {
    uint32_t nv = uint32_t(g.positions.size() / 3);
    for (uint32_t j = 0; j < nv; ++j) {
      flat_verts.push_back(g.positions[j * 3 + 0]);
      flat_verts.push_back(g.positions[j * 3 + 1]);
      flat_verts.push_back(g.positions[j * 3 + 2]);
    }
    for (uint32_t j = 0; j < uint32_t(g.indices.size()); ++j) {
      flat_idx.push_back(g.indices[j] + base_idx);
    }
    // Store per-triangle shading data.
    for (uint32_t j = 0; j < uint32_t(g.indices.size()) / 3; ++j) {
      uint32_t i0 = g.indices[j * 3 + 0];
      uint32_t i1 = g.indices[j * 3 + 1];
      uint32_t i2 = g.indices[j * 3 + 2];
      // Face normal.
      Vec3 p0{g.positions[i0 * 3 + 0], g.positions[i0 * 3 + 1], g.positions[i0 * 3 + 2]};
      Vec3 p1{g.positions[i1 * 3 + 0], g.positions[i1 * 3 + 1], g.positions[i1 * 3 + 2]};
      Vec3 p2{g.positions[i2 * 3 + 0], g.positions[i2 * 3 + 1], g.positions[i2 * 3 + 2]};
      Vec3 e1 = Sub(p1, p0), e2 = Sub(p2, p0);
      Vec3 n = Normalize(Cross(e1, e2));
      mesh_normals.push_back(n);
    }
    size_t nm = (base_colors.size() > geos.size()) ? geos.size() : base_colors.size();
    Vec3 bc = (&g - &geos[0]) < nm ? base_colors[&g - &geos[0]] : Vec3{0.5f, 0.5f, 0.5f};
    for (uint32_t j = 0; j < uint32_t(g.indices.size()) / 3; ++j) {
      mesh_base_colors.push_back(bc);
    }
    base_idx += nv;
  }

  uint32_t ntris = uint32_t(flat_idx.size() / 3);
  if (ntris == 0) {
    std::cerr << "No triangles to render.\n";
    return false;
  }

  // Build the BVH scene.
  lrt_tri_build_options bopts;
  std::memset(&bopts, 0, sizeof(bopts));
  bopts.quality = LRT_TRI_BUILD_DEFAULT;
  bopts.layout = LRT_TRI_LAYOUT_BVH4;
  bopts.num_threads = 1;

  lrt_result lrterr = LRT_RESULT_OK;
  lrt_tri_scene *scene =
      lrt_tri_scene_build(flat_verts.data(), ntris, &bopts, &lrterr);
  if (!scene || lrterr != LRT_RESULT_OK) {
    std::cerr << "Failed to build LightRT scene.\n";
    return false;
  }

  // Create the Vulkan engine.
  lrt_vk_engine_options vopts;
  std::memset(&vopts, 0, sizeof(vopts));
  vopts.device_index = -1;
  vopts.prefer_discrete = 1;
  vopts.want_ray_tracing = opt.vulkan_rt;
  lrt_result vkerr = LRT_RESULT_OK;
  lrt_vk_engine *vk = lrt_vk_engine_create(&vopts, &vkerr);
  if (!vk) {
    std::cerr << "Failed to create LightRT Vulkan engine.\n";
    lrt_tri_scene_free(scene);
    return false;
  }

  uint32_t vk_caps = lrt_vk_engine_caps(vk);
  bool has_rt = (vk_caps & LRT_VK_CAP_RAY_QUERY) != 0;
  std::cerr << "Vulkan device: " << lrt_vk_engine_device_name(vk) << "\n";
  std::cerr << "Vulkan caps: compute=1"
            << ((vk_caps & LRT_VK_CAP_BUFFER_ADDRESS) ? " buf_addr" : "")
            << ((vk_caps & LRT_VK_CAP_ACCEL_STRUCT) ? " accel" : "")
            << ((vk_caps & LRT_VK_CAP_RAY_QUERY) ? " ray_query" : "")
            << "\n";

  // Render — per-pixel tracing with flat shading.
  const int w = opt.width > 0 ? opt.width : 960;
  const int h = height;
  tinyusdz::Image img;
  img.width = w;
  img.height = h;
  img.channels = 4;
  img.bpp = 8;
  img.data.resize(size_t(w) * size_t(h) * 4, 0);

  Vec3 eye = camera.origin;
  Vec3 fwd = camera.forward;
  Vec3 up = camera.up;
  Vec3 right = Normalize(Cross(fwd, up));
  Vec3 camUp = Cross(right, fwd);
  float aspect = float(w) / float(h);
  float fov = camera.yfov;
  float half_h = std::tan(fov * 0.5f);
  float half_w = half_h * aspect;
  const int spp = std::max(1, opt.samples);
  const float ambient = opt.ambient;
  const float light_dir[3] = {0.5f, 0.8f, 0.6f};

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      Vec3 color{0, 0, 0};
      for (int s = 0; s < spp; ++s) {
        float fx = (float(x) + 0.5f) / float(w) * 2.0f - 1.0f;
        float fy = (float(y) + 0.5f) / float(h) * 2.0f - 1.0f;
        Vec3 dir = Add(Mul(right, fx * half_w), Add(Mul(camUp, fy * half_h), fwd));
        dir = Normalize(dir);

        lrt_ray ray;
        ray.org[0] = eye.x; ray.org[1] = eye.y; ray.org[2] = eye.z;
        ray.tmin = 0.001f;
        ray.dir[0] = dir.x; ray.dir[1] = dir.y; ray.dir[2] = dir.z;
        ray.tmax = 1.0e10f;

        lrt_hit hit;
        lrt_result trerr;
        int traced;
        if (has_rt && opt.vulkan_rt) {
          traced = lrt_vk_trace_scene_rtx(vk, flat_verts.data(), ntris,
                                           &ray, 1, &hit, &trerr);
        } else {
          traced = lrt_vk_trace_scene(vk, scene, &ray, 1, &hit, &trerr);
        }
        if (traced > 0 && hit.prim_id != 0xFFFFFFFFu && hit.prim_id < ntris) {
          Vec3 bc = hit.prim_id < mesh_base_colors.size()
                        ? mesh_base_colors[hit.prim_id]
                        : Vec3{0.5f, 0.5f, 0.5f};
          Vec3 N = hit.prim_id < mesh_normals.size()
                       ? mesh_normals[hit.prim_id]
                       : Vec3{0, 1, 0};
          float diff = std::max(0.0f, Dot(N, Vec3{light_dir[0], light_dir[1], light_dir[2]}));
          Vec3 shaded = Add(Mul(bc, diff), Mul(bc, ambient));
          color = Add(color, shaded);
        }
      }
      color = Mul(color, 1.0f / float(spp));
      size_t pi = (size_t(y) * size_t(w) + size_t(x)) * 4;
      img.data[pi + 0] = uint8_t(std::min(255.0f, color.x * 255.0f));
      img.data[pi + 1] = uint8_t(std::min(255.0f, color.y * 255.0f));
      img.data[pi + 2] = uint8_t(std::min(255.0f, color.z * 255.0f));
      img.data[pi + 3] = 255;
    }
  }

  // Write PNG.
  tinyusdz::image::WriteOption wopt;
  wopt.format = tinyusdz::image::WriteImageFormat::Autodetect;
  auto ret = tinyusdz::image::WriteImageToFile(opt.output, img, wopt);
  if (!ret) {
    std::cerr << "Failed to write PNG: " << ret.error() << "\n";
    lrt_tri_scene_free(scene);
    lrt_vk_engine_destroy(vk);
    return false;
  }

  std::cerr << "triangles: " << ntris << " (" << geos.size() << " meshes)\n";
  std::cerr << "backend: LightRT VK ("
            << (has_rt && opt.vulkan_rt ? "ray_query" : "compute trace")
            << ")\n";
  lrt_tri_scene_free(scene);
  lrt_vk_engine_destroy(vk);
  return true;
}
#endif

int main(int argc, char **argv) {
  Options opt;
  if (!ParseArgs(argc, argv, &opt)) {
    return EXIT_FAILURE;
  }

  // Configure the process memory budget: -maxMem <GiB>, else auto
  // min(32 GiB, 0.5 * system MemAvailable). Keeps tusdrender from being
  // OOM-killed on huge (instance-expanded) scenes; it aborts with a clear
  // message instead.
  MemBudget::Get().Init(opt.max_mem_gib);
  if (opt.stats) {
    std::cerr << "memory cap: " << MemBudget::GiB(MemBudget::Get().Cap());
    size_t avail = MemBudget::AvailableSystemMemory();
    if (avail) std::cerr << " (system avail: " << MemBudget::GiB(avail) << ")";
    std::cerr << "\n";
  }

  // Interactive / scripted modes (memory-persistent rendering over the next
  // loader): -js runs a JavaScript animation/control script, -mcp runs an MCP
  // stdio control server. Both keep the scene + BVH resident for repeated
  // re-rendering.
  if (opt.mcp || !opt.js_script.empty()) {
#ifdef TINYUSDZ_WITH_QJS
    if (opt.mcp) return RunMCPMode(opt);
    return RunJSScriptMode(opt, opt.js_script);
#else
    std::cerr << "-js/-mcp require building with -DTINYUSDZ_WITH_QJS=ON.\n";
    return EXIT_FAILURE;
#endif
  }

  // RT preview backend: the `next` lazy loader (fast, low-memory compose +
  // mmap USDC; also handles .usdz + .usda). Falls back to the legacy eager
  // loader for other inputs or when -legacyLoad is requested.
  if (opt.rt_preview && !opt.legacy_load) {
    return RunRTPreviewNext(opt);
  }

#ifdef HAVE_VULKAN
  // Vulkan backend: loads the scene through the `next` lazy loader, then
  // renders via Vulkan rasterizer or ray tracer.
  if (opt.vulkan) {
    // Load through next loader.
    tinyusdz::next::Stage stage;
    std::string warn, err;
    tinyusdz::next::pcp::CompositionOptions comp_opts;
    if (!opt.variant_overrides.empty())
      comp_opts.variant_overrides = opt.variant_overrides;
    if (!tinyusdz::next::LoadUSDComposed(opt.input, &stage, &warn, &err,
                                         &comp_opts)) {
      if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";
      std::cerr << "Failed to load USD: " << err << "\n";
      return EXIT_FAILURE;
    }
    if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";

    // Collect meshes and build geometry.
    std::vector<tinyusdz::next::UsdPrim> mesh_prims;
    std::vector<matrix4d> worlds;
    std::vector<Vec3> base_colors;
    std::vector<int32_t> tex_ids;
    std::vector<float> roughnesses;
    std::vector<float> metallics;
    std::vector<RTPreviewStats::MeshGeometry> geos;

    {
      // Traverse and collect meshes.
      std::vector<tinyusdz::next::UsdPrim> mesh_stack;
      for (const auto &root : stage.GetRootPrims()) {
        std::vector<tinyusdz::next::UsdPrim> stack;
        stack.push_back(root);
        while (!stack.empty()) {
          auto prim = stack.back();
          stack.pop_back();
          if (prim.GetTypeName() == "Mesh") {
            mesh_stack.push_back(prim);
            Vec3 bc{0.5f, 0.5f, 0.5f};
            // Try to get displayColor.
            const tinyusdz::next::Value *dcv = prim.GetPropertyValue("primvars:displayColor");
            if (dcv) {
              const std::vector<float> *dc = dcv->as_float_array();
              if (dc && dc->size() >= 3) {
                bc = Vec3{(*dc)[0], (*dc)[1], (*dc)[2]};
              }
            }
            base_colors.push_back(bc);
            tex_ids.push_back(-1);
            roughnesses.push_back(0.5f);
            metallics.push_back(0.0f);
          }
          for (const auto &child : prim.GetChildren()) {
            stack.push_back(child);
          }
        }
      }

      // Stream geometry.
      for (auto &prim : mesh_stack) {
        RTPreviewStats::MeshGeometry geo;
        uint32_t nv = 0;
        const tinyusdz::next::Value *val = prim.GetPropertyValue("points");
        if (!val) continue;
        const std::vector<float> *pts = val->as_float_array();
        if (!pts || pts->empty()) continue;
        nv = uint32_t(pts->size() / 3);
        geo.positions = *pts;

        val = prim.GetPropertyValue("normals");
        if (val) {
          const std::vector<float> *nrm = val->as_float_array();
          if (nrm && nrm->size() >= nv * 3)
            geo.normals = *nrm;
        }
        if (geo.normals.empty()) {
          geo.normals.resize(nv * 3, 0);
        }

        val = prim.GetPropertyValue("primvars:st");
        if (val) {
          const std::vector<float> *uv = val->as_float_array();
          if (uv && !uv->empty()) {
            geo.uvs = *uv;
          }
        }
        if (geo.uvs.empty()) geo.uvs.resize(nv * 2, 0);

        val = prim.GetPropertyValue("faceVertexIndices");
        if (val) {
          const std::vector<int> *idx = val->as_int_array();
          if (idx && !idx->empty()) {
            geo.indices.assign(idx->begin(), idx->end());
          }
        }

        geos.push_back(std::move(geo));
      }
    }

    if (geos.empty()) {
      std::cerr << "No renderable geometry found.\n";
      return EXIT_FAILURE;
    }

    // Resolve camera.
    CameraFrame camera;
    double up_axis = 1.0; // Y-up
    {
      std::string up = stage.GetUpAxis();
      if (up == "Z") up_axis = 2.0;
      else if (up == "X") up_axis = 0.0;
    }
    tinyusdz::Axis usdUp = (up_axis == 2.0) ? tinyusdz::Axis::Z
                           : (up_axis == 0.0) ? tinyusdz::Axis::X
                           : tinyusdz::Axis::Y;

    Bounds bounds;
    for (const auto &g : geos) {
      for (size_t j = 0; j < g.positions.size() / 3; ++j) {
        bounds.lo.x = std::min(bounds.lo.x, g.positions[j * 3 + 0]);
        bounds.lo.y = std::min(bounds.lo.y, g.positions[j * 3 + 1]);
        bounds.lo.z = std::min(bounds.lo.z, g.positions[j * 3 + 2]);
        bounds.hi.x = std::max(bounds.hi.x, g.positions[j * 3 + 0]);
        bounds.hi.y = std::max(bounds.hi.y, g.positions[j * 3 + 1]);
        bounds.hi.z = std::max(bounds.hi.z, g.positions[j * 3 + 2]);
      }
    }
    bounds.valid = true;
    int out_height = opt.height > 0 ? opt.height : 540;

    Options auto_opt = opt;
    auto_opt.camera.clear();
    if (opt.autoframe) {
      camera = MakeCameraFrame({}, auto_opt, bounds, out_height, usdUp);
    } else {
      camera.origin = Vec3{0, 0, 5};
      camera.forward = Normalize(Sub(Vec3{0, 0, 0}, camera.origin));
      camera.up = Vec3{0, 1, 0};
      camera.yfov = 45.0f * 3.14159265f / 180.0f;
    }

    if (!RunVulkanLightRT(opt, base_colors, geos, camera, out_height)) {
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }
#endif

  tinyusdz::Stage stage;
  std::string warn;
  std::string err;
  tinyusdz::USDLoadOptions load_options;
  load_options.mmap_zero_copy = opt.rt_preview;
  load_options.max_memory_limit_in_mb = opt.rt_preview ? 65536 : 16384;
  load_options.load_assets = !opt.rt_preview;
  if (opt.progress) {
    load_options.progress_callback = LoadProgress;
  }
  const auto load_t0 = std::chrono::steady_clock::now();
  if (!tinyusdz::LoadUSDFromFile(opt.input, &stage, &warn, &err, load_options)) {
    if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";
    std::cerr << "Failed to load USD: " << err << "\n";
    return EXIT_FAILURE;
  }
  const auto load_t1 = std::chrono::steady_clock::now();
  const double load_seconds =
      std::chrono::duration<double>(load_t1 - load_t0).count();
  if (!warn.empty()) {
    std::cerr << "WARN: " << warn << "\n";
  }

  if (opt.rt_preview) {
    if (tinyusdz::IsUSDC(opt.input) && !stage.has_mmap_zero_copy()) {
      std::cerr << "RT preview requires mmap zero-copy metadata for USDC input. "
                << "Write flattened USDC without --compress-float-arrays.\n";
      return EXIT_FAILURE;
    }

    std::vector<float> vertices;
    std::vector<TriInfo> tris;
    Bounds bounds;
    RTPreviewStats rt_stats;
    std::string rt_err;
    if (!BuildRTPreviewScene(stage, opt, &vertices, &tris, &bounds, &rt_stats,
                             &rt_err)) {
      if (opt.stats) {
        std::cerr << "rt preview: 1\n";
        std::cerr << "rt meshes: " << rt_stats.meshes << "\n";
        std::cerr << "rt skipped meshes: " << rt_stats.skipped_meshes << "\n";
        std::cerr << "rt mmap point meshes: "
                  << rt_stats.meshes_with_mmap_points << "\n";
        std::cerr << "rt owned point meshes: "
                  << rt_stats.meshes_with_owned_points << "\n";
        std::cerr << "rt mmap deferred bytes: "
                  << rt_stats.mmap_deferred_bytes << "\n";
        std::cerr << "rt copied point bytes: " << rt_stats.copied_point_bytes
                  << "\n";
        std::cerr << "rt copied topology bytes: "
                  << rt_stats.copied_topology_bytes << "\n";
        std::cerr << "rt purpose default triangles: "
                  << rt_stats.purpose_default_triangles << "\n";
        std::cerr << "rt purpose render triangles: "
                  << rt_stats.purpose_render_triangles << "\n";
        std::cerr << "rt purpose proxy triangles: "
                  << rt_stats.purpose_proxy_triangles << "\n";
        std::cerr << "rt purpose guide triangles: "
                  << rt_stats.purpose_guide_triangles << "\n";
      }
      std::cerr << rt_err << "\n";
      return EXIT_FAILURE;
    }

    lrt_tri_build_options build_opts;
    std::memset(&build_opts, 0, sizeof(build_opts));
    build_opts.quality = opt.quality;
    build_opts.layout = LRT_TRI_LAYOUT_AUTO;
    build_opts.max_leaf_size = 0;
    build_opts.num_threads = WorkerThreadCount(opt.threads);

    const auto bvh_t0 = std::chrono::steady_clock::now();
    lrt_result lrt_err = LRT_RESULT_OK;
    lrt_tri_scene *lrt_scene =
        lrt_tri_scene_build(vertices.data(), tris.size(), &build_opts, &lrt_err);
    const auto bvh_t1 = std::chrono::steady_clock::now();
    if (!lrt_scene) {
      std::cerr << "Failed to build LightRT scene (err=" << int(lrt_err) << ").\n";
      return EXIT_FAILURE;
    }

    int height = opt.height > 0 ? opt.height : 540;
    RenderScene empty_render_scene;
    CameraFrame camera;
    if (!FindStageCameraFrame(stage, opt.camera, opt.timecode, &camera)) {
      if (!opt.camera.empty()) {
        std::cerr << "WARN: Camera not found: " << opt.camera
                  << ". Using auto-fit camera.\n";
      }
      Options auto_opt = opt;
      auto_opt.camera.clear();
      camera = MakeCameraFrame(empty_render_scene, auto_opt, bounds, height,
                               stage.metas().upAxis.get_value());
    }
    DirectScene direct_scene;
    LightCache light_cache;
    IblCache ibl_cache;

    if (opt.stats) {
      lrt_tri_stats st;
      std::memset(&st, 0, sizeof(st));
      lrt_tri_scene_stats(lrt_scene, &st);
      double bvh_seconds =
          std::chrono::duration<double>(bvh_t1 - bvh_t0).count();
      std::cerr << "rt preview: 1\n";
      std::cerr << "rt meshes: " << rt_stats.meshes << "\n";
      std::cerr << "rt skipped meshes: " << rt_stats.skipped_meshes << "\n";
      std::cerr << "rt mmap point meshes: "
                << rt_stats.meshes_with_mmap_points << "\n";
      std::cerr << "rt owned point meshes: "
                << rt_stats.meshes_with_owned_points << "\n";
      std::cerr << "rt mmap deferred bytes: "
                << rt_stats.mmap_deferred_bytes << "\n";
      std::cerr << "rt copied point bytes: " << rt_stats.copied_point_bytes
                << "\n";
      std::cerr << "rt copied topology bytes: "
                << rt_stats.copied_topology_bytes << "\n";
      std::cerr << "rt purpose default triangles: "
                << rt_stats.purpose_default_triangles << "\n";
      std::cerr << "rt purpose render triangles: "
                << rt_stats.purpose_render_triangles << "\n";
      std::cerr << "rt purpose proxy triangles: "
                << rt_stats.purpose_proxy_triangles << "\n";
      std::cerr << "rt purpose guide triangles: "
                << rt_stats.purpose_guide_triangles << "\n";
      std::cerr << "rt packed triangle bytes: "
                << rt_stats.packed_triangle_bytes << "\n";
      std::cerr << "load seconds: " << load_seconds << "\n";
      std::cerr << "rt triangle stream seconds: " << rt_stats.build_seconds
                << "\n";
      std::cerr << "rt bvh build seconds: " << bvh_seconds << "\n";
      std::cerr << "triangles: " << tris.size() << "\n";
      std::cerr << "lightrt: " << lrt_tri_kernel_name(lrt_scene) << "\n";
      std::cerr << "bvh nodes: " << st.node_count << ", leaves: "
                << st.leaf_count << ", memory: " << st.memory_bytes
                << " bytes\n";
    }

    const auto render_t0 = std::chrono::steady_clock::now();
    tinyusdz::Image img =
        RenderImage(lrt_scene, &direct_scene, tris, light_cache, nullptr, camera,
                    opt, height);
    const auto render_t1 = std::chrono::steady_clock::now();
    if (opt.stats) {
      std::cerr << "render seconds: "
                << std::chrono::duration<double>(render_t1 - render_t0).count()
                << "\n";
    }
    lrt_tri_scene_free(lrt_scene);

    tinyusdz::image::WriteOption wopt;
    wopt.format = tinyusdz::image::WriteImageFormat::Autodetect;
    auto ret = tinyusdz::image::WriteImageToFile(opt.output, img, wopt);
    if (!ret) {
      std::cerr << "Failed to write image: " << ret.error() << "\n";
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }

  tinyusdz::tydra::RenderScene render_scene;
  tinyusdz::tydra::RenderSceneConverter converter;
  tinyusdz::tydra::RenderSceneConverterEnv env(stage);
  env.timecode = opt.timecode;
  env.mesh_config.triangulate = !opt.direct_prims;
  env.mesh_config.subdivision_level = opt.subdivision_level;
  env.mesh_config.build_vertex_indices = !opt.direct_prims;
  env.mesh_config.compute_tangents_and_binormals = false;
  env.scene_config.load_texture_assets = true;
  env.set_search_paths({tinyusdz::io::GetBaseDir(opt.input)});
  if (opt.no_assetresolver) {
    SetupNullAssetResolution(&env.asset_resolver);
  }
  if (!converter.ConvertToRenderScene(env, &render_scene)) {
    std::cerr << "Failed to convert USD Stage to RenderScene:\n"
              << converter.GetError() << "\n";
    return EXIT_FAILURE;
  }
  std::string converter_warn = converter.GetWarning();
  if (!converter_warn.empty()) {
    std::cerr << "WARN: " << converter_warn << "\n";
  }

  std::vector<float> vertices;
  std::vector<TriInfo> tris;
  Bounds bounds;
  DirectScene direct_scene;
  LightCache light_cache;
  if (opt.direct_prims) {
    std::string direct_err;
    if (!BuildDirectScene(stage, render_scene, opt, &vertices, &tris, &bounds,
                          &direct_scene, &direct_err)) {
      std::cerr << direct_err << "\n";
      return EXIT_FAILURE;
    }
  }
  CollectAllGeometry(render_scene, &vertices, &tris, &bounds,
                     opt.direct_prims ? &direct_scene.direct_paths : nullptr,
                     &light_cache);
  const bool has_direct = direct_scene.spheres || direct_scene.round_curves ||
                          direct_scene.flat_curves || direct_scene.points ||
                          direct_scene.bez_curves || direct_scene.tets ||
                          !direct_scene.shapes.empty();
  if (tris.empty() && !has_direct) {
    std::cerr << "No renderable geometry found.\n";
    return EXIT_FAILURE;
  }

  lrt_tri_build_options build_opts;
  std::memset(&build_opts, 0, sizeof(build_opts));
  build_opts.quality = opt.quality;
  build_opts.layout = LRT_TRI_LAYOUT_AUTO;
  build_opts.max_leaf_size = 0;
  build_opts.num_threads = WorkerThreadCount(opt.threads);

  lrt_result lrt_err = LRT_RESULT_OK;
  lrt_tri_scene *lrt_scene = nullptr;
  if (!tris.empty()) {
    lrt_scene = lrt_tri_scene_build(vertices.data(), tris.size(), &build_opts, &lrt_err);
    if (!lrt_scene) {
      std::cerr << "Failed to build LightRT scene (err=" << int(lrt_err) << ").\n";
      return EXIT_FAILURE;
    }
  }

  int height = opt.height;
  if (height <= 0) {
    height = 540;
    const Node *cam_node = FindCameraNode(render_scene, opt.camera);
    if (cam_node) {
      const RenderCamera &cam = render_scene.cameras[size_t(cam_node->id)];
      if (cam.verticalAspectRatio > 0.0f) {
        height = std::max(1, int(std::round(float(opt.width) *
                                           cam.verticalAspectRatio)));
      }
    }
  }

  tinyusdz::Axis up_axis = tinyusdz::Axis::Y;
  if (render_scene.meta.upAxis == "X") {
    up_axis = tinyusdz::Axis::X;
  } else if (render_scene.meta.upAxis == "Z") {
    up_axis = tinyusdz::Axis::Z;
  }
  CameraFrame camera = MakeCameraFrame(render_scene, opt, bounds, height,
                                       up_axis);
  CollectLights(render_scene, &light_cache);
  IblCache ibl_cache;
  BuildIblCache(render_scene, light_cache, &ibl_cache);

  if (opt.stats) {
    lrt_tri_stats st;
    std::memset(&st, 0, sizeof(st));
    if (lrt_scene) lrt_tri_scene_stats(lrt_scene, &st);
    std::cerr << "triangles: " << tris.size() << "\n";
    std::cerr << "direct spheres: " << direct_scene.sphere_info.size() << "\n";
    std::cerr << "direct round curve segments: "
              << direct_scene.round_curve_info.size() << "\n";
    std::cerr << "direct flat curve segments: "
              << direct_scene.flat_curve_info.size() << "\n";
    std::cerr << "direct Hermite/Bezier curve segments: "
              << direct_scene.bez_curve_info.size() << "\n";
    std::cerr << "direct points: " << direct_scene.point_info.size() << "\n";
    std::cerr << "direct tetrahedra: " << direct_scene.tet_prims.size()
              << "\n";
    std::cerr << "direct analytic shapes: " << direct_scene.shapes.size()
              << "\n";
    std::cerr << "subdivision level: " << opt.subdivision_level << "\n";
    std::cerr << "lights: " << light_cache.finite.size() << "\n";
    std::cerr << "mesh light triangles: " << light_cache.mesh.size() << "\n";
    std::cerr << "domelight: " << (light_cache.has_dome ? 1 : 0) << "\n";
    std::cerr << "light sampling finite cdf entries: "
              << light_cache.finite_cdf.size() << "\n";
    std::cerr << "light sampling mesh cdf entries: "
              << light_cache.mesh_cdf.size() << "\n";
    std::cerr << "light sampling env cdf entries: "
              << light_cache.env_cdf.size() << "\n";
    std::cerr << "ibl envmap: " << (ibl_cache.valid ? 1 : 0) << "\n";
    std::cerr << "ibl diffuse size: "
              << (ibl_cache.diffuse.width * ibl_cache.diffuse.height) << "\n";
    std::cerr << "ibl prefilter levels: " << ibl_cache.prefiltered.size()
              << "\n";
    std::cerr << "ibl brdf lut size: "
              << (ibl_cache.brdf_size * ibl_cache.brdf_size) << "\n";
    if (lrt_scene) std::cerr << "lightrt: " << lrt_tri_kernel_name(lrt_scene) << "\n";
    std::cerr << "bvh nodes: " << st.node_count << ", leaves: " << st.leaf_count
              << ", memory: " << st.memory_bytes << " bytes\n";
    std::cerr << "load seconds: " << load_seconds << "\n";
  }

  const auto render_t0 = std::chrono::steady_clock::now();
  tinyusdz::Image img =
      RenderImage(lrt_scene, &direct_scene, tris, light_cache,
                  ibl_cache.valid ? &ibl_cache : nullptr, camera, opt, height);
  const auto render_t1 = std::chrono::steady_clock::now();
  if (opt.stats) {
    std::cerr << "render seconds: "
              << std::chrono::duration<double>(render_t1 - render_t0).count()
              << "\n";
  }
  if (lrt_scene) lrt_tri_scene_free(lrt_scene);

  tinyusdz::image::WriteOption wopt;
  wopt.format = tinyusdz::image::WriteImageFormat::Autodetect;
  auto ret = tinyusdz::image::WriteImageToFile(opt.output, img, wopt);
  if (!ret) {
    std::cerr << "Failed to write image: " << ret.error() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
