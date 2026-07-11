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

#include "next/diff/layer-diff.hh"
#include "next/pcp/layer-registry.hh"
#include "next/pipeline/flatten.hh"
#include "next/validation/usd-validation.hh"
#include "tsd/tinysubdiv.hh"

#include "external/jsonhpp/nlohmann/json.hpp"
#include "next/resolver/asset-resolver.hh"
#include "next/schema/geom-mesh.hh"
#include "next/schema/geom-xform.hh"
#include "next/schema/usd-shade.hh"
#include "next/stage/stage.hh"
#include "next/tinyusdz-next.hh"
#include "next/writer/usda-writer.hh"
#include "next/writer/usdc-writer.hh"
#include "next/writer/usdz-writer.hh"
#include "tydra/next/render-converter.hh"
#include "tydra/next/render-data.hh"
#include "tydra/next/urdf-to-usd.hh"

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

static const char* NodeTypeName(tr::NodeType type) {
  switch (type) {
    case tr::NodeType::Xform:
      return "xform";
    case tr::NodeType::Mesh:
      return "mesh";
    case tr::NodeType::Points:
      return "points";
    case tr::NodeType::PointInstancer:
      return "pointInstancer";
    case tr::NodeType::Camera:
      return "camera";
    case tr::NodeType::PointLight:
      return "pointLight";
    case tr::NodeType::DirectionalLight:
      return "directionalLight";
    case tr::NodeType::SpotLight:
      return "spotLight";
    case tr::NodeType::RectLight:
      return "rectLight";
    case tr::NodeType::DiskLight:
      return "diskLight";
    case tr::NodeType::DomeLight:
      return "domeLight";
    case tr::NodeType::SphereLight:
      return "sphereLight";
    case tr::NodeType::Skeleton:
      return "skeleton";
    case tr::NodeType::Curves:
      return "curves";
    default:
      return "unknown";
  }
}

static const char* InterpolationName(tr::Interpolation interpolation) {
  switch (interpolation) {
    case tr::Interpolation::Constant:
      return "constant";
    case tr::Interpolation::Uniform:
      return "uniform";
    case tr::Interpolation::Vertex:
      return "vertex";
    case tr::Interpolation::Varying:
      return "varying";
    case tr::Interpolation::FaceVarying:
      return "faceVarying";
    default:
      return "constant";
  }
}

static const char* CurveTypeName(tr::CurveType type) {
  return type == tr::CurveType::Linear ? "linear" : "cubic";
}

static const char* CurveBasisName(tr::CurveBasis basis) {
  switch (basis) {
    case tr::CurveBasis::Bezier:
      return "bezier";
    case tr::CurveBasis::BSpline:
      return "bspline";
    case tr::CurveBasis::CatmullRom:
      return "catmullRom";
    default:
      return "bezier";
  }
}

static const char* CurveWrapName(tr::CurveWrap wrap) {
  switch (wrap) {
    case tr::CurveWrap::Nonperiodic:
      return "nonperiodic";
    case tr::CurveWrap::Periodic:
      return "periodic";
    case tr::CurveWrap::Pinned:
      return "pinned";
    default:
      return "nonperiodic";
  }
}

static const char* LightTypeName(tr::LightType type) {
  switch (type) {
    case tr::LightType::Point:
      return "point";
    case tr::LightType::Directional:
      return "directional";
    case tr::LightType::Spot:
      return "spot";
    case tr::LightType::Rect:
      return "rect";
    case tr::LightType::Disk:
      return "disk";
    case tr::LightType::Dome:
      return "dome";
    case tr::LightType::Sphere:
      return "sphere";
    case tr::LightType::Cylinder:
      return "cylinder";
    case tr::LightType::Geometry:
      return "geometry";
    default:
      return "unknown";
  }
}

static const char* AnimationPathName(tr::AnimationChannel::TargetPath path) {
  switch (path) {
    case tr::AnimationChannel::TargetPath::Translation:
      return "Translation";
    case tr::AnimationChannel::TargetPath::Rotation:
      return "Rotation";
    case tr::AnimationChannel::TargetPath::Scale:
      return "Scale";
    case tr::AnimationChannel::TargetPath::Weights:
      return "Weights";
    case tr::AnimationChannel::TargetPath::CustomProperty:
      return "CustomProperty";
    default:
      return "Unknown";
  }
}

static const char* AnimationInterpolationName(
    tr::AnimationChannel::Interpolation interp) {
  switch (interp) {
    case tr::AnimationChannel::Interpolation::Step:
      return "STEP";
    case tr::AnimationChannel::Interpolation::Linear:
      return "LINEAR";
    case tr::AnimationChannel::Interpolation::CubicSpline:
      return "CUBICSPLINE";
    default:
      return "LINEAR";
  }
}

static const char* CameraTypeName(tr::CameraType type) {
  switch (type) {
    case tr::CameraType::Perspective:
      return "perspective";
    case tr::CameraType::Orthographic:
      return "orthographic";
    default:
      return "unknown";
  }
}

template <typename T>
emscripten::val VectorToArray(const std::vector<T>& values) {
  emscripten::val array = emscripten::val::array();
  for (const auto& v : values) array.call<void>("push", v);
  return array;
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

std::string JsonEscape(const std::string& s) {
  std::ostringstream out;
  for (char c : s) {
    switch (c) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          out << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<int>(static_cast<unsigned char>(c))
              << std::dec << std::setfill(' ');
        } else {
          out << c;
        }
        break;
    }
  }
  return out.str();
}

const char* RenderMaterialShaderTypeName(tr::RenderMaterial::ShaderType type) {
  switch (type) {
    case tr::RenderMaterial::ShaderType::PreviewSurface:
      return "PreviewSurface";
    case tr::RenderMaterial::ShaderType::OpenPBR:
      return "OpenPBR";
    case tr::RenderMaterial::ShaderType::None:
    default:
      return "None";
  }
}

std::string ShaderParamJson(const tr::RenderScene& scene,
                            const tr::ShaderParam& p) {
  std::ostringstream ss;
  ss << "{\"value\":[" << p.value.x << "," << p.value.y << ","
     << p.value.z << "," << p.value.w << "]";
  if (p.texture_id >= 0) {
    ss << ",\"texture\":\"" << JsonEscape(TexturePath(scene, p)) << "\"";
    ss << ",\"textureId\":" << p.texture_id;
    if (static_cast<size_t>(p.texture_id) < scene.textures.size()) {
      ss << ",\"colorspace\":\""
         << JsonEscape(scene.textures[static_cast<size_t>(p.texture_id)]
                           .source_color_space)
         << "\"";
    }
  }
  ss << "}";
  return ss.str();
}

std::string RenderMaterialJson(const tr::RenderScene& scene,
                               const tr::RenderMaterial& mat) {
  std::ostringstream ss;
  ss << "{\"name\":\"" << JsonEscape(mat.name) << "\",";
  ss << "\"primPath\":\"" << JsonEscape(mat.prim_path) << "\",";
  ss << "\"shaderType\":\"" << RenderMaterialShaderTypeName(mat.shader_type)
     << "\",";
  ss << "\"materialXConfig\":{";
  ss << "\"authored\":" << (mat.mtlx_config.authored ? "true" : "false");
  ss << ",\"version\":\"" << JsonEscape(mat.mtlx_config.version) << "\"";
  ss << ",\"namespace\":\"" << JsonEscape(mat.mtlx_config.name_space) << "\"";
  ss << ",\"colorspace\":\"" << JsonEscape(mat.mtlx_config.colorspace) << "\"";
  ss << ",\"sourceUri\":\"" << JsonEscape(mat.mtlx_config.source_uri) << "\"";
  ss << "}";
  if (mat.preview_surface) {
    const tr::PreviewSurfaceShader& ps = *mat.preview_surface;
    ss << ",\"previewSurface\":{";
    ss << "\"diffuseColor\":" << ShaderParamJson(scene, ps.diffuse_color);
    ss << ",\"emissiveColor\":" << ShaderParamJson(scene, ps.emissive_color);
    ss << ",\"metallic\":" << ShaderParamJson(scene, ps.metallic);
    ss << ",\"roughness\":" << ShaderParamJson(scene, ps.roughness);
    ss << ",\"opacity\":" << ShaderParamJson(scene, ps.opacity);
    ss << ",\"normal\":" << ShaderParamJson(scene, ps.normal);
    ss << "}";
  }
  if (mat.openpbr) {
    const tr::OpenPBRSurfaceShader& op = *mat.openpbr;
    ss << ",\"openPBR\":{";
    ss << "\"baseColor\":" << ShaderParamJson(scene, op.base_color);
    ss << ",\"baseWeight\":" << ShaderParamJson(scene, op.base_weight);
    ss << ",\"baseRoughness\":" << ShaderParamJson(scene, op.base_roughness);
    ss << ",\"baseMetalness\":" << ShaderParamJson(scene, op.base_metalness);
    ss << ",\"specularWeight\":" << ShaderParamJson(scene, op.specular_weight);
    ss << ",\"specularColor\":" << ShaderParamJson(scene, op.specular_color);
    ss << ",\"emissionColor\":" << ShaderParamJson(scene, op.emission_color);
    ss << ",\"emissionLuminance\":"
       << ShaderParamJson(scene, op.emission_luminance);
    ss << ",\"opacity\":" << ShaderParamJson(scene, op.opacity);
    ss << ",\"normal\":" << ShaderParamJson(scene, op.normal);
    ss << ",\"nodegraphJson\":\"" << JsonEscape(op.nodegraph_json) << "\"";
    ss << "}";
  }
  ss << "}";
  return ss.str();
}

static size_t AnimationComponentCount(const tr::AnimationChannel& channel) {
  switch (channel.target_path) {
    case tr::AnimationChannel::TargetPath::Rotation:
      return 4;
    case tr::AnimationChannel::TargetPath::Weights:
      return 1;
    case tr::AnimationChannel::TargetPath::CustomProperty:
      return 4;
    case tr::AnimationChannel::TargetPath::Translation:
    case tr::AnimationChannel::TargetPath::Scale:
      return 3;
    default:
      return 4;
  }
}

static void AppendAnimationKeyframeValues(const tr::AnimationChannel& channel,
                                        const tr::Keyframe& keyframe,
                                        std::vector<float>* values) {
  if (!values) return;

  const size_t component_count = AnimationComponentCount(channel);
  const tr::Float4& v = keyframe.value;

  switch (component_count) {
    case 1:
      values->push_back(v.x);
      break;
    case 3:
      values->push_back(v.x);
      values->push_back(v.y);
      values->push_back(v.z);
      break;
    default:
      values->push_back(v.x);
      values->push_back(v.y);
      values->push_back(v.z);
      values->push_back(v.w);
      break;
  }
}

static int AnimationTargetNodeCount(const tr::AnimationClip& clip) {
  std::set<int32_t> node_ids;
  for (const tr::AnimationChannel& channel : clip.channels) {
    if (channel.target_node >= 0) {
      node_ids.insert(channel.target_node);
    }
  }
  return static_cast<int>(node_ids.size());
}

static bool AnimationHasSkeletalChannels(const tr::AnimationClip& clip) {
  for (const tr::AnimationChannel& channel : clip.channels) {
    if (channel.is_skeletal) return true;
  }
  return false;
}

// Parse a dependency layer's bytes for the flatten compositor. Crate bytes
// keep the lazy CrateReader path (arrays pass through verbatim); anything else
// (USDA text, USDZ package) dispatches through the content-sniffing pcp
// memory loader.
static std::unique_ptr<tn::Layer> ParseNextLayerBytes(
    const uint8_t* data, size_t size, const std::string& key,
    const tn::CrateReadOptions& read_opts, std::string* error) {
  if (size >= 8 && std::memcmp(data, "PXR-USDC", 8) == 0) {
    tn::CrateReader reader(read_opts);
    tn::CrateReadResult rr = reader.Read(data, size);
    if (!rr.success) {
      if (error) {
        *error = rr.errors.empty() ? ("crate read failed: " + key)
                                   : rr.errors[0].message;
      }
      return nullptr;
    }
    std::unique_ptr<tn::Layer> layer = rr.stage.ReleaseRootLayer();
    if (layer) layer->build_path_index();  // compositor looks prims up by path
    return layer;
  }

  tn::pcp::LayerLoadOptions lopts;
  lopts.max_memory = read_opts.max_memory;
  std::string warn;
  std::string parse_err;
  std::shared_ptr<tn::Layer> loaded =
      tn::pcp::LoadLayerFromMemory(key, data, size, &warn, &parse_err, lopts);
  if (!loaded) {
    if (error) {
      *error = parse_err.empty() ? ("failed to parse layer: " + key)
                                 : parse_err;
    }
    return nullptr;
  }
  std::unique_ptr<tn::Layer> layer(new tn::Layer(std::move(*loaded)));
  layer->build_path_index();
  return layer;
}

}  // namespace

namespace {

// Copy a JS typed array (or null/undefined -> empty) into a C++ vector.
template <typename T>
void CopyTypedArrayToVector(const emscripten::val& v, std::vector<T>& out) {
  out.clear();
  if (v.isNull() || v.isUndefined()) return;
  const size_t len = v["length"].as<size_t>();
  if (!len) return;
  out.resize(len);
  emscripten::val heap =
      emscripten::val(emscripten::typed_memory_view(len, out.data()));
  heap.call<void>("set", v);
}

}  // namespace

// ===========================================================================
// SubdivStreamer.refineStream(...) refines a control mesh and delivers the
// refined surface to a JS callback in bounded batches (zero-copy heap views),
// so the full level-N output never resides in the wasm heap at once. Ported
// verbatim from the legacy module (pure tsd::, no legacy-core dependency).
// ===========================================================================
class SubdivStreamer {
 public:
  // points: Float32Array (xyz interleaved). fvc/fvi: Uint32Array.
  // scheme: 0=catmullClark, 1=loop, 2=bilinear.
  // boundary: 0=edgeAndCorner, 1=edgeOnly, 2=none.
  // uvValues: Float32Array (stride 2) or null/empty for no texturing.
  // uvIndices: Uint32Array (per face-corner) or null for identity.
  // uvInterp: 0 = linear ("all"); 1 = smooth seam-split ("cornersPlus1").
  // batchFaces: parent faces per output batch (0 => default).
  // blockFaces: >0 bounds the WORKING set; 0 => whole-mesh streaming.
  // haloRings: block halo radius (0 => library default).
  // onBatch(positions, normals|null, indices, faceSource, uv|null,
  //         numVertices, numFaces, batchIndex): views valid only for the call.
  // Returns "" on success, else an error message.
  std::string refineStream(const emscripten::val &points,
                           const emscripten::val &fvc,
                           const emscripten::val &fvi,
                           const emscripten::val &uvValues,
                           const emscripten::val &uvIndices, int uvInterp,
                           int scheme, int boundary, int level, int batchFaces,
                           int blockFaces, int haloRings, bool wantNormals,
                           emscripten::val onBatch) {
    namespace tsd = tinyusdz::tsd;

    std::vector<float> pts;
    std::vector<uint32_t> counts;
    std::vector<uint32_t> indices;
    CopyTypedArrayToVector(points, pts);
    CopyTypedArrayToVector(fvc, counts);
    CopyTypedArrayToVector(fvi, indices);
    if ((pts.size() % 3) != 0) {
      return "points length must be a multiple of 3";
    }
    if (counts.empty() || indices.empty()) {
      return "empty mesh";
    }

    std::vector<float> uvs;
    std::vector<uint32_t> uvidx;
    CopyTypedArrayToVector(uvValues, uvs);
    CopyTypedArrayToVector(uvIndices, uvidx);
    const bool has_uv = (uvs.size() >= 2) && ((uvs.size() % 2) == 0);

    tsd::MeshView mesh;
    mesh.points = pts.data();
    mesh.num_points = uint32_t(pts.size() / 3);
    mesh.face_vertex_counts = counts.data();
    mesh.num_faces = uint32_t(counts.size());
    mesh.face_vertex_indices = indices.data();
    mesh.num_face_vertex_indices = uint32_t(indices.size());

    tsd::FVarChannelView uvchan;
    if (has_uv) {
      uvchan.values = uvs.data();
      uvchan.num_values = uint32_t(uvs.size() / 2);
      uvchan.indices = uvidx.empty() ? nullptr : uvidx.data();
      uvchan.stride = 2;
      uvchan.interpolation = (uvInterp == 1)
                                 ? tsd::FVarLinearInterpolation::CornersPlus1
                                 : tsd::FVarLinearInterpolation::All;
    }

    tsd::Options opts;
    opts.scheme = (scheme == 1)   ? tsd::Scheme::Loop
                  : (scheme == 2) ? tsd::Scheme::Bilinear
                                  : tsd::Scheme::CatmullClark;
    opts.boundary = (boundary == 1)   ? tsd::BoundaryInterpolation::EdgeOnly
                    : (boundary == 2) ? tsd::BoundaryInterpolation::None
                                      : tsd::BoundaryInterpolation::EdgeAndCorner;
    opts.level = level;
    opts.remove_holes = true;

    tsd::StreamOptions so;
    so.batch_faces = (batchFaces > 0) ? uint32_t(batchFaces) : 4096u;
    so.emit_triangles = true;
    so.want_normals = wantNormals;
    so.dedup_within_batch = true;
    so.block_faces = (blockFaces > 0) ? uint32_t(blockFaces) : 0u;
    so.halo_rings = (haloRings > 0) ? uint32_t(haloRings) : 0u;

    struct SinkCtx {
      emscripten::val *cb;
      bool want_normals;
    } ctx{&onBatch, wantNormals};

    auto sink = [](void *user, const tsd::StreamBatch *b) -> bool {
      SinkCtx *c = static_cast<SinkCtx *>(user);
      emscripten::val pos(emscripten::typed_memory_view(
          size_t(b->num_vertices) * 3, const_cast<float *>(b->positions)));
      emscripten::val nrm =
          (c->want_normals && b->normals)
              ? emscripten::val(emscripten::typed_memory_view(
                    size_t(b->num_vertices) * 3, const_cast<float *>(b->normals)))
              : emscripten::val::null();
      emscripten::val idx(emscripten::typed_memory_view(
          size_t(b->num_indices), const_cast<uint32_t *>(b->indices)));
      emscripten::val fsrc(emscripten::typed_memory_view(
          size_t(b->num_faces), const_cast<uint32_t *>(b->face_source)));
      emscripten::val uv =
          (b->num_fvar == 1)
              ? emscripten::val(emscripten::typed_memory_view(
                    size_t(b->num_indices) * 2,
                    const_cast<float *>(b->fvar[0].values)))
              : emscripten::val::null();
      (*c->cb)(pos, nrm, idx, fsrc, uv, b->num_vertices, b->num_faces,
               b->batch_index);
      return true;
    };

    std::string err;
    const tsd::Result r = tsd::RefineStream(
        mesh, has_uv ? &uvchan : nullptr, has_uv ? 1u : 0u, nullptr, 0, opts, so,
        sink, &ctx, &err);
    if (r != tsd::Result::Success) {
      return std::string("RefineStream failed (") + tsd::to_string(r) +
             "): " + err;
    }
    return "";
  }

  // Total wasm linear-memory bytes (grow-only => heap high-water mark).
  double heapBytes() const {
    return emscripten::val::module_property("HEAPU8")["length"].as<double>();
  }
};

// Resumable multi-layer flatten session: JS drives a need-layer loop,
// providing dependency layer bytes (USDA / USDC / USDZ, fetched over HTTP or
// pulled from a package) until the flatten converges, then receives a
// flattened USDC buffer. One instance = one session; mirrors the legacy
// module's nextFlattenAsyncBegin/ProvideLayer/Step/End protocol with the
// session id replaced by the instance.
class NextFlattenSession {
 public:
  NextFlattenSession() = default;

  emscripten::val begin(emscripten::val rootBytes, const std::string& rootName,
                        bool lazyArrays) {
    emscripten::val result = emscripten::val::object();
    reset_();
    std::string copy_error;
    root_ = CopyUint8ArrayToString(rootBytes, &copy_error);
    if (root_.empty()) {
      result.set("success", false);
      result.set("error", copy_error.empty()
                              ? std::string("empty root layer buffer")
                              : copy_error);
      return result;
    }
    root_name_ = rootName;
    lazy_arrays_ = lazyArrays;
    began_ = true;
    result.set("success", true);
    result.set("status", "ready");
    return result;
  }

  emscripten::val setVariantOverride(const std::string& prim_path,
                                     const std::string& selection) {
    emscripten::val result = emscripten::val::object();
    variant_overrides_[prim_path] = selection;
    result.set("success", true);
    return result;
  }

  emscripten::val provideLayer(const std::string& key, emscripten::val data) {
    emscripten::val result = emscripten::val::object();
    if (!began_) {
      result.set("success", false);
      result.set("error", "session not started (call begin first)");
      return result;
    }
    std::string copy_error;
    std::string bytes = CopyUint8ArrayToString(data, &copy_error);
    if (bytes.empty()) {
      result.set("success", false);
      result.set("error", "Invalid or empty layer data for: " + key);
      return result;
    }
    std::string norm_key = tn::AssetResolver::NormalizePath(key);
    while (norm_key.rfind("./", 0) == 0) norm_key = norm_key.substr(2);
    layers_[norm_key] = std::move(bytes);
    parsed_layers_.erase(norm_key);
    result.set("success", true);
    return result;
  }

  emscripten::val step(emscripten::val chunkCb) {
    emscripten::val result = emscripten::val::object();
    if (!began_) {
      result.set("success", false);
      result.set("error", "session not started (call begin first)");
      return result;
    }

    tn::pipeline::FlattenOptions opts;
    opts.read.lazy_arrays = lazy_arrays_;
    opts.root_anchor_path = root_name_;
    opts.fail_on_composition_error = true;
    opts.composition.variant_overrides = variant_overrides_;

    using tn::AssetResolver;
    AssetResolver resolver;
    std::string missing_key;
    auto consumed = std::make_shared<std::set<std::string>>();
    auto resolved_cache =
        std::make_shared<std::map<std::string, std::string>>();
    resolver.SetCustomResolver(
        [this, consumed, resolved_cache](
            const std::string& asset, const std::string& anchor) -> std::string {
          const std::string cache_key = anchor + "\n" + asset;
          auto hit = resolved_cache->find(cache_key);
          if (hit != resolved_cache->end()) return hit->second;
          auto try_key = [this, &consumed](std::string key) -> std::string {
            key = AssetResolver::NormalizePath(key);
            while (key.rfind("./", 0) == 0) key = key.substr(2);
            return (layers_.count(key) || consumed->count(key))
                       ? key
                       : std::string();
          };
          if (!anchor.empty()) {
            std::string k = try_key(AssetResolver::JoinPath(
                AssetResolver::GetDirectory(anchor), asset));
            if (!k.empty()) {
              (*resolved_cache)[cache_key] = k;
              return k;
            }
          }
          {
            std::string k = try_key(asset);
            if (!k.empty()) {
              (*resolved_cache)[cache_key] = k;
              return k;
            }
          }
          for (const auto& cand : AssetResolver::SuffixCandidates(asset)) {
            std::string k = try_key(cand);
            if (!k.empty()) {
              (*resolved_cache)[cache_key] = k;
              return k;
            }
          }
          // Return the best normalized candidate so the loader can surface
          // exactly which layer JS should fetch.
          std::string request = asset;
          if (!anchor.empty()) {
            request = AssetResolver::JoinPath(
                AssetResolver::GetDirectory(anchor), asset);
          }
          request = AssetResolver::NormalizePath(request);
          while (request.rfind("./", 0) == 0) request = request.substr(2);
          (*resolved_cache)[cache_key] = request;
          return request;
        });
    opts.resolver = &resolver;

    const tn::CrateReadOptions read_opts = opts.read;
    opts.layer_loader = [this, read_opts, consumed, &missing_key](
                            const std::string& key,
                            std::string* error) -> std::unique_ptr<tn::Layer> {
      auto cached = parsed_layers_.find(key);
      if (cached != parsed_layers_.end() && cached->second) {
        consumed->insert(key);
        std::unique_ptr<tn::Layer> layer(
            new tn::Layer(cached->second->Clone()));
        layer->build_path_index();
        return layer;
      }

      auto it = layers_.find(key);
      if (it == layers_.end()) {
        missing_key = key;
        if (error) *error = "NEED_LAYER:" + key;
        return nullptr;
      }
      consumed->insert(key);
      const std::string& src = it->second;
      std::unique_ptr<tn::Layer> layer = ParseNextLayerBytes(
          reinterpret_cast<const uint8_t*>(src.data()), src.size(), key,
          read_opts, error);
      if (!layer) return nullptr;
      parsed_layers_[key] =
          std::shared_ptr<tn::Layer>(new tn::Layer(layer->Clone()));
      return layer;
    };

    const bool buffered = chunkCb.isNull() || chunkCb.isUndefined();
    tn::pipeline::FlattenStats stats;
    std::string err;
    bool ok = false;
    std::vector<uint8_t> out;
    bool aborted = false;
    const uint8_t* root_data = reinterpret_cast<const uint8_t*>(root_.data());
    const size_t root_size = root_.size();
    if (buffered) {
      ok = tn::pipeline::FlattenUSDMemoryToUSDC(root_name_, root_data,
                                                root_size, out, opts, &stats,
                                                &err);
    } else {
      opts.write.streaming = true;
      tn::CrateWriteSink sink = [&](const uint8_t* data, size_t size) -> bool {
        emscripten::val view(emscripten::typed_memory_view(size, data));
        emscripten::val r = chunkCb(view);
        if (r.isFalse()) {
          aborted = true;
          return false;
        }
        return true;
      };
      ok = tn::pipeline::FlattenUSDMemoryToUSDCToSink(
          root_name_, root_data, root_size, sink, opts, &stats, &err);
    }

    if (!missing_key.empty()) {
      result.set("success", true);
      result.set("status", "need-layer");
      result.set("key", missing_key);
      return result;
    }
    result.set("success", ok);
    if (!ok) {
      if (aborted) {
        result.set("success", true);
        result.set("status", "ready");
        return result;
      }
      result.set("status", "error");
      result.set("error", err);
      return result;
    }

    result.set("status", "done");
    if (buffered) result.set("data", Uint8ArrayFromVector(out));
    result.set("inputBytes", static_cast<double>(stats.input_bytes));
    result.set("outputBytes", static_cast<double>(stats.output_bytes));
    result.set("primCount", static_cast<double>(stats.prim_count));
    result.set("arraysPassedThrough",
               static_cast<double>(stats.arrays_passed_through));
    result.set("arraysReencoded", static_cast<double>(stats.arrays_reencoded));
    result.set("readMs", stats.read_ms);
    result.set("composeMs", stats.compose_ms);
    result.set("writeMs", stats.write_ms);
    {
      emscripten::val assets = emscripten::val::array();
      for (const auto& path : stats.referenced_assets) {
        assets.call<void>("push", path);
      }
      result.set("assetPaths", assets);
      result.set("assetPathCount",
                 static_cast<double>(stats.referenced_assets.size()));
    }
    return result;
  }

  void end() { reset_(); }

 private:
  void reset_() {
    root_.clear();
    root_.shrink_to_fit();
    root_name_.clear();
    lazy_arrays_ = true;
    began_ = false;
    layers_.clear();
    parsed_layers_.clear();
    variant_overrides_.clear();
  }

  std::string root_;
  std::string root_name_;
  bool lazy_arrays_ = true;
  bool began_ = false;
  std::map<std::string, std::string> layers_;
  std::map<std::string, std::shared_ptr<tn::Layer>> parsed_layers_;
  std::map<std::string, std::string> variant_overrides_;
};

nlohmann::json NextValueJSON(const tn::Value& value) {
  if (const bool* v = value.as_bool()) return *v;
  if (const int32_t* v = value.as_int()) return *v;
  if (const uint32_t* v = value.as_uint()) return *v;
  if (const int64_t* v = value.as_int64()) return *v;
  if (const uint64_t* v = value.as_uint64()) return *v;
  if (const float* v = value.as_float()) return *v;
  if (const double* v = value.as_double()) return *v;
  if (const std::string* v = value.as_string()) return *v;
  if (const std::string* v = value.as_token()) return *v;
  if (const std::string* v = value.as_asset_path()) return *v;
  if (const std::vector<float>* v = value.as_float_array()) return *v;
  if (const std::vector<double>* v = value.as_double_array()) return *v;
  if (const std::vector<int32_t>* v = value.as_int_array()) return *v;
  if (const std::vector<int64_t>* v = value.as_int64_array()) return *v;
  if (const std::vector<uint32_t>* v = value.as_uint_array()) return *v;
  if (const std::vector<uint64_t>* v = value.as_uint64_array()) return *v;
  if (const std::vector<std::string>* v = value.as_token_array()) return *v;

  auto float_array = [](const float* ptr, size_t count) {
    nlohmann::json out = nlohmann::json::array();
    for (size_t i = 0; i < count; ++i) out.push_back(ptr[i]);
    return out;
  };
  auto double_array = [](const double* ptr, size_t count) {
    nlohmann::json out = nlohmann::json::array();
    for (size_t i = 0; i < count; ++i) out.push_back(ptr[i]);
    return out;
  };
  switch (value.type_id()) {
    case tn::TypeId::Float2:
    case tn::TypeId::Texcoord2f:
      return float_array(static_cast<const float*>(value.raw_data()), 2);
    case tn::TypeId::Float3:
    case tn::TypeId::Point3f:
    case tn::TypeId::Vector3f:
    case tn::TypeId::Normal3f:
    case tn::TypeId::Color3f:
      return float_array(static_cast<const float*>(value.raw_data()), 3);
    case tn::TypeId::Float4:
    case tn::TypeId::Color4f:
    case tn::TypeId::Quatf:
      return float_array(static_cast<const float*>(value.raw_data()), 4);
    case tn::TypeId::Double2:
    case tn::TypeId::Texcoord2d:
      return double_array(static_cast<const double*>(value.raw_data()), 2);
    case tn::TypeId::Double3:
    case tn::TypeId::Point3d:
    case tn::TypeId::Vector3d:
    case tn::TypeId::Normal3d:
    case tn::TypeId::Color3d:
      return double_array(static_cast<const double*>(value.raw_data()), 3);
    case tn::TypeId::Double4:
    case tn::TypeId::Color4d:
    case tn::TypeId::Quatd:
      return double_array(static_cast<const double*>(value.raw_data()), 4);
    case tn::TypeId::Matrix4f:
      return float_array(value.as_matrix4f(), 16);
    case tn::TypeId::Matrix4d:
      return double_array(value.as_matrix4d(), 16);
    default:
      break;
  }
  return nullptr;
}

void AppendNextPhysicsPrimJSON(const tn::UsdPrim& prim, nlohmann::json* out) {
  if (!out || !prim.IsValid()) return;
  nlohmann::json item;
  item["name"] = prim.GetName();
  item["path"] = prim.GetPath().str();
  item["type"] = prim.GetTypeName();
  item["apiSchemas"] = prim.GetMeta().apiSchemas();
  item["properties"] = nlohmann::json::object();
  item["relationships"] = nlohmann::json::object();
  for (const std::string& name : prim.GetPropertyNames()) {
    const tn::Value* value = prim.GetPropertyValue(name);
    if (value) item["properties"][name] = NextValueJSON(*value);
  }
  for (const std::string& name : prim.GetRelationshipNames()) {
    const std::vector<tn::Path>* targets = prim.GetRelationship(name);
    if (!targets) continue;
    nlohmann::json paths = nlohmann::json::array();
    for (const tn::Path& path : *targets) paths.push_back(path.str());
    item["relationships"][name] = std::move(paths);
  }
  if (const tn::Value* purpose = prim.GetPropertyValue("purpose")) {
    if (const std::string* token = purpose->as_token()) item["purpose"] = *token;
  }
  if (const tn::Value* matrix = prim.GetPropertyValue("xformOp:transform")) {
    item["matrix"] = NextValueJSON(*matrix);
  }
  const std::string& type = prim.GetTypeName();
  if (type == "Mesh" || type == "Cube" || type == "Sphere" ||
      type == "Cylinder" || type == "Capsule" || type == "Plane") {
    nlohmann::json geometry;
    geometry["type"] = type == "Mesh" ? "mesh" :
                         type == "Cube" ? "box" :
                         std::string(1, static_cast<char>(std::tolower(type[0]))) +
                             type.substr(1);
    auto copy_property = [&](const char* property, const char* key) {
      if (const tn::Value* value = prim.GetPropertyValue(property)) {
        geometry[key] = NextValueJSON(*value);
      }
    };
    copy_property("points", "positions");
    copy_property("faceVertexIndices", "indices");
    copy_property("normals", "normals");
    copy_property("primvars:st", "uvs");
    copy_property("size", "size");
    copy_property("radius", "radius");
    copy_property("height", "height");
    copy_property("width", "width");
    copy_property("length", "length");
    copy_property("axis", "axis");
    item["geometry"] = std::move(geometry);
  }
  out->push_back(std::move(item));
  for (const tn::UsdPrim& child : prim.GetChildren()) {
    AppendNextPhysicsPrimJSON(child, out);
  }
}

class NextUSDZConverterNative {
 public:
  NextUSDZConverterNative() = default;

  std::string error() const { return error_; }
  std::string warn() const { return warn_; }

  void clearURDFMeshBuffers() { urdf_mesh_buffers_.clear(); }

  bool setVisualMesh(const std::string& name, const emscripten::val& positions,
                     const emscripten::val& normals,
                     const emscripten::val& uvs,
                     const emscripten::val& indices) {
    return setURDFMeshBuffer(name, positions, normals, uvs, indices);
  }

  bool setCollisionMesh(const std::string& name,
                        const emscripten::val& positions,
                        const emscripten::val& normals,
                        const emscripten::val& uvs,
                        const emscripten::val& indices) {
    return setURDFMeshBuffer(name, positions, normals, uvs, indices);
  }

  bool createURDFPhysicsScene(const std::string& robot_json) {
    tn::Stage stage;
    std::string warn;
    std::string err;
    if (!tr::ConvertURDFJsonToUSDStage(robot_json, &urdf_mesh_buffers_,
                                       &stage, &warn, &err)) {
      warn_ = std::move(warn);
      error_ = err.empty() ? "URDF/MJCF conversion failed" : std::move(err);
      has_stage_ = false;
      return false;
    }
    stage_ = std::move(stage);
    warn_ = std::move(warn);
    error_.clear();
    has_stage_ = true;
    return true;
  }

  bool loadFromBinary(const emscripten::val& bytes,
                      const std::string& filename) {
    std::string copy_error;
    std::string input = CopyUint8ArrayToString(bytes, &copy_error);
    if (!copy_error.empty()) {
      error_ = copy_error;
      has_stage_ = false;
      return false;
    }
    tn::LoadUSDOptions options;
    options.usda_options.parse_options.enable_usda_lazy_arrays = true;
    tn::Stage stage;
    std::string warn;
    std::string err;
    if (!tn::LoadUSDFromMemoryOwned(std::move(input), &stage, options,
                                    &warn, &err)) {
      error_ = err.empty() ? "Failed to load " + filename : std::move(err);
      warn_ = std::move(warn);
      has_stage_ = false;
      return false;
    }
    stage_ = std::move(stage);
    warn_ = std::move(warn);
    error_.clear();
    has_stage_ = true;
    return true;
  }

  void setAsset(const std::string& name, const emscripten::val& bytes) {
    std::string copy_error;
    std::string data = CopyUint8ArrayToString(bytes, &copy_error);
    if (!copy_error.empty()) {
      error_ = copy_error;
      return;
    }
    assets_[name] = std::vector<uint8_t>(data.begin(), data.end());
    error_.clear();
  }

  void setUSDCExportLimitMB(int file_size_mb, int memory_mb) {
    (void)file_size_mb;
    (void)memory_mb;
  }

  std::string extractPhysicsSceneJSON() {
    if (!has_stage_) {
      error_ = "No stage loaded";
      return std::string();
    }
    nlohmann::json root;
    root["name"] = stage_.GetMeta().defaultPrim;
    root["upAxis"] = stage_.GetMeta().upAxis;
    root["prims"] = nlohmann::json::array();
    for (const tn::UsdPrim& prim : stage_.GetRootPrims()) {
      AppendNextPhysicsPrimJSON(prim, &root["prims"]);
    }
    error_.clear();
    return root.dump();
  }

  std::string exportAsUSDA() {
    if (!has_stage_) {
      error_ = "No stage loaded";
      return std::string();
    }
    std::string output = tn::WriteUSDAToString(stage_);
    if (output.empty()) error_ = "USDA export failed";
    else error_.clear();
    return output;
  }

  emscripten::val exportAsUSDC() {
    if (!has_stage_) {
      error_ = "No stage loaded";
      return emscripten::val::null();
    }
    std::vector<uint8_t> output;
    tn::USDCWriteResult result = tn::WriteUSDCToMemory(output, stage_);
    if (!result.success) {
      error_ = result.error.empty() ? "USDC export failed" : result.error;
      return emscripten::val::null();
    }
    error_.clear();
    return Uint8ArrayFromVector(output);
  }

  emscripten::val exportAsUSDZ() {
    if (!has_stage_) {
      error_ = "No stage loaded";
      return emscripten::val::null();
    }
    std::vector<uint8_t> usdc;
    tn::USDCWriteResult usdc_result = tn::WriteUSDCToMemory(usdc, stage_);
    if (!usdc_result.success) {
      error_ = usdc_result.error.empty() ? "USDC export failed"
                                         : usdc_result.error;
      return emscripten::val::null();
    }
    std::vector<uint8_t> output;
    tn::USDZWriteResult result = tn::WriteUSDZFromUSDCAndAssetsToMemory(
        output, usdc.data(), usdc.size(), assets_);
    if (!result.success) {
      error_ = result.error.empty() ? "USDZ export failed" : result.error;
      return emscripten::val::null();
    }
    error_.clear();
    return Uint8ArrayFromVector(output);
  }

  emscripten::val rewriteRoot(emscripten::val bytes, const std::string& filename,
                              emscripten::val options) {
    error_.clear();
    warn_.clear();

    std::string copy_error;
    std::string input = CopyUint8ArrayToString(bytes, &copy_error);
    if (!copy_error.empty()) return ErrorResult(copy_error);

    tn::Stage stage;
    tn::LoadUSDOptions load_opts;
    load_opts.usda_options.parse_options.enable_usda_lazy_arrays = true;
    if (!options.isNull() && !options.isUndefined()) {
      emscripten::val max_memory = options["maxMemory"];
      if (!max_memory.isUndefined() && !max_memory.isNull()) {
        load_opts.max_memory = max_memory.as<size_t>();
      }
      emscripten::val usda_lazy = options["usdaLazy"];
      if (!usda_lazy.isUndefined() && !usda_lazy.isNull()) {
        load_opts.usda_options.parse_options.enable_usda_lazy_arrays =
            usda_lazy.as<bool>();
      }
    }

    const bool ok = tn::LoadUSDFromMemoryOwned(
        std::move(input), &stage, load_opts, &warn_, &error_);
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
  bool setURDFMeshBuffer(const std::string& name,
                         const emscripten::val& positions,
                         const emscripten::val& normals,
                         const emscripten::val& uvs,
                         const emscripten::val& indices) {
    if (name.empty()) {
      error_ = "setVisualMesh/setCollisionMesh requires a non-empty name";
      return false;
    }
    tr::URDFMeshBuffer buffer;
    CopyTypedArrayToVector(positions, buffer.positions);
    CopyTypedArrayToVector(normals, buffer.normals);
    CopyTypedArrayToVector(uvs, buffer.uvs);
    std::vector<uint32_t> unsigned_indices;
    CopyTypedArrayToVector(indices, unsigned_indices);
    buffer.indices.reserve(unsigned_indices.size());
    for (uint32_t index : unsigned_indices) {
      if (index > static_cast<uint32_t>(INT32_MAX)) {
        error_ = "Mesh index exceeds int32 range";
        return false;
      }
      buffer.indices.push_back(static_cast<int32_t>(index));
    }
    if (buffer.positions.size() < 9 || buffer.positions.size() % 3 != 0) {
      error_ = "Mesh positions must contain at least three xyz points";
      return false;
    }
    if (!buffer.normals.empty() &&
        buffer.normals.size() != buffer.positions.size()) {
      error_ = "Mesh normals length must match positions length";
      return false;
    }
    if (!buffer.uvs.empty() &&
        buffer.uvs.size() != (buffer.positions.size() / 3) * 2) {
      error_ = "Mesh UV length must equal vertex count * 2";
      return false;
    }
    if (!buffer.indices.empty() && buffer.indices.size() % 3 != 0) {
      error_ = "Mesh indices must contain triangles";
      return false;
    }
    urdf_mesh_buffers_[name] = std::move(buffer);
    error_.clear();
    return true;
  }

  emscripten::val ErrorResult(const std::string& error) {
    error_ = error;
    emscripten::val out = emscripten::val::object();
    out.set("success", false);
    out.set("error", error_);
    return out;
  }

  std::string error_;
  std::string warn_;
  tn::Stage stage_;
  bool has_stage_ = false;
  std::map<std::string, tr::URDFMeshBuffer> urdf_mesh_buffers_;
  std::map<std::string, std::vector<uint8_t>> assets_;
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
  void setComputeTangents(bool enabled) { compute_tangents_ = enabled; }
  void setBuildVertexIndices(bool enabled) {
    build_vertex_indices_ = enabled;
    build_vertex_indices_set_ = true;
  }

  void provideAsset(const std::string& name, const emscripten::val& bytes) {
    std::string copy_error;
    std::string data = CopyUint8ArrayToString(bytes, &copy_error);
    if (!copy_error.empty()) {
      error_ = copy_error;
      return;
    }
    std::string key = tn::AssetResolver::NormalizePath(name);
    while (key.rfind("./", 0) == 0) key.erase(0, 2);
    clip_assets_[key] = std::move(data);
  }
  void clearAssets() { clip_assets_.clear(); }

  // Strongest variant selection for a variant SET (applies to every prim
  // carrying that set, matching the compositor's set-name-keyed overrides).
  // Takes effect on the next begin()/beginOwned().
  void setVariantOverride(const std::string &set_name,
                          const std::string &selection) {
    variant_overrides_[set_name] = selection;
  }
  void clearVariantOverrides() { variant_overrides_.clear(); }

  // Authored variant sets of the most recently loaded root layer (recorded
  // before composition consumes them): [{primPath, setName, selected,
  // variants: [names...]}].
  emscripten::val listVariants() const {
    emscripten::val out = emscripten::val::array();
    for (const VariantSetInfo &info : variant_sets_) {
      emscripten::val item = emscripten::val::object();
      item.set("primPath", info.prim_path);
      item.set("setName", info.set_name);
      item.set("selected", info.selected);
      emscripten::val names = emscripten::val::array();
      for (const std::string &name : info.variant_names) {
        names.call<void>("push", name);
      }
      item.set("variants", names);
      out.call<void>("push", item);
    }
    return out;
  }
  void setTangentMethod(const std::string &method) {
    tangent_method_ = method;
    std::transform(tangent_method_.begin(), tangent_method_.end(),
                   tangent_method_.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
  }

  // Adopt the root bytes by move. USDC lazy arrays and USDA lazy slices retain
  // this buffer directly instead of copying it again inside the loader.
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
      opts.usda_options.parse_options.enable_usda_lazy_arrays = true;
      opts.usdc_options.crate_options.progress_callback =
          [](const char *phase, size_t current, size_t total) -> bool {
        reportNextCrateProgress(
            phase, static_cast<double>(current), static_cast<double>(total));
        return true;
      };
      std::string warn;
      std::string err;
      const bool ok = tinyusdz::next::LoadUSDFromMemoryOwned(
          std::move(crate), &stage_, opts, &warn, &err);
      if (!ok) {
        error_ = err.empty() ? std::string("USD memory load failed") : err;
        r.set("success", false);
        r.set("error", error_);
        return r;
      }
    }
    // Record authored variant sets (consumed by composition below), then
    // compose in place when the layer carries composition arcs — variants,
    // internal references, inherits/specializes. External arcs cannot anchor
    // for memory roots and surface as warnings (multi-layer scenes go through
    // NextFlattenSession instead).
    collectVariantSets_();
    if (tinyusdz::next::StageNeedsComposition(stage_)) {
      tinyusdz::next::pcp::CompositionOptions copts;
      copts.variant_overrides = variant_overrides_;
      std::string cwarn, cerr;
      if (!tinyusdz::next::ComposeLoadedStage(&stage_, &cwarn, &cerr, &copts,
                                              "memory-root")) {
        error_ = cerr.empty() ? std::string("in-memory composition failed")
                              : cerr;
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
    buildRenderScene_();
    loaded_ = true;
    r.set("success", true);
    r.set("meshCount", meshCount());
    r.set("points", static_cast<int>(pointsCount()));
    r.set("pointsCount", static_cast<int>(pointsCount()));
    r.set("curves", static_cast<int>(curvesCount()));
    r.set("curvesCount", static_cast<int>(curvesCount()));
    r.set("nodes", static_cast<int>(nodeCount()));
    r.set("nodeCount", static_cast<int>(nodeCount()));
    r.set("lights", static_cast<int>(lightCount()));
    r.set("lightCount", static_cast<int>(lightCount()));
    r.set("cameras", static_cast<int>(cameraCount()));
    r.set("cameraCount", static_cast<int>(cameraCount()));
    r.set("pointInstancers", static_cast<int>(pointInstancerCount()));
    r.set("pointInstancerCount", static_cast<int>(pointInstancerCount()));
    r.set("skeletons", static_cast<int>(skeletonCount()));
    r.set("skeletonCount", static_cast<int>(skeletonCount()));
    r.set("unsupportedRenderables", static_cast<int>(unsupportedRenderableCount()));
    r.set("unsupportedRenderableCount", static_cast<int>(unsupportedRenderableCount()));
    r.set("animations", static_cast<int>(animationCount()));
    r.set("animationCount", static_cast<int>(animationCount()));
    r.set("pointInstanceDraws", static_cast<int>(pointInstanceDrawCount()));
    r.set("pointInstanceDrawCount", static_cast<int>(pointInstanceDrawCount()));
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

  int nodeCount() const {
    return render_scene_valid_ ? static_cast<int>(render_scene_.nodes.size()) : 0;
  }

  int lightCount() const {
    return render_scene_valid_ ? static_cast<int>(render_scene_.lights.size()) : 0;
  }

  int pointsCount() const {
    return render_scene_valid_ ? static_cast<int>(render_scene_.points.size()) : 0;
  }

  int curvesCount() const {
    return render_scene_valid_ ? static_cast<int>(render_scene_.curves.size()) : 0;
  }

  int cameraCount() const {
    return render_scene_valid_ ? static_cast<int>(render_scene_.cameras.size()) : 0;
  }

  int pointInstancerCount() const {
    return render_scene_valid_ ? static_cast<int>(render_scene_.point_instancers.size())
                              : 0;
  }

  int pointInstanceDrawCount() const {
    return render_scene_valid_
               ? static_cast<int>(render_scene_.point_instance_draws.size())
               : 0;
  }

  int skeletonCount() const {
    return render_scene_valid_ ? static_cast<int>(render_scene_.skeletons.size()) : 0;
  }

  int animationCount() const {
    return render_scene_valid_ ? static_cast<int>(render_scene_.animations.size()) : 0;
  }

  int unsupportedRenderableCount() const {
    return render_scene_valid_
               ? static_cast<int>(render_scene_.unsupported_renderables.size())
               : 0;
  }

  std::string error() const { return error_; }

  emscripten::val getNode(int32_t node_id) const {
    emscripten::val out = emscripten::val::object();
    if (!render_scene_valid_ || node_id < 0 ||
        static_cast<size_t>(node_id) >= render_scene_.nodes.size()) {
      out.set("error", std::string("invalid node index"));
      return out;
    }
    const tr::SceneNode& node = render_scene_.nodes[static_cast<size_t>(node_id)];
    out.set("index", node_id);
    out.set("name", node.name);
    out.set("primPath", node.prim_path);
    out.set("type", NodeTypeName(node.type));
    out.set("visible", node.visible);
    out.set("dataId", node.data_id);
    out.set("parentId", node.parent_id);
    out.set("localMatrix", MatrixValue(MatrixToArray(node.local_transform)));
    out.set("worldMatrix", MatrixValue(MatrixToArray(node.world_transform)));
    out.set("children", VectorToArray(node.children));
    return out;
  }

  emscripten::val getLight(int32_t light_id) const {
    emscripten::val out = emscripten::val::object();
    if (!render_scene_valid_ || light_id < 0 ||
        static_cast<size_t>(light_id) >= render_scene_.lights.size()) {
      out.set("error", std::string("invalid light index"));
      return out;
    }
    const tr::RenderLight& light = render_scene_.lights[static_cast<size_t>(light_id)];
    out.set("index", light_id);
    out.set("name", light.name);
    out.set("primPath", light.prim_path);
    out.set("type", LightTypeName(light.type));
    out.set("typeCode", static_cast<int>(light.type));
    out.set("intensity", light.intensity);
    out.set("exposure", light.exposure);
    out.set("normalize", light.normalize);
    out.set("enableColorTemperature", light.enable_color_temperature);
    out.set("colorTemperature", light.color_temperature);
    out.set("diffuse", light.diffuse);
    out.set("specular", light.specular);
    out.set("shapingFocus", light.shaping_focus);
    out.set("shapingFocusTint", Float3Value(light.shaping_focus_tint));
    out.set("shapingConeSoftness", light.shaping_cone_softness);
    out.set("shapingIesFile", light.shaping_ies_file);
    out.set("shapingIesAngleScale", light.shaping_ies_angle_scale);
    out.set("shapingIesNormalize", light.shaping_ies_normalize);
    out.set("lightLinkTargets", VectorToArray(light.light_link_targets));
    out.set("shadowLinkTargets", VectorToArray(light.shadow_link_targets));
    out.set("filterTargets", VectorToArray(light.filter_targets));
    // Resolved CollectionAPI membership: when *LinksAll is false, the
    // *LinkMeshIndices arrays list the affected RenderScene mesh ids.
    out.set("lightLinksAll", light.light_links_all);
    out.set("lightLinkMeshIndices", VectorToArray(light.light_link_mesh_indices));
    out.set("shadowLinksAll", light.shadow_links_all);
    out.set("shadowLinkMeshIndices", VectorToArray(light.shadow_link_mesh_indices));
    out.set("enableShadow", light.enable_shadow);
    out.set("color", Float3Value(light.color));
    out.set("transform", MatrixValue(MatrixToArray(light.transform)));
    out.set("shadowColor", Float3Value(light.shadow_color));
    out.set("shadowDistance", light.shadow_distance);
    out.set("shadowFalloff", light.shadow_falloff);
    out.set("shadowFalloffGamma", light.shadow_falloff_gamma);
    switch (light.type) {
      case tr::LightType::Sphere:
        out.set("radius", light.params.sphere.radius);
        break;
      case tr::LightType::Rect:
        out.set("width", light.params.rect.width);
        out.set("height", light.params.rect.height);
        break;
      case tr::LightType::Disk:
        out.set("radius", light.params.disk.radius);
        break;
      case tr::LightType::Spot:
        out.set("angle", light.params.spot.angle);
        break;
      case tr::LightType::Dome: {
        out.set("textureId", light.params.dome.texture_id);
        const char* format = "automatic";
        switch (light.params.dome.texture_format) {
          case tr::RenderLight::DomeTextureFormat::Latlong:
            format = "latlong"; break;
          case tr::RenderLight::DomeTextureFormat::MirroredBall:
            format = "mirroredBall"; break;
          case tr::RenderLight::DomeTextureFormat::Angular:
            format = "angular"; break;
          default: break;
        }
        out.set("domeTextureFormat", std::string(format));
        break;
      }
      case tr::LightType::Cylinder:
        out.set("radius", light.params.cylinder.radius);
        out.set("length", light.params.cylinder.length);
        break;
      case tr::LightType::Directional:
        out.set("angle", light.params.distant.angle);
        break;
      default:
        break;
    }
    return out;
  }

  emscripten::val getPoints(int32_t points_id) {
    emscripten::val out = emscripten::val::object();
    if (!render_scene_valid_ || points_id < 0 ||
        static_cast<size_t>(points_id) >= render_scene_.points.size()) {
      out.set("error", std::string("invalid points index"));
      return out;
    }
    const tr::RenderPoints& points =
        render_scene_.points[static_cast<size_t>(points_id)];
    s_points_cloud_points_.clear();
    s_points_cloud_widths_.clear();
    s_points_cloud_colors_.clear();
    s_points_cloud_points_.reserve(points.points.size());
    for (size_t i = 0; i < points.points.size(); ++i) {
      s_points_cloud_points_.push_back(points.points[i]);
    }
    s_points_cloud_widths_.reserve(points.widths.size());
    for (size_t i = 0; i < points.widths.size(); ++i) {
      s_points_cloud_widths_.push_back(points.widths[i]);
    }
    s_points_cloud_colors_.reserve(points.colors.size());
    for (size_t i = 0; i < points.colors.size(); ++i) {
      s_points_cloud_colors_.push_back(points.colors[i]);
    }

    out.set("index", points_id);
    out.set("name", points.name);
    out.set("primPath", points.prim_path);
    out.set("pointCount", static_cast<int>(points.point_count()));
    out.set("materialId", points.material_id);
    out.set("points", heapF_(s_points_cloud_points_, 3));
    if (!s_points_cloud_widths_.empty()) {
      out.set("widths", heapF_(s_points_cloud_widths_, 1));
    }
    if (!s_points_cloud_colors_.empty()) {
      out.set("colors", heapF_(s_points_cloud_colors_, 3));
    }
    out.set("hasBounds", points.has_bbox);
    if (points.has_bbox) {
      out.set("bboxMin", Float3Value(points.bbox_min));
      out.set("bboxMax", Float3Value(points.bbox_max));
    }
    return out;
  }

  emscripten::val getCurves(int32_t curves_id) {
    emscripten::val out = emscripten::val::object();
    if (!render_scene_valid_ || curves_id < 0 ||
        static_cast<size_t>(curves_id) >= render_scene_.curves.size()) {
      out.set("error", std::string("invalid curves index"));
      return out;
    }
    const tr::RenderCurves& curves =
        render_scene_.curves[static_cast<size_t>(curves_id)];

    s_curve_points_.clear();
    s_curve_widths_.clear();
    s_curve_colors_.clear();
    s_curve_tessellated_points_.clear();
    s_curve_tessellated_widths_.clear();
    s_curve_tessellated_colors_.clear();
    s_curve_points_.reserve(curves.points.size());
    for (size_t i = 0; i < curves.points.size(); ++i) {
      s_curve_points_.push_back(curves.points[i]);
    }
    s_curve_widths_.reserve(curves.widths.size());
    for (size_t i = 0; i < curves.widths.size(); ++i) {
      s_curve_widths_.push_back(curves.widths[i]);
    }
    s_curve_colors_.reserve(curves.colors.size());
    for (size_t i = 0; i < curves.colors.size(); ++i) {
      s_curve_colors_.push_back(curves.colors[i]);
    }
    s_curve_tessellated_points_.reserve(curves.tessellated_points.size());
    for (size_t i = 0; i < curves.tessellated_points.size(); ++i) {
      s_curve_tessellated_points_.push_back(curves.tessellated_points[i]);
    }
    s_curve_tessellated_widths_.reserve(curves.tessellated_widths.size());
    for (size_t i = 0; i < curves.tessellated_widths.size(); ++i) {
      s_curve_tessellated_widths_.push_back(curves.tessellated_widths[i]);
    }
    s_curve_tessellated_colors_.reserve(curves.tessellated_colors.size());
    for (size_t i = 0; i < curves.tessellated_colors.size(); ++i) {
      s_curve_tessellated_colors_.push_back(curves.tessellated_colors[i]);
    }

    out.set("index", curves_id);
    out.set("name", curves.name);
    out.set("primPath", curves.prim_path);
    out.set("curveCount", static_cast<int>(curves.curve_count()));
    out.set("controlPointCount", static_cast<int>(curves.control_point_count()));
    out.set("tessellatedPointCount",
            static_cast<int>(curves.tessellated_point_count()));
    out.set("type", CurveTypeName(curves.type));
    out.set("typeCode", static_cast<int>(curves.type));
    out.set("basis", CurveBasisName(curves.basis));
    out.set("basisCode", static_cast<int>(curves.basis));
    out.set("wrap", CurveWrapName(curves.wrap));
    out.set("wrapCode", static_cast<int>(curves.wrap));
    out.set("isNurbs", curves.is_nurbs);
    out.set("materialId", curves.material_id);
    out.set("widthsInterpolation", InterpolationName(curves.widths_interp));
    out.set("colorsInterpolation", InterpolationName(curves.colors_interp));
    out.set("curveVertexCounts", VectorToArray(curves.curve_vertex_counts));
    out.set("tessellatedVertexCounts",
            VectorToArray(curves.tessellated_vertex_counts));
    out.set("points", heapF_(s_curve_points_, 3));
    out.set("tessellatedPoints", heapF_(s_curve_tessellated_points_, 3));
    if (!s_curve_widths_.empty()) {
      out.set("widths", heapF_(s_curve_widths_, 1));
    }
    if (!s_curve_colors_.empty()) {
      out.set("colors", heapF_(s_curve_colors_, 3));
    }
    if (!s_curve_tessellated_widths_.empty()) {
      out.set("tessellatedWidths", heapF_(s_curve_tessellated_widths_, 1));
    }
    if (!s_curve_tessellated_colors_.empty()) {
      out.set("tessellatedColors", heapF_(s_curve_tessellated_colors_, 3));
    }
    out.set("hasBounds", curves.has_bbox);
    if (curves.has_bbox) {
      out.set("bboxMin", Float3Value(curves.bbox_min));
      out.set("bboxMax", Float3Value(curves.bbox_max));
    }
    return out;
  }

  emscripten::val getCamera(int32_t camera_id) const {
    emscripten::val out = emscripten::val::object();
    if (!render_scene_valid_ || camera_id < 0 ||
        static_cast<size_t>(camera_id) >= render_scene_.cameras.size()) {
      out.set("error", std::string("invalid camera index"));
      return out;
    }
    const tr::RenderCamera& camera = render_scene_.cameras[static_cast<size_t>(camera_id)];
    out.set("index", camera_id);
    out.set("name", camera.name);
    out.set("primPath", camera.prim_path);
    out.set("type", CameraTypeName(camera.type));
    out.set("typeCode", static_cast<int>(camera.type));
    out.set("transform", MatrixValue(MatrixToArray(camera.transform)));
    out.set("focalLength", camera.focal_length);
    out.set("horizontalAperture", camera.horizontal_aperture);
    out.set("verticalAperture", camera.vertical_aperture);
    out.set("orthoWidth", camera.ortho_width);
    out.set("nearClip", camera.near_clip);
    out.set("farClip", camera.far_clip);
    out.set("focusDistance", camera.focus_distance);
    out.set("fStop", camera.fstop);
    out.set("shutterOpen", camera.shutter_open);
    out.set("shutterClose", camera.shutter_close);
    out.set("fovY", camera.fov_y());
    out.set("fovX", camera.fov_x());
    out.set("aspect", camera.aspect_ratio());
    return out;
  }

  emscripten::val getPointInstancer(int32_t instancer_id) const {
    emscripten::val out = emscripten::val::object();
    if (!render_scene_valid_ || instancer_id < 0 ||
        static_cast<size_t>(instancer_id) >=
            render_scene_.point_instancers.size()) {
      out.set("error", std::string("invalid point instancer index"));
      return out;
    }
    const tr::RenderPointInstancer& instancer =
        render_scene_.point_instancers[static_cast<size_t>(instancer_id)];
    out.set("index", instancer_id);
    out.set("name", instancer.name);
    out.set("primPath", instancer.prim_path);
    out.set("prototypePaths", VectorToArray(instancer.prototype_paths));
    out.set("prototypeNodeIds", VectorToArray(instancer.prototype_node_ids));
    out.set("prototypeMeshOffsets", VectorToArray(instancer.prototype_mesh_offsets));
    out.set("prototypeMeshIds", VectorToArray(instancer.prototype_mesh_ids));
    out.set("drawStart", static_cast<int>(instancer.draw_start));
    out.set("drawCount", static_cast<int>(instancer.draw_count));
    out.set("protoCount", static_cast<int>(instancer.prototype_count()));
    out.set("instanceCount", static_cast<int>(instancer.instance_count()));
    out.set("visibleInstanceCount",
            static_cast<int>(instancer.visible_instance_count()));
    out.set("hasTransforms", !instancer.transforms.empty());
    out.set("hasOrientations", instancer.has_orientations());
    out.set("hasScales", instancer.has_scales());
    out.set("hasVelocities", instancer.has_velocities());
    out.set("hasAngularVelocities", instancer.has_angular_velocities());
    out.set("valid", instancer.valid);
    if (!instancer.validation_error.empty()) {
      out.set("validationError", instancer.validation_error);
    }
    return out;
  }

  emscripten::val getPointInstanceDraw(int32_t draw_id) const {
    emscripten::val out = emscripten::val::object();
    if (!render_scene_valid_ || draw_id < 0 ||
        static_cast<size_t>(draw_id) >=
            render_scene_.point_instance_draws.size()) {
      out.set("error", std::string("invalid point instance draw index"));
      return out;
    }
    const tr::RenderPointInstanceDraw* draw =
        render_scene_.get_point_instance_draw(static_cast<size_t>(draw_id));
    if (!draw) {
      out.set("error", std::string("invalid point instance draw index"));
      return out;
    }
    out.set("index", draw_id);
    out.set("pointInstancerId", draw->point_instancer_id);
    out.set("instanceIndex", static_cast<int>(draw->instance_index));
    out.set("prototypeIndex", static_cast<int>(draw->prototype_index));
    out.set("meshId", draw->mesh_id);
    out.set("materialId", draw->material_id);
    out.set("expandedMeshId", draw->expanded_mesh_id);
    out.set("transform", MatrixValue(MatrixToArray(draw->transform)));
    if (draw->mesh_id >= 0 &&
        static_cast<size_t>(draw->mesh_id) < render_scene_.meshes.size()) {
      out.set("meshPath", render_scene_.meshes[static_cast<size_t>(draw->mesh_id)].prim_path);
    }
    if (draw->material_id >= 0 &&
        static_cast<size_t>(draw->material_id) < render_scene_.materials.size()) {
      out.set("materialPath", render_scene_.materials[static_cast<size_t>(draw->material_id)].prim_path);
    }
    return out;
  }

  emscripten::val getSkeleton(int32_t skeleton_id) const {
    emscripten::val out = emscripten::val::object();
    if (!render_scene_valid_ || skeleton_id < 0 ||
        static_cast<size_t>(skeleton_id) >= render_scene_.skeletons.size()) {
      out.set("error", std::string("invalid skeleton index"));
      return out;
    }
    const tr::Skeleton& skel = render_scene_.skeletons[static_cast<size_t>(skeleton_id)];
    out.set("index", skeleton_id);
    out.set("name", skel.name);
    out.set("primPath", skel.prim_path);
    out.set("rootJoint", skel.root_joint);
    out.set("jointCount", static_cast<int>(skel.joints.size()));
    out.set("animationId", skel.animation_id);
    out.set("animationSourcePath", skel.animation_source_path);
    emscripten::val joints = emscripten::val::array();
    for (size_t i = 0; i < skel.joints.size(); ++i) {
      const tr::SkeletonJoint& j = skel.joints[i];
      emscripten::val jo = emscripten::val::object();
      jo.set("index", static_cast<int>(i));
      jo.set("name", j.name);
      jo.set("path", j.path);
      jo.set("parentId", j.parent_id);
      jo.set("bindMatrix", MatrixValue(MatrixToArray(j.bind_transform)));
      jo.set("restMatrix", MatrixValue(MatrixToArray(j.rest_transform)));
      jo.set("children", VectorToArray(j.children));
      joints.set(static_cast<int>(i), jo);
    }
    out.set("joints", joints);
    return out;
  }

  emscripten::val getAnimation(int32_t anim_id) const {
    emscripten::val out = emscripten::val::object();
    if (!loaded_ || anim_id < 0 ||
        static_cast<size_t>(anim_id) >= render_scene_.animations.size()) {
      return out;
    }
    const tr::AnimationClip& clip = render_scene_.animations[static_cast<size_t>(anim_id)];

    const double duration = clip.end_time - clip.start_time;
    const double clamped_duration = std::isfinite(duration)
                                       ? std::max(0.0, duration)
                                       : 0.0;
    out.set("index", anim_id);
    out.set("name", clip.name.empty()
                           ? std::string("Animation") + std::to_string(anim_id)
                           : clip.name);
    out.set("primPath", clip.prim_path);
    out.set("startTime", clip.start_time);
    out.set("endTime", clip.end_time);
    out.set("duration", clamped_duration);

    emscripten::val channels = emscripten::val::array();
    emscripten::val samplers = emscripten::val::array();
    emscripten::val tracks = emscripten::val::array();

    for (size_t i = 0; i < clip.channels.size(); ++i) {
      const tr::AnimationChannel& channel = clip.channels[i];

      std::vector<float> times;
      times.reserve(channel.keyframes.size());
      std::vector<float> values;
      values.reserve(channel.keyframes.size() *
                     AnimationComponentCount(channel));

      for (const auto& keyframe : channel.keyframes) {
        times.push_back(static_cast<float>(keyframe.time));
        AppendAnimationKeyframeValues(channel, keyframe, &values);
      }

      const std::string path = AnimationPathName(channel.target_path);
      const std::string interpolation =
          AnimationInterpolationName(channel.interpolation);
      const int32_t sampler_id = static_cast<int32_t>(i);

      emscripten::val sampler = emscripten::val::object();
      sampler.set("index", sampler_id);
      sampler.set("interpolation", interpolation);
      sampler.set("times", VectorToArray(times));
      sampler.set("values", VectorToArray(values));
      sampler.set("valueStride", static_cast<int>(channel.value_stride));
      sampler.set("elementCount", static_cast<int>(channel.element_count));
      sampler.set("isSkeletal", channel.is_skeletal);
      if (!channel.array_values.empty()) {
        sampler.set("arrayValues", VectorToArray(channel.array_values));
      }
      samplers.set(static_cast<int>(sampler_id), sampler);

      emscripten::val ch = emscripten::val::object();
      ch.set("sampler", sampler_id);
      ch.set("target_node", channel.target_node);
      ch.set("target_prim_path", channel.target_prim_path);
      ch.set("target_type", channel.is_skeletal ? std::string("SkelAnimation")
                                                 : std::string("SceneNode"));
      ch.set("skeleton_id", channel.target_skeleton);
      ch.set("joint_id", -1);
      ch.set("path", path);
      ch.set("isCustomProperty",
             channel.target_path == tr::AnimationChannel::TargetPath::CustomProperty);
      ch.set("propertyName", channel.property_name);
      ch.set("isSkeletal", channel.is_skeletal);
      ch.set("targetSkeletonPath", channel.target_skeleton_path);
      ch.set("jointOrder", VectorToArray(channel.joint_order));
      ch.set("jointRemap", VectorToArray(channel.joint_remap));
      ch.set("blendShapeOrder", VectorToArray(channel.blend_shape_order));
      ch.set("valueStride", static_cast<int>(channel.value_stride));
      ch.set("elementCount", static_cast<int>(channel.element_count));
      channels.set(static_cast<int>(i), ch);

      emscripten::val track = emscripten::val::object();
      track.set("sampler", sampler_id);
      track.set("target_node", channel.target_node);
      track.set("path", path);
      track.set("interpolation", interpolation);
      track.set("times", VectorToArray(times));
      track.set("values", VectorToArray(values));
      track.set("isSkeletal", channel.is_skeletal);
      track.set("propertyName", channel.property_name);
      track.set("targetSkeletonId", channel.target_skeleton);
      track.set("targetSkeletonPath", channel.target_skeleton_path);
      track.set("jointRemap", VectorToArray(channel.joint_remap));
      track.set("valueStride", static_cast<int>(channel.value_stride));
      track.set("elementCount", static_cast<int>(channel.element_count));
      if (!channel.array_values.empty()) {
        track.set("arrayValues", VectorToArray(channel.array_values));
      }

      std::string track_type = "number";
      switch (channel.target_path) {
        case tr::AnimationChannel::TargetPath::Translation:
          track.set("name", path);
          track_type = channel.is_skeletal ? "vector3Array" : "vector3";
          break;
        case tr::AnimationChannel::TargetPath::Rotation:
          track.set("name", path);
          track_type = channel.is_skeletal ? "quaternionArray" : "quaternion";
          break;
        case tr::AnimationChannel::TargetPath::Scale:
          track.set("name", path);
          track_type = channel.is_skeletal ? "vector3Array" : "vector3";
          break;
        case tr::AnimationChannel::TargetPath::Weights:
          track.set("name", path);
          track_type = channel.is_skeletal ? "weightArray" : "number";
          break;
        case tr::AnimationChannel::TargetPath::CustomProperty:
        default:
          track.set("name", path);
          track_type = "number";
          break;
      }
      track.set("type", track_type);
      tracks.set(static_cast<int>(i), track);
    }

    out.set("channels", channels);
    out.set("samplers", samplers);
    out.set("tracks", tracks);
    out.set("numChannels", static_cast<int>(clip.channels.size()));
    out.set("numSamplers", static_cast<int>(clip.channels.size()));
    out.set("has_skeletal_animation", AnimationHasSkeletalChannels(clip));
    out.set("has_node_animation", static_cast<bool>(!clip.channels.empty()));

    if (clip.channels.empty()) {
      out.set("tracks", emscripten::val::array());
    }

    return out;
  }

  emscripten::val getAllAnimations() const {
    emscripten::val animations = emscripten::val::array();
    if (!loaded_) {
      return animations;
    }
    for (int i = 0; i < static_cast<int>(render_scene_.animations.size()); ++i) {
      animations.call<void>("push", getAnimation(i));
    }
    return animations;
  }

  emscripten::val getAnimationInfo(int32_t anim_id) const {
    emscripten::val info = emscripten::val::object();
    if (!loaded_ || anim_id < 0 ||
        static_cast<size_t>(anim_id) >= render_scene_.animations.size()) {
      return info;
    }

    const tr::AnimationClip& clip = render_scene_.animations[static_cast<size_t>(anim_id)];
    info.set("id", anim_id);
    info.set("name", clip.name.empty()
                          ? std::string("Animation") + std::to_string(anim_id)
                          : clip.name);
    info.set("duration", clip.end_time - clip.start_time);
    info.set("numTracks", static_cast<int>(clip.channels.size()));
    info.set("numSamplers", static_cast<int>(clip.channels.size()));
    info.set("numTargetNodes", AnimationTargetNodeCount(clip));
    const bool has_skel = AnimationHasSkeletalChannels(clip);
    info.set("has_skeletal_animation", has_skel);
    info.set("has_node_animation", static_cast<bool>(!clip.channels.empty()));
    info.set("startTime", clip.start_time);
    info.set("endTime", clip.end_time);
    info.set("clipAssetPaths", VectorToArray(clip.clip_asset_paths));
    info.set("numClipAssetPaths",
             static_cast<int>(clip.clip_asset_paths.size()));
    info.set("valueClipBaked", clip.value_clip_baked);
    info.set("sourceType", clip.value_clip_baked
                               ? std::string("ValueClip")
                               : (has_skel ? std::string("SkelAnimation")
                                           : std::string("XformOp")));
    return info;
  }

  emscripten::val getAllAnimationInfos() const {
    emscripten::val infos = emscripten::val::array();
    if (!loaded_) {
      return infos;
    }
    for (int i = 0; i < static_cast<int>(render_scene_.animations.size()); ++i) {
      infos.call<void>("push", getAnimationInfo(i));
    }
    return infos;
  }

  emscripten::val getUnsupportedRenderables() const {
    emscripten::val out = emscripten::val::array();
    if (!render_scene_valid_) return out;
    for (size_t i = 0; i < render_scene_.unsupported_renderables.size(); ++i) {
      const tr::UnsupportedRenderable& unsupported =
          render_scene_.unsupported_renderables[static_cast<size_t>(i)];
      emscripten::val item = emscripten::val::object();
      item.set("index", static_cast<int>(i));
      item.set("primPath", unsupported.prim_path);
      item.set("type", unsupported.type_name);
      item.set("reason", unsupported.reason);
      out.set(static_cast<int>(i), item);
    }
    return out;
  }

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
    if (render_scene_valid_) {
      s.set("renderSceneNodes", static_cast<int>(render_scene_.nodes.size()));
      s.set("renderSceneMeshes", static_cast<int>(render_scene_.meshes.size()));
      s.set("renderScenePoints", static_cast<int>(render_scene_.points.size()));
      s.set("renderSceneCurves", static_cast<int>(render_scene_.curves.size()));
      s.set("renderScenePointInstancers",
            static_cast<int>(render_scene_.point_instancers.size()));
      s.set("renderScenePointInstanceDraws",
            static_cast<int>(render_scene_.point_instance_draws.size()));
      s.set("renderSceneMaterials",
            static_cast<int>(render_scene_.materials.size()));
      s.set("renderSceneTextures",
            static_cast<int>(render_scene_.textures.size()));
      s.set("renderSceneImages", static_cast<int>(render_scene_.images.size()));
      s.set("renderSceneLights", static_cast<int>(render_scene_.lights.size()));
      s.set("renderSceneCameras",
            static_cast<int>(render_scene_.cameras.size()));
      s.set("renderSceneAnimations",
            static_cast<int>(render_scene_.animations.size()));
      s.set("renderSceneSkeletons",
            static_cast<int>(render_scene_.skeletons.size()));
      s.set("renderSceneUnsupportedRenderables",
            static_cast<int>(render_scene_.unsupported_renderables.size()));
      s.set("renderSceneWarnings", static_cast<int>(render_scene_warnings_.size()));
    } else {
      s.set("renderSceneNodes", 0);
      s.set("renderSceneMeshes", 0);
      s.set("renderScenePoints", 0);
      s.set("renderSceneCurves", 0);
      s.set("renderScenePointInstancers", 0);
      s.set("renderScenePointInstanceDraws", 0);
      s.set("renderSceneMaterials", 0);
      s.set("renderSceneTextures", 0);
      s.set("renderSceneImages", 0);
      s.set("renderSceneLights", 0);
      s.set("renderSceneCameras", 0);
      s.set("renderSceneAnimations", 0);
      s.set("renderSceneSkeletons", 0);
      s.set("renderSceneUnsupportedRenderables", 0);
      s.set("renderSceneWarnings", 0);
    }
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
    render_scene_valid_ = false;
    render_scene_ = tr::RenderScene();
    render_scene_warnings_.clear();
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
    freeVec_(s_tangents_);
    freeVec_(s_indices_);
    freeVec_(s_joint_indices_);
    freeVec_(s_joint_weights_);
    freeVec_(s_point_source_indices_);
    freeVec_(s_points_cloud_points_);
    freeVec_(s_points_cloud_widths_);
    freeVec_(s_points_cloud_colors_);
    freeVec_(s_curve_points_);
    freeVec_(s_curve_widths_);
    freeVec_(s_curve_colors_);
    freeVec_(s_curve_tessellated_points_);
    freeVec_(s_curve_tessellated_widths_);
    freeVec_(s_curve_tessellated_colors_);
  }

  void buildRenderScene_() {
    tr::ConverterConfig cfg;
    cfg.time_code = 0.0;
    cfg.mesh.compute_tangents = compute_tangents_;
    cfg.mesh.tangent_method = tangentMethod_();
    cfg.mesh.enable_bone_reduction = true;
    cfg.mesh.target_bone_count = 4;
    cfg.material.load_textures = false;
    cfg.material.allow_missing_textures = true;
    cfg.point_instancer.duplicate_meshes = false;
    cfg.animation.clip_stage_loader =
        [this](const std::string& asset_path, tn::Stage* stage,
               std::string* warn, std::string* err) {
          std::string key = tn::AssetResolver::NormalizePath(asset_path);
          while (key.rfind("./", 0) == 0) key.erase(0, 2);
          auto it = clip_assets_.find(key);
          if (it == clip_assets_.end()) {
            for (const std::string& candidate :
                 tn::AssetResolver::SuffixCandidates(key)) {
              it = clip_assets_.find(candidate);
              if (it != clip_assets_.end()) break;
            }
          }
          if (it == clip_assets_.end()) {
            if (err) *err = "asset was not supplied to RenderStream";
            return false;
          }
          tn::LoadUSDOptions options;
          options.usda_options.parse_options.enable_usda_lazy_arrays = true;
          std::string bytes = it->second;
          return tn::LoadUSDFromMemoryOwned(std::move(bytes), stage, options,
                                            warn, err);
        };
    tr::RenderSceneConverter converter(cfg);
    tr::ConvertResult result = converter.Convert(stage_);
    render_scene_ = std::move(result.scene);
    render_scene_warnings_ = result.warnings;
    render_scene_valid_ = result.success;
    if (!result.success && !result.error.empty()) {
      error_ = result.error;
    }
  }

 private:
  struct TextureMeta {
    std::string path;
    std::string source_color_space = "auto";
    std::string wrap_s = "useMetadata";
    std::string wrap_t = "useMetadata";
    bool is_udim = false;
  };

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
    TextureMeta base_color_meta;
    TextureMeta normal_meta;
    TextureMeta roughness_meta;
    TextureMeta metallic_meta;
    TextureMeta occlusion_meta;
    TextureMeta emissive_meta;
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
    if (compute_tangents_ && computeScratchTangents_()) {
      out.set("tangents", heapF_(s_tangents_, 4));
      out.set("tangentMethod", tangent_method_);
    }
    s_joint_indices_.clear();
    s_joint_weights_.clear();
    if (render_scene_valid_) {
      auto it = render_scene_.mesh_by_path.find(prim.GetPath().str());
      if (it != render_scene_.mesh_by_path.end() && it->second >= 0 &&
          static_cast<size_t>(it->second) < render_scene_.meshes.size()) {
        const tr::RenderMesh& rmesh =
            render_scene_.meshes[static_cast<size_t>(it->second)];
        if (rmesh.skin) {
          const int element_size =
              static_cast<int>(rmesh.skin->influences_per_vertex);
          if (element_size > 0 &&
              s_point_source_indices_.size() == s_points_.size() / 3) {
            const size_t influences = static_cast<size_t>(element_size);
            s_joint_indices_.reserve(s_point_source_indices_.size() * influences);
            s_joint_weights_.reserve(s_point_source_indices_.size() * influences);
            for (uint32_t source_point : s_point_source_indices_) {
              const size_t source = static_cast<size_t>(source_point) * influences;
              if (source + influences > rmesh.skin->joint_indices.size() ||
                  source + influences > rmesh.skin->joint_weights.size()) {
                continue;
              }
              for (size_t influence = 0; influence < influences; ++influence) {
                s_joint_indices_.push_back(
                    rmesh.skin->joint_indices[source + influence]);
                s_joint_weights_.push_back(
                    rmesh.skin->joint_weights[source + influence]);
              }
            }
          }
          if (!s_joint_indices_.empty()) {
            out.set("jointIndices", heapU16_(s_joint_indices_));
          }
          if (!s_joint_weights_.empty()) {
            out.set("jointWeights", heapF_(s_joint_weights_, 1));
          }
          out.set("skel_id", rmesh.skin->skeleton_id);
          out.set("skeletonPath", rmesh.skin->skeleton_path);
          out.set("elementSize", element_size);
          out.set("hasGeomBindTransform", true);
          out.set("geomBindTransform",
                  MatrixValue(MatrixToArray(rmesh.skin->geom_bind_transform)));
        }
        if (!rmesh.blend_shapes.empty()) {
          auto float_array = [](const tr::FloatChunked& values) {
            emscripten::val array = emscripten::val::array();
            for (size_t index = 0; index < values.size(); ++index) {
              array.set(static_cast<unsigned>(index), values[index]);
            }
            return array;
          };
          auto remapped_offsets = [this](
              const tr::FloatChunked& values,
              const std::vector<uint32_t>& sparse_points) {
            emscripten::val array = emscripten::val::array();
            std::unordered_map<uint32_t, size_t> sparse_index;
            for (size_t i = 0; i < sparse_points.size(); ++i) {
              sparse_index.emplace(sparse_points[i], i);
            }
            size_t output = 0;
            for (uint32_t source_point : s_point_source_indices_) {
              size_t source_offset = static_cast<size_t>(source_point) * 3;
              if (!sparse_points.empty()) {
                const auto it = sparse_index.find(source_point);
                source_offset = it == sparse_index.end()
                                    ? (std::numeric_limits<size_t>::max)()
                                    : it->second * 3;
              }
              for (size_t component = 0; component < 3; ++component) {
                const float value =
                    source_offset != (std::numeric_limits<size_t>::max)() &&
                            source_offset + component < values.size()
                        ? values[source_offset + component]
                        : 0.0f;
                array.set(static_cast<unsigned>(output++), value);
              }
            }
            return array;
          };
          emscripten::val shapes = emscripten::val::array();
          for (size_t shape_index = 0;
               shape_index < rmesh.blend_shapes.size(); ++shape_index) {
            const tr::RenderMesh::BlendShape& shape =
                rmesh.blend_shapes[shape_index];
            emscripten::val value = emscripten::val::object();
            value.set("name", shape.name);
            value.set("weight", shape.weight);
            value.set("pointOffsets",
                      remapped_offsets(shape.point_offsets,
                                       shape.point_indices));
            value.set("normalOffsets",
                      shape.normal_offsets.empty()
                          ? float_array(shape.normal_offsets)
                          : remapped_offsets(shape.normal_offsets,
                                             shape.point_indices));
            value.set("pointIndices", emscripten::val::array());
            emscripten::val inbetweens = emscripten::val::array();
            for (size_t i = 0; i < shape.inbetweens.size(); ++i) {
              const tr::RenderMesh::BlendShape::Inbetween& source =
                  shape.inbetweens[i];
              emscripten::val inbetween = emscripten::val::object();
              inbetween.set("name", source.name);
              inbetween.set("weight", source.weight);
              inbetween.set("pointOffsets",
                            remapped_offsets(source.point_offsets,
                                             shape.point_indices));
              inbetweens.set(static_cast<unsigned>(i), inbetween);
            }
            value.set("inbetweens", inbetweens);
            shapes.set(static_cast<unsigned>(shape_index), value);
          }
          out.set("blendShapes", shapes);
        }
      }
    }
    out.set("localMatrix", matArray_(localMatrix_(prim)));
    out.set("worldMatrix", matArray_(worldMatrixForPrim_(prim)));
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

  tr::MeshConfig::TangentComputationMethod tangentMethod_() const {
    if (tangent_method_ == "mikk" || tangent_method_ == "mikktspace") {
      return tr::MeshConfig::TangentComputationMethod::MikkTSpace;
    }
    if (tangent_method_ == "fast" || tangent_method_ == "fastmikk" ||
        tangent_method_ == "fastmikktspace") {
      return tr::MeshConfig::TangentComputationMethod::FastMikkTSpace;
    }
    if (tangent_method_ == "lengyel") {
      return tr::MeshConfig::TangentComputationMethod::Lengyel;
    }
    return tr::MeshConfig::TangentComputationMethod::Hybrid;
  }

  bool computeScratchTangents_() {
    s_tangents_.clear();
    const size_t vertex_count = s_points_.size() / 3;
    if (vertex_count == 0 || s_normals_.size() != vertex_count * 3 ||
        s_uv_.size() != vertex_count * 2) {
      return false;
    }
    // ~40B/vertex of temporaries; fail the (optional) tangents instead of
    // abort()ing when the heap is nearly full.
    if (!tr::ProbeAlloc(vertex_count * (3 + 3 + 4) * sizeof(float))) {
      return false;
    }

    std::vector<float> tan(vertex_count * 3, 0.0f);
    std::vector<float> bit(vertex_count * 3, 0.0f);

    auto addTri = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
      if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count) {
        return;
      }
      const float *p0 = &s_points_[size_t(i0) * 3];
      const float *p1 = &s_points_[size_t(i1) * 3];
      const float *p2 = &s_points_[size_t(i2) * 3];
      const float *u0 = &s_uv_[size_t(i0) * 2];
      const float *u1 = &s_uv_[size_t(i1) * 2];
      const float *u2 = &s_uv_[size_t(i2) * 2];
      const float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
      const float e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
      const float du1 = u1[0] - u0[0];
      const float dv1 = u1[1] - u0[1];
      const float du2 = u2[0] - u0[0];
      const float dv2 = u2[1] - u0[1];
      const float det = du1 * dv2 - du2 * dv1;
      if (std::fabs(det) <= 1.0e-12f) return;
      const float r = 1.0f / det;
      const float sdir[3] = {(dv2 * e1[0] - dv1 * e2[0]) * r,
                             (dv2 * e1[1] - dv1 * e2[1]) * r,
                             (dv2 * e1[2] - dv1 * e2[2]) * r};
      const float tdir[3] = {(du1 * e2[0] - du2 * e1[0]) * r,
                             (du1 * e2[1] - du2 * e1[1]) * r,
                             (du1 * e2[2] - du2 * e1[2]) * r};
      const uint32_t ids[3] = {i0, i1, i2};
      for (uint32_t id : ids) {
        tan[size_t(id) * 3 + 0] += sdir[0];
        tan[size_t(id) * 3 + 1] += sdir[1];
        tan[size_t(id) * 3 + 2] += sdir[2];
        bit[size_t(id) * 3 + 0] += tdir[0];
        bit[size_t(id) * 3 + 1] += tdir[1];
        bit[size_t(id) * 3 + 2] += tdir[2];
      }
    };

    if (!s_indices_.empty()) {
      for (size_t i = 0; i + 2 < s_indices_.size(); i += 3) {
        addTri(s_indices_[i], s_indices_[i + 1], s_indices_[i + 2]);
      }
    } else {
      for (size_t i = 0; i + 2 < vertex_count; i += 3) {
        addTri(static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1),
               static_cast<uint32_t>(i + 2));
      }
    }

    s_tangents_.assign(vertex_count * 4, 0.0f);
    for (size_t v = 0; v < vertex_count; ++v) {
      const float *n = &s_normals_[v * 3];
      const float *tv = &tan[v * 3];
      const float ndt = n[0] * tv[0] + n[1] * tv[1] + n[2] * tv[2];
      float tx = tv[0] - n[0] * ndt;
      float ty = tv[1] - n[1] * ndt;
      float tz = tv[2] - n[2] * ndt;
      const float len = std::sqrt(tx * tx + ty * ty + tz * tz);
      if (len > 1.0e-12f) {
        tx /= len;
        ty /= len;
        tz /= len;
      } else {
        tx = 1.0f;
        ty = 0.0f;
        tz = 0.0f;
      }
      const float *bv = &bit[v * 3];
      const float cx = n[1] * tz - n[2] * ty;
      const float cy = n[2] * tx - n[0] * tz;
      const float cz = n[0] * ty - n[1] * tx;
      const float w = (cx * bv[0] + cy * bv[1] + cz * bv[2]) < 0.0f
                          ? -1.0f
                          : 1.0f;
      s_tangents_[v * 4 + 0] = tx;
      s_tangents_[v * 4 + 1] = ty;
      s_tangents_[v * 4 + 2] = tz;
      s_tangents_[v * 4 + 3] = w;
    }
    return true;
  }

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
    s_point_source_indices_.clear();

    if (!needExpand) {
      s_points_ = std::move(P);
      s_point_source_indices_.resize(vtxCount);
      for (size_t i = 0; i < vtxCount; ++i) {
        s_point_source_indices_[i] = static_cast<uint32_t>(i);
      }
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

    // Preserve the pre-existing adaptive behavior unless callers explicitly
    // request an index strategy.
    const bool doWeld = build_vertex_indices_set_ ? build_vertex_indices_ : [&]() {
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
      return vtxCount > 0 && vtxCount < cornerCount / 3;
    }();

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
      // Pre-flight the expanded-soup buffers (~44B per corner incl. this
      // scratch) so a huge mesh in a nearly-full heap fails this mesh with an
      // error instead of abort()ing the module (-fno-exceptions).
      if (!tr::ProbeAlloc(corners * 44)) {
        s_points_.clear(); s_normals_.clear(); s_uv_.clear(); s_indices_.clear();
        if (err) *err = "Out of memory expanding mesh corners";
        return false;
      }
      s_points_.resize(corners * 3);
      s_point_source_indices_.resize(corners, 0);
      if (!UV.empty()) s_uv_.assign(corners * 2, 0.0f);
      if (haveN) s_normals_.resize(corners * 3);
      for (size_t c = 0; c < corners; ++c) {
        const size_t slot = slots[c];
        const int32_t vi = (slot < faceVtx) ? fvi[slot] : -1;
        if (vi >= 0) s_point_source_indices_[c] = static_cast<uint32_t>(vi);
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
    // Pre-flight a coarse budget for the weld structures: the index buffer is
    // ~4B per corner and the weld map ~56B per unique vertex (worst case one
    // per corner). This turns the realistic huge-mesh OOM into a per-mesh
    // error instead of an allocator abort; the map still grows incrementally.
    {
      size_t weld_corners = 0;
      for (int32_t nn : fvc) {
        if (nn >= 3) weld_corners += static_cast<size_t>(nn - 2) * 3;
      }
      const size_t probe = weld_corners * 4 +
                           std::min(weld_corners, vtxCount ? vtxCount * 2 : weld_corners) * 56;
      if (!tr::ProbeAlloc(probe)) {
        if (err) *err = "Out of memory welding mesh vertices";
        return false;
      }
    }
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
      s_point_source_indices_.push_back(
          vi < 0 ? uint32_t(0) : static_cast<uint32_t>(vi));
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

  static bool isUdimPath_(const std::string &path) {
    return path.find("<UDIM>") != std::string::npos ||
           path.find("%04d") != std::string::npos ||
           path.find("%(UDIM)d") != std::string::npos;
  }

  TextureMeta texMeta_(const std::string &connPath) {
    TextureMeta meta;
    if (connPath.empty()) return meta;
    const size_t slash = connPath.rfind('/');
    const size_t dot = connPath.find('.', slash == std::string::npos ? 0 : slash);
    const std::string primPath = (dot == std::string::npos)
                                     ? connPath
                                     : connPath.substr(0, dot);
    tinyusdz::next::UsdPrim tex = stage_.GetPrimAtPath(primPath);
    if (!tex.IsValid()) return meta;

    const tinyusdz::next::Value *file = tex.GetPropertyValue("inputs:file");
    if (file) {
      if (const std::string *a = file->as_asset_path()) {
        meta.path = *a;
      } else if (const std::string *s = file->as_string()) {
        meta.path = *s;
      }
    }
    auto read_tokenish = [&](const char *name, std::string *out) {
      if (!out) return;
      const tinyusdz::next::Value *v = tex.GetPropertyValue(name);
      if (!v) return;
      if (const std::string *tok = v->as_token()) {
        *out = *tok;
      } else if (const std::string *str = v->as_string()) {
        *out = *str;
      }
    };
    read_tokenish("inputs:sourceColorSpace", &meta.source_color_space);
    // colorSpace asset metadata on inputs:file wins over sourceColorSpace
    // (legacy tydra resolution order).
    if (const tinyusdz::next::PropMeta *file_meta =
            tex.GetPropertyMeta("inputs:file")) {
      if (!file_meta->colorSpace.empty()) {
        meta.source_color_space = file_meta->colorSpace;
      }
    }
    read_tokenish("inputs:wrapS", &meta.wrap_s);
    read_tokenish("inputs:wrapT", &meta.wrap_t);
    meta.is_udim = isUdimPath_(meta.path);
    return meta;
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
        rec.base_color_meta = texMeta_(ps.diffuse_texture);
        rec.normal_meta = texMeta_(ps.normal_texture);
        rec.roughness_meta = texMeta_(ps.roughness_texture);
        rec.metallic_meta = texMeta_(ps.metallic_texture);
        rec.occlusion_meta = texMeta_(ps.occlusion_texture);
        rec.emissive_meta = texMeta_(ps.emissive_texture);
        rec.base_color_texture = rec.base_color_meta.path;
        rec.normal_texture = rec.normal_meta.path;
        rec.roughness_texture = rec.roughness_meta.path;
        rec.metallic_texture = rec.metallic_meta.path;
        rec.occlusion_texture = rec.occlusion_meta.path;
        rec.emissive_texture = rec.emissive_meta.path;
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

  bool appendToAccumulator_(const tinyusdz::next::UsdPrim &prim,
                            int32_t material_id,
                            bool soup,
                            MergeAccumulator *acc) {
    if (!acc) return false;
    // Pre-flight the accumulator growth (vector doubling can transiently need
    // ~2x): a failed probe keeps this mesh unmerged instead of abort()ing.
    {
      const size_t add_bytes =
          (s_points_.size() + s_normals_.size() + s_uv_.size()) * sizeof(float) +
          s_indices_.size() * sizeof(uint32_t);
      if (!tr::ProbeAlloc(add_bytes * 2 + acc->mesh.points.size() * sizeof(float))) {
        return false;
      }
    }
    const std::array<double, 16> world = worldMatrixForPrim_(prim);
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
    return true;
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
      const std::array<double, 16> world = worldMatrixForPrim_(prim);
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
      if (!appendToAccumulator_(prim, material_id, soup, &acc)) {
        // Heap too full to merge: flush the group and emit this mesh unmerged.
        flushAccumulator_(&acc);
        OutputMesh out;
        out.merged = false;
        out.source_index = static_cast<int>(i);
        outputs_.push_back(out);
        stats_.skipped_merge_count++;
      }
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
  emscripten::val heapU16_(const std::vector<uint16_t> &v) const {
    emscripten::val d = emscripten::val::object();
    d.set("ptr", static_cast<double>(reinterpret_cast<uintptr_t>(v.data())));
    d.set("length", static_cast<double>(v.size()));
    d.set("comps", 1);
    d.set("dtype", std::string("u16"));
    d.set("byteLength", static_cast<double>(v.size() * sizeof(uint16_t)));
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

  // World transform for a prim, preferring the RenderScene node table: its
  // hierarchy traversal handles native instances correctly, while the plain
  // GetParent() chain in worldMatrix_ drops the instance root's own xform.
  std::array<double, 16> worldMatrixForPrim_(
      const tinyusdz::next::UsdPrim &prim) const {
    if (render_scene_valid_) {
      const auto it = render_scene_.node_by_path.find(prim.GetPath().str());
      if (it != render_scene_.node_by_path.end() && it->second >= 0 &&
          static_cast<size_t>(it->second) < render_scene_.nodes.size()) {
        const tr::SceneNode &node =
            render_scene_.nodes[static_cast<size_t>(it->second)];
        std::array<double, 16> world;
        for (int i = 0; i < 16; ++i) {
          world[static_cast<size_t>(i)] =
              static_cast<double>(node.world_transform.m[i]);
        }
        return world;
      }
    }
    return worldMatrix_(prim);
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
    const tr::RenderMaterial* render_mat = nullptr;
    auto mat_it = render_scene_.material_by_path.find(rec.prim_path);
    if (mat_it != render_scene_.material_by_path.end()) {
      render_mat = render_scene_.get_material(mat_it->second);
    }
    if (render_mat) {
      m.set("shaderType", RenderMaterialShaderTypeName(render_mat->shader_type));
      emscripten::val mtlx = emscripten::val::object();
      mtlx.set("authored", render_mat->mtlx_config.authored);
      mtlx.set("version", render_mat->mtlx_config.version);
      mtlx.set("namespace", render_mat->mtlx_config.name_space);
      mtlx.set("colorspace", render_mat->mtlx_config.colorspace);
      mtlx.set("sourceUri", render_mat->mtlx_config.source_uri);
      m.set("materialXConfig", mtlx);
      m.set("materialXJson", RenderMaterialJson(render_scene_, *render_mat));
      if (render_mat->openpbr &&
          !render_mat->openpbr->nodegraph_json.empty()) {
        m.set("openPBRNodeGraphJson", render_mat->openpbr->nodegraph_json);
      }
    }
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
    auto metaObject = [](const TextureMeta &meta) {
      emscripten::val out = emscripten::val::object();
      out.set("path", meta.path);
      out.set("sourceColorSpace", meta.source_color_space);
      out.set("wrapS", meta.wrap_s);
      out.set("wrapT", meta.wrap_t);
      out.set("isUdim", meta.is_udim);
      return out;
    };
    emscripten::val texture_meta = emscripten::val::object();
    if (!rec.base_color_meta.path.empty()) {
      texture_meta.set("baseColor", metaObject(rec.base_color_meta));
    }
    if (!rec.normal_meta.path.empty()) {
      texture_meta.set("normal", metaObject(rec.normal_meta));
    }
    if (!rec.roughness_meta.path.empty()) {
      texture_meta.set("roughness", metaObject(rec.roughness_meta));
    }
    if (!rec.metallic_meta.path.empty()) {
      texture_meta.set("metallic", metaObject(rec.metallic_meta));
    }
    if (!rec.occlusion_meta.path.empty()) {
      texture_meta.set("occlusion", metaObject(rec.occlusion_meta));
    }
    if (!rec.emissive_meta.path.empty()) {
      texture_meta.set("emissive", metaObject(rec.emissive_meta));
    }
    m.set("textureMetadata", texture_meta);
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
    // Prefer the converter's triangle-space subset ranges: they account for
    // holes, degenerate faces, earcut splits and topology sanitization,
    // which the stage-side re-derivation below cannot.
    if (render_scene_valid_) {
      const auto mit = render_scene_.mesh_by_path.find(prim.GetPath().str());
      if (mit != render_scene_.mesh_by_path.end() &&
          static_cast<size_t>(mit->second) < render_scene_.meshes.size()) {
        const tr::RenderMesh &rmesh =
            render_scene_.meshes[static_cast<size_t>(mit->second)];
        if (!rmesh.material_subsets.empty() &&
            !rmesh.face_triangle_offsets.empty()) {
          emscripten::val materials = emscripten::val::array();
          emscripten::val groups = emscripten::val::array();
          std::map<int32_t, int> mat_index_by_scene_id;
          int group_index = 0;
          for (const tr::RenderMesh::MaterialSubset &ms :
               rmesh.material_subsets) {
            if (ms.material_id < 0 ||
                static_cast<size_t>(ms.material_id) >=
                    render_scene_.materials.size()) {
              continue;
            }
            int mat_index = -1;
            const auto found = mat_index_by_scene_id.find(ms.material_id);
            if (found == mat_index_by_scene_id.end()) {
              const std::string &mat_path =
                  render_scene_.materials[static_cast<size_t>(ms.material_id)]
                      .prim_path;
              tinyusdz::next::UsdPrim mat_prim = stage_.GetPrimAtPath(mat_path);
              const int32_t record_id = registerMaterial_(mat_prim);
              mat_index = static_cast<int>(mat_index_by_scene_id.size());
              mat_index_by_scene_id.emplace(ms.material_id, mat_index);
              materials.set(mat_index, materialObject_(record_id));
            } else {
              mat_index = found->second;
            }
            emscripten::val g = emscripten::val::object();
            g.set("start", static_cast<int>(ms.face_start * 3u));
            g.set("count", static_cast<int>(ms.face_count * 3u));
            g.set("materialIndex", mat_index);
            groups.set(group_index++, g);
          }
          if (group_index > 0) {
            out.set("materials", materials);
            out.set("submeshes", groups);
          }
          return;
        }
      }
    }

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
  tr::RenderScene render_scene_;
  bool render_scene_valid_ = false;
  std::vector<std::string> render_scene_warnings_;
  std::vector<tinyusdz::next::UsdGeomMesh> meshes_;
  std::vector<OutputMesh> outputs_;
  std::vector<MaterialRecord> materials_;
  std::unordered_map<std::string, int32_t> material_key_to_id_;
  std::unordered_map<std::string, int32_t> material_path_to_id_;
  std::set<std::string> source_material_keys_;
  std::set<std::string> source_texture_keys_;
  std::set<std::string> texture_keys_;
  std::map<std::string, std::string> clip_assets_;
  struct VariantSetInfo {
    std::string prim_path;
    std::string set_name;
    std::string selected;
    std::vector<std::string> variant_names;
  };

  void collectVariantSets_() {
    variant_sets_.clear();
    const tinyusdz::next::Layer *root = stage_.GetRootLayer();
    if (!root) return;
    for (const auto &prim : root->prims()) {
      const auto &meta = prim.meta();
      for (const auto &vs : meta.variantSets()) {
        VariantSetInfo info;
        info.prim_path = prim.path().str();
        info.set_name = vs.name;
        // Authored selection lives in the prim's `variants = {...}` metadata
        // (variantSelections / legacy single variantSelection), not on the
        // VariantSetData itself.
        info.selected = vs.selected;
        for (const auto &sel : meta.variantSelections()) {
          if (sel.first == vs.name) {
            info.selected = sel.second;
            break;
          }
        }
        if (info.selected.empty() && !meta.variantSelection.empty()) {
          const std::string &legacy = meta.variantSelection;
          const size_t eq = legacy.find('=');
          if (eq != std::string::npos && legacy.substr(0, eq) == vs.name) {
            info.selected = legacy.substr(eq + 1);
          }
        }
        for (const auto &variant : vs.variants) {
          info.variant_names.push_back(variant.name);
        }
        variant_sets_.push_back(std::move(info));
      }
    }
  }

  std::vector<VariantSetInfo> variant_sets_;
  std::map<std::string, std::string> variant_overrides_;

  Stats stats_;
  bool loaded_ = false;
  bool material_dedup_ = false;
  bool mesh_merge_ = false;
  bool mesh_merge_bake_transform_ = false;
  bool flatten_render_tree_ = false;
  bool compute_tangents_ = false;
  bool build_vertex_indices_ = true;
  bool build_vertex_indices_set_ = false;
  std::string tangent_method_ = "hybrid";
  std::string error_;
  std::vector<float> s_points_, s_normals_, s_uv_, s_tangents_;
  std::vector<float> s_points_cloud_points_, s_points_cloud_widths_;
  std::vector<float> s_points_cloud_colors_;
  std::vector<float> s_curve_points_, s_curve_widths_, s_curve_colors_;
  std::vector<float> s_curve_tessellated_points_;
  std::vector<float> s_curve_tessellated_widths_;
  std::vector<float> s_curve_tessellated_colors_;
  std::vector<uint32_t> s_indices_;
  std::vector<uint32_t> s_point_source_indices_;
  std::vector<uint16_t> s_joint_indices_;
  std::vector<float> s_joint_weights_;
};

namespace {

int OptInt(const emscripten::val& opts, const char* key, int def) {
  emscripten::val v = opts[key];
  if (v.isUndefined() || v.isNull()) return def;
  return v.as<int>();
}

double OptDouble(const emscripten::val& opts, const char* key, double def) {
  emscripten::val v = opts[key];
  if (v.isUndefined() || v.isNull()) return def;
  return v.as<double>();
}

bool OptBool(const emscripten::val& opts, const char* key, bool def) {
  emscripten::val v = opts[key];
  if (v.isUndefined() || v.isNull()) return def;
  return v.as<bool>();
}

std::string OptStr(const emscripten::val& opts, const char* key,
                   const char* def) {
  emscripten::val v = opts[key];
  if (v.isUndefined() || v.isNull()) return def;
  return v.as<std::string>();
}

}  // namespace

namespace {

tn::ValidationOptions ParseValidationOptionsJSONForWeb(
    const std::string& options_json) {
  tn::ValidationOptions opts;
  if (options_json.empty()) return opts;

  nlohmann::json args = nlohmann::json::parse(options_json, nullptr, false);
  if (args.is_discarded() || !args.is_object() || !args.contains("groups") ||
      !args["groups"].is_array()) {
    return opts;
  }

  opts.core = false;
  opts.geom = false;
  opts.shade = false;
  opts.lux = false;
  opts.physics = false;
  opts.crate = false;
  for (const auto& group : args["groups"]) {
    if (!group.is_string()) continue;
    const std::string name = group.get<std::string>();
    if (name == "core") {
      opts.core = true;
    } else if (name == "geom") {
      opts.geom = true;
    } else if (name == "shade") {
      opts.shade = true;
    } else if (name == "lux") {
      opts.lux = true;
    } else if (name == "physics") {
      opts.physics = true;
    } else if (name == "crate") {
      opts.crate = true;
    } else if (name == "all") {
      opts = tn::MakeValidateAllOptions();
    }
  }
  if (!opts.core && !opts.geom && !opts.shade && !opts.lux && !opts.physics &&
      !opts.crate) {
    opts.core = true;
  }
  return opts;
}

nlohmann::json ValidationResultToJSON(const tn::USDValidationResult& v) {
  nlohmann::json result;
  result["parse_ok"] = true;
  result["ok"] = v.ok();
  result["error_count"] = v.error_count();
  result["warning_count"] = v.warning_count();
  result["spec_version"] = tn::GetAOUSDCoreSpecVersionString();
  {
    nlohmann::json groups = nlohmann::json::array();
    for (const std::string& name :
         tn::GetValidationGroupNames(v.checked_groups)) {
      groups.push_back(name);
    }
    result["checked_groups"] = groups;
  }
  nlohmann::json issues = nlohmann::json::array();
  for (const tn::USDValidationIssue* issue :
       tn::GetOrderedValidationIssues(v)) {
    nlohmann::json item;
    item["severity"] =
        issue->severity == tn::USDValidationSeverity::Error ? "error"
                                                            : "warning";
    item["rule_id"] = issue->rule_id;
    item["location"] = issue->location;
    item["message"] = issue->message;
    issues.push_back(item);
  }
  result["issues"] = issues;
  return result;
}

}  // namespace

// validateFromBinary(bytes, filename, optionsJson) -> JSON string, matching
// the legacy TinyUSDZLoaderNative.validateFromBinary contract consumed by
// web/js/validation.js. Runs AOUSD-core validation over next::Layer.
static std::string validateFromBinary(const emscripten::val& data,
                                      const std::string& filename,
                                      const std::string& options_json) {
  std::string copy_error;
  std::string bytes = CopyUint8ArrayToString(data, &copy_error);
  const tn::ValidationOptions options =
      ParseValidationOptionsJSONForWeb(options_json);

  nlohmann::json result;
  tn::USDValidationResult validation;
  std::string warn, err;
  const bool loaded = tn::ValidateUSDFromMemoryAgainstAOUSDCore(
      reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), filename,
      options, &validation, &warn, &err);
  if (!loaded) {
    result["parse_ok"] = false;
    result["ok"] = false;
    result["error"] = err.empty() ? copy_error : err;
    if (!warn.empty()) result["warn"] = warn;
    return result.dump();
  }

  result = ValidationResultToJSON(validation);
  if (!warn.empty()) result["warn"] = warn;
  return result.dump();
}

// usddiff(opts) -> { success, hasDiffs, text?, json?, error?, warn? }
// opts: { left:{data:Uint8Array, name?}, right:{data:Uint8Array, name?},
//         format?:"text"|"json"|"both", ulps?, eps?, compareMetadata?,
//         fuzzyAssetPaths? }
// Pre-composition layer diff over next::Layer, mirroring the legacy module's
// usddiff / native tusddiff contract.
static emscripten::val usddiff(const emscripten::val& opts) {
  emscripten::val result = emscripten::val::object();

  if (opts.isUndefined() || opts.isNull()) {
    result.set("success", false);
    result.set("error", std::string("usddiff: missing options"));
    return result;
  }
  emscripten::val left = opts["left"];
  emscripten::val right = opts["right"];
  if (left.isUndefined() || left.isNull() || right.isUndefined() ||
      right.isNull()) {
    result.set("success", false);
    result.set("error",
               std::string("usddiff: 'left' and 'right' are required"));
    return result;
  }

  std::string copy_error;
  std::string lhsBuf = CopyUint8ArrayToString(left["data"], &copy_error);
  std::string rhsBuf = CopyUint8ArrayToString(right["data"], &copy_error);
  const std::string lhsName = OptStr(left, "name", "left");
  const std::string rhsName = OptStr(right, "name", "right");
  const std::string format = OptStr(opts, "format", "text");

  tn::DiffOptions diffOpts;
  {
    const int ulps = OptInt(opts, "ulps", -1);
    if (ulps >= 0) {
      diffOpts.floatUlps = static_cast<uint32_t>(ulps);
      diffOpts.doubleUlps = static_cast<uint64_t>(ulps);
    }
    diffOpts.absEps = OptDouble(opts, "eps", diffOpts.absEps);
    diffOpts.compareMetadata =
        OptBool(opts, "compareMetadata", diffOpts.compareMetadata);
    diffOpts.fuzzyAssetPaths =
        OptBool(opts, "fuzzyAssetPaths", diffOpts.fuzzyAssetPaths);
  }

  std::string warn, err;
  std::shared_ptr<tn::Layer> lhs = tn::pcp::LoadLayerFromMemory(
      lhsName, reinterpret_cast<const uint8_t*>(lhsBuf.data()), lhsBuf.size(),
      &warn, &err);
  if (!lhs) {
    result.set("success", false);
    result.set("error", "Error loading " + lhsName + ": " + err);
    return result;
  }
  err.clear();
  std::shared_ptr<tn::Layer> rhs = tn::pcp::LoadLayerFromMemory(
      rhsName, reinterpret_cast<const uint8_t*>(rhsBuf.data()), rhsBuf.size(),
      &warn, &err);
  if (!rhs) {
    result.set("success", false);
    result.set("error", "Error loading " + rhsName + ": " + err);
    return result;
  }

  std::unordered_map<std::string, tn::PrimSpecDiff> psDiffs;
  std::unordered_map<std::string, tn::PropDiff> propDiffs;
  tn::LayerMetaDiff layerMetaDiff;
  tn::Diff(*lhs, *rhs, psDiffs, propDiffs, diffOpts, &layerMetaDiff);
  const bool hasDiffs =
      !psDiffs.empty() || !propDiffs.empty() || layerMetaDiff.changed();

  result.set("success", true);
  result.set("hasDiffs", hasDiffs);
  if (!warn.empty()) result.set("warn", warn);
  if (format == "text" || format == "both") {
    result.set("text", hasDiffs
                           ? tn::DiffToText(*lhs, *rhs, lhsName, rhsName,
                                            diffOpts)
                           : std::string("No differences found.\n"));
  }
  if (format == "json" || format == "both") {
    result.set("json", tn::DiffToJSON(*lhs, *rhs, lhsName, rhsName, diffOpts));
  }
  return result;
}

EMSCRIPTEN_BINDINGS(tinyusdz_next_render_stream) {
  emscripten::function("usddiff", &usddiff);
  emscripten::function("validateFromBinary", &validateFromBinary);

  emscripten::class_<NextUSDZConverterNative>("NextUSDZConverterNative")
      .constructor<>()
      .function("rewriteRoot", &NextUSDZConverterNative::rewriteRoot)
      .function("clearURDFMeshBuffers",
                &NextUSDZConverterNative::clearURDFMeshBuffers)
      .function("setVisualMesh", &NextUSDZConverterNative::setVisualMesh)
      .function("setCollisionMesh", &NextUSDZConverterNative::setCollisionMesh)
      .function("createURDFPhysicsScene",
                &NextUSDZConverterNative::createURDFPhysicsScene)
      .function("loadFromBinary", &NextUSDZConverterNative::loadFromBinary)
      .function("extractPhysicsSceneJSON",
                &NextUSDZConverterNative::extractPhysicsSceneJSON)
      .function("setAsset", &NextUSDZConverterNative::setAsset)
      .function("setUSDCExportLimitMB",
                &NextUSDZConverterNative::setUSDCExportLimitMB)
      .function("exportAsUSDA", &NextUSDZConverterNative::exportAsUSDA)
      .function("exportAsUSDC", &NextUSDZConverterNative::exportAsUSDC)
      .function("exportAsUSDZ", &NextUSDZConverterNative::exportAsUSDZ)
      .function("error", &NextUSDZConverterNative::error)
      .function("warn", &NextUSDZConverterNative::warn);

  emscripten::class_<SubdivStreamer>("SubdivStreamer")
      .constructor<>()
      .function("refineStream", &SubdivStreamer::refineStream)
      .function("heapBytes", &SubdivStreamer::heapBytes);

  emscripten::class_<NextFlattenSession>("NextFlattenSession")
      .constructor<>()
      .function("begin", &NextFlattenSession::begin)
      .function("setVariantOverride", &NextFlattenSession::setVariantOverride)
      .function("provideLayer", &NextFlattenSession::provideLayer)
      .function("step", &NextFlattenSession::step)
      .function("end", &NextFlattenSession::end);

  emscripten::class_<RenderStream>("RenderStream")
      .constructor<>()
      .function("setMaterialDedup", &RenderStream::setMaterialDedup)
      .function("setMeshMerge", &RenderStream::setMeshMerge)
      .function("setMeshMergeBakeTransform",
                &RenderStream::setMeshMergeBakeTransform)
      .function("setFlattenRenderTree", &RenderStream::setFlattenRenderTree)
      .function("setComputeTangents", &RenderStream::setComputeTangents)
      .function("setBuildVertexIndices", &RenderStream::setBuildVertexIndices)
      .function("setTangentMethod", &RenderStream::setTangentMethod)
      .function("provideAsset", &RenderStream::provideAsset)
      .function("clearAssets", &RenderStream::clearAssets)
      .function("setVariantOverride", &RenderStream::setVariantOverride)
      .function("clearVariantOverrides", &RenderStream::clearVariantOverrides)
      .function("listVariants", &RenderStream::listVariants)
      .function("begin", &RenderStream::begin)
      .function("beginOwned", &RenderStream::beginOwned)
      .function("meshCount", &RenderStream::meshCount)
      .function("numMeshes", &RenderStream::meshCount)
      .function("nodeCount", &RenderStream::nodeCount)
      .function("numNodes", &RenderStream::nodeCount)
      .function("lightCount", &RenderStream::lightCount)
      .function("numLights", &RenderStream::lightCount)
      .function("pointsCount", &RenderStream::pointsCount)
      .function("numPoints", &RenderStream::pointsCount)
      .function("curvesCount", &RenderStream::curvesCount)
      .function("numCurves", &RenderStream::curvesCount)
      .function("cameraCount", &RenderStream::cameraCount)
      .function("numCameras", &RenderStream::cameraCount)
      .function("pointInstancerCount", &RenderStream::pointInstancerCount)
      .function("numPointInstancers", &RenderStream::pointInstancerCount)
      .function("pointInstanceDrawCount", &RenderStream::pointInstanceDrawCount)
      .function("numPointInstanceDraws", &RenderStream::pointInstanceDrawCount)
      .function("skeletonCount", &RenderStream::skeletonCount)
      .function("numSkeletons", &RenderStream::skeletonCount)
      .function("unsupportedRenderableCount",
                &RenderStream::unsupportedRenderableCount)
      .function("numUnsupportedRenderables", &RenderStream::unsupportedRenderableCount)
      .function("numAnimations", &RenderStream::animationCount)
      .function("getAnimation", &RenderStream::getAnimation)
      .function("getAllAnimations", &RenderStream::getAllAnimations)
      .function("getAnimationInfo", &RenderStream::getAnimationInfo)
      .function("getAllAnimationInfos", &RenderStream::getAllAnimationInfos)
      .function("getNode", &RenderStream::getNode)
      .function("getLight", &RenderStream::getLight)
      .function("getPoints", &RenderStream::getPoints)
      .function("getCurves", &RenderStream::getCurves)
      .function("getCamera", &RenderStream::getCamera)
      .function("getPointInstancer", &RenderStream::getPointInstancer)
      .function("getPointInstanceDraw", &RenderStream::getPointInstanceDraw)
      .function("getSkeleton", &RenderStream::getSkeleton)
      .function("getUnsupportedRenderables",
                &RenderStream::getUnsupportedRenderables)
      .function("getSceneMetadata", &RenderStream::getSceneMetadata)
      .function("getStats", &RenderStream::getStats)
      .function("getMesh", &RenderStream::getMesh)
      .function("error", &RenderStream::error)
      .function("end", &RenderStream::end);
}
