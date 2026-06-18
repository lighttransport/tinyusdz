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
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "asset-resolution.hh"
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
#include "next/schema/geom-mesh.hh"
#include "next/stage/stage.hh"
#include "next/tinyusdz-next.hh"
#include "next/types/value.hh"
#include "tydra/next/scene-access.hh"

extern "C" {
#include "lightrt_c_tri.h"
}

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
  bool progress{false};
  lrt_tri_quality quality{LRT_TRI_BUILD_DEFAULT};
  int threads{0};
  int subdivision_level{0};
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
      << "  -rtPreview             Use mmap zero-copy mesh preview path for large USDC.\n"
      << "  -progress              Print long-running load/build progress.\n"
      << "  -quality <fast|default|hq>\n"
      << "                         LightRT BVH build quality (default default).\n"
      << "  -threads <N>           LightRT build threads (0 = backend default).\n"
      << "  -subdiv <N>            Subdivision level for Mesh subdivisionScheme\n"
      << "                         catmullClark/loop/bilinear (default 0).\n"
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
  if (positional.size() != 2) {
    PrintUsage(argv[0]);
    return false;
  }
  opt->input = positional[0];
  opt->output = positional[1];
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

bool BuildIblCache(const RenderScene &scene, const LightCache &lights,
                   IblCache *ibl) {
  if (!ibl || !lights.has_dome) return false;
  EnvImage env;
  if (!DecodeTextureToEnvImage(scene, lights.dome.texture_id, &env)) {
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
  Vec3 o = Add(p, Mul(n, 1.0e-4f));
  lrt_ray ray;
  ray.org[0] = o.x;
  ray.org[1] = o.y;
  ray.org[2] = o.z;
  ray.tmin = 1.0e-4f;
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

Vec3 Shade(lrt_tri_scene *scene, const DirectScene *direct,
           const std::vector<TriInfo> &tris,
           const LightCache &lights, const IblCache *ibl,
           const CameraFrame &camera,
           const Options &opt, const Vec3 &ray_org, const Vec3 &ray_dir) {
  lrt_ray ray;
  ray.org[0] = ray_org.x;
  ray.org[1] = ray_org.y;
  ray.org[2] = ray_org.z;
  ray.tmin = camera.znear;
  ray.dir[0] = ray_dir.x;
  ray.dir[1] = ray_dir.y;
  ray.dir[2] = ray_dir.z;
  ray.tmax = camera.zfar;
  lrt_hit hit;
  bool tri_hit =
      IntersectVisibleTriangles(scene, tris, ray, opt.purpose_mask, &hit) &&
      hit.prim_id != LRT_TRI_NO_HIT && size_t(hit.prim_id) < tris.size();
  float best_t = tri_hit ? hit.t : camera.zfar;
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
    tri = tris[size_t(hit.prim_id)];
  }
  Vec3 p = Add(ray_org, Mul(ray_dir, hit_t));
  Vec3 n = tri.n;
  if (Dot(n, ray_dir) > 0.0f) {
    n = Mul(n, -1.0f);
  }
  Vec3 c = Add(Mul(tri.base_color, opt.ambient), tri.emission);
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
    c = Add(c, Add(Mul(Mul(tri.base_color, diffuse), kd), spec));
  } else if (lights.has_dome) {
    c = Add(c, Mul(Mul(tri.base_color, lights.env_color), 0.25f));
  }
  if (lights.finite.empty() && lights.mesh.empty()) {
    Vec3 l = Normalize(Sub(camera.origin, p));
    float ndotl = std::max(0.0f, Dot(n, l));
    if (ndotl > 0.0f &&
        (!opt.shadows ||
         !Occluded(scene, tris, p, n, l,
                   std::max(0.0f, Length(Sub(camera.origin, p)) - 1.0e-3f),
                   direct, opt.purpose_mask))) {
      c = Add(c, Mul(tri.base_color, ndotl));
    }
    return c;
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
    if (opt.shadows &&
        Occluded(scene, tris, p, n, l, max_t, direct, opt.purpose_mask)) {
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
  return c;
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
                            int height) {
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
            color = Add(color, Shade(scene, direct, tris, lights, ibl, camera,
                                     opt, org, dir));
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

void AddRTPreviewMeshNext(const tinyusdz::next::UsdPrim &prim,
                          const matrix4d &world, tinyusdz::Purpose purpose,
                          uint32_t purpose_mask, std::vector<float> *vertices,
                          std::vector<TriInfo> *tris, Bounds *bounds,
                          RTPreviewStats *stats) {
  tinyusdz::next::UsdGeomMesh mesh(prim);
  if (!mesh.IsValid()) return;
  const std::vector<float> points = mesh.GetPoints();           // flat xyz
  const std::vector<int> counts = mesh.GetFaceVertexCounts();
  const std::vector<int> indices = mesh.GetFaceVertexIndices();
  if (points.empty() || counts.empty() || indices.empty()) {
    stats->skipped_meshes++;
    return;
  }
  const size_t npts = points.size() / 3;

  // Note: no per-mesh reserve() here. `vertices`/`tris` are the shared output
  // buffers accumulated across all meshes; reserving size()+delta per mesh
  // would defeat amortized growth and reallocate the (multi-GB) buffer on every
  // mesh. Plain append relies on the vector's geometric growth.
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
}

struct MeshJobNext {
  tinyusdz::next::UsdPrim prim;
  matrix4d world{matrix4d::identity()};
  tinyusdz::Purpose purpose{tinyusdz::Purpose::Default};
};

// Serial walk: resolve parent-dependent world matrices + purpose and flatten
// Mesh prims into jobs. Transform evaluation (which materializes xformOp Values
// in place) stays serial; the per-mesh geometry read + triangulation runs in
// parallel afterwards on disjoint Mesh prims.
void CollectRTPreviewMeshesNext(const tinyusdz::next::Stage &stage,
                                const tinyusdz::next::UsdPrim &prim,
                                const matrix4d &parent_world,
                                tinyusdz::Purpose inherited_purpose,
                                std::vector<MeshJobNext> *jobs) {
  double dmat[16];
  tinyusdz::tydra::next::ComputeLocalTransform(prim, dmat, 0.0);
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

  if (prim.GetTypeName() == "Mesh") {
    MeshJobNext job;
    job.prim = prim;
    job.world = world;
    job.purpose = purpose;
    jobs->push_back(std::move(job));
  }
  for (const tinyusdz::next::UsdPrim &child : prim.GetChildren()) {
    CollectRTPreviewMeshesNext(stage, child, world, purpose, jobs);
  }
}

int RunRTPreviewNext(const Options &opt) {
  const auto load_t0 = std::chrono::steady_clock::now();
  tinyusdz::next::Stage stage;
  std::string warn;
  std::string err;
  if (!tinyusdz::next::LoadUSD(opt.input, &stage, &warn, &err)) {
    if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";
    std::cerr << "Failed to load USD (next): " << err << "\n";
    return EXIT_FAILURE;
  }
  if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";
  const auto load_t1 = std::chrono::steady_clock::now();
  const double load_seconds =
      std::chrono::duration<double>(load_t1 - load_t0).count();

  std::vector<float> vertices;
  std::vector<TriInfo> tris;
  Bounds bounds;
  RTPreviewStats rt_stats;
  const auto stream_t0 = std::chrono::steady_clock::now();

  // Pass A (serial): flatten Mesh prims into jobs with resolved world matrices.
  std::vector<MeshJobNext> jobs;
  for (const tinyusdz::next::UsdPrim &root : stage.GetRootPrims()) {
    CollectRTPreviewMeshesNext(stage, root, matrix4d::identity(),
                               tinyusdz::Purpose::Default, &jobs);
  }
  rt_stats.meshes = jobs.size();

  // Pass B (parallel): read geometry + triangulate each mesh into its own
  // result buffer (disjoint Mesh prims -> distinct lazy Values, no shared
  // mutable state in DecodeCrateArray). Work-stealing via an atomic cursor.
  struct MeshResultNext {
    std::vector<float> vertices;
    std::vector<TriInfo> tris;
    Bounds bounds;
    RTPreviewStats stats;
  };
  std::vector<MeshResultNext> results(jobs.size());
  const unsigned nthreads =
      std::min<unsigned>(WorkerThreadCount(opt.threads),
                         jobs.empty() ? 1u : unsigned(jobs.size()));
  std::atomic<size_t> cursor{0};
  auto worker = [&]() {
    for (;;) {
      const size_t i = cursor.fetch_add(1, std::memory_order_relaxed);
      if (i >= jobs.size()) break;
      MeshJobNext &job = jobs[i];
      MeshResultNext &r = results[i];
      AddRTPreviewMeshNext(job.prim, job.world, job.purpose, opt.purpose_mask,
                           &r.vertices, &r.tris, &r.bounds, &r.stats);
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

  // Pass C (serial merge): concatenate in job order (deterministic output).
  size_t total_floats = 0, total_tris = 0;
  for (const MeshResultNext &r : results) {
    total_floats += r.vertices.size();
    total_tris += r.tris.size();
  }
  vertices.reserve(total_floats);
  tris.reserve(total_tris);
  for (MeshResultNext &r : results) {
    vertices.insert(vertices.end(), r.vertices.begin(), r.vertices.end());
    tris.insert(tris.end(), r.tris.begin(), r.tris.end());
    MergeBounds(&bounds, r.bounds);
    MergeStats(&rt_stats, r.stats);
    std::vector<float>().swap(r.vertices);
    std::vector<TriInfo>().swap(r.tris);
  }

  const auto stream_t1 = std::chrono::steady_clock::now();
  rt_stats.build_seconds =
      std::chrono::duration<double>(stream_t1 - stream_t0).count();
  rt_stats.packed_triangle_bytes = uint64_t(vertices.size()) * sizeof(float);
  if (tris.empty()) {
    std::cerr << "RT preview (next) found no renderable Mesh triangles.\n";
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

  const int height = opt.height > 0 ? opt.height : 540;
  tinyusdz::Axis up_axis = tinyusdz::Axis::Y;
  if (stage.GetUpAxis() == "X") up_axis = tinyusdz::Axis::X;
  else if (stage.GetUpAxis() == "Z") up_axis = tinyusdz::Axis::Z;
  if (!opt.camera.empty()) {
    std::cerr << "WARN: named camera (" << opt.camera
              << ") is not supported with the next loader; using auto-fit. "
              << "Use -legacyLoad for named cameras.\n";
  }
  RenderScene empty_render_scene;
  Options auto_opt = opt;
  auto_opt.camera.clear();
  CameraFrame camera =
      MakeCameraFrame(empty_render_scene, auto_opt, bounds, height, up_axis);

  DirectScene direct_scene;
  LightCache light_cache;

  if (opt.stats) {
    lrt_tri_stats st;
    std::memset(&st, 0, sizeof(st));
    lrt_tri_scene_stats(lrt_scene, &st);
    const double bvh_seconds =
        std::chrono::duration<double>(bvh_t1 - bvh_t0).count();
    std::cerr << "rt preview: 1\n";
    std::cerr << "rt loader: next\n";
    std::cerr << "rt meshes: " << rt_stats.meshes << "\n";
    std::cerr << "rt skipped meshes: " << rt_stats.skipped_meshes << "\n";
    std::cerr << "rt purpose default triangles: "
              << rt_stats.purpose_default_triangles << "\n";
    std::cerr << "rt purpose render triangles: "
              << rt_stats.purpose_render_triangles << "\n";
    std::cerr << "rt purpose proxy triangles: "
              << rt_stats.purpose_proxy_triangles << "\n";
    std::cerr << "rt purpose guide triangles: "
              << rt_stats.purpose_guide_triangles << "\n";
    std::cerr << "rt packed triangle bytes: " << rt_stats.packed_triangle_bytes
              << "\n";
    std::cerr << "load seconds: " << load_seconds << "\n";
    std::cerr << "rt triangle stream seconds: " << rt_stats.build_seconds
              << "\n";
    std::cerr << "rt bvh build seconds: " << bvh_seconds << "\n";
    std::cerr << "triangles: " << tris.size() << "\n";
    std::cerr << "lightrt: " << lrt_tri_kernel_name(lrt_scene) << "\n";
    std::cerr << "bvh nodes: " << st.node_count << ", leaves: " << st.leaf_count
              << ", memory: " << st.memory_bytes << " bytes\n";
  }

  const auto render_t0 = std::chrono::steady_clock::now();
  tinyusdz::Image img = RenderImage(lrt_scene, &direct_scene, tris, light_cache,
                                    nullptr, camera, opt, height);
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

}  // namespace

int main(int argc, char **argv) {
  Options opt;
  if (!ParseArgs(argc, argv, &opt)) {
    return EXIT_FAILURE;
  }

  // Default RT preview backend: the `next` lazy loader (fast, low-memory USDC
  // parse). Falls back to the legacy eager loader for non-USDC inputs or when
  // -legacyLoad is requested.
  if (opt.rt_preview && !opt.legacy_load && tinyusdz::IsUSDC(opt.input)) {
    return RunRTPreviewNext(opt);
  }

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
