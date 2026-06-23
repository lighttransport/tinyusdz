// SPDX-License-Identifier: Apache-2.0
// tusdrender — shared low-level types: memory budget/pool, Vec3/Bounds, the
// per-triangle/material records, the pooled-vector typedefs, and the inline
// vector/matrix math. Leaf header (no other tusdr_* deps).
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <vector>
#if defined(__linux__)
#include <malloc.h>
#include <unistd.h>
#endif

#include "value-types.hh"
#include "xform.hh"

namespace tusdr {

using tinyusdz::value::color3f;
using tinyusdz::value::float3;
using tinyusdz::value::matrix4d;

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

// Inverse of CombineTriMat: pull the shading parameters out of a full TriInfo so
// a curve BLAS can store one TriMat per material instead of replicating ~64 B of
// material on every hair segment (the geometry p0/p1 stay per-segment; p2/n are
// synthetic/recomputed). Mirrors the mesh TriStore + TriMat split.
inline TriMat ExtractTriMat(const TriInfo &t) {
  TriMat m;
  m.base_color = t.base_color;
  m.emission = t.emission;
  m.roughness = t.roughness;
  m.metallic = t.metallic;
  m.tex_id = t.tex_id;
  m.normal_tex_id = t.normal_tex_id;
  m.rough_tex_id = t.rough_tex_id;
  m.metal_tex_id = t.metal_tex_id;
  m.emission_tex_id = t.emission_tex_id;
  m.occ_tex_id = t.occ_tex_id;
  m.occlusion = t.occlusion;
  m.opacity = t.opacity;
  m.rough_ch = t.rough_ch;
  m.metal_ch = t.metal_ch;
  m.occ_ch = t.occ_ch;
  return m;
}

inline bool SameTriMat(const TriMat &a, const TriMat &b) {
  return a.base_color.x == b.base_color.x && a.base_color.y == b.base_color.y &&
         a.base_color.z == b.base_color.z && a.emission.x == b.emission.x &&
         a.emission.y == b.emission.y && a.emission.z == b.emission.z &&
         a.roughness == b.roughness && a.metallic == b.metallic &&
         a.tex_id == b.tex_id && a.normal_tex_id == b.normal_tex_id &&
         a.rough_tex_id == b.rough_tex_id && a.metal_tex_id == b.metal_tex_id &&
         a.emission_tex_id == b.emission_tex_id && a.occ_tex_id == b.occ_tex_id &&
         a.occlusion == b.occlusion && a.opacity == b.opacity &&
         a.rough_ch == b.rough_ch && a.metal_ch == b.metal_ch &&
         a.occ_ch == b.occ_ch;
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
// Per-corner displayColor/Opacity stored as RGBA8 (12 B/tri vs 48 B as float):
// the rendered difference is <=1/255 (1 LSB), well below output precision.
using ByteVec = std::vector<uint8_t, PoolAlloc<uint8_t>>;
using TriVec = std::vector<TriInfo, PoolAlloc<TriInfo>>;
// Indexed geometry (Phase 2): unique vertex ids, 3 per triangle, into a BLAS's
// uverts array. Lets the stream hand 1x unique verts + indices to the indexed
// LightRT build instead of a 3x-expanded soup.
using IdxVec = std::vector<uint32_t, PoolAlloc<uint32_t>>;
using TriStoreVec = std::vector<TriStore, PoolAlloc<TriStore>>;

inline float Dot(const Vec3 &a, const Vec3 &b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 Cross(const Vec3 &a, const Vec3 &b) {
  return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
              a.x * b.y - a.y * b.x};
}

inline Vec3 Add(const Vec3 &a, const Vec3 &b) {
  return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3 Sub(const Vec3 &a, const Vec3 &b) {
  return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3 Mul(const Vec3 &a, float s) { return Vec3{a.x * s, a.y * s, a.z * s}; }

inline Vec3 Mul(const Vec3 &a, const Vec3 &b) {
  return Vec3{a.x * b.x, a.y * b.y, a.z * b.z};
}

inline Vec3 Div(const Vec3 &a, float s) {
  if (std::abs(s) <= 1.0e-20f) return Vec3{0.0f, 0.0f, 0.0f};
  return Vec3{a.x / s, a.y / s, a.z / s};
}

inline float Length(const Vec3 &v) { return std::sqrt(std::max(0.0f, Dot(v, v))); }

inline float Luminance(const Vec3 &v) {
  return 0.2126f * v.x + 0.7152f * v.y + 0.0722f * v.z;
}

inline Vec3 Normalize(const Vec3 &v) {
  float len = Length(v);
  if (len <= 1.0e-20f) {
    return Vec3{0.0f, 0.0f, 0.0f};
  }
  return Mul(v, 1.0f / len);
}

inline Vec3 Clamp01(const Vec3 &v) {
  return Vec3{std::max(0.0f, std::min(1.0f, v.x)),
              std::max(0.0f, std::min(1.0f, v.y)),
              std::max(0.0f, std::min(1.0f, v.z))};
}

inline Vec3 Lerp(const Vec3 &a, const Vec3 &b, float t) {
  return Add(Mul(a, 1.0f - t), Mul(b, t));
}

inline Vec3 Reflect(const Vec3 &v, const Vec3 &n) {
  return Sub(v, Mul(n, 2.0f * Dot(v, n)));
}

inline float ClampFloat(float v, float lo, float hi) {
  return std::max(lo, std::min(hi, v));
}

inline Vec3 FromFloat3(const float3 &v) { return Vec3{v[0], v[1], v[2]}; }
inline Vec3 FromPoint3(const tinyusdz::value::point3f &v) {
  return Vec3{v[0], v[1], v[2]};
}
inline Vec3 FromVector3(const tinyusdz::value::vector3f &v) {
  return Vec3{v[0], v[1], v[2]};
}

inline Vec3 TransformPoint(const matrix4d &m, const Vec3 &p) {
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

inline Vec3 TransformVector(const matrix4d &m, const Vec3 &v) {
  return Vec3{
      float(m.m[0][0] * double(v.x) + m.m[1][0] * double(v.y) +
            m.m[2][0] * double(v.z)),
      float(m.m[0][1] * double(v.x) + m.m[1][1] * double(v.y) +
            m.m[2][1] * double(v.z)),
      float(m.m[0][2] * double(v.x) + m.m[1][2] * double(v.y) +
            m.m[2][2] * double(v.z))};
}

inline void Expand(Bounds *b, const Vec3 &p) {
  if (!b) return;
  b->lo.x = std::min(b->lo.x, p.x);
  b->lo.y = std::min(b->lo.y, p.y);
  b->lo.z = std::min(b->lo.z, p.z);
  b->hi.x = std::max(b->hi.x, p.x);
  b->hi.y = std::max(b->hi.y, p.y);
  b->hi.z = std::max(b->hi.z, p.z);
  b->valid = true;
}

}  // namespace tusdr
