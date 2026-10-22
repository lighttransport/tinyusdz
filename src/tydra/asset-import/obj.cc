// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <set>
#include <tuple>

#include "external/filesystem/include/ghc/filesystem.hpp"
#include "external/tinyobjloader-c/tiny_obj_c.h"
#include "import.hh"

namespace lightusd {
namespace tydra {
namespace import_asset {
namespace {
struct Budget {
  size_t used = 0;
  Reader& reader;
  bool exceeded = false;
  explicit Budget(Reader& r) : reader(r) {}
  static void* Alloc(void* ud, size_t size, size_t) {
    auto& b = *static_cast<Budget*>(ud);
    const size_t remaining = b.reader.options.max_memory -
        std::min(b.reader.bytes, b.reader.options.max_memory);
    if (size > remaining) {
      b.exceeded = true;
      return nullptr;
    }
    void* p = std::malloc(size ? size : 1);
    if (p) { b.used += size; b.reader.bytes += size; }
    return p;
  }
  static void Free(void* ud, void* p, size_t size) {
    auto& b = *static_cast<Budget*>(ud);
    if (p) {
      b.used -= std::min(size, b.used);
      b.reader.bytes -= std::min(size, b.reader.bytes);
      std::free(p);
    }
  }
};
struct Materials {
  Reader* reader;
  std::string source;
  std::map<std::string, std::string> anchors;
  std::set<std::string> explicit_roughness, explicit_metallic;
  static tobj_result Resolve(void* ud, const char* name, tobj_mtl_source* out) {
    auto& m = *static_cast<Materials*>(ud);
    auto* bytes = new (std::nothrow) std::vector<uint8_t>;
    if (!bytes) return TOBJ_ERR_ALLOC;
    std::string resolved, err;
    if (!m.reader->Read(name, m.source, bytes, &resolved, &err)) {
      delete bytes;
      m.reader->Warn("MTL: " + err);
      return TOBJ_ERR_NOT_FOUND;
    }
    // Record each newmtl's defining file: textures are relative to that MTL,
    // not to the OBJ (which may be in a different directory).
    std::string_view text(bytes->empty() ? "" : reinterpret_cast<const char*>(bytes->data()),
                          bytes->size());
    size_t pos = 0;
    std::string current_material;
    while (pos < text.size()) {
      size_t end = text.find('\n', pos);
      if (end == std::string::npos) end = text.size();
      std::string_view line = text.substr(pos, end - pos);
      size_t begin = line.find_first_not_of(" \t\r");
      if (begin != std::string::npos && line.compare(begin, 6, "newmtl") == 0 &&
          line.size() > begin + 6 &&
          (line[begin + 6] == ' ' || line[begin + 6] == '\t')) {
        begin = line.find_first_not_of(" \t", begin + 6);
        if (begin != std::string::npos) {
          size_t last = line.find_last_not_of(" \t\r");
          current_material = line.substr(begin, last - begin + 1);
          m.anchors[current_material] = resolved;
        }
      }
      if (begin != std::string::npos && line.size() > begin + 2 &&
          (line[begin + 2] == ' ' || line[begin + 2] == '\t')) {
        if (line.compare(begin, 2, "Pr") == 0)
          m.explicit_roughness.insert(current_material);
        if (line.compare(begin, 2, "Pm") == 0)
          m.explicit_metallic.insert(current_material);
      }
      pos = end + 1;
    }
    out->data = bytes->data();
    out->len = bytes->size();
    out->release_ud = bytes;
    out->release = [](void* p, const uint8_t*, size_t) {
      delete static_cast<std::vector<uint8_t>*>(p);
    };
    return TOBJ_OK;
  }
};
std::array<double, 3> Unit(std::array<double, 3> n) {
  double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
  if (len > 1e-30)
    for (double& v : n) v /= len;
  return n;
}
Json RGB(const float* v) { return Json::array({v[0], v[1], v[2]}); }
}  // namespace

Json MeshJSON(const Mesh& m) {
  return Json{{"name", m.name},
              {"type", m.type},
              {"geometry", Json{{"positions", Array(m.positions)},
                                {"normals", Array(m.normals)},
                                {"uvs", Array(m.uvs)},
                                {"colors", Array(m.colors)},
                                {"indices", Array(m.indices)},
                                {"faceVertexCounts", Array(m.counts)},
                                {"materialIndices", Array(m.materials)},
                                {"jointIndices", Array(m.joint_indices)},
                                {"jointWeights", Array(m.joint_weights)},
                                {"influences", m.influences}}}};
}

bool ReadOBJ(std::string_view text, const std::string& source, Reader& reader,
             bool triangulate, OBJ* obj, std::string* err) {
  if (!obj || text.size() > reader.options.max_input) {
    if (err) *err = "OBJ input limit exceeded or null output";
    return false;
  }
  // Numeric validation is done by the bounded C11 scanner, without a second
  // pass or a NUL-terminated copy of a potentially multi-gigabyte input.
  Budget budget(reader);
  const size_t remaining = reader.options.max_memory -
      std::min(reader.bytes, reader.options.max_memory);
  tobj_load_config cfg = tobj_default_config();
  cfg.allocator = {};
  cfg.allocator.alloc = Budget::Alloc;
  cfg.allocator.free = Budget::Free;
  cfg.allocator.user_data = &budget;
  cfg.allocator.max_alloc_size = 0;  // Budget::Alloc enforces the aggregate cap.
  cfg.triangulate = triangulate;
  cfg.vertex_color_fallback = false;
  cfg.store_vertex_colors = true;
  cfg.max_input_bytes = reader.options.max_input;
  cfg.max_vertices =
      std::min(size_t(INT32_MAX), remaining / (sizeof(float) * 3));
  cfg.max_indices =
      std::min(size_t(INT32_MAX), remaining / sizeof(tobj_index));
  cfg.max_faces = cfg.max_indices;
  cfg.max_face_arity = 65536;
  cfg.max_line_bytes =
      std::min(size_t(16) * 1024 * 1024, reader.options.max_input);
  Materials materials{&reader, source, {}, {}, {}};
  cfg.mtl_resolver = Materials::Resolve;
  cfg.mtl_resolver_user_data = &materials;
  tobj_scene_f scene{};
  tobj_diag diag{};
  diag.user_data = &reader;
  diag.on_message = [](void* p, tobj_severity severity, size_t line,
                       const char* message, size_t len) {
    auto& r = *static_cast<Reader*>(p);
    std::string msg =
        "OBJ line " + std::to_string(line) + ": " + std::string(message, len);
    if (severity == TOBJ_SEV_WARNING)
      r.Warn(msg);
    else
      r.failure = msg;
  };
  tobj_result status = tobj_load_obj_from_memory_f(
      &scene, reinterpret_cast<const uint8_t*>(text.data()), text.size(), &cfg,
      &diag);
  if (status != TOBJ_OK) {
    if (err)
      *err = budget.exceeded
                 ? "OBJ memory budget exceeded"
                 : (reader.failure.empty() ? tobj_result_string(status)
                                           : reader.failure);
    return false;
  }
  struct Cleanup {
    tobj_scene_f* scene;
    ~Cleanup() { tobj_scene_free_f(scene); }
  } cleanup{&scene};
  const auto& a = scene.attrib;
  std::vector<const tobj_skin_weight_f*> weights;
  size_t influence_count = 0;
  if (a.num_skin_weights) {
    if (!reader.ChargeArray(a.vertices.count / 3, sizeof(void*), err)) return false;
    weights.resize(a.vertices.count / 3, nullptr);
    for (size_t i = 0; i < a.num_skin_weights; ++i) {
      const auto& row = a.skin_weights[i];
      weights[static_cast<size_t>(row.vertex_id)] = &row;
      influence_count = std::max(influence_count, row.num_weights);
    }
    if (!influence_count)
      reader.Warn("OBJ vw records have no influences; omitted skin binding");
  }
  auto skin = [&](Mesh* mesh) {
    if (!influence_count) return true;
    if (influence_count > size_t(INT32_MAX) ||
        !reader.ChargeArray(mesh->source_vertices.size(), influence_count * 8, err))
      return false;
    const size_t elements = mesh->source_vertices.size() * influence_count;
    mesh->joint_indices.reserve(elements);
    mesh->joint_weights.reserve(elements);
    mesh->influences = static_cast<int32_t>(influence_count);
    size_t missing = 0;
    for (int32_t source_id : mesh->source_vertices) {
      const auto* row = weights[static_cast<size_t>(source_id)];
      if (!row || !row->num_weights) ++missing;
      for (size_t k = 0; k < influence_count; ++k) {
        const bool present = row && k < row->num_weights;
        mesh->joint_indices.push_back(present ? row->weights[k].joint_id : 0);
        mesh->joint_weights.push_back(present ? row->weights[k].weight : 0.0f);
      }
    }
    if (missing)
      reader.Warn("OBJ missing skin weights for " + std::to_string(missing) +
                  " points on " + mesh->name + "; padded with zero weights");
    return true;
  };
  for (size_t i = 0; i < a.vertices.count; i++)
    if (!std::isfinite(a.vertices.ptr[i])) {
      if (err) *err = "OBJ has non-finite positions";
      return false;
    }
  if (!reader.ChargeArray(scene.num_materials, 4096, err)) return false;
  for (size_t mi = 0; mi < scene.num_materials; mi++) {
    const auto& m = scene.materials[mi];
    const std::string name = m.name ? m.name : "material";
    Json mat{{"name", name},
             {"rgba", Json::array({m.diffuse[0], m.diffuse[1], m.diffuse[2],
                                   m.dissolve})},
             {"specularColor", RGB(m.specular)},
             {"emissiveColor", RGB(m.emission)},
             {"ior", m.ior},
             {"roughness", materials.explicit_roughness.count(name) != 0
                               ? m.roughness
                               : float(std::sqrt(2.0 / (m.shininess + 2.0)))},
             {"metallic", m.metallic},
             {"useSpecularWorkflow",
              materials.explicit_metallic.count(name) ? 0 : 1}};
    Json maps = Json::array();
    auto texture = [&](const char* file, const char* input,
                       const tobj_texture_option_f& opt, bool color) {
      if (!file || !*file) return;
      std::vector<uint8_t> bytes;
      std::string resolved, why;
      const std::string anchor =
          materials.anchors.count(name) ? materials.anchors[name] : source;
      if (!reader.Read(file, anchor, &bytes, &resolved, &why)) {
        reader.Warn("Texture: " + why);
        return;
      }
      maps.push_back(Json{
          {"input", input},
          {"file", resolved},
          {"color", color},
          {"scale", Json::array({opt.scale[0], opt.scale[1]})},
          {"offset", Json::array({opt.origin_offset[0], opt.origin_offset[1]})},
          {"clamp", opt.clamp}});
    };
    texture(m.diffuse_texname, "diffuseColor", m.diffuse_texopt, true);
    texture(m.specular_texname, "specularColor", m.specular_texopt, true);
    texture(m.emissive_texname, "emissiveColor", m.emissive_texopt, true);
    texture(m.alpha_texname, "opacity", m.alpha_texopt, false);
    texture(m.roughness_texname, "roughness", m.roughness_texopt, false);
    texture(m.metallic_texname, "metallic", m.metallic_texopt, false);
    texture(m.normal_texname, "normal", m.normal_texopt, false);
    if (m.bump_texname && *m.bump_texname)
      reader.Warn(
          "MTL bump maps require height-to-normal conversion and were not "
          "connected");
    if ((m.displacement_texname && *m.displacement_texname) || m.sheen != 0 ||
        m.anisotropy != 0)
      reader.Warn("MTL contains shading features outside UsdPreviewSurface");
    mat["maps"] = std::move(maps);
    obj->materials.push_back(std::move(mat));
  }
  // Accumulate area-weighted geometric normals across all shapes sharing a
  // position and smoothing group. Newell's formula works for n-gons too.
  using SmoothKey = std::pair<int, unsigned>;
  std::map<SmoothKey, std::array<double, 3>> smooth;
  std::vector<std::vector<std::array<double, 3>>> face_normals(
      scene.num_shapes);
  for (size_t si = 0; si < scene.num_shapes; si++) {
    const auto& shape = scene.shapes[si];
    size_t off = 0;
    auto& fn = face_normals[si];
    if (!reader.ChargeArray(shape.mesh.num_faces, sizeof(std::array<double, 3>), err))
      return false;
    fn.resize(shape.mesh.num_faces);
    for (size_t f = 0; f < shape.mesh.num_faces; f++) {
      unsigned count = shape.mesh.num_face_vertices[f];
      if (count < 3 || off + count > shape.mesh.num_indices) {
        if (err) *err = "Invalid OBJ face";
        return false;
      }
      std::array<double, 3> n{{0, 0, 0}};
      for (unsigned k = 0; k < count; k++) {
        int vi = shape.mesh.indices[off + k].vertex_index;
        int vj = shape.mesh.indices[off + (k + 1) % count].vertex_index;
        if (vi < 0 || vj < 0 || size_t(vi) >= a.vertices.count / 3 ||
            size_t(vj) >= a.vertices.count / 3) {
          if (err) *err = "OBJ vertex index out of range";
          return false;
        }
        const float *p = a.vertices.ptr + size_t(vi) * 3,
                    *q = a.vertices.ptr + size_t(vj) * 3;
        n[0] += (double(p[1]) - q[1]) * (double(p[2]) + q[2]);
        n[1] += (double(p[2]) - q[2]) * (double(p[0]) + q[0]);
        n[2] += (double(p[0]) - q[0]) * (double(p[1]) + q[1]);
      }
      fn[f] = Unit(n);
      unsigned sg = shape.mesh.smoothing_group_ids
                        ? shape.mesh.smoothing_group_ids[f]
                        : 0;
      if (sg)
        for (unsigned k = 0; k < count; k++) {
          const SmoothKey key{shape.mesh.indices[off + k].vertex_index, sg};
          auto found = smooth.find(key);
          if (found == smooth.end()) {
            if (!reader.Charge(sizeof(SmoothKey) + sizeof(std::array<double, 3>) +
                               4 * sizeof(void*), err)) return false;
            found = smooth.emplace(key, std::array<double, 3>{}).first;
          }
          auto& sum = found->second;
          for (int c = 0; c < 3; c++) sum[c] += n[c];
        }
      off += count;
    }
  }
  for (size_t si = 0; si < scene.num_shapes; si++) {
    const auto& shape = scene.shapes[si];
    if ((triangulate && (shape.lines.num_lines || shape.points.num_indices)) ||
        shape.freeform)
      reader.Warn(
          "OBJ line, point or free-form primitives are not converted to "
          "polygon meshes");
    if (!shape.mesh.num_faces) continue;
    Mesh mesh;
    mesh.name = shape.name && *shape.name ? shape.name : "mesh";
    size_t off = 0;
    bool uv_complete = true;
    using Key = std::tuple<int, int, int, unsigned, size_t>;
    std::map<Key, int32_t> corners;
    // Output vectors (including capacity slack), corner-map nodes and scratch.
    // Use checked multiplication before any large allocations or int casts.
    if (shape.mesh.num_indices > size_t(INT32_MAX) ||
        !reader.ChargeArray(shape.mesh.num_indices, 160, err)) {
      if (err && err->empty()) *err = "OBJ expanded index limit exceeded";
      return false;
    }
    const size_t initial_points = std::min(shape.mesh.num_indices, a.vertices.count / 3);
    mesh.positions.reserve(initial_points * 3);
    mesh.normals.reserve(initial_points * 3);
    mesh.source_vertices.reserve(initial_points);
    mesh.indices.reserve(shape.mesh.num_indices);
    mesh.counts.reserve(shape.mesh.num_faces);
    mesh.materials.reserve(shape.mesh.num_faces);
    if (a.texcoords.count) mesh.uvs.reserve(initial_points * 2);
    if (a.colors.count) mesh.colors.reserve(initial_points * 3);
    for (size_t f = 0; f < shape.mesh.num_faces; f++) {
      unsigned count = shape.mesh.num_face_vertices[f];
      unsigned sg = shape.mesh.smoothing_group_ids
                        ? shape.mesh.smoothing_group_ids[f]
                        : 0;
      mesh.counts.push_back(static_cast<int32_t>(count));
      mesh.materials.push_back(
          shape.mesh.material_ids ? shape.mesh.material_ids[f] : -1);
      for (unsigned k = 0; k < count; k++) {
        const auto ix = shape.mesh.indices[off + k];
        bool hn = ix.normal_index >= 0 &&
                  size_t(ix.normal_index) < a.normals.count / 3;
        bool ht = ix.texcoord_index >= 0 &&
                  size_t(ix.texcoord_index) < a.texcoords.count / 2;
        uv_complete &= ht;
        Key key{ix.vertex_index, hn ? ix.normal_index : -1,
                ht ? ix.texcoord_index : -1, hn ? 0 : sg, hn || sg ? 0 : f + 1};
        auto found = corners.find(key);
        if (found == corners.end()) {
          int32_t index = static_cast<int32_t>(mesh.positions.size() / 3);
          found = corners.emplace(key, index).first;
          mesh.source_vertices.push_back(ix.vertex_index);
          const float* p = a.vertices.ptr + size_t(ix.vertex_index) * 3;
          mesh.positions.insert(mesh.positions.end(), p, p + 3);
          auto n = hn ? std::array<
                            double,
                            3>{{a.normals.ptr[size_t(ix.normal_index) * 3],
                                a.normals.ptr[size_t(ix.normal_index) * 3 + 1],
                                a.normals.ptr[size_t(ix.normal_index) * 3 + 2]}}
                      : (sg ? Unit(smooth[{ix.vertex_index, sg}])
                            : face_normals[si][f]);
          for (double c : n) mesh.normals.push_back(static_cast<float>(c));
          mesh.uvs.push_back(ht ? a.texcoords.ptr[size_t(ix.texcoord_index) * 2]
                                : 0);
          mesh.uvs.push_back(
              ht ? a.texcoords.ptr[size_t(ix.texcoord_index) * 2 + 1] : 0);
          if (a.colors.count == a.vertices.count)
            mesh.colors.insert(mesh.colors.end(),
                               a.colors.ptr + size_t(ix.vertex_index) * 3,
                               a.colors.ptr + size_t(ix.vertex_index) * 3 + 3);
        }
        mesh.indices.push_back(found->second);
      }
      off += count;
    }
    if (!uv_complete) {
      mesh.uvs.clear();
      if (a.texcoords.count)
        reader.Warn("OBJ has incomplete UVs; omitted primvars:st on " +
                    mesh.name);
    }
    if (!skin(&mesh)) return false;
    obj->meshes.push_back(std::move(mesh));
  }
  if (!triangulate)
    for (size_t si = 0; si < scene.num_shapes; ++si) {
      const auto& shape = scene.shapes[si];
      for (bool lines : {true, false}) {
        const size_t count =
            lines ? shape.lines.num_indices : shape.points.num_indices;
        if (!count) continue;
        if (count > size_t(INT32_MAX) || !reader.ChargeArray(count, 64, err)) {
          if (err && err->empty()) *err = "OBJ point/line index limit exceeded";
          return false;
        }
        Mesh primitive;
        primitive.positions.reserve(count * 3);
        primitive.source_vertices.reserve(count);
        primitive.type = lines ? "BasisCurves" : "Points";
        primitive.name =
            std::string(shape.name && *shape.name ? shape.name : "mesh") +
            (lines ? "_curves" : "_points");
        bool complete_uv = true;
        for (size_t i = 0; i < count; ++i) {
          const auto ix =
              lines ? shape.lines.indices[i] : shape.points.indices[i];
          if (ix.vertex_index < 0 ||
              size_t(ix.vertex_index) >= a.vertices.count / 3) {
            if (err) *err = "OBJ point/line index out of range";
            return false;
          }
          const size_t v = static_cast<size_t>(ix.vertex_index);
          primitive.source_vertices.push_back(ix.vertex_index);
          primitive.positions.insert(primitive.positions.end(),
                                     a.vertices.ptr + v * 3,
                                     a.vertices.ptr + v * 3 + 3);
          if (a.colors.count == a.vertices.count)
            primitive.colors.insert(primitive.colors.end(),
                                    a.colors.ptr + v * 3,
                                    a.colors.ptr + v * 3 + 3);
          if (ix.texcoord_index >= 0 &&
              size_t(ix.texcoord_index) < a.texcoords.count / 2) {
            const size_t t = static_cast<size_t>(ix.texcoord_index) * 2;
            primitive.uvs.insert(primitive.uvs.end(), a.texcoords.ptr + t,
                                 a.texcoords.ptr + t + 2);
          } else
            complete_uv = false;
        }
        if (!complete_uv) primitive.uvs.clear();
        if (lines)
          for (size_t i = 0; i < shape.lines.num_lines; ++i)
            primitive.counts.push_back(shape.lines.num_line_vertices[i]);
        if (!skin(&primitive)) return false;
        obj->meshes.push_back(std::move(primitive));
      }
    }
  if (obj->meshes.empty()) {
    if (err)
      *err = triangulate ? "OBJ contains no polygon faces"
                         : "OBJ contains no supported primitives";
    return false;
  }
  return true;
}

bool BuildOBJScene(std::string_view text, const std::string& source,
                   const Options& options, OBJ* obj, Result* result,
                   std::string* err) {
  if (!obj || !result || text.size() > options.max_input ||
      text.size() > options.max_memory) {
    if (err) *err = "OBJ input limit exceeded";
    return false;
  }
  *result = Result{};
  if (options.up_axis != "Z" && options.up_axis != "Y") {
    if (err) *err = "up_axis must be Z or Y";
    return false;
  }
  Reader reader(options);
  reader.root_source = source;
  reader.allow_unanchored = !source.empty();
  *obj = OBJ{};
  if (!reader.Charge(text.size(), err) ||
      !ReadOBJ(text, source, reader, false, obj, err)) return false;
  result->payload = Json{{"sourceFormat", "obj"}, {"noPhysics", true},
      {"upAxis", options.up_axis}, {"materials", std::move(obj->materials)}};
  if (!GatherTextures(reader, result, err)) return false;
  result->warnings = reader.warnings;
  result->memory_estimate = reader.bytes;
  return true;
}

bool BuildOBJPayload(std::string_view text, const std::string& source,
                     const Options& options, Result* result, std::string* err) {
  OBJ obj;
  if (!BuildOBJScene(text, source, options, &obj, result, err)) return false;
  // The next reader bypasses this compatibility bridge. Bound its scalar JSON
  // nodes as well, so legacy/CLI imports cannot silently amplify a small input.
  Reader conversion_budget(options);
  conversion_budget.bytes = result->memory_estimate;
  for (const auto& mesh : obj.meshes) {
    for (size_t count : {mesh.positions.size(), mesh.normals.size(), mesh.uvs.size(),
                        mesh.colors.size(), mesh.indices.size(), mesh.counts.size(),
                        mesh.materials.size(), mesh.joint_indices.size(), mesh.joint_weights.size()})
      if (!conversion_budget.ChargeArray(count, sizeof(Json), err)) return false;
    if (!conversion_budget.ChargeArray(result->payload["materials"].size(), sizeof(Json), err) ||
        !conversion_budget.Charge(2048, err)) return false;
  }
  Json names = Json::array(), visuals = Json::array();
  for (const auto& m : result->payload["materials"]) names.push_back(m["name"]);
  for (const auto& m : obj.meshes) {
    Json visual = MeshJSON(m);
    visual["renderOnly"] = true;
    visual["geometry"]["materialNames"] = names;
    visuals.push_back(std::move(visual));
  }
  result->payload["links"] = Json::array({Json{{"name", "Geometry"},
      {"visuals", std::move(visuals)}}});
  return true;
}

bool BuildOBJPayloadFile(const std::string& filename, const Options& options,
                         Result* result, std::string* err) {
  Reader reader(options);
  std::vector<uint8_t> bytes;
  std::string source;
  if (!reader.Read(filename, "", &bytes, &source, err)) return false;
  return BuildOBJPayload(
      std::string_view(bytes.empty() ? "" : reinterpret_cast<const char*>(bytes.data()), bytes.size()),
      source, options, result, err);
}
}  // namespace import_asset
}  // namespace tydra
}  // namespace lightusd
