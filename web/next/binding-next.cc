// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Lean Emscripten binding for the "next" core: USD load/compose (next_core) +
// tydra_next RenderScene extraction, exposed as `TinyUSDZLoaderNative` with the
// SAME JS-facing method shapes as the legacy web/binding.cc, so the existing
// three.js loader (web/js/src/tinyusdz/{TinyUSDZLoader,TinyUSDZLoaderUtils,
// TinyUSDZWorker}.js) drives this module unchanged.
//
// Increment 1 (core three.js path): lifecycle, loadFromBinary, scene-graph node
// tree (world transforms), mesh geometry (getMeshPtr zero-copy + getMeshCopy
// owned), material JSON, texture/image metadata. NOTE: tydra_next is being
// refactored elsewhere — this binding consumes it read-only and adds nothing to
// it. Feature gaps vs legacy (full material-JSON schema, instancing, skinning,
// image decode) are deliberately follow-up increments.

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "next/tinyusdz-next.hh"  // LoadUSDFromMemory (USDA/USDC/USDZ auto-detect)
#include "next/stage/stage.hh"
#include "tydra/next/render-converter.hh"
#include "tydra/next/render-data.hh"

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
    scene_ = td::RenderScene(); mesh_heap_.clear();
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
    td::RenderSceneConverter conv;
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
    o.set("wrapS", val(std::string(wrap_str(t.wrap_s))));
    o.set("wrapT", val(std::string(wrap_str(t.wrap_t))));
    o.set("isUDIM", val(false));
    return o;
  }
  int numImages() const { return static_cast<int>(scene_.images.size()); }
  val getImageCopy(int img_id) const {
    if (img_id < 0 || img_id >= numImages()) return val::null();
    const td::TextureImage& im = scene_.images[img_id];
    val o = val::object();
    o.set("width", val(static_cast<int>(im.width)));
    o.set("height", val(static_cast<int>(im.height)));
    o.set("channels", val(static_cast<int>(im.channels)));
    o.set("uri", val(im.resolved_path));
    o.set("decoded", val(im.is_loaded()));
    if (im.is_loaded()) {
      std::vector<uint8_t> d = im.data.flatten();
      val a = val::global("Uint8Array").new_(d.size());
      if (!d.empty()) a.call<void>("set", val(typed_memory_view(d.size(), d.data())));
      o.set("data", a);
    }
    return o;
  }

 private:
  struct MeshHeap {
    std::vector<float> points, normals, uv0;
    std::vector<uint32_t> indices, fvc;
  };

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

  val materialJSON(int mat_id, const std::string& fmt) const {
    val o = val::object();
    if (mat_id < 0 || mat_id >= numMaterials()) {
      o.set("error", val(std::string("material id out of range")));
      return o;
    }
    const td::RenderMaterial& m = scene_.materials[mat_id];
    const bool has_ps = (m.shader_type == td::RenderMaterial::ShaderType::PreviewSurface) &&
                        m.preview_surface;
    const bool has_pbr = (m.shader_type == td::RenderMaterial::ShaderType::OpenPBR) &&
                         m.openpbr;
    std::string j = "{";
    j += "\"name\":\"" + jesc(m.name) + "\",";
    j += "\"absPath\":\"" + jesc(m.prim_path) + "\",";
    j += "\"hasUsdPreviewSurface\":" + std::string(has_ps ? "true" : "false") + ",";
    j += "\"hasOpenPBR\":" + std::string(has_pbr ? "true" : "false") + ",";
    j += "\"doubleSided\":" + std::string(m.double_sided ? "true" : "false");
    // comps==1 -> scalar (e.g. metallic), comps>1 -> [r,g,b]/[x,y,z] array;
    // matches the legacy getMaterial field shapes the three.js setup reads.
    auto param = [&](const char* key, const td::ShaderParam& p, int comps) {
      const float* v = &p.value.x;
      j += ",\"" + std::string(key) + "\":";
      if (comps == 1) {
        j += jnum(v[0]);
      } else {
        j += "[";
        for (int i = 0; i < comps; ++i) { if (i) j += ","; j += jnum(v[i]); }
        j += "]";
      }
      j += ",\"" + std::string(key) + "TextureId\":" + std::to_string(p.texture_id);
    };
    if (has_ps) {
      const td::PreviewSurfaceShader& s = *m.preview_surface;
      param("diffuseColor", s.diffuse_color, 3);
      param("emissiveColor", s.emissive_color, 3);
      param("metallic", s.metallic, 1);
      param("roughness", s.roughness, 1);
      param("opacity", s.opacity, 1);
      param("ior", s.ior, 1);
      param("occlusion", s.occlusion, 1);
      param("normal", s.normal, 3);
    }
    j += "}";
    o.set("data", val(j));
    o.set("format", val(fmt.empty() ? std::string("json") : fmt));
    return o;
  }

  bool loaded_ = false;
  int max_mem_mb_ = 0;
  std::string error_, warn_;
  td::RenderScene scene_;
  std::unordered_map<int, MeshHeap> mesh_heap_;
};

EMSCRIPTEN_BINDINGS(tinyusdz_next) {
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
      .function("numMaterials", &TinyUSDZLoaderNative::numMaterials)
      .function("getMaterial", &TinyUSDZLoaderNative::getMaterial)
      .function("getMaterialWithFormat", &TinyUSDZLoaderNative::getMaterialWithFormat)
      .function("numTextures", &TinyUSDZLoaderNative::numTextures)
      .function("getTexture", &TinyUSDZLoaderNative::getTexture)
      .function("numImages", &TinyUSDZLoaderNative::numImages)
      .function("getImageCopy", &TinyUSDZLoaderNative::getImageCopy);
}
