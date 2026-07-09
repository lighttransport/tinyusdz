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
#include <cctype>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <cmath>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "next/schema/geom-mesh.hh"
#include "next/schema/geom-xform.hh"
#include "next/schema/usd-shade.hh"
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

emscripten::val Uint8ArrayFromBytes(const uint8_t* data, size_t size) {
  emscripten::val out = emscripten::val::global("Uint8Array").new_(
      emscripten::val(static_cast<double>(size)));
  if (size > 0) {
    emscripten::val view = emscripten::val(
        emscripten::typed_memory_view(size, data));
    out.call<void>("set", view);
  }
  return out;
}

emscripten::val Uint8ArrayFromString(const std::string& s) {
  return Uint8ArrayFromBytes(reinterpret_cast<const uint8_t*>(s.data()),
                             s.size());
}

emscripten::val Uint8ArrayFromVector(const std::vector<uint8_t>& v) {
  return Uint8ArrayFromBytes(v.data(), v.size());
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

class NextUSDZConverterNative {
 public:
  NextUSDZConverterNative() = default;

  std::string error() const { return error_; }
  std::string warn() const { return warn_; }

  emscripten::val rewriteRoot(emscripten::val bytes, const std::string& filename,
                              emscripten::val options) {
    error_.clear();
    warn_.clear();

    std::string copy_error;
    std::string input = CopyUint8ArrayToString(bytes, &copy_error);
    if (!copy_error.empty()) return ErrorResult(copy_error);

    tn::Stage stage;
    tn::LoadUSDOptions load_opts;
    if (!options.isNull() && !options.isUndefined()) {
      emscripten::val max_memory = options["maxMemory"];
      if (!max_memory.isUndefined() && !max_memory.isNull()) {
        load_opts.max_memory = max_memory.as<size_t>();
      }
    }

    const bool ok = tn::LoadUSDFromMemory(
        reinterpret_cast<const uint8_t*>(input.data()), input.size(), &stage,
        load_opts, &warn_, &error_);
    if (!ok) {
      return ErrorResult(error_.empty() ? "next-core USD load failed" : error_);
    }

    std::string root_format = "usdc";
    if (!options.isNull() && !options.isUndefined()) {
      emscripten::val fmt = options["rootLayerFormat"];
      if (!fmt.isUndefined() && !fmt.isNull()) {
        root_format = fmt.as<std::string>();
      }
    }
    std::transform(root_format.begin(), root_format.end(), root_format.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    emscripten::val out = emscripten::val::object();
    out.set("success", true);
    out.set("sourcePath", filename);

    if (root_format == "usda") {
      std::string text = tn::WriteUSDAToString(stage);
      out.set("rootName", std::string("root.usda"));
      out.set("rootLayerFormat", std::string("usda"));
      out.set("data", Uint8ArrayFromString(text));
      out.set("size", static_cast<double>(text.size()));
      return out;
    }

    std::vector<uint8_t> usdc;
    tn::USDCWriteResult wr = tn::WriteUSDCToMemory(usdc, stage);
    if (!wr.success) {
      return ErrorResult(wr.error.empty() ? "next-core USDC write failed"
                                          : wr.error);
    }
    out.set("rootName", std::string("root.usdc"));
    out.set("rootLayerFormat", std::string("usdc"));
    out.set("data", Uint8ArrayFromVector(usdc));
    out.set("size", static_cast<double>(usdc.size()));
    out.set("tokenCount", static_cast<double>(wr.token_count));
    out.set("pathCount", static_cast<double>(wr.path_count));
    out.set("specCount", static_cast<double>(wr.spec_count));
    return out;
  }

 private:
  emscripten::val ErrorResult(const std::string& error) {
    error_ = error;
    emscripten::val out = emscripten::val::object();
    out.set("success", false);
    out.set("error", error_);
    return out;
  }

  std::string error_;
  std::string warn_;
};

class RenderStream {
 public:
  RenderStream() = default;

  void setMaterialDedup(bool enabled) { material_dedup_ = enabled; }
  void setMeshMerge(bool enabled) { mesh_merge_ = enabled; }
  void setMeshMergeBakeTransform(bool enabled) {
    mesh_merge_bake_transform_ = enabled;
  }
  void setFlattenRenderTree(bool enabled) { flatten_render_tree_ = enabled; }

  // Adopt the root bytes by move and load lazily when the input is USDC.
  // USDA-root/full-archive inputs still go through the generic next loader.
  emscripten::val beginOwned(std::string &&crate) {
    emscripten::val r = emscripten::val::object();
    end();
    error_.clear();
    if (IsUSDCBytes(crate)) {
      tinyusdz::next::USDCLoadOptions opts;
      opts.crate_options.progress_callback =
          [](const char *phase, size_t current, size_t total) -> bool {
        reportNextCrateProgress(
            phase, static_cast<double>(current), static_cast<double>(total));
        return true;
      };
      tinyusdz::next::USDCLoadResult res =
          tinyusdz::next::LoadUSDCFromMemoryOwned(std::move(crate), opts);
      if (!res.success) {
        error_ = res.error_summary.empty() ? std::string("USDC load failed")
                                           : res.error_summary;
        r.set("success", false);
        r.set("error", error_);
        return r;
      }
      stage_ = std::move(res.stage);
    } else {
      tinyusdz::next::LoadUSDOptions opts;
      opts.usdc_options.crate_options.progress_callback =
          [](const char *phase, size_t current, size_t total) -> bool {
        reportNextCrateProgress(
            phase, static_cast<double>(current), static_cast<double>(total));
        return true;
      };
      std::string warn;
      std::string err;
      const bool ok = tinyusdz::next::LoadUSDFromMemory(
          reinterpret_cast<const uint8_t *>(crate.data()), crate.size(),
          &stage_, opts, &warn, &err);
      if (!ok) {
        error_ = err.empty() ? std::string("USD memory load failed") : err;
        r.set("success", false);
        r.set("error", error_);
        return r;
      }
    }
    meshes_ = tinyusdz::next::GetAllMeshes(stage_);
    stats_ = Stats{};
    stats_.source_mesh_count = meshes_.size();
    if (mesh_merge_) {
      buildOptimizedOutputs_();
    }
    loaded_ = true;
    r.set("success", true);
    r.set("meshCount", meshCount());
    return r;
  }

  // Begin from a JS Uint8Array (one copy into the WASM heap, then adopted).
  emscripten::val begin(emscripten::val bytes) {
    const size_t size = bytes["byteLength"].as<size_t>();
    constexpr size_t kMaxLayerBytes = size_t(1) << 30;  // 1 GiB
    if (size > kMaxLayerBytes) {
      emscripten::val r = emscripten::val::object();
      error_ = "Input exceeds 1 GiB limit";
      r.set("success", false);
      r.set("error", error_);
      return r;
    }
    std::string s;
    s.resize(size);
    if (size > 0) {
      emscripten::val view = emscripten::val::global("Uint8Array").new_(
          bytes["buffer"], bytes["byteOffset"],
          emscripten::val(static_cast<double>(size)));
      emscripten::val heapView = emscripten::val(emscripten::typed_memory_view(
          size, reinterpret_cast<uint8_t *>(&s[0])));
      heapView.call<void>("set", view);
    }
    return beginOwned(std::move(s));
  }

  int meshCount() const {
    if (!loaded_ && outputs_.empty()) return 0;
    if (mesh_merge_) return static_cast<int>(outputs_.size());
    return static_cast<int>(meshes_.size());
  }
  std::string error() const { return error_; }

  emscripten::val getStats() const {
    emscripten::val s = emscripten::val::object();
    s.set("sourceMeshes", static_cast<int>(stats_.source_mesh_count));
    const size_t source_material_count =
        std::max(stats_.source_material_count, source_material_keys_.size());
    const size_t source_texture_count =
        std::max(stats_.source_texture_count, source_texture_keys_.size());
    s.set("sourceMaterials", static_cast<int>(source_material_count));
    s.set("sourceTextures", static_cast<int>(source_texture_count));
    s.set("optimizedMeshes", static_cast<int>(meshCount()));
    s.set("optimizedMaterials", static_cast<int>(materials_.size()));
    s.set("optimizedTextures", static_cast<int>(texture_keys_.size()));
    s.set("mergedMeshes", static_cast<int>(stats_.merged_mesh_count));
    s.set("mergeGroups", static_cast<int>(stats_.merge_group_count));
    s.set("skippedMergeMeshes", static_cast<int>(stats_.skipped_merge_count));
    s.set("materialDedup", material_dedup_);
    s.set("meshMerge", mesh_merge_);
    s.set("meshMergeBakeTransform", mesh_merge_bake_transform_);
    s.set("flattenRenderTree", flatten_render_tree_);
    return s;
  }

  emscripten::val getSceneMetadata() const {
    emscripten::val metadata = emscripten::val::object();
    if (!loaded_) return metadata;
    const tinyusdz::next::StageMeta &meta = stage_.GetMeta();
    metadata.set("upAxis", meta.upAxis);
    metadata.set("metersPerUnit", meta.metersPerUnit);
    metadata.set("framesPerSecond", meta.framesPerSecond);
    metadata.set("timeCodesPerSecond", meta.timeCodesPerSecond);
    metadata.set("startTimeCode", meta.startTimeCode);
    metadata.set("endTimeCode", meta.endTimeCode);
    return metadata;
  }

  // Materialize mesh i's geometry into the scratch and return zero-copy
  // descriptors {points,indices,normals,uv0} + resolved material. Valid until the
  // next getMesh()/end(); the JS caller must upload before calling getMesh again.
  emscripten::val getMesh(int i) {
    emscripten::val out = emscripten::val::object();
    if (!loaded_ || i < 0 || i >= meshCount()) {
      out.set("error", std::string("invalid mesh index"));
      return out;
    }
    if (mesh_merge_) {
      const OutputMesh &record = outputs_[static_cast<size_t>(i)];
      if (record.merged) return outputMergedMesh_(record);
      return outputSourceMesh_(record.source_index);
    }
    return outputSourceMesh_(i);
  }

  // Free the stage, mesh list and scratch (returns the heap to the allocator).
  void end() {
    loaded_ = false;
    meshes_.clear();
    meshes_.shrink_to_fit();
    outputs_.clear();
    outputs_.shrink_to_fit();
    materials_.clear();
    material_key_to_id_.clear();
    material_path_to_id_.clear();
    source_material_keys_.clear();
    source_texture_keys_.clear();
    texture_keys_.clear();
    stage_ = tinyusdz::next::Stage();
    freeVec_(s_points_);
    freeVec_(s_normals_);
    freeVec_(s_uv_);
    freeVec_(s_indices_);
  }

 private:
  struct MaterialRecord {
    int32_t id = -1;
    std::string key;
    std::string prim_path;
    float base_color[3] = {0.8f, 0.8f, 0.8f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float opacity = 1.0f;
    float occlusion = 1.0f;
    float emissive[3] = {0.0f, 0.0f, 0.0f};
    float opacity_threshold = -1.0f;
    std::string base_color_texture;
    std::string normal_texture;
    std::string roughness_texture;
    std::string metallic_texture;
    std::string occlusion_texture;
    std::string emissive_texture;
  };

  struct OutputMesh {
    bool merged = false;
    int source_index = -1;
    std::string name;
    std::string prim_path;
    std::vector<float> points;
    std::vector<float> normals;
    std::vector<float> uv;
    std::vector<uint32_t> indices;
    bool soup = false;
    int32_t material_id = -1;
    std::array<double, 16> local_matrix;
    std::array<double, 16> world_matrix;
  };

  struct Stats {
    size_t source_mesh_count = 0;
    size_t source_material_count = 0;
    size_t source_texture_count = 0;
    size_t merged_mesh_count = 0;
    size_t merge_group_count = 0;
    size_t skipped_merge_count = 0;
  };

  emscripten::val outputSourceMesh_(int i) {
    emscripten::val out = emscripten::val::object();
    if (i < 0 || i >= static_cast<int>(meshes_.size())) {
      out.set("error", std::string("invalid source mesh index"));
      return out;
    }
    const tinyusdz::next::UsdPrim &prim = meshes_[static_cast<size_t>(i)].GetPrim();

    bool soup = false;  // indexed, or non-indexed soup
    std::string mesh_err;
    if (!buildRenderMesh_(prim, &soup, &mesh_err)) {
      out.set("error", mesh_err.empty() ? std::string("mesh build failed")
                                        : mesh_err);
      return out;
    }

    out.set("vertexCount", static_cast<double>(s_points_.size() / 3));
    out.set("primName", prim.GetName());
    out.set("primPath", prim.GetPath().str());
    out.set("points", heapF_(s_points_, 3));
    if (!soup && !s_indices_.empty()) out.set("indices", heapU32_(s_indices_));
    if (!s_normals_.empty()) out.set("normals", heapF_(s_normals_, 3));
    if (!s_uv_.empty()) out.set("uv0", heapF_(s_uv_, 2));
    out.set("localMatrix", matArray_(localMatrix_(prim)));
    out.set("worldMatrix", matArray_(worldMatrix_(prim)));
    const int32_t material_id = materialIdForBoundPrim_(prim);
    out.set("materialId", material_id);
    out.set("material", materialObject_(material_id));
    addGeomSubsetMaterials_(prim, out);
    return out;
  }

  emscripten::val outputMergedMesh_(const OutputMesh &record) const {
    emscripten::val out = emscripten::val::object();
    out.set("vertexCount", static_cast<double>(record.points.size() / 3));
    out.set("primName", record.name);
    out.set("primPath", record.prim_path);
    out.set("points", heapF_(record.points, 3));
    if (!record.soup && !record.indices.empty()) {
      out.set("indices", heapU32_(record.indices));
    }
    if (!record.normals.empty()) out.set("normals", heapF_(record.normals, 3));
    if (!record.uv.empty()) out.set("uv0", heapF_(record.uv, 2));
    out.set("localMatrix", matArray_(record.local_matrix));
    out.set("worldMatrix", matArray_(record.world_matrix));
    out.set("materialId", record.material_id);
    out.set("material", materialObject_(record.material_id));
    return out;
  }

  template <typename T>
  static void freeVec_(std::vector<T> &v) { std::vector<T>().swap(v); }

  // Read an array property through a COPY of the lazy Value, so the Stage's own
  // property stays lazy (per-mesh decode does not accumulate across meshes).
  std::vector<float> matFloat_(const tinyusdz::next::UsdPrim &prim, const char *name) {
    const tinyusdz::next::Value *v = prim.GetPropertyValue(name);
    if (!v) return {};
    tinyusdz::next::Value tmp = *v;
    const std::vector<float> *a = tmp.as_float_array();
    return a ? *a : std::vector<float>{};
  }
  std::vector<int32_t> matInt_(const tinyusdz::next::UsdPrim &prim, const char *name) {
    const tinyusdz::next::Value *v = prim.GetPropertyValue(name);
    if (!v) return {};
    tinyusdz::next::Value tmp = *v;
    const std::vector<int32_t> *a = tmp.as_int_array();
    return a ? *a : std::vector<int32_t>{};
  }

  // Build render geometry for one mesh into the scratch (s_points_/s_normals_/
  // s_uv_/s_indices_). Returns true if the result is a NON-INDEXED triangle soup
  // (drawn with drawArrays), false if INDEXED.
  //   - all primvars per-vertex  -> keep the indexed form directly (compact);
  //   - indexed UVs / per-vertex UV with face-varying normals -> de-index AND
  //     WELD inline (one vertex per distinct pos/uv/normal tuple), recovering
  //     vertex sharing while keeping correct attributes at seams;
  //   - PURE face-varying UVs (no st indices) -> emit the non-indexed soup, the
  //     minimal form when corners are mostly unique (welding would only add
  //     index + hash-map overhead).
  // The full soup is never materialized in the welded path; at most one mesh is
  // resident at a time either way.
  bool buildRenderMesh_(const tinyusdz::next::UsdPrim &prim, bool *soup_out,
                        std::string *err) {
    if (soup_out) *soup_out = false;
    std::vector<float> P = matFloat_(prim, "points");
    std::vector<int32_t> fvc = matInt_(prim, "faceVertexCounts");
    std::vector<int32_t> fvi = matInt_(prim, "faceVertexIndices");
    std::vector<float> N = matFloat_(prim, "normals");
    std::vector<float> UV = matFloat_(prim, "primvars:st");
    if (UV.empty()) UV = matFloat_(prim, "primvars:st0");
    if (UV.empty()) UV = matFloat_(prim, "st");
    std::vector<int32_t> stIdx = matInt_(prim, "primvars:st:indices");

    const size_t vtxCount = P.size() / 3;
    const size_t faceVtx = fvi.size();
    const size_t uvCount = UV.size() / 2;
    const size_t nCount = N.size() / 3;

    auto fail = [&](const std::string &msg) {
      if (err) *err = msg;
      return false;
    };
    if ((P.size() % 3) != 0) {
      return fail("Mesh points array length is not divisible by 3");
    }
    if ((N.size() % 3) != 0) {
      return fail("Mesh normals array length is not divisible by 3");
    }
    if ((UV.size() % 2) != 0) {
      return fail("Mesh texture coordinate array length is not divisible by 2");
    }
    if (!stIdx.empty()) {
      if (stIdx.size() != faceVtx) {
        return fail("Mesh texture coordinate index count does not match face vertex count");
      }
      for (int32_t idx : stIdx) {
        if (idx < 0 || static_cast<size_t>(idx) >= uvCount) {
          return fail("Mesh texture coordinate index is out of range");
        }
      }
    }
    if (fvc.empty()) {
      if (!fvi.empty() && (fvi.size() % 3) != 0) {
        return fail("Mesh indexed triangle list length is not divisible by 3");
      }
      for (int32_t idx : fvi) {
        if (idx < 0 || static_cast<size_t>(idx) >= vtxCount) {
          return fail("Mesh face index is out of point range");
        }
      }
    } else {
      size_t base = 0;
      for (int32_t n : fvc) {
        if (n < 0) {
          return fail("Mesh face vertex count is negative");
        }
        const size_t count = static_cast<size_t>(n);
        if (base > fvi.size() || count > fvi.size() - base) {
          return fail("Mesh face vertex counts exceed index array length");
        }
        base += count;
      }
      if (base != fvi.size()) {
        return fail("Mesh face vertex counts do not match index array length");
      }
      for (int32_t idx : fvi) {
        if (idx < 0 || static_cast<size_t>(idx) >= vtxCount) {
          return fail("Mesh face index is out of point range");
        }
      }
    }

    const bool uvFaceVarying = !UV.empty() && uvCount != vtxCount &&
                               (uvCount == faceVtx || !stIdx.empty());
    const bool nFaceVarying = !N.empty() && nCount != vtxCount && nCount == faceVtx;
    const bool needExpand = uvFaceVarying || nFaceVarying || !stIdx.empty();

    s_points_.clear(); s_normals_.clear(); s_uv_.clear(); s_indices_.clear();

    if (!needExpand) {
      s_points_ = std::move(P);
      triangulate_(fvi, fvc, s_indices_);
      if (nCount == vtxCount) s_normals_ = std::move(N);
      else computeNormals_(s_points_, s_indices_, s_normals_);
      if (uvCount == vtxCount) s_uv_ = std::move(UV);
      if (soup_out) *soup_out = false;
      return true;
    }

    const bool haveN = (nCount == vtxCount) || nFaceVarying;
    constexpr size_t kMaxRenderCorners = size_t(1) << 24;
    constexpr size_t kMaxEmittedVertices = size_t(1) << 24;
    auto readVec3 = [](const std::vector<float> &src, int32_t idx,
                       float *x, float *y, float *z) {
      if (idx < 0) return false;
      const size_t i = static_cast<size_t>(idx);
      if (i >= (src.size() / 3)) return false;
      const size_t off = i * 3;
      if (off + 2 >= src.size()) return false;
      *x = src[off];
      *y = src[off + 1];
      *z = src[off + 2];
      return true;
    };
    auto readVec2 = [](const std::vector<float> &src, int32_t idx,
                       float *x, float *y) {
      if (idx < 0) return false;
      const size_t i = static_cast<size_t>(idx);
      if (i >= (src.size() / 2)) return false;
      const size_t off = i * 2;
      if (off + 1 >= src.size()) return false;
      *x = src[off];
      *y = src[off + 1];
      return true;
    };
    auto indexFromSlot = [](size_t slot) -> int32_t {
      return slot <= static_cast<size_t>((std::numeric_limits<int32_t>::max)())
                 ? static_cast<int32_t>(slot)
                 : -1;
    };
    auto faceSpanAvailable = [](size_t base, int32_t n, size_t total) {
      if (n < 3) return false;
      const size_t count = static_cast<size_t>(n);
      return base <= total && count <= total - base;
    };
    auto advanceFaceBase = [](size_t base, int32_t n) {
      if (n <= 0) return base;
      const size_t add = static_cast<size_t>(n);
      if (base > (std::numeric_limits<size_t>::max)() - add) {
        return (std::numeric_limits<size_t>::max)();
      }
      return base + add;
    };

    // Decide weld vs soup by POSITION sharing, not interpolation type: a welded
    // mesh has at least vtxCount vertices, so it can only beat the (index-free)
    // soup when positions are heavily shared (vtxCount well below the triangle-
    // corner count). Face-varying UVs still weld well when positions share — what
    // matters is the expansion factor. When vtxCount is already close to the
    // corner count, the soup is minimal, so skip welding and keep it.
    size_t triCount = 0;
    for (int32_t nn : fvc) {
      if (nn >= 3) {
        const size_t add = static_cast<size_t>(nn - 2);
        if (triCount > (std::numeric_limits<size_t>::max)() - add) {
          triCount = (std::numeric_limits<size_t>::max)();
          break;
        }
        triCount += add;
      }
    }
    const size_t cornerCount =
        (triCount > (std::numeric_limits<size_t>::max)() / 3)
            ? (std::numeric_limits<size_t>::max)()
            : triCount * 3;
    const bool doWeld = vtxCount > 0 && vtxCount < cornerCount / 3;

    if (!doWeld) {
      // Non-indexed triangle soup (the minimal form for unique-per-corner UVs).
      std::vector<size_t> slots;
      size_t b = 0;
      for (int32_t n : fvc) {
        if (faceSpanAvailable(b, n, faceVtx)) {
          for (int32_t k = 2; k < n; ++k) {
            if (slots.size() > kMaxRenderCorners - 3) {
              s_points_.clear(); s_normals_.clear(); s_uv_.clear(); s_indices_.clear();
              if (err) *err = "Mesh exceeds RenderStream triangle-corner limit";
              return false;
            }
            slots.push_back(b);
            slots.push_back(b + static_cast<size_t>(k) - 1);
            slots.push_back(b + static_cast<size_t>(k));
          }
        }
        b = advanceFaceBase(b, n);
      }
      const size_t corners = slots.size();
      s_points_.resize(corners * 3);
      if (!UV.empty()) s_uv_.assign(corners * 2, 0.0f);
      if (haveN) s_normals_.resize(corners * 3);
      for (size_t c = 0; c < corners; ++c) {
        const size_t slot = slots[c];
        const int32_t vi = (slot < faceVtx) ? fvi[slot] : -1;
        float px = 0.0f, py = 0.0f, pz = 0.0f;
        if (readVec3(P, vi, &px, &py, &pz)) {
          s_points_[c * 3] = px; s_points_[c * 3 + 1] = py; s_points_[c * 3 + 2] = pz;
        }
        if (!UV.empty()) {
          const int32_t ui = uvFaceVarying ? indexFromSlot(slot) : vi;  // st:indices is empty here
          float u = 0.0f, v = 0.0f;
          if (readVec2(UV, ui, &u, &v)) { s_uv_[c * 2] = u; s_uv_[c * 2 + 1] = v; }
        }
        if (haveN) {
          const int32_t ni = nFaceVarying ? indexFromSlot(slot) : vi;
          float nx = 0.0f, ny = 0.0f, nz = 0.0f;
          if (readVec3(N, ni, &nx, &ny, &nz)) {
            s_normals_[c * 3] = nx; s_normals_[c * 3 + 1] = ny; s_normals_[c * 3 + 2] = nz;
          }
        }
      }
      if (s_normals_.empty()) computeNormals_(s_points_, s_indices_, s_normals_);  // empty idx -> flat per-tri
      if (soup_out) *soup_out = true;
      return true;  // non-indexed soup
    }

    // De-index + weld: emit one welded vertex per unique (pos[,uv][,normal])
    // corner, producing an INDEXED mesh. Built inline as faces are walked, so the
    // full per-corner soup never exists — peak ~= welded verts + index buffer.
    struct WeldKey {
      uint32_t b[8];
      bool operator==(const WeldKey &o) const { return std::memcmp(b, o.b, sizeof(b)) == 0; }
    };
    struct WeldHash {
      size_t operator()(const WeldKey &k) const {
        uint64_t h = 1469598103934665603ull;  // FNV-1a (folded to size_t for wasm32)
        for (uint32_t w : k.b) { h ^= w; h *= 1099511628211ull; }
        return static_cast<size_t>(h ^ (h >> 32));
      }
    };
    std::unordered_map<WeldKey, uint32_t, WeldHash> weld;
    // Welded vertices are bounded below by the point count; reserve to cut
    // rehash spikes (which transiently inflate the peak).
    constexpr size_t kMaxInitialWeldReserve = size_t(1) << 20;
    weld.reserve(vtxCount ? std::min(vtxCount, kMaxInitialWeldReserve) : 1024);

    auto emit = [&](size_t slot) -> bool {
      const int32_t vi = (slot < faceVtx) ? fvi[slot] : -1;
      float px = 0, py = 0, pz = 0, u = 0, v = 0, nx = 0, ny = 0, nz = 0;
      (void)readVec3(P, vi, &px, &py, &pz);
      if (!UV.empty()) {
        const int32_t ui = (!stIdx.empty() && slot < stIdx.size())
                                ? stIdx[slot]
                                : (uvFaceVarying ? indexFromSlot(slot) : vi);
        (void)readVec2(UV, ui, &u, &v);
      }
      if (haveN) {
        const int32_t ni = nFaceVarying ? indexFromSlot(slot) : vi;
        (void)readVec3(N, ni, &nx, &ny, &nz);
      }
      WeldKey key;
      std::memcpy(&key.b[0], &px, 4); std::memcpy(&key.b[1], &py, 4); std::memcpy(&key.b[2], &pz, 4);
      std::memcpy(&key.b[3], &u, 4); std::memcpy(&key.b[4], &v, 4);
      std::memcpy(&key.b[5], &nx, 4); std::memcpy(&key.b[6], &ny, 4); std::memcpy(&key.b[7], &nz, 4);
      auto it = weld.find(key);
      if (it != weld.end()) {
        s_indices_.push_back(it->second);
        return true;
      }
      const size_t nextIdx = s_points_.size() / 3;
      if (nextIdx >= kMaxEmittedVertices ||
          nextIdx > static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return false;
      }
      const uint32_t idx = static_cast<uint32_t>(nextIdx);
      s_points_.push_back(px); s_points_.push_back(py); s_points_.push_back(pz);
      if (!UV.empty()) { s_uv_.push_back(u); s_uv_.push_back(v); }
      if (haveN) { s_normals_.push_back(nx); s_normals_.push_back(ny); s_normals_.push_back(nz); }
      weld.emplace(key, idx);
      s_indices_.push_back(idx);
      return true;
    };

    size_t base = 0;
    for (int32_t n : fvc) {
      if (faceSpanAvailable(base, n, faceVtx)) {
        for (int32_t k = 2; k < n; ++k) {
          if (!emit(base) ||
              !emit(base + static_cast<size_t>(k) - 1) ||
              !emit(base + static_cast<size_t>(k))) {
            s_points_.clear(); s_normals_.clear(); s_uv_.clear(); s_indices_.clear();
            if (err) *err = "Mesh exceeds RenderStream emitted-vertex limit";
            return false;
          }
        }
      }
      base = advanceFaceBase(base, n);
    }
    // Normals not authored -> smooth normals on the welded indexed mesh.
    if (!haveN) computeNormals_(s_points_, s_indices_, s_normals_);
    if (soup_out) *soup_out = false;
    return true;  // welded result is INDEXED
  }

  static std::string fmtFloat_(float v) {
    std::ostringstream ss;
    ss << std::setprecision(9) << v;
    return ss.str();
  }

  static std::string normTexKey_(const std::string &path) {
    size_t first = 0;
    while (first < path.size() && (path[first] == '.' || path[first] == '/')) {
      ++first;
    }
    return path.substr(first);
  }

  static void addTextureKey_(const std::string &role,
                             const std::string &path,
                             std::set<std::string> *keys) {
    if (!keys || path.empty()) return;
    keys->insert(role + ":" + normTexKey_(path));
  }

  static void appendMaterialKey_(const MaterialRecord &m,
                                 std::ostringstream *ss) {
    *ss << "bc=" << fmtFloat_(m.base_color[0]) << "," << fmtFloat_(m.base_color[1])
        << "," << fmtFloat_(m.base_color[2]);
    *ss << "|metal=" << fmtFloat_(m.metallic);
    *ss << "|rough=" << fmtFloat_(m.roughness);
    *ss << "|opacity=" << fmtFloat_(m.opacity);
    *ss << "|occ=" << fmtFloat_(m.occlusion);
    *ss << "|emit=" << fmtFloat_(m.emissive[0]) << "," << fmtFloat_(m.emissive[1])
        << "," << fmtFloat_(m.emissive[2]);
    *ss << "|alpha=" << fmtFloat_(m.opacity_threshold);
    *ss << "|base=" << normTexKey_(m.base_color_texture);
    *ss << "|normal=" << normTexKey_(m.normal_texture);
    *ss << "|roughtex=" << normTexKey_(m.roughness_texture);
    *ss << "|metaltex=" << normTexKey_(m.metallic_texture);
    *ss << "|occtex=" << normTexKey_(m.occlusion_texture);
    *ss << "|emittex=" << normTexKey_(m.emissive_texture);
  }

  MaterialRecord materialRecordForPrim_(
      const tinyusdz::next::UsdPrim &mat) {
    MaterialRecord rec;
    if (!mat.IsValid()) {
      rec.prim_path = "__default";
      std::ostringstream ss;
      appendMaterialKey_(rec, &ss);
      rec.key = ss.str();
      return rec;
    }
    rec.prim_path = mat.GetPath().str();
    tinyusdz::next::UsdPrim shader;
    const std::string shaderPath = tinyusdz::next::GetSurfaceShader(stage_, mat);
    if (!shaderPath.empty()) shader = stage_.GetPrimAtPath(shaderPath);
    if (!shader.IsValid()) {
      for (const auto &ch : mat.GetChildren()) {
        if (tinyusdz::next::IsPreviewSurface(ch)) { shader = ch; break; }
      }
    }
    if (shader.IsValid()) {
      tinyusdz::next::PreviewSurfaceData ps;
      if (tinyusdz::next::GetPreviewSurfaceData(stage_, shader, &ps)) {
        rec.base_color[0] = ps.diffuse_color[0];
        rec.base_color[1] = ps.diffuse_color[1];
        rec.base_color[2] = ps.diffuse_color[2];
        rec.metallic = ps.metallic;
        rec.roughness = ps.roughness;
        rec.opacity = ps.opacity;
        rec.occlusion = ps.occlusion;
        rec.emissive[0] = ps.emissive_color[0];
        rec.emissive[1] = ps.emissive_color[1];
        rec.emissive[2] = ps.emissive_color[2];
        rec.opacity_threshold = ps.opacity_threshold > 0.0f
                                    ? ps.opacity_threshold
                                    : -1.0f;
        rec.base_color_texture = texFile_(ps.diffuse_texture);
        rec.normal_texture = texFile_(ps.normal_texture);
        rec.roughness_texture = texFile_(ps.roughness_texture);
        rec.metallic_texture = texFile_(ps.metallic_texture);
        rec.occlusion_texture = texFile_(ps.occlusion_texture);
        rec.emissive_texture = texFile_(ps.emissive_texture);
      }
    }
    std::ostringstream ss;
    appendMaterialKey_(rec, &ss);
    rec.key = ss.str();
    return rec;
  }

  int32_t registerMaterial_(const tinyusdz::next::UsdPrim &mat) {
    const std::string mat_path = mat.IsValid() ? mat.GetPath().str()
                                               : std::string("__default");
    MaterialRecord rec = materialRecordForPrim_(mat);
    source_material_keys_.insert(mat_path);
    addTextureKey_("color", rec.base_color_texture, &source_texture_keys_);
    addTextureKey_("data", rec.normal_texture, &source_texture_keys_);
    addTextureKey_("data", rec.roughness_texture, &source_texture_keys_);
    addTextureKey_("data", rec.metallic_texture, &source_texture_keys_);
    addTextureKey_("data", rec.occlusion_texture, &source_texture_keys_);
    addTextureKey_("color", rec.emissive_texture, &source_texture_keys_);

    const std::string key = material_dedup_ ? rec.key : mat_path;
    auto it = material_key_to_id_.find(key);
    if (it != material_key_to_id_.end()) return it->second;
    rec.id = static_cast<int32_t>(materials_.size());
    rec.key = key;
    materials_.push_back(rec);
    material_key_to_id_[key] = rec.id;
    material_path_to_id_[mat_path] = rec.id;
    addTextureKey_("color", rec.base_color_texture, &texture_keys_);
    addTextureKey_("data", rec.normal_texture, &texture_keys_);
    addTextureKey_("data", rec.roughness_texture, &texture_keys_);
    addTextureKey_("data", rec.metallic_texture, &texture_keys_);
    addTextureKey_("data", rec.occlusion_texture, &texture_keys_);
    addTextureKey_("color", rec.emissive_texture, &texture_keys_);
    return rec.id;
  }

  int32_t materialIdForBoundPrim_(const tinyusdz::next::UsdPrim &prim) {
    tinyusdz::next::UsdPrim mat = tinyusdz::next::GetBoundMaterial(stage_, prim);
    return registerMaterial_(mat);
  }

  static bool hasGeomSubset_(const tinyusdz::next::UsdPrim &prim) {
    for (const tinyusdz::next::UsdPrim &child : prim.GetChildren()) {
      if (child.IsValid() && child.GetTypeName() == "GeomSubset") return true;
    }
    return false;
  }

  static bool sameMatrix_(const std::array<double, 16> &a,
                          const std::array<double, 16> &b) {
    for (size_t i = 0; i < 16; ++i) {
      if (std::abs(a[i] - b[i]) > 1.0e-12) return false;
    }
    return true;
  }

  static std::string matrixKey_(const std::array<double, 16> &m) {
    std::ostringstream ss;
    ss << std::setprecision(17);
    for (double v : m) ss << v << ",";
    return ss.str();
  }

  static void transformPoint_(const std::array<double, 16> &m,
                              float *x, float *y, float *z) {
    const double px = *x;
    const double py = *y;
    const double pz = *z;
    *x = static_cast<float>(m[0] * px + m[4] * py + m[8] * pz + m[12]);
    *y = static_cast<float>(m[1] * px + m[5] * py + m[9] * pz + m[13]);
    *z = static_cast<float>(m[2] * px + m[6] * py + m[10] * pz + m[14]);
  }

  static void transformNormal_(const std::array<double, 16> &m,
                               float *x, float *y, float *z) {
    const double nx = *x;
    const double ny = *y;
    const double nz = *z;
    double tx = m[0] * nx + m[4] * ny + m[8] * nz;
    double ty = m[1] * nx + m[5] * ny + m[9] * nz;
    double tz = m[2] * nx + m[6] * ny + m[10] * nz;
    const double len = std::sqrt(tx * tx + ty * ty + tz * tz);
    if (len > 0.0) {
      tx /= len;
      ty /= len;
      tz /= len;
    }
    *x = static_cast<float>(tx);
    *y = static_cast<float>(ty);
    *z = static_cast<float>(tz);
  }

  struct MergeAccumulator {
    OutputMesh mesh;
    size_t source_count = 0;
  };

  static size_t triangleIndexCount_(const std::vector<uint32_t> &indices,
                                    const std::vector<float> &points) {
    return indices.empty() ? points.size() / 3 : indices.size();
  }

  void flushAccumulator_(MergeAccumulator *acc) {
    if (!acc || acc->source_count == 0) return;
    acc->mesh.merged = true;
    acc->mesh.name = "merged_material_" + std::to_string(acc->mesh.material_id);
    acc->mesh.prim_path = "/__tinyusdz_next_merged/" + acc->mesh.name + "_" +
                          std::to_string(outputs_.size());
    outputs_.push_back(std::move(acc->mesh));
    stats_.merge_group_count++;
    acc->mesh = OutputMesh{};
    acc->source_count = 0;
  }

  void appendToAccumulator_(const tinyusdz::next::UsdPrim &prim,
                            int32_t material_id,
                            bool soup,
                            MergeAccumulator *acc) {
    if (!acc) return;
    const std::array<double, 16> world = worldMatrix_(prim);
    if (acc->source_count == 0) {
      acc->mesh.soup = soup;
      acc->mesh.material_id = material_id;
      acc->mesh.local_matrix = mesh_merge_bake_transform_ ? identityMatrix_()
                                                          : localMatrix_(prim);
      acc->mesh.world_matrix = mesh_merge_bake_transform_ ? identityMatrix_()
                                                          : world;
    }
    const uint32_t vertex_offset =
        static_cast<uint32_t>(acc->mesh.points.size() / 3);
    const size_t point_base = acc->mesh.points.size();
    acc->mesh.points.insert(acc->mesh.points.end(), s_points_.begin(),
                            s_points_.end());
    if (!s_normals_.empty()) {
      acc->mesh.normals.insert(acc->mesh.normals.end(), s_normals_.begin(),
                               s_normals_.end());
    }
    if (!s_uv_.empty()) {
      acc->mesh.uv.insert(acc->mesh.uv.end(), s_uv_.begin(), s_uv_.end());
    }
    if (!soup) {
      acc->mesh.indices.reserve(acc->mesh.indices.size() + s_indices_.size());
      for (uint32_t idx : s_indices_) acc->mesh.indices.push_back(idx + vertex_offset);
    }
    if (mesh_merge_bake_transform_) {
      for (size_t off = point_base; off + 2 < acc->mesh.points.size(); off += 3) {
        transformPoint_(world, &acc->mesh.points[off],
                        &acc->mesh.points[off + 1],
                        &acc->mesh.points[off + 2]);
      }
      const size_t normal_base =
          acc->mesh.normals.size() >= s_normals_.size()
              ? acc->mesh.normals.size() - s_normals_.size()
              : acc->mesh.normals.size();
      for (size_t off = normal_base; off + 2 < acc->mesh.normals.size(); off += 3) {
        transformNormal_(world, &acc->mesh.normals[off],
                         &acc->mesh.normals[off + 1],
                         &acc->mesh.normals[off + 2]);
      }
    }
    acc->source_count++;
    stats_.merged_mesh_count++;
  }

  void buildOptimizedOutputs_() {
    outputs_.clear();
    std::unordered_map<std::string, MergeAccumulator> groups;
    constexpr size_t kMaxGroupVertices = size_t(1) << 20;
    constexpr size_t kMaxGroupIndices = size_t(3) << 20;

    for (size_t i = 0; i < meshes_.size(); ++i) {
      const tinyusdz::next::UsdPrim &prim = meshes_[i].GetPrim();
      const int32_t material_id = materialIdForBoundPrim_(prim);
      if (hasGeomSubset_(prim)) {
        OutputMesh out;
        out.merged = false;
        out.source_index = static_cast<int>(i);
        outputs_.push_back(out);
        stats_.skipped_merge_count++;
        continue;
      }

      bool soup = false;
      std::string mesh_err;
      if (!buildRenderMesh_(prim, &soup, &mesh_err)) {
        OutputMesh out;
        out.merged = false;
        out.source_index = static_cast<int>(i);
        outputs_.push_back(out);
        stats_.skipped_merge_count++;
        continue;
      }
      const bool has_normals = !s_normals_.empty();
      const bool has_uv = !s_uv_.empty();
      const std::array<double, 16> world = worldMatrix_(prim);
      std::ostringstream key;
      key << material_id << "|soup=" << soup << "|n=" << has_normals
          << "|uv=" << has_uv;
      if (!mesh_merge_bake_transform_) key << "|m=" << matrixKey_(world);
      MergeAccumulator &acc = groups[key.str()];
      if (acc.source_count > 0 &&
          (acc.mesh.soup != soup ||
           acc.mesh.material_id != material_id ||
           (!mesh_merge_bake_transform_ &&
            !sameMatrix_(acc.mesh.world_matrix, world)))) {
        flushAccumulator_(&acc);
      }
      const size_t next_vertices = acc.mesh.points.size() / 3 + s_points_.size() / 3;
      const size_t next_indices =
          triangleIndexCount_(acc.mesh.indices, acc.mesh.points) +
          triangleIndexCount_(s_indices_, s_points_);
      if (acc.source_count > 0 &&
          (next_vertices > kMaxGroupVertices || next_indices > kMaxGroupIndices)) {
        flushAccumulator_(&acc);
      }
      appendToAccumulator_(prim, material_id, soup, &acc);
    }
    for (auto &kv : groups) flushAccumulator_(&kv.second);
    stats_.source_material_count = source_material_keys_.size();
    stats_.source_texture_count = source_texture_keys_.size();
  }

  // Resolve a UsdUVTexture connection path ("/.../Tex.outputs:rgb") to its
  // inputs:file asset path, which the JS caller maps to an archive texture entry.
  std::string texFile_(const std::string &connPath) {
    if (connPath.empty()) return "";
    const size_t slash = connPath.rfind('/');
    const size_t dot = connPath.find('.', slash == std::string::npos ? 0 : slash);
    const std::string primPath = (dot == std::string::npos) ? connPath : connPath.substr(0, dot);
    tinyusdz::next::UsdPrim tex = stage_.GetPrimAtPath(primPath);
    if (!tex.IsValid()) return "";
    const tinyusdz::next::Value *v = tex.GetPropertyValue("inputs:file");
    if (!v) return "";
    if (const std::string *a = v->as_asset_path()) return *a;
    if (const std::string *s = v->as_string()) return *s;
    return "";
  }

  // Fan-triangulate faceVertexIndices grouped by faceVertexCounts.
  static void triangulate_(const std::vector<int32_t> &fvi,
                           const std::vector<int32_t> &fvc,
                           std::vector<uint32_t> &out) {
    out.clear();
    if (fvi.empty()) return;
    if (fvc.empty()) {  // assume an already-triangulated index list
      out.reserve(fvi.size());
      for (int32_t v : fvi) {
        if (v >= 0) out.push_back(static_cast<uint32_t>(v));
      }
      return;
    }
    size_t base = 0;
    auto faceSpanAvailable = [](size_t base, int32_t n, size_t total) {
      if (n < 3) return false;
      const size_t count = static_cast<size_t>(n);
      return base <= total && count <= total - base;
    };
    auto advanceFaceBase = [](size_t base, int32_t n) {
      if (n <= 0) return base;
      const size_t add = static_cast<size_t>(n);
      if (base > (std::numeric_limits<size_t>::max)() - add) {
        return (std::numeric_limits<size_t>::max)();
      }
      return base + add;
    };
    for (int32_t n : fvc) {
      if (!faceSpanAvailable(base, n, fvi.size())) {
        base = advanceFaceBase(base, n);
        continue;
      }
      for (int32_t k = 2; k < n; ++k) {
        const int32_t a = fvi[base];
        const int32_t b = fvi[base + static_cast<size_t>(k) - 1];
        const int32_t c = fvi[base + static_cast<size_t>(k)];
        if (a < 0 || b < 0 || c < 0) continue;
        out.push_back(static_cast<uint32_t>(a));
        out.push_back(static_cast<uint32_t>(b));
        out.push_back(static_cast<uint32_t>(c));
      }
      base = advanceFaceBase(base, n);
    }
  }

  static std::vector<uint32_t> faceTriangleStarts_(
      const std::vector<int32_t> &fvc) {
    std::vector<uint32_t> starts;
    starts.reserve(fvc.size() + 1);
    uint32_t cursor = 0;
    for (int32_t n : fvc) {
      starts.push_back(cursor);
      if (n >= 3) cursor += static_cast<uint32_t>(n - 2);
    }
    starts.push_back(cursor);
    return starts;
  }

  static std::vector<int32_t> matIntStatic_(
      const tinyusdz::next::UsdPrim &prim, const char *name) {
    const tinyusdz::next::Value *v = prim.GetPropertyValue(name);
    if (!v) return {};
    tinyusdz::next::Value tmp = *v;
    const std::vector<int32_t> *a = tmp.as_int_array();
    return a ? *a : std::vector<int32_t>{};
  }

  // Area-weighted vertex normals from the triangulated indices.
  static void computeNormals_(const std::vector<float> &pos,
                              const std::vector<uint32_t> &idx,
                              std::vector<float> &out) {
    out.assign(pos.size(), 0.0f);
    const size_t nv = pos.size() / 3;
    auto addTri = [&](uint32_t a, uint32_t b, uint32_t c) {
      if (a >= nv || b >= nv || c >= nv) return;
      const float ex1 = pos[b * 3] - pos[a * 3], ey1 = pos[b * 3 + 1] - pos[a * 3 + 1], ez1 = pos[b * 3 + 2] - pos[a * 3 + 2];
      const float ex2 = pos[c * 3] - pos[a * 3], ey2 = pos[c * 3 + 1] - pos[a * 3 + 1], ez2 = pos[c * 3 + 2] - pos[a * 3 + 2];
      const float nx = ey1 * ez2 - ez1 * ey2, ny = ez1 * ex2 - ex1 * ez2, nz = ex1 * ey2 - ey1 * ex2;
      for (uint32_t vi : {a, b, c}) { out[vi * 3] += nx; out[vi * 3 + 1] += ny; out[vi * 3 + 2] += nz; }
    };
    if (!idx.empty()) {
      for (size_t t = 0; t + 2 < idx.size(); t += 3) addTri(idx[t], idx[t + 1], idx[t + 2]);
    } else {
      for (uint32_t v = 0; v + 2 < nv; v += 3) addTri(v, v + 1, v + 2);
    }
    for (size_t i = 0; i < nv; ++i) {
      float x = out[i * 3], y = out[i * 3 + 1], z = out[i * 3 + 2];
      float l = std::sqrt(x * x + y * y + z * z);
      if (l > 0) { out[i * 3] = x / l; out[i * 3 + 1] = y / l; out[i * 3 + 2] = z / l; }
      else { out[i * 3 + 2] = 1.0f; }
    }
  }

  emscripten::val heapF_(const std::vector<float> &v, int comps) const {
    emscripten::val d = emscripten::val::object();
    d.set("ptr", static_cast<double>(reinterpret_cast<uintptr_t>(v.data())));
    d.set("length", static_cast<double>(v.size()));
    d.set("comps", comps);
    d.set("dtype", std::string("f32"));
    d.set("byteLength", static_cast<double>(v.size() * sizeof(float)));
    return d;
  }
  emscripten::val heapU32_(const std::vector<uint32_t> &v) const {
    emscripten::val d = emscripten::val::object();
    d.set("ptr", static_cast<double>(reinterpret_cast<uintptr_t>(v.data())));
    d.set("length", static_cast<double>(v.size()));
    d.set("comps", 1);
    d.set("dtype", std::string("u32"));
    d.set("byteLength", static_cast<double>(v.size() * sizeof(uint32_t)));
    return d;
  }
  static emscripten::val arr3_(const float *c) {
    emscripten::val a = emscripten::val::array();
    a.call<void>("push", c[0]);
    a.call<void>("push", c[1]);
    a.call<void>("push", c[2]);
    return a;
  }
  static emscripten::val matArray_(const std::array<double, 16> &m) {
    emscripten::val a = emscripten::val::array();
    for (double v : m) a.call<void>("push", v);
    return a;
  }
  static std::array<double, 16> identityMatrix_() {
    return {1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0};
  }
  static std::array<double, 16> multiplyMatrix_(
      const std::array<double, 16> &a, const std::array<double, 16> &b) {
    std::array<double, 16> r{};
    for (int row = 0; row < 4; ++row) {
      for (int col = 0; col < 4; ++col) {
        double v = 0.0;
        for (int k = 0; k < 4; ++k) {
          v += a[static_cast<size_t>(row * 4 + k)] *
               b[static_cast<size_t>(k * 4 + col)];
        }
        r[static_cast<size_t>(row * 4 + col)] = v;
      }
    }
    return r;
  }
  static std::array<double, 16> localMatrix_(
      const tinyusdz::next::UsdPrim &prim) {
    std::array<double, 16> m = identityMatrix_();
    tinyusdz::next::UsdGeomXform xform(prim);
    double raw[16];
    if (!xform.ComputeLocalTransform(raw)) return m;
    for (int i = 0; i < 16; ++i) m[static_cast<size_t>(i)] = raw[i];
    return m;
  }
  static std::array<double, 16> worldMatrix_(
      const tinyusdz::next::UsdPrim &prim) {
    std::vector<tinyusdz::next::UsdPrim> chain;
    for (tinyusdz::next::UsdPrim p = prim; p.IsValid(); p = p.GetParent()) {
      chain.push_back(p);
    }
    std::array<double, 16> world = identityMatrix_();
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
      const std::array<double, 16> local = localMatrix_(*it);
      world = multiplyMatrix_(local, world);
    }
    return world;
  }

  emscripten::val materialObjectForPrim_(
      const tinyusdz::next::UsdPrim &mat) {
    emscripten::val m = emscripten::val::object();
    if (!mat.IsValid()) return m;
    // Resolve the surface shader: prefer the material's outputs:surface (a
    // connection), but fall back to the first UsdPreviewSurface child shader —
    // the common case and robust when the output connection is not resolved.
    tinyusdz::next::UsdPrim shader;
    const std::string shaderPath = tinyusdz::next::GetSurfaceShader(stage_, mat);
    if (!shaderPath.empty()) shader = stage_.GetPrimAtPath(shaderPath);
    if (!shader.IsValid()) {
      for (const auto &ch : mat.GetChildren()) {
        if (tinyusdz::next::IsPreviewSurface(ch)) { shader = ch; break; }
      }
    }
    if (!shader.IsValid()) return m;
    tinyusdz::next::PreviewSurfaceData ps;
    if (!tinyusdz::next::GetPreviewSurfaceData(stage_, shader, &ps)) return m;
    m.set("baseColor", arr3_(ps.diffuse_color));
    m.set("metallic", ps.metallic);
    m.set("roughness", ps.roughness);
    m.set("opacity", ps.opacity);
    m.set("occlusion", ps.occlusion);
    m.set("emissive", arr3_(ps.emissive_color));
    if (ps.opacity_threshold > 0.0f) m.set("opacityThreshold", ps.opacity_threshold);
    // PreviewSurfaceData texture fields are connection paths to the UsdUVTexture
    // shader; resolve each to its inputs:file asset path for the JS caller.
    auto setTex = [&](const char *key, const std::string &connPath) {
      const std::string file = texFile_(connPath);
      if (!file.empty()) m.set(key, file);
    };
    setTex("baseColorTexture", ps.diffuse_texture);
    setTex("normalTexture", ps.normal_texture);
    setTex("roughnessTexture", ps.roughness_texture);
    setTex("metallicTexture", ps.metallic_texture);
    setTex("occlusionTexture", ps.occlusion_texture);
    setTex("emissiveTexture", ps.emissive_texture);
    return m;
  }

  emscripten::val materialObject_(int32_t material_id) const {
    emscripten::val m = emscripten::val::object();
    if (material_id < 0 ||
        static_cast<size_t>(material_id) >= materials_.size()) {
      return m;
    }
    const MaterialRecord &rec = materials_[static_cast<size_t>(material_id)];
    m.set("id", rec.id);
    m.set("key", rec.key);
    m.set("primPath", rec.prim_path);
    m.set("baseColor", arr3_(rec.base_color));
    m.set("metallic", rec.metallic);
    m.set("roughness", rec.roughness);
    m.set("opacity", rec.opacity);
    m.set("occlusion", rec.occlusion);
    m.set("emissive", arr3_(rec.emissive));
    if (rec.opacity_threshold > 0.0f) {
      m.set("opacityThreshold", rec.opacity_threshold);
    }
    if (!rec.base_color_texture.empty()) {
      m.set("baseColorTexture", rec.base_color_texture);
    }
    if (!rec.normal_texture.empty()) {
      m.set("normalTexture", rec.normal_texture);
    }
    if (!rec.roughness_texture.empty()) {
      m.set("roughnessTexture", rec.roughness_texture);
    }
    if (!rec.metallic_texture.empty()) {
      m.set("metallicTexture", rec.metallic_texture);
    }
    if (!rec.occlusion_texture.empty()) {
      m.set("occlusionTexture", rec.occlusion_texture);
    }
    if (!rec.emissive_texture.empty()) {
      m.set("emissiveTexture", rec.emissive_texture);
    }
    return m;
  }

  // Resolve the prim's bound material to UsdPreviewSurface values + texture
  // asset paths (resolved to GPU textures by the JS caller from the archive).
  emscripten::val resolveMaterial_(const tinyusdz::next::UsdPrim &prim) {
    tinyusdz::next::UsdPrim mat = tinyusdz::next::GetBoundMaterial(stage_, prim);
    return materialObjectForPrim_(mat);
  }

  void addGeomSubsetMaterials_(const tinyusdz::next::UsdPrim &prim,
                               emscripten::val &out) {
    std::vector<int32_t> fvc = matIntStatic_(prim, "faceVertexCounts");
    if (fvc.empty()) return;

    struct SubsetInfo {
      tinyusdz::next::UsdPrim prim;
      std::vector<int32_t> faces;
    };
    std::vector<SubsetInfo> subsets;
    for (const tinyusdz::next::UsdPrim &child : prim.GetChildren()) {
      if (!child.IsValid() || child.GetTypeName() != "GeomSubset") continue;
      const tinyusdz::next::Value *family = child.GetPropertyValue("familyName");
      if (family) {
        const std::string *tok = family->as_token();
        if (tok && *tok != "materialBind") continue;
      }
      std::vector<int32_t> faces = matIntStatic_(child, "indices");
      if (faces.empty()) continue;
      tinyusdz::next::UsdPrim mat = tinyusdz::next::GetBoundMaterial(stage_, child);
      if (!mat.IsValid()) continue;
      subsets.push_back({child, std::move(faces)});
    }
    if (subsets.empty()) return;

    std::vector<int> face_material(fvc.size(), -1);
    emscripten::val materials = emscripten::val::array();
    for (size_t i = 0; i < subsets.size(); ++i) {
      const int mat_index = static_cast<int>(i);
      for (int32_t face : subsets[i].faces) {
        if (face >= 0 && static_cast<size_t>(face) < face_material.size()) {
          face_material[static_cast<size_t>(face)] = mat_index;
        }
      }
      tinyusdz::next::UsdPrim mat =
          tinyusdz::next::GetBoundMaterial(stage_, subsets[i].prim);
      const int32_t material_id = registerMaterial_(mat);
      materials.set(mat_index, materialObject_(material_id));
    }

    const std::vector<uint32_t> tri_starts = faceTriangleStarts_(fvc);
    emscripten::val groups = emscripten::val::array();
    int group_index = 0;
    size_t face_begin = 0;
    while (face_begin < face_material.size()) {
      const int mat_index = face_material[face_begin];
      size_t face_end = face_begin + 1;
      while (face_end < face_material.size() &&
             face_material[face_end] == mat_index) {
        face_end++;
      }
      if (mat_index >= 0 && face_begin < tri_starts.size() &&
          face_end < tri_starts.size()) {
        const uint32_t start = tri_starts[face_begin] * 3u;
        const uint32_t count = (tri_starts[face_end] - tri_starts[face_begin]) * 3u;
        if (count > 0) {
          emscripten::val g = emscripten::val::object();
          g.set("start", static_cast<int>(start));
          g.set("count", static_cast<int>(count));
          g.set("materialIndex", mat_index);
          groups.set(group_index++, g);
        }
      }
      face_begin = face_end;
    }

    out.set("materials", materials);
    out.set("submeshes", groups);
  }

  tinyusdz::next::Stage stage_;
  std::vector<tinyusdz::next::UsdGeomMesh> meshes_;
  std::vector<OutputMesh> outputs_;
  std::vector<MaterialRecord> materials_;
  std::unordered_map<std::string, int32_t> material_key_to_id_;
  std::unordered_map<std::string, int32_t> material_path_to_id_;
  std::set<std::string> source_material_keys_;
  std::set<std::string> source_texture_keys_;
  std::set<std::string> texture_keys_;
  Stats stats_;
  bool loaded_ = false;
  bool material_dedup_ = false;
  bool mesh_merge_ = false;
  bool mesh_merge_bake_transform_ = false;
  bool flatten_render_tree_ = false;
  std::string error_;
  std::vector<float> s_points_, s_normals_, s_uv_;
  std::vector<uint32_t> s_indices_;
};

EMSCRIPTEN_BINDINGS(tinyusdz_next_render_stream) {
  emscripten::class_<NextUSDZConverterNative>("NextUSDZConverterNative")
      .constructor<>()
      .function("rewriteRoot", &NextUSDZConverterNative::rewriteRoot)
      .function("error", &NextUSDZConverterNative::error)
      .function("warn", &NextUSDZConverterNative::warn);

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
