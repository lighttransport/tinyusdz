// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Lean Emscripten binding for the "next" core: USD load/compose (next_core) +
// tydra_next RenderScene extraction, exposed as `TinyUSDZLoaderNative` with the
// SAME JS-facing method shapes as the legacy web/binding.cc, so the existing
// three.js loader (web/js/src/tinyusdz/{TinyUSDZLoader,TinyUSDZLoaderUtils,
// TinyUSDZWorker}.js) drives this module unchanged.
//
// Covered so far:
//  - lifecycle, loadFromBinary (USDA/USDC/USDZ), scene-graph node tree with
//    world transforms
//  - mesh geometry: getMeshPtr (zero-copy heap descriptors) + getMeshCopy
//    (owned typed arrays) — points, triangulated indices, normals, uv0
//  - materials: getMaterial/getMaterialWithFormat("json") in the SAME JSON
//    schema as legacy tydra::serializeMaterial — flat UsdPreviewSurface
//    (surfaceShader + *TextureId) AND nested OpenPBR (openPBR.<layer>.<param>)
//  - textures: getTexture full metadata (uv transform, bias/scale, wrap,
//    channel, image id); getImageCopy/getImagePtr expose each image's RAW
//    ENCODED bytes (png/jpg/...) pulled straight from the USDZ archive with
//    bufferId>=0 so the three.js loader Blob-decodes PNG/JPEG itself.
//  - image decode: decodeEXR / decodeHDR module functions (tinyexr v3 C
//    backend for EXR, stb_image for HDR) so the JS EXR/HDR texture path
//    decodes in-wasm. Instancing, skinning, and external-ref composition
//    remain follow-up increments.
//
// tydra_next is being refactored elsewhere — this binding consumes it
// read-only and adds nothing to it.

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "next/tinyusdz-next.hh"  // LoadUSDFromMemory (USDA/USDC/USDZ auto-detect)
#include "next/reader/usdz-reader.hh"  // USDZReader (raw archive entry access)
#include "next/stage/stage.hh"
#include "tydra/next/render-converter.hh"
#include "tydra/next/render-data.hh"

#if defined(TINYUSDZ_NEXT_WITH_EXR)
#include "exr.h"  // tinyexr v3 pure-C11 backend (src/external/tinyexr/include)
// stb_image HDR (Radiance RGBE) decoder — implemented in stb_hdr.cc.
extern "C" float* stbi_loadf_from_memory(const unsigned char* buffer, int len,
                                         int* x, int* y, int* channels_in_file,
                                         int desired_channels);
extern "C" void stbi_image_free(void* retval_from_stbi_load);
extern "C" int stbi_is_hdr_from_memory(const unsigned char* buffer, int len);
#endif

using emscripten::typed_memory_view;
using emscripten::val;
namespace tn = tinyusdz::next;
namespace td = tinyusdz::tydra::next;

namespace {

// Owned JS typed arrays (copy out of a contiguous C++ buffer).
val f32_owned(const float* p, size_t n) {
  val a = val::global("Float32Array").new_(n);
  if (n) a.call<void>("set", val(typed_memory_view(n, p)));
  return a;
}
val u32_owned(const uint32_t* p, size_t n) {
  val a = val::global("Uint32Array").new_(n);
  if (n) a.call<void>("set", val(typed_memory_view(n, p)));
  return a;
}

const char* node_type_str(td::NodeType t) {
  switch (t) {
    case td::NodeType::Xform: return "xform";
    case td::NodeType::Mesh: return "mesh";
    case td::NodeType::PointInstancer: return "pointinstancer";
    case td::NodeType::Camera: return "camera";
    case td::NodeType::Skeleton: return "skeleton";
    default: return "light";
  }
}

const char* wrap_str(td::WrapMode w) {
  switch (w) {
    case td::WrapMode::Clamp: return "clamp";
    case td::WrapMode::Mirror: return "mirror";
    case td::WrapMode::Black: return "black";
    default: return "repeat";
  }
}

val mat16(const td::Matrix4& m) {
  val a = val::array();
  for (int i = 0; i < 16; ++i) a.set(i, val(static_cast<double>(m.m[i])));
  return a;
}

// Minimal JSON escaping for names/paths.
std::string jesc(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 2);
  for (char c : s) {
    if (c == '"' || c == '\\') { o.push_back('\\'); o.push_back(c); }
    else if (c == '\n') o += "\\n";
    else o.push_back(c);
  }
  return o;
}
std::string jnum(float v) {
  // Compact, JSON.parse-safe float print.
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v));
  return buf;
}

// Normalize a USD asset path for archive lookup: drop a leading "./" or "/".
std::string norm_asset(const std::string& s) {
  size_t b = 0;
  if (s.size() >= 2 && s[0] == '.' && s[1] == '/') b = 2;
  else if (!s.empty() && s[0] == '/') b = 1;
  return s.substr(b);
}

// Find the archive entry for an asset path: exact match, else the entry whose
// name equals the asset's normalized form or shares its basename.
int findZipEntry(const tinyusdz::next::USDZReader& zip, const std::string& asset) {
  const std::string want = norm_asset(asset);
  const size_t slash = want.find_last_of('/');
  const std::string base = (slash == std::string::npos) ? want : want.substr(slash + 1);
  int base_match = -1;
  for (size_t i = 0; i < zip.NumEntries(); ++i) {
    const std::string& e = zip.EntryName(i);
    if (e == want || norm_asset(e) == want) return static_cast<int>(i);
    const size_t es = e.find_last_of('/');
    const std::string eb = (es == std::string::npos) ? e : e.substr(es + 1);
    if (eb == base && base_match < 0) base_match = static_cast<int>(i);
  }
  return base_match;
}

const char* mime_from_path(const std::string& s) {
  auto ends = [&](const char* ext) {
    const size_t n = std::strlen(ext);
    return s.size() >= n &&
           std::equal(ext, ext + n, s.end() - n,
                      [](char a, char b) { return std::tolower(a) == std::tolower(b); });
  };
  if (ends(".png")) return "image/png";
  if (ends(".jpg") || ends(".jpeg")) return "image/jpeg";
  if (ends(".exr")) return "image/x-exr";
  if (ends(".hdr")) return "image/vnd.radiance";
  if (ends(".ktx") || ends(".ktx2")) return "image/ktx2";
  if (ends(".tga")) return "image/x-tga";
  if (ends(".bmp")) return "image/bmp";
  return "application/octet-stream";
}

}  // namespace

// One loaded stage's composed RenderScene + zero-copy heap caches.
class TinyUSDZLoaderNative {
 public:
  TinyUSDZLoaderNative() = default;

  // ---- lifecycle ----------------------------------------------------------
  bool ok() const { return loaded_; }
  std::string error() const { return error_; }
  std::string warn() const { return warn_; }
  void setMaxMemoryLimitMB(int mb) { max_mem_mb_ = mb; }  // reserved; no-op here
  void reset() {
    loaded_ = false; error_.clear(); warn_.clear();
    scene_ = td::RenderScene(); mesh_heap_.clear(); image_bytes_.clear();
  }

  // ---- load ---------------------------------------------------------------
  bool loadFromBinary(const std::string& binary, const std::string& /*filename*/) {
    reset();
    tn::Stage stage;
    std::string w, e;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(binary.data());
    if (!tn::LoadUSDFromMemory(p, binary.size(), &stage, &w, &e)) {
      error_ = e.empty() ? "failed to load USD" : e;
      warn_ = w;
      return false;
    }
    // For a USDZ, hand the converter a texture loader that pulls each image's
    // RAW ENCODED bytes (png/jpg/...) straight from the archive (no filesystem,
    // no in-wasm image decode). getImageCopy exposes them with encoded=true +
    // mimeType so the JS/app decodes via the browser (createImageBitmap).
    td::ConverterConfig cfg;
    tn::USDZReader zip;
    const bool is_usdz = binary.size() >= 4 && p[0] == 'P' && p[1] == 'K' &&
                         p[2] == 0x03 && p[3] == 0x04;
    if (is_usdz && zip.Open(p, binary.size())) {
      cfg.material.custom_texture_loader =
          [&zip](const std::string& asset, td::TextureImage* out) -> bool {
            const int idx = findZipEntry(zip, asset);
            if (idx < 0) return false;
            const uint8_t* d = zip.EntryData(static_cast<size_t>(idx));
            const size_t n = zip.EntrySize(static_cast<size_t>(idx));
            if (!d || n == 0) return false;
            out->data.append(d, n);       // encoded bytes (not decoded pixels)
            out->resolved_path = asset;
            out->width = 0; out->height = 0;  // unknown until the JS decodes
            return true;
          };
    }
    td::RenderSceneConverter conv(cfg);
    td::ConvertResult res = conv.Convert(stage);
    warn_ = w;
    for (const auto& cw : res.warnings) { warn_ += cw; warn_ += '\n'; }
    if (!res.success) {
      error_ = res.error.empty() ? "RenderScene conversion failed" : res.error;
      return false;
    }
    scene_ = std::move(res.scene);
    loaded_ = true;
    return true;
  }

  // ---- scene graph --------------------------------------------------------
  int numRootNodes() const { return static_cast<int>(scene_.root_nodes.size()); }
  int getDefaultRootNodeId() const {
    return scene_.root_nodes.empty() ? -1 : scene_.root_nodes[0];
  }
  std::string getUpAxis() const {
    // tydra_next bakes up-axis into transforms; report Y (three.js default).
    return "Y";
  }
  val getSceneMetadata() const {
    val o = val::object();
    o.set("upAxis", val(getUpAxis()));
    o.set("numMeshes", val(static_cast<int>(scene_.meshes.size())));
    o.set("numMaterials", val(static_cast<int>(scene_.materials.size())));
    return o;
  }
  val getDefaultRootNode() const { return getRootNode(getDefaultRootNodeId()); }
  val getRootNode(int node_id) const { return buildNode(node_id); }

  // ---- meshes -------------------------------------------------------------
  int numMeshes() const { return static_cast<int>(scene_.meshes.size()); }

  // Owned, retain-safe copy (the JS getMeshCopy fallback / skinning path).
  val getMeshCopy(int mesh_id) {
    if (mesh_id < 0 || mesh_id >= numMeshes()) return val::null();
    const td::RenderMesh& m = scene_.meshes[mesh_id];
    const MeshHeap& h = ensureMeshHeap(mesh_id);
    val o = val::object();
    o.set("primName", val(m.name));
    o.set("displayName", val(m.name));
    o.set("absPath", val(m.prim_path));
    o.set("points", f32_owned(h.points.data(), h.points.size()));
    o.set("faceVertexIndices", u32_owned(h.indices.data(), h.indices.size()));
    o.set("faceVertexCounts", u32_owned(h.fvc.data(), h.fvc.size()));
    if (!h.normals.empty()) {
      o.set("normals", f32_owned(h.normals.data(), h.normals.size()));
      o.set("normalsFormat", val(std::string("float32")));
    }
    if (!h.uv0.empty()) {
      o.set("texcoords", f32_owned(h.uv0.data(), h.uv0.size()));
      val sets = val::object(), s0 = val::object();
      s0.set("data", f32_owned(h.uv0.data(), h.uv0.size()));
      s0.set("vertexCount", val(static_cast<int>(h.uv0.size() / 2)));
      s0.set("slotId", val(0));
      sets.set("uv0", s0);
      o.set("uvSets", sets);
    }
    o.set("materialId", val(m.material_id));
    o.set("doubleSided", val(false));
    o.set("vertexCount", val(static_cast<int>(h.points.size() / 3)));
    o.set("triangulated", val(true));
    o.set("submeshes", submeshes(m));
    // Skinning (UsdSkel): 4 joint influences per vertex. jointIndices as an
    // Int32Array (legacy shape), jointWeights as Float32Array.
    if (m.has_skin()) {
      const td::RenderMesh::SkinBinding& sk = *m.skin;
      std::vector<uint16_t> ji = sk.joint_indices.flatten();
      val jia = val::global("Int32Array").new_(ji.size());
      if (!ji.empty()) {
        std::vector<int32_t> j32(ji.begin(), ji.end());
        jia.call<void>("set", val(typed_memory_view(j32.size(), j32.data())));
      }
      std::vector<float> jw = sk.joint_weights.flatten();
      o.set("jointIndices", jia);
      o.set("jointWeights", f32_owned(jw.data(), jw.size()));
      o.set("elementSize", val(4));
      o.set("skel_id", val(sk.skeleton_id));
      o.set("geomBindTransform", mat16(sk.geom_bind_transform));
      o.set("hasGeomBindTransform", val(true));
    }
    return o;
  }

  // Zero-copy heap descriptors {ptr,length,dtype,comps,count}; views alias the
  // WASM heap and stay valid until the next load / delete (cache is retained).
  val getMeshPtr(int mesh_id) {
    if (mesh_id < 0 || mesh_id >= numMeshes()) return val::null();
    const td::RenderMesh& m = scene_.meshes[mesh_id];
    const MeshHeap& h = ensureMeshHeap(mesh_id);
    val o = val::object();
    o.set("primName", val(m.name));
    o.set("displayName", val(m.name));
    o.set("absPath", val(m.prim_path));
    o.set("vertexCount", val(static_cast<int>(h.points.size() / 3)));
    o.set("materialId", val(m.material_id));
    o.set("doubleSided", val(false));
    o.set("triangulated", val(true));
    o.set("singleIndexable", val(true));
    o.set("hasSubmeshes", val(!m.material_subsets.empty()));
    o.set("points", descF32(h.points, 3));
    o.set("indices", descU32(h.indices, 1));
    o.set("faceVertexCounts", descU32(h.fvc, 1));
    if (!h.normals.empty()) o.set("normals", descF32(h.normals, 3));
    if (!h.uv0.empty()) o.set("uv0", descF32(h.uv0, 2));
    o.set("submeshes", submeshes(m));
    return o;
  }

  // ---- point-instancer instances -----------------------------------------
  // tydra_next flattens each visible PointInstancer instance-mesh into one
  // RenderPointInstanceDraw (prototype mesh id + world transform). Exposed with
  // the legacy getInstance shape for InstancedMesh / worker consumers.
  int numInstances() const {
    return static_cast<int>(scene_.point_instance_draws.size());
  }
  val getInstance(int id) const {
    if (id < 0 || id >= numInstances()) return val::null();
    const td::RenderPointInstanceDraw& d = scene_.point_instance_draws[id];
    val o = val::object();
    const char* pn = "";
    const char* ap = "";
    if (d.point_instancer_id >= 0 &&
        d.point_instancer_id < static_cast<int>(scene_.point_instancers.size())) {
      const auto& pi = scene_.point_instancers[d.point_instancer_id];
      pn = pi.name.c_str();
      ap = pi.prim_path.c_str();
    }
    o.set("primName", val(std::string(pn)));
    o.set("displayName", val(std::string(pn)));
    o.set("absPath", val(std::string(ap)));
    o.set("prototypeIndex", val(static_cast<int>(d.prototype_index)));
    o.set("instanceId", val(static_cast<int>(d.instance_index)));
    o.set("meshId", val(d.mesh_id));
    o.set("materialId", val(d.material_id));
    o.set("localMatrix", mat16(d.transform));
    o.set("globalMatrix", mat16(d.transform));
    o.set("visible", val(true));
    return o;
  }
  val getInstancesForMesh(int mesh_id) const {
    val arr = val::array();
    int k = 0;
    for (size_t i = 0; i < scene_.point_instance_draws.size(); ++i) {
      if (scene_.point_instance_draws[i].mesh_id == mesh_id)
        arr.set(k++, val(static_cast<int>(i)));
    }
    return arr;
  }

  // ---- skeletons (UsdSkel) ------------------------------------------------
  int numSkeletons() const { return static_cast<int>(scene_.skeletons.size()); }
  val getSkeleton(int skel_id) const {
    val result = val::object();
    if (skel_id < 0 || skel_id >= numSkeletons()) {
      result.set("error", val(std::string("Invalid skeleton ID")));
      return result;
    }
    const td::Skeleton& s = scene_.skeletons[skel_id];
    result.set("id", val(skel_id));
    result.set("prim_name", val(s.name));
    result.set("display_name", val(s.name));
    result.set("abs_path", val(s.prim_path));
    result.set("anim_id", val(s.animation_id));
    result.set("root_node", buildSkelNode(s, s.root_joint));
    return result;
  }

  // ---- materials ----------------------------------------------------------
  int numMaterials() const { return static_cast<int>(scene_.materials.size()); }
  val getMaterial(int mat_id) const { return materialJSON(mat_id, "json"); }
  val getMaterialWithFormat(int mat_id, const std::string& fmt) const {
    return materialJSON(mat_id, fmt);
  }

  // ---- textures / images (metadata; pixel decode is a follow-up) ----------
  int numTextures() const { return static_cast<int>(scene_.textures.size()); }
  val getTexture(int tex_id) const {
    if (tex_id < 0 || tex_id >= numTextures()) return val::null();
    const td::RenderTexture& t = scene_.textures[tex_id];
    val o = val::object();
    o.set("textureImageId", val(t.image_id));
    o.set("assetPath", val(t.asset_path));
    o.set("wrapS", val(std::string(wrap_str(t.wrap_s))));
    o.set("wrapT", val(std::string(wrap_str(t.wrap_t))));
    o.set("isUDIM", val(false));
    // UV transform (UsdUVTexture) — three.js applies these to the texture matrix.
    val off = val::array(); off.set(0, val(t.offset.x)); off.set(1, val(t.offset.y));
    o.set("offset", off);
    val scl = val::array(); scl.set(0, val(t.scale.x)); scl.set(1, val(t.scale.y));
    o.set("scale", scl);
    o.set("rotation", val(t.rotation));
    // Per-channel value bias/scale (normal-map remap, etc.).
    val bias = val::array(), sval = val::array();
    for (int i = 0; i < 4; ++i) { bias.set(i, val((&t.bias.x)[i])); sval.set(i, val((&t.scale_value.x)[i])); }
    o.set("bias", bias);
    o.set("scaleValue", sval);
    const char* ch = "rgba";
    switch (t.output_channel) {
      case td::RenderTexture::Channel::R: ch = "r"; break;
      case td::RenderTexture::Channel::G: ch = "g"; break;
      case td::RenderTexture::Channel::B: ch = "b"; break;
      case td::RenderTexture::Channel::A: ch = "a"; break;
      case td::RenderTexture::Channel::RGB: ch = "rgb"; break;
      default: ch = "rgba"; break;
    }
    o.set("channel", val(std::string(ch)));
    return o;
  }
  int numImages() const { return static_cast<int>(scene_.images.size()); }
  val getImageCopy(int img_id) {
    if (img_id < 0 || img_id >= numImages()) return val::null();
    const td::TextureImage& im = scene_.images[img_id];
    const ImageBytes& ib = ensureImageBytes(img_id);
    val o = val::object();
    o.set("width", val(static_cast<int>(im.width)));
    o.set("height", val(static_cast<int>(im.height)));
    o.set("channels", val(static_cast<int>(im.channels)));
    o.set("uri", val(im.resolved_path));
    // `data` holds RAW ENCODED bytes (png/jpg/exr/...) pulled from the USDZ
    // archive — NOT decoded pixels (this lean module has no image codec).
    // `bufferId >= 0` + `decoded:false` is the exact shape the three.js loader
    // (TinyUSDZLoaderUtils.getTextureFromUSD / TinyUSDZMaterialX.loadTextureFromUSD
    // "Case 2") already handles: it wraps the bytes in a Blob and decodes them
    // via THREE.TextureLoader (PNG/JPEG) or EXRLoader/HDRLoader (no wasm codec).
    // When there are no bytes, bufferId=-1 signals "URI only".
    o.set("bufferId", val(ib.bytes.empty() ? -1 : img_id));
    o.set("decoded", val(false));
    o.set("encoded", val(!ib.bytes.empty()));
    o.set("mimeType", val(std::string(mime_from_path(im.resolved_path))));
    if (!ib.bytes.empty()) {
      val a = val::global("Uint8Array").new_(ib.bytes.size());
      a.call<void>("set", val(typed_memory_view(ib.bytes.size(), ib.bytes.data())));
      o.set("data", a);
    }
    return o;
  }

  // Zero-copy variant: encoded bytes as a heap descriptor (retained cache).
  val getImagePtr(int img_id) {
    if (img_id < 0 || img_id >= numImages()) return val::null();
    const td::TextureImage& im = scene_.images[img_id];
    const ImageBytes& ib = ensureImageBytes(img_id);
    val o = val::object();
    o.set("width", val(static_cast<int>(im.width)));
    o.set("height", val(static_cast<int>(im.height)));
    o.set("channels", val(static_cast<int>(im.channels)));
    o.set("uri", val(im.resolved_path));
    o.set("bufferId", val(ib.bytes.empty() ? -1 : img_id));
    o.set("decoded", val(false));
    o.set("encoded", val(!ib.bytes.empty()));
    o.set("mimeType", val(std::string(mime_from_path(im.resolved_path))));
    if (!ib.bytes.empty()) {
      o.set("ptr", val(static_cast<double>(reinterpret_cast<uintptr_t>(ib.bytes.data()))));
      o.set("byteLength", val(static_cast<double>(ib.bytes.size())));
    }
    return o;
  }

 private:
  struct MeshHeap {
    std::vector<float> points, normals, uv0;
    std::vector<uint32_t> indices, fvc;
  };
  // Flattened encoded image bytes, retained so getImagePtr's heap view stays
  // valid and repeated getImageCopy calls don't re-flatten.
  struct ImageBytes { std::vector<uint8_t> bytes; };

  const ImageBytes& ensureImageBytes(int img_id) {
    auto it = image_bytes_.find(img_id);
    if (it != image_bytes_.end()) return it->second;
    ImageBytes ib;
    const td::TextureImage& im = scene_.images[img_id];
    if (im.is_loaded()) ib.bytes = im.data.flatten();
    return image_bytes_.emplace(img_id, std::move(ib)).first->second;
  }

  const MeshHeap& ensureMeshHeap(int mesh_id) {
    auto it = mesh_heap_.find(mesh_id);
    if (it != mesh_heap_.end()) return it->second;
    const td::RenderMesh& m = scene_.meshes[mesh_id];
    MeshHeap h;
    h.points = m.points.flatten();
    if (m.is_triangulated && !m.triangulated_indices.empty()) {
      h.indices = m.triangulated_indices.flatten();
    } else {
      h.indices = m.face_vertex_indices.flatten();
    }
    h.fvc = m.face_vertex_counts.flatten();
    if (m.has_normals()) h.normals = m.normals.flatten();
    if (m.has_texcoords()) h.uv0 = m.texcoords_0.flatten();
    auto res = mesh_heap_.emplace(mesh_id, std::move(h));
    return res.first->second;
  }

  static val descF32(const std::vector<float>& v, int comps) {
    val d = val::object();
    d.set("ptr", val(static_cast<double>(reinterpret_cast<uintptr_t>(v.data()))));
    d.set("length", val(static_cast<double>(v.size())));
    d.set("dtype", val(std::string("f32")));
    d.set("comps", val(comps));
    d.set("count", val(static_cast<double>(v.size() / (comps > 0 ? comps : 1))));
    d.set("byteLength", val(static_cast<double>(v.size() * 4)));
    return d;
  }
  static val descU32(const std::vector<uint32_t>& v, int comps) {
    val d = val::object();
    d.set("ptr", val(static_cast<double>(reinterpret_cast<uintptr_t>(v.data()))));
    d.set("length", val(static_cast<double>(v.size())));
    d.set("dtype", val(std::string("u32")));
    d.set("comps", val(comps));
    d.set("count", val(static_cast<double>(v.size() / (comps > 0 ? comps : 1))));
    d.set("byteLength", val(static_cast<double>(v.size() * 4)));
    return d;
  }

  val submeshes(const td::RenderMesh& m) const {
    val arr = val::array();
    int i = 0;
    for (const auto& s : m.material_subsets) {
      val e = val::object();
      e.set("start", val(static_cast<int>(s.face_start * 3)));
      e.set("count", val(static_cast<int>(s.face_count * 3)));
      e.set("materialId", val(s.material_id));
      arr.set(i++, e);
    }
    return arr;
  }

  // Nested SkelNode tree (joint_id/joint_name/joint_path/bind_transform/
  // rest_transform/children) matching the legacy getSkeleton().root_node that
  // USDSkeletalHelper.js walks to build the THREE.Skeleton bone hierarchy.
  val buildSkelNode(const td::Skeleton& s, int joint_idx) const {
    val o = val::object();
    if (joint_idx < 0 || joint_idx >= static_cast<int>(s.joints.size())) return o;
    const td::SkeletonJoint& j = s.joints[joint_idx];
    o.set("joint_id", val(joint_idx));
    o.set("joint_name", val(j.name));
    o.set("joint_path", val(j.path));
    o.set("bind_transform", mat16(j.bind_transform));
    o.set("rest_transform", mat16(j.rest_transform));
    val ch = val::array();
    int i = 0;
    for (int32_t c : j.children) ch.set(i++, buildSkelNode(s, c));
    o.set("children", ch);
    return o;
  }

  val buildNode(int node_id) const {
    if (node_id < 0 || node_id >= static_cast<int>(scene_.nodes.size())) {
      return val::null();
    }
    const td::SceneNode& n = scene_.nodes[node_id];
    val o = val::object();
    o.set("primName", val(n.name));
    o.set("displayName", val(n.name));
    o.set("absPath", val(n.prim_path));
    o.set("nodeType", val(std::string(node_type_str(n.type))));
    o.set("contentId", val(n.type == td::NodeType::Mesh ? n.data_id : -1));
    o.set("localMatrix", mat16(n.local_transform));
    o.set("globalMatrix", mat16(n.world_transform));
    o.set("visible", val(n.visible));
    val ch = val::array();
    int i = 0;
    for (int32_t c : n.children) ch.set(i++, buildNode(c));
    o.set("children", ch);
    return o;
  }

  // Serialize a material to the SAME JSON schema the legacy
  // tydra::serializeMaterial produces (src/tydra/material-serializer.cc), so the
  // three.js material code parses it unchanged:
  //   { hasUsdPreviewSurface, hasOpenPBR,
  //     surfaceShader: { diffuseColor:[..], metallic, ..., *TextureId },
  //     openPBR: { base:{param}, specular:{..}, ..., geometry:{..} } }
  // where each openPBR param is {name,type:"value",value} or
  // {name,type:"texture",textureId}. Fields tydra_next does not carry are simply
  // omitted (the JS falls back to DEFAULT_OPENPBR_PARAMS via `?? default`).
  val materialJSON(int mat_id, const std::string& fmt) const {
    val o = val::object();
    if (mat_id < 0 || mat_id >= numMaterials()) {
      o.set("error", val(std::string("material id out of range")));
      return o;
    }
    const td::RenderMaterial& m = scene_.materials[mat_id];
    const bool has_ps = static_cast<bool>(m.preview_surface);
    const bool has_pbr = static_cast<bool>(m.openpbr);
    std::string j = "{";
    j += "\"name\":\"" + jesc(m.name) + "\",";
    j += "\"absPath\":\"" + jesc(m.prim_path) + "\",";
    j += "\"doubleSided\":" + std::string(m.double_sided ? "true" : "false") + ",";
    j += "\"hasUsdPreviewSurface\":" + std::string(has_ps ? "true" : "false") + ",";
    j += "\"hasOpenPBR\":" + std::string(has_pbr ? "true" : "false");
    if (has_ps) { j += ",\"surfaceShader\":"; appendPreviewSurface(j, *m.preview_surface); }
    if (has_pbr) { j += ",\"openPBR\":"; appendOpenPBR(j, *m.openpbr); }
    j += "}";
    o.set("data", val(j));
    o.set("format", val(fmt.empty() ? std::string("json") : fmt));
    return o;
  }

  static void appendVec3(std::string& j, const td::ShaderParam& p) {
    j += "[" + jnum(p.value.x) + "," + jnum(p.value.y) + "," + jnum(p.value.z) + "]";
  }
  // openPBR layer param: {"name":X,"type":"value","value":V|[r,g,b]} or
  // {"name":X,"type":"texture","textureId":N}.
  static void oparam(std::string& j, const char* name, const td::ShaderParam& p,
                     bool vec3) {
    j += "\"" + std::string(name) + "\":{\"name\":\"" + name + "\",";
    if (p.texture_id >= 0) {
      j += "\"type\":\"texture\",\"textureId\":" + std::to_string(p.texture_id) + "}";
    } else {
      j += "\"type\":\"value\",\"value\":";
      if (vec3) appendVec3(j, p); else j += jnum(p.value.x);
      j += "}";
    }
  }

  static void appendPreviewSurface(std::string& j, const td::PreviewSurfaceShader& s) {
    j += "{\"type\":\"PreviewSurfaceShader\",";
    j += "\"useSpecularWorkflow\":" + std::string(s.use_specular_workflow ? "true" : "false") + ",";
    j += "\"diffuseColor\":"; appendVec3(j, s.diffuse_color); j += ",";
    j += "\"emissiveColor\":"; appendVec3(j, s.emissive_color); j += ",";
    j += "\"specularColor\":"; appendVec3(j, s.specular_color); j += ",";
    j += "\"metallic\":" + jnum(s.metallic.value.x) + ",";
    j += "\"roughness\":" + jnum(s.roughness.value.x) + ",";
    j += "\"clearcoat\":" + jnum(s.clearcoat.value.x) + ",";
    j += "\"clearcoatRoughness\":" + jnum(s.clearcoat_roughness.value.x) + ",";
    j += "\"opacity\":" + jnum(s.opacity.value.x) + ",";
    j += "\"opacityThreshold\":" + jnum(s.opacity_threshold.value.x) + ",";
    j += "\"ior\":" + jnum(s.ior.value.x) + ",";
    j += "\"normal\":"; appendVec3(j, s.normal); j += ",";
    j += "\"displacement\":" + jnum(s.displacement.value.x) + ",";
    j += "\"occlusion\":" + jnum(s.occlusion.value.x);
    auto texid = [&](const char* key, const td::ShaderParam& p) {
      if (p.texture_id >= 0)
        j += ",\"" + std::string(key) + "TextureId\":" + std::to_string(p.texture_id);
    };
    texid("diffuseColor", s.diffuse_color);
    texid("emissiveColor", s.emissive_color);
    texid("specularColor", s.specular_color);
    texid("metallic", s.metallic);
    texid("roughness", s.roughness);
    texid("clearcoat", s.clearcoat);
    texid("clearcoatRoughness", s.clearcoat_roughness);
    texid("opacity", s.opacity);
    texid("ior", s.ior);
    texid("normal", s.normal);
    texid("displacement", s.displacement);
    texid("occlusion", s.occlusion);
    j += "}";
  }

  static void appendOpenPBR(std::string& j, const td::OpenPBRSurfaceShader& s) {
    j += "{";
    j += "\"base\":{";
    oparam(j, "base_weight", s.base_weight, false); j += ",";
    oparam(j, "base_color", s.base_color, true); j += ",";
    oparam(j, "base_roughness", s.base_roughness, false); j += ",";
    oparam(j, "base_metalness", s.base_metalness, false);
    j += "},\"specular\":{";
    oparam(j, "specular_weight", s.specular_weight, false); j += ",";
    oparam(j, "specular_color", s.specular_color, true); j += ",";
    oparam(j, "specular_roughness", s.specular_roughness, false); j += ",";
    oparam(j, "specular_ior", s.specular_ior, false); j += ",";
    oparam(j, "specular_anisotropy", s.specular_anisotropy, false); j += ",";
    oparam(j, "specular_rotation", s.specular_rotation, false);
    j += "},\"transmission\":{";
    oparam(j, "transmission_weight", s.transmission_weight, false); j += ",";
    oparam(j, "transmission_color", s.transmission_color, true); j += ",";
    oparam(j, "transmission_depth", s.transmission_depth, false);
    j += "},\"subsurface\":{";
    oparam(j, "subsurface_weight", s.subsurface_weight, false); j += ",";
    oparam(j, "subsurface_color", s.subsurface_color, true); j += ",";
    oparam(j, "subsurface_radius", s.subsurface_radius, true);
    j += "},\"sheen\":{";
    oparam(j, "sheen_weight", s.sheen_weight, false); j += ",";
    oparam(j, "sheen_color", s.sheen_color, true); j += ",";
    oparam(j, "sheen_roughness", s.sheen_roughness, false);
    j += "},\"coat\":{";
    oparam(j, "coat_weight", s.coat_weight, false); j += ",";
    oparam(j, "coat_color", s.coat_color, true); j += ",";
    oparam(j, "coat_roughness", s.coat_roughness, false); j += ",";
    oparam(j, "coat_ior", s.coat_ior, false);
    j += "},\"emission\":{";
    oparam(j, "emission_luminance", s.emission_luminance, false); j += ",";
    oparam(j, "emission_color", s.emission_color, true);
    j += "},\"geometry\":{";
    oparam(j, "opacity", s.opacity, false); j += ",";
    oparam(j, "geometry_opacity", s.opacity, false); j += ",";
    oparam(j, "normal", s.normal, true); j += ",";
    oparam(j, "tangent", s.tangent, true);
    j += "}}";
  }

  bool loaded_ = false;
  int max_mem_mb_ = 0;
  std::string error_, warn_;
  td::RenderScene scene_;
  std::unordered_map<int, MeshHeap> mesh_heap_;
  std::unordered_map<int, ImageBytes> image_bytes_;
};

// ============================================================================
// Image decode (module-level free functions, matching the legacy binding):
// decodeEXR / decodeHDR return { success, width, height, channels:4,
// data:(Float32Array|Uint16Array), pixelFormat }. The three.js loader's
// EXR/HDR path (TinyUSDZLoaderUtils.decodeEXRFromBuffer / decodeHDR) calls
// these when present, else falls back to THREE.EXRLoader/HDRLoader on a Blob.
// ============================================================================
namespace {

#if defined(TINYUSDZ_NEXT_WITH_EXR)
// Pack an RGBA fp32 buffer into the requested output format and attach it to
// `result` (float32 -> Float32Array; float16 -> half via Uint16Array).
void set_pixels(val& result, const std::vector<float>& rgba,
                const std::string& format) {
  const size_t n = rgba.size();
  if (format == "float16") {
    std::vector<uint16_t> half(n);
    exr_float_to_half(rgba.data(), half.data(), n);
    val a = val::global("Uint16Array").new_(n);
    a.call<void>("set", val(typed_memory_view(n, half.data())));
    result.set("data", a);
    result.set("pixelFormat", std::string("float16"));
    result.set("bitsPerChannel", 16);
  } else {
    val a = val::global("Float32Array").new_(n);
    a.call<void>("set", val(typed_memory_view(n, rgba.data())));
    result.set("data", a);
    result.set("pixelFormat", std::string("float32"));
    result.set("bitsPerChannel", 32);
  }
}

val decodeEXR(const val& data, const std::string& format) {
  val result = val::object();
  std::vector<uint8_t> buf = emscripten::convertJSArrayToNumberVector<uint8_t>(data);
  exr_image img;
  std::memset(&img, 0, sizeof(img));
  const exr_result r = exr_load_from_memory(buf.data(), buf.size(), nullptr, &img);
  if (!EXR_OK(r)) {
    result.set("success", false);
    result.set("error", std::string(exr_result_string(r)));
    return result;
  }
  if (img.num_parts < 1 || img.parts == nullptr) {
    exr_image_free(&img);
    result.set("success", false); result.set("error", std::string("EXR has no parts"));
    return result;
  }
  const exr_part* part = &img.parts[0];
  if (part->is_deep || part->images == nullptr || part->width <= 0 || part->height <= 0) {
    exr_image_free(&img);
    result.set("success", false); result.set("error", std::string("EXR part deep or empty"));
    return result;
  }
  int iR = -1, iG = -1, iB = -1, iA = -1, iY = -1;
  for (int c = 0; c < part->header.num_channels; ++c) {
    const char* n = part->header.channels[c].name;
    if (!std::strcmp(n, "R")) iR = c; else if (!std::strcmp(n, "G")) iG = c;
    else if (!std::strcmp(n, "B")) iB = c; else if (!std::strcmp(n, "A")) iA = c;
    else if (!std::strcmp(n, "Y")) iY = c;
  }
  if (iR < 0 && iG < 0 && iB < 0 && iY < 0) {
    exr_image_free(&img);
    result.set("success", false); result.set("error", std::string("EXR has no R/G/B/Y channel"));
    return result;
  }
  const size_t npix = size_t(part->width) * size_t(part->height);
  auto getf = [&](int idx, size_t p) -> float {
    if (idx < 0 || !part->images[idx]) return 0.0f;
    const void* base = part->images[idx];
    switch (part->header.channels[idx].pixel_type) {
      case EXR_PIXEL_HALF: { uint16_t h = reinterpret_cast<const uint16_t*>(base)[p]; float f; exr_half_to_float(&h, &f, 1); return f; }
      case EXR_PIXEL_FLOAT: return reinterpret_cast<const float*>(base)[p];
      case EXR_PIXEL_UINT: return float(reinterpret_cast<const uint32_t*>(base)[p]);
    }
    return 0.0f;
  };
  std::vector<float> rgba(npix * 4);
  for (size_t p = 0; p < npix; ++p) {
    rgba[p*4+0] = iR >= 0 ? getf(iR, p) : getf(iY, p);
    rgba[p*4+1] = iG >= 0 ? getf(iG, p) : getf(iY, p);
    rgba[p*4+2] = iB >= 0 ? getf(iB, p) : getf(iY, p);
    rgba[p*4+3] = iA >= 0 ? getf(iA, p) : 1.0f;
  }
  const int w = part->width, h = part->height;
  exr_image_free(&img);
  set_pixels(result, rgba, format);
  result.set("success", true);
  result.set("width", w); result.set("height", h); result.set("channels", 4);
  return result;
}

val decodeHDR(const val& data, const std::string& format) {
  val result = val::object();
  std::vector<uint8_t> buf = emscripten::convertJSArrayToNumberVector<uint8_t>(data);
  int w = 0, hh = 0, comp = 0;
  float* px = stbi_loadf_from_memory(buf.data(), static_cast<int>(buf.size()),
                                     &w, &hh, &comp, 4);  // force RGBA
  if (!px) {
    result.set("success", false); result.set("error", std::string("Failed to decode HDR"));
    return result;
  }
  std::vector<float> rgba(px, px + size_t(w) * size_t(hh) * 4);
  stbi_image_free(px);
  set_pixels(result, rgba, format);
  result.set("success", true);
  result.set("width", w); result.set("height", hh); result.set("channels", 4);
  return result;
}
val decodeEXRDefault(const val& d) { return decodeEXR(d, "float32"); }
val decodeHDRDefault(const val& d) { return decodeHDR(d, "float16"); }
#endif  // TINYUSDZ_NEXT_WITH_EXR

}  // namespace

EMSCRIPTEN_BINDINGS(tinyusdz_next) {
#if defined(TINYUSDZ_NEXT_WITH_EXR)
  emscripten::function("decodeEXR", &decodeEXR);
  emscripten::function("decodeHDR", &decodeHDR);
  emscripten::function("decodeEXRDefault", &decodeEXRDefault);
  emscripten::function("decodeHDRDefault", &decodeHDRDefault);
#endif
  emscripten::class_<TinyUSDZLoaderNative>("TinyUSDZLoaderNative")
      .constructor<>()
      .function("ok", &TinyUSDZLoaderNative::ok)
      .function("error", &TinyUSDZLoaderNative::error)
      .function("warn", &TinyUSDZLoaderNative::warn)
      .function("reset", &TinyUSDZLoaderNative::reset)
      .function("setMaxMemoryLimitMB", &TinyUSDZLoaderNative::setMaxMemoryLimitMB)
      .function("loadFromBinary", &TinyUSDZLoaderNative::loadFromBinary)
      .function("numRootNodes", &TinyUSDZLoaderNative::numRootNodes)
      .function("getDefaultRootNodeId", &TinyUSDZLoaderNative::getDefaultRootNodeId)
      .function("getUpAxis", &TinyUSDZLoaderNative::getUpAxis)
      .function("getSceneMetadata", &TinyUSDZLoaderNative::getSceneMetadata)
      .function("getDefaultRootNode", &TinyUSDZLoaderNative::getDefaultRootNode)
      .function("getRootNode", &TinyUSDZLoaderNative::getRootNode)
      .function("numMeshes", &TinyUSDZLoaderNative::numMeshes)
      .function("getMeshCopy", &TinyUSDZLoaderNative::getMeshCopy)
      .function("getMeshPtr", &TinyUSDZLoaderNative::getMeshPtr)
      .function("numInstances", &TinyUSDZLoaderNative::numInstances)
      .function("getInstance", &TinyUSDZLoaderNative::getInstance)
      .function("getInstancesForMesh", &TinyUSDZLoaderNative::getInstancesForMesh)
      .function("numSkeletons", &TinyUSDZLoaderNative::numSkeletons)
      .function("getSkeleton", &TinyUSDZLoaderNative::getSkeleton)
      .function("numMaterials", &TinyUSDZLoaderNative::numMaterials)
      .function("getMaterial", &TinyUSDZLoaderNative::getMaterial)
      .function("getMaterialWithFormat", &TinyUSDZLoaderNative::getMaterialWithFormat)
      .function("numTextures", &TinyUSDZLoaderNative::numTextures)
      .function("getTexture", &TinyUSDZLoaderNative::getTexture)
      .function("numImages", &TinyUSDZLoaderNative::numImages)
      .function("getImageCopy", &TinyUSDZLoaderNative::getImageCopy)
      .function("getImagePtr", &TinyUSDZLoaderNative::getImagePtr);
}
