// SPDX-License-Identifier: Apache-2.0
// tusdview — view-dependent district LOD pre-pass (see lod_stream.hh). Mirrors
// tools/tusdrender/tusdr_lod.cc: compose the proxy scene, walk it at prim
// granularity to aggregate per-district world bounds + proxy vert counts (the
// viewer's --next DrawScene merges meshes, losing district granularity, so we
// walk the next::Stage directly), rank by view importance, promote the nearest
// under host/VRAM budgets, and emit a wrapper layer the viewer then loads.
#include "lod_stream.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid
#include <stdlib.h>   // _fullpath, _MAX_PATH
#else
#include <climits>   // PATH_MAX
#include <unistd.h>  // realpath, getpid
#endif

#include "hipew.h"                     // HIP VRAM query
#include "next/lightusd-next.hh"       // next::Stage, LoadUSDComposed, Value
#include "next_scene_loader.hh"        // FindNextCamera, NextCameraPose
#include "tydra/next/scene-access.hh"  // ComputeLocalTransform, HasResetXformStack
#include "value-types.hh"             // value::matrix4d

namespace tusdview {

namespace {

namespace tnext = lightusd::next;
using matrix4d = lightusd::value::matrix4d;
constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;

struct District {
  std::string path, name;
  double verts = 0.0;
  float bbmin[3] = {1e30f, 1e30f, 1e30f};
  float bbmax[3] = {-1e30f, -1e30f, -1e30f};
  bool has_bounds = false;
  float dist = 0.0f;
  float align = 1.0f;
  double score = 0.0;
  bool full = false;
};

// Row-major matrix helpers matching next_scene_loader.cc (row-vector p*M; world =
// local applied first, i.e. local * parent).
matrix4d Mat4dFromArray(const double d[16]) {
  matrix4d m;
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) m.m[i][j] = d[i * 4 + j];
  return m;
}
matrix4d Mul4(const matrix4d& a, const matrix4d& b) {
  matrix4d r;
  for (int j = 0; j < 4; ++j)
    for (int i = 0; i < 4; ++i) {
      double v = 0.0;
      for (int k = 0; k < 4; ++k) v += a.m[j][k] * b.m[k][i];
      r.m[j][i] = v;
    }
  return r;
}
void XformPoint(const matrix4d& m, float x, float y, float z, float o[3]) {
  o[0] = float(x * m.m[0][0] + y * m.m[1][0] + z * m.m[2][0] + m.m[3][0]);
  o[1] = float(x * m.m[0][1] + y * m.m[1][1] + z * m.m[2][1] + m.m[3][1]);
  o[2] = float(x * m.m[0][2] + y * m.m[1][2] + z * m.m[2][2] + m.m[3][2]);
}

bool DistrictOf(const std::string& path, const std::string& container,
                std::string* district_path, std::string* name) {
  const std::string needle = "/" + container + "/";
  size_t c = path.find(needle);
  if (c == std::string::npos) return false;
  size_t start = c + needle.size();
  size_t end = path.find('/', start);
  if (end == std::string::npos) end = path.size();
  if (end <= start) return false;
  *name = path.substr(start, end - start);
  *district_path = path.substr(0, end);
  return true;
}

// Recursively walk the composed stage, accumulating world transforms, and add
// each Mesh's world-AABB + vert count to its district.
void WalkDistricts(const tnext::UsdPrim& prim, const matrix4d& parent_world,
                   double time, const std::string& container,
                   std::map<std::string, District>* districts) {
  if (!prim.IsActive()) return;
  double dmat[16];
  lightusd::tydra::next::ComputeLocalTransform(prim, dmat, time);
  const matrix4d local = Mat4dFromArray(dmat);
  const bool reset = lightusd::tydra::next::HasResetXformStack(prim);
  const matrix4d world = reset ? local : Mul4(local, parent_world);

  if (prim.GetTypeName() == "Mesh") {
    std::string dpath, dname;
    if (DistrictOf(prim.GetPath().str(), container, &dpath, &dname)) {
      const tnext::Value* val = prim.GetPropertyValue("points");
      const std::vector<float>* pts = val ? val->as_float_array() : nullptr;
      if (pts && !pts->empty()) {
        const size_t nv = pts->size() / 3;
        float lmin[3] = {1e30f, 1e30f, 1e30f}, lmax[3] = {-1e30f, -1e30f, -1e30f};
        for (size_t i = 0; i < nv; ++i)
          for (int k = 0; k < 3; ++k) {
            float v = (*pts)[i * 3 + k];
            lmin[k] = std::min(lmin[k], v);
            lmax[k] = std::max(lmax[k], v);
          }
        District& d = (*districts)[dpath];
        if (d.path.empty()) { d.path = dpath; d.name = dname; }
        d.verts += double(nv);
        for (int c = 0; c < 8; ++c) {
          float w[3];
          XformPoint(world, (c & 1) ? lmax[0] : lmin[0],
                     (c & 2) ? lmax[1] : lmin[1], (c & 4) ? lmax[2] : lmin[2], w);
          for (int k = 0; k < 3; ++k) {
            d.bbmin[k] = std::min(d.bbmin[k], w[k]);
            d.bbmax[k] = std::max(d.bbmax[k], w[k]);
          }
        }
        d.has_bounds = true;
      }
    }
  }
  // Mirror tusdrender: do not descend into a PointInstancer (its prototype
  // geometry is placed separately; counting it here would misplace it).
  if (prim.GetTypeName() == "PointInstancer") return;
  for (const tnext::UsdPrim& child : prim.GetChildren())
    WalkDistricts(child, world, time, container, districts);
}

std::string AbsolutePath(const std::string& p) {
#ifdef _WIN32
  char buf[_MAX_PATH];
  if (_fullpath(buf, p.c_str(), _MAX_PATH)) return std::string(buf);
#else
  char buf[PATH_MAX];
  if (realpath(p.c_str(), buf)) return std::string(buf);
#endif
  return p;
}
std::string TempDir() {
#ifdef _WIN32
  const char* t = std::getenv("TEMP");
  if (!t || !*t) t = std::getenv("TMP");
  if (t && *t) return std::string(t);
  return ".";
#else
  const char* t = std::getenv("TMPDIR");
  if (t && *t) return std::string(t);
  return "/tmp";
#endif
}
size_t ReadMeminfoBytes(const char* path, const char* key) {
  std::ifstream f(path);
  std::string tok;
  while (f >> tok) {
    if (tok == key) {
      size_t kb = 0;
      f >> kb;
      return kb * 1024;
    }
    std::getline(f, tok);
  }
  return 0;
}
size_t HipVramBytes() {
  if (hipewInit(HIPEW_INIT_HIP) != HIPEW_SUCCESS) return 0;
  if (!hipInit || hipInit(0) != hipSuccess) return 0;
  if (!hipSetDevice || hipSetDevice(0) != hipSuccess) return 0;
  size_t freeB = 0, totalB = 0;
  if (!hipMemGetInfo || hipMemGetInfo(&freeB, &totalB) != hipSuccess) return 0;
  return totalB;
}

}  // namespace

std::string PrepareLodStream(const std::string& input, const LodStreamOptions& o) {
  // 1) Compose the proxy scene (authored districtLod=proxy selections).
  tnext::Stage stage;
  std::string warn, err;
  if (!tnext::LoadUSDComposed(input, &stage, &warn, &err, nullptr)) {
    std::cerr << "[lodStream] proxy compose failed: " << err << "\n";
    return "";
  }

  // 2) Reference camera (else scene centre).
  NextCameraPose cam;
  const bool have_cam =
      !o.camera.empty() && FindNextCamera(stage, o.camera, o.time, &cam);

  // 3) Walk the stage and aggregate per district.
  std::map<std::string, District> districts;
  for (const auto& root : stage.GetRootPrims())
    WalkDistricts(root, matrix4d::identity(), o.time, o.container, &districts);
  if (districts.empty()) {
    std::cerr << "[lodStream] no districts under '" << o.container
              << "' -- loading scene as authored.\n";
    return "";
  }

  float cam_ref[3] = {0, 0, 0};
  if (have_cam) {
    for (int k = 0; k < 3; ++k) cam_ref[k] = cam.eye[k];
  } else {
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (auto& kv : districts)
      for (int k = 0; k < 3; ++k) {
        lo[k] = std::min(lo[k], kv.second.bbmin[k]);
        hi[k] = std::max(hi[k], kv.second.bbmax[k]);
      }
    for (int k = 0; k < 3; ++k) cam_ref[k] = 0.5f * (lo[k] + hi[k]);
    std::cerr << "[lodStream] camera '" << o.camera
              << "' not resolved; ranking by distance to scene centre.\n";
  }

  // 4) Rank by projected coverage (verts / dist^2) weighted by view alignment^2.
  std::vector<District> ranked;
  int n_skipped = 0;
  for (auto& kv : districts) {
    District d = kv.second;
    if (d.verts < o.minVerts) { ++n_skipped; continue; }
    float centre[3], to[3];
    for (int k = 0; k < 3; ++k) {
      centre[k] = 0.5f * (d.bbmin[k] + d.bbmax[k]);
      to[k] = centre[k] - cam_ref[k];
    }
    d.dist = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
    d.align = (have_cam && d.dist > 1e-3f)
                  ? (to[0] * cam.forward[0] + to[1] * cam.forward[1] +
                     to[2] * cam.forward[2]) / d.dist
                  : 1.0f;
    const double dist2 = double(d.dist) * double(d.dist) + 1.0;
    const double a = d.align > 0.0f ? double(d.align) : 0.0;
    d.score = (d.verts / dist2) * a * a;
    ranked.push_back(d);
  }
  std::sort(ranked.begin(), ranked.end(),
            [](const District& a, const District& b) { return a.score > b.score; });
  if (ranked.empty()) {
    std::cerr << "[lodStream] no districts above " << o.minVerts
              << " proxy verts -- loading scene as authored.\n";
    return "";
  }

  // 5) Budgets + greedy nearest-first promotion (always promote at least one).
  const double host_avail = double(ReadMeminfoBytes("/proc/meminfo", "MemAvailable:"));
  const double host_budget =
      o.maxMemGiB > 0.0 ? o.maxMemGiB * kGiB : 0.5 * host_avail;
  const double vram_total = double(HipVramBytes());
  const double vram_budget =
      o.maxVramGiB > 0.0 ? o.maxVramGiB * kGiB
                         : (vram_total > 0.0 ? 0.5 * vram_total : 1e30);
  const double cost_host = o.districtMemGiB * kGiB;
  const double cost_vram = o.districtVramGiB * kGiB;
  double host_used = double(ReadMeminfoBytes("/proc/self/status", "VmRSS:"));
  double vram_used = 0.0;
  int n_full = 0;
  for (size_t i = 0; i < ranked.size(); ++i) {
    if (i == 0 || (host_used + cost_host <= host_budget &&
                   vram_used + cost_vram <= vram_budget)) {
      ranked[i].full = true;
      host_used += cost_host;
      vram_used += cost_vram;
      ++n_full;
    }
  }

  std::cerr << "[lodStream] budgets: host " << (host_budget / kGiB)
            << " GiB (avail " << (host_avail / kGiB) << "), vram "
            << (vram_budget / kGiB) << " GiB (device " << (vram_total / kGiB)
            << ")\n[lodStream] " << ranked.size() << " districts (" << n_skipped
            << " sub-threshold), promoting " << n_full << " to full\n";
  for (const District& d : ranked) {
    std::cerr << "[lodStream]   " << (d.full ? "FULL  " : "proxy ") << d.name
              << "  score=" << d.score << "  dist=" << d.dist
              << "  align=" << d.align << "  proxyVerts=" << uint64_t(d.verts)
              << "\n";
  }

  // 6) Write the wrapper: subLayer the original + promote each chosen district.
  std::vector<const District*> full;
  for (const District& d : ranked)
    if (d.full) full.push_back(&d);
  if (full.empty()) return "";

  std::vector<std::string> prefix;
  {
    const std::string& p = full[0]->path;
    size_t pos = 0;
    while (pos < p.size()) {
      if (p[pos] == '/') {
        size_t e = p.find('/', pos + 1);
        if (e == std::string::npos) e = p.size();
        std::string comp = p.substr(pos + 1, e - pos - 1);
        if (comp == full[0]->name) break;
        if (!comp.empty()) prefix.push_back(comp);
        pos = e;
      } else {
        ++pos;
      }
    }
  }

  const std::string abs_input = AbsolutePath(input);
#ifdef _WIN32
  const int pid = _getpid();
#else
  const int pid = getpid();
#endif
  const std::string wrapper =
      TempDir() + "/tusdview_lod_" + std::to_string(pid) + ".usda";
  std::ofstream ofs(wrapper);
  if (!ofs) {
    std::cerr << "[lodStream] cannot write wrapper " << wrapper << "\n";
    return "";
  }
  ofs << "#usda 1.0\n(\n    subLayers = [\n        @" << abs_input
      << "@\n    ]\n    upAxis = \"" << stage.GetUpAxis() << "\"\n)\n\n";
  std::string indent;
  for (const std::string& comp : prefix) {
    ofs << indent << "over \"" << comp << "\"\n" << indent << "{\n";
    indent += "    ";
  }
  for (const District* d : full) {
    ofs << indent << "over \"" << d->name << "\" (\n"
        << indent << "    variants = {\n"
        << indent << "        string districtLod = \"full\"\n"
        << indent << "    }\n" << indent << ")\n" << indent << "{\n"
        << indent << "}\n";
  }
  for (size_t i = 0; i < prefix.size(); ++i) {
    indent.resize(indent.size() - 4);
    ofs << indent << "}\n";
  }
  ofs.close();

  std::cerr << "[lodStream] wrote wrapper " << wrapper << " (" << full.size()
            << " full districts); loading it.\n";
  return wrapper;
}

}  // namespace tusdview
