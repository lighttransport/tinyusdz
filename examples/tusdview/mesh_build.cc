// SPDX-License-Identifier: Apache-2.0
#include "mesh_build.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <unordered_map>

#include "light3d/math.h"
#include "tydra/render-data-converter.hh"
#include "tydra/render-data-shader.hh"

namespace tusdview {

namespace tydra = tinyusdz::tydra;
using tinyusdz::value::matrix4d;

namespace {

// USD value::matrix4d is row-major (row-vector, pre-multiply: p' = p*M).
// light3d::Mat4 is column-major (column-vector: p' = M*p). For the same
// geometric transform M_gl = transpose(M_usd); combined with the storage-order
// difference this reduces to an element-wise copy: out[i*4+j] = M.m[i][j].
void MatToColMajor(const matrix4d& M, float out[16]) {
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      out[i * 4 + j] = static_cast<float>(M.m[i][j]);
    }
  }
}

// Read up to `n` floats of vertex item `item` from a float-typed VertexAttribute.
// Missing/out-of-range components are zero.
void ReadFloats(const tydra::VertexAttribute& a, size_t item, int n, float* out) {
  for (int k = 0; k < n; ++k) out[k] = 0.0f;
  if (a.empty()) return;
  size_t comps = a.format_size() / sizeof(float);
  if (comps == 0) return;
  if (item >= a.vertex_count()) return;
  const float* f = reinterpret_cast<const float*>(a.buffer());
  if (!f) return;
  for (int k = 0; k < n && static_cast<size_t>(k) < comps; ++k) {
    out[k] = f[item * comps + static_cast<size_t>(k)];
  }
}

bool AttrUsableAsVertex(const tydra::VertexAttribute& a, size_t vertexCount) {
  return !a.empty() && a.is_vertex() && a.vertex_count() == vertexCount &&
         (a.format_size() % sizeof(float)) == 0;
}

// Collect mesh_id -> world matrix from the node hierarchy (first occurrence).
void CollectMeshTransforms(const tydra::Node& node,
                           std::unordered_map<int, matrix4d>* out) {
  if (node.nodeType == tydra::NodeType::Mesh && node.id >= 0) {
    out->emplace(node.id, node.global_matrix);
  }
  for (const auto& c : node.children) {
    CollectMeshTransforms(c, out);
  }
}

int MapWrap(tydra::UVTexture::WrapMode w) {
  switch (w) {
    case tydra::UVTexture::WrapMode::REPEAT:
      return static_cast<int>(WrapMode::Repeat);
    case tydra::UVTexture::WrapMode::MIRROR:
      return static_cast<int>(WrapMode::Mirror);
    case tydra::UVTexture::WrapMode::CLAMP_TO_BORDER:
      return static_cast<int>(WrapMode::ClampToBorder);
    case tydra::UVTexture::WrapMode::CLAMP_TO_EDGE:
    default:
      return static_cast<int>(WrapMode::ClampToEdge);
  }
}

bool IsSrgb(tydra::ColorSpace cs) {
  return cs == tydra::ColorSpace::sRGB || cs == tydra::ColorSpace::sRGB_Texture ||
         cs == tydra::ColorSpace::sRGB_DisplayP3;
}

bool MeshHasSkinData(const tydra::RenderMesh& mesh, size_t pointCount) {
  const auto& jw = mesh.joint_and_weights;
  if (mesh.skel_id < 0 || jw.elementSize < 1) return false;
  const size_t infl = static_cast<size_t>(jw.elementSize);
  return jw.jointIndices.size() == pointCount * infl &&
         jw.jointWeights.size() == pointCount * infl;
}

void SetIdentity4(float out[16]) {
  matrix4d ident = matrix4d::identity();
  MatToColMajor(ident, out);
}

void WriteSkinVertex(const tydra::RenderMesh& mesh, size_t srcPoint,
                     size_t dstVertex, DrawMeshCPU* dm) {
  if (!dm || dm->jointIdx.empty() || dm->jointWt.empty()) return;
  const auto& jw = mesh.joint_and_weights;
  const size_t infl = static_cast<size_t>(jw.elementSize);
  if (srcPoint >= mesh.points.size()) return;
  const size_t src = srcPoint * infl;
  if (src + infl > jw.jointIndices.size() || src + infl > jw.jointWeights.size()) {
    return;
  }
  std::array<std::pair<float, uint32_t>, 4> top{};
  for (auto& v : top) v = {0.0f, 0u};
  for (size_t k = 0; k < infl; ++k) {
    const float w = jw.jointWeights[src + k];
    const int ji = jw.jointIndices[src + k];
    if (w <= 0.0f || ji < 0) continue;
    const uint32_t j = static_cast<uint32_t>(ji);
    for (size_t slot = 0; slot < top.size(); ++slot) {
      if (w > top[slot].first) {
        for (size_t m = top.size() - 1; m > slot; --m) top[m] = top[m - 1];
        top[slot] = {w, j};
        break;
      }
    }
  }
  float sum = 0.0f;
  for (const auto& v : top) sum += v.first;
  const size_t dst = dstVertex * 4;
  for (size_t k = 0; k < 4; ++k) {
    dm->jointIdx[dst + k] = top[k].second;
    dm->jointWt[dst + k] = (sum > 0.0f) ? (top[k].first / sum) : 0.0f;
  }
}

// Decode a TextureImage's buffer into an RGBA8 light3d::Image. Returns false if
// the image cannot be decoded (caller skips it).
bool DecodeToRGBA8(const tydra::RenderScene& rs, const tydra::TextureImage& img,
                   light3d::Image* out) {
  if (img.buffer_id < 0 ||
      static_cast<size_t>(img.buffer_id) >= rs.buffers.size()) {
    return false;
  }
  if (!img.decoded || img.width <= 0 || img.height <= 0 || img.channels <= 0) {
    return false;
  }
  const tydra::BufferData& buf = rs.buffers[static_cast<size_t>(img.buffer_id)];
  const size_t w = static_cast<size_t>(img.width);
  const size_t h = static_cast<size_t>(img.height);
  const size_t ch = static_cast<size_t>(img.channels);
  const size_t npix = w * h;

  out->width = img.width;
  out->height = img.height;
  out->channels = 4;
  out->data.assign(npix * 4, 255);

  auto store = [&](size_t pix, float r, float g, float b, float a) {
    auto clamp8 = [](float v) -> uint8_t {
      if (v < 0.0f) v = 0.0f;
      if (v > 255.0f) v = 255.0f;
      return static_cast<uint8_t>(v + 0.5f);
    };
    out->data[pix * 4 + 0] = clamp8(r);
    out->data[pix * 4 + 1] = clamp8(g);
    out->data[pix * 4 + 2] = clamp8(b);
    out->data[pix * 4 + 3] = clamp8(a);
  };

  if (img.texelComponentType == tydra::ComponentType::UInt8) {
    if (buf.data.size() < npix * ch) return false;
    const uint8_t* p = buf.data.data();
    for (size_t i = 0; i < npix; ++i) {
      float c0 = p[i * ch + 0];
      float c1 = ch > 1 ? p[i * ch + 1] : c0;
      float c2 = ch > 2 ? p[i * ch + 2] : c0;
      float c3 = ch > 3 ? p[i * ch + 3] : 255.0f;
      if (ch == 1) { c1 = c0; c2 = c0; c3 = 255.0f; }
      else if (ch == 2) { c2 = c0; c1 = c0; c3 = p[i * ch + 1]; }
      store(i, c0, c1, c2, c3);
    }
    return true;
  }
  if (img.texelComponentType == tydra::ComponentType::Float) {
    const size_t need = npix * ch * sizeof(float);
    if (buf.data.size() < need) return false;
    const float* p = reinterpret_cast<const float*>(buf.data.data());
    for (size_t i = 0; i < npix; ++i) {
      float c0 = p[i * ch + 0] * 255.0f;
      float c1 = ch > 1 ? p[i * ch + 1] * 255.0f : c0;
      float c2 = ch > 2 ? p[i * ch + 2] * 255.0f : c0;
      float c3 = ch > 3 ? p[i * ch + 3] * 255.0f : 255.0f;
      if (ch == 1) { c1 = c0; c2 = c0; c3 = 255.0f; }
      else if (ch == 2) { c2 = c0; c1 = c0; c3 = p[i * ch + 1] * 255.0f; }
      store(i, c0, c1, c2, c3);
    }
    return true;
  }
  return false;  // unsupported texel type
}

// --- Shared per-element builders (used by both BuildDrawScene and the
// streaming path so the two produce identical output) ---------------------

// Build the renderable textures (dedup by texture_image_id) and a mapping
// drawTexMap[uvTextureIndex] -> DrawScene texture index (-1 if skipped).
void BuildDrawTextures(const tydra::RenderScene& rs, DrawScene* out,
                       std::vector<int>* drawTexMap) {
  drawTexMap->assign(rs.textures.size(), -1);
  std::unordered_map<int64_t, int> imgToDrawTex;
  for (size_t uvIdx = 0; uvIdx < rs.textures.size(); ++uvIdx) {
    const tydra::UVTexture& uv = rs.textures[uvIdx];
    const int64_t imgId = uv.texture_image_id;
    if (imgId < 0 || static_cast<size_t>(imgId) >= rs.images.size()) {
      out->skipped.push_back("texture '" + uv.prim_name +
                             "': no image (possibly UDIM/unresolved)");
      continue;
    }
    auto found = imgToDrawTex.find(imgId);
    if (found != imgToDrawTex.end()) {
      (*drawTexMap)[uvIdx] = found->second;
      continue;
    }
    const tydra::TextureImage& img = rs.images[static_cast<size_t>(imgId)];
    DrawTextureCPU tex;
    if (!DecodeToRGBA8(rs, img, &tex.image)) {
      out->skipped.push_back("texture '" + uv.prim_name +
                             "': undecoded/unsupported image");
      continue;
    }
    tex.srgb = IsSrgb(img.colorSpace);
    tex.wrapS = MapWrap(uv.wrapS);
    tex.wrapT = MapWrap(uv.wrapT);
    int drawIdx = static_cast<int>(out->textures.size());
    out->textures.push_back(std::move(tex));
    imgToDrawTex[imgId] = drawIdx;
    (*drawTexMap)[uvIdx] = drawIdx;
  }
}

// Build the renderable materials from the RenderScene, mapping UVTexture
// indices to DrawScene texture slots via `drawTexMap`.
void BuildDrawMaterials(const tydra::RenderScene& rs, DrawScene* out,
                        const std::vector<int>& drawTexMap) {
  auto mapTex = [&](int texId) -> int {
    if (texId < 0 || static_cast<size_t>(texId) >= drawTexMap.size()) return -1;
    return drawTexMap[static_cast<size_t>(texId)];
  };

  out->materials.reserve(rs.materials.size());
  for (const auto& mat : rs.materials) {
    DrawMaterialCPU dm;
    dm.name = mat.name;
    if (mat.surfaceShader.has_value()) {
      const tydra::PreviewSurfaceShader& s = *mat.surfaceShader;
      dm.baseColorTex = mapTex(s.diffuseColor.texture_id);
      dm.emissiveTex = mapTex(s.emissiveColor.texture_id);
      dm.normalTex = mapTex(s.normal.texture_id);
      int mrTex = mapTex(s.metallic.texture_id);
      if (mrTex < 0) mrTex = mapTex(s.roughness.texture_id);
      dm.metalRoughTex = mrTex;
      // When a parameter is driven by a texture, the shader multiplies the
      // texel by the factor below, so use a neutral factor (1) instead of the
      // constant fallback (which would darken/override the texture).
      dm.baseColor[0] = dm.baseColorTex >= 0 ? 1.0f : s.diffuseColor.value[0];
      dm.baseColor[1] = dm.baseColorTex >= 0 ? 1.0f : s.diffuseColor.value[1];
      dm.baseColor[2] = dm.baseColorTex >= 0 ? 1.0f : s.diffuseColor.value[2];
      dm.metallic = (s.metallic.texture_id >= 0) ? 1.0f : s.metallic.value;
      dm.roughness = (s.roughness.texture_id >= 0) ? 1.0f : s.roughness.value;
      dm.emissive[0] = dm.emissiveTex >= 0 ? 1.0f : s.emissiveColor.value[0];
      dm.emissive[1] = dm.emissiveTex >= 0 ? 1.0f : s.emissiveColor.value[1];
      dm.emissive[2] = dm.emissiveTex >= 0 ? 1.0f : s.emissiveColor.value[2];
      dm.alpha = s.opacity.value;
      switch (mat.materialTag) {
        case tydra::MaterialTag::Masked:
          dm.alphaMode = static_cast<int>(AlphaMode::Mask);
          dm.alphaCutoff = s.opacityThreshold.value;
          break;
        case tydra::MaterialTag::Translucent:
          dm.alphaMode = static_cast<int>(AlphaMode::Blend);
          break;
        case tydra::MaterialTag::Opaque:
        default:
          dm.alphaMode = static_cast<int>(AlphaMode::Opaque);
          break;
      }
    } else if (mat.openPBRShader.has_value()) {
      const tydra::OpenPBRSurfaceShader& s = *mat.openPBRShader;
      dm.baseColor[0] = s.base_color.value[0];
      dm.baseColor[1] = s.base_color.value[1];
      dm.baseColor[2] = s.base_color.value[2];
      dm.metallic = s.base_metalness.value;
      dm.roughness = s.base_roughness.value;
      dm.emissive[0] = s.emission_color.value[0];
      dm.emissive[1] = s.emission_color.value[1];
      dm.emissive[2] = s.emission_color.value[2];
      dm.alpha = s.opacity.value;
      dm.baseColorTex = mapTex(s.base_color.texture_id);
      dm.alphaMode = (mat.materialTag == tydra::MaterialTag::Translucent)
                         ? static_cast<int>(AlphaMode::Blend)
                         : static_cast<int>(AlphaMode::Opaque);
    }
    // else: leave default gray.
    out->materials.push_back(std::move(dm));
  }
}

// Build the geometry (interleaved vertices, indices, submeshes, normals) of one
// RenderMesh into `dm`. World transform is left as identity; call PlaceDrawMesh
// afterwards. Returns false if the mesh has no usable geometry.
bool MakeDrawMesh(const tydra::RenderMesh& mesh, DrawMeshCPU* dmOut) {
  const size_t nPoints = mesh.points.size();
  const std::vector<uint32_t>& srcIndices = mesh.faceVertexIndices();
  if (nPoints == 0 || srcIndices.empty()) {
    return false;
  }

  DrawMeshCPU dm;
  dm.name = mesh.prim_name;
  dm.absPath = mesh.abs_path;
  dm.doubleSided = mesh.doubleSided;
  SetIdentity4(dm.skinGeomBind);
  dm.skelId = mesh.skel_id;
  if (MeshHasSkinData(mesh, nPoints)) {
    dm.jointIdx.assign(nPoints * 4, 0u);
    dm.jointWt.assign(nPoints * 4, 0.0f);
    MatToColMajor(mesh.joint_and_weights.geomBindTransform, dm.skinGeomBind);
  }

  // Primary texcoord slot (0 if present, else the first available).
  const tydra::VertexAttribute* uvAttr = nullptr;
  {
    auto it = mesh.texcoords.find(0);
    if (it != mesh.texcoords.end()) {
      uvAttr = &it->second;
    } else if (!mesh.texcoords.empty()) {
      uvAttr = &mesh.texcoords.begin()->second;
    }
  }

  const bool normalsPerVertex = AttrUsableAsVertex(mesh.normals, nPoints);
  const bool uvPerVertex = uvAttr && AttrUsableAsVertex(*uvAttr, nPoints);
  bool gotNormals = false;

  if (mesh.is_single_indexable) {
    // Indexed path: one DrawVertex per point; indices reference points.
    dm.vertices.resize(nPoints);
    for (size_t i = 0; i < nPoints; ++i) {
      DrawVertex& v = dm.vertices[i];
      v.px = mesh.points[i][0];
      v.py = mesh.points[i][1];
      v.pz = mesh.points[i][2];
      float nrm[3] = {0, 0, 0};
      if (normalsPerVertex) ReadFloats(mesh.normals, i, 3, nrm);
      v.nx = nrm[0]; v.ny = nrm[1]; v.nz = nrm[2];
      float uv[2] = {0, 0};
      if (uvPerVertex) ReadFloats(*uvAttr, i, 2, uv);
      // Flip V: USD `st` has v=0 at the image bottom, but decoded images are
      // top-row-first and uploaded so v=0 samples the top, so invert here.
      v.u = uv[0]; v.v = 1.0f - uv[1];
      WriteSkinVertex(mesh, i, i, &dm);
    }
    dm.indices.assign(srcIndices.begin(), srcIndices.end());
    gotNormals = normalsPerVertex;
  } else {
    // Facevarying fallback: expand one vertex per face-vertex.
    dm.vertices.resize(srcIndices.size());
    if (MeshHasSkinData(mesh, nPoints)) {
      dm.jointIdx.assign(srcIndices.size() * 4, 0u);
      dm.jointWt.assign(srcIndices.size() * 4, 0.0f);
    }
    dm.indices.resize(srcIndices.size());
    const bool normalsFV = !mesh.normals.empty() && mesh.normals.is_facevarying();
    const bool uvFV = uvAttr && !uvAttr->empty() && uvAttr->is_facevarying();
    gotNormals = normalsFV || normalsPerVertex;
    for (size_t k = 0; k < srcIndices.size(); ++k) {
      const uint32_t pidx = srcIndices[k];
      DrawVertex& v = dm.vertices[k];
      if (pidx < nPoints) {
        v.px = mesh.points[pidx][0];
        v.py = mesh.points[pidx][1];
        v.pz = mesh.points[pidx][2];
      } else {
        v.px = v.py = v.pz = 0.0f;
      }
      float nrm[3] = {0, 0, 0};
      if (normalsFV) ReadFloats(mesh.normals, k, 3, nrm);
      else if (normalsPerVertex && pidx < nPoints) ReadFloats(mesh.normals, pidx, 3, nrm);
      v.nx = nrm[0]; v.ny = nrm[1]; v.nz = nrm[2];
      float uv[2] = {0, 0};
      if (uvFV) ReadFloats(*uvAttr, k, 2, uv);
      else if (uvPerVertex && pidx < nPoints) ReadFloats(*uvAttr, pidx, 2, uv);
      // Flip V: USD `st` has v=0 at the image bottom, but decoded images are
      // top-row-first and uploaded so v=0 samples the top, so invert here.
      v.u = uv[0]; v.v = 1.0f - uv[1];
      if (pidx < nPoints) WriteSkinVertex(mesh, pidx, k, &dm);
      dm.indices[k] = static_cast<uint32_t>(k);
    }
  }

  // Generate smooth normals if none were provided/usable.
  if (!gotNormals) {
    const float sign = mesh.is_rightHanded ? 1.0f : -1.0f;
    for (auto& v : dm.vertices) { v.nx = v.ny = v.nz = 0.0f; }
    for (size_t t = 0; t + 2 < dm.indices.size(); t += 3) {
      uint32_t i0 = dm.indices[t], i1 = dm.indices[t + 1], i2 = dm.indices[t + 2];
      if (i0 >= dm.vertices.size() || i1 >= dm.vertices.size() ||
          i2 >= dm.vertices.size())
        continue;
      DrawVertex& a = dm.vertices[i0];
      DrawVertex& b = dm.vertices[i1];
      DrawVertex& c = dm.vertices[i2];
      float e1[3] = {b.px - a.px, b.py - a.py, b.pz - a.pz};
      float e2[3] = {c.px - a.px, c.py - a.py, c.pz - a.pz};
      float nx = (e1[1] * e2[2] - e1[2] * e2[1]) * sign;
      float ny = (e1[2] * e2[0] - e1[0] * e2[2]) * sign;
      float nz = (e1[0] * e2[1] - e1[1] * e2[0]) * sign;
      a.nx += nx; a.ny += ny; a.nz += nz;
      b.nx += nx; b.ny += ny; b.nz += nz;
      c.nx += nx; c.ny += ny; c.nz += nz;
    }
    for (auto& v : dm.vertices) {
      float len = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
      if (len > 1e-8f) { v.nx /= len; v.ny /= len; v.nz /= len; }
    }
  }

  // --- Submeshes (group triangles by material) ---
  const size_t triCount = dm.indices.size() / 3;
  if (mesh.material_subsetMap.empty()) {
    DrawSubmesh sub;
    sub.indexOffset = 0;
    sub.indexCount = static_cast<uint32_t>(dm.indices.size());
    sub.materialId = mesh.material_id;
    dm.submeshes.push_back(sub);
  } else {
    std::vector<int> triMat(triCount, mesh.material_id);
    for (const auto& kv : mesh.material_subsetMap) {
      const tydra::MaterialSubset& ss = kv.second;
      for (int triIdx : ss.indices()) {
        if (triIdx >= 0 && static_cast<size_t>(triIdx) < triCount) {
          triMat[static_cast<size_t>(triIdx)] = ss.material_id;
        }
      }
    }
    // Bucket triangles by material id, preserving order within a material.
    std::map<int, std::vector<uint32_t>> buckets;
    for (size_t t = 0; t < triCount; ++t) {
      auto& bucket = buckets[triMat[t]];
      bucket.push_back(dm.indices[t * 3 + 0]);
      bucket.push_back(dm.indices[t * 3 + 1]);
      bucket.push_back(dm.indices[t * 3 + 2]);
    }
    std::vector<uint32_t> grouped;
    grouped.reserve(dm.indices.size());
    for (auto& kv : buckets) {
      DrawSubmesh sub;
      sub.indexOffset = static_cast<uint32_t>(grouped.size());
      sub.indexCount = static_cast<uint32_t>(kv.second.size());
      sub.materialId = kv.first;
      grouped.insert(grouped.end(), kv.second.begin(), kv.second.end());
      dm.submeshes.push_back(sub);
    }
    dm.indices.swap(grouped);
  }

  // World left as identity; PlaceDrawMesh applies the node transform.
  light3d::Mat4 ident = light3d::Mat4::identity();
  std::memcpy(dm.world, ident.m, sizeof(ident.m));

  *dmOut = std::move(dm);
  return true;
}

// Apply a world transform to a built DrawMeshCPU and compute its world-space
// AABB from the (local) vertex positions.
void PlaceDrawMesh(DrawMeshCPU* dm, const matrix4d& worldMat) {
  light3d::Mat4 world = light3d::Mat4::identity();
  MatToColMajor(worldMat, world.m);
  std::memcpy(dm->world, world.m, sizeof(world.m));

  float lmin[3] = {1e30f, 1e30f, 1e30f};
  float lmax[3] = {-1e30f, -1e30f, -1e30f};
  for (const auto& v : dm->vertices) {
    const float p[3] = {v.px, v.py, v.pz};
    for (int c = 0; c < 3; ++c) {
      lmin[c] = std::min(lmin[c], p[c]);
      lmax[c] = std::max(lmax[c], p[c]);
    }
  }
  float wmin[3] = {1e30f, 1e30f, 1e30f};
  float wmax[3] = {-1e30f, -1e30f, -1e30f};
  for (int corner = 0; corner < 8; ++corner) {
    light3d::Vec3 lp{(corner & 1) ? lmax[0] : lmin[0],
                     (corner & 2) ? lmax[1] : lmin[1],
                     (corner & 4) ? lmax[2] : lmin[2]};
    light3d::Vec3 wp = light3d::transformPoint(world, lp);
    float wparr[3] = {wp.x, wp.y, wp.z};
    for (int c = 0; c < 3; ++c) {
      wmin[c] = std::min(wmin[c], wparr[c]);
      wmax[c] = std::max(wmax[c], wparr[c]);
    }
  }
  for (int c = 0; c < 3; ++c) { dm->aabbMin[c] = wmin[c]; dm->aabbMax[c] = wmax[c]; }
}

// Union the world AABB of every built mesh into the scene bounds.
void ComputeSceneBounds(DrawScene* out) {
  bool first = true;
  for (const auto& dm : out->meshes) {
    if (first) {
      for (int c = 0; c < 3; ++c) { out->aabbMin[c] = dm.aabbMin[c]; out->aabbMax[c] = dm.aabbMax[c]; }
      first = false;
    } else {
      for (int c = 0; c < 3; ++c) {
        out->aabbMin[c] = std::min(out->aabbMin[c], dm.aabbMin[c]);
        out->aabbMax[c] = std::max(out->aabbMax[c], dm.aabbMax[c]);
      }
    }
  }
  out->hasBounds = !first;
}

void FinalizeSkinningLayout(const tydra::RenderScene& rs, DrawScene* out) {
  if (!out) return;
  int nextMatrix = 0;
  for (DrawMeshCPU& dm : out->meshes) {
    const bool hasAttribs =
        !dm.jointIdx.empty() && !dm.jointWt.empty() &&
        dm.jointIdx.size() == dm.vertices.size() * 4 &&
        dm.jointWt.size() == dm.vertices.size() * 4;
    if (!hasAttribs || dm.skelId < 0 ||
        static_cast<size_t>(dm.skelId) >= rs.skeletons.size()) {
      dm.jointIdx.clear();
      dm.jointWt.clear();
      dm.skelId = -1;
      dm.skinMatrixBase = -1;
      continue;
    }
    const size_t nj = rs.skeletons[static_cast<size_t>(dm.skelId)].num_joints();
    if (nj == 0 || nj > static_cast<size_t>(std::numeric_limits<int>::max() - nextMatrix)) {
      dm.jointIdx.clear();
      dm.jointWt.clear();
      dm.skelId = -1;
      dm.skinMatrixBase = -1;
      continue;
    }
    dm.skinMatrixBase = nextMatrix;
    for (uint32_t& j : dm.jointIdx) {
      if (j < nj) {
        j += static_cast<uint32_t>(dm.skinMatrixBase);
      } else {
        j = static_cast<uint32_t>(dm.skinMatrixBase);
      }
    }
    nextMatrix += static_cast<int>(nj);
  }
  out->boneMatrixCount = nextMatrix;
}

// Returns true if adding `dm` would exceed the triangle / vertex-byte budget.
bool OverBudget(const DrawScene& out, size_t cumulativeVertexBytes,
                const DrawMeshCPU& dm, const LoadControl& ctrl) {
  const size_t thisTris = dm.indices.size() / 3;
  const size_t estBytes =
      dm.vertices.size() * sizeof(DrawVertex) +
      dm.jointIdx.size() * sizeof(uint32_t) +
      dm.jointWt.size() * sizeof(float) +
      dm.indices.size() * sizeof(uint32_t);
  return out.triangleCount + thisTris > ctrl.maxTriangles ||
         cumulativeVertexBytes + estBytes > ctrl.maxVertexBytes;
}

}  // namespace

void BuildDrawScene(const tydra::RenderScene& rs, DrawScene* out, LoadControl* ctrl) {
  *out = DrawScene{};

  // Mesh world transforms from the node hierarchy.
  std::unordered_map<int, matrix4d> meshXform;
  for (const auto& n : rs.nodes) {
    CollectMeshTransforms(n, &meshXform);
  }

  std::vector<int> drawTexMap;
  BuildDrawTextures(rs, out, &drawTexMap);
  BuildDrawMaterials(rs, out, drawTexMap);

  size_t cumulativeVertexBytes = 0;
  for (size_t m = 0; m < rs.meshes.size(); ++m) {
    const tydra::RenderMesh& mesh = rs.meshes[m];
    DrawMeshCPU dm;
    if (!MakeDrawMesh(mesh, &dm)) {
      out->skipped.push_back("mesh '" + mesh.prim_name + "': empty geometry");
      continue;
    }

    // Cancellation + render budget (stops before adding an over-budget mesh so
    // huge scenes neither freeze the per-frame loop nor thrash VRAM).
    if (ctrl) {
      if (ctrl->cancel.load()) {
        out->truncated = true;
        out->skipped.push_back("build cancelled at mesh " + std::to_string(m) + "/" +
                               std::to_string(rs.meshes.size()));
        break;
      }
      if (OverBudget(*out, cumulativeVertexBytes, dm, *ctrl)) {
        out->truncated = true;
        out->skipped.push_back(
            "render budget reached: stopped at mesh " + std::to_string(m) + "/" +
            std::to_string(rs.meshes.size()) + " (" +
            std::to_string(out->triangleCount / 1000) + "K tris)");
        break;
      }
    }

    matrix4d world = matrix4d::identity();
    auto xit = meshXform.find(static_cast<int>(m));
    if (xit != meshXform.end()) world = xit->second;
    PlaceDrawMesh(&dm, world);

    cumulativeVertexBytes +=
        dm.vertices.size() * sizeof(DrawVertex) +
        dm.jointIdx.size() * sizeof(uint32_t) +
        dm.jointWt.size() * sizeof(float) +
        dm.indices.size() * sizeof(uint32_t);
    out->triangleCount += dm.indices.size() / 3;
    out->meshes.push_back(std::move(dm));
  }

  FinalizeSkinningLayout(rs, out);
  ComputeSceneBounds(out);
}

bool BuildDrawSceneStreaming(tydra::RenderSceneConverter& converter,
                             const tydra::RenderSceneConverterEnv& env,
                             tydra::RenderScene* render, DrawScene* out,
                             LoadControl* ctrl) {
  *out = DrawScene{};

  // Streaming state (lives for the duration of the synchronous conversion).
  std::vector<int> rsMeshToDraw;   // rs mesh index -> out->meshes index (-1 skipped)
  std::vector<bool> placed;        // per out->meshes: world applied yet?
  size_t cumulativeVertexBytes = 0;

  tydra::RenderSceneSink sink;

  // Build each mesh's geometry as it is converted (LOCAL space; placed later).
  sink.on_mesh = [&](const tydra::RenderMesh& mesh, size_t index,
                     const std::string&, void*) -> bool {
    if (index >= rsMeshToDraw.size()) rsMeshToDraw.resize(index + 1, -1);
    DrawMeshCPU dm;
    if (!MakeDrawMesh(mesh, &dm)) {
      out->skipped.push_back("mesh '" + mesh.prim_name + "': empty geometry");
      return true;
    }
    // Render budget (draw-side cap): stop adding draw meshes but let the
    // conversion finish so out->render stays complete for the GUI.
    if (ctrl && !out->truncated && OverBudget(*out, cumulativeVertexBytes, dm, *ctrl)) {
      out->truncated = true;
      out->skipped.push_back("render budget reached at mesh " +
                             std::to_string(index) + " (" +
                             std::to_string(out->triangleCount / 1000) + "K tris)");
    }
    if (out->truncated) return true;  // skip building further draw meshes

    cumulativeVertexBytes +=
        dm.vertices.size() * sizeof(DrawVertex) +
        dm.jointIdx.size() * sizeof(uint32_t) +
        dm.jointWt.size() * sizeof(float) +
        dm.indices.size() * sizeof(uint32_t);
    out->triangleCount += dm.indices.size() / 3;
    rsMeshToDraw[index] = static_cast<int>(out->meshes.size());
    out->meshes.push_back(std::move(dm));
    placed.push_back(false);
    return true;
  };

  // Place meshes once the node hierarchy (world matrices) is known.
  sink.on_root_node = [&](const tydra::Node& root, size_t, void*) -> bool {
    std::unordered_map<int, matrix4d> meshXform;
    CollectMeshTransforms(root, &meshXform);
    for (const auto& kv : meshXform) {
      const int rsIdx = kv.first;
      if (rsIdx < 0 || static_cast<size_t>(rsIdx) >= rsMeshToDraw.size()) continue;
      const int drawIdx = rsMeshToDraw[static_cast<size_t>(rsIdx)];
      if (drawIdx < 0 || static_cast<size_t>(drawIdx) >= out->meshes.size()) continue;
      PlaceDrawMesh(&out->meshes[static_cast<size_t>(drawIdx)], kv.second);
      placed[static_cast<size_t>(drawIdx)] = true;
    }
    return true;
  };

  // Build textures + materials from the finished scene, then finalize bounds.
  sink.on_complete = [&](const tydra::RenderScene& scene, void*) -> bool {
    std::vector<int> drawTexMap;
    BuildDrawTextures(scene, out, &drawTexMap);
    BuildDrawMaterials(scene, out, drawTexMap);
    // Any mesh not referenced by a node keeps identity placement (matches
    // BuildDrawScene, which uses identity when no transform is found).
    const matrix4d ident = matrix4d::identity();
    for (size_t i = 0; i < out->meshes.size(); ++i) {
      if (!placed[i]) PlaceDrawMesh(&out->meshes[i], ident);
    }
    FinalizeSkinningLayout(scene, out);
    ComputeSceneBounds(out);
    return true;
  };

  return converter.ConvertToRenderSceneStreaming(env, sink, render);
}

}  // namespace tusdview
