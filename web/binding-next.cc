// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Lean Emscripten binding for the next-core + tydra-next render path.
// This intentionally avoids the legacy tinyusdz_static library and exposes the
// RenderStream contract consumed by web/js/src/tinyusdz/TinyUSDZLoader.js for
// first-stage `backend=next` browser coverage.

#include <emscripten/bind.h>
#include <emscripten/emscripten.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "next/stage/stage.hh"
#include "next/tinyusdz-next.hh"
#include "tydra/next/render-converter.hh"
#include "tydra/next/render-data.hh"

namespace tn = tinyusdz::next;
namespace tr = tinyusdz::tydra::next;

namespace {

EM_JS(void, reportNextCrateProgress, (const char* phase, double current, double total), {
  if (typeof Module.onNextCrateProgress === 'function') {
    const cur = Number(current);
    const tot = Number(total);
    Module.onNextCrateProgress({
      phase: UTF8ToString(Number(phase)),
      current: cur,
      total: tot,
      percentage: tot > 0 ? (cur / tot) * 100 : 0
    });
  }
});

std::string CopyUint8ArrayToString(const emscripten::val& bytes,
                                   std::string* error) {
  if (bytes.isNull() || bytes.isUndefined()) {
    if (error) *error = "Input is null or undefined";
    return {};
  }
  const size_t size = bytes["byteLength"].as<size_t>();
  constexpr size_t kMaxLayerBytes = size_t(1) << 30;  // 1 GiB
  if (size > kMaxLayerBytes) {
    if (error) *error = "Input exceeds 1 GiB limit";
    return {};
  }
  std::string out(size, '\0');
  if (size > 0) {
    emscripten::val view = emscripten::val::global("Uint8Array").new_(
        bytes["buffer"], bytes["byteOffset"],
        emscripten::val(static_cast<double>(size)));
    emscripten::val heap_view = emscripten::val(
        emscripten::typed_memory_view(size, reinterpret_cast<uint8_t*>(&out[0])));
    heap_view.call<void>("set", view);
  }
  return out;
}

bool IsUSDCBytes(const std::string& bytes) {
  return bytes.size() >= 8 &&
         std::memcmp(bytes.data(), "PXR-USDC", 8) == 0;
}

template <typename T>
void ClearVector(std::vector<T>* v) {
  if (!v) return;
  std::vector<T>().swap(*v);
}

std::array<double, 16> IdentityMatrix() {
  return {1.0, 0.0, 0.0, 0.0,
          0.0, 1.0, 0.0, 0.0,
          0.0, 0.0, 1.0, 0.0,
          0.0, 0.0, 0.0, 1.0};
}

std::array<double, 16> MatrixToArray(const tr::Matrix4& m) {
  std::array<double, 16> out{};
  for (size_t i = 0; i < 16; ++i) out[i] = static_cast<double>(m.m[i]);
  return out;
}

emscripten::val MatrixValue(const std::array<double, 16>& m) {
  emscripten::val a = emscripten::val::array();
  for (double v : m) a.call<void>("push", v);
  return a;
}

emscripten::val Float3Value(const tr::Float3& c) {
  emscripten::val a = emscripten::val::array();
  a.call<void>("push", c.x);
  a.call<void>("push", c.y);
  a.call<void>("push", c.z);
  return a;
}

emscripten::val Float4Value(const tr::Float4& c) {
  emscripten::val a = emscripten::val::array();
  a.call<void>("push", c.x);
  a.call<void>("push", c.y);
  a.call<void>("push", c.z);
  a.call<void>("push", c.w);
  return a;
}

template <typename Chunked>
float ChunkedFloatAt(const Chunked& values, size_t i, float fallback = 0.0f) {
  return i < values.size() ? values[i] : fallback;
}

uint32_t ChunkedU32At(const tr::UInt32Chunked& values, size_t i,
                      uint32_t fallback = 0) {
  return i < values.size() ? values[i] : fallback;
}

std::vector<uint32_t> CornerToFace(const tr::RenderMesh& mesh) {
  std::vector<uint32_t> out(mesh.face_vertex_indices.size(), 0);
  size_t corner = 0;
  for (size_t face = 0; face < mesh.face_vertex_counts.size(); ++face) {
    const uint32_t count = mesh.face_vertex_counts[face];
    for (uint32_t i = 0; i < count && corner < out.size(); ++i, ++corner) {
      out[corner] = static_cast<uint32_t>(face);
    }
  }
  return out;
}

int32_t SubsetMaterialForFace(const tr::RenderMesh& mesh, uint32_t face) {
  for (const tr::RenderMesh::MaterialSubset& subset : mesh.material_subsets) {
    const uint32_t begin = subset.face_start;
    const uint32_t end = begin + subset.face_count;
    if (face >= begin && face < end) return subset.material_id;
  }
  return -1;
}

const tr::RenderTexture* TextureAt(const tr::RenderScene& scene, int32_t id) {
  if (id < 0 || static_cast<size_t>(id) >= scene.textures.size()) return nullptr;
  return &scene.textures[static_cast<size_t>(id)];
}

std::string TexturePath(const tr::RenderScene& scene, const tr::ShaderParam& p) {
  const tr::RenderTexture* tex = TextureAt(scene, p.texture_id);
  return tex ? tex->asset_path : std::string();
}

std::string MaterialKey(const tr::RenderScene& scene, int32_t material_id) {
  const tr::RenderMaterial* mat = scene.get_material(material_id);
  if (!mat) return "__default";
  std::ostringstream ss;
  ss << material_id << "|" << mat->prim_path;
  return ss.str();
}

}  // namespace

class RenderStream {
 public:
  RenderStream() = default;

  void setMaterialDedup(bool enabled) { material_dedup_ = enabled; }
  void setMeshMerge(bool enabled) { mesh_merge_ = enabled; }
  void setMeshMergeBakeTransform(bool enabled) {
    mesh_merge_bake_transform_ = enabled;
  }
  void setFlattenRenderTree(bool enabled) { flatten_render_tree_ = enabled; }

  emscripten::val begin(emscripten::val bytes) {
    std::string error;
    std::string input = CopyUint8ArrayToString(bytes, &error);
    if (!error.empty()) return ErrorResult(error);
    return beginOwned(std::move(input));
  }

  emscripten::val beginOwned(std::string&& crate) {
    end();
    error_.clear();

    tn::Stage stage;
    if (IsUSDCBytes(crate)) {
      tn::USDCLoadOptions opts;
      opts.crate_options.progress_callback =
        [](const char* phase, size_t current, size_t total) -> bool {
        reportNextCrateProgress(phase, static_cast<double>(current),
                                static_cast<double>(total));
        return true;
      };
      tn::USDCLoadResult load =
          tn::LoadUSDCFromMemoryOwned(std::move(crate), opts);
      if (!load.success) {
        return ErrorResult(load.error_summary.empty() ? "USDC load failed"
                                                      : load.error_summary);
      }
      stage = std::move(load.stage);
    } else {
      tn::LoadUSDOptions opts;
      opts.usdc_options.crate_options.progress_callback =
          [](const char* phase, size_t current, size_t total) -> bool {
        reportNextCrateProgress(phase, static_cast<double>(current),
                                static_cast<double>(total));
        return true;
      };
      std::string warn;
      std::string err;
      const bool ok = tn::LoadUSDFromMemory(
          reinterpret_cast<const uint8_t*>(crate.data()), crate.size(), &stage,
          opts, &warn, &err);
      if (!ok) {
        return ErrorResult(err.empty() ? "USD memory load failed" : err);
      }
    }

    tr::ConverterConfig config;
    config.mesh.triangulate = true;
    config.mesh.compute_normals = true;
    config.material.load_textures = false;
    config.material.allow_missing_textures = true;
    config.point_instancer.duplicate_meshes = true;

    tr::RenderSceneConverter converter(config);
    tr::ConvertResult converted = converter.Convert(stage);
    if (!converted.success) {
      return ErrorResult(converted.error.empty() ? "tydra-next conversion failed"
                                                : converted.error);
    }

    scene_ = std::move(converted.scene);
    buildOutputList();
    loaded_ = true;

    emscripten::val r = emscripten::val::object();
    r.set("success", true);
    r.set("meshCount", meshCount());
    return r;
  }

  int meshCount() const {
    return loaded_ ? static_cast<int>(outputs_.size()) : 0;
  }

  std::string error() const { return error_; }

  emscripten::val getSceneMetadata() const {
    emscripten::val metadata = emscripten::val::object();
    if (!loaded_) return metadata;
    metadata.set("upAxis", scene_.up_axis == tr::RenderScene::UpAxis::Z ? "Z" : "Y");
    metadata.set("metersPerUnit", scene_.meters_per_unit);
    metadata.set("framesPerSecond", scene_.frames_per_second);
    metadata.set("startTimeCode", scene_.start_time);
    metadata.set("endTimeCode", scene_.end_time);
    return metadata;
  }

  emscripten::val getStats() const {
    emscripten::val s = emscripten::val::object();
    const tr::RenderScene::Stats stats = scene_.get_stats();
    s.set("sourceMeshes", static_cast<int>(stats.mesh_count));
    s.set("sourceMaterials", static_cast<int>(stats.material_count));
    s.set("sourceTextures", static_cast<int>(stats.texture_count));
    s.set("optimizedMeshes", meshCount());
    s.set("optimizedMaterials", static_cast<int>(scene_.materials.size()));
    s.set("optimizedTextures", static_cast<int>(scene_.textures.size()));
    s.set("mergedMeshes", 0);
    s.set("mergeGroups", 0);
    s.set("skippedMergeMeshes", 0);
    s.set("pointInstancers", static_cast<int>(scene_.point_instancers.size()));
    s.set("pointInstanceDraws",
          static_cast<int>(scene_.point_instance_draws.size()));
    s.set("materialDedup", material_dedup_);
    s.set("meshMerge", mesh_merge_);
    s.set("meshMergeBakeTransform", mesh_merge_bake_transform_);
    s.set("flattenRenderTree", flatten_render_tree_);
    return s;
  }

  emscripten::val getMesh(int i) {
    if (!loaded_ || i < 0 || static_cast<size_t>(i) >= outputs_.size()) {
      return ErrorObject("invalid mesh index");
    }
    scratch_points_.clear();
    scratch_normals_.clear();
    scratch_uv_.clear();
    scratch_groups_.clear();

    const OutputRef& ref = outputs_[static_cast<size_t>(i)];
    const tr::RenderMesh* mesh = scene_.get_mesh(ref.mesh_id);
    if (!mesh) return ErrorObject("mesh id is out of range");
    buildTriangleSoup(*mesh);

    emscripten::val out = emscripten::val::object();
    out.set("vertexCount", static_cast<double>(scratch_points_.size() / 3));
    out.set("primName", mesh->name);
    out.set("primPath", mesh->prim_path);
    out.set("points", heapF(scratch_points_, 3));
    if (!scratch_normals_.empty()) out.set("normals", heapF(scratch_normals_, 3));
    if (!scratch_uv_.empty()) out.set("uv0", heapF(scratch_uv_, 2));
    out.set("localMatrix", MatrixValue(ref.local_matrix));
    out.set("worldMatrix", MatrixValue(ref.world_matrix));
    out.set("materialId", mesh->material_id);
    out.set("material", materialObject(mesh->material_id));
    addSubsetMaterials(*mesh, out);
    return out;
  }

  void end() {
    loaded_ = false;
    outputs_.clear();
    scene_ = tr::RenderScene();
    ClearVector(&scratch_points_);
    ClearVector(&scratch_normals_);
    ClearVector(&scratch_uv_);
    scratch_groups_.clear();
  }

 private:
  struct OutputRef {
    int32_t mesh_id = -1;
    std::array<double, 16> local_matrix = IdentityMatrix();
    std::array<double, 16> world_matrix = IdentityMatrix();
  };

  static emscripten::val ErrorObject(const std::string& error) {
    emscripten::val out = emscripten::val::object();
    out.set("error", error);
    return out;
  }

  emscripten::val ErrorResult(const std::string& error) {
    error_ = error;
    emscripten::val out = emscripten::val::object();
    out.set("success", false);
    out.set("error", error_);
    return out;
  }

  emscripten::val heapF(const std::vector<float>& v, int comps) const {
    emscripten::val d = emscripten::val::object();
    d.set("ptr", static_cast<double>(reinterpret_cast<uintptr_t>(v.data())));
    d.set("length", static_cast<double>(v.size()));
    d.set("comps", comps);
    d.set("dtype", std::string("f32"));
    d.set("byteLength", static_cast<double>(v.size() * sizeof(float)));
    return d;
  }

  emscripten::val materialObject(int32_t material_id) const {
    emscripten::val m = emscripten::val::object();
    const tr::RenderMaterial* mat = scene_.get_material(material_id);
    m.set("id", material_id);
    m.set("key", MaterialKey(scene_, material_id));
    if (!mat) {
      m.set("primPath", std::string("__default"));
      m.set("baseColor", Float3Value(tr::Float3(0.8f, 0.8f, 0.8f)));
      m.set("metallic", 0.0f);
      m.set("roughness", 0.5f);
      m.set("opacity", 1.0f);
      m.set("occlusion", 1.0f);
      m.set("emissive", Float3Value(tr::Float3(0.0f, 0.0f, 0.0f)));
      return m;
    }

    m.set("primPath", mat->prim_path);
    m.set("baseColor", Float3Value(tr::Float3(0.8f, 0.8f, 0.8f)));
    m.set("metallic", 0.0f);
    m.set("roughness", 0.5f);
    m.set("opacity", 1.0f);
    m.set("occlusion", 1.0f);
    m.set("emissive", Float3Value(tr::Float3(0.0f, 0.0f, 0.0f)));

    if (mat->preview_surface) {
      const tr::PreviewSurfaceShader& ps = *mat->preview_surface;
      m.set("baseColor", Float4Value(ps.diffuse_color.value));
      m.set("metallic", ps.metallic.value.x);
      m.set("roughness", ps.roughness.value.x);
      m.set("opacity", ps.opacity.value.x);
      m.set("occlusion", ps.occlusion.value.x);
      m.set("emissive", Float4Value(ps.emissive_color.value));
      if (ps.opacity_threshold.value.x > 0.0f) {
        m.set("opacityThreshold", ps.opacity_threshold.value.x);
      }
      setTextureField(m, "baseColorTexture", ps.diffuse_color);
      setTextureField(m, "normalTexture", ps.normal);
      setTextureField(m, "roughnessTexture", ps.roughness);
      setTextureField(m, "metallicTexture", ps.metallic);
      setTextureField(m, "occlusionTexture", ps.occlusion);
      setTextureField(m, "emissiveTexture", ps.emissive_color);
    } else if (mat->openpbr) {
      const tr::OpenPBRSurfaceShader& op = *mat->openpbr;
      m.set("baseColor", Float4Value(op.base_color.value));
      m.set("metallic", op.base_metalness.value.x);
      m.set("roughness", op.base_roughness.value.x);
      m.set("opacity", op.opacity.value.x);
      m.set("emissive", Float4Value(op.emission_color.value));
      setTextureField(m, "baseColorTexture", op.base_color);
      setTextureField(m, "normalTexture", op.normal);
      setTextureField(m, "roughnessTexture", op.base_roughness);
      setTextureField(m, "metallicTexture", op.base_metalness);
      setTextureField(m, "emissiveTexture", op.emission_color);
    }
    return m;
  }

  void setTextureField(emscripten::val& m, const char* key,
                       const tr::ShaderParam& param) const {
    const std::string path = TexturePath(scene_, param);
    if (!path.empty()) m.set(key, path);
  }

  void addSubsetMaterials(const tr::RenderMesh& mesh, emscripten::val& out) const {
    if (mesh.material_subsets.empty() || scratch_groups_.empty()) return;
    std::vector<int32_t> material_ids;
    for (const Group& group : scratch_groups_) {
      if (std::find(material_ids.begin(), material_ids.end(),
                    group.material_id) == material_ids.end()) {
        material_ids.push_back(group.material_id);
      }
    }

    emscripten::val materials = emscripten::val::array();
    for (size_t i = 0; i < material_ids.size(); ++i) {
      materials.set(static_cast<int>(i), materialObject(material_ids[i]));
    }
    emscripten::val groups = emscripten::val::array();
    int out_index = 0;
    for (const Group& group : scratch_groups_) {
      const auto it = std::find(material_ids.begin(), material_ids.end(),
                                group.material_id);
      if (it == material_ids.end()) continue;
      emscripten::val g = emscripten::val::object();
      g.set("start", static_cast<int>(group.start));
      g.set("count", static_cast<int>(group.count));
      g.set("materialIndex", static_cast<int>(it - material_ids.begin()));
      groups.set(out_index++, g);
    }
    out.set("materials", materials);
    out.set("submeshes", groups);
  }

  void buildOutputList() {
    outputs_.clear();
    std::set<int32_t> emitted;
    for (const tr::SceneNode& node : scene_.nodes) {
      if (!node.visible || node.type != tr::NodeType::Mesh ||
          node.data_id < 0 || emitted.count(node.data_id)) {
        continue;
      }
      OutputRef ref;
      ref.mesh_id = node.data_id;
      ref.local_matrix = MatrixToArray(node.local_transform);
      ref.world_matrix = MatrixToArray(node.world_transform);
      outputs_.push_back(ref);
      emitted.insert(node.data_id);
    }

    for (const tr::RenderPointInstanceDraw& draw : scene_.point_instance_draws) {
      const int32_t mesh_id = draw.expanded_mesh_id >= 0 ? draw.expanded_mesh_id
                                                         : draw.mesh_id;
      if (mesh_id < 0) continue;
      OutputRef ref;
      ref.mesh_id = mesh_id;
      if (draw.expanded_mesh_id >= 0) {
        ref.local_matrix = IdentityMatrix();
        ref.world_matrix = IdentityMatrix();
      } else {
        ref.local_matrix = MatrixToArray(draw.transform);
        ref.world_matrix = MatrixToArray(draw.transform);
      }
      outputs_.push_back(ref);
    }
  }

  struct Group {
    uint32_t start = 0;
    uint32_t count = 0;
    int32_t material_id = -1;
  };

  void appendGroup(uint32_t start, uint32_t count, int32_t material_id) {
    if (count == 0 || material_id < 0) return;
    if (!scratch_groups_.empty()) {
      Group& prev = scratch_groups_.back();
      if (prev.material_id == material_id && prev.start + prev.count == start) {
        prev.count += count;
        return;
      }
    }
    scratch_groups_.push_back(Group{start, count, material_id});
  }

  void buildTriangleSoup(const tr::RenderMesh& mesh) {
    const tr::UInt32Chunked& indices = !mesh.triangulated_indices.empty()
                                           ? mesh.triangulated_indices
                                           : mesh.face_vertex_indices;
    const std::vector<uint32_t> corner_to_face = CornerToFace(mesh);
    const bool has_facevarying_remap =
        mesh.triangulated_face_vertex_indices.size() == indices.size();

    uint32_t group_start = 0;
    uint32_t group_count = 0;
    int32_t group_material = -1;

    for (size_t tri = 0; tri + 2 < indices.size(); tri += 3) {
      uint32_t face = std::numeric_limits<uint32_t>::max();
      if (has_facevarying_remap) {
        const uint32_t corner = mesh.triangulated_face_vertex_indices[tri];
        if (corner < corner_to_face.size()) face = corner_to_face[corner];
      }
      const int32_t subset_material =
          face == std::numeric_limits<uint32_t>::max()
              ? -1
              : SubsetMaterialForFace(mesh, face);
      if (subset_material >= 0) {
        if (group_count == 0) {
          group_start = static_cast<uint32_t>(scratch_points_.size() / 3);
          group_material = subset_material;
        } else if (group_material != subset_material) {
          appendGroup(group_start, group_count, group_material);
          group_start = static_cast<uint32_t>(scratch_points_.size() / 3);
          group_count = 0;
          group_material = subset_material;
        }
      } else if (group_count > 0) {
        appendGroup(group_start, group_count, group_material);
        group_count = 0;
        group_material = -1;
      }

      for (size_t c = 0; c < 3; ++c) {
        const uint32_t vi = ChunkedU32At(indices, tri + c);
        appendPoint(mesh, vi);
        appendNormal(mesh, vi, has_facevarying_remap
                                  ? ChunkedU32At(mesh.triangulated_face_vertex_indices,
                                                 tri + c)
                                  : vi);
        appendUV(mesh, vi, has_facevarying_remap
                              ? ChunkedU32At(mesh.triangulated_face_vertex_indices,
                                             tri + c)
                              : vi);
      }
      if (subset_material >= 0) group_count += 3;
    }
    if (group_count > 0) appendGroup(group_start, group_count, group_material);
  }

  void appendPoint(const tr::RenderMesh& mesh, uint32_t vi) {
    const size_t off = static_cast<size_t>(vi) * 3;
    scratch_points_.push_back(ChunkedFloatAt(mesh.points, off + 0));
    scratch_points_.push_back(ChunkedFloatAt(mesh.points, off + 1));
    scratch_points_.push_back(ChunkedFloatAt(mesh.points, off + 2));
  }

  void appendNormal(const tr::RenderMesh& mesh, uint32_t vi, uint32_t corner) {
    if (mesh.normals.empty()) return;
    uint32_t idx = vi;
    if (mesh.normals_interp == tr::Interpolation::FaceVarying) idx = corner;
    const size_t off = static_cast<size_t>(idx) * 3;
    if (off + 2 >= mesh.normals.size()) return;
    scratch_normals_.push_back(mesh.normals[off + 0]);
    scratch_normals_.push_back(mesh.normals[off + 1]);
    scratch_normals_.push_back(mesh.normals[off + 2]);
  }

  void appendUV(const tr::RenderMesh& mesh, uint32_t vi, uint32_t corner) {
    if (mesh.texcoords_0.empty()) return;
    uint32_t idx = vi;
    if (mesh.texcoords_0_interp == tr::Interpolation::FaceVarying) idx = corner;
    const size_t off = static_cast<size_t>(idx) * 2;
    if (off + 1 >= mesh.texcoords_0.size()) return;
    scratch_uv_.push_back(mesh.texcoords_0[off + 0]);
    scratch_uv_.push_back(mesh.texcoords_0[off + 1]);
  }

  tr::RenderScene scene_;
  std::vector<OutputRef> outputs_;
  std::vector<float> scratch_points_;
  std::vector<float> scratch_normals_;
  std::vector<float> scratch_uv_;
  std::vector<Group> scratch_groups_;
  bool loaded_ = false;
  bool material_dedup_ = false;
  bool mesh_merge_ = false;
  bool mesh_merge_bake_transform_ = false;
  bool flatten_render_tree_ = false;
  std::string error_;
};

EMSCRIPTEN_BINDINGS(tinyusdz_next_render_stream) {
  emscripten::class_<RenderStream>("RenderStream")
      .constructor<>()
      .function("setMaterialDedup", &RenderStream::setMaterialDedup)
      .function("setMeshMerge", &RenderStream::setMeshMerge)
      .function("setMeshMergeBakeTransform",
                &RenderStream::setMeshMergeBakeTransform)
      .function("setFlattenRenderTree", &RenderStream::setFlattenRenderTree)
      .function("begin", &RenderStream::begin)
      .function("beginOwned", &RenderStream::beginOwned)
      .function("meshCount", &RenderStream::meshCount)
      .function("getSceneMetadata", &RenderStream::getSceneMetadata)
      .function("getStats", &RenderStream::getStats)
      .function("getMesh", &RenderStream::getMesh)
      .function("error", &RenderStream::error)
      .function("end", &RenderStream::end);
}
