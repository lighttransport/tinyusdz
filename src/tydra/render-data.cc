// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// TODO:
//   - [ ] Subdivision surface to polygon mesh conversion.
//     - [ ] Correctly handle primvar with 'vertex' interpolation(Use the basis
//     function of subd surface)
//   - [x] Support time-varying shader attribute(timeSamples)
//   - [ ] Wide gamut colorspace conversion support
//     - [ ] linear sRGB <-> linear DisplayP3
//   - [x] Compute tangentes and binormals
//   - [x] displayColor, displayOpacity primvar(vertex color)
//   - [x] Support Skeleton
//   - [x] Support SkelAnimation
//     - [x] joint animation
//     - [x] blendshape animation
//     - [x] explicit joint order
//   - [ ] Support Inbetween BlendShape
//   - [ ] Support material binding collection(Collection API)
//   - [ ] Support multiple skel animation
//   https://github.com/PixarAnimationStudios/OpenUSD/issues/2246
//   - [ ] Adjust normal vector computation with handness?
//   - [ ] Node xform animation
//   - [ ] Better build of index buffer
//     - [ ] Preserve the order of 'points' variable(mesh.points, Skin
//     indices/weights, BlendShape points, ...) as much as possible.
//     - Implement spatial hash
//
#include <numeric>

#include "common-utils.hh"
#include "common-types.hh"
#include "image-loader.hh"
#include "image-util.hh"
#include "image-types.hh"
#include "linear-algebra.hh"
#include "math-util.inc"
#include "pprinter.hh"
#include "prim-types.hh"
#include "str-util.hh"
#include "tiny-format.hh"
#include "tinyusdz.hh"
#include "usdGeom.hh"
#include "usdShade.hh"
#include "usdLux.hh"
#include "usdMtlx.hh"
#include "value-pprint.hh"
#include "logger.hh"
#include "bone-util.hh"
#include "shape-to-mesh.hh"
#include "materialx-to-json.hh"


// Helper macros for iterating over TypedTimeSamples in both AoS and SoA modes
#ifdef TINYUSDZ_USE_TIMESAMPLES_SOA
#define FOREACH_TIMESAMPLES_BEGIN(ts, var_t, var_value, var_blocked) \
  { \
    const auto &_times = (ts).get_times(); \
    const auto &_values = (ts).get_values(); \
    const auto &_blocked = (ts).get_blocked(); \
    for (size_t _idx = 0; _idx < _times.size(); _idx++) { \
      const double var_t = _times[_idx]; \
      const auto &var_value = _values[_idx]; \
      const bool var_blocked = _blocked[_idx]; \
      if (!var_blocked) {

#define FOREACH_TIMESAMPLES_END() \
      } \
    } \
  }

#define TIMESAMPLES_EMPTY(ts) ((ts).size() == 0)

#else
#define FOREACH_TIMESAMPLES_BEGIN(ts, var_t, var_value, var_blocked) \
  for (const auto &_sample : (ts).get_samples()) { \
    const double var_t = _sample.t; \
    const auto &var_value = _sample.value; \
    const bool var_blocked = _sample.blocked; \
    if (!var_blocked) {

#define FOREACH_TIMESAMPLES_END() \
    } \
  }

//#define TIMESAMPLES_EMPTY(ts) ((ts).get_samples().empty())
#endif

#if defined(TINYUSDZ_WITH_COLORIO)
#include "external/tiny-color-io.h"
#endif

#if defined(TINYUSDZ_WITH_MESHOPT)
#include "external/meshoptimizer/meshoptimizer.h"
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// For tangent/binormal computation
// NOTE: HalfEdge is not used atm.
#include "external/half-edge.hh"

#ifdef TYDRA_ROBUST_TANGENT
#include "robust-tangent.hh"
#endif

// For triangulation.
// TODO: Use tinyobjloader's triangulation
#include "external/mapbox/earcut/earcut.hpp"

// For kNN point search
// #include "external/nanoflann.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

//
#include "common-macros.inc"
#include "math-util.inc"

//
#include "tydra/attribute-eval.hh"
#include "tydra/render-data.hh"
#include "tydra/scene-access.hh"
#include "tydra/shader-network.hh"

namespace tinyusdz {

namespace tydra {

namespace {

#define PushError(msg) TYDRA_PUSH_ERROR(err, msg)

// Structure to hold MaterialX NodeGraph info extracted from geometry_normal/geometry_tangent connections
// Used to capture tangent rotation and normal map scale for anisotropic materials
struct MtlxNodeGraphInfo {
  float tangent_rotation{0.0f};      // From ND_rotate3d_vector3 node's "amount" input (degrees)
  float normal_map_scale{1.0f};      // From ND_normalmap_float node's "scale" input
  bool has_normal_map{false};        // True if ND_normalmap node was found in the chain
  bool has_tangent_rotation{false};  // True if ND_rotate3d_vector3 node was found
  std::string normal_map_texture;    // Path to normal map texture asset
};

// Extract MaterialX NodeGraph info by traversing connections
// Returns the extracted info or an error message
static nonstd::expected<MtlxNodeGraphInfo, std::string> ExtractMtlxNodeGraphInfo(
    const Stage &stage,
    const Prim *material_prim,
    const std::vector<Path> &connections,
    std::string *err) {

  MtlxNodeGraphInfo info;

  if (connections.empty()) {
    return info;  // No connections, return default
  }

  // Follow the first connection (we only support single connection)
  Path current_path = connections[0];

  // Maximum depth to prevent infinite loops
  int max_depth = 15;

  while (max_depth-- > 0) {
    std::string current_prim_part = current_path.prim_part();
    std::string current_prop_part = current_path.prop_part();
    if (err) {
      *err += "DEBUG: current_prim_part=" + current_prim_part + ", prop_part=" + current_prop_part + "\n";
    }

    const Prim *current_prim{nullptr};

    // Try to find the prim in the stage
    std::string lookup_err;
    bool found = stage.find_prim_at_path(Path(current_prim_part, ""), current_prim, &lookup_err);

    // If not found in stage, try looking in material's children (NodeGraph case)
    if (!found || !current_prim) {
      if (err) {
        *err += "DEBUG: Not found in stage, looking in material children. material_prim=" + std::string(material_prim ? "valid" : "null") + "\n";
      }
      if (material_prim) {
        if (err) {
          *err += "DEBUG: material_prim has " + std::to_string(material_prim->children().size()) + " children\n";
        }
        // Look for NodeGraph child
        for (const auto& child : material_prim->children()) {
          if (err) {
            *err += "DEBUG: Checking child: '" + child.element_name() + "' type='" + child.type_name() + "' is_nodegraph=" + (child.as<NodeGraph>() ? "true" : "false") + "\n";
          }
          // Try to match by type if the path contains "NodeGraph" and this child is a NodeGraph
          if (current_prim_part.find("NodeGraph") != std::string::npos && child.as<NodeGraph>()) {
            if (err) {
              *err += "DEBUG: Found NodeGraph by type matching\n";
            }
            const NodeGraph* ng = child.as<NodeGraph>();

            // Extract target name from path
            size_t last_slash = current_prim_part.rfind('/');
            std::string target_name = (last_slash != std::string::npos)
                ? current_prim_part.substr(last_slash + 1)
                : current_prim_part;

            // If target_name matches the NodeGraph name OR is "NodeGraph" itself
            if (target_name == child.element_name() || target_name == "NodeGraph") {
              // The path points to the NodeGraph itself
              // Set current_prim to the NodeGraph's prim so we can handle it below
              if (err) {
                *err += "DEBUG: Path points to NodeGraph itself, setting current_prim\n";
              }
              current_prim = &child;
              break;
            }

            // Found NodeGraph, look for the target node in NodeGraph children
            if (err) {
              *err += "DEBUG: Looking for '" + target_name + "' in NodeGraph with " + std::to_string(child.children().size()) + " children\n";
            }
            for (const auto& ng_child : child.children()) {
              if (err) {
                *err += "DEBUG: NodeGraph child: '" + ng_child.element_name() + "'\n";
              }
              if (ng_child.element_name() == target_name) {
                if (err) {
                  *err += "DEBUG: Found target in NodeGraph children\n";
                }
                current_prim = &ng_child;
                break;
              }
            }
            (void)ng;  // suppress unused variable warning
            if (current_prim) break;
          }
        }
      }
    }

    if (!current_prim) {
      // Can't find the prim, stop traversal
      break;
    }

    // Check if it's a Shader
    const Shader *shader = current_prim->as<Shader>();
    if (err) {
      *err += "DEBUG: Checking prim type: type_name='" + current_prim->type_name() + "' is_shader=" + (shader ? "true" : "false") + "\n";
    }
    if (!shader) {
      // Not a shader, might be a NodeGraph - try to follow its output
      if (const NodeGraph *ng = current_prim->as<NodeGraph>()) {
        // Get the output property specified in the connection
        std::string prop_part = current_path.prop_part();
        if (err) {
          std::string props_list;
          for (const auto& kv : ng->props) {
            props_list += " '" + kv.first + "'";
          }
          *err += "DEBUG NodeGraph props:" + props_list + ", looking for: '" + prop_part + "'\n";
        }
        auto it = ng->props.find(prop_part);
        if (it != ng->props.end() && it->second.is_attribute()) {
          const Attribute &attr = it->second.get_attribute();
          if (attr.has_connections()) {
            const auto &conns = attr.connections();
            if (!conns.empty()) {
              if (err) {
                *err += "DEBUG: Found property, following connection to: " + conns[0].full_path_name() + "\n";
              }
              current_path = conns[0];
              continue;
            }
          }
        } else {
          if (err) {
            *err += "DEBUG: Property '" + prop_part + "' not found in ng->props\n";
          }
        }
      }
      break;
    }

    // Check for specific MaterialX node types
    const std::string &node_type = shader->info_id;
    if (err) {
      *err += "DEBUG: Shader info_id='" + node_type + "'\n";
    }

    // For generic shaders (MaterialX nodes), properties are stored in ShaderNode inside shader->value
    // We need to get the correct props map
    const std::map<std::string, Property> *shader_props = &shader->props;
    if (const ShaderNode *shader_node = shader->value.as<ShaderNode>()) {
      shader_props = &shader_node->props;
      DCOUT("Using ShaderNode props (size=" << shader_props->size() << ")");
    }

    if (node_type == "ND_normalmap_float" || node_type == "ND_normalmap") {
      info.has_normal_map = true;
      DCOUT("Found ND_normalmap shader, props=" << shader_props->size());

      // Extract scale input
      auto scale_it = shader_props->find("inputs:scale");
      if (scale_it != shader_props->end() && scale_it->second.is_attribute()) {
        const Attribute &scale_attr = scale_it->second.get_attribute();
        if (auto scale_val = scale_attr.get_value<float>()) {
          info.normal_map_scale = scale_val.value();
        }
      }

      // Follow inputs:in to find the texture
      auto in_it = shader_props->find("inputs:in");
      if (in_it != shader_props->end() && in_it->second.is_attribute()) {
        const Attribute &in_attr = in_it->second.get_attribute();
        if (in_attr.has_connections()) {
          current_path = in_attr.connections()[0];
          DCOUT("Following inputs:in connection to: " << current_path.full_path_name());
          continue;
        }
      }
      DCOUT("No inputs:in connection, breaking");
      break;  // No more connections to follow
    } else if (node_type == "ND_rotate3d_vector3") {
      info.has_tangent_rotation = true;

      // Extract amount input (rotation angle in degrees)
      auto amount_it = shader_props->find("inputs:amount");
      if (amount_it != shader_props->end() && amount_it->second.is_attribute()) {
        const Attribute &amount_attr = amount_it->second.get_attribute();
        if (auto amount_val = amount_attr.get_value<float>()) {
          info.tangent_rotation = amount_val.value();
        }
      }

      // Follow inputs:in to continue traversal
      auto in_it = shader_props->find("inputs:in");
      if (in_it != shader_props->end() && in_it->second.is_attribute()) {
        const Attribute &in_attr = in_it->second.get_attribute();
        if (in_attr.has_connections()) {
          current_path = in_attr.connections()[0];
          continue;
        }
      }
    } else if (node_type == "ND_image_vector3" || node_type == "ND_image_vector4" ||
               node_type == "ND_image_color3" || node_type == "ND_image_color4") {
      // Found the texture node - extract file path
      DCOUT("Found ND_image node, props=" << shader_props->size());
      auto file_it = shader_props->find("inputs:file");
      if (file_it != shader_props->end() && file_it->second.is_attribute()) {
        const Attribute &file_attr = file_it->second.get_attribute();
        if (auto asset_val = file_attr.get_value<value::AssetPath>()) {
          info.normal_map_texture = asset_val.value().GetAssetPath();
          DCOUT("Found normal_map_texture: " << info.normal_map_texture);
        }
      }
      break;  // End of chain
    } else if (node_type == "ND_normalize_vector3" ||
               node_type == "ND_convert_vector4_vector3" ||
               node_type == "ND_convert_color4_vector3") {
      // Conversion/normalization nodes - follow inputs:in
      auto in_it = shader_props->find("inputs:in");
      if (in_it != shader_props->end() && in_it->second.is_attribute()) {
        const Attribute &in_attr = in_it->second.get_attribute();
        if (in_attr.has_connections()) {
          current_path = in_attr.connections()[0];
          continue;
        }
      }
      break;
    } else if (node_type == "ND_tangent_vector3" || node_type == "ND_normal_vector3") {
      // Geometry nodes - end of chain
      break;
    } else {
      // Unknown node type, try to follow inputs:in if it exists
      auto in_it = shader_props->find("inputs:in");
      if (in_it != shader_props->end() && in_it->second.is_attribute()) {
        const Attribute &in_attr = in_it->second.get_attribute();
        if (in_attr.has_connections()) {
          current_path = in_attr.connections()[0];
          continue;
        }
      }
      break;
    }
  }

  return info;
}

//
// Convert vertex attribute with Uniform variability(interpolation) to
// facevarying variability, by replicating uniform value per face over face
// vertices.
//

//
// Convert vertex attribute with Uniform variability(interpolation) to vertex
// variability, by replicating uniform value for vertices of a face. For shared
// vertex, the value will be overwritten.
//

nonstd::expected<std::vector<uint8_t>, std::string> UniformToVertex(
    const std::vector<uint8_t> &inputs, const size_t stride_bytes,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices) {
  // NOTE: Uniform -> Vertex convertsion may give wrong result.
  std::vector<uint8_t> dst;

  if (stride_bytes == 0) {
    return nonstd::make_unexpected(fmt::format("stride_bytes is zero."));
  }

  if (faceVertexIndices.size() < 3) {
    return nonstd::make_unexpected(
        fmt::format("faceVertexIndices.size must be 3 or greater, but got {}.",
                    faceVertexCounts.size()));
  }

  if ((inputs.size() % stride_bytes) != 0) {
    return nonstd::make_unexpected(
        fmt::format("input bytes {} must be dividable by stride_bytes {}.",
                    inputs.size(), stride_bytes));
  }

  size_t num_uniforms = inputs.size() / stride_bytes;

  if (num_uniforms != faceVertexCounts.size()) {
    return nonstd::make_unexpected(fmt::format(
        "The number of input uniform attributes {} must be the same with "
        "faceVertexCounts.size() {}",
        num_uniforms, faceVertexCounts.size()));
  }

  const uint32_t num_vertices =
      *std::max_element(faceVertexIndices.cbegin(), faceVertexIndices.cend()) + 1;

  dst.resize(num_vertices * stride_bytes);

  size_t fvIndexOffset{0};

  for (size_t i = 0; i < faceVertexCounts.size(); i++) {
    size_t cnt = faceVertexCounts[i];

    if ((fvIndexOffset + cnt) > faceVertexIndices.size()) {
      return nonstd::make_unexpected(
          fmt::format("faceVertexCounts[{}] {} gives buffer-overrun to "
                      "faceVertexIndices.size {}.",
                      i, cnt, faceVertexIndices.size()));
    }

    for (size_t k = 0; k < cnt; k++) {
      uint32_t v_idx = faceVertexIndices[fvIndexOffset + k];

      // may overwrite the value when a vertex is referenced from multiple facet.
      memcpy(dst.data() + v_idx * stride_bytes,
             inputs.data() + i * stride_bytes, stride_bytes);
    }

    fvIndexOffset += cnt;
  }

  return dst;
}

// Generic uniform to facevarying conversion
nonstd::expected<std::vector<uint8_t>, std::string> UniformToFaceVarying(
    const std::vector<uint8_t> &src, const size_t stride_bytes,
    const std::vector<uint32_t> &faceVertexCounts) {
  std::vector<uint8_t> dst;

  if (stride_bytes == 0) {
    return nonstd::make_unexpected("stride_bytes is zero.");
  }

  if ((src.size() % stride_bytes) != 0) {
    return nonstd::make_unexpected(
        fmt::format("input bytes {} must be the multiple of stride_bytes {}",
                    src.size(), stride_bytes));
  }

  size_t num_uniforms = src.size() / stride_bytes;

  if (num_uniforms != faceVertexCounts.size()) {
    return nonstd::make_unexpected(fmt::format(
        "The number of input uniform attributes {} must be the same with "
        "faceVertexCounts.size() {}",
        num_uniforms, faceVertexCounts.size()));
  }

  std::vector<uint8_t> buf;
  buf.resize(stride_bytes);

  for (size_t i = 0; i < faceVertexCounts.size(); i++) {
    size_t cnt = faceVertexCounts[i];

    memcpy(buf.data(), src.data() + i * stride_bytes, stride_bytes);

    // repeat cnt times.
    for (size_t k = 0; k < cnt; k++) {
      dst.insert(dst.end(), buf.begin(), buf.end());
    }
  }

  return dst;
}

//
// Convert vertex attribute with Vertex variability(interpolation) to
// facevarying attribute, by expanding(flatten) the value per vertex per face.
//

// Generic vertex to facevarying conversion
nonstd::expected<std::vector<uint8_t>, std::string> VertexToFaceVarying(
    const std::vector<uint8_t> &src, const size_t stride_bytes,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices) {

  if (src.empty()) {
    return nonstd::make_unexpected("src data is empty.");
  }

  if (stride_bytes == 0) {
    return nonstd::make_unexpected("stride_bytes must be non-zero.");
  }

  if ((src.size() % stride_bytes) != 0) {
    return nonstd::make_unexpected(
        fmt::format("src size {} must be the multiple of stride_bytes {}",
                    src.size(), stride_bytes));
  }

  const size_t num_vertices = src.size() / stride_bytes;

  // Pre-allocate output buffer to exact size needed
  const size_t total_face_vertices = faceVertexIndices.size();
  std::vector<uint8_t> dst;
  dst.resize(total_face_vertices * stride_bytes);

  const uint8_t* src_data = src.data();
  uint8_t* dst_ptr = dst.data();

  size_t faceVertexIndexOffset{0};

  for (size_t i = 0; i < faceVertexCounts.size(); i++) {
    size_t cnt = faceVertexCounts[i];

    for (size_t k = 0; k < cnt; k++) {
      size_t fv_idx = k + faceVertexIndexOffset;

      if (fv_idx >= faceVertexIndices.size()) {
        return nonstd::make_unexpected(
            fmt::format("faeVertexIndex {} out-of-range at faceVertexCount[{}]",
                        fv_idx, i));
      }

      size_t v_idx = faceVertexIndices[fv_idx];

      if (v_idx >= num_vertices) {
        return nonstd::make_unexpected(fmt::format(
            "faeVertexIndices[{}] {} exceeds the number of vertices {}", fv_idx,
            v_idx, num_vertices));
      }

      // Direct memcpy to pre-allocated destination
      std::memcpy(dst_ptr, src_data + v_idx * stride_bytes, stride_bytes);
      dst_ptr += stride_bytes;
    }

    faceVertexIndexOffset += cnt;
  }

  return dst;
}


static nonstd::expected<std::vector<uint8_t>, std::string> ConstantToVertex(
    const std::vector<uint8_t> &src, const size_t stride_bytes,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices) {
  if (faceVertexCounts.empty()) {
    return nonstd::make_unexpected("faceVertexCounts is empty.");
  }

  if (faceVertexIndices.size() < 3) {
    return nonstd::make_unexpected(
        fmt::format("faceVertexIndices.size must be at least 3, but got {}.",
                    faceVertexIndices.size()));
  }

  const uint32_t num_vertices =
      *std::max_element(faceVertexIndices.cbegin(), faceVertexIndices.cend()) + 1;

  std::vector<uint8_t> dst;

  if (src.empty()) {
    return nonstd::make_unexpected("src data is empty.");
  }

  if (stride_bytes == 0) {
    return nonstd::make_unexpected("stride_bytes must be non-zero.");
  }

  if (src.size() != stride_bytes) {
    return nonstd::make_unexpected(
        fmt::format("src size {} must be equal to stride_bytes {}", src.size(),
                    stride_bytes));
  }

  dst.resize(stride_bytes * num_vertices);

  size_t faceVertexIndexOffset = 0;
  for (size_t i = 0; i < faceVertexCounts.size(); i++) {
    uint32_t cnt = faceVertexCounts[i];
    if (cnt < 3) {
      return nonstd::make_unexpected(fmt::format(
          "faeVertexCounts[{}] must be equal to or greater than 3, but got {}",
          i, cnt));
    }

    for (size_t k = 0; k < cnt; k++) {
      size_t fv_idx = k + faceVertexIndexOffset;

      if (fv_idx >= faceVertexIndices.size()) {
        return nonstd::make_unexpected(
            fmt::format("faeVertexIndex {} out-of-range at faceVertexCount[{}]",
                        fv_idx, i));
      }

      size_t v_idx = faceVertexIndices[fv_idx];

      if (v_idx >= num_vertices) {  // this should not happen. just in case.
        return nonstd::make_unexpected(fmt::format(
            "faeVertexIndices[{}] {} exceeds the number of vertices {}", fv_idx,
            v_idx, num_vertices));
      }

      memcpy(dst.data() + v_idx * stride_bytes, src.data(), stride_bytes);
    }

    faceVertexIndexOffset += cnt;
  }

  return dst;
}


// T = int
template <typename T>
bool TryConvertFacevaryingToVertexInt(
    const std::vector<T> &src, std::vector<T> *dst,
    const std::vector<uint32_t> &faceVertexIndices) {
  if (!dst) {
    return false;
  }

  if (src.size() != faceVertexIndices.size()) {
    return false;
  }

  // size must be at least 1 triangle(3 verts).
  if (faceVertexIndices.size() < 3) {
    return false;
  }

  // vidx, value
  std::unordered_map<uint32_t, T> vdata;

  uint32_t max_vidx = 0;
  for (size_t i = 0; i < faceVertexIndices.size(); i++) {
    uint32_t vidx = faceVertexIndices[i];
    max_vidx = (std::max)(vidx, max_vidx);

    if (vdata.count(vidx)) {
      if (!math::is_close(vdata[vidx], src[i])) {
        return false;
      }
    } else {
      vdata[vidx] = src[i];
    }
  }

  dst->resize(max_vidx + 1);
  memset(dst->data(), 0, (max_vidx + 1) * sizeof(T));

  for (const auto &v : vdata) {
    (*dst)[v.first] = v.second;
  }

  return true;
}

// T = float, double, float2, ...
template <typename T, typename EpsTy>
bool TryConvertFacevaryingToVertexFloat(
    const std::vector<T> &src, std::vector<T> *dst,
    const std::vector<uint32_t> &faceVertexIndices, const EpsTy eps) {
  DCOUT("TryConvertFacevaryingToVertexFloat");
  if (!dst) {
    return false;
  }

  if (src.size() != faceVertexIndices.size()) {
    DCOUT("size mismatch.");
    return false;
  }

  // size must be at least 1 triangle(3 verts).
  if (faceVertexIndices.size() < 3) {
    return false;
  }

  // vidx, value
  std::unordered_map<uint32_t, T> vdata;

  uint32_t max_vidx = 0;
  for (size_t i = 0; i < faceVertexIndices.size(); i++) {
    uint32_t vidx = faceVertexIndices[i];
    max_vidx = (std::max)(vidx, max_vidx);

    if (vdata.count(vidx)) {
      if (!math::is_close(vdata[vidx], src[i], eps)) {
        DCOUT("diff at faceVertexIndices[" << i << "]");
        return false;
      }
    } else {
      vdata[vidx] = src[i];
    }
  }

  dst->resize(max_vidx + 1);
  memset(dst->data(), 0, (max_vidx + 1) * sizeof(T));

  for (const auto &v : vdata) {
    (*dst)[v.first] = v.second;
  }

  return true;
}

// T = matrix type.
template <typename T>
bool TryConvertFacevaryingToVertexMat(
    const std::vector<T> &src, std::vector<T> *dst,
    const std::vector<uint32_t> &faceVertexIndices) {
  if (!dst) {
    return false;
  }

  if (src.size() != faceVertexIndices.size()) {
    return false;
  }

  // size must be at least 1 triangle(3 verts).
  if (faceVertexIndices.size() < 3) {
    return false;
  }

  // vidx, value
  std::unordered_map<uint32_t, T> vdata;

  uint32_t max_vidx = 0;
  for (size_t i = 0; i < faceVertexIndices.size(); i++) {
    uint32_t vidx = faceVertexIndices[i];
    max_vidx = (std::max)(vidx, max_vidx);

    if (vdata.count(vidx)) {
      if (!is_close(vdata[vidx], src[i])) {
        return false;
      }
    } else {
      vdata[vidx] = src[i];
    }
  }

  dst->assign(max_vidx + 1, T::identity());

  for (const auto &v : vdata) {
    (*dst)[v.first] = v.second;
  }

  return true;
}

///
/// Try to convert 'facevarying' vertex attribute to 'vertex' attribute.
/// Inspect each vertex value is the same(with given eps)
///
/// Current limitation:
/// - stride must be 0 or tightly packed.
/// - elementSize must be 1
///
/// @return true when 'facevarying' vertex attribute successfully converted to
/// 'vertex'
///
static bool TryConvertFacevaryingToVertex(
    const VertexAttribute &src, VertexAttribute *dst,
    const std::vector<uint32_t> &faceVertexIndices, std::string *err,
    const float eps) {
  DCOUT("TryConvertFacevaryingToVertex");
  if (!dst) {
    PUSH_ERROR_AND_RETURN("Output `dst` is nullptr.");
  }

  if (!src.is_facevarying()) {
    PUSH_ERROR_AND_RETURN("Input must be 'facevarying' attribute");
  }

  if (src.element_size() != 1) {
    PUSH_ERROR_AND_RETURN("Input's element_size must be 1.");
  }

  if ((src.stride != 0) && (src.stride_bytes() != src.format_size())) {
    PUSH_ERROR_AND_RETURN(
        "Input attribute must be tightly packed. stride_bytes = "
        << src.stride_bytes() << ", format_size = " << src.format_size());
  }

#define CONVERT_FUN_INT(__fmt, __ty)                                      \
  if (src.format == __fmt) {                                              \
    std::vector<__ty> vsrc;                                               \
    vsrc.resize(src.vertex_count());                                      \
    memcpy(vsrc.data(), src.get_data().data(), src.get_data().size());    \
    std::vector<__ty> vdst;                                               \
    bool ret = TryConvertFacevaryingToVertexInt<__ty>(vsrc, &vdst,        \
                                                      faceVertexIndices); \
    if (!ret) {                                                           \
      return false;                                                       \
    }                                                                     \
    dst->name = src.name;                                                 \
    dst->elementSize = 1;                                                 \
    dst->format = src.format;                                             \
    dst->variability = VertexVariability::Vertex;                         \
    dst->data.resize(vdst.size() * src.format_size());                    \
    memcpy(dst->data.data(), vdst.data(), dst->data.size());              \
    return true;                                                          \
  } else

#define CONVERT_FUN_FLOAT(__fmt, __ty, __epsty)                        \
  if (src.format == __fmt) {                                           \
    std::vector<__ty> vsrc;                                            \
    vsrc.resize(src.vertex_count());                                   \
    memcpy(vsrc.data(), src.get_data().data(), src.get_data().size()); \
    std::vector<__ty> vdst;                                            \
    bool ret = TryConvertFacevaryingToVertexFloat<__ty, __epsty>(      \
        vsrc, &vdst, faceVertexIndices, __epsty(eps));                 \
    if (!ret) {                                                        \
      return false;                                                    \
    }                                                                  \
    dst->name = src.name;                                                 \
    dst->elementSize = 1;                                              \
    dst->format = src.format;                                          \
    dst->variability = VertexVariability::Vertex;                      \
    dst->data.resize(vdst.size() * src.format_size());                 \
    memcpy(dst->data.data(), vdst.data(), dst->data.size());           \
    return true;                                                       \
  } else

#define CONVERT_FUN_MAT(__fmt, __ty)                                      \
  if (src.format == __fmt) {                                              \
    std::vector<__ty> vsrc;                                               \
    vsrc.resize(src.vertex_count());                                      \
    memcpy(vsrc.data(), src.get_data().data(), src.get_data().size());    \
    std::vector<__ty> vdst;                                               \
    bool ret = TryConvertFacevaryingToVertexMat<__ty>(vsrc, &vdst,        \
                                                      faceVertexIndices); \
    if (!ret) {                                                           \
      return false;                                                       \
    }                                                                     \
    dst->name = src.name;                                                 \
    dst->elementSize = 1;                                                 \
    dst->format = src.format;                                             \
    dst->variability = VertexVariability::Vertex;                         \
    dst->data.resize(vdst.size() * src.format_size());                    \
    memcpy(dst->data.data(), vdst.data(), dst->data.size());              \
    return true;                                                          \
  } else

  // NOTE: VertexAttributeFormat::Bool is preserved
  CONVERT_FUN_INT(VertexAttributeFormat::Bool, uint8_t)
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Float, float, float)
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Vec2, value::float2, float)
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Vec3, value::float3, float)
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Vec4, value::float4, float)
  CONVERT_FUN_INT(VertexAttributeFormat::Char, signed char)
  // CONVERT_FUN(VertexAttributeFormat::Char2, value::char2)
  // CONVERT_FUN(VertexAttributeFormat::Char3, value::char3)
  // CONVERT_FUN(VertexAttributeFormat::Char4,    // int8x4
  CONVERT_FUN_INT(VertexAttributeFormat::Byte, uint8_t)
  // CONVERT_FUN(VertexAttributeFormat::Byte2,    // uint8x2
  // CONVERT_FUN(VertexAttributeFormat::Byte3,    // uint8x3
  // CONVERT_FUN(VertexAttributeFormat::Byte4,    // uint8x4
  CONVERT_FUN_INT(VertexAttributeFormat::Short, int16_t)
  // CONVERT_FUN(VertexAttributeFormat::Short2, value::short2)
  // CONVERT_FUN(VertexAttributeFormat::Short3, value::short3)
  // CONVERT_FUN(VertexAttributeFormat::Short4, value::short4)
  CONVERT_FUN_INT(VertexAttributeFormat::Ushort, uint16_t)
  // CONVERT_FUN(VertexAttributeFormat::Ushort2, uint16_t)
  // CONVERT_FUN(VertexAttributeFormat::Ushort3, uint16_t)
  // CONVERT_FUN(VertexAttributeFormat::Ushort4, uint16_t)
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Half, value::half, float)
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Half2, value::half2, float)
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Half3, value::half3, float)
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Half4, value::half4, float)
  CONVERT_FUN_INT(VertexAttributeFormat::Int, int)
  CONVERT_FUN_INT(VertexAttributeFormat::Ivec2, value::int2)
  CONVERT_FUN_INT(VertexAttributeFormat::Ivec3, value::int3)
  CONVERT_FUN_INT(VertexAttributeFormat::Ivec4, value::int4)
  CONVERT_FUN_INT(VertexAttributeFormat::Uint, uint32_t)
  CONVERT_FUN_INT(VertexAttributeFormat::Uvec2, value::uint2)
  CONVERT_FUN_INT(VertexAttributeFormat::Uvec3, value::uint3)
  CONVERT_FUN_INT(VertexAttributeFormat::Uvec4, value::uint4)
  // NOTE: Use float precision eps is upcasted to double precision.
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Double, double, double)
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Dvec2, value::double2, double)
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Dvec3, value::double3, double)
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Dvec4, value::double4, double)
  CONVERT_FUN_MAT(VertexAttributeFormat::Mat2, value::matrix2f)
  CONVERT_FUN_MAT(VertexAttributeFormat::Mat3, value::matrix3f)
  CONVERT_FUN_MAT(VertexAttributeFormat::Mat4, value::matrix4f)
  CONVERT_FUN_MAT(VertexAttributeFormat::Dmat2, value::matrix2d)
  CONVERT_FUN_MAT(VertexAttributeFormat::Dmat3, value::matrix3d)
  CONVERT_FUN_MAT(VertexAttributeFormat::Dmat4, value::matrix4d) {
    if (err) {
      (*err) +=
          fmt::format("Unsupported/Unimplemented VertexAttributeFormat: {}",
                      to_string(src.format));
    }
  }

#undef CONVERT_FUN_INT
#undef CONVERT_FUN_FLOAT
#undef CONVERT_FUN_MAT

  return false;
}




///
/// Triangulate VeretexAttribute data.
///
static bool TriangulateVertexAttribute(
    VertexAttribute &vattr, const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<size_t> &triangulatedToOrigFaceVertexIndexMap,
    const std::vector<uint32_t> &triangulatedFaceCounts,
    const std::vector<uint32_t> &triangulatedFaceVertexIndices,
    std::string *err) {
  if (vattr.vertex_count() == 0) {
    return true;
  }

  if (triangulatedFaceCounts.empty()) {
    PUSH_ERROR_AND_RETURN("triangulatedFaceCounts is empty.");
  }

  if (faceVertexCounts.size() != triangulatedFaceCounts.size()) {
    PUSH_ERROR_AND_RETURN(
        "faceVertexCounts.size must be equal to triangulatedFaceCounts.size.");
  }

  if ((triangulatedFaceVertexIndices.size() % 3) != 0) {
    PUSH_ERROR_AND_RETURN("Invalid size for triangulatedFaceVertexIndices.");
  }

  if (vattr.is_facevarying()) {
    if (triangulatedToOrigFaceVertexIndexMap.size() !=
        triangulatedFaceVertexIndices.size()) {
      PUSH_ERROR_AND_RETURN(
          "triangulatedToOrigFaceVertexIndexMap.size must be equal to "
          "triangulatedFaceVertexIndices.");
    }

    size_t num_vs = vattr.vertex_count();
    const size_t stride = vattr.stride_bytes();
    const size_t total_size = triangulatedFaceVertexIndices.size() * stride;

    std::vector<uint8_t> buf;
    buf.resize(total_size);  // Pre-allocate exact size

    const uint8_t* src_data = vattr.get_data().data();
    uint8_t* dst_ptr = buf.data();

    for (uint32_t f = 0; f < triangulatedFaceVertexIndices.size(); f++) {
      // Array index to faceVertexIndices(before triangulation).
      size_t src_fvIdx = triangulatedToOrigFaceVertexIndexMap[f];

      if (src_fvIdx >= num_vs) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("triangulatedToOrigFaceVertexIndexMap[{}] {} exceeds num_vs {}.", f, src_fvIdx, num_vs));
      }

      // Use memcpy instead of insert for better performance
      std::memcpy(dst_ptr, src_data + src_fvIdx * stride, stride);
      dst_ptr += stride;
    }

    vattr.data = std::move(buf);
  } else if (vattr.is_vertex()) {
    // # of vertices does not change, so nothing is required.
    return true;
  } else if (vattr.is_indexed()) {
    PUSH_ERROR_AND_RETURN("Indexed VertexAttribute is not supported.");
  } else if (vattr.is_constant()) {
    const size_t stride = vattr.stride_bytes();

    // Pre-calculate total size to avoid reallocations
    size_t total_triangles = 0;
    for (size_t f = 0; f < triangulatedFaceCounts.size(); f++) {
      total_triangles += triangulatedFaceCounts[f];
    }
    // Each triangle has 3 vertices
    const size_t total_size = total_triangles * 3 * stride;

    std::vector<uint8_t> buf;
    buf.resize(total_size);

    const uint8_t* src_data = vattr.get_data().data();
    uint8_t* dst_ptr = buf.data();

    for (size_t f = 0; f < triangulatedFaceCounts.size(); f++) {
      uint32_t nf = triangulatedFaceCounts[f];
      const uint8_t* face_data = src_data + f * stride;

      // copy `nf` triangles (each with 3 vertices)
      for (size_t k = 0; k < nf * 3; k++) {
        std::memcpy(dst_ptr, face_data, stride);
        dst_ptr += stride;
      }
    }

    vattr.data = std::move(buf);
  } else if (vattr.is_uniform()) {
    // nothing is required
    return true;
  }

  return true;
}

std::vector<const tinyusdz::GeomSubset *> GetMaterialBindGeomSubsets(
    const tinyusdz::Prim &prim) {
  std::vector<const tinyusdz::GeomSubset *> dst;

  // GeomSubet Prim must be a child Prim of GeomMesh.
  for (const auto &child : prim.children()) {
    if (const tinyusdz::GeomSubset *psubset =
            child.as<tinyusdz::GeomSubset>()) {
      value::token tok;
      if (!psubset->familyName.get_value(&tok)) {
        continue;
      }

      if (tok.str() != "materialBind") {
        continue;
      }

      dst.push_back(psubset);
    }
  }

  return dst;
}

//
// name does not include "primvars:" prefix.
//
nonstd::expected<VertexAttribute, std::string> GetTextureCoordinate(
    const Stage &stage, const GeomMesh &mesh, const std::string &name,
    const double t, const value::TimeSampleInterpolationType tinterp) {
  VertexAttribute vattr;

  (void)stage;

  // HACK
  //return nonstd::make_unexpected("Disabled");

  std::string err;
  GeomPrimvar primvar;
  if (!GetGeomPrimvar(stage, &mesh, name, &primvar, &err)) {
    return nonstd::make_unexpected(err);
  }

  if (!primvar.has_value()) {
    return nonstd::make_unexpected("No value exist for primvars:" + name +
                                   "\n");
  }

  //TUSDZ_LOG_I("get tex\n");
  // TODO: allow float2?
  if (primvar.get_type_id() !=
      value::TypeTraits<std::vector<value::texcoord2f>>::type_id()) {
    return nonstd::make_unexpected(
        "Texture coordinate primvar must be texCoord2f[] type, but got " +
        primvar.get_type_name() + "\n");
  }

  //TUSDZ_LOG_I("flatten_with_indices\n");
  std::vector<value::texcoord2f> uvs;
  if (!primvar.flatten_with_indices(t, &uvs, tinterp)) {
    //TUSDZ_LOG_I("flatten_with_indices failed\n");
    return nonstd::make_unexpected(
        "Failed to retrieve texture coordinate primvar with concrete type.\n");
  }

  if (primvar.get_interpolation() == Interpolation::Varying) {
    vattr.variability = VertexVariability::Varying;
  } else if (primvar.get_interpolation() == Interpolation::Constant) {
    vattr.variability = VertexVariability::Constant;
  } else if (primvar.get_interpolation() == Interpolation::Uniform) {
    vattr.variability = VertexVariability::Uniform;
  } else if (primvar.get_interpolation() == Interpolation::Vertex) {
    vattr.variability = VertexVariability::Vertex;
  } else if (primvar.get_interpolation() == Interpolation::FaceVarying) {
    vattr.variability = VertexVariability::FaceVarying;
  }


  //TUSDZ_LOG_I("texcoord. " << name << ", " << uvs.size());
  DCOUT("texcoord " << name << " : " << uvs);

  vattr.format = VertexAttributeFormat::Vec2;
  vattr.data.resize(uvs.size() * sizeof(value::texcoord2f));
  memcpy(vattr.data.data(), uvs.data(), vattr.data.size());
  vattr.indices.clear();  // just in case.

  vattr.name = name;  // TODO: add "primvars:" namespace?
  //TUSDZ_LOG_I("end");

  return std::move(vattr);
}


namespace {

template <typename UnderlyingTy>
bool ScalarValueToVertexAttribute(const value::Value &value,
                                  const std::string &name,
                                  const VertexAttributeFormat format,
                                  VertexAttribute &dst, std::string *err) {
  if (VertexAttributeFormatSize(format) != sizeof(UnderlyingTy)) {
    PUSH_ERROR_AND_RETURN("format size mismatch.");
    return false;
  }

  if (auto pv = value.as<UnderlyingTy>()) {
    dst.data.resize(sizeof(UnderlyingTy));
    memcpy(dst.data.data(), pv, sizeof(UnderlyingTy));

    dst.elementSize = 1;
    dst.stride = 0;
    dst.format = format;
    dst.variability = VertexVariability::Constant;
    dst.name = name;
    dst.indices.clear();
    return true;
  }

  PUSH_ERROR_AND_RETURN("[Internal error] value is not scalar-typed value.");
}

template <typename UnderlyingTy>
bool ArrayValueToVertexAttribute(
    const value::Value &value, const std::string &name,
    const uint32_t elementSize, const VertexVariability variability,
    const uint32_t num_vertices, const uint32_t num_face_counts,
    const uint32_t num_face_vertex_indices, const VertexAttributeFormat format,
    VertexAttribute &dst, std::string *err) {
  if (!value::TypeTraits<UnderlyingTy>::is_array()) {
    PUSH_ERROR_AND_RETURN(
        "[Internal error] UnderlyingTy template parameter must be array type.");
  }

  size_t baseTySize = value::TypeTraits<UnderlyingTy>::size();

  size_t value_counts = value.array_size();
  if (value_counts == 0) {
    PUSH_ERROR_AND_RETURN("Empty array size");
  }

  if (variability == VertexVariability::Indexed) {
    PUSH_ERROR_AND_RETURN("Indexed variability is not supported.");
  }

  if (VertexAttributeFormatSize(format) != baseTySize) {
    PUSH_ERROR_AND_RETURN("format size mismatch. expected "
                          << VertexAttributeFormatSize(format) << " but got "
                          << baseTySize);
    return false;
  }

  DCOUT("value.type = " << value.type_name());
  DCOUT("UnderlyingTy = " << value::TypeTraits<UnderlyingTy>::type_name());
  const auto p = value.as<UnderlyingTy>();
  if (!p) {
    DCOUT("p is nullptr");
  }

  if (auto pv = value.as<UnderlyingTy>()) {
    switch (variability) {
    case VertexVariability::Constant: {
      if (value_counts != elementSize) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "{} # of items {} expected, but got {}. Variability = Constant",
            name, elementSize, value_counts));
      }
      break;
    }
    case VertexVariability::Uniform: {
      if (value_counts != (elementSize * num_face_counts)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "{} # of items {} expected, but got {}. Variability = Uniform",
            name, elementSize * num_face_counts, value_counts));
      }
      break;
    }
    case VertexVariability::Vertex: {
      if (value_counts != (elementSize * num_vertices)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "{} # of items {} expected, but got {}. Variability = Vertex",
            name, elementSize * num_vertices, value_counts));
      }
      break;
    case VertexVariability::Varying: {
      if (value_counts != (elementSize * num_vertices)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "{} # of items {} expected, but got {}. Variability = Varying",
            name, elementSize * num_vertices, value_counts));
      }
      break;
    }
    case VertexVariability::FaceVarying: {
      if (value_counts != (elementSize * num_face_vertex_indices)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "# of items {} expected, but got {}. Variability = FaceVarying",
            elementSize * num_face_vertex_indices, value_counts));
      }
      break;
    }
    case VertexVariability::Indexed: {
      PUSH_ERROR_AND_RETURN(fmt::format(
            "{} Internal error. 'Indexed' variability is not supported."));
      }
      break;
    }
    }

    dst.data.resize(value_counts * baseTySize);
    memcpy(dst.data.data(), pv->data(), value_counts * baseTySize);

    dst.elementSize = elementSize;
    dst.stride = 0;
    dst.format = format;
    dst.variability = variability;
    dst.name = name;
    dst.indices.clear();
    return true;
  }

  PUSH_ERROR_AND_RETURN(fmt::format(
      "Requested underlying type {} but input `value` has underlying type {}.",
      value::TypeTraits<UnderlyingTy>::type_name(),
      value.underlying_type_name()));
}

#if defined(TINYUSDZ_WITH_MESHOPT)
//
// Optimize RenderMesh indices using meshoptimizer
//
[[maybe_unused]] static void OptimizeRenderMeshIndices(RenderMesh& mesh) {
  // Only optimize triangulated meshes with valid indices
  if (!mesh.is_triangulated() || mesh.triangulatedFaceVertexIndices.empty() || mesh.points.empty()) {
    return;
  }

  const size_t index_count = mesh.triangulatedFaceVertexIndices.size();
  const size_t vertex_count = mesh.points.size();

  if (index_count == 0 || vertex_count == 0) {
    return;
  }

  // Create optimized index buffer
  std::vector<unsigned int> optimized_indices(index_count);

  // Convert indices to unsigned int for meshoptimizer
  std::vector<unsigned int> indices(index_count);
  for (size_t i = 0; i < index_count; i++) {
    indices[i] = static_cast<unsigned int>(mesh.triangulatedFaceVertexIndices[i]);
  }

  // Step 1: Optimize vertex cache
  meshopt_optimizeVertexCache(optimized_indices.data(), indices.data(),
                              index_count, vertex_count);

  // Step 2: Optimize overdraw (requires vertex positions)
  if (!mesh.points.empty()) {
    std::vector<unsigned int> overdraw_optimized(index_count);
    meshopt_optimizeOverdraw(overdraw_optimized.data(), optimized_indices.data(),
                             index_count,
                             reinterpret_cast<const float*>(mesh.points.data()),
                             vertex_count,
                             sizeof(vec3), // stride
                             1.05f); // threshold (allow up to 5% vertex cache degradation)

    optimized_indices = std::move(overdraw_optimized);
  }

  // Step 3: Optimize vertex fetch
  std::vector<unsigned int> fetch_remap(vertex_count);
  size_t unique_vertices = meshopt_optimizeVertexFetchRemap(fetch_remap.data(),
                                                            optimized_indices.data(),
                                                            index_count,
                                                            vertex_count);

  // Only apply vertex fetch optimization if it reduces vertex count
  if (unique_vertices < vertex_count && unique_vertices > 0) {
    // Remap indices
    meshopt_remapIndexBuffer(optimized_indices.data(), optimized_indices.data(),
                             index_count, fetch_remap.data());

    // Remap vertex positions
    std::vector<vec3> optimized_points(unique_vertices);
    meshopt_remapVertexBuffer(optimized_points.data(), mesh.points.data(),
                              vertex_count, sizeof(vec3), fetch_remap.data());

    mesh.points = std::move(optimized_points);

    // TODO: Remap other vertex attributes (normals, texcoords, etc.) as needed
    // This would require more complex logic to handle all vertex attributes
  }

  // Convert back to uint32_t and update mesh
  for (size_t i = 0; i < index_count; i++) {
    mesh.triangulatedFaceVertexIndices[i] = static_cast<uint32_t>(optimized_indices[i]);
  }
}
#endif

}  // namespace

bool ToVertexAttribute(const GeomPrimvar &primvar, const std::string &name,
                       const uint32_t num_vertices,
                       const uint32_t num_face_counts,
                       const uint32_t num_face_vertex_indices,
                       VertexAttribute &dst, std::string *err, const double t,
                       const value::TimeSampleInterpolationType tinterp,
                       std::string *warn = nullptr) {
  uint32_t elementSize = uint32_t(primvar.get_elementSize());
  if (elementSize == 0) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("elementSize is zero for primvar: {}", primvar.name()));
  }

  VertexAttribute vattr;

  const tinyusdz::Attribute &attr = primvar.get_attribute();

  // Check if primvar has timesamples and report detailed warning
  if (attr.has_timesamples()) {
    std::string msg = fmt::format(
        "Geometry primvar '{}' has timesamples (animated values). "
        "RenderMesh conversion uses value at specified time (timecode={}). "
        "To capture animation, you need to convert at multiple timesamples. "
        "Consider using ConvertMesh() at each timeframe or implementing "
        "per-frame conversion. This is particularly important for vertex "
        "attributes like normals, tangents, texture coordinates, colors, "
        "and opacities that may change over time.",
        name, t);
    if (warn) {
      (*warn) += msg + "\n";
    }
    DCOUT("WARN: " << msg);
  }

  value::Value value;
  if (!primvar.flatten_with_indices(t, &value, tinterp)) {
    PUSH_ERROR_AND_RETURN("Failed to flatten primvar");
  }

  bool is_array = value.type_id() & value::TYPE_ID_1D_ARRAY_BIT;
  DCOUT("is_array " << (is_array ? "true" : "false"));

  VertexVariability variability;
  if (primvar.get_interpolation() == Interpolation::Varying) {
    variability = VertexVariability::Varying;
  } else if (primvar.get_interpolation() == Interpolation::Constant) {
    variability = VertexVariability::Constant;
  } else if (primvar.get_interpolation() == Interpolation::Uniform) {
    variability = VertexVariability::Uniform;
  } else if (primvar.get_interpolation() == Interpolation::Vertex) {
    variability = VertexVariability::Vertex;
  } else if (primvar.get_interpolation() == Interpolation::FaceVarying) {
    variability = VertexVariability::FaceVarying;
  } else {
    PUSH_ERROR_AND_RETURN("[Internal Error] Invalid `interpolation` type.");
  }

  uint32_t baseUnderlyingTypeId =
      value.underlying_type_id() & (~value::TYPE_ID_1D_ARRAY_BIT);
  DCOUT("flattened primvar type: " << value.type_name() << ", underlying type "
                                   << value::GetTypeName(baseUnderlyingTypeId));

  // Cast to underlying type

#define TO_TYPED_VALUE(__underlying_ty, __vfmt)                                \
  if (baseUnderlyingTypeId == value::TypeTraits<__underlying_ty>::type_id()) { \
    if (is_array) {                                                            \
      return ArrayValueToVertexAttribute<std::vector<__underlying_ty>>(        \
          value, name, elementSize, variability, num_vertices,                 \
          num_face_counts, num_face_vertex_indices, __vfmt, dst, err);         \
    } else {                                                                   \
      return ScalarValueToVertexAttribute<__underlying_ty>(value, name,        \
                                                           __vfmt, dst, err);  \
    }                                                                          \
  } else

  // specialization for bool type: bool is represented as uint8 in USD primvar
  if (baseUnderlyingTypeId == value::TypeTraits<bool>::type_id()) {
    if (is_array) {
      return ArrayValueToVertexAttribute<std::vector<uint8_t>>(
          value, name, elementSize, variability, num_vertices, num_face_counts,
          num_face_vertex_indices, VertexAttributeFormat::Bool, dst, err);
    } else {
      return ScalarValueToVertexAttribute<uint8_t>(
          value, name, VertexAttributeFormat::Bool, dst, err);
    }
  } else
    TO_TYPED_VALUE(uint8_t, VertexAttributeFormat::Byte)
  TO_TYPED_VALUE(value::uchar2, VertexAttributeFormat::Byte2)
  TO_TYPED_VALUE(value::uchar3, VertexAttributeFormat::Byte3)
  TO_TYPED_VALUE(value::uchar4, VertexAttributeFormat::Byte4)
  TO_TYPED_VALUE(char, VertexAttributeFormat::Char)
  TO_TYPED_VALUE(value::char2, VertexAttributeFormat::Char2)
  TO_TYPED_VALUE(value::char3, VertexAttributeFormat::Char3)
  TO_TYPED_VALUE(value::char4, VertexAttributeFormat::Char4)
  TO_TYPED_VALUE(short, VertexAttributeFormat::Short)
  TO_TYPED_VALUE(value::short2, VertexAttributeFormat::Short2)
  TO_TYPED_VALUE(value::short3, VertexAttributeFormat::Short3)
  TO_TYPED_VALUE(value::short4, VertexAttributeFormat::Short4)
  TO_TYPED_VALUE(uint16_t, VertexAttributeFormat::Ushort)
  TO_TYPED_VALUE(value::ushort2, VertexAttributeFormat::Ushort2)
  TO_TYPED_VALUE(value::ushort3, VertexAttributeFormat::Ushort3)
  TO_TYPED_VALUE(value::ushort4, VertexAttributeFormat::Ushort4)
  TO_TYPED_VALUE(int, VertexAttributeFormat::Int)
  TO_TYPED_VALUE(value::int2, VertexAttributeFormat::Ivec2)
  TO_TYPED_VALUE(value::int3, VertexAttributeFormat::Ivec3)
  TO_TYPED_VALUE(value::int4, VertexAttributeFormat::Ivec4)
  TO_TYPED_VALUE(uint32_t, VertexAttributeFormat::Uint)
  TO_TYPED_VALUE(value::uint2, VertexAttributeFormat::Uvec2)
  TO_TYPED_VALUE(value::uint3, VertexAttributeFormat::Uvec3)
  TO_TYPED_VALUE(value::uint4, VertexAttributeFormat::Uvec4)
  TO_TYPED_VALUE(float, VertexAttributeFormat::Float)
  TO_TYPED_VALUE(value::float2, VertexAttributeFormat::Vec2)
  TO_TYPED_VALUE(value::float3, VertexAttributeFormat::Vec3)
  TO_TYPED_VALUE(value::float4, VertexAttributeFormat::Vec4)
  TO_TYPED_VALUE(value::half, VertexAttributeFormat::Half)
  TO_TYPED_VALUE(value::half2, VertexAttributeFormat::Half2)
  TO_TYPED_VALUE(value::half3, VertexAttributeFormat::Half3)
  TO_TYPED_VALUE(value::half4, VertexAttributeFormat::Half4)
  TO_TYPED_VALUE(double, VertexAttributeFormat::Double)
  TO_TYPED_VALUE(value::double2, VertexAttributeFormat::Dvec2)
  TO_TYPED_VALUE(value::double3, VertexAttributeFormat::Dvec3)
  TO_TYPED_VALUE(value::double4, VertexAttributeFormat::Dvec4)
  TO_TYPED_VALUE(value::matrix2f, VertexAttributeFormat::Mat2)
  TO_TYPED_VALUE(value::matrix3f, VertexAttributeFormat::Mat3)
  TO_TYPED_VALUE(value::matrix4f, VertexAttributeFormat::Mat4)
  TO_TYPED_VALUE(value::matrix2d, VertexAttributeFormat::Dmat2)
  TO_TYPED_VALUE(value::matrix3d, VertexAttributeFormat::Dmat3)
  TO_TYPED_VALUE(value::matrix4d, VertexAttributeFormat::Dmat4) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Unknown or unsupported data type for Geom PrimVar: {}",
                    attr.type_name()));
  }

#undef TO_TYPED_VALUE
}


#if 1
///
/// Input: points, faceVertexCounts, faceVertexIndices
/// Output: triangulated faceVertexCounts(all filled with 3), triangulated
/// faceVertexIndices, triangulatedToOrigFaceVertexIndexMap (length =
/// triangulated faceVertexIndices. triangulatedToOrigFaceVertexIndexMap[i]
/// stores an array index to original faceVertexIndices. For remapping
/// facevarying primvar attributes.)
///
/// triangulatedFaceVertexCounts: len = len(faceVertexCounts). Records the
/// number of triangle faces. 1 = triangle. 2 = quad, ... For remapping face
/// indices(e.g. GeomSubset::indices)
///
/// triangulated*** output is generated even when input mesh is fully composed
/// from triangles(`faceVertexCounts` are all filled with 3) Return false when a
/// polygon is degenerated. No overlap check at the moment
///
/// Example:
///   - faceVertexCounts = [4]
///   - faceVertexIndices = [0, 1, 3, 2]
///
///   - triangulatedFaceVertexCounts = [3, 3]
///   - triangulatedFaceVertexIndices = [0, 1, 3, 0, 3, 2]
///   - triangulatedToOrigFaceVertexIndexMap = [0, 1, 2, 0, 2, 3]
///
/// T = value::float3 or value::double3
/// BaseTy = float or double
template <typename T, typename BaseTy>
bool TriangulatePolygon(
    const std::vector<T> &points, const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices,
    std::vector<uint32_t> &triangulatedFaceVertexCounts,
    std::vector<uint32_t> &triangulatedFaceVertexIndices,
    std::vector<size_t> &triangulatedToOrigFaceVertexIndexMap,
    std::vector<uint32_t> &triangulatedFaceCounts,
    MeshConverterConfig::TriangulationMethod triangulation_method,
    std::string &warn, std::string &err) {
  triangulatedFaceVertexCounts.clear();
  triangulatedFaceVertexIndices.clear();
  triangulatedToOrigFaceVertexIndexMap.clear();
  triangulatedFaceCounts.clear();

  // Pre-allocate based on estimated triangle count.
  // For each face with N vertices, we generate N-2 triangles, each with 3 indices.
  // Total triangles ≈ total_vertex_indices - 2*num_faces
  // This is an upper bound estimate.
  size_t numFaces = faceVertexCounts.size();
  size_t numFaceVertexIndices = faceVertexIndices.size();
  size_t estimatedTriangles = numFaceVertexIndices > 2 * numFaces
                             ? numFaceVertexIndices - 2 * numFaces
                             : numFaces;
  size_t estimatedIndices = estimatedTriangles * 3;

  triangulatedFaceVertexCounts.reserve(estimatedTriangles);
  triangulatedFaceVertexIndices.reserve(estimatedIndices);
  triangulatedToOrigFaceVertexIndexMap.reserve(estimatedIndices);
  triangulatedFaceCounts.reserve(numFaces);

  size_t faceIndexOffset = 0;

  // For each polygon(face)
  for (size_t i = 0; i < faceVertexCounts.size(); i++) {
    uint32_t npolys = faceVertexCounts[i];

    if (npolys < 3) {
      err = fmt::format(
          "faceVertex count must be 3(triangle) or "
          "more(polygon), but got faceVertexCounts[{}] = {}\n",
          i, npolys);
      return false;
    }

    if (faceIndexOffset + npolys > faceVertexIndices.size()) {
      err = fmt::format(
          "Invalid faceVertexIndices or faceVertexCounts. faceVertex index "
          "exceeds faceVertexIndices.size() at [{}]\n",
          i);
      return false;
    }

    if (npolys == 3) {
      // No need for triangulation.
      triangulatedFaceVertexCounts.push_back(3);
      triangulatedFaceVertexIndices.push_back(
          faceVertexIndices[faceIndexOffset + 0]);
      triangulatedFaceVertexIndices.push_back(
          faceVertexIndices[faceIndexOffset + 1]);
      triangulatedFaceVertexIndices.push_back(
          faceVertexIndices[faceIndexOffset + 2]);
      triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset + 0);
      triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset + 1);
      triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset + 2);
      triangulatedFaceCounts.push_back(1);
#if 1
    } else if (npolys == 4) {
      // Use simple split
      // TODO: Split at shortest edge for better triangulation.
      triangulatedFaceVertexCounts.push_back(3);
      triangulatedFaceVertexCounts.push_back(3);

      triangulatedFaceVertexIndices.push_back(
          faceVertexIndices[faceIndexOffset + 0]);
      triangulatedFaceVertexIndices.push_back(
          faceVertexIndices[faceIndexOffset + 1]);
      triangulatedFaceVertexIndices.push_back(
          faceVertexIndices[faceIndexOffset + 2]);

      triangulatedFaceVertexIndices.push_back(
          faceVertexIndices[faceIndexOffset + 0]);
      triangulatedFaceVertexIndices.push_back(
          faceVertexIndices[faceIndexOffset + 2]);
      triangulatedFaceVertexIndices.push_back(
          faceVertexIndices[faceIndexOffset + 3]);

      triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset + 0);
      triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset + 1);
      triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset + 2);
      triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset + 0);
      triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset + 2);
      triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset + 3);
      triangulatedFaceCounts.push_back(2);
#endif
    } else {
      // Polygon with 5+ vertices
      if (triangulation_method == MeshConverterConfig::TriangulationMethod::TriangleFan) {
        // Simple triangle fan triangulation
        // This assumes the polygon is convex
        // Creates triangles: (0,1,2), (0,2,3), (0,3,4), ...

        size_t ntris = npolys - 2;

        for (size_t k = 0; k < ntris; k++) {
          triangulatedFaceVertexCounts.push_back(3);

          // First vertex is always the pivot (index 0)
          triangulatedFaceVertexIndices.push_back(
              faceVertexIndices[faceIndexOffset + 0]);
          triangulatedFaceVertexIndices.push_back(
              faceVertexIndices[faceIndexOffset + k + 1]);
          triangulatedFaceVertexIndices.push_back(
              faceVertexIndices[faceIndexOffset + k + 2]);

          triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset + 0);
          triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset + k + 1);
          triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset + k + 2);
        }

        triangulatedFaceCounts.push_back(uint32_t(ntris));

      } else {
        // Use earcut algorithm (default, handles complex polygons)
        // Use double for accuracy. `float` precision may classify small-are polygon as degenerated.
        // Find the normal axis of the polygon using Newell's method
        value::double3 n = {0, 0, 0};

        size_t vi0;
        size_t vi0_2;

        //std::cout << "npoly " << npolys << "\n";

        for (size_t k = 0; k < npolys; ++k) {
          vi0 = faceVertexIndices[faceIndexOffset + k];

          size_t j = (k + 1) % npolys;
          vi0_2 = faceVertexIndices[faceIndexOffset + j];

          if (vi0 >= points.size()) {
            err = fmt::format("Invalid vertex index.\n");
            return false;
          }

          if (vi0_2 >= points.size()) {
            err = fmt::format("Invalid vertex index.\n");
            return false;
          }

          T v0 = points[vi0];
          T v1 = points[vi0_2];

          const T point1 = {v0[0], v0[1], v0[2]};
          const T point2 = {v1[0], v1[1], v1[2]};

          T a = {point1[0] - point2[0], point1[1] - point2[1],
                 point1[2] - point2[2]};
          T b = {point1[0] + point2[0], point1[1] + point2[1],
                 point1[2] + point2[2]};

          n[0] += double(a[1] * b[2]);
          n[1] += double(a[2] * b[0]);
          n[2] += double(a[0] * b[1]);
          DCOUT("v0 " << v0);
          DCOUT("v1 " << v1);
          DCOUT("n " << n);
        }
        //BaseTy length_n = vlength(n);
        double length_n = vlength(n);

        // Check if zero length normal
        if (std::fabs(length_n) < std::numeric_limits<double>::epsilon()) {
          DCOUT("length_n " << length_n);
          err = "Degenerated polygon found.\n";
          return false;
        }

        // Negative is to flip the normal to the correct direction
        n = vnormalize(n);

        T axis_w, axis_v, axis_u;
        axis_w[0] = BaseTy(n[0]);
        axis_w[1] = BaseTy(n[1]);
        axis_w[2] = BaseTy(n[2]);
        T a;
        if (std::fabs(axis_w[0]) > BaseTy(0.9999999)) {  // TODO: use 1.0 - eps?
          a = {BaseTy(0), BaseTy(1), BaseTy(0)};
        } else {
          a = {BaseTy(1), BaseTy(0), BaseTy(0)};
        }
        axis_v = vnormalize(vcross(axis_w, a));
        axis_u = vcross(axis_w, axis_v);

        using Point3D = std::array<BaseTy, 3>;
        using Point2D = std::array<BaseTy, 2>;
        std::vector<Point2D> polyline;

        // TMW change: Find best normal and project v0x and v0y to those
        // coordinates, instead of picking a plane aligned with an axis (which
        // can flip polygons).

        // Fill polygon data.
        for (size_t k = 0; k < npolys; k++) {
          size_t vidx = faceVertexIndices[faceIndexOffset + k];

          value::float3 v = points[vidx];
          // Point3 polypoint = {v0[0],v0[1],v0[2]};

          // world to local
          Point3D loc = {vdot(v, axis_u), vdot(v, axis_v), vdot(v, axis_w)};

          polyline.push_back({loc[0], loc[1]});
        }

        std::vector<std::vector<Point2D>> polygon_2d;
        polygon_2d.push_back(polyline);
        // Single polygon only(no holes)

        std::vector<uint32_t> indices = mapbox::earcut<uint32_t>(polygon_2d);
        //  => result = 3 * faces, clockwise

        if (indices.empty()) {
          warn += "Failed to triangualte a polygon. input is not CCW, have holes or invalid topology.\n";

          //DumpTriangle(points, indices);
        }

        if ((indices.size() % 3) != 0) {
          // This should not be happen, though.
          err = "Failed to triangulate.\n";
          return false;
        }

        size_t ntris = indices.size() / 3;
        //std::cout << "ntris " << ntris << "\n";


        // Up to 2GB tris.
        if (ntris > size_t((std::numeric_limits<int32_t>::max)())) {
          err = "Too many triangles are generated.\n";
          return false;
        }

        if (ntris > 0) {
          for (size_t k = 0; k < ntris; k++) {
            triangulatedFaceVertexCounts.push_back(3);
            // earcut returns clockwise triangles, but USD expects CCW
            // so we reverse the winding order by swapping indices 1 and 2
            triangulatedFaceVertexIndices.push_back(
                faceVertexIndices[faceIndexOffset + indices[3 * k + 0]]);
            triangulatedFaceVertexIndices.push_back(
                faceVertexIndices[faceIndexOffset + indices[3 * k + 2]]);
            triangulatedFaceVertexIndices.push_back(
                faceVertexIndices[faceIndexOffset + indices[3 * k + 1]]);

            triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset +
                                                           indices[3 * k + 0]);
            triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset +
                                                           indices[3 * k + 2]);
            triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset +
                                                           indices[3 * k + 1]);
          }
          triangulatedFaceCounts.push_back(uint32_t(ntris));
        }
      }
    }

    faceIndexOffset += npolys;
  }

  return true;
}
#endif


struct ComputeTangentPackedVertexData {
  // value::float3 position;
  uint32_t point_index;
  value::float3 normal;
  value::float2 uv;

  // comparator for std::map
  bool operator<(const DefaultPackedVertexData &rhs) const {
    return memcmp(reinterpret_cast<const void *>(this),
                  reinterpret_cast<const void *>(&rhs),
                  sizeof(DefaultPackedVertexData)) > 0;
  }
};

struct ComputeTangentPackedVertexDataHasher {
  inline size_t operator()(const ComputeTangentPackedVertexData &v) const {
    // Simple hasher using FNV1 32bit
    // TODO: Use 64bit FNV1?
    // TODO: Use spatial hash or LSH(LocallySensitiveHash) for position value.
    static constexpr uint32_t kFNV_Prime = 0x01000193;
    static constexpr uint32_t kFNV_Offset_Basis = 0x811c9dc5;

    const uint8_t *ptr = reinterpret_cast<const uint8_t *>(&v);
    size_t n = sizeof(DefaultPackedVertexData);

    uint32_t hash = kFNV_Offset_Basis;
    for (size_t i = 0; i < n; i++) {
      hash = (kFNV_Prime * hash) ^ (ptr[i]);
    }

    return size_t(hash);
  }
};

struct ComputeTangentPackedVertexDataEqual {
  bool operator()(const ComputeTangentPackedVertexData &lhs,
                  const ComputeTangentPackedVertexData &rhs) const {
    return memcmp(reinterpret_cast<const void *>(&lhs),
                  reinterpret_cast<const void *>(&rhs),
                  sizeof(ComputeTangentPackedVertexData)) == 0;
  }
};

template <class PackedVert>
struct ComputeTangentVertexInput {
  // std::vector<value::float3> positions;
  std::vector<uint32_t> point_indices;
  std::vector<value::float3> normals;
  std::vector<value::float2> uvs;

  size_t size() const { return point_indices.size(); }

  void get(size_t idx, PackedVert &output) const {
    if (idx < point_indices.size()) {
      output.point_index = point_indices[idx];
    } else {
      output.point_index = ~0u;  // never should reach here though.
    }
    if (idx < normals.size()) {
      output.normal = normals[idx];
    } else {
      output.normal = {0.0f, 0.0f, 0.0f};
    }
    if (idx < uvs.size()) {
      output.uv = uvs[idx];
    } else {
      output.uv = {0.0f, 0.0f};
    }
  }
};

template <class PackedVert>
struct ComputeTangentVertexOutput {
  // std::vector<value::float3> positions;
  std::vector<uint32_t> point_indices;
  std::vector<value::float3> normals;
  std::vector<value::float2> uvs;

  size_t size() const { return point_indices.size(); }

  void push_back(const PackedVert &v) {
    // positions.push_back(v.position);
    point_indices.push_back(v.point_index);
    normals.push_back(v.normal);
    uvs.push_back(v.uv);
  }
};

///
/// Compute facevarying tangent and facevarying binormal.
///
/// Reference:
/// http://www.opengl-tutorial.org/intermediate-tutorials/tutorial-13-normal-mapping
///
/// Implemented code uses two adjacent edge composed from three vertices v_{i},
/// v_{i+1}, v_{i+2} for i < (N - 1) , where N is the number of vertices per
/// facet.
///
/// This may produce unwanted tangent/binormal frame for ill-defined
/// polygon(quad, pentagon, ...). Also, we assume input mesh has well-formed and
/// has no or few vertices with similar property(position, uvs and normals)
///
/// TODO:
/// - [ ] Implement better getSimilarVertexIndex in the above opengl-tutorial to
/// better average tangent/binormal.
///   - Use kNN search(e.g. nanoflann https://github.com/jlblancoc/nanoflann ),
///   or point-query by building BVH over the mesh points.
///     - BVH builder candidate:
///       - NanoRT https://github.com/lighttransport/nanort
///       - bvh https://github.com/madmann91/bvh
///   - Or we can quantize vertex attributes and compute locally sensitive
///   hashsing? https://dl.acm.org/doi/10.1145/3188745.3188846
/// - [ ] Support robusut computing tangent/binormal on arbitrary mesh.
///  - e.g. vector field calculation, use instance-mesh algorithm, etc...
//   - Use half-edges to find adjacent face/vertex.
///
///
/// @param[in] vertices Vertex points(`vertex` variability).
/// @param[in] faceVertexCounts faceVertexCounts of the mesh.
/// @param[in] faceVertexIndices faceVertexIndices of the mesh.
/// @param[in] texcoords Primary texcoords.
/// @param[in] normals normals.
/// @param[in] is_facevarying_input false = texcoords and normals are 'vertex'
/// variability. true = 'facevarying' variability.
/// @param[out] tangents Computed tangents;
/// @param[out] binormals Computed binormals;
/// @param[out] out_vertex_indices Vertex indices.
/// @param[out] err Error message.
///
#ifdef TYDRA_ROBUST_TANGENT
/// Wrapper function to use robust tangent computation
static bool ComputeTangentsAndBinormalsRobust(
    const std::vector<vec3> &vertices,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices,
    const std::vector<vec2> &texcoords, const std::vector<vec3> &normals,
    bool is_facevarying_input,  // false: 'vertex' varying
    std::vector<vec3> *tangents, std::vector<vec3> *binormals,
    std::vector<uint32_t> *out_vertex_indices, std::string *err) {

  if (!tangents || !binormals || !out_vertex_indices) {
    PUSH_ERROR_AND_RETURN("Output arguments are nullptr.");
  }

  if (vertices.empty()) {
    PUSH_ERROR_AND_RETURN("vertices is empty.");
  }

  if (faceVertexIndices.size() < 3) {
    PUSH_ERROR_AND_RETURN("faceVertexIndices.size < 3");
  }

  // Convert tydra data structures to robust tangent computation format
  MeshData mesh;

  // Copy vertices
  for (const auto& v : vertices) {
    mesh.positions.emplace_back(v[0], v[1], v[2]);
  }

  // Copy normals (if available)
  if (!normals.empty()) {
    for (const auto& n : normals) {
      mesh.normals.emplace_back(n[0], n[1], n[2]);
    }
  }

  // Copy texcoords (if available)
  if (!texcoords.empty()) {
    for (const auto& uv : texcoords) {
      mesh.texcoords.emplace_back(uv[0], uv[1]);
    }
  }

  // Convert face indices to triangles
  // Handle both triangle and polygon cases
  size_t faceVertexIndexOffset = 0;
  bool hasFaceVertexCounts = !faceVertexCounts.empty();

  if (hasFaceVertexCounts) {
    for (size_t i = 0; i < faceVertexCounts.size(); i++) {
      size_t nv = faceVertexCounts[i];
      if (nv < 3) continue;

      // Triangulate polygon faces (simple fan triangulation)
      for (size_t f = 0; f < nv - 2; f++) {
        uint32_t i0 = faceVertexIndices[faceVertexIndexOffset];
        uint32_t i1 = faceVertexIndices[faceVertexIndexOffset + f + 1];
        uint32_t i2 = faceVertexIndices[faceVertexIndexOffset + f + 2];
        mesh.triangles.emplace_back(i0, i1, i2);
      }
      faceVertexIndexOffset += nv;
    }
  } else {
    // All triangles
    for (size_t i = 0; i < faceVertexIndices.size(); i += 3) {
      mesh.triangles.emplace_back(
        faceVertexIndices[i],
        faceVertexIndices[i + 1],
        faceVertexIndices[i + 2]
      );
    }
  }

  // Configure robust tangent computation options
  TangentComputeOptions options;
  options.useLengyelMethod = true;
  options.weightByArea = true;
  options.weightByAngle = true;
  options.orthogonalize = true;
  options.normalize = true;

  // Compute tangent spaces
  auto tangentSpaces = TangentComputer::ComputeTangentSpaces(mesh, options);

  if (tangentSpaces.empty()) {
    PUSH_ERROR_AND_RETURN("Failed to compute tangent spaces.");
  }

  // Convert back to tydra format
  tangents->clear();
  binormals->clear();
  tangents->resize(tangentSpaces.size());
  binormals->resize(tangentSpaces.size());

  for (size_t i = 0; i < tangentSpaces.size(); i++) {
    const auto& ts = tangentSpaces[i];
    (*tangents)[i] = vec3{ts.tangent.x, ts.tangent.y, ts.tangent.z};
    (*binormals)[i] = vec3{ts.binormal.x, ts.binormal.y, ts.binormal.z};
  }

  // Create identity vertex indices for now (robust computation already handles vertex sharing)
  out_vertex_indices->clear();
  for (size_t i = 0; i < faceVertexIndices.size(); i++) {
    out_vertex_indices->push_back(static_cast<uint32_t>(i));
  }

  return true;
}
#endif

static bool ComputeTangentsAndBinormals(
    const std::vector<vec3> &vertices,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices,
    const std::vector<vec2> &texcoords, const std::vector<vec3> &normals,
    bool is_facevarying_input,  // false: 'vertex' varying
    std::vector<vec3> *tangents, std::vector<vec3> *binormals,
    std::vector<uint32_t> *out_vertex_indices, std::string *err) {
  if (!tangents) {
    PUSH_ERROR_AND_RETURN("tangents arg is nullptr.");
  }

  if (!binormals) {
    PUSH_ERROR_AND_RETURN("binormals arg is nullptr.");
  }

  if (!out_vertex_indices) {
    PUSH_ERROR_AND_RETURN("out_indices arg is nullptr.");
  }

  if (vertices.empty()) {
    PUSH_ERROR_AND_RETURN("vertices is empty.");
  }

  // At least 1 triangle face should exist.
  if (faceVertexIndices.size() < 3) {
    PUSH_ERROR_AND_RETURN("faceVertexIndices.size < 3");
  }

  if (texcoords.empty()) {
    PUSH_ERROR_AND_RETURN("texcoords is empty");
  }

  if (normals.empty()) {
    PUSH_ERROR_AND_RETURN("normals is empty");
  }

  if (is_facevarying_input) {
    if (vertices.size() != faceVertexIndices.size()) {
      PUSH_ERROR_AND_RETURN("Invalid vertices.size.");
    }
    if (texcoords.size() != faceVertexIndices.size()) {
      PUSH_ERROR_AND_RETURN("Invalid texcoords.size.");
    }
    if (normals.size() != faceVertexIndices.size()) {
      PUSH_ERROR_AND_RETURN("Invalid normals.size.");
    }
  } else {
    uint32_t max_vert_index =
        *std::max_element(faceVertexIndices.begin(), faceVertexIndices.end());
    if (max_vert_index >= vertices.size()) {
      PUSH_ERROR_AND_RETURN("Invalid vertices.size.");
    }
    if (max_vert_index >= texcoords.size()) {
      PUSH_ERROR_AND_RETURN("Invalid texcoords.size.");
    }
    if (max_vert_index >= normals.size()) {
      PUSH_ERROR_AND_RETURN("Invalid normals.size.");
    }
  }

  bool hasFaceVertexCounts = true;
  if (faceVertexCounts.size() == 0) {
    // Assume all triangle faces.
    if ((faceVertexIndices.size() % 3) != 0) {
      PUSH_ERROR_AND_RETURN(
          "Invalid faceVertexIndices. It must be all triangles: "
          "faceVertexIndices.size % 3 == 0");
    }
    hasFaceVertexCounts = false;
  }

  // tn, bn = facevarying
  std::vector<value::normal3f> tn(faceVertexIndices.size());
  memset(&tn.at(0), 0, sizeof(value::normal3f) * tn.size());
  std::vector<value::normal3f> bn(faceVertexIndices.size());
  memset(&bn.at(0), 0, sizeof(value::normal3f) * bn.size());

  //
  // 1. Compute facevarying tangent/binormal for each faceVertex.
  //
  size_t faceVertexIndexOffset{0};
  for (size_t i = 0; i < faceVertexCounts.size(); i++) {
    size_t nv = hasFaceVertexCounts ? faceVertexCounts[i] : 3;

    if ((faceVertexIndexOffset + nv) >= faceVertexIndices.size()) {
      // Invalid faceVertexIndices
      PUSH_ERROR_AND_RETURN("Invalid value in faceVertexOffset.");
    }

    if (nv < 3) {
      PUSH_ERROR_AND_RETURN("Degenerated facet found.");
    }

    // Process each two-edges per facet.
    //
    // Example:
    //
    // fv3
    //  o----------------o fv2
    //   \              /
    //    \            /
    //     o----------o
    //    fv0         fv1

    // facet0:  fv0, fv1, fv2
    // facet1:  fv1, fv2, fv3

    for (size_t f = 0; f < nv - 2; f++) {
      size_t fid0 = faceVertexIndexOffset + f;
      size_t fid1 = faceVertexIndexOffset + f + 1;
      size_t fid2 = faceVertexIndexOffset + f + 2;

      uint32_t vf0 =
          is_facevarying_input ? uint32_t(fid0) : faceVertexIndices[fid0];
      uint32_t vf1 =
          is_facevarying_input ? uint32_t(fid1) : faceVertexIndices[fid1];
      uint32_t vf2 =
          is_facevarying_input ? uint32_t(fid2) : faceVertexIndices[fid2];

      if ((vf0 >= vertices.size()) || (vf1 >= vertices.size()) ||
          (vf2 >= vertices.size())) {
        // index out-of-range
        PUSH_ERROR_AND_RETURN(
            "Invalid value in faceVertexIndices. some exceeds vertices.size()");
      }

      vec3 v1 = vertices[vf0];
      vec3 v2 = vertices[vf1];
      vec3 v3 = vertices[vf2];

      float v1x = v1[0];
      float v1y = v1[1];
      float v1z = v1[2];

      float v2x = v2[0];
      float v2y = v2[1];
      float v2z = v2[2];

      float v3x = v3[0];
      float v3y = v3[1];
      float v3z = v3[2];

      float w1x = 0.0f;
      float w1y = 0.0f;
      float w2x = 0.0f;
      float w2y = 0.0f;
      float w3x = 0.0f;
      float w3y = 0.0f;

      if ((vf0 >= texcoords.size()) || (vf1 >= texcoords.size()) ||
          (vf2 >= texcoords.size())) {
        // index out-of-range
        PUSH_ERROR_AND_RETURN("Invalid index. some exceeds texcoords.size()");
      }

      {
        vec2 uv1 = texcoords[vf0];
        vec2 uv2 = texcoords[vf1];
        vec2 uv3 = texcoords[vf2];

        w1x = uv1[0];
        w1y = uv1[1];
        w2x = uv2[0];
        w2y = uv2[1];
        w3x = uv3[0];
        w3y = uv3[1];
      }

      float x1 = v2x - v1x;
      float x2 = v3x - v1x;
      float y1 = v2y - v1y;
      float y2 = v3y - v1y;
      float z1 = v2z - v1z;
      float z2 = v3z - v1z;

      float s1 = w2x - w1x;
      float s2 = w3x - w1x;
      float t1 = w2y - w1y;
      float t2 = w3y - w1y;

      float r = 1.0;

      if (std::fabs(double(s1 * t2 - s2 * t1)) > 1.0e-20) {
        r /= (s1 * t2 - s2 * t1);
      }

      vec3 tdir{(t2 * x1 - t1 * x2) * r, (t2 * y1 - t1 * y2) * r,
                (t2 * z1 - t1 * z2) * r};
      vec3 bdir{(s1 * x2 - s2 * x1) * r, (s1 * y2 - s2 * y1) * r,
                (s1 * z2 - s2 * z1) * r};

      //
      // NOTE: for quad or polygon mesh, this overwrites previous 2 facevarying
      // points for each face.
      //       And this would not be a good way to compute tangents for
      //       quad/polygon.
      //

      tn[fid0][0] = tdir[0];
      tn[fid0][1] = tdir[1];
      tn[fid0][2] = tdir[2];

      tn[fid1][0] = tdir[0];
      tn[fid1][1] = tdir[1];
      tn[fid1][2] = tdir[2];

      tn[fid2][0] = tdir[0];
      tn[fid2][1] = tdir[1];
      tn[fid2][2] = tdir[2];

      bn[fid0][0] = bdir[0];
      bn[fid0][1] = bdir[1];
      bn[fid0][2] = bdir[2];

      bn[fid1][0] = bdir[0];
      bn[fid1][1] = bdir[1];
      bn[fid1][2] = bdir[2];

      bn[fid2][0] = bdir[0];
      bn[fid2][1] = bdir[1];
      bn[fid2][2] = bdir[2];
    }

    faceVertexIndexOffset += nv;
  }

  //
  // 2. Build indices(use same index for shared-vertex)
  //
  std::vector<uint32_t> vertex_indices;  // len = faceVertexIndices.size()
  {
    ComputeTangentVertexInput<ComputeTangentPackedVertexData> vertex_input;
    ComputeTangentVertexOutput<ComputeTangentPackedVertexData> vertex_output;

    if (is_facevarying_input) {
      // input position is still in 'vertex' variability.
      for (size_t i = 0; i < faceVertexIndices.size(); i++) {
        vertex_input.point_indices.push_back(faceVertexIndices[i]);
      }
      vertex_input.normals = normals;
      vertex_input.uvs = texcoords;
    } else {
      // expand to facevarying.
      for (size_t i = 0; i < faceVertexIndices.size(); i++) {
        vertex_input.point_indices.push_back(faceVertexIndices[i]);
        vertex_input.normals.push_back(normals[faceVertexIndices[i]]);
        vertex_input.uvs.push_back(texcoords[faceVertexIndices[i]]);
      }
    }

    std::vector<uint32_t> vertex_point_indices;

    BuildIndices<ComputeTangentVertexInput<ComputeTangentPackedVertexData>,
                 ComputeTangentVertexOutput<ComputeTangentPackedVertexData>,
                 ComputeTangentPackedVertexData,
                 ComputeTangentPackedVertexDataHasher,
                 ComputeTangentPackedVertexDataEqual>(
        vertex_input, vertex_output, vertex_indices, vertex_point_indices);

    DCOUT("faceVertexIndices.size : " << faceVertexIndices.size());
    DCOUT("# of indices after the build: "
          << vertex_indices.size() << ", reduced "
          << (faceVertexIndices.size() - vertex_indices.size()) << " indices.");
    // We only need indices. Discard vertex_output and vertrex_point_indices
  }

  const uint32_t num_verts =
      *std::max_element(vertex_indices.begin(), vertex_indices.end());

  //
  // 3. normalize * orthogonalize;
  //

  // per-vertex tangents/binormals
  std::vector<value::normal3f> v_tn;
  v_tn.assign(num_verts, {0.0f, 0.0f, 0.0f});

  std::vector<value::normal3f> v_bn;
  v_bn.assign(num_verts, {0.0f, 0.0f, 0.0f});

  for (size_t i = 0; i < vertex_indices.size(); i++) {
    value::normal3f Tn = tn[vertex_indices[i]];
    value::normal3f Bn = bn[vertex_indices[i]];

    v_tn[vertex_indices[i]][0] += Tn[0];
    v_tn[vertex_indices[i]][1] += Tn[1];
    v_tn[vertex_indices[i]][2] += Tn[2];

    v_bn[vertex_indices[i]][0] += Bn[0];
    v_bn[vertex_indices[i]][1] += Bn[1];
    v_bn[vertex_indices[i]][2] += Bn[2];
  }

  for (size_t i = 0; i < size_t(num_verts); i++) {
    if (vlength(v_tn[i]) > 0.0f) {
      v_tn[i] = vnormalize(v_tn[i]);
    }
    if (vlength(v_bn[i]) > 0.0f) {
      v_bn[i] = vnormalize(v_bn[i]);
    }
  }

  tangents->assign(num_verts, {0.0f, 0.0f, 0.0f});
  binormals->assign(num_verts, {0.0f, 0.0f, 0.0f});

  for (size_t i = 0; i < vertex_indices.size(); i++) {
    value::normal3f n;

    // http://www.terathon.com/code/tangent.html

    n[0] = normals[vertex_indices[i]][0];
    n[1] = normals[vertex_indices[i]][1];
    n[2] = normals[vertex_indices[i]][2];

    value::normal3f Tn = v_tn[vertex_indices[i]];
    value::normal3f Bn = v_bn[vertex_indices[i]];

    // Gram-Schmidt orthogonalize
    Tn = (Tn - n * vdot(n, Tn));
    if (vlength(Tn) > 0.0f) {
      Tn = vnormalize(Tn);
    }

    // Calculate handedness
    if (vdot(vcross(n, Tn), Bn) < 0.0f) {
      Tn = Tn * -1.0f;
    }

    ((*tangents)[vertex_indices[i]])[0] = Tn[0];
    ((*tangents)[vertex_indices[i]])[1] = Tn[1];
    ((*tangents)[vertex_indices[i]])[2] = Tn[2];

    ((*binormals)[vertex_indices[i]])[0] = Bn[0];
    ((*binormals)[vertex_indices[i]])[1] = Bn[1];
    ((*binormals)[vertex_indices[i]])[2] = Bn[2];
  }

  (*out_vertex_indices) = vertex_indices;

  return true;
}

//
// Compute geometric normal in CCW(Counter Clock-Wise) manner
// Also computes the area of the input triangle.
//
inline static value::float3 GeometricNormal(const value::float3 v0,
                                            const value::float3 v1,
                                            const value::float3 v2,
                                            float &area) {
  const value::float3 v10 = v1 - v0;
  const value::float3 v20 = v2 - v0;

  value::float3 Nf = vcross(v10, v20);  // CCW
  area = 0.5f * vlength(Nf);
  Nf = vnormalize(Nf);

  return Nf;
}

//
// Compute a normal for vertices.
// Normal vector is computed as weighted(by the area of the triangle) vector.
//
// TODO: Implement better normal calculation. ref.
// http://www.bytehazard.com/articles/vertnorm.html
//
static bool ComputeNormals(const std::vector<vec3> &vertices,
                           const std::vector<uint32_t> &faceVertexCounts,
                           const std::vector<uint32_t> &faceVertexIndices,
                           std::vector<vec3> &normals, std::string *err) {
  normals.assign(vertices.size(), {0.0f, 0.0f, 0.0f});

  size_t faceVertexIndexOffset{0};
  for (size_t f = 0; f < faceVertexCounts.size(); f++) {
    size_t nv = faceVertexCounts[f];

    if (nv < 3) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Invalid face num {} at faceVertexCounts[{}]", nv, f));
    }

    // For quad/polygon, first three vertices are used to compute face normal
    // (Assume quad/polygon plane is co-planar)
    uint32_t vidx0 = faceVertexIndices[faceVertexIndexOffset + 0];
    uint32_t vidx1 = faceVertexIndices[faceVertexIndexOffset + 1];
    uint32_t vidx2 = faceVertexIndices[faceVertexIndexOffset + 2];

    if (vidx0 >= vertices.size()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("vertexIndex0 {} exceeds vertices.size {}", vidx0, vertices.size()));
    }

    if (vidx1 >= vertices.size()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("vertexIndex1 {} exceeds vertices.size {}", vidx1, vertices.size()));
    }

    if (vidx2 >= vertices.size()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("vertexIndex2 {} exceeds vertices.size {}", vidx2, vertices.size()));
    }

    float area{0.0f};
    value::float3 Nf = GeometricNormal(vertices[vidx0], vertices[vidx1],
                                       vertices[vidx2], area);

    for (size_t v = 0; v < nv; v++) {
      uint32_t vidx = faceVertexIndices[faceVertexIndexOffset + v];
      if (vidx >= vertices.size()) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "vertexIndex exceeds vertices.size {}", vertices.size()));
      }
      normals[vidx] += area * Nf;
    }

    faceVertexIndexOffset += nv;
  }

  for (size_t v = 0; v < normals.size(); v++) {
    normals[v] = vnormalize(normals[v]);
  }

  return true;
}

}  // namespace


namespace {

bool ListUVNames(const RenderMaterial &material,
                 const std::vector<UVTexture> &textures,
                 StringAndIdMap &si_map) {
  // Helper lambdas to extract UV names from shader parameters
  auto fun_vec3 = [&](const ShaderParam<vec3> &param) {
    int32_t texId = param.texture_id;
    if ((texId >= 0) && (size_t(texId) < textures.size())) {
      const UVTexture &tex = textures[size_t(texId)];
      if (tex.varname_uv.size()) {
        if (!si_map.count(tex.varname_uv)) {
          uint64_t slotId = si_map.size();
          DCOUT("Add textureSlot: " << tex.varname_uv << ", " << slotId);
          si_map.add(tex.varname_uv, slotId);
        }
      }
    }
  };

  auto fun_float = [&](const ShaderParam<float> &param) {
    int32_t texId = param.texture_id;
    if ((texId >= 0) && (size_t(texId) < textures.size())) {
      const UVTexture &tex = textures[size_t(texId)];
      if (tex.varname_uv.size()) {
        if (!si_map.count(tex.varname_uv)) {
          uint64_t slotId = si_map.size();
          DCOUT("Add textureSlot: " << tex.varname_uv << ", " << slotId);
          si_map.add(tex.varname_uv, slotId);
        }
      }
    }
  };

  // Check UsdPreviewSurface shader
  if (material.surfaceShader.has_value()) {
    fun_vec3(material.surfaceShader->diffuseColor);
    fun_vec3(material.surfaceShader->normal);
    fun_float(material.surfaceShader->metallic);
    fun_float(material.surfaceShader->roughness);
    fun_float(material.surfaceShader->clearcoat);
    fun_float(material.surfaceShader->clearcoatRoughness);
    fun_float(material.surfaceShader->opacity);
    fun_float(material.surfaceShader->opacityThreshold);
    fun_float(material.surfaceShader->ior);
    fun_float(material.surfaceShader->displacement);
    fun_float(material.surfaceShader->occlusion);
  }

  // Check MaterialX OpenPBR shader
  if (material.openPBRShader.has_value()) {
    // Base layer
    fun_float(material.openPBRShader->base_weight);
    fun_vec3(material.openPBRShader->base_color);
    fun_float(material.openPBRShader->base_roughness);
    fun_float(material.openPBRShader->base_metalness);
    fun_float(material.openPBRShader->base_diffuse_roughness);

    // Specular layer
    fun_float(material.openPBRShader->specular_weight);
    fun_vec3(material.openPBRShader->specular_color);
    fun_float(material.openPBRShader->specular_roughness);
    fun_float(material.openPBRShader->specular_ior);
    fun_float(material.openPBRShader->specular_ior_level);
    fun_float(material.openPBRShader->specular_anisotropy);
    fun_float(material.openPBRShader->specular_rotation);

    // Transmission
    fun_float(material.openPBRShader->transmission_weight);
    fun_vec3(material.openPBRShader->transmission_color);
    fun_float(material.openPBRShader->transmission_depth);
    fun_vec3(material.openPBRShader->transmission_scatter);
    fun_float(material.openPBRShader->transmission_scatter_anisotropy);
    fun_float(material.openPBRShader->transmission_dispersion);

    // Subsurface
    fun_float(material.openPBRShader->subsurface_weight);
    fun_vec3(material.openPBRShader->subsurface_color);
    fun_float(material.openPBRShader->subsurface_radius);
    fun_vec3(material.openPBRShader->subsurface_radius_scale);
    fun_float(material.openPBRShader->subsurface_scale);
    fun_float(material.openPBRShader->subsurface_anisotropy);

    // Sheen
    fun_float(material.openPBRShader->sheen_weight);
    fun_vec3(material.openPBRShader->sheen_color);
    fun_float(material.openPBRShader->sheen_roughness);

    // Fuzz
    fun_float(material.openPBRShader->fuzz_weight);
    fun_vec3(material.openPBRShader->fuzz_color);
    fun_float(material.openPBRShader->fuzz_roughness);

    // Thin film
    fun_float(material.openPBRShader->thin_film_weight);
    fun_float(material.openPBRShader->thin_film_thickness);
    fun_float(material.openPBRShader->thin_film_ior);

    // Coat
    fun_float(material.openPBRShader->coat_weight);
    fun_vec3(material.openPBRShader->coat_color);
    fun_float(material.openPBRShader->coat_roughness);
    fun_float(material.openPBRShader->coat_anisotropy);
    fun_float(material.openPBRShader->coat_rotation);
    fun_float(material.openPBRShader->coat_ior);
    fun_vec3(material.openPBRShader->coat_affect_color);
    fun_float(material.openPBRShader->coat_affect_roughness);

    // Emission
    fun_float(material.openPBRShader->emission_luminance);
    fun_vec3(material.openPBRShader->emission_color);

    // Geometry
    fun_float(material.openPBRShader->opacity);
    fun_vec3(material.openPBRShader->normal);
    fun_vec3(material.openPBRShader->tangent);
  }

  return true;
}

#undef PushError

}  // namespace

class SkelRootSkeletonResolver {
 public:
  using SkelRootToSkeletonMap =
      std::unordered_map<std::string, std::pair<Path, const Skeleton *>>;

  static void BuildMap(const PathPrimMap<Skeleton> &allSkeletons,
                       const PathPrimMap<SkelRoot> &allSkelRoots,
                       SkelRootToSkeletonMap *out_map) {
    if (!out_map) {
      return;
    }

    out_map->clear();
    out_map->reserve(allSkelRoots.size());

    for (const auto &kv : allSkeletons) {
      const std::string &skel_path_str = kv.first;
      const Skeleton *skel_ptr = kv.second;
      Path current_path(skel_path_str, "");

      while (current_path.is_valid() && !current_path.is_root_path()) {
        Path parent_path = current_path.get_parent_prim_path();
        const std::string parent_path_str = parent_path.prim_part();

        if (allSkelRoots.find(parent_path_str) != allSkelRoots.end()) {
          // Deterministic selection: if multiple Skeletons exist under one
          // SkelRoot, keep lexicographically smallest absolute skeleton path.
          auto it = out_map->find(parent_path_str);
          if (it == out_map->end()) {
            out_map->emplace(parent_path_str,
                             std::make_pair(Path(skel_path_str, ""), skel_ptr));
          } else if (skel_path_str < it->second.first.prim_part()) {
            it->second = std::make_pair(Path(skel_path_str, ""), skel_ptr);
          }
          break;
        }

        current_path = parent_path;
      }
    }
  }

  // Find skeleton for mesh by walking up ancestor hierarchy.
  // Returns true if skeleton found, with path and skeleton pointer stored.
  static bool FindByAncestor(const Path &meshPath,
                             const PathPrimMap<Skeleton> &allSkeletons,
                             const PathPrimMap<SkelRoot> &allSkelRoots,
                             const SkelRootToSkeletonMap *skelRootToSkeleton,
                             Path *outSkelPath, const Skeleton **outSkelPtr) {
    if (allSkeletons.empty()) {
      return false;
    }

    // Walk up ancestor chain
    Path currentPath = meshPath;
    while (currentPath.is_valid() && !currentPath.is_root_path()) {
      Path parentPath = currentPath.get_parent_prim_path();
      std::string parentPathStr = parentPath.prim_part();

      DCOUT("FindSkeletonByAncestor: checking parent " << parentPathStr);

      // Check if parent is a SkelRoot
      auto skelRootIt = allSkelRoots.find(parentPathStr);
      if (skelRootIt != allSkelRoots.end()) {
        DCOUT("Found SkelRoot ancestor: " << parentPathStr);

        if (skelRootToSkeleton) {
          auto mapped = skelRootToSkeleton->find(parentPathStr);
          if (mapped != skelRootToSkeleton->end()) {
            *outSkelPath = mapped->second.first;
            if (outSkelPtr) *outSkelPtr = mapped->second.second;
            DCOUT("Found skeleton under SkelRoot from cache: "
                  << mapped->second.first.prim_part());
            return true;
          }
        }

        // Found SkelRoot ancestor - search its children for Skeleton
        std::string bestSkelPath;
        const Skeleton *bestSkelPtr{nullptr};
        for (const auto &kv : allSkeletons) {
          const std::string &skelPath = kv.first;
          const Skeleton *skelPtr = kv.second;

          if (IsStrictDescendantPath(skelPath, parentPathStr)) {
            if (!bestSkelPtr || skelPath < bestSkelPath) {
              bestSkelPath = skelPath;
              bestSkelPtr = skelPtr;
            }
          }
        }

        if (bestSkelPtr) {
          *outSkelPath = Path(bestSkelPath, "");
          if (outSkelPtr) *outSkelPtr = bestSkelPtr;
          DCOUT("Found skeleton under SkelRoot: " << bestSkelPath);
          return true;
        }
      }

      // Also check if parent itself is a Skeleton
      auto skelIt = allSkeletons.find(parentPathStr);
      if (skelIt != allSkeletons.end()) {
        *outSkelPath = Path(parentPathStr, "");
        if (outSkelPtr) *outSkelPtr = skelIt->second;
        DCOUT("Found skeleton as ancestor: " << parentPathStr);
        return true;
      }

      currentPath = parentPath;
    }

    // Fallback: if only one skeleton in scene, use it
    if (allSkeletons.size() == 1) {
      *outSkelPath = Path(allSkeletons.begin()->first, "");
      if (outSkelPtr) *outSkelPtr = allSkeletons.begin()->second;
      DCOUT("Fallback: using only skeleton in scene: "
            << allSkeletons.begin()->first);
      return true;
    }

    return false;
  }

 private:
  static bool IsStrictDescendantPath(const std::string &descendantPath,
                                     const std::string &ancestorPath) {
    if (ancestorPath.empty() || descendantPath.empty()) {
      return false;
    }

    // All absolute prim paths are expected to start with '/'.
    if (descendantPath[0] != '/' || ancestorPath[0] != '/') {
      return false;
    }

    if (descendantPath.size() <= ancestorPath.size()) {
      return false;
    }

    if (descendantPath.compare(0, ancestorPath.size(), ancestorPath) != 0) {
      return false;
    }

    // Require a path-segment boundary:
    // "/A/SkelRoot/Skel" is a descendant of "/A/SkelRoot", but
    // "/A/SkelRootExtra/Skel" is not.
    return descendantPath[ancestorPath.size()] == '/';
  }
};

///
/// Convert vertex variability either 'vertex' or 'facevarying'
///
/// @param[in] to_vertex_varying true: Convert to 'vertrex' varying. false:
/// Convert to 'facevarying'
///
bool RenderSceneConverter::ConvertVertexVariabilityImpl(
    VertexAttribute &vattr, const bool to_vertex_varying,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices) {
  if (vattr.data.empty()) {
    return true;
  }

  if (vattr.variability == VertexVariability::Uniform) {
    if (to_vertex_varying) {
      auto result = UniformToVertex(vattr.get_data(), vattr.stride_bytes(),
                                    faceVertexCounts, faceVertexIndices);

      if (!result) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Convert `{}` attribute with uniform-varying "
                        "to vertex-varying failed: {}",
                        vattr.name, result.error()));
      }

      vattr.data = result.value();
      vattr.variability = VertexVariability::Vertex;

    } else {
      auto result = UniformToFaceVarying(vattr.get_data(), vattr.stride_bytes(),
                                         faceVertexCounts);
      if (!result) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Convert uniform `{}` attribute to failed: {}",
                        vattr.name, result.error()));
      }

      vattr.data = result.value();
      vattr.variability = VertexVariability::FaceVarying;
    }
  } else if (vattr.variability == VertexVariability::Constant) {
    if (to_vertex_varying) {
      auto result = ConstantToVertex(vattr.get_data(), vattr.stride_bytes(),
                                     faceVertexCounts, faceVertexIndices);

      if (!result) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Convert `{}` attribute with uniform-varying "
                        "to vertex-varying failed: {}",
                        vattr.name, result.error()));
      }

      vattr.data = result.value();
      vattr.variability = VertexVariability::Vertex;

    } else {
      auto result = UniformToFaceVarying(vattr.get_data(), vattr.stride_bytes(),
                                         faceVertexCounts);
      if (!result) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Convert uniform `{}` attribute to failed: {}",
                        vattr.name, result.error()));
      }

      vattr.data = result.value();
      vattr.variability = VertexVariability::FaceVarying;
    }

  } else if ((vattr.variability == VertexVariability::Vertex) ||
             (vattr.variability == VertexVariability::Varying)) {
    if (!to_vertex_varying) {
      auto result = VertexToFaceVarying(vattr.get_data(), vattr.stride_bytes(),
                                        faceVertexCounts, faceVertexIndices);
      if (!result) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Convert vertex/varying `{}` attribute to failed: {}",
                        vattr.name, result.error()));
      }

      vattr.data = result.value();
      vattr.variability = VertexVariability::FaceVarying;
    }

  } else if (vattr.variability == VertexVariability::FaceVarying) {
    if (to_vertex_varying) {
      PUSH_ERROR_AND_RETURN(
          "Internal error. `to_vertex_varying` should not be true when "
          "FaceVarying.");
    }

  } else {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Unsupported/unimplemented interpolation: {} ",
                    to_string(vattr.variability)));
  }

  return true;
}

bool RenderSceneConverter::BuildVertexIndicesImpl(RenderMesh &mesh) {
  //
  // - If mesh is triangulated, use triangulatedFaceVertexIndices, otherwise use
  // faceVertxIndices.
  // - Make vertex attributes 'facevarying' variability
  // - Assign same id for similar(currently identitical) vertex attribute.
  // - Reorder vertex attributes to 'vertex' variability.
  //

  //TUSDZ_LOG_I("BuildVertexIndicesImpl");

  const std::vector<uint32_t> &fvIndices =
      mesh.triangulatedFaceVertexIndices.size()
          ? mesh.triangulatedFaceVertexIndices
          : mesh.usdFaceVertexIndices;

  //std::cout << "triangulatedFaceVertexIndices.max_value: " << *std::max_element(mesh.triangulatedFaceVertexIndices.begin(), mesh.triangulatedFaceVertexIndices.end() << "\n");

  //std::cout << "usdFaceVertexIndices.min_value: " << *std::min_element(mesh.usdFaceVertexIndices.begin(), mesh.usdFaceVertexIndices.end() << "\n");
  //std::cout << "usdFaceVertexIndices.max_value: " << *std::max_element(mesh.usdFaceVertexIndices.begin(), mesh.usdFaceVertexIndices.end() << "\n");

  DefaultVertexInput<DefaultPackedVertexData> vertex_input;

  size_t num_verts = mesh.points.size();
  size_t num_fvs = fvIndices.size();
  vertex_input.point_indices = fvIndices;

  if (mesh.normals.vertex_count()) {
    if (!mesh.normals.is_facevarying()) {
      PUSH_ERROR_AND_RETURN(
          "Internal error. normals must be 'facevarying' variability.");
    }
    if (mesh.normals.vertex_count() != num_fvs) {
      PUSH_ERROR_AND_RETURN(
          "Internal error. The number of normal items does not match with "
          "the number of facevarying items.");
    }
  }

  const value::float2 *texcoord0_ptr = nullptr;
  const value::float2 *texcoord1_ptr = nullptr;

  for (const auto &it : mesh.texcoords) {
    if (it.second.vertex_count() > 0) {
      if (!it.second.is_facevarying()) {
        PUSH_ERROR_AND_RETURN(
            "Internal error. texcoords must be 'facevarying' variability.");
      }
      if (it.second.vertex_count() != num_fvs) {
        PUSH_ERROR_AND_RETURN(
            "Internal error. The number of texcoord items does not match "
            "with the number of facevarying items.");
      }

      if (it.first == 0) {
        texcoord0_ptr = reinterpret_cast<const value::float2 *>(
            it.second.get_data().data());
      } else if (it.first == 1) {
        texcoord1_ptr = reinterpret_cast<const value::float2 *>(
            it.second.get_data().data());
      } else {
        // ignore.
      }
    }
  }

  const value::float3 *tangents_ptr = nullptr;
  const value::float3 *binormals_ptr = nullptr;

  if (texcoord0_ptr) {
    if (mesh.tangents.vertex_count()) {
      if (!mesh.tangents.is_facevarying()) {
        PUSH_ERROR_AND_RETURN(
            "Internal error. tangents must be 'facevarying' variability.");
      }
      if (mesh.tangents.vertex_count() != num_fvs) {
        PUSH_ERROR_AND_RETURN(
            "Internal error. The number of tangents items does not match "
            "with the number of facevarying items.");
      }

      tangents_ptr = reinterpret_cast<const value::float3 *>(
          mesh.tangents.get_data().data());
    }

    if (mesh.binormals.vertex_count()) {
      if (!mesh.binormals.is_facevarying()) {
        PUSH_ERROR_AND_RETURN(
            "Internal error. binormals must be 'facevarying' variability.");
      }
      if (mesh.binormals.vertex_count() != num_fvs) {
        PUSH_ERROR_AND_RETURN(
            "Internal error. The number of binormals items does not match "
            "with the number of facevarying items.");
      }
      binormals_ptr = reinterpret_cast<const value::float3 *>(
          mesh.binormals.get_data().data());
    }
  }

  if (mesh.vertex_colors.vertex_count()) {
    if (!mesh.vertex_colors.is_facevarying()) {
      PUSH_ERROR_AND_RETURN(
          "Internal error. vertex_colors must be 'facevarying' variability.");
    }
    if (mesh.vertex_colors.vertex_count() != num_fvs) {
      PUSH_ERROR_AND_RETURN(
          "Internal error. The number of vertex_color items does not match "
          "with the number of facevarying items.");
    }
  }

  if (mesh.vertex_opacities.vertex_count()) {
    if (!mesh.vertex_opacities.is_facevarying()) {
      PUSH_ERROR_AND_RETURN(
          "Internal error. vertex_opacities must be 'facevarying' "
          "variability.");
    }
    if (mesh.vertex_colors.vertex_count() != num_fvs) {
      PUSH_ERROR_AND_RETURN(
          "Internal error. The number of vertex_opacity items does not match "
          "with the number of facevarying items.");
    }
  }

  const value::float3 *normals_ptr =
      (mesh.normals.vertex_count() > 0)
          ? reinterpret_cast<const value::float3 *>(
                mesh.normals.get_data().data())
          : nullptr;
  const value::float3 *colors_ptr =
      (mesh.vertex_colors.vertex_count() > 0)
          ? reinterpret_cast<const value::float3 *>(
                mesh.vertex_colors.get_data().data())
          : nullptr;
  const float *opacities_ptr =
      (mesh.vertex_opacities.vertex_count() > 0)
          ? reinterpret_cast<const float *>(
                mesh.vertex_opacities.get_data().data())
          : nullptr;


  if (texcoord0_ptr) {
    vertex_input.uv0s.assign(num_fvs, {0.0f, 0.0f});
  }

  if (texcoord1_ptr) {
    vertex_input.uv1s.assign(num_fvs, {0.0f, 0.0f});
  }

  if (normals_ptr) {
    vertex_input.normals.assign(num_fvs, {0.0f, 0.0f, 0.0f});
  }

  if (tangents_ptr) {
    vertex_input.tangents.assign(num_fvs, {0.0f, 0.0f, 0.0f});
  }

  if (binormals_ptr) {
    vertex_input.binormals.assign(num_fvs, {0.0f, 0.0f, 0.0f});
  }

  if (colors_ptr) {
    vertex_input.colors.assign(num_fvs, {0.0f, 0.0f, 0.0f});
  }

  if (opacities_ptr) {
    vertex_input.opacities.assign(num_fvs, 0.0f);
  }

  for (size_t i = 0; i < num_fvs; i++) {
    size_t fvi = fvIndices[i];
    if (fvi >= num_verts) {
      PUSH_ERROR("usdFaceVertexIndices.min_value: " << *std::min_element(mesh.usdFaceVertexIndices.begin(), mesh.usdFaceVertexIndices.end()) << "\n");
      PUSH_ERROR("usdFaceVertexIndices.max_value: " << *std::max_element(mesh.usdFaceVertexIndices.begin(), mesh.usdFaceVertexIndices.end()) << "\n");
      PUSH_ERROR("triangulatedFaceVertexIndices.min_value: " << *std::min_element(mesh.triangulatedFaceVertexIndices.begin(), mesh.triangulatedFaceVertexIndices.end()) << "\n");
      PUSH_ERROR("triangulatedFaceVertexIndices.max_value: " << *std::max_element(mesh.triangulatedFaceVertexIndices.begin(), mesh.triangulatedFaceVertexIndices.end()) << "\n");
      PUSH_ERROR_AND_RETURN(fmt::format(
          "Invalid faceVertexIndex {}. Must be less than {}(triangulated = {})", fvi, num_fvs, mesh.triangulatedFaceVertexIndices.size() ? "true" : "faise"));
    }

    if (normals_ptr) {
      vertex_input.normals[i] = normals_ptr[i];
    }
    if (texcoord0_ptr) {
      vertex_input.uv0s[i] = texcoord0_ptr[i];
    }
    if (texcoord1_ptr) {
      vertex_input.uv1s[i] = texcoord1_ptr[i];
    }
    if (tangents_ptr) {
      vertex_input.tangents[i] = tangents_ptr[i];
    }
    if (binormals_ptr) {
      vertex_input.binormals[i] = binormals_ptr[i];
    }
    if (colors_ptr) {
      vertex_input.colors[i] = colors_ptr[i];
    }
    if (opacities_ptr) {
      vertex_input.opacities[i] = opacities_ptr[i];
    }
  }

  std::vector<uint32_t> out_indices;
  std::vector<uint32_t> out_point_indices;  // to reorder position data
  DefaultVertexOutput<DefaultPackedVertexData> vertex_output;


  BuildIndices<DefaultVertexInput<DefaultPackedVertexData>,
               DefaultVertexOutput<DefaultPackedVertexData>,
               DefaultPackedVertexData, DefaultPackedVertexDataHasher,
               DefaultPackedVertexDataEqual>(vertex_input, vertex_output,
                                             out_indices, out_point_indices);

  if (out_indices.size() != out_point_indices.size()) {
    PUSH_ERROR_AND_RETURN(
        "Internal error. out_indices.size != out_point_indices.");
  }

  DCOUT("faceVertexIndices.size : " << fvIndices.size());
  DCOUT("# of indices after the build: "
        << out_indices.size() << ", reduced "
        << (fvIndices.size() - out_indices.size()) << " indices.");



  //
  // Reorder 'vertex' varying attributes(points, jointIndices/jointWeights,
  // BlendShape points, ...)
  // TODO: Preserve input order as much as possible.
  //
  {
    uint32_t numPoints =
        *std::max_element(out_indices.begin(), out_indices.end()) + 1;
    {
      std::vector<value::float3> tmp_points(numPoints);
      // TODO: Use vertex_output[i].point_index?
      for (size_t i = 0; i < out_point_indices.size(); i++) {
        if (out_point_indices[i] >= mesh.points.size()) {
          PUSH_ERROR_AND_RETURN("Internal error. point index out-of-range.");
        }
        tmp_points[out_indices[i]] = mesh.points[out_point_indices[i]];
      }
      mesh.points.swap(tmp_points);
    }

    if (mesh.joint_and_weights.jointIndices.size()) {
      if (mesh.joint_and_weights.elementSize < 1) {
        PUSH_ERROR_AND_RETURN(
            "Internal error. Invalid elementSize in mesh.joint_and_weights.");
      }
      uint32_t elementSize = uint32_t(mesh.joint_and_weights.elementSize);
      std::vector<int> tmp_indices(size_t(numPoints) * size_t(elementSize));
      std::vector<float> tmp_weights(size_t(numPoints) * size_t(elementSize));
      for (size_t i = 0; i < out_point_indices.size(); i++) {
        if ((elementSize * out_point_indices[i]) >=
            mesh.joint_and_weights.jointIndices.size()) {
          PUSH_ERROR_AND_RETURN(
              "Internal error. point index exceeds jointIndices.size.");
        }
        for (size_t k = 0; k < elementSize; k++) {
          tmp_indices[size_t(elementSize) * size_t(out_indices[i]) + k] =
              mesh.joint_and_weights
                  .jointIndices[size_t(elementSize) * size_t(out_point_indices[i]) + k];
        }

        if ((elementSize * out_point_indices[i]) >=
            mesh.joint_and_weights.jointWeights.size()) {
          PUSH_ERROR_AND_RETURN(
              "Internal error. point index exceeds jointWeights.size.");
        }

        for (size_t k = 0; k < elementSize; k++) {
          tmp_weights[size_t(elementSize) * size_t(out_indices[i]) + k] =
              mesh.joint_and_weights
                  .jointWeights[size_t(elementSize) * size_t(out_point_indices[i]) + k];
        }
      }
      mesh.joint_and_weights.jointIndices.swap(tmp_indices);
      mesh.joint_and_weights.jointWeights.swap(tmp_weights);
    }

    if (mesh.targets.size()) {
      // For BlendShape, reordering pointIndices, pointOffsets and normalOffsets is not enough.
      // Some points could be duplicated, so we need to find a mapping of org pointIdx -> pointIdx list in reordered points,
      // Then splat point attributes accordingly.

      // org pointIdx -> List of pointIdx in reordered points.
      std::unordered_map<uint32_t, std::vector<uint32_t>> pointIdxRemap;

      for (size_t i = 0; i < vertex_output.size(); i++) {
        pointIdxRemap[vertex_output.point_indices[i]].push_back(uint32_t(i));
      }

      for (auto &target : mesh.targets) {

        std::vector<value::float3> tmpPointOffsets;
        std::vector<value::float3> tmpNormalOffsets;
        std::vector<uint32_t> tmpPointIndices;

        for (size_t i = 0; i < target.second.pointIndices.size(); i++) {

          uint32_t orgPointIdx = target.second.pointIndices[i];
          if (!pointIdxRemap.count(orgPointIdx)) {
            PUSH_ERROR_AND_RETURN("Invalid pointIndices value.");
          }
          const std::vector<uint32_t> &dstPointIndices = pointIdxRemap.at(orgPointIdx);

          for (size_t k = 0; k < dstPointIndices.size(); k++) {
            if (target.second.pointOffsets.size()) {
              if (i >= target.second.pointOffsets.size()) {
                PUSH_ERROR_AND_RETURN("Invalid pointOffsets.size.");
              }
              tmpPointOffsets.push_back(target.second.pointOffsets[i]);
            }
            if (target.second.normalOffsets.size()) {
              if (i >= target.second.normalOffsets.size()) {
                PUSH_ERROR_AND_RETURN("Invalid normalOffsets.size.");
              }
              tmpNormalOffsets.push_back(target.second.normalOffsets[i]);
            }

            tmpPointIndices.push_back(dstPointIndices[k]);
          }
        }

        target.second.pointIndices.swap(tmpPointIndices);
        target.second.pointOffsets.swap(tmpPointOffsets);
        target.second.normalOffsets.swap(tmpNormalOffsets);

      }

      // TODO: Inbetween BlendShapes

    }

  }

  // Other 'facevarying' attributes are now 'vertex' variability
  if (normals_ptr) {
    mesh.normals.set_buffer(
        reinterpret_cast<const uint8_t *>(vertex_output.normals.data()),
        vertex_output.normals.size() * sizeof(value::float3));
    mesh.normals.variability = VertexVariability::Vertex;
  }

  if (texcoord0_ptr) {
    mesh.texcoords[0].set_buffer(
        reinterpret_cast<const uint8_t *>(vertex_output.uv0s.data()),
        vertex_output.uv0s.size() * sizeof(value::float2));
    mesh.texcoords[0].variability = VertexVariability::Vertex;
  }

  if (texcoord1_ptr) {
    mesh.texcoords[1].set_buffer(
        reinterpret_cast<const uint8_t *>(vertex_output.uv1s.data()),
        vertex_output.uv1s.size() * sizeof(value::float2));
    mesh.texcoords[1].variability = VertexVariability::Vertex;
  }

  if (tangents_ptr) {
    mesh.tangents.set_buffer(
        reinterpret_cast<const uint8_t *>(vertex_output.tangents.data()),
        vertex_output.tangents.size() * sizeof(value::float3));
    mesh.tangents.variability = VertexVariability::Vertex;
  }

  if (binormals_ptr) {
    mesh.binormals.set_buffer(
        reinterpret_cast<const uint8_t *>(vertex_output.binormals.data()),
        vertex_output.binormals.size() * sizeof(value::float3));
    mesh.binormals.variability = VertexVariability::Vertex;
  }

  if (colors_ptr) {
    mesh.vertex_colors.set_buffer(
        reinterpret_cast<const uint8_t *>(vertex_output.colors.data()),
        vertex_output.colors.size() * sizeof(value::float3));
    mesh.vertex_colors.variability = VertexVariability::Vertex;
  }

  if (opacities_ptr) {
    mesh.vertex_opacities.set_buffer(
        reinterpret_cast<const uint8_t *>(vertex_output.opacities.data()),
        vertex_output.opacities.size() * sizeof(float));
    mesh.vertex_opacities.variability = VertexVariability::Vertex;
  }

  if (mesh.is_triangulated()) {
    mesh.triangulatedFaceVertexIndices = std::move(out_indices);
  } else {
    mesh.usdFaceVertexIndices = std::move(out_indices);
  }


  return true;
}

bool RenderSceneConverter::BuildVertexIndicesFastImpl(RenderMesh &mesh) {
  //
  // - If mesh is triangulated, use triangulatedFaceVertexIndices, otherwise use
  // faceVertxIndices.
  // - Make vertex attributes 'facevarying' variability
  // - No similarity search.
  // - Reorder vertex attributes to 'vertex' variability.
  //

  //TUSDZ_LOG_I("BuildVertexIndicesFastImpl");

  const std::vector<uint32_t> &fvIndices =
      mesh.triangulatedFaceVertexIndices.size()
          ? mesh.triangulatedFaceVertexIndices
          : mesh.usdFaceVertexIndices;

  size_t num_verts = mesh.points.size();
  size_t num_fvs = fvIndices.size();

  if (mesh.normals.vertex_count()) {
    if (!mesh.normals.is_facevarying()) {
      PUSH_ERROR_AND_RETURN(
          "Internal error. normals must be 'facevarying' variability.");
    }
    if (mesh.normals.vertex_count() != num_fvs) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "Internal error. The number of normal items {} does not match with "
          "the number of facevarying items {}.", mesh.normals.vertex_count(), num_fvs));
    }
  }

  for (const auto &it : mesh.texcoords) {
    if (it.second.vertex_count() > 0) {
      if (!it.second.is_facevarying()) {
        PUSH_ERROR_AND_RETURN(
            "Internal error. texcoords must be 'facevarying' variability.");
      }
      if (it.second.vertex_count() != num_fvs) {
        PUSH_ERROR_AND_RETURN(
            "Internal error. The number of texcoord items does not match "
            "with the number of facevarying items.");
      }
    }
  }

  if (mesh.tangents.vertex_count()) {
    if (!mesh.tangents.is_facevarying()) {
      PUSH_ERROR_AND_RETURN(
          "Internal error. tangents must be 'facevarying' variability.");
    }
    if (mesh.tangents.vertex_count() != num_fvs) {
      PUSH_ERROR_AND_RETURN(
          "Internal error. The number of tangents items does not match "
          "with the number of facevarying items.");
    }
  }

  if (mesh.binormals.vertex_count()) {
    if (!mesh.binormals.is_facevarying()) {
      PUSH_ERROR_AND_RETURN(
          "Internal error. binormals must be 'facevarying' variability.");
    }
    if (mesh.binormals.vertex_count() != num_fvs) {
      PUSH_ERROR_AND_RETURN(
          "Internal error. The number of binormals items does not match "
          "with the number of facevarying items.");
    }
  }

  if (mesh.vertex_colors.vertex_count()) {
    if (!mesh.vertex_colors.is_facevarying()) {
      PUSH_ERROR_AND_RETURN(
          "Internal error. vertex_colors must be 'facevarying' variability.");
    }
    if (mesh.vertex_colors.vertex_count() != num_fvs) {
      PUSH_ERROR_AND_RETURN(
          "Internal error. The number of vertex_color items does not match "
          "with the number of facevarying items.");
    }
  }

  if (mesh.vertex_opacities.vertex_count()) {
    if (!mesh.vertex_opacities.is_facevarying()) {
      PUSH_ERROR_AND_RETURN(
          "Internal error. vertex_opacities must be 'facevarying' "
          "variability.");
    }
    if (mesh.vertex_colors.vertex_count() != num_fvs) {
      PUSH_ERROR_AND_RETURN(
          "Internal error. The number of vertex_opacity items does not match "
          "with the number of facevarying items.");
    }
  }

  // range check
  for (size_t i = 0; i < num_fvs; i++) {
    size_t fvi = fvIndices[i];
    if (fvi >= num_verts) {
      PUSH_ERROR("usdFaceVertexIndices.min_value: " << *std::min_element(mesh.usdFaceVertexIndices.begin(), mesh.usdFaceVertexIndices.end()) << "\n");
      PUSH_ERROR("usdFaceVertexIndices.max_value: " << *std::max_element(mesh.usdFaceVertexIndices.begin(), mesh.usdFaceVertexIndices.end()) << "\n");
      PUSH_ERROR("triangulatedFaceVertexIndices.min_value: " << *std::min_element(mesh.triangulatedFaceVertexIndices.begin(), mesh.triangulatedFaceVertexIndices.end()) << "\n");
      PUSH_ERROR("triangulatedFaceVertexIndices.max_value: " << *std::max_element(mesh.triangulatedFaceVertexIndices.begin(), mesh.triangulatedFaceVertexIndices.end()) << "\n");
      PUSH_ERROR_AND_RETURN(fmt::format(
          "Invalid faceVertexIndex {}. Must be less than {}(triangulated = {})", fvi, num_fvs, mesh.triangulatedFaceVertexIndices.size() ? "true" : "faise"));
    }
  }

  //
  // Reorder 'vertex' varying attributes(points, jointIndices/jointWeights,
  // BlendShape points, ...)
  // TODO: Preserve input order as much as possible.
  //
  {
    uint32_t numPoints = uint32_t(fvIndices.size());
    {
      // Reuse buffer to avoid repeated allocation across multiple mesh conversions
      _tmp_points_buffer.resize(numPoints);
      // TODO: Use vertex_output[i].point_index?
      for (size_t i = 0; i < fvIndices.size(); i++) {
        if (fvIndices[i] >= mesh.points.size()) {
          PUSH_ERROR_AND_RETURN("Internal error. point index out-of-range.");
        }
        _tmp_points_buffer[i] = mesh.points[fvIndices[i]];
      }
      mesh.points.swap(_tmp_points_buffer);
    }

    if (mesh.joint_and_weights.jointIndices.size()) {
      if (mesh.joint_and_weights.elementSize < 1) {
        PUSH_ERROR_AND_RETURN(
            "Internal error. Invalid elementSize in mesh.joint_and_weights.");
      }
      uint32_t elementSize = uint32_t(mesh.joint_and_weights.elementSize);
      std::vector<int> tmp_indices(size_t(numPoints) * size_t(elementSize));
      std::vector<float> tmp_weights(size_t(numPoints) * size_t(elementSize));
      for (size_t i = 0; i < fvIndices.size(); i++) {
        if ((elementSize * fvIndices[i]) >=
            mesh.joint_and_weights.jointIndices.size()) {
          PUSH_ERROR_AND_RETURN(
              "Internal error. point index exceeds jointIndices.size.");
        }
        for (size_t k = 0; k < elementSize; k++) {
          tmp_indices[size_t(elementSize) * size_t(i) + k] =
              mesh.joint_and_weights
                  .jointIndices[size_t(elementSize) * size_t(fvIndices[i]) + k];
        }

        if ((elementSize * fvIndices[i]) >=
            mesh.joint_and_weights.jointWeights.size()) {
          PUSH_ERROR_AND_RETURN(
              "Internal error. point index exceeds jointWeights.size.");
        }

        for (size_t k = 0; k < elementSize; k++) {
          tmp_weights[size_t(elementSize) * size_t(i) + k] =
              mesh.joint_and_weights
                  .jointWeights[size_t(elementSize) * size_t(fvIndices[i]) + k];
        }
      }
      mesh.joint_and_weights.jointIndices.swap(tmp_indices);
      mesh.joint_and_weights.jointWeights.swap(tmp_weights);
    }

    if (mesh.targets.size()) {
      // For BlendShape, reordering pointIndices, pointOffsets and normalOffsets is not enough.
      // Some points could be duplicated, so we need to find a mapping of org pointIdx -> pointIdx list in reordered points,
      // Then splat point attributes accordingly.

      // org pointIdx -> List of pointIdx in reordered points.
      std::unordered_map<uint32_t, std::vector<uint32_t>> pointIdxRemap;

      for (size_t i = 0; i < fvIndices.size(); i++) {
        pointIdxRemap[fvIndices[i]].push_back(uint32_t(i));
      }

      for (auto &target : mesh.targets) {

        std::vector<value::float3> tmpPointOffsets;
        std::vector<value::float3> tmpNormalOffsets;
        std::vector<uint32_t> tmpPointIndices;

        for (size_t i = 0; i < target.second.pointIndices.size(); i++) {

          uint32_t orgPointIdx = target.second.pointIndices[i];
          if (!pointIdxRemap.count(orgPointIdx)) {
            PUSH_ERROR_AND_RETURN("Invalid pointIndices value.");
          }
          const std::vector<uint32_t> &dstPointIndices = pointIdxRemap.at(orgPointIdx);

          for (size_t k = 0; k < dstPointIndices.size(); k++) {
            if (target.second.pointOffsets.size()) {
              if (i >= target.second.pointOffsets.size()) {
                PUSH_ERROR_AND_RETURN("Invalid pointOffsets.size.");
              }
              tmpPointOffsets.push_back(target.second.pointOffsets[i]);
            }
            if (target.second.normalOffsets.size()) {
              if (i >= target.second.normalOffsets.size()) {
                PUSH_ERROR_AND_RETURN("Invalid normalOffsets.size.");
              }
              tmpNormalOffsets.push_back(target.second.normalOffsets[i]);
            }

            tmpPointIndices.push_back(dstPointIndices[k]);
          }
        }

        target.second.pointIndices.swap(tmpPointIndices);
        target.second.pointOffsets.swap(tmpPointOffsets);
        target.second.normalOffsets.swap(tmpNormalOffsets);

      }

      // TODO: Inbetween BlendShapes

    }

    //TUSDZ_LOG_I("proc normal");

  }


  // Just change variability
  if (mesh.normals.vertex_count() > 0) {
      mesh.normals.variability = VertexVariability::Vertex;
  }

  for (auto &it : mesh.texcoords) {
    if (it.second.vertex_count() > 0) {
      it.second.variability = VertexVariability::Vertex;
    }
  }

  if (mesh.tangents.vertex_count() > 0) {
      mesh.tangents.variability = VertexVariability::Vertex;
  }

  if (mesh.binormals.vertex_count() > 0) {
      mesh.binormals.variability = VertexVariability::Vertex;
  }

  if (mesh.vertex_colors.vertex_count() > 0) {
      mesh.vertex_colors.variability = VertexVariability::Vertex;
  }

  if (mesh.vertex_opacities.vertex_count() > 0) {
      mesh.vertex_opacities.variability = VertexVariability::Vertex;
  }


  //TUSDZ_LOG_I("build indices");

  // TODO: omit indices.
  std::vector<uint32_t> out_indices;
  out_indices.resize(fvIndices.size());
  for (size_t i = 0; i < out_indices.size(); i++) {
    out_indices[i] = uint32_t(i);
  }

  if (mesh.is_triangulated()) {
    mesh.triangulatedFaceVertexIndices = std::move(out_indices);
  } else {
    mesh.usdFaceVertexIndices = std::move(out_indices);
  }

  //TUSDZ_LOG_I("done build indices");

  return true;
}

//
// Convert GeomCube to RenderMesh by generating tessellated geometry
//
bool RenderSceneConverter::ConvertCube(
    const RenderSceneConverterEnv &env, const Path &abs_prim_path,
    const GeomCube &cube, const MaterialPath &material_path,
    const std::map<std::string, MaterialPath> &subset_material_path_map,
    const StringAndIdMap &rmaterial_map,
    const std::vector<const tinyusdz::GeomSubset *> &material_subsets,
    const std::vector<std::pair<std::string, const tinyusdz::BlendShape *>> &blendshapes,
    RenderMesh *dstMesh) {

  // Extract cube size
  double size;
  if (!cube.size.get_value().get_scalar(&size)) {
    size = 2.0;  // Use default value if not available
  }

  // Generate cube mesh geometry
  std::vector<value::float3> points_f3;
  std::vector<int> faceVertexCounts;
  std::vector<int> faceVertexIndices;
  std::vector<value::float3> normals_f3;
  std::vector<value::float2> uvs_f2;

  GenerateCubeMesh(size, points_f3, faceVertexCounts, faceVertexIndices, normals_f3, uvs_f2);

  // Create temporary GeomMesh with generated data
  GeomMesh temp_mesh;

  // Convert points from float3 to point3f
  std::vector<value::point3f> points;
  for (const auto &p : points_f3) {
    points.push_back(value::point3f{p[0], p[1], p[2]});
  }
  temp_mesh.points.set_value(points);
  temp_mesh.faceVertexCounts.set_value(faceVertexCounts);
  temp_mesh.faceVertexIndices.set_value(faceVertexIndices);

  // Copy properties from cube
  temp_mesh.orientation = cube.orientation;
  temp_mesh.doubleSided = cube.doubleSided;

  // Set normals as face-varying primvar
  {
    std::vector<value::normal3f> normal3f_data;
    for (const auto &n : normals_f3) {
      normal3f_data.push_back(value::normal3f{n[0], n[1], n[2]});
    }
    temp_mesh.normals.set_value(normal3f_data);
  }

  // Set UVs as st primvar (face-varying)
  {
    GeomPrimvar primvar;
    primvar.set_name("st");
    primvar.set_interpolation(Interpolation::FaceVarying);
    std::vector<value::texcoord2f> uv_data;
    for (const auto &uv : uvs_f2) {
      uv_data.push_back(value::texcoord2f{uv[0], uv[1]});
    }
    primvar.set_value(uv_data);
    temp_mesh.set_primvar(primvar);
  }

  // Forward to ConvertMesh
  return ConvertMesh(env, abs_prim_path, temp_mesh, material_path,
                     subset_material_path_map, rmaterial_map,
                     material_subsets, blendshapes, dstMesh);
}

//
// Convert GeomSphere to RenderMesh by generating tessellated geometry
//
bool RenderSceneConverter::ConvertSphere(
    const RenderSceneConverterEnv &env, const Path &abs_prim_path,
    const GeomSphere &sphere, const MaterialPath &material_path,
    const std::map<std::string, MaterialPath> &subset_material_path_map,
    const StringAndIdMap &rmaterial_map,
    const std::vector<const tinyusdz::GeomSubset *> &material_subsets,
    const std::vector<std::pair<std::string, const tinyusdz::BlendShape *>> &blendshapes,
    RenderMesh *dstMesh) {

  // Extract sphere radius
  double radius;
  if (!sphere.radius.get_value().get_scalar(&radius)) {
    radius = 2.0;  // Use default value if not available
  }

  // Generate sphere mesh geometry
  // Default to icosphere with 2 subdivisions (4 divisions as per user request seems to mean subdivisions)
  std::vector<value::float3> points_f3;
  std::vector<int> faceVertexCounts;
  std::vector<int> faceVertexIndices;
  std::vector<value::float3> normals_f3;
  std::vector<value::float2> uvs_f2;

  // TODO: Make tessellation mode and subdivisions configurable via RenderSceneConverterEnv
  // For now, use icosphere with 2 subdivisions as default
  int subdivisions = 2;
  GenerateIcosphereMesh(radius, subdivisions, points_f3, faceVertexCounts, faceVertexIndices, normals_f3, uvs_f2);

  // Create temporary GeomMesh with generated data
  GeomMesh temp_mesh;

  // Convert points from float3 to point3f
  std::vector<value::point3f> points;
  for (const auto &p : points_f3) {
    points.push_back(value::point3f{p[0], p[1], p[2]});
  }
  temp_mesh.points.set_value(points);
  temp_mesh.faceVertexCounts.set_value(faceVertexCounts);
  temp_mesh.faceVertexIndices.set_value(faceVertexIndices);

  // Copy properties from sphere
  temp_mesh.orientation = sphere.orientation;
  temp_mesh.doubleSided = sphere.doubleSided;

  // Set normals as face-varying primvar
  {
    std::vector<value::normal3f> normal3f_data;
    for (const auto &n : normals_f3) {
      normal3f_data.push_back(value::normal3f{n[0], n[1], n[2]});
    }
    temp_mesh.normals.set_value(normal3f_data);
  }

  // Set UVs as st primvar (face-varying)
  {
    GeomPrimvar primvar;
    primvar.set_name("st");
    primvar.set_interpolation(Interpolation::FaceVarying);
    std::vector<value::texcoord2f> uv_data;
    for (const auto &uv : uvs_f2) {
      uv_data.push_back(value::texcoord2f{uv[0], uv[1]});
    }
    primvar.set_value(uv_data);
    temp_mesh.set_primvar(primvar);
  }

  // Forward to ConvertMesh
  return ConvertMesh(env, abs_prim_path, temp_mesh, material_path,
                     subset_material_path_map, rmaterial_map,
                     material_subsets, blendshapes, dstMesh);
}

bool RenderSceneConverter::ConvertMesh(
    const RenderSceneConverterEnv &env, const Path &abs_prim_path,
    const GeomMesh &mesh, const MaterialPath &material_path,
    const std::map<std::string, MaterialPath> &subset_material_path_map,
    // const std::map<std::string, int64_t> &rmaterial_idMap,
    const StringAndIdMap &rmaterial_map,
    const std::vector<const tinyusdz::GeomSubset *> &material_subsets,
    const std::vector<std::pair<std::string, const tinyusdz::BlendShape *>>
        &blendshapes,
    RenderMesh *dstMesh) {
  //
  // Steps:
  //
  // 1. Get points, faceVertexIndices and faceVertexOffsets at specified time.
  //   - Validate GeomSubsets
  // 2. Assign Material and list up texcoord primvars
  // 3. convert texcoord, normals, vetexcolor(displaycolors)
  //   - First try to convert it to `vertex` varying(Can be drawn with single
  //   index buffer)
  //   - Otherwise convert to `facevarying` as the last resort.
  // 4. Triangulate indices  when `triangulate` is enabled.
  //   - Triangulate texcoord, normals, vertexcolor.
  // 5. Convert Skin weights
  // 6. Convert BlendShape
  // 7. Build indices(convert 'facevarying' to 'vertrex')
  // 8. Calcualte normals(if not present in the mesh)
  // 9. Build tangent frame(for normal mapping)
  //
  //

  if (!dstMesh) {
    PUSH_ERROR_AND_RETURN("`dst` mesh pointer is nullptr");
  }

  RenderMesh dst;

  dst.is_rightHanded =
      (mesh.orientation.get_value() == tinyusdz::Orientation::RightHanded);
  dst.doubleSided = mesh.doubleSided.get_value();

  //
  // 1. Mandatory attribute: points, faceVertexCounts and faceVertexIndices.
  //
  // TODO: Make error when Mesh's indices is empty?
  //

  {
    std::vector<value::point3f> points;
    bool ret = EvaluateTypedAnimatableAttribute(
        env.stage, mesh.points, "points", &points, &_err, env.timecode,
        value::TimeSampleInterpolationType::Linear);
    if (!ret) {
      return false;
    }

    if (points.empty()) {

      // maybe points is explicitly authored, but empty.
      // point3f points = []

      dst.points.clear();
      //PUSH_ERROR_AND_RETURN(
      //    fmt::format("`points` is empty. Prim {}", abs_prim_path));

    } else {

      //if (env.mesh_config.lowmem) {
      //  auto *pmesh = const_cast<GeomMesh *>(&mesh);
      //  std::vector<value::point3f> empty;
      //  pmesh->points = empty;
      //}

      dst.points.resize(points.size());
      memcpy(dst.points.data(), points.data(),
             sizeof(value::float3) * points.size());

      std::vector<value::point3f>().swap(points);
    }

  }

  {
    std::vector<int32_t> indices;
    bool ret = EvaluateTypedAnimatableAttribute(
        env.stage, mesh.faceVertexIndices, "faceVertexIndices", &indices, &_err,
        env.timecode, value::TimeSampleInterpolationType::Held);
    if (!ret) {
      return false;
    }

    dst.usdFaceVertexIndices.reserve(indices.size());
    for (size_t i = 0; i < indices.size(); i++) {
      if (indices[i] < 0) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "faceVertexIndices[{}] contains negative index value {}.", i,
            indices[i]));
      }
      if (size_t(indices[i]) > dst.points.size()) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("faceVertexIndices[{}] {} exceeds points.size {}.", i,
                        indices[i], dst.points.size()));
      }
      dst.usdFaceVertexIndices.push_back(uint32_t(indices[i]));
    }
  }

  {
    std::vector<int> counts;
    bool ret = EvaluateTypedAnimatableAttribute(
        env.stage, mesh.faceVertexCounts, "faceVertexCounts", &counts, &_err,
        env.timecode, value::TimeSampleInterpolationType::Held);
    if (!ret) {
      return false;
    }

    size_t sumCounts = 0;
    dst.usdFaceVertexCounts.clear();
    dst.usdFaceVertexCounts.reserve(counts.size());
    for (size_t i = 0; i < counts.size(); i++) {
      if (counts[i] < 3) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("faceVertexCounts[{}] contains invalid value {}. The "
                        "count value must be >= 3",
                        i, counts[i]));
      }

      if ((sumCounts + size_t(counts[i])) > dst.usdFaceVertexIndices.size()) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "faceVertexCounts[{}] exceeds faceVertexIndices.size {}.", i,
            dst.usdFaceVertexIndices.size()));
      }
      dst.usdFaceVertexCounts.push_back(uint32_t(counts[i]));
      sumCounts += size_t(counts[i]);
    }
  }


  //
  // 2. bindMaterial GeoMesh and GeomSubset.
  //
  // Assume Material conversion is done before ConvertMesh.
  // Here we only assign RenderMaterial id and extract GeomSubset::indices
  // information.
  //

  DCOUT("rmaterial_ap.size " << rmaterial_map.size());
  if (rmaterial_map.count(material_path.material_path)) {
    dst.material_id = int(rmaterial_map.at(material_path.material_path));
  }

  if (rmaterial_map.count(material_path.backface_material_path)) {
    dst.backface_material_id =
        int(rmaterial_map.at(material_path.backface_material_path));
  }

  if (env.mesh_config.validate_geomsubset) {
    size_t elementCount = dst.usdFaceVertexCounts.size();

    if (material_subsets.size() &&
        mesh.subsetFamilyTypeMap.count(value::token("materialBind"))) {
      const GeomSubset::FamilyType familyType =
          mesh.subsetFamilyTypeMap.at(value::token("materialBind"));
      if (!GeomSubset::ValidateSubsets(material_subsets, elementCount,
                                       familyType, &_err)) {
        PUSH_ERROR_AND_RETURN("GeomSubset validation failed.");
      }
    }
  }

  for (const auto &psubset : material_subsets) {
    MaterialSubset ms;
    ms.prim_name = psubset->name;
    // ms.prim_index = // TODO
    ms.abs_path = abs_prim_path.prim_part() + std::string("/") + psubset->name;
    ms.display_name = psubset->meta.has_displayName() ? psubset->meta.get_displayName() : "";

    // TODO: Raise error when indices is empty?
    if (psubset->indices.authored()) {
      std::vector<int> indices;  // index to faceVertexCounts
      bool ret = EvaluateTypedAnimatableAttribute(
          env.stage, psubset->indices, "indices", &indices, &_err, env.timecode,
          value::TimeSampleInterpolationType::Held);
      if (!ret) {
        return false;
      }

      ms.usdIndices = indices;
    }

    if (subset_material_path_map.count(psubset->name)) {
      const auto &mp = subset_material_path_map.at(psubset->name);
      if (rmaterial_map.count(mp.material_path)) {
        ms.material_id = int(rmaterial_map.at(mp.material_path));
        DCOUT("MaterialSubset " << psubset->name << " : material_id "
                                << ms.material_id);
      }
      if (rmaterial_map.count(mp.backface_material_path)) {
        ms.backface_material_id =
            int(rmaterial_map.at(mp.backface_material_path));
        DCOUT("MaterialSubset " << psubset->name << " : backface_material_id "
                                << ms.backface_material_id);
      }
    }

    // TODO: Ensure prim_name is unique.
    dst.material_subsetMap[ms.prim_name] = ms;
  }

  uint32_t num_vertices = uint32_t(dst.points.size());
  uint32_t num_faces = uint32_t(dst.usdFaceVertexCounts.size());
  uint32_t num_face_vertex_indices = uint32_t(dst.usdFaceVertexIndices.size());

  //
  // List up texcoords in this mesh.
  // - If no material assigned to this mesh, look into
  // `default_texcoords_primvar_name`
  // - If materials are assigned, find all corresponding UV primvars in this
  // mesh.
  //

  // key:slotId, value:texcoord data
  std::unordered_map<uint32_t, VertexAttribute> uvAttrs;

  // We need Material info to get corresponding primvar name.
  if (rmaterial_map.empty()) {
    // No material assigned to the Mesh, but we may still want texcoords solely(
    // assign material after the conversion)
    // So find a primvar whose name matches default texcoord name.
    if (mesh.has_primvar(env.mesh_config.default_texcoords_primvar_name)) {
      DCOUT("uv primvar  with default_texcoords_primvar_name found.");
      auto ret = GetTextureCoordinate(
          env.stage, mesh, env.mesh_config.default_texcoords_primvar_name,
          env.timecode, env.tinterp);
      if (ret) {
        //TUSDZ_LOG_I("uv attr");

        // Use slotId 0 - use move to avoid copy
        uvAttrs[0] = std::move(ret.value());
      } else {
        PUSH_WARN("Failed to get texture coordinate for `"
                  << env.mesh_config.default_texcoords_primvar_name
                  << "` : " << ret.error());
      }
    }
  } else {
    for (auto mit = rmaterial_map.i_begin(); mit != rmaterial_map.i_end();
         mit++) {
      int64_t rmaterial_id = int64_t(mit->first);

      if ((rmaterial_id > -1) && (size_t(rmaterial_id) < materials.size())) {
        const RenderMaterial &material = materials[size_t(rmaterial_id)];

        // Cache ListUVNames per material_id to avoid redundant shader walks
        auto uv_cache_it = _uvNameCache.find(rmaterial_id);
        if (uv_cache_it == _uvNameCache.end()) {
          StringAndIdMap tmp;
          if (!ListUVNames(material, textures, tmp)) {
            DCOUT("Failed to list UV names");
            return false;
          }
          uv_cache_it = _uvNameCache.emplace(rmaterial_id, std::move(tmp)).first;
        }
        const StringAndIdMap &uvname_map = uv_cache_it->second;

        for (auto it = uvname_map.i_begin(); it != uvname_map.i_end(); it++) {
          uint64_t slotId = it->first;
          std::string uvname = it->second;

          if (!uvAttrs.count(uint32_t(slotId))) {
            // FIXME: Use GetGeomPrimvar() & ToVertexAttribute()
            auto ret = GetTextureCoordinate(env.stage, mesh, uvname,
                                            env.timecode, env.tinterp);
            if (ret) {
              VertexAttribute &vattr = ret.value();

              if (vattr.is_vertex()) {
                if (vattr.vertex_count() != num_vertices) {
                  PUSH_ERROR_AND_RETURN(fmt::format("Array length of texture coordinate `{}`(Prim path {}) must be {}, but got {}", uvname, abs_prim_path.prim_part(), num_vertices, vattr.vertex_count()));
                }
              } else if (vattr.is_constant()) {
                if (vattr.vertex_count() != 1) {
                  PUSH_ERROR_AND_RETURN(fmt::format("Array length of texture coordinate `{}`(Prim path {}) must be {}, but got {}", uvname, abs_prim_path.prim_part(), 1, vattr.vertex_count()));
                }
              } else if (vattr.is_uniform()) {
                if (vattr.vertex_count() != num_faces) {
                  PUSH_ERROR_AND_RETURN(fmt::format("Array length of texture coordinate `{}`(Prim path {}) must be {}, but got {}", uvname, abs_prim_path.prim_part(), num_faces, vattr.vertex_count()));
                }
              } else if (vattr.is_facevarying()) {
                if (vattr.vertex_count() != num_face_vertex_indices) {
                  PUSH_ERROR_AND_RETURN(fmt::format("Array length of texture coordinate `{}`(Prim path {}) must be {}, but got {}", uvname, abs_prim_path.prim_part(), num_face_vertex_indices, vattr.vertex_count()));
                }
              } else {
                PUSH_ERROR_AND_RETURN("Internal error. Unknown variability of texcoord attribute.");
                return false;
              }

              // Use move to avoid copy
              uvAttrs[uint32_t(slotId)] = std::move(vattr);
            } else {
              PUSH_WARN("Failed to get texture coordinate for `"
                        << uvname << "` : " << ret.error());
            }
          }
        }
      }
    }
  }

  //TUSDZ_LOG_I("done uvAttr");

  if (mesh.has_primvar(env.mesh_config.default_tangents_primvar_name)) {
    GeomPrimvar pvar;

    if (!GetGeomPrimvar(env.stage, &mesh,
                        env.mesh_config.default_tangents_primvar_name, &pvar,
                        &_err)) {
      return false;
    }

    std::string warn_msg;
    if (!ToVertexAttribute(pvar, env.mesh_config.default_tangents_primvar_name,
                           num_vertices, num_faces, num_face_vertex_indices,
                           dst.tangents, &_err, env.timecode, env.tinterp,
                           &warn_msg)) {
      return false;
    }
    if (!warn_msg.empty()) {
      _warn += warn_msg;
    }
  }

  if (mesh.has_primvar(env.mesh_config.default_binormals_primvar_name)) {
    GeomPrimvar pvar;

    if (!GetGeomPrimvar(env.stage, &mesh,
                        env.mesh_config.default_binormals_primvar_name, &pvar,
                        &_err)) {
      return false;
    }

    std::string warn_msg;
    if (!ToVertexAttribute(pvar, env.mesh_config.default_binormals_primvar_name,
                           num_vertices, num_faces, num_face_vertex_indices,
                           dst.binormals, &_err, env.timecode, env.tinterp,
                           &warn_msg)) {
      return false;
    }
    if (!warn_msg.empty()) {
      _warn += warn_msg;
    }
  }

  constexpr auto kDisplayColor = "displayColor";
  if (mesh.has_primvar(kDisplayColor)) {
    GeomPrimvar pvar;

    if (!GetGeomPrimvar(env.stage, &mesh, kDisplayColor, &pvar, &_err)) {
      return false;
    }

    VertexAttribute vcolor;
    std::string warn_msg;
    if (!ToVertexAttribute(pvar, kDisplayColor, num_vertices, num_faces,
                           num_face_vertex_indices, vcolor, &_err, env.timecode,
                           env.tinterp, &warn_msg)) {
      return false;
    }
    if (!warn_msg.empty()) {
      _warn += warn_msg;
    }

    if ((vcolor.elementSize == 1) && (vcolor.vertex_count() == 1) &&
        (vcolor.stride_bytes() == 3 * 4)) {
      memcpy(&dst.displayColor, vcolor.data.data(), vcolor.stride_bytes());
    } else {
      dst.vertex_colors = vcolor;
    }
  }

  constexpr auto kDisplayOpacity = "displayOpacity";
  if (mesh.has_primvar(kDisplayOpacity)) {
    GeomPrimvar pvar;
    if (!GetGeomPrimvar(env.stage, &mesh, kDisplayOpacity, &pvar, &_err)) {
      return false;
    }

    VertexAttribute vopacity;
    std::string warn_msg;
    if (!ToVertexAttribute(pvar, kDisplayOpacity, num_vertices, num_faces,
                           num_face_vertex_indices, vopacity, &_err,
                           env.timecode, env.tinterp, &warn_msg)) {
      return false;
    }
    if (!warn_msg.empty()) {
      _warn += warn_msg;
    }

    if ((vopacity.elementSize == 1) && (vopacity.vertex_count() == 1) &&
        (vopacity.stride_bytes() == 4)) {
      memcpy(&dst.displayOpacity, vopacity.data.data(),
             vopacity.stride_bytes());
    } else {
      dst.vertex_opacities = vopacity;
    }
  }



  //
  // Check if the Mesh can be drawn with single index buffer during converting
  // normals/texcoords/displayColors/displayOpacities, since OpenGL and Vulkan
  // does not support drawing a primitive with multiple index buffers.
  //
  // If the Mesh contains any face-varying attribute,
  // First try to convert it 'vertex' variabily, if it fails, all attribute are
  // converted to face-varying so that the Mesh can be drawn without index
  // buffer. This will hurt the performance of rendering in OpenGL/Vulkan,
  // especially when the Mesh is animated with skinning.
  //
  // We leave user-defined primvar as-is, so no check for it.
  //
  bool is_single_indexable{true};

  //
  // Convert normals
  //
  {
    Interpolation interp = mesh.get_normalsInterpolation();
    std::vector<value::normal3f> normals;

    if (mesh.has_primvar("normals")) {  // primvars:normals
      GeomPrimvar pvar;
      if (!GetGeomPrimvar(env.stage, &mesh, "normals", &pvar, &_err)) {
        return false;
      }

      // Check if normals primvar has timesamples
      const tinyusdz::Attribute &normals_attr = pvar.get_attribute();
      if (normals_attr.has_timesamples()) {
        std::string msg = fmt::format(
            "Geometry primvar 'normals' has timesamples (animated values). "
            "RenderMesh conversion uses value at specified time (timecode={}). "
            "To capture animation, you need to convert at multiple timesamples. "
            "Consider using ConvertMesh() at each timeframe or implementing "
            "per-frame conversion. Animated normals are particularly important "
            "for correct shading and normal mapping.",
            env.timecode);
        _warn += msg + "\n";
        DCOUT("WARN: " << msg);
      }

      if (!pvar.flatten_with_indices(env.timecode, &normals, env.tinterp,
                                     &_err)) {
        PUSH_ERROR_AND_RETURN("Failed to expand `normals` primvar.");
      }

    } else if (mesh.normals.authored()) {  // look 'normals'
      if (!EvaluateTypedAnimatableAttribute(env.stage, mesh.normals, "normals",
                                            &normals, &_err, env.timecode,
                                            env.tinterp)) {
      }
    }

    dst.normals.get_data().resize(normals.size() * sizeof(value::normal3f));
    memcpy(dst.normals.get_data().data(), normals.data(),
           normals.size() * sizeof(value::normal3f));
    dst.normals.elementSize = 1;
    dst.normals.stride = sizeof(value::normal3f);
    dst.normals.format = VertexAttributeFormat::Vec3;

    if (interp == Interpolation::Varying) {
      dst.normals.variability = VertexVariability::Varying;
    } else if (interp == Interpolation::Constant) {
      dst.normals.variability = VertexVariability::Constant;
    } else if (interp == Interpolation::Uniform) {
      dst.normals.variability = VertexVariability::Uniform;
    } else if (interp == Interpolation::Vertex) {
      dst.normals.variability = VertexVariability::Vertex;
    } else if (interp == Interpolation::FaceVarying) {
      dst.normals.variability = VertexVariability::FaceVarying;
    } else {
      PUSH_ERROR_AND_RETURN(
          "[Internal Error] Invalid interpolation value for normals.");
    }
    dst.normals.indices.clear();
    dst.normals.name = "normals";

    if (is_single_indexable &&
        (dst.normals.variability == VertexVariability::FaceVarying)) {
      VertexAttribute va_normals;
      if (TryConvertFacevaryingToVertex(
              dst.normals, &va_normals, dst.usdFaceVertexIndices, &_warn,
              env.mesh_config.facevarying_to_vertex_eps)) {
        DCOUT("normals is converted to 'vertex' varying.");
        dst.normals = std::move(va_normals);
      } else {
        DCOUT(
            "normals cannot be converted to 'vertex' varying. Staying "
            "'facevarying'");
        DCOUT("warn = " << _warn);
        is_single_indexable = false;
      }
    }
  }

  //
  // Convert UVs
  //

  for (auto &it : uvAttrs) {
    uint64_t slotId = it.first;
    VertexAttribute &vattr = it.second;

    if (vattr.format != VertexAttributeFormat::Vec2) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Texcoord VertexAttribute must be Vec2 type.\n"));
    }

    if (vattr.element_size() != 1) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("elementSize must be 1 for Texcoord attribute."));
    }

    DCOUT("Add texcoord attr `" << vattr.name << "` to slot Id " << slotId);

    if (is_single_indexable &&
        (vattr.variability == VertexVariability::FaceVarying)) {
      VertexAttribute va_uvs;
      if (TryConvertFacevaryingToVertex(
              vattr, &va_uvs, dst.usdFaceVertexIndices, &_warn,
              env.mesh_config.facevarying_to_vertex_eps)) {
        DCOUT("texcoord[" << slotId << "] is converted to 'vertex' varying.");
        dst.texcoords[uint32_t(slotId)] = std::move(va_uvs);
      } else {
        DCOUT("texcoord[" << slotId
                          << "] cannot be converted to 'vertex' varying. "
                             "Staying 'facevarying'");
        is_single_indexable = false;
        dst.texcoords[uint32_t(slotId)] = std::move(vattr);
      }
    } else {
      dst.texcoords[uint32_t(slotId)] = std::move(vattr);
    }
  }

  if (dst.vertex_colors.vertex_count() > 1) {
    // Use const reference instead of copy for validation
    const VertexAttribute &vattr = dst.vertex_colors;

    if (vattr.format != VertexAttributeFormat::Vec3) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Color VertexAttribute must be Vec3 type.\n"));
    }

    if (vattr.element_size() != 1) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("elementSize = 1 expected for VertexColor, but got {}",
                      vattr.element_size()));
    }

    if (is_single_indexable &&
        (vattr.variability == VertexVariability::FaceVarying)) {
      VertexAttribute va;
      if (TryConvertFacevaryingToVertex(
              dst.vertex_colors, &va, dst.usdFaceVertexIndices, &_warn,
              env.mesh_config.facevarying_to_vertex_eps)) {
        dst.vertex_colors = std::move(va);
      } else {
        DCOUT(
            "vertex_colors cannot be converted to 'vertex' varying. Staying "
            "'facevarying'");
        is_single_indexable = false;
      }
    }
  }

  if (dst.vertex_opacities.vertex_count() > 1) {
    // Use const reference instead of copy for validation
    const VertexAttribute &vattr = dst.vertex_opacities;

    if (vattr.format != VertexAttributeFormat::Float) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Opacity VertexAttribute must be Float type.\n"));
    }

    if (vattr.element_size() != 1) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("elementSize = 1 expected for VertexOpacity, but got {}",
                      vattr.element_size()));
    }

    if (is_single_indexable &&
        (vattr.variability == VertexVariability::FaceVarying)) {
      VertexAttribute va;
      if (TryConvertFacevaryingToVertex(
              dst.vertex_opacities, &va, dst.usdFaceVertexIndices, &_warn,
              env.mesh_config.facevarying_to_vertex_eps)) {
        dst.vertex_opacities = std::move(va);
      } else {
        DCOUT(
            "vertex_opacities cannot be converted to 'vertex' varying. Staying "
            "'facevarying'");
        is_single_indexable = false;
      }
    }
  }

  DCOUT(mesh.name << " : is_single_indexable = " << is_single_indexable);

  //
  // Convert built-in vertex attributes to either 'vertex' or 'facevarying'
  //
  {
    if (!ConvertVertexVariabilityImpl(dst.normals, is_single_indexable,
                                      dst.usdFaceVertexCounts,
                                      dst.usdFaceVertexIndices)) {
      return false;
    }
    for (auto &it : dst.texcoords) {
      if (!ConvertVertexVariabilityImpl(it.second, is_single_indexable,
                                        dst.usdFaceVertexCounts,
                                        dst.usdFaceVertexIndices)) {
        return false;
      }
    }

    if (!ConvertVertexVariabilityImpl(dst.vertex_colors, is_single_indexable,
                                      dst.usdFaceVertexCounts,
                                      dst.usdFaceVertexIndices)) {
      return false;
    }
    if (!ConvertVertexVariabilityImpl(dst.vertex_opacities, is_single_indexable,
                                      dst.usdFaceVertexCounts,
                                      dst.usdFaceVertexIndices)) {
      return false;
    }
  }


  ///
  /// 4. Triangulate
  ///  - triangulate faceVertexCounts, faceVertexIndices
  ///  - Remap faceIndex in MaterialSubset(GeomSubset).
  ///  - Triangulate vertex attributes(normals, uvcoords, vertex
  ///  colors/opacities).
  ///
  bool triangulate = env.mesh_config.triangulate;
  if (triangulate) {
    DCOUT("Triangulate mesh");
    std::vector<uint32_t> triangulatedFaceVertexCounts;  // should be all 3's
    std::vector<uint32_t> triangulatedFaceVertexIndices;
    std::vector<size_t>
        triangulatedToOrigFaceVertexIndexMap;  // used for rearrange facevertex
                                               // attrib
    std::vector<uint32_t>
        triangulatedFaceCounts;  // used for rearrange face indices(e.g
                                 // GeomSubset indices)

    std::string err;

    if (!TriangulatePolygon<value::float3, float>(
            dst.points, dst.usdFaceVertexCounts, dst.usdFaceVertexIndices,
            triangulatedFaceVertexCounts, triangulatedFaceVertexIndices,
            triangulatedToOrigFaceVertexIndexMap, triangulatedFaceCounts,
            env.mesh_config.triangulation_method,
            _warn, err)) {
      PUSH_ERROR_AND_RETURN("Triangulation failed: " + err);
    }

    if (dst.material_subsetMap.size()) {
      // Remap faceId in GeomSubsets

      //
      // size: len(triangulatedFaceCounts)
      // value: array index in triangulatedFaceVertexCounts
      // Up to 4GB faces.
      //
      std::vector<uint32_t> faceIndexOffsets;
      faceIndexOffsets.resize(triangulatedFaceCounts.size());

      size_t faceIndexOffset = 0;
      for (size_t i = 0; i < triangulatedFaceCounts.size(); i++) {
        size_t ncount = triangulatedFaceCounts[i];

        faceIndexOffsets[i] = uint32_t(faceIndexOffset);
        // DCOUT("faceIndexOffset[" << i << "] = " << faceIndexOffsets[i]);

        faceIndexOffset += ncount;

        if (faceIndexOffset >= std::numeric_limits<uint32_t>::max()) {
          PUSH_ERROR_AND_RETURN("Triangulated Mesh contains 4G or more faces.");
        }
      }

      // Remap indices in MaterialSubset
      //
      // example:
      //
      // faceVertexCounts = [4, 4]
      // faceVertexIndices = [0, 1, 2, 3, 4, 5, 6, 7]
      //
      // triangulatedFaceVertexCounts = [3, 3, 3, 3]
      // triangulatedFaceVertexIndices = [0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7]
      // triangulatedFaceCounts = [2, 2]
      //
      // geomsubset.indices = [0, 1] # index to faceVertexCounts
      // faceIndexOffsets = [0, 2]
      //
      // => triangulated geomsubset.indices = [0, 1, 2, 3] # index to
      // triangulatedFaceVertexCounts
      //
      //
      for (auto &it : dst.material_subsetMap) {
        std::vector<int> triangulated_indices;

        for (size_t i = 0; i < it.second.usdIndices.size(); i++) {
          int32_t srcIndex = it.second.usdIndices[i];
          if (srcIndex < 0) {
            PUSH_ERROR_AND_RETURN("Invalid index value in GeomSubset.");
          }

          uint32_t baseFaceIndex = faceIndexOffsets[size_t(srcIndex)];
          // DCOUT(i << ", baseFaceIndex = " << baseFaceIndex);

          for (size_t k = 0; k < triangulatedFaceCounts[uint32_t(srcIndex)];
               k++) {
            if ((baseFaceIndex + k) > size_t((std::numeric_limits<int32_t>::max)())) {
              PUSH_ERROR_AND_RETURN(fmt::format("Index value exceeds 2GB."));
            }
            // assume triangulated faceIndex in each polygon is monotonically
            // increasing.
            triangulated_indices.push_back(int(baseFaceIndex + k));
          }
        }

        it.second.triangulatedIndices = std::move(triangulated_indices);
      }
    }

    //
    // Triangulate built-in vertex attributes.
    //
    {
      if (!TriangulateVertexAttribute(dst.normals, dst.usdFaceVertexCounts,
                                      triangulatedToOrigFaceVertexIndexMap,
                                      triangulatedFaceCounts,
                                      triangulatedFaceVertexIndices, &_err)) {
        PUSH_ERROR_AND_RETURN("Failed to triangulate normals attribute.");
      }

      if (!TriangulateVertexAttribute(dst.tangents, dst.usdFaceVertexCounts,
                                      triangulatedToOrigFaceVertexIndexMap,
                                      triangulatedFaceCounts,
                                      triangulatedFaceVertexIndices, &_err)) {
        PUSH_ERROR_AND_RETURN("Failed to triangulate tangents attribute.");
      }

      if (!TriangulateVertexAttribute(dst.binormals, dst.usdFaceVertexCounts,
                                      triangulatedToOrigFaceVertexIndexMap,
                                      triangulatedFaceCounts,
                                      triangulatedFaceVertexIndices, &_err)) {
        PUSH_ERROR_AND_RETURN("Failed to triangulate binormals attribute.");
      }

      for (auto &it : dst.texcoords) {
        if (!TriangulateVertexAttribute(it.second, dst.usdFaceVertexCounts,
                                        triangulatedToOrigFaceVertexIndexMap,
                                        triangulatedFaceCounts,
                                        triangulatedFaceVertexIndices, &_err)) {
          PUSH_ERROR_AND_RETURN(fmt::format(
              "Failed to triangulate texcoords[{}] attribute.", it.first));
        }
      }

      if (!TriangulateVertexAttribute(
              dst.vertex_colors, dst.usdFaceVertexCounts,
              triangulatedToOrigFaceVertexIndexMap, triangulatedFaceCounts,
              triangulatedFaceVertexIndices, &_err)) {
        PUSH_ERROR_AND_RETURN("Failed to triangulate vertex_colors attribute.");
      }

      if (!TriangulateVertexAttribute(
              dst.vertex_opacities, dst.usdFaceVertexCounts,
              triangulatedToOrigFaceVertexIndexMap, triangulatedFaceCounts,
              triangulatedFaceVertexIndices, &_err)) {
        PUSH_ERROR_AND_RETURN(
            "Failed to triangulate vertopacitiesex_colors attribute.");
      }
    }

    dst.triangulatedFaceVertexCounts = std::move(triangulatedFaceVertexCounts);
    dst.triangulatedFaceVertexIndices =
        std::move(triangulatedFaceVertexIndices);

    dst.triangulatedToOrigFaceVertexIndexMap =
        std::move(triangulatedToOrigFaceVertexIndexMap);
    dst.triangulatedFaceCounts = std::move(triangulatedFaceCounts);
  }


  //
  // 5. Vertex skin weights(jointIndex and jointWeights)
  //
  if (mesh.has_primvar("skel:jointIndices") &&
      mesh.has_primvar("skel:jointWeights")) {
    DCOUT("Convert skin weights");
    GeomPrimvar jointIndices;
    GeomPrimvar jointWeights;

    if (!GetGeomPrimvar(env.stage, &mesh, "skel:jointIndices", &jointIndices,
                        &_err)) {
      return false;
    }

    if (!GetGeomPrimvar(env.stage, &mesh, "skel:jointWeights", &jointWeights,
                        &_err)) {
      return false;
    }

    // interpolation must be 'vertex'
    if (!jointIndices.has_interpolation()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("`skel:jointIndices` primvar must author `interpolation` "
                      "metadata(and set it to `vertex`)"));
    }

    // TODO: Disallow Varying?
    if ((jointIndices.get_interpolation() != Interpolation::Vertex) &&
        (jointIndices.get_interpolation() != Interpolation::Varying)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("`skel:jointIndices` primvar must use `vertex` for "
                      "`interpolation` metadata, but got `{}`.",
                      to_string(jointIndices.get_interpolation())));
    }

    if (!jointWeights.has_interpolation()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("`skel:jointWeights` primvar must author `interpolation` "
                      "metadata(and set it to `vertex`)"));
    }

    // TODO: Disallow Varying?
    if ((jointWeights.get_interpolation() != Interpolation::Vertex) &&
        (jointWeights.get_interpolation() != Interpolation::Varying)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("`skel:jointWeights` primvar must use `vertex` for "
                      "`interpolation` metadata, but got `{}`.",
                      to_string(jointWeights.get_interpolation())));
    }

    uint32_t jointIndicesElementSize = jointIndices.get_elementSize();
    uint32_t jointWeightsElementSize = jointWeights.get_elementSize();

    if (jointIndicesElementSize == 0) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "`elementSize` metadata of `skel:jointIndices` is zero."));
    }

    if (jointWeightsElementSize == 0) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "`elementSize` metadata of `skel:jointWeights` is zero."));
    }

    if (jointIndicesElementSize > env.mesh_config.max_skin_elementSize) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "`elementSize` {} of `skel:jointIndices` too large. Max allowed is "
          "set to {}",
          jointIndicesElementSize, env.mesh_config.max_skin_elementSize));
    }

    if (jointWeightsElementSize > env.mesh_config.max_skin_elementSize) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "`elementSize` {} of `skel:jointWeights` too large. Max allowed is "
          "set to {}",
          jointWeightsElementSize, env.mesh_config.max_skin_elementSize));
    }

    if (jointIndicesElementSize != jointWeightsElementSize) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("`elementSize` {} of `skel:jointIndices` must equal to "
                      "`elementSize` {} of `skel:jointWeights`",
                      jointIndicesElementSize, jointWeightsElementSize));
    }

    std::vector<int> jointIndicesArray;
    if (!jointIndices.flatten_with_indices(env.timecode, &jointIndicesArray,
                                           env.tinterp)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Failed to flatten Indexed Primvar `skel:jointIndices`. "
                      "Ensure `skel:jointIndices` is type `int[]`"));
    }

    std::vector<float> jointWeightsArray;
    if (!jointWeights.flatten_with_indices(env.timecode, &jointWeightsArray,
                                           env.tinterp)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Failed to flatten Indexed Primvar `skel:jointWeights`. "
                      "Ensure `skel:jointWeights` is type `float[]`"));
    }

    if (jointIndicesArray.size() != jointWeightsArray.size()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("`skel:jointIndices` nitems {} must be equal to "
                      "`skel:jointWeights` ntems {}",
                      jointIndicesArray.size(), jointWeightsArray.size()));
    }

    if (jointIndicesArray.empty()) {
      PUSH_ERROR_AND_RETURN(fmt::format("`skel:jointIndices` is empty array."));
    }

    // TODO: Validate jointIndex.

    dst.joint_and_weights.jointIndices = jointIndicesArray;
    dst.joint_and_weights.jointWeights = jointWeightsArray;
    dst.joint_and_weights.elementSize = int(jointIndicesElementSize);

    // Helper lambda to round up bone count to standard GPU skinning values
    auto roundBoneCountUp = [](uint32_t count) -> uint32_t {
      // Standard GPU skinning bone counts: 4, 8, 16, 32, 48, 64, 80, 96, 128
      const uint32_t standardCounts[] = {4, 8, 16, 32, 48, 64, 80, 96, 128};
      for (uint32_t stdCount : standardCounts) {
        if (count <= stdCount) {
          return stdCount;
        }
      }
      return 128; // Max supported
    };

    // Apply bone reduction if enabled
    if (env.mesh_config.enable_bone_reduction &&
        (env.mesh_config.target_bone_count < jointIndicesElementSize)) {
      uint32_t numVertices = uint32_t(jointIndicesArray.size() / jointIndicesElementSize);

      DCOUT("Reducing bone influences from " << jointIndicesElementSize
            << " to " << env.mesh_config.target_bone_count
            << " per vertex (" << numVertices << " vertices)");

      // Configure bone reduction with advanced settings
      BoneReductionConfig bone_config;
      bone_config.target_bone_count = env.mesh_config.target_bone_count;
      bone_config.strategy = BoneReductionStrategy::ErrorMetric; // Use error-aware reduction
      bone_config.min_weight_threshold = 0.001f; // Ignore very small weights
      bone_config.error_tolerance = 0.5f;
      bone_config.normalize_weights = true;

      // TODO: Pass skeleton hierarchy info if available for better reduction quality
      // For now, use nullptr (hierarchy-agnostic reduction)
      BoneHierarchyInfo *hierarchy_info = nullptr;
      BoneReductionStats reduction_stats;

      if (!ReduceBoneInfluences(
              dst.joint_and_weights.jointIndices,
              dst.joint_and_weights.jointWeights,
              jointIndicesElementSize,
              numVertices,
              bone_config,
              hierarchy_info,
              &reduction_stats)) {
        PUSH_WARN("Bone reduction failed, using original bone influences.");
      } else {
        // Update elementSize to reflect reduced bone count
        dst.joint_and_weights.elementSize = int(env.mesh_config.target_bone_count);
        DCOUT("Bone reduction complete. New elementSize: " << dst.joint_and_weights.elementSize);
        DCOUT("  Modified vertices: " << reduction_stats.num_vertices_modified << " / " << numVertices);
        DCOUT("  Avg weight error: " << reduction_stats.avg_weight_error);
        DCOUT("  Max weight error: " << reduction_stats.max_weight_error);
      }
    }
    // Round bone count without reduction (pad with zeros)
    else if (env.mesh_config.round_bone_count && !env.mesh_config.enable_bone_reduction) {
      uint32_t currentElementSize = jointIndicesElementSize;
      uint32_t roundedElementSize = roundBoneCountUp(currentElementSize);

      if (roundedElementSize > currentElementSize) {
        uint32_t numVertices = uint32_t(jointIndicesArray.size() / jointIndicesElementSize);

        DCOUT("Rounding bone count from " << currentElementSize
              << " to " << roundedElementSize
              << " per vertex (" << numVertices << " vertices)");

        // Create new arrays with padded size
        std::vector<int32_t> paddedIndices(numVertices * roundedElementSize, 0);
        std::vector<float> paddedWeights(numVertices * roundedElementSize, 0.0f);

        // Copy existing data and pad with zeros
        for (uint32_t v = 0; v < numVertices; v++) {
          for (uint32_t j = 0; j < currentElementSize; j++) {
            uint32_t srcIdx = v * currentElementSize + j;
            uint32_t dstIdx = v * roundedElementSize + j;
            paddedIndices[dstIdx] = dst.joint_and_weights.jointIndices[srcIdx];
            paddedWeights[dstIdx] = dst.joint_and_weights.jointWeights[srcIdx];
          }
          // Remaining slots are already zero-initialized
        }

        dst.joint_and_weights.jointIndices = std::move(paddedIndices);
        dst.joint_and_weights.jointWeights = std::move(paddedWeights);
        dst.joint_and_weights.elementSize = int(roundedElementSize);

        DCOUT("Bone count rounded. New elementSize: " << dst.joint_and_weights.elementSize);
      }
    }

    // Skeleton binding: first try explicit relationship, then fallback to ancestor discovery
    {
      Path skelPath;
      bool hasSkelPath = false;

      // First try explicit skel:skeleton relationship
      if (mesh.skeleton.has_value()) {
        DCOUT("Convert Skeleton (explicit relationship)");

        if (mesh.skeleton.value().is_path()) {
          skelPath = mesh.skeleton.value().targetPath;
          hasSkelPath = true;
        } else if (mesh.skeleton.value().is_pathvector()) {
          // Use the first one
          if (mesh.skeleton.value().targetPathVector.size()) {
            skelPath = mesh.skeleton.value().targetPathVector[0];
            hasSkelPath = true;
          } else {
            PUSH_WARN("`skel:skeleton` has invalid definition.");
          }
        } else {
          PUSH_WARN("`skel:skeleton` has invalid definition.");
        }
      }

      // Fallback: find skeleton by ancestor if mesh has skinning data but no explicit binding
      const Skeleton *discoveredSkelPtr{nullptr};
      if (!hasSkelPath && !dst.joint_and_weights.jointIndices.empty()) {
        DCOUT("Mesh has skinning data but no skel:skeleton - trying ancestor discovery");
        if (_allSkeletons && _allSkelRoots) {
          Path meshPath(abs_prim_path.full_path_name(), "");
          if (SkelRootSkeletonResolver::FindByAncestor(
                  meshPath, *_allSkeletons, *_allSkelRoots,
                  &_skelRootToSkeleton, &skelPath, &discoveredSkelPtr)) {
            hasSkelPath = true;
            DCOUT("Found skeleton by ancestor: " << skelPath.prim_part());
          } else {
            PUSH_WARN("Mesh has skinning data but no skeleton found: " + abs_prim_path.full_path_name());
          }
        }
      }

      if (hasSkelPath && skelPath.is_valid()) {
        // Check if skeleton already exists via O(1) hash lookup
        std::string skelPathStr = skelPath.prim_part();
        auto skel_cache_it = _skelPathToIndex.find(skelPathStr);

        if (skel_cache_it != _skelPathToIndex.end()) {
          // Skeleton already converted, reuse it
          dst.skel_id = skel_cache_it->second;
        } else {
          int32_t skel_id = int32_t(skeletons.size());

          SkelHierarchy skel;

          // Use ConvertSkeletonFromPtr if we have the skeleton pointer from discovery,
          // otherwise use ConvertSkeletonImplWithPath for explicit relationship case
          if (discoveredSkelPtr) {
            // Extract prim name from path (last component)
            std::string primName = skelPath.prim_part();
            size_t lastSlash = primName.rfind('/');
            if (lastSlash != std::string::npos) {
              primName = primName.substr(lastSlash + 1);
            }
            if (!ConvertSkeletonFromPtr(env, skelPath, *discoveredSkelPtr, primName, &skel)) {
              return false;
            }
          } else {
            if (!ConvertSkeletonImplWithPath(env, skelPath, &skel)) {
              return false;
            }
          }
          DCOUT("Converted skeleton attached to : " << abs_prim_path);

          _skelPathToIndex[skelPathStr] = skel_id;
          skeletons.emplace_back(std::move(skel));
          DCOUT("add skeleton\n");

          dst.skel_id = skel_id;
        }

      }
    }

    // Explicit joint orders
    // If the mesh has `skel:joints`, remap jointIndex.
    {
      std::vector<value::token> joints = mesh.get_joints();
      if ((dst.skel_id >= 0) && (dst.skel_id < int(skeletons.size())) && joints.size()) {
        //  DCOUT("has explicit joint orders.\n");

        const auto &skel = skeletons[size_t(dst.skel_id)];

        // Cache BuildSkelNameToIndexMap per skeleton ID (avoids rebuilding per mesh)
        auto cache_it = _skelNameToIndexCache.find(dst.skel_id);
        if (cache_it == _skelNameToIndexCache.end()) {
          cache_it = _skelNameToIndexCache.emplace(dst.skel_id, BuildSkelNameToIndexMap(skel)).first;
        }
        const auto &name_to_index_map = cache_it->second;

        // Flat vector remap: index_remap[i] = skeleton joint index for mesh joint i
        std::vector<int> index_remap(joints.size(), -1);

        for (size_t i = 0; i < joints.size(); i++) {
          std::string joint_name = joints[i].str();

          auto nit = name_to_index_map.find(joint_name);
          if (nit == name_to_index_map.end()) {
            PUSH_ERROR_AND_RETURN(fmt::format("joint_name {} not found in Skeleton", joint_name));
          }

          index_remap[i] = nit->second;
        }

        for (size_t i = 0; i < dst.joint_and_weights.jointIndices.size(); i++) {
          int src_idx = dst.joint_and_weights.jointIndices[i];
          if (src_idx >= 0 && size_t(src_idx) < index_remap.size() && index_remap[size_t(src_idx)] >= 0) {
            dst.joint_and_weights.jointIndices[i] = index_remap[size_t(src_idx)];
          }
        }
      }

    }

    // geomBindTransform(optional).
    // If not authored, defaults to identity matrix (already set in struct initialization)
    if (mesh.has_primvar("skel:geomBindTransform")) {
      GeomPrimvar bindTransformPvar;

      if (!GetGeomPrimvar(env.stage, &mesh, "skel:geomBindTransform",
                          &bindTransformPvar, &_err)) {
        return false;
      }

      value::matrix4d bindTransform;
      if (!bindTransformPvar.get_value(&bindTransform)) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Failed to get `skel:geomBindTransform` attribute. "
                        "Ensure `skel:geomBindTransform` is type `matrix4d`"));
      }

      dst.joint_and_weights.geomBindTransform = bindTransform;
      dst.joint_and_weights.hasGeomBindTransform = true;
    } else {
      // geomBindTransform not authored - use identity (already set by default)
      // Flag remains false to indicate fallback was used
      DCOUT("skel:geomBindTransform not authored, using identity matrix");
    }
  }

  //
  // 6. BlendShapes
  //
  //    NOTE: (Built-in) BlendShape attributes are per-point, so it is not
  //    affected by triangulation and single-indexable indices build.
  //
  for (const auto &it : blendshapes) {
    const std::string &bs_path = it.first;
    const BlendShape *bs = it.second;

    if (!bs) {
      continue;
    }

    //
    // TODO: in-between attribs
    //

    std::vector<int> vertex_indices;
    std::vector<value::vector3f> normal_offsets;
    std::vector<value::vector3f> vertex_offsets;

    bs->pointIndices.get_value(&vertex_indices);
    bs->normalOffsets.get_value(&normal_offsets);
    bs->offsets.get_value(&vertex_offsets);

    ShapeTarget shapeTarget;
    shapeTarget.abs_path = bs_path;
    shapeTarget.prim_name = bs->name;
    shapeTarget.display_name = bs->metas().has_displayName() ? bs->metas().get_displayName() : "";

    if (vertex_indices.empty()) {
      PUSH_WARN(
          fmt::format("`pointIndices` in BlendShape `{}` is not authored or "
                      "empty. Skipping.",
                      bs->name));
    }

    // Check if index is valid.
    std::vector<uint32_t> indices;
    indices.resize(vertex_indices.size());

    for (size_t i = 0; i < vertex_indices.size(); i++) {
      if (vertex_indices[i] < 0) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "negative index in `pointIndices`. Prim path: `{}`", bs_path));
      }

      if (uint32_t(vertex_indices[i]) > dst.points.size()) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("pointIndices[{}] {} exceeds the number of points in "
                        "GeomMesh {}. Prim path: `{}`",
                        i, vertex_indices[i], dst.points.size(), bs_path));
      }

      indices[i] = uint32_t(vertex_indices[i]);
    }
    shapeTarget.pointIndices = indices;

    if (vertex_offsets.size() &&
        (vertex_offsets.size() == vertex_indices.size())) {
      shapeTarget.pointOffsets.resize(vertex_offsets.size());
      memcpy(shapeTarget.pointOffsets.data(), vertex_offsets.data(),
             sizeof(value::normal3f) * vertex_offsets.size());
    }

    if (normal_offsets.size() &&
        (normal_offsets.size() == vertex_indices.size())) {
      shapeTarget.normalOffsets.resize(normal_offsets.size());
      memcpy(shapeTarget.normalOffsets.data(), normal_offsets.data(),
             sizeof(value::normal3f) * normal_offsets.size());
    }

    // TODO inbetweens

    // TODO: key duplicate check
    dst.targets[bs->name] = shapeTarget;
    DCOUT("Converted blendshape target: " << bs->name);
  }


  //
  // 7. Compute normals
  //
  //    Compute normals when normals is not present or compute tangents
  //    requiested but normals is not present. Normals are computed with
  //    'vertex' variability to compute smooth normals for shared vertex.
  //
  //    When triangulated, normals are computed for triangulated mesh.
  //
  bool compute_normals =
      (env.mesh_config.compute_normals && dst.normals.empty());
  bool compute_tangents =
      (env.mesh_config.compute_tangents_and_binormals &&
       (dst.binormals.empty() == 0 && dst.tangents.empty() == 0));

  if (compute_normals || (compute_tangents && dst.normals.empty())) {
    //TUSDZ_LOG_I("Build normals");
    DCOUT("Compute normals");
    std::vector<vec3> normals;
    if (!ComputeNormals(dst.points, dst.faceVertexCounts(),
                       dst.faceVertexIndices(), normals, &_err)) {
      DCOUT("compute normals failed.");
      return false;
    }

    dst.normals.set_buffer(reinterpret_cast<const uint8_t *>(normals.data()),
                           normals.size() * sizeof(vec3));
    dst.normals.elementSize = 1;
    dst.normals.variability = VertexVariability::Vertex;
    dst.normals.format = VertexAttributeFormat::Vec3;
    dst.normals.stride = 0;
    dst.normals.indices.clear();
    dst.normals.name = "normals";

    if (!is_single_indexable) {
      auto result = VertexToFaceVarying(
          dst.normals.get_data(), dst.normals.stride_bytes(),
          dst.faceVertexCounts(), dst.faceVertexIndices());
      if (!result) {
        PUSH_WARN(fmt::format(
            "Convert vertex/varying `normals` attribute failed for Mesh '{}': {}. Normals removed from RenderMesh.",
            abs_prim_path.full_path_name(), result.error()));
        // Clear normals from RenderMesh since conversion failed
        dst.normals.data.clear();
        dst.normals.indices.clear();
      } else {
        dst.normals.data = result.value();
        dst.normals.variability = VertexVariability::FaceVarying;
      }
    }
  }


  //
  // 8. Build indices
  //
  if (env.mesh_config.build_vertex_indices && (!is_single_indexable)) {
    if (!env.mesh_config.prefer_non_indexed) {
      DCOUT("Build vertex indices");
      //TUSDZ_LOG_I("Build vertex indices");

      if (!BuildVertexIndicesFastImpl(dst)) {
        return false;
      }

      is_single_indexable = true;
    }
  }

  //
  // 8. Compute tangents.
  //
  if (compute_tangents) {
    DCOUT("Compute tangents.");
    //TUSDZ_LOG_I("Build tangents");

    std::vector<vec2> texcoords;
    std::vector<vec3> normals;

    // TODO: Support arbitrary slotID
    if (!dst.texcoords.count(0)) {
      PUSH_ERROR_AND_RETURN(
          "texcoord is required to compute tangents/binormals.\n");
    }

    texcoords.resize(dst.texcoords[0].vertex_count());
    normals.resize(dst.normals.vertex_count());

    memcpy(texcoords.data(), dst.texcoords[0].buffer(),
           dst.texcoords[0].num_bytes());
    memcpy(normals.data(), dst.normals.buffer(), dst.normals.num_bytes());

    std::vector<vec3> tangents;
    std::vector<vec3> binormals;
    std::vector<uint32_t> vertex_indices;

#ifdef TYDRA_ROBUST_TANGENT
    if (!ComputeTangentsAndBinormalsRobust(dst.points, dst.faceVertexCounts(),
                                          dst.faceVertexIndices(), texcoords,
                                          normals, !is_single_indexable, &tangents,
                                          &binormals, &vertex_indices, &_err)) {
      PUSH_ERROR_AND_RETURN("Failed to compute tangents/binormals with robust method.");
    }
#else
    if (!ComputeTangentsAndBinormals(dst.points, dst.faceVertexCounts(),
                                     dst.faceVertexIndices(), texcoords,
                                     normals, !is_single_indexable, &tangents,
                                     &binormals, &vertex_indices, &_err)) {
      PUSH_ERROR_AND_RETURN("Failed to compute tangents/binormals.");
    }
#endif

    // 1. Firstly, always convert tangents/binormals to 'facevarying'
    // variability
    {
      std::vector<vec3> facevarying_tangents;
      std::vector<vec3> facevarying_binormals;
      facevarying_tangents.assign(vertex_indices.size(), {0.0f, 0.0f, 0.0f});
      facevarying_binormals.assign(vertex_indices.size(), {0.0f, 0.0f, 0.0f});
      for (size_t i = 0; i < vertex_indices.size(); i++) {
        facevarying_tangents[i] = tangents[vertex_indices[i]];
        facevarying_binormals[i] = binormals[vertex_indices[i]];
      }

      dst.tangents.data.resize(facevarying_tangents.size() * sizeof(vec3));
      memcpy(dst.tangents.data.data(), facevarying_tangents.data(),
             facevarying_tangents.size() * sizeof(vec3));

      dst.tangents.format = VertexAttributeFormat::Vec3;
      dst.tangents.stride = 0;
      dst.tangents.elementSize = 1;
      dst.tangents.variability = VertexVariability::FaceVarying;

      dst.binormals.data.resize(facevarying_binormals.size() * sizeof(vec3));
      memcpy(dst.binormals.data.data(), facevarying_binormals.data(),
             facevarying_binormals.size() * sizeof(vec3));

      dst.binormals.format = VertexAttributeFormat::Vec3;
      dst.binormals.stride = 0;
      dst.binormals.elementSize = 1;
      dst.binormals.variability = VertexVariability::FaceVarying;
    }

    // 2. Build single vertex indices if `build_vertex_indices` is true.
    if (env.mesh_config.build_vertex_indices) {
      if (!BuildVertexIndicesImpl(dst)) {
        return false;
      }
      is_single_indexable = true;
    }
  }

  dst.is_single_indexable = is_single_indexable;

  dst.prim_name = mesh.name;
  dst.abs_path = abs_prim_path.full_path_name();
  dst.display_name = mesh.metas().has_displayName() ? mesh.metas().get_displayName() : "";

  //
  // Check for MeshLightAPI - if present, mark this mesh as an area light
  //
  const auto &prim_metas = mesh.metas();
  if (prim_metas.has_apiSchemas()) {
    const auto api_schemas = prim_metas.get_apiSchemas();
    bool has_meshlight_api = false;

    for (const auto &schema_pair : api_schemas.names) {
      if (schema_pair.first == APISchemas::APIName::MeshLightAPI) {
        has_meshlight_api = true;
        break;
      }
    }

    if (has_meshlight_api) {
      DCOUT("Mesh has MeshLightAPI: " << abs_prim_path.full_path_name());

      dst.is_area_light = true;

      // Extract MeshLightAPI properties from mesh
      // MeshLightAPI inherits from LightAPI, which uses "inputs:" prefix

      // color
      if (mesh.props.count("inputs:color")) {
        const Property &prop = mesh.props.at("inputs:color");
        const Attribute &attr = prop.get_attribute();
        const primvar::PrimVar &pvar = attr.get_var();
        if (auto val = pvar.get_value<value::color3f>()) {
          dst.light_color[0] = val.value()[0];
          dst.light_color[1] = val.value()[1];
          dst.light_color[2] = val.value()[2];
        }
      }

      // intensity
      if (mesh.props.count("inputs:intensity")) {
        const Property &prop = mesh.props.at("inputs:intensity");
        const Attribute &attr = prop.get_attribute();
        const primvar::PrimVar &pvar = attr.get_var();
        if (auto val = pvar.get_value<float>()) {
          dst.light_intensity = val.value();
        }
      }

      // exposure (optional)
      if (mesh.props.count("inputs:exposure")) {
        const Property &prop = mesh.props.at("inputs:exposure");
        const Attribute &attr = prop.get_attribute();
        const primvar::PrimVar &pvar = attr.get_var();
        if (auto val = pvar.get_value<float>()) {
          dst.light_exposure = val.value();
        }
      }

      // normalize
      if (mesh.props.count("inputs:normalize")) {
        const Property &prop = mesh.props.at("inputs:normalize");
        const Attribute &attr = prop.get_attribute();
        const primvar::PrimVar &pvar = attr.get_var();
        if (auto val = pvar.get_value<bool>()) {
          dst.light_normalize = val.value();
        }
      }

      // materialSyncMode
      if (mesh.props.count("inputs:materialSyncMode")) {
        const Property &prop = mesh.props.at("inputs:materialSyncMode");
        const Attribute &attr = prop.get_attribute();
        const primvar::PrimVar &pvar = attr.get_var();
        if (auto val = pvar.get_value<value::token>()) {
          dst.light_material_sync_mode = val.value().str();
        }
      }

      // Set default if not specified
      if (dst.light_material_sync_mode.empty()) {
        dst.light_material_sync_mode = "materialGlowTintsLight";  // USD default
      }

      DCOUT("  Area light properties:"
            << " color=(" << dst.light_color[0] << "," << dst.light_color[1] << "," << dst.light_color[2] << ")"
            << " intensity=" << dst.light_intensity
            << " exposure=" << dst.light_exposure
            << " normalize=" << dst.light_normalize
            << " materialSyncMode=" << dst.light_material_sync_mode);
    }
  }


  (*dstMesh) = std::move(dst);

  return true;
}

namespace {

struct UVConnectionResolveCacheEntry {
  bool found{false};
  Path tex_abs_path;
  const UsdUVTexture *texture{nullptr};
  const Shader *shader{nullptr};
};

struct MtlxConnectionResolveCacheEntry {
  Path tex_abs_path;
  const Shader *image_shader{nullptr};
  std::string st_varname;
  const AssetInfo *asset_info{nullptr};
};

struct ConnectionResolveCache {
  const Stage *stage{nullptr};
  std::unordered_map<std::string, UVConnectionResolveCacheEntry> uv_texture_by_connection;
  std::unordered_map<std::string, MtlxConnectionResolveCacheEntry> mtlx_texture_by_connection;
};

static ConnectionResolveCache &GetConnectionResolveCache(const Stage &stage) {
  static thread_local ConnectionResolveCache cache;
  if (cache.stage != &stage) {
    cache.stage = &stage;
    cache.uv_texture_by_connection.clear();
    cache.mtlx_texture_by_connection.clear();
  }
  return cache;
}

static void ResetConnectionResolveCache(const Stage &stage) {
  ConnectionResolveCache &cache = GetConnectionResolveCache(stage);
  cache.uv_texture_by_connection.clear();
  cache.mtlx_texture_by_connection.clear();
}

// Convert UsdTransform2d -> PrimvarReader_float2 shader network.
nonstd::expected<bool, std::string> ConvertTexTransform2d(
    const Stage &stage, const Path &tx_abs_path, const UsdTransform2d &tx,
    UVTexture *tex_out, double timecode) {
  float rotation;  // in angles
  if (!tx.rotation.get_value().get(timecode, &rotation)) {
    return nonstd::make_unexpected(
        fmt::format("Failed to retrieve rotation attribute from {}\n",
                    tx_abs_path.full_path_name()));
  }

  value::float2 scale;
  if (!tx.scale.get_value().get(timecode, &scale)) {
    return nonstd::make_unexpected(
        fmt::format("Failed to retrieve scale attribute from {}\n",
                    tx_abs_path.full_path_name()));
  }

  value::float2 translation;
  if (!tx.translation.get_value().get(timecode, &translation)) {
    return nonstd::make_unexpected(
        fmt::format("Failed to retrieve translation attribute from {}\n",
                    tx_abs_path.full_path_name()));
  }

  // must be authored and connected to PrimvarReader.
  if (!tx.in.authored()) {
    return nonstd::make_unexpected("`inputs:in` must be authored.\n");
  }

  if (!tx.in.is_connection()) {
    return nonstd::make_unexpected("`inputs:in` must be a connection.\n");
  }

  const auto &paths = tx.in.get_connections();
  if (paths.size() != 1) {
    return nonstd::make_unexpected(
        "`inputs:in` must be a single connection Path.\n");
  }

  std::string prim_part = paths[0].prim_part();
  std::string prop_part = paths[0].prop_part();

  if (prop_part != "outputs:result") {
    return nonstd::make_unexpected(
        "`inputs:in` connection Path's property part must be "
        "`outputs:result`\n");
  }

  std::string err;

  const Prim *pprim{nullptr};
  if (!stage.find_prim_at_path(Path(prim_part, ""), pprim, &err)) {
    return nonstd::make_unexpected(fmt::format(
        "`inputs:in` connection Path not found in the Stage. {}\n", prim_part));
  }

  if (!pprim) {
    return nonstd::make_unexpected(
        fmt::format("[InternalError] Prim is nullptr: {}\n", prim_part));
  }

  const Shader *pshader = pprim->as<Shader>();
  if (!pshader) {
    return nonstd::make_unexpected(
        fmt::format("{} must be Shader Prim, but got {}\n", prim_part,
                    pprim->prim_type_name()));
  }

  const UsdPrimvarReader_float2 *preader =
      pshader->value.as<UsdPrimvarReader_float2>();
  if (!preader) {
    return nonstd::make_unexpected(fmt::format(
        "Shader {} must be UsdPrimvarReader_float2 type, but got {}(internal type {})\n",
        prim_part, pshader->info_id, pshader->value.type_name()));
  }

  // Get value producing attribute(i.e, follow .connection and return
  // terminal Attribute value)
  //value::token varname;

  // 'string' for inputs:varname preferred.
  std::string varname;
  TerminalAttributeValue attr;
  if (!tydra::EvaluateAttribute(stage, *pprim, "inputs:varname", &attr, &err)) {
    return nonstd::make_unexpected(
        "`inputs:varname` evaluation failed: " + err + "\n");
  }
  if (auto pvt = attr.as<value::token>()) {
    varname = pvt->str();
  } else if (auto pvs = attr.as<std::string>()) {
    varname = *pvs;
  } else if (auto pvsd = attr.as<value::StringData>()) {
    varname = (*pvsd).value;
  } else {
    return nonstd::make_unexpected(
        "`inputs:varname` must be `token` or `string` type, but got " + attr.type_name() +
        "\n");
  }
  if (varname.empty()) {
    return nonstd::make_unexpected("`inputs:varname` is empty token\n");
  }
  DCOUT("inputs:varname = " << varname);

  // Build transform matrix.
  // https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_texture_transform
  // Since USD uses post-multiply,
  //
  // matrix = scale * rotate * translate
  //
  {
    mat3 s;
    s.set_scale(scale[0], scale[1], 1.0f);

    mat3 r = mat3::identity();

    r.m[0][0] = std::cos(math::radian(rotation));
    r.m[0][1] = std::sin(math::radian(rotation));

    r.m[1][0] = -std::sin(math::radian(rotation));
    r.m[1][1] = std::cos(math::radian(rotation));

    mat3 t = mat3::identity();
    t.set_translation(translation[0], translation[1], 1.0f);

    tex_out->transform = s * r * t;
  }

  tex_out->tx_rotation = rotation;
  tex_out->tx_translation = translation;
  tex_out->tx_scale = scale;
  tex_out->has_transform2d = true;

  tex_out->varname_uv = varname;

  return true;
}

template <typename T>
nonstd::expected<bool, std::string> GetConnectedUVTexture(
    const Stage &stage, const TypedAnimatableAttributeWithFallback<T> &src,
    Path *tex_abs_path, const UsdUVTexture **dst, const Shader **shader_out) {
  if (!dst) {
    return nonstd::make_unexpected("[InternalError] dst is nullptr.\n");
  }

  if (!src.is_connection()) {
    return nonstd::make_unexpected("Attribute must be connection.\n");
  }

  if (src.get_connections().size() != 1) {
    return nonstd::make_unexpected(
        "Attribute connections must be single connection Path.\n");
  }

  //
  // Example: color3f inputs:diffuseColor.connect = </path/to/tex.outputs:rgb>
  //
  // => path.prim_part : /path/to/tex
  // => path.prop_part : outputs:rgb
  //

  const Path &path = src.get_connections()[0];

  const std::string prim_part = path.prim_part();
  const std::string prop_part = path.prop_part();
  const std::string cache_key = path.full_path_name();
  ConnectionResolveCache &resolve_cache = GetConnectionResolveCache(stage);

  if (shader_out) {
    *shader_out = nullptr;
  }
  *dst = nullptr;

  auto cache_it = resolve_cache.uv_texture_by_connection.find(cache_key);
  if (cache_it != resolve_cache.uv_texture_by_connection.end()) {
    if (tex_abs_path) {
      *tex_abs_path = cache_it->second.tex_abs_path;
    }
    *dst = cache_it->second.texture;
    if (shader_out) {
      *shader_out = cache_it->second.shader;
    }
    return cache_it->second.found;
  }

  auto cache_result = [&](bool found, const Path &resolved_path,
                          const UsdUVTexture *texture,
                          const Shader *shader) {
    UVConnectionResolveCacheEntry entry;
    entry.found = found;
    entry.tex_abs_path = resolved_path;
    entry.texture = texture;
    entry.shader = shader;
    resolve_cache.uv_texture_by_connection[cache_key] = std::move(entry);
  };

  // NOTE: no `outputs:rgba` in the spec.
  constexpr auto kOutputsRGB = "outputs:rgb";
  constexpr auto kOutputsR = "outputs:r";
  constexpr auto kOutputsG = "outputs:g";
  constexpr auto kOutputsB = "outputs:b";
  constexpr auto kOutputsA = "outputs:a";

  TUSDZ_LOG_I("path: " << path);

  // Check if prop_part is a standard UsdUVTexture output
  bool is_standard_output = (prop_part == kOutputsRGB) ||
                            (prop_part == kOutputsR) ||
                            (prop_part == kOutputsG) ||
                            (prop_part == kOutputsB) ||
                            (prop_part == kOutputsA);

  const Prim *prim{nullptr};
  std::string err;
  bool found_in_stage = stage.find_prim_at_path(Path(prim_part, ""), prim, &err);

  // If not found in stage lookup, try to navigate through Material's children
  // This handles the case where NodeGraph is a child of Material but not in the Stage index
  if (!found_in_stage || !prim) {
    DCOUT("Prim not found in stage lookup, trying Material children approach");

    // Extract Material path - it should be everything before the last element
    size_t last_slash = prim_part.rfind('/');
    if (last_slash == std::string::npos) {
      return nonstd::make_unexpected(
          fmt::format("Prim {} not found in the Stage: {}\n", prim_part, err));
    }

    std::string material_path = prim_part.substr(0, last_slash);
    std::string child_name = prim_part.substr(last_slash + 1);

    DCOUT("Looking for Material at: " << material_path);
    DCOUT("Child name: " << child_name);

    // Find the Material
    const Prim *mat_prim{nullptr};
    if (!stage.find_prim_at_path(Path(material_path, ""), mat_prim, &err)) {
      return nonstd::make_unexpected(
          fmt::format("Prim {} not found (material lookup also failed): {}\n", prim_part, err));
    }

    // Look for child prim
    if (mat_prim) {
      std::string children_info = "Material has " + std::to_string(mat_prim->children().size()) + " children: ";
      for (const auto& child : mat_prim->children()) {
        std::string elem_name = child.element_name();
        std::string child_type = child.data().type_name();
        children_info += "'" + elem_name + "'(" + child_type + ") ";

        // Check by name match
        if (elem_name == child_name) {
          prim = &child;
          break;
        }
        // Also check if it's a NodeGraph/Shader by type name
        // This handles cases where element_name might not be set properly
        // e.g., looking for "NodeGraphs" and finding type "NodeGraph" with empty name
        if (child_type == "NodeGraph" && (child_name == "NodeGraphs" || child_name == "NodeGraph")) {
          prim = &child;
          break;
        }
        if (child_type == "Shader" && child_name == "Shader") {
          prim = &child;
          break;
        }
      }

      if (!prim) {
        DCOUT(children_info);
        return nonstd::make_unexpected(
            fmt::format("Child prim '{}' not found in Material {}. {}\n", child_name, material_path, children_info));
      }
    } else {
      return nonstd::make_unexpected(
          fmt::format("Material prim {} is null\n", material_path));
    }
  }

  if (!prim) {
    return nonstd::make_unexpected("[InternalError] Prim ptr is null.\n");
  }

  // Check if this is a NodeGraph - if so, we need to traverse through it
  if (const NodeGraph *ng = prim->as<NodeGraph>()) {
    DCOUT("Connection goes through NodeGraph: " << prim_part);

    // Look for the output property in the NodeGraph's props
    const auto &props = ng->props;
    auto it = props.find(prop_part);
    if (it == props.end()) {
      return nonstd::make_unexpected(
          fmt::format("NodeGraph {} does not have output property {}", prim_part, prop_part));
    }

    const Property &output_prop = it->second;
    if (!output_prop.is_attribute()) {
      return nonstd::make_unexpected(
          fmt::format("NodeGraph output {} is not an attribute", prop_part));
    }

    const Attribute &output_attr = output_prop.get_attribute();
    if (!output_attr.has_connections()) {
      return nonstd::make_unexpected(
          fmt::format("NodeGraph output {} has no connections", prop_part));
    }

    // Get the connection from the NodeGraph output
    const auto &output_conns = output_attr.connections();
    if (output_conns.size() != 1) {
      return nonstd::make_unexpected(
          fmt::format("NodeGraph output {} must have exactly one connection, got {}",
                      prop_part, output_conns.size()));
    }

    const Path &ng_output_path = output_conns[0];
    DCOUT("NodeGraph output connects to: " << ng_output_path);

    // Recursively follow the connection through the NodeGraph
    // We need to traverse to the next node in the chain
    std::string next_prim_part = ng_output_path.prim_part();
    std::string next_prop_part = ng_output_path.prop_part();

    // Find the next prim in the chain
    // It might be a child of the NodeGraph, so use the same child lookup logic
    const Prim *next_prim{nullptr};
    bool found_next = stage.find_prim_at_path(Path(next_prim_part, ""), next_prim, &err);

    // If not found in stage, it might be a child of the current NodeGraph
    if (!found_next || !next_prim) {
      DCOUT("Next prim not found in stage, checking NodeGraph children");

      // Check if it's a child of this NodeGraph
      size_t last_slash = next_prim_part.rfind('/');
      if (last_slash != std::string::npos) {
        std::string parent_path = next_prim_part.substr(0, last_slash);
        std::string child_name = next_prim_part.substr(last_slash + 1);

        // If the parent is this NodeGraph, look in its children
        if (parent_path == prim_part) {
          for (const auto& child : prim->children()) {
            std::string elem_name = child.element_name();
            if (elem_name == child_name) {
              next_prim = &child;
              break;
            }
          }

          if (!next_prim) {
            return nonstd::make_unexpected(
                fmt::format("Child prim '{}' not found in NodeGraph {}\n", child_name, prim_part));
          }
        } else {
          return nonstd::make_unexpected(
              fmt::format("Prim {} not found in the Stage: {}\n", next_prim_part, err));
        }
      } else {
        return nonstd::make_unexpected(
            fmt::format("Prim {} not found in the Stage: {}\n", next_prim_part, err));
      }
    }

    if (!next_prim) {
      return nonstd::make_unexpected("[InternalError] next_prim is null.\n");
    }

    // For nested NodeGraphs or other intermediate nodes, we would need to continue traversing
    // For now, we only support the common pattern: NodeGraph -> Shader(UsdUVTexture)
    // Nested NodeGraphs are rare and can be handled if needed

    // Check if it's a Shader with UsdUVTexture
    if (const Shader *pshader = next_prim->as<Shader>()) {
      if (const UsdUVTexture *ptex = pshader->value.as<UsdUVTexture>()) {
        // Verify the property part is valid for UsdUVTexture
        if (next_prop_part != kOutputsRGB && next_prop_part != kOutputsR &&
            next_prop_part != kOutputsG && next_prop_part != kOutputsB &&
            next_prop_part != kOutputsA) {
          return nonstd::make_unexpected(fmt::format(
              "UsdUVTexture connection property part must be outputs:rgb/r/g/b/a, got {}",
              next_prop_part));
        }

        DCOUT("Found UsdUVTexture through NodeGraph: " << next_prim_part);
        (*dst) = ptex;

        if (shader_out) {
          (*shader_out) = pshader;
        }

        if (tex_abs_path) {
          (*tex_abs_path) = ng_output_path;
        }

        cache_result(true, ng_output_path, ptex, pshader);
        return true;
      }
      // Shader exists but it's not a UsdUVTexture - this is OK, NodeGraph might connect to other shader types
      // Return false (not found) rather than error
      DCOUT(fmt::format("NodeGraph {} output {} connects to Shader {} but it's not UsdUVTexture",
                        prim_part, prop_part, next_prim_part));
      cache_result(false, ng_output_path, nullptr, pshader);
      return false;
    }

    // If we get here, the NodeGraph doesn't connect to a UsdUVTexture
    // This is not necessarily an error - the connection might be to a MaterialX shader or other node type
    DCOUT(fmt::format("NodeGraph {} output {} connects to {} (type: {}), not a UsdUVTexture",
                      prim_part, prop_part, next_prim_part, next_prim->prim_type_name()));
    cache_result(false, ng_output_path, nullptr, nullptr);
    return false;
  }

  // Not a NodeGraph - must be a direct UsdUVTexture connection
  if (!is_standard_output) {
    return nonstd::make_unexpected(fmt::format(
        "connection Path's property part must be `{}`, `{}`, `{}`, `{}` or `{}` "
        "for UsdUVTexture, but got `{}`(prim_part: {}).",
        kOutputsRGB, kOutputsR, kOutputsG, kOutputsB, kOutputsA, prop_part, prim_part));
  }

  if (tex_abs_path) {
    (*tex_abs_path) = Path(prim_part, prop_part);
  }

  if (const Shader *pshader = prim->as<Shader>()) {
    if (const UsdUVTexture *ptex = pshader->value.as<UsdUVTexture>()) {
      DCOUT("ptex = " << ptex);
      (*dst) = ptex;

      if (shader_out) {
        (*shader_out) = pshader;
      }

      cache_result(true, Path(prim_part, prop_part), ptex, pshader);
      return true;
    }
  }

  return nonstd::make_unexpected(
      fmt::format("Prim {} must be `Shader` Prim type, but got `{}`", prim_part,
                  prim->prim_type_name()));
}

// Helper function to find ND_image_color4 texture nodes in a MaterialX NodeGraph
// by traversing connections from the given output
template <typename T>
nonstd::expected<bool, std::string> GetConnectedMtlxTexture(
    const Stage &stage, const TypedAnimatableAttributeWithFallback<T> &src,
    Path *tex_abs_path, const Shader **image_shader_out,
    std::string *st_varname_out, const AssetInfo **assetInfo_out,
    const std::string &default_texcoords_primvar_name = "st") {

  if (!src.is_connection()) {
    return nonstd::make_unexpected("Attribute must be connection.\n");
  }

  if (src.get_connections().size() != 1) {
    return nonstd::make_unexpected(
        "Attribute connections must be single connection Path.\n");
  }

  const Path &path = src.get_connections()[0];
  const std::string prim_part = path.prim_part();
  const std::string prop_part = path.prop_part();
  const std::string cache_key =
      path.full_path_name() + "|" + default_texcoords_primvar_name;
  ConnectionResolveCache &resolve_cache = GetConnectionResolveCache(stage);

  auto mtlx_cache_it =
      resolve_cache.mtlx_texture_by_connection.find(cache_key);
  if (mtlx_cache_it != resolve_cache.mtlx_texture_by_connection.end()) {
    if (tex_abs_path) {
      *tex_abs_path = mtlx_cache_it->second.tex_abs_path;
    }
    if (image_shader_out) {
      *image_shader_out = mtlx_cache_it->second.image_shader;
    }
    if (st_varname_out) {
      *st_varname_out = mtlx_cache_it->second.st_varname;
    }
    if (assetInfo_out) {
      *assetInfo_out = mtlx_cache_it->second.asset_info;
    }
    return true;
  }

  auto cache_result = [&](const Path &resolved_path, const Shader *image_shader,
                          const std::string &st_varname,
                          const AssetInfo *asset_info) {
    MtlxConnectionResolveCacheEntry entry;
    entry.tex_abs_path = resolved_path;
    entry.image_shader = image_shader;
    entry.st_varname = st_varname;
    entry.asset_info = asset_info;
    resolve_cache.mtlx_texture_by_connection[cache_key] = std::move(entry);
  };

  DCOUT("Checking MaterialX connection: " << path.full_path_name());
  DCOUT("  prim_part: " << prim_part);
  DCOUT("  prop_part: " << prop_part);

  // The prim_part should be the NodeGraph path itself
  // For </root/_materials/Material/NodeGraphs.outputs:node_out>,
  // prim_part = "/root/_materials/Material/NodeGraphs"

  // First, try to find via stage lookup
  const Prim *ng_prim{nullptr};
  std::string err;
  bool found_in_stage = stage.find_prim_at_path(Path(prim_part, ""), ng_prim, &err);

  // If not found in stage lookup, try to navigate through Material's children
  if (!found_in_stage || !ng_prim) {
    DCOUT("Prim not found in stage lookup, trying Material children approach");

    // Extract Material path - it should be everything before the last element
    size_t last_slash = prim_part.rfind('/');
    if (last_slash == std::string::npos) {
      return nonstd::make_unexpected(
          fmt::format("Invalid NodeGraph path structure: {}\n", prim_part));
    }

    std::string material_path = prim_part.substr(0, last_slash);
    std::string nodegraph_name = prim_part.substr(last_slash + 1);

    DCOUT("Looking for Material at: " << material_path);
    DCOUT("NodeGraph name: " << nodegraph_name);

    // Find the Material
    const Prim *mat_prim{nullptr};
    if (!stage.find_prim_at_path(Path(material_path, ""), mat_prim, &err)) {
      return nonstd::make_unexpected(
          fmt::format("Material {} not found: {}\n", material_path, err));
    }

    // Look for NodeGraph child
    if (mat_prim) {
      std::string children_info = "Material has " + std::to_string(mat_prim->children().size()) + " children: ";
      for (const auto& child : mat_prim->children()) {
        std::string child_name = child.element_name();
        std::string child_type = child.data().type_name();
        children_info += "'" + child_name + "'(" + child_type + ") ";

        // Check if this is a NodeGraph (by type, since name might be empty)
        if (child_type == "NodeGraph") {
          // If the child has no name but is the right type, use it
          // This handles the case where the NodeGraph doesn't have element_name set
          ng_prim = &child;
          break;
        } else if (child_name == nodegraph_name) {
          // Also check by exact name match
          ng_prim = &child;
          break;
        }
      }

      if (!ng_prim) {
        return nonstd::make_unexpected(
            fmt::format("NodeGraph '{}' not found. {}\n", nodegraph_name, children_info));
      }
    } else {
      return nonstd::make_unexpected(
          fmt::format("Material prim is null\n"));
    }
  }

  DCOUT("Found prim: " << prim_part << ", type: " << (ng_prim ? ng_prim->data().type_name() : "null"));

  const NodeGraph *ng = ng_prim ? ng_prim->as<NodeGraph>() : nullptr;
  if (!ng) {
    // Debug output to understand why it's not a NodeGraph
    if (ng_prim) {
      return nonstd::make_unexpected(
          fmt::format("{} is not a NodeGraph, prim_type: {}\n", prim_part, ng_prim->data().type_name()));
    }
    return nonstd::make_unexpected(
        fmt::format("{} is not a NodeGraph\n", prim_part));
  }

  // Find the output connection we're looking for
  // The prop_part should be like "outputs:node_out"
  std::string output_name = prop_part;
  if (startsWith(output_name, "outputs:")) {
    output_name = output_name.substr(8); // Remove "outputs:" prefix
  }

  // Look for the connection in props
  // Try both with and without ".connect" suffix
  std::string conn_prop_name = "outputs:" + output_name + ".connect";
  auto it = ng->props.find(conn_prop_name);

  if (it == ng->props.end()) {
    // Try without .connect suffix
    conn_prop_name = "outputs:" + output_name;
    it = ng->props.find(conn_prop_name);

    if (it == ng->props.end()) {
      // List available props for debugging
      std::string available_props = "Available props: ";
      for (const auto& prop : ng->props) {
        available_props += prop.first + " ";
      }
      return nonstd::make_unexpected(
          fmt::format("Output connection '{}' not found in NodeGraph. {}\n",
                      conn_prop_name, available_props));
    }
  }

  // NodeGraph outputs can be stored as attributes or relationships
  Path current_path;
  bool found_connection = false;

  if (it->second.is_attribute()) {
    // It's an attribute - look for connections on the attribute
    const Attribute &attr = it->second.get_attribute();
    if (attr.has_connections() && !attr.connections().empty()) {
      current_path = attr.connections()[0];
      found_connection = true;
    }
  } else if (it->second.is_relationship()) {
    // Also support relationship format
    auto targets = it->second.get_relationTargets();
    if (!targets.empty()) {
      current_path = targets[0];
      found_connection = true;
    }
  }

  if (!found_connection) {
    return nonstd::make_unexpected(
        fmt::format("Output {} has no connection targets\n", conn_prop_name));
  }
  const Shader *image_shader = nullptr;

  // Traverse the node connections to find ND_image_color4
  // Maximum depth to prevent infinite loops
  int max_depth = 10;
  std::string traversal_log = "Traversal: ";
  while (max_depth-- > 0) {
    std::string current_prim_part = current_path.prim_part();

    const Prim *current_prim{nullptr};

    // First, try regular stage lookup
    bool current_found_in_stage = stage.find_prim_at_path(Path(current_prim_part, ""), current_prim, &err);

    // If not found and this is under a NodeGraph, look in NodeGraph children
    if (!current_found_in_stage || !current_prim) {
      // Check if this path is under the NodeGraph we found earlier
      size_t last_slash = current_prim_part.rfind('/');
      if (last_slash != std::string::npos) {
        std::string parent_path = current_prim_part.substr(0, last_slash);
        std::string child_name = current_prim_part.substr(last_slash + 1);

        // Check if parent is our NodeGraph
        if (ng_prim && parent_path.find("NodeGraphs") != std::string::npos) {
          // Look for the child in the NodeGraph prim
          for (const auto& child : ng_prim->children()) {
            if (child.element_name() == child_name) {
              current_prim = &child;
              break;
            }
          }
        }
      }

      if (!current_prim) {
        return nonstd::make_unexpected(
            fmt::format("Shader {} not found\n", current_prim_part));
      }
    }

    const Shader *current_shader = current_prim ? current_prim->as<Shader>() : nullptr;
    if (!current_shader) {
      return nonstd::make_unexpected(
          fmt::format("{} is not a Shader. {}\n", current_prim_part, traversal_log));
    }

    // Log this node
    traversal_log += current_shader->info_id + " -> ";

    // Check if this is an ND_image node (color or vector variants)
    if (current_shader->info_id == "ND_image_color4" ||
        current_shader->info_id == "ND_image_color3" ||
        current_shader->info_id == "ND_image_vector4" ||
        current_shader->info_id == "ND_image_vector3" ||
        current_shader->info_id == "ND_image_float") {
      image_shader = current_shader;
      if (tex_abs_path) {
        *tex_abs_path = current_path;
      }
      if (image_shader_out) {
        *image_shader_out = image_shader;
      }
      if (assetInfo_out) {
        // get_assetInfo_struct returns AssetInfo converted from customData/assetInfo
        // Note: We only check if assetInfo is authored, but we don't return the pointer
        // since the storage has changed. The caller should use get_assetInfo_struct() directly.
        if (current_shader->metas().has_assetInfo()) {
          // AssetInfo is authored - caller should query it directly if needed
          *assetInfo_out = nullptr;
        }
      }

      // For MaterialX ND_texcoord_vector2 node, use configured default primvar name
      // (similar to OpenUSD's USDMTLX_PRIMARY_UV_NAME environment setting)
      if (st_varname_out) {
        *st_varname_out = default_texcoords_primvar_name.empty() ? "st" : default_texcoords_primvar_name;
      }

      cache_result(current_path, image_shader,
                   default_texcoords_primvar_name.empty() ? "st" : default_texcoords_primvar_name,
                   nullptr);
      return true;
    }

    // Check if this node has an input connection we should follow
    // For ND_convert_color4_color3, follow inputs:in
    bool found_next = false;
    DCOUT("Checking shader " << current_shader->info_id << " at " << current_prim_part);

    // Debug: log all properties from both Shader and ShaderNode
    std::string props_list = "ShaderProps: ";
    for (const auto& prop : current_shader->props) {
      props_list += prop.first + " ";
    }

    // Check if the shader has a ShaderNode value with properties
    const ShaderNode *shader_node = current_shader->value.as<ShaderNode>();
    if (shader_node && !shader_node->props.empty()) {
      props_list += " NodeProps: ";
      for (const auto& prop : shader_node->props) {
        props_list += prop.first + " ";
      }
    }
    traversal_log += "[" + props_list + "] ";

    // Helper lambda to check for connections in a property map
    auto find_connection = [&](const std::map<std::string, Property>& props_map) -> bool {
      for (const auto& prop : props_map) {
        if (startsWith(prop.first, "inputs:")) {
          bool is_connection = false;
          Path next_path;

          if (endsWith(prop.first, ".connect")) {
            // Explicit .connect suffix
            is_connection = true;
            if (prop.second.is_relationship()) {
              auto next_targets = prop.second.get_relationTargets();
              if (!next_targets.empty()) {
                next_path = next_targets[0];
              }
            }
          } else if (prop.second.is_attribute()) {
            // Check if attribute has connections
            const Attribute &attr = prop.second.get_attribute();
            if (attr.has_connections() && !attr.connections().empty()) {
              is_connection = true;
              next_path = attr.connections()[0];
            }
          }

          if (is_connection && !next_path.full_path_name().empty()) {
            DCOUT("  Following connection from " << prop.first << " to " << next_path);
            current_path = next_path;
            return true;
          }
        }
      }
      return false;
    };

    // Try shader_node->props first, then fall back to current_shader->props
    if (shader_node && !shader_node->props.empty()) {
      found_next = find_connection(shader_node->props);
    }
    if (!found_next) {
      found_next = find_connection(current_shader->props);
    }

    if (!found_next) {
      break;
    }
  }

  return nonstd::make_unexpected(
      fmt::format("No ND_image texture node found (supported: ND_image_color4/color3/vector4/vector3/float). {}\n", traversal_log));
}

static bool RawAssetRead(
    const value::AssetPath &assetPath, const AssetInfo &assetInfo,
    const AssetResolutionResolver &assetResolver,
    Asset *assetOut,
    std::string &resolvedPathOut,
    void *userdata, std::string *warn,
    std::string *err) {
  if (!assetOut) {
    if (err) {
      (*err) = "`assetOut` argument is nullptr\n";
    }
    return false;
  }

  // TODO: assetInfo
  (void)assetInfo;
  (void)userdata;
  (void)warn;

  std::string resolvedPath = assetResolver.resolve(assetPath.GetAssetPath());

  if (resolvedPath.empty()) {
    if (err) {
      (*err) += fmt::format("Failed to resolve asset path: {}\n",
                            assetPath.GetAssetPath());
    }
    return false;
  }

  Asset asset;
  bool ret = assetResolver.open_asset(resolvedPath, assetPath.GetAssetPath(),
                                      &asset, warn, err);
  if (!ret) {
    if (err) {
      (*err) += fmt::format("Failed to open asset: {}", resolvedPath);
    }
    return false;
  }

  DCOUT("Resolved asset path = " << resolvedPath);

  resolvedPathOut = resolvedPath;
  (*assetOut) = std::move(asset);

  return true;
}

}  // namespace

// Convert UsdUVTexture shader node.
// @return true upon conversion success(textures.back() contains the converted
// UVTexture)
//
// Possible network configuration
//
// - UsdUVTexture -> UsdPrimvarReader
// - UsdUVTexture -> UsdTransform2d -> UsdPrimvarReader
bool RenderSceneConverter::ConvertUVTexture(const RenderSceneConverterEnv &env,
                                            const Path &tex_abs_path,
                                            const AssetInfo &assetInfo,
                                            const UsdUVTexture &texture,
                                            UVTexture *tex_out) {
  DCOUT("ConvertUVTexture " << tex_abs_path);

  if (!tex_out) {
    PUSH_ERROR_AND_RETURN("tex_out arg is nullptr.");
  }
  std::string err;

  UVTexture tex;

  // Workaround for Blender export bug: UsdUVTexture without asset:file
  // This happens when Blender exports materials incorrectly
  bool has_file = texture.file.authored();

  value::AssetPath assetPath;
  if (has_file) {
    if (auto apath = texture.file.get_value()) {
      if (!apath.value().get(env.timecode, &assetPath)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Failed to get `asset:file` value from Path {} at time {}",
            tex_abs_path.prim_part(), env.timecode));
      }
    } else {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Failed to get `asset:file` value from Path {}",
                      tex_abs_path.prim_part()));
    }
  } else {
    // Blender export bug workaround: create placeholder texture
    PUSH_WARN(fmt::format("`asset:file` is not authored for UsdUVTexture at {}. "
                         "This is likely a Blender export bug. Creating placeholder texture.",
                         tex_abs_path.prim_part()));
    assetPath = value::AssetPath("");  // empty path
  }

  // TextureImage and BufferData
  {
    // Check image cache first - if the same asset path was already loaded,
    // reuse the existing image to avoid redundant I/O and memory usage
    std::string cacheKey = assetPath.GetAssetPath();
    const auto cachedImageIt = imageMap.find(cacheKey);
    if (cachedImageIt != imageMap.s_end()) {
      // Image already loaded, reuse it
      tex.texture_image_id = int64_t(cachedImageIt->second);
      DCOUT("Reusing cached image for: " << cacheKey << " (image_id=" << tex.texture_image_id << ")");
    } else {
      // Image not in cache, need to load it

    TextureImage texImage;
    BufferData assetImageBuffer;

    // Texel data is treated as byte array
    assetImageBuffer.componentType = ComponentType::UInt8;

    bool tex_loaded{false};

    if (env.scene_config.load_texture_assets) {
      DCOUT("load texture : " << assetPath.GetAssetPath());
      std::string warn;

      TextureImageLoaderFunction tex_loader_fun =
          env.material_config.texture_image_loader_function;

      if (!tex_loader_fun) {
        tex_loader_fun = DefaultTextureImageLoaderFunction;
      }

      tex_loaded = tex_loader_fun(
          assetPath, assetInfo, env.asset_resolver, &texImage,
          &assetImageBuffer.data,
          env.material_config.texture_image_loader_function_userdata, &warn,
          &err);

      if (warn.size()) {
        DCOUT("WARN: " << warn);
        PushWarn(warn);
      }

      if (!tex_loaded && !env.material_config.allow_texture_load_failure) {
        PUSH_ERROR_AND_RETURN(fmt::format("Failed to load texture image: `{}` err = {}", assetPath.GetAssetPath(), err));
      }


      if (err.size()) {
        // report as warn.
        PUSH_WARN(fmt::format("Failed to load texture image: `{}`. Skip loading. reason = {} ", assetPath.GetAssetPath(), err));
      }

      // store unresolved asset path.
      texImage.asset_identifier = assetPath.GetAssetPath();
      texImage.decoded = true;

    } else {

      Asset asset;
      std::string resolvedPath;
      if (RawAssetRead(assetPath, assetInfo, env.asset_resolver, &asset, resolvedPath, /* userdata */nullptr, /* warn */nullptr, &err )) {

        // store resolved asset path.
        texImage.asset_identifier = resolvedPath;


        BufferData imageBuffer;
        imageBuffer.componentType = tydra::ComponentType::UInt8;

        imageBuffer.data.resize(asset.size());
        memcpy(imageBuffer.data.data(), asset.data(), asset.size());

        // Assign buffer id
        texImage.buffer_id = int64_t(buffers.size());

        // TODO: Share image data as much as possible.
        // e.g. Texture A and B uses same image file, but texturing parameter is
        // different.
        buffers.emplace_back(imageBuffer);

        texImage.decoded = false;
        DCOUT("texture image is read, but not decoded.");

      } else {
        // store resolved asset path.
        texImage.asset_identifier = env.asset_resolver.resolve(assetPath.GetAssetPath());
        texImage.decoded = false;

        DCOUT("store asset path.");
      }

    }

    // colorSpace.
    // First look into `colorSpace` metadata of asset, then
    // look into `inputs:sourceColorSpace' attribute.
    // When both `colorSpace` metadata and `inputs:sourceColorSpace' attribute
    // exists, `colorSpace` metadata supercedes.
    // NOTE: `inputs:sourceColorSpace` attribute should be deprecated in favor of `colorSpace` metadata.
    bool inferColorSpaceFailed = false;
    if (has_file && texture.file.metas().has_colorSpace()) {
      ColorSpace cs;
      value::token cs_token = texture.file.metas().get_colorSpace();
      if (InferColorSpace(cs_token, &cs)) {
        texImage.usdColorSpace = cs;
        DCOUT("Inferred colorSpace: " << to_string(cs));
      } else {
        inferColorSpaceFailed = true;
      }
    }

    bool sourceColorSpaceSet = false;
    if (inferColorSpaceFailed || !has_file || !texture.file.metas().has_colorSpace()) {
      if (texture.sourceColorSpace.authored()) {
        UsdUVTexture::SourceColorSpace cs;
        if (texture.sourceColorSpace.get_value().get(env.timecode, &cs)) {
          if (cs == UsdUVTexture::SourceColorSpace::SRGB) {
            texImage.usdColorSpace = tydra::ColorSpace::sRGB;
            sourceColorSpaceSet = true;
          } else if (cs == UsdUVTexture::SourceColorSpace::Raw) {
            texImage.usdColorSpace = tydra::ColorSpace::Raw;
            sourceColorSpaceSet = true;
          } else if (cs == UsdUVTexture::SourceColorSpace::Auto) {

            if (tex_loaded) {

              // The spec says: https://openusd.org/release/spec_usdpreviewsurface.html
              //
              // auto : Check for gamma/color space metadata in the texture file itself; if metadata is indicative of sRGB, mark texture as sRGB . If no relevant metadata is found, mark texture as sRGB if it is either 8-bit and has 3 channels or if it is 8-bit and has 4 channels. Otherwise, do not mark texture as sRGB and use texture data as it was read from the texture.
              //
              if (((texImage.assetTexelComponentType == ComponentType::UInt8) ||
                  (texImage.assetTexelComponentType == ComponentType::Int8)) &&
                ((texImage.channels == 3) || (texImage.channels ==4))) {
                texImage.usdColorSpace = tydra::ColorSpace::sRGB;
                sourceColorSpaceSet = true;
              } else {
                PUSH_WARN(fmt::format("Infer colorSpace failed for {}. Set to Raw for now. Results may be wrong.", assetPath.GetAssetPath()));
                // At least 'not' sRGB. For now set to Raw.

                texImage.usdColorSpace = tydra::ColorSpace::Raw;
                sourceColorSpaceSet = true;
              }
            } else {
              texImage.usdColorSpace = tydra::ColorSpace::Unknown;
              sourceColorSpaceSet = true;
            }
          }
        }
      }
    }

    if (!sourceColorSpaceSet && inferColorSpaceFailed && has_file) {
      value::token cs_token = texture.file.metas().get_colorSpace();
      PUSH_ERROR_AND_RETURN(
          fmt::format("Invalid or unknown colorSpace metadataum: {}. Please "
                      "report an issue to TinyUSDZ github repo.",
                      cs_token.str()));
    }

    if (tex_loaded) {
      BufferData imageBuffer;

      // Linearlization and widen texel bit depth if required.
      if (env.material_config.linearize_color_space) {
        // TODO: Support ACEScg and Lin_DisplayP3
        DCOUT("linearlize colorspace.");
        size_t width = size_t(texImage.width);
        size_t height = size_t(texImage.height);
        size_t channels = size_t(texImage.channels);

        if (channels > 4) {
          PUSH_ERROR_AND_RETURN(
              fmt::format("TODO: Multiband color channels(5 or more) are not "
                          "supported(yet)."));
        }

        if (assetImageBuffer.componentType == tydra::ComponentType::UInt8) {
          if (texImage.usdColorSpace == tydra::ColorSpace::sRGB) {
            if (env.material_config.preserve_texel_bitdepth) {
              // u8 sRGB -> u8 Linear
              imageBuffer.componentType = tydra::ComponentType::UInt8;

              bool ret = srgb_8bit_to_linear_8bit(
                  assetImageBuffer.data, width, height, channels,
                  /* channel stride */ channels, &imageBuffer.data, &_err);
              if (!ret) {
                PUSH_ERROR_AND_RETURN(
                    "Failed to convert sRGB u8 image to Linear u8 image.");
              }

            } else {
              DCOUT("u8 sRGB -> fp32 linear.");
              // u8 sRGB -> fp32 Linear
              imageBuffer.componentType = tydra::ComponentType::Float;

              std::vector<float> buf;
              bool ret = srgb_8bit_to_linear_f32(
                  assetImageBuffer.data, width, height, channels,
                  /* channel stride */ channels, &buf, &_err);
              if (!ret) {
                PUSH_ERROR_AND_RETURN(
                    "Failed to convert sRGB u8 image to Linear f32 image.");
              }

              DCOUT("sz = " << buf.size());
              imageBuffer.data.resize(buf.size() * sizeof(float));
              memcpy(imageBuffer.data.data(), buf.data(),
                     sizeof(float) * buf.size());
            }

            texImage.colorSpace = tydra::ColorSpace::Lin_sRGB;

          } else if (texImage.usdColorSpace == tydra::ColorSpace::Lin_sRGB) {
            if (env.material_config.preserve_texel_bitdepth) {
              // no op.
              imageBuffer = std::move(assetImageBuffer);

            } else {
              // u8 -> fp32
              imageBuffer.componentType = tydra::ComponentType::Float;

              std::vector<float> buf;
              bool ret = u8_to_f32_image(assetImageBuffer.data, width, height,
                                         channels, &buf, &_err);
              if (!ret) {
                PUSH_ERROR_AND_RETURN("Failed to convert u8 image to f32 image.");
              }

              imageBuffer.data.resize(buf.size() * sizeof(float));
              memcpy(imageBuffer.data.data(), buf.data(),
                     sizeof(float) * buf.size());
            }

            texImage.colorSpace = tydra::ColorSpace::Lin_sRGB;

          } else {
            PUSH_ERROR(fmt::format("TODO: Color space {}",
                                   to_string(texImage.usdColorSpace)));
          }

        } else if (assetImageBuffer.componentType ==
                   tydra::ComponentType::Float) {
          // ignore preserve_texel_bitdepth

          if (texImage.usdColorSpace == tydra::ColorSpace::sRGB) {
            // srgb f32 -> linear f32
            std::vector<float> in_buf;
            std::vector<float> out_buf;
            in_buf.resize(assetImageBuffer.data.size() / sizeof(float));
            memcpy(in_buf.data(), assetImageBuffer.data.data(),
                   in_buf.size() * sizeof(float));

            out_buf.resize(assetImageBuffer.data.size() / sizeof(float));

            // TODO: scale factor & bias
            float scale_factor = 1.0f;
            float bias = 0.0f;
            float alpha_scale_factor = 1.0f;
            float alpha_bias = 0.0f;

            bool ret =
                srgb_f32_to_linear_f32(in_buf, width, height, channels,
                                       /* channel stride */ channels, &out_buf, scale_factor, bias, alpha_scale_factor, alpha_bias, &_err);

            if (!ret) {
              PUSH_ERROR_AND_RETURN(
                  "Failed to convert sRGB f32 image to Linear f32 image.");
            }

            imageBuffer.data.resize(assetImageBuffer.data.size());
            memcpy(imageBuffer.data.data(), out_buf.data(),
                   imageBuffer.data.size());


          } else if (texImage.usdColorSpace == tydra::ColorSpace::Lin_sRGB) {
            // no op
            imageBuffer = std::move(assetImageBuffer);

          } else {
            PUSH_ERROR(fmt::format("TODO: Color space {}",
                                   to_string(texImage.usdColorSpace)));
          }

        } else {
          PUSH_ERROR(fmt::format("TODO: asset texture texel format {}",
                                 to_string(assetImageBuffer.componentType)));
        }

      } else {
        // Same color space.
        DCOUT("assetImageBuffer.sz = " << assetImageBuffer.data.size());

        if (assetImageBuffer.componentType == tydra::ComponentType::UInt8) {
          if (env.material_config.preserve_texel_bitdepth) {
            // Do nothing.
            imageBuffer = std::move(assetImageBuffer);

          } else {
            size_t width = size_t(texImage.width);
            size_t height = size_t(texImage.height);
            size_t channels = size_t(texImage.channels);

            // u8 to f32, but no sRGB -> linear conversion(this would break
            // UsdPreviewSurface's spec though)
            PUSH_WARN(
                "8bit sRGB texture is converted to fp32 sRGB texture(without "
                "linearlization)");
            std::vector<float> buf;
            bool ret = u8_to_f32_image(assetImageBuffer.data, width, height,
                                       channels, &buf, &_err);
            if (!ret) {
              PUSH_ERROR_AND_RETURN("Failed to convert u8 image to f32 image.");
            }
            imageBuffer.componentType = tydra::ComponentType::Float;

            imageBuffer.data.resize(buf.size() * sizeof(float));
            memcpy(imageBuffer.data.data(), buf.data(),
                   sizeof(float) * buf.size());
          }

          texImage.colorSpace = texImage.usdColorSpace;

        } else if (assetImageBuffer.componentType ==
                   tydra::ComponentType::Float) {
          // ignore preserve_texel_bitdepth

          // f32 to f32, so no op
          imageBuffer = std::move(assetImageBuffer);

        } else {
          PUSH_ERROR(fmt::format("TODO: asset texture texel format {}",
                                 to_string(assetImageBuffer.componentType)));
        }
      }

      // Assign buffer id
      texImage.buffer_id = int64_t(buffers.size());

      buffers.emplace_back(imageBuffer);

      tex.texture_image_id = int64_t(images.size());

      // Add to image cache for reuse by other textures with same asset path
      imageMap.add(cacheKey, uint64_t(tex.texture_image_id));

      images.emplace_back(texImage);

      std::stringstream ss;
      ss << "Loaded texture image " << assetPath.GetAssetPath()
         << " : buffer_id " + std::to_string(texImage.buffer_id) << "\n";
      ss << "  width x height x components " << texImage.width << " x "
         << texImage.height << " x " << texImage.channels << "\n";
      ss << "  colorSpace " << tinyusdz::tydra::to_string(texImage.colorSpace)
         << "\n";
      PushInfo(ss.str());
    } else {

      tex.texture_image_id = int64_t(images.size());

      // Add to image cache for reuse by other textures with same asset path
      imageMap.add(cacheKey, uint64_t(tex.texture_image_id));

      images.emplace_back(texImage);

      std::stringstream ss;
      ss << "Loaded texture image " << assetPath.GetAssetPath()
         << " : buffer_id " + std::to_string(texImage.buffer_id) << "\n";
      ss << "  width x height x components " << texImage.width << " x "
         << texImage.height << " x " << texImage.channels << "\n";
      ss << "  colorSpace " << tinyusdz::tydra::to_string(texImage.colorSpace)
         << "\n";
      PushInfo(ss.str());

    }
    } // end of image cache else block (image not in cache)
  }

  //
  // Set authored outputChannels
  //
  if (texture.outputsRGB.authored()) {
    tex.authoredOutputChannels.insert(UVTexture::Channel::RGB);
  }

  if (texture.outputsA.authored()) {
    tex.authoredOutputChannels.insert(UVTexture::Channel::A);
  }

  if (texture.outputsR.authored()) {
    tex.authoredOutputChannels.insert(UVTexture::Channel::R);
  }

  if (texture.outputsG.authored()) {
    tex.authoredOutputChannels.insert(UVTexture::Channel::G);
  }

  if (texture.outputsB.authored()) {
    tex.authoredOutputChannels.insert(UVTexture::Channel::B);
  }


  //
  // Convert other UVTexture parameters
  //

  if (texture.bias.authored()) {
    tex.bias = texture.bias.get_value();
  }

  if (texture.scale.authored()) {
    tex.scale = texture.scale.get_value();
  }

  if (texture.st.authored()) {
    if (texture.st.is_connection()) {
      const auto &paths = texture.st.get_connections();
      if (paths.size() != 1) {
        PUSH_ERROR_AND_RETURN(
            "UsdUVTexture inputs:st connection must be single Path.");
      }
      const Path &path = paths[0];

      const Prim *readerPrim{nullptr};
      if (!env.stage.find_prim_at_path(Path(path.prim_part(), ""), readerPrim,
                                       &err)) {
        PUSH_ERROR_AND_RETURN(
            "UsdUVTexture inputs:st connection targetPath not found in the "
            "Stage: " +
            err);
      }

      if (!readerPrim) {
        PUSH_ERROR_AND_RETURN(
            "[InternlError] Invalid Prim connected to inputs:st");
      }

      const Shader *pshader = readerPrim->as<Shader>();
      if (!pshader) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("UsdUVTexture inputs:st connected Prim must be "
                        "Shader Prim, but got {} Prim",
                        readerPrim->prim_type_name()));
      }

      // currently UsdTranform2d or PrimvarReaer_float2 only for inputs:st
      if (const UsdPrimvarReader_float2 *preader =
              pshader->value.as<UsdPrimvarReader_float2>()) {
        if (!preader) {
          PUSH_ERROR_AND_RETURN(
              fmt::format("Shader's info:id must be UsdPrimvarReader_float2, "
                          "but got {}",
                          pshader->info_id));
        }

        // Get value producing attribute(i.e, follow .connection and return
        // terminal Attribute value)
        std::string varname;
        TerminalAttributeValue attr;
        if (!tydra::EvaluateAttribute(env.stage, *readerPrim, "inputs:varname",
                                      &attr, &err)) {
          PUSH_ERROR_AND_RETURN(
              fmt::format("Failed to evaluate UsdPrimvarReader_float2's "
                          "inputs:varname.\n{}",
                          err));
        }

        if (auto pv = attr.as<value::token>()) {
          varname = (*pv).str();
        } else if (auto pvs = attr.as<std::string>()) {
          varname = (*pvs);
        } else if (auto pvsd = attr.as<value::StringData>()) {
          varname = (*pvsd).value;
        } else {
          PUSH_ERROR_AND_RETURN(
              "`inputs:varname` must be `string` or `token` type, but got " +
              attr.type_name());
        }
        if (varname.empty()) {
          PUSH_ERROR_AND_RETURN("`inputs:varname` is empty token.");
        }
        DCOUT("inputs:varname = " << varname);

        tex.varname_uv = varname;
      } else if (const UsdTransform2d *ptransform =
                     pshader->value.as<UsdTransform2d>()) {
        auto result = ConvertTexTransform2d(env.stage, path, *ptransform, &tex,
                                            env.timecode);
        if (!result) {
          PUSH_ERROR_AND_RETURN(result.error());
        }
      } else {
        PUSH_ERROR_AND_RETURN(
            "Unsupported Shader type for `inputs:st` connection: " +
            pshader->info_id + "\n");
      }

    } else {
      //TUSDZ_LOG_I("get_value");
      Animatable<value::texcoord2f> fallbacks = texture.st.get_value();
      value::texcoord2f uv;
      if (fallbacks.get(env.timecode, &uv)) {
        tex.fallback_uv[0] = uv[0];
        tex.fallback_uv[1] = uv[1];
      } else {
        // TODO: report warning.
        PUSH_WARN("Failed to get fallback `st` texcoord attribute.");
      }
      //TUSDZ_LOG_I("uv done");
    }
  }

  if (texture.wrapS.authored()) {
    tinyusdz::UsdUVTexture::Wrap wrap;

    if (!texture.wrapS.get_value().get(env.timecode, &wrap)) {
      PUSH_ERROR_AND_RETURN("Invalid UsdUVTexture inputs:wrapS value.");
    }

    if (wrap == UsdUVTexture::Wrap::Repeat) {
      tex.wrapS = UVTexture::WrapMode::REPEAT;
    } else if (wrap == UsdUVTexture::Wrap::Mirror) {
      tex.wrapS = UVTexture::WrapMode::MIRROR;
    } else if (wrap == UsdUVTexture::Wrap::Clamp) {
      tex.wrapS = UVTexture::WrapMode::CLAMP_TO_EDGE;
    } else if (wrap == UsdUVTexture::Wrap::Black) {
      tex.wrapS = UVTexture::WrapMode::CLAMP_TO_BORDER;
    } else {
      tex.wrapS = UVTexture::WrapMode::CLAMP_TO_EDGE;
    }
  }

  if (texture.wrapT.authored()) {
    tinyusdz::UsdUVTexture::Wrap wrap;

    if (!texture.wrapT.get_value().get(env.timecode, &wrap)) {
      PUSH_ERROR_AND_RETURN("Invalid UsdUVTexture inputs:wrapT value.");
    }

    if (wrap == UsdUVTexture::Wrap::Repeat) {
      tex.wrapT = UVTexture::WrapMode::REPEAT;
    } else if (wrap == UsdUVTexture::Wrap::Mirror) {
      tex.wrapT = UVTexture::WrapMode::MIRROR;
    } else if (wrap == UsdUVTexture::Wrap::Clamp) {
      tex.wrapT = UVTexture::WrapMode::CLAMP_TO_EDGE;
    } else if (wrap == UsdUVTexture::Wrap::Black) {
      tex.wrapT = UVTexture::WrapMode::CLAMP_TO_BORDER;
    } else {
      tex.wrapT = UVTexture::WrapMode::CLAMP_TO_EDGE;
    }
  }

  DCOUT("Converted UVTexture.");

  (*tex_out) = tex;
  return true;
}

template <typename T, typename Dty>
bool RenderSceneConverter::ConvertPreviewSurfaceShaderParam(
    const RenderSceneConverterEnv &env, const Path &shader_abs_path,
    const TypedAttributeWithFallback<Animatable<T>> &param,
    const std::string &param_name, ShaderParam<Dty> &dst_param,
    bool is_materialx) {
  if (!param.authored()) {
    return true;
  }

  if (param.is_blocked()) {
    PUSH_ERROR_AND_RETURN(fmt::format("{} attribute is blocked.", param_name));
  } else if (param.is_connection()) {
    DCOUT(fmt::format("{} is attribute connection.", param_name));

    // Check if this is a MaterialX connection to a NodeGraph
    if (is_materialx && param.get_connections().size() == 1) {
      const Path &conn_path = param.get_connections()[0];
      if (conn_path.prim_part().find("/NodeGraphs") != std::string::npos) {
        // This is a MaterialX NodeGraph connection, traverse to find texture
        const Shader *image_shader{nullptr};
        Path texPath;
        std::string st_varname;
        const AssetInfo *assetInfo{nullptr};

        auto mtlx_result = GetConnectedMtlxTexture(
            env.stage, param, &texPath, &image_shader, &st_varname, &assetInfo,
            env.mesh_config.default_texcoords_primvar_name);

        if (mtlx_result) {
          // Found a MaterialX texture node
          DCOUT("Found MaterialX texture node: " << texPath);

          // Extract the file path from the image shader
          value::AssetPath texAssetPath;
          bool found_file = false;

          // Helper lambda to find file input in a property map
          auto find_file_input = [&](const std::map<std::string, Property>& props_map) -> bool {
            for (const auto& prop : props_map) {
              if (prop.first == "inputs:file" && prop.second.is_attribute()) {
                const Attribute &attr = prop.second.get_attribute();
                if (attr.has_value()) {
                  auto asset_val = attr.get_value<value::AssetPath>();
                  if (asset_val) {
                    texAssetPath = *asset_val;
                    return true;
                  }
                }
              }
            }
            return false;
          };

          // Check both ShaderNode props and Shader props
          const ShaderNode *shader_node = image_shader->value.as<ShaderNode>();
          if (shader_node && !shader_node->props.empty()) {
            found_file = find_file_input(shader_node->props);
          }
          if (!found_file) {
            found_file = find_file_input(image_shader->props);
          }

          if (!found_file) {
            PUSH_WARN(fmt::format("MaterialX image node {} has no file input", texPath.prim_part()));
            return true;
          }

          // Create a synthetic UsdUVTexture to pass to ConvertUVTexture
          UsdUVTexture synth_tex;
          synth_tex.file.set_value(texAssetPath);

          // Helper lambda to extract wrap mode from properties
          auto extract_wrap_modes = [&](const std::map<std::string, Property>& props_map) {
            for (const auto& prop : props_map) {
              if (prop.first == "inputs:uaddressmode" && prop.second.is_attribute()) {
                const Attribute &attr = prop.second.get_attribute();
                if (attr.has_value()) {
                  auto val = attr.get_value<std::string>();
                  if (val) {
                    if (*val == "periodic") {
                      synth_tex.wrapS.set_value(UsdUVTexture::Wrap::Repeat);
                    } else if (*val == "clamp") {
                      synth_tex.wrapS.set_value(UsdUVTexture::Wrap::Clamp);
                    }
                  }
                }
              }
              if (prop.first == "inputs:vaddressmode" && prop.second.is_attribute()) {
                const Attribute &attr = prop.second.get_attribute();
                if (attr.has_value()) {
                  auto val = attr.get_value<std::string>();
                  if (val) {
                    if (*val == "periodic") {
                      synth_tex.wrapT.set_value(UsdUVTexture::Wrap::Repeat);
                    } else if (*val == "clamp") {
                      synth_tex.wrapT.set_value(UsdUVTexture::Wrap::Clamp);
                    }
                  }
                }
              }
            }
          };

          // Map MaterialX wrap modes to USD - check both ShaderNode and Shader props
          if (shader_node && !shader_node->props.empty()) {
            extract_wrap_modes(shader_node->props);
          }
          extract_wrap_modes(image_shader->props);

          // Use ConvertUVTexture to properly handle the texture
          UVTexture rtex;
          AssetInfo mtlx_assetInfo; // Use the assetInfo if available
          if (assetInfo) {
            mtlx_assetInfo = *assetInfo;
          }

          // Handle colorSpace from attribute metadata if available
          // AssetInfo doesn't have set_string, so we'll need to handle this differently
          // For now, just use the assetInfo as-is

          if (!ConvertUVTexture(env, texPath, mtlx_assetInfo, synth_tex, &rtex)) {
            PUSH_ERROR_AND_RETURN(fmt::format(
                "Failed to convert MaterialX texture for {}", param_name));
          }

          // Set the connected output channel and UV primvar name
          rtex.connectedOutputChannel = tydra::UVTexture::Channel::RGB;
          rtex.varname_uv = st_varname;

          uint64_t texId = textures.size();
          textures.push_back(rtex);

          textureMap.add(texId, shader_abs_path.prim_part() + "." + param_name);

          DCOUT(fmt::format("MaterialX TexId {}.{} = {}",
                            shader_abs_path.prim_part(), param_name, texId));

          dst_param.texture_id = int32_t(texId);

          return true;
        } else {
          PUSH_WARN(fmt::format("Failed to find MaterialX texture for {}: {}",
                                param_name, mtlx_result.error()));
        }
      }
    }

    // Fall back to standard UsdUVTexture handling
    const UsdUVTexture *ptex{nullptr};
    const Shader *pshader{nullptr};
    Path texPath;
    auto result =
        GetConnectedUVTexture(env.stage, param, &texPath, &ptex, &pshader);

    if (!result) {
      PUSH_ERROR_AND_RETURN(result.error());
    }

    if (!ptex) {
      PUSH_WARN(fmt::format("[InternalError] ptex is nullptr for parameter '{}' in shader '{}'. Texture not assigned.",
                            param_name, shader_abs_path.full_path_name()));
      // Treat as no texture assigned (e.g., if nullptr for normal map, treat as no normal map)
      return true;
    }
    DCOUT("ptex = " << ptex->name);

    if (!pshader) {
      PUSH_WARN(fmt::format("[InternalError] pshader is nullptr for parameter '{}' in shader '{}'. Texture not assigned.",
                            param_name, shader_abs_path.full_path_name()));
      // Treat as no texture assigned (e.g., if nullptr for normal map, treat as no normal map)
      return true;
    }

    DCOUT("Get connected UsdUVTexture Prim: " << texPath);

    UVTexture rtex;
    const AssetInfo assetInfo = pshader->metas().get_assetInfo_struct();
    if (!ConvertUVTexture(env, texPath, assetInfo, *ptex, &rtex)) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "Failed to convert UVTexture connected to {}", param_name));
    }

    // Extract connected outputChannel from prop part.
    std::string prop_part = texPath.prop_part();

    // TODO: Attribute type check.
    if (prop_part == "outputs:r") {
      rtex.connectedOutputChannel = tydra::UVTexture::Channel::R;
    } else if (prop_part == "outputs:g") {
      rtex.connectedOutputChannel = tydra::UVTexture::Channel::G;
    } else if (prop_part == "outputs:b") {
      rtex.connectedOutputChannel = tydra::UVTexture::Channel::B;
    } else if (prop_part == "outputs:a") {
      rtex.connectedOutputChannel = tydra::UVTexture::Channel::A;
    } else if (prop_part == "outputs:rgb") {
      rtex.connectedOutputChannel = tydra::UVTexture::Channel::RGB;
    } else {
      PUSH_ERROR_AND_RETURN(fmt::format("Unknown or invalid connection to a property of output channel: {}(Abs path {})", prop_part, texPath.full_path_name()));
    }


    uint64_t texId = textures.size();
    textures.push_back(rtex);

    textureMap.add(texId, shader_abs_path.prim_part() + "." + param_name);

    DCOUT(fmt::format("TexId {}.{} = {}",
                      shader_abs_path.prim_part(), param_name, texId));

    dst_param.texture_id = int32_t(texId);

    return true;
  } else {
    T val;
    if (!param.get_value().get(env.timecode, &val)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Failed to get {} at `default` timecode.", param_name));
    }

    dst_param.set_value(val);

    return true;
  }
}

bool RenderSceneConverter::ConvertPreviewSurfaceShader(
    const RenderSceneConverterEnv &env, const Path &shader_abs_path,
    const UsdPreviewSurface &shader, PreviewSurfaceShader *rshader_out) {
  if (!rshader_out) {
    PUSH_ERROR_AND_RETURN("rshader_out arg is nullptr.");
  }

  PreviewSurfaceShader rshader;

  if (shader.useSpecularWorkflow.authored()) {
    if (shader.useSpecularWorkflow.is_blocked()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("useSpecularWorkflow attribute is blocked."));
    } else if (shader.useSpecularWorkflow.is_connection()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("TODO: useSpecularWorkflow with connection."));
    } else {
      int val;
      if (!shader.useSpecularWorkflow.get_value().get(env.timecode, &val)) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Failed to get useSpcularWorkflow value at time `{}`.",
                        env.timecode));
      }

      rshader.useSpecularWorkflow = val ? true : false;
    }
  }

  if (!ConvertPreviewSurfaceShaderParam(env, shader_abs_path,
                                        shader.diffuseColor, "diffuseColor",
                                        rshader.diffuseColor)) {
    return false;
  }

  if (!ConvertPreviewSurfaceShaderParam(env, shader_abs_path,
                                        shader.emissiveColor, "emissiveColor",
                                        rshader.emissiveColor)) {
    return false;
  }

  if (!ConvertPreviewSurfaceShaderParam(env, shader_abs_path,
                                        shader.specularColor, "specularColor",
                                        rshader.specularColor)) {
    return false;
  }

  if (!ConvertPreviewSurfaceShaderParam(env, shader_abs_path, shader.normal,
                                        "normal", rshader.normal)) {
    return false;
  }

  if (!ConvertPreviewSurfaceShaderParam(env, shader_abs_path, shader.roughness,
                                        "roughness", rshader.roughness)) {
    return false;
  }

  if (!ConvertPreviewSurfaceShaderParam(env, shader_abs_path, shader.metallic,
                                        "metallic", rshader.metallic)) {
    return false;
  }

  if (!ConvertPreviewSurfaceShaderParam(env, shader_abs_path, shader.clearcoat,
                                        "clearcoat", rshader.clearcoat)) {
    return false;
  }

  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.clearcoatRoughness, "clearcoatRoughness",
          rshader.clearcoatRoughness)) {
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(env, shader_abs_path, shader.opacity,
                                        "opacity", rshader.opacity)) {
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.opacityThreshold, "opacityThreshold",
          rshader.opacityThreshold)) {
    return false;
  }

  if (!ConvertPreviewSurfaceShaderParam(env, shader_abs_path, shader.ior, "ior",
                                        rshader.ior)) {
    return false;
  }

  if (!ConvertPreviewSurfaceShaderParam(env, shader_abs_path, shader.occlusion,
                                        "occlusion", rshader.occlusion)) {
    return false;
  }

  if (!ConvertPreviewSurfaceShaderParam(env, shader_abs_path,
                                        shader.displacement, "displacement",
                                        rshader.displacement)) {
    return false;
  }

  (*rshader_out) = rshader;
  return true;
}

bool RenderSceneConverter::ConvertOpenPBRSurfaceShader(
    const RenderSceneConverterEnv &env, const Path &shader_abs_path,
    const OpenPBRSurface &shader, OpenPBRSurfaceShader *rshader_out) {
  if (!rshader_out) {
    PUSH_ERROR_AND_RETURN("rshader_out argument is nullptr.");
  }

  OpenPBRSurfaceShader rshader;

  // Convert base layer parameters
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.base_weight, "base_weight",
          rshader.base_weight, true)) {
    PushWarn(fmt::format("Failed to convert base_weight parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.base_color, "base_color",
          rshader.base_color, true)) {
    PushWarn(fmt::format("Failed to convert base_color parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.base_roughness, "base_roughness",
          rshader.base_roughness, true)) {
    PushWarn(fmt::format("Failed to convert base_roughness parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.base_metalness, "base_metalness",
          rshader.base_metalness, true)) {
    PushWarn(fmt::format("Failed to convert base_metalness parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.base_diffuse_roughness, "base_diffuse_roughness",
          rshader.base_diffuse_roughness, true)) {
    PushWarn(fmt::format("Failed to convert base_diffuse_roughness parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }

  // Convert specular layer parameters
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.specular_weight, "specular_weight",
          rshader.specular_weight, true)) {
    PushWarn(fmt::format("Failed to convert specular_weight parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.specular_color, "specular_color",
          rshader.specular_color, true)) {
    PushWarn(fmt::format("Failed to convert specular_color parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.specular_roughness, "specular_roughness",
          rshader.specular_roughness, true)) {
    PushWarn(fmt::format("Failed to convert specular_roughness parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.specular_ior, "specular_ior",
          rshader.specular_ior, true)) {
    PushWarn(fmt::format("Failed to convert specular_ior parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.specular_ior_level, "specular_ior_level",
          rshader.specular_ior_level)) {
    PushWarn(fmt::format("Failed to convert specular_ior_level parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.specular_anisotropy, "specular_anisotropy",
          rshader.specular_anisotropy)) {
    PushWarn(fmt::format("Failed to convert specular_anisotropy parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.specular_rotation, "specular_rotation",
          rshader.specular_rotation)) {
    PushWarn(fmt::format("Failed to convert specular_rotation parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }

  // Convert transmission parameters
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.transmission_weight, "transmission_weight",
          rshader.transmission_weight, true)) {
    PushWarn(fmt::format("Failed to convert transmission_weight parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.transmission_color, "transmission_color",
          rshader.transmission_color, true)) {
    PushWarn(fmt::format("Failed to convert transmission_color parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.transmission_depth, "transmission_depth",
          rshader.transmission_depth)) {
    PushWarn(fmt::format("Failed to convert transmission_depth parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.transmission_scatter, "transmission_scatter",
          rshader.transmission_scatter)) {
    PushWarn(fmt::format("Failed to convert transmission_scatter parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.transmission_scatter_anisotropy,
          "transmission_scatter_anisotropy", rshader.transmission_scatter_anisotropy)) {
    PushWarn(fmt::format("Failed to convert transmission_scatter_anisotropy parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.transmission_dispersion,
          "transmission_dispersion", rshader.transmission_dispersion)) {
    PushWarn(fmt::format("Failed to convert transmission_dispersion parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }

  // Convert subsurface parameters
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.subsurface_weight, "subsurface_weight",
          rshader.subsurface_weight, true)) {
    PushWarn(fmt::format("Failed to convert subsurface_weight parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.subsurface_color, "subsurface_color",
          rshader.subsurface_color, true)) {
    PushWarn(fmt::format("Failed to convert subsurface_color parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.subsurface_radius, "subsurface_radius",
          rshader.subsurface_radius, true)) {
    PushWarn(fmt::format("Failed to convert subsurface_radius parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.subsurface_radius_scale, "subsurface_radius_scale",
          rshader.subsurface_radius_scale, true)) {
    PushWarn(fmt::format("Failed to convert subsurface_radius_scale parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.subsurface_scale, "subsurface_scale",
          rshader.subsurface_scale, true)) {
    PushWarn(fmt::format("Failed to convert subsurface_scale parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.subsurface_anisotropy,
          "subsurface_anisotropy", rshader.subsurface_anisotropy, true)) {
    PushWarn(fmt::format("Failed to convert subsurface_anisotropy parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }

  // Convert sheen parameters
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.sheen_weight, "sheen_weight",
          rshader.sheen_weight, true)) {
    PushWarn(fmt::format("Failed to convert sheen_weight parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.sheen_color, "sheen_color",
          rshader.sheen_color, true)) {
    PushWarn(fmt::format("Failed to convert sheen_color parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.sheen_roughness, "sheen_roughness",
          rshader.sheen_roughness, true)) {
    PushWarn(fmt::format("Failed to convert sheen_roughness parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }

  // Convert fuzz parameters (velvet/fabric-like appearance)
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.fuzz_weight, "fuzz_weight",
          rshader.fuzz_weight, true)) {
    PushWarn(fmt::format("Failed to convert fuzz_weight parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.fuzz_color, "fuzz_color",
          rshader.fuzz_color, true)) {
    PushWarn(fmt::format("Failed to convert fuzz_color parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.fuzz_roughness, "fuzz_roughness",
          rshader.fuzz_roughness, true)) {
    PushWarn(fmt::format("Failed to convert fuzz_roughness parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }

  // Convert thin film parameters (iridescence from thin film interference)
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.thin_film_weight, "thin_film_weight",
          rshader.thin_film_weight, true)) {
    PushWarn(fmt::format("Failed to convert thin_film_weight parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.thin_film_thickness, "thin_film_thickness",
          rshader.thin_film_thickness, true)) {
    PushWarn(fmt::format("Failed to convert thin_film_thickness parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.thin_film_ior, "thin_film_ior",
          rshader.thin_film_ior, true)) {
    PushWarn(fmt::format("Failed to convert thin_film_ior parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }

  // Convert coat layer parameters
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.coat_weight, "coat_weight",
          rshader.coat_weight, true)) {
    PushWarn(fmt::format("Failed to convert coat_weight parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.coat_color, "coat_color",
          rshader.coat_color, true)) {
    PushWarn(fmt::format("Failed to convert coat_color parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.coat_roughness, "coat_roughness",
          rshader.coat_roughness, true)) {
    PushWarn(fmt::format("Failed to convert coat_roughness parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.coat_anisotropy, "coat_anisotropy",
          rshader.coat_anisotropy, true)) {
    PushWarn(fmt::format("Failed to convert coat_anisotropy parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.coat_rotation, "coat_rotation",
          rshader.coat_rotation, true)) {
    PushWarn(fmt::format("Failed to convert coat_rotation parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.coat_ior, "coat_ior",
          rshader.coat_ior, true)) {
    PushWarn(fmt::format("Failed to convert coat_ior parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.coat_affect_color, "coat_affect_color",
          rshader.coat_affect_color, true)) {
    PushWarn(fmt::format("Failed to convert coat_affect_color parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.coat_affect_roughness, "coat_affect_roughness",
          rshader.coat_affect_roughness, true)) {
    PushWarn(fmt::format("Failed to convert coat_affect_roughness parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }

  // Convert emission parameters
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.emission_luminance, "emission_luminance",
          rshader.emission_luminance, true)) {
    PushWarn(fmt::format("Failed to convert emission_luminance parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.emission_color, "emission_color",
          rshader.emission_color, true)) {
    PushWarn(fmt::format("Failed to convert emission_color parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }

  // Convert geometry parameters
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.opacity, "opacity",
          rshader.opacity, true)) {
    PushWarn(fmt::format("Failed to convert opacity parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.normal, "normal",
          rshader.normal, true)) {
    PushWarn(fmt::format("Failed to convert normal parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          env, shader_abs_path, shader.tangent, "tangent",
          rshader.tangent, true)) {
    PushWarn(fmt::format("Failed to convert tangent parameter for shader: {}", shader_abs_path.prim_part()));
    return false;
  }

  // TODO: Convert MaterialX NodeGraph connections to JSON if present
  // This allows reconstruction of node-based shading in JavaScript/WASM
  // NOTE: Currently disabled because GetPrimAtPath returns Prim* not optional<Prim>
  // and ConvertShaderWithNodeGraphToJson is not yet implemented

  // Leave nodeGraphJson empty for now - will be populated when converter is implemented
  (void)shader_abs_path; // Suppress unused variable warning

  (*rshader_out) = rshader;
  return true;
}

bool RenderSceneConverter::ConvertMaterial(const RenderSceneConverterEnv &env,
                                           const Path &mat_abs_path,
                                           const tinyusdz::Material &material,
                                           RenderMaterial *rmat_out) {
  if (!rmat_out) {
    PUSH_ERROR_AND_RETURN("rmat_out argument is nullptr.");
  }

  RenderMaterial rmat;
  rmat.abs_path = mat_abs_path.prim_part();
  rmat.name = mat_abs_path.element_name();
  DCOUT("rmat.abs_path = " << rmat.abs_path);
  DCOUT("rmat.name = " << rmat.name);
  std::string err;
  Path surfacePath;

  //
  // surface shader
  // First try outputs:surface (standard USD), then outputs:mtlx:surface (MaterialX)
  {
    if (material.surface.authored()) {
      auto paths = material.surface.get_connections();
      DCOUT("paths = " << paths);
      // must have single targetPath.
      if (paths.size() != 1) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("{}'s outputs:surface must be connection with single "
                        "target Path.\n",
                        mat_abs_path.full_path_name()));
      }
      surfacePath = paths[0];
    } else {
      // May be PhysicsMaterial?
      // Create dummy material

      PUSH_WARN(fmt::format("{}'s outputs:surface isn't authored, so not a valid Material/Shader. Create a default Material\n",
                      mat_abs_path.full_path_name()));


      (*rmat_out) = rmat;
      return true;

      PUSH_ERROR_AND_RETURN(
          fmt::format("{}'s outputs:surface isn't authored.\n",
                      mat_abs_path.full_path_name()));
    }

    const Prim *shaderPrim{nullptr};
    if (!env.stage.find_prim_at_path(
            Path(surfacePath.prim_part(), /* prop part */ ""), shaderPrim,
            &err)) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "{}'s outputs:surface isn't connected to exising Prim path.\n",
          mat_abs_path.full_path_name()));
    }

    if (!shaderPrim) {
      // this should not happen though.
      PUSH_ERROR_AND_RETURN("[InternalError] invalid Shader Prim.\n");
    }

    const Shader *shader = shaderPrim->as<Shader>();

    if (!shader) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("{}'s outputs:surface must be connected to Shader Prim, "
                      "but connected to `{}` Prim.\n",
                      shaderPrim->prim_type_name()));
    }

    // Check for UsdPreviewSurface, OpenPBRSurface, or MtlxOpenPBRSurface (Blender v4.5+ export)
    const UsdPreviewSurface *psurface = shader->value.as<UsdPreviewSurface>();
    const OpenPBRSurface *openpbr = shader->value.as<OpenPBRSurface>();
    const MtlxOpenPBRSurface *mtlx_openpbr = shader->value.as<MtlxOpenPBRSurface>();

    // prop part must be `outputs:surface` for now.
    if (surfacePath.prop_part() != "outputs:surface") {
      PUSH_ERROR_AND_RETURN(
          fmt::format("{}'s outputs:surface connection must point to property "
                      "`outputs:surface`, but got `{}`",
                      mat_abs_path.full_path_name(), surfacePath.prop_part()));
    }

    if (psurface) {
      // Convert UsdPreviewSurface
      PreviewSurfaceShader pss;
      if (!ConvertPreviewSurfaceShader(env, surfacePath, *psurface, &pss)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Failed to convert UsdPreviewSurface : {}", surfacePath.prim_part()));
      }
      rmat.surfaceShader = pss;
    }

    if (openpbr) {
      // Convert OpenPBRSurface
      OpenPBRSurfaceShader openpbr_shader;
      if (!ConvertOpenPBRSurfaceShader(env, surfacePath, *openpbr, &openpbr_shader)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Failed to convert OpenPBRSurface : {}", surfacePath.prim_part()));
      }
      rmat.openPBRShader = openpbr_shader;
    }

    if (mtlx_openpbr) {
      // Convert MtlxOpenPBRSurface (Blender v4.5+ MaterialX export with ND_open_pbr_surface_surfaceshader)
      // For now, convert it to OpenPBRSurface format by copying compatible parameters
      OpenPBRSurface converted_openpbr;

      // Copy base layer properties
      converted_openpbr.base_weight = mtlx_openpbr->base_weight;
      converted_openpbr.base_color = mtlx_openpbr->base_color;
      converted_openpbr.base_roughness = mtlx_openpbr->base_diffuse_roughness;
      converted_openpbr.base_metalness = mtlx_openpbr->base_metalness;
      converted_openpbr.base_diffuse_roughness = mtlx_openpbr->base_diffuse_roughness;

      // Copy specular properties
      converted_openpbr.specular_weight = mtlx_openpbr->specular_weight;
      converted_openpbr.specular_color = mtlx_openpbr->specular_color;
      converted_openpbr.specular_roughness = mtlx_openpbr->specular_roughness;
      converted_openpbr.specular_ior = mtlx_openpbr->specular_ior;
      converted_openpbr.specular_anisotropy = mtlx_openpbr->specular_anisotropy;
      converted_openpbr.specular_rotation = mtlx_openpbr->specular_rotation;

      // Copy transmission properties
      converted_openpbr.transmission_weight = mtlx_openpbr->transmission_weight;
      converted_openpbr.transmission_color = mtlx_openpbr->transmission_color;
      converted_openpbr.transmission_depth = mtlx_openpbr->transmission_depth;
      converted_openpbr.transmission_scatter = mtlx_openpbr->transmission_scatter;
      converted_openpbr.transmission_scatter_anisotropy = mtlx_openpbr->transmission_scatter_anisotropy;
      converted_openpbr.transmission_dispersion = mtlx_openpbr->transmission_dispersion;

      // Copy subsurface properties
      converted_openpbr.subsurface_weight = mtlx_openpbr->subsurface_weight;
      converted_openpbr.subsurface_color = mtlx_openpbr->subsurface_color;
      converted_openpbr.subsurface_scale = mtlx_openpbr->subsurface_scale;
      converted_openpbr.subsurface_anisotropy = mtlx_openpbr->subsurface_anisotropy;

      // Copy coat properties
      converted_openpbr.coat_weight = mtlx_openpbr->coat_weight;
      converted_openpbr.coat_color = mtlx_openpbr->coat_color;
      converted_openpbr.coat_roughness = mtlx_openpbr->coat_roughness;
      converted_openpbr.coat_anisotropy = mtlx_openpbr->coat_anisotropy;
      converted_openpbr.coat_rotation = mtlx_openpbr->coat_rotation;
      converted_openpbr.coat_ior = mtlx_openpbr->coat_ior;
      // Note: MtlxOpenPBRSurface has float coat_affect_color, OpenPBRSurface has color3f
      // Just skip coat_affect_color conversion for now since types don't match easily
      // TODO: Proper type conversion if needed
      converted_openpbr.coat_affect_roughness = mtlx_openpbr->coat_affect_roughness;

      // Copy fuzz properties (velvet/fabric-like appearance)
      converted_openpbr.fuzz_weight = mtlx_openpbr->fuzz_weight;
      converted_openpbr.fuzz_color = mtlx_openpbr->fuzz_color;
      converted_openpbr.fuzz_roughness = mtlx_openpbr->fuzz_roughness;

      // Copy thin film properties (iridescence)
      converted_openpbr.thin_film_weight = mtlx_openpbr->thin_film_weight;
      converted_openpbr.thin_film_thickness = mtlx_openpbr->thin_film_thickness;
      converted_openpbr.thin_film_ior = mtlx_openpbr->thin_film_ior;

      // Copy emission properties
      converted_openpbr.emission_luminance = mtlx_openpbr->emission_luminance;
      converted_openpbr.emission_color = mtlx_openpbr->emission_color;

      // Copy geometry properties
      converted_openpbr.opacity = mtlx_openpbr->geometry_opacity;
      // Copy normal and tangent if they have values (TypedAttribute -> TypedAttributeWithFallback)
      if (mtlx_openpbr->geometry_normal.has_value()) {
        auto normal_val = mtlx_openpbr->geometry_normal.get_value();
        if (normal_val) {
          converted_openpbr.normal = normal_val.value();
        }
      }
      if (mtlx_openpbr->geometry_tangent.has_value()) {
        auto tangent_val = mtlx_openpbr->geometry_tangent.get_value();
        if (tangent_val) {
          converted_openpbr.tangent = tangent_val.value();
        }
      }

      // Convert to OpenPBRSurfaceShader
      OpenPBRSurfaceShader openpbr_shader;
      if (!ConvertOpenPBRSurfaceShader(env, surfacePath, converted_openpbr, &openpbr_shader)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Failed to convert MtlxOpenPBRSurface : {}", surfacePath.prim_part()));
      }

      // Extract tangent rotation, normal map scale, and normal map texture from NodeGraph connections
      // First, get the material prim from the stage
      PUSH_WARN("DEBUG: Attempting to extract normal map texture from MtlxOpenPBRSurface");
      const Prim *material_prim{nullptr};
      bool found_prim = env.stage.find_prim_at_path(
              Path(mat_abs_path.prim_part(), /* prop part */ ""), material_prim,
              &err);
      PUSH_WARN(fmt::format("DEBUG: find_prim_at_path({}) returned {}, material_prim={}",
                            mat_abs_path.prim_part(), found_prim, (material_prim ? "valid" : "null")));
      if (found_prim && material_prim) {
        // Check if geometry_normal has connections (links to NodeGraph with ND_normalmap node)
        const auto &normal_conns = mtlx_openpbr->geometry_normal.get_connections();
        PUSH_WARN(fmt::format("DEBUG: geometry_normal has {} connections, has_value={}",
                              normal_conns.size(), mtlx_openpbr->geometry_normal.has_value()));
        if (!normal_conns.empty()) {
          auto normal_info_result = ExtractMtlxNodeGraphInfo(
              env.stage, material_prim, normal_conns, &err);
          if (normal_info_result) {
            const auto &normal_info = normal_info_result.value();
            if (normal_info.has_normal_map) {
              openpbr_shader.normal_map_scale = normal_info.normal_map_scale;
              DCOUT("Extracted normal_map_scale: " << normal_info.normal_map_scale);

              // If a normal map texture was found, create a UVTexture entry
              if (!normal_info.normal_map_texture.empty()) {
                // Create a texture image entry
                TextureImage tex_image;
                tex_image.asset_identifier = normal_info.normal_map_texture;
                tex_image.colorSpace = ColorSpace::Raw;  // Normal maps are always raw/linear
                tex_image.usdColorSpace = ColorSpace::Raw;

                // Check if this image already exists via imageMap lookup
                int64_t image_id = -1;
                if (imageMap.count(normal_info.normal_map_texture)) {
                  image_id = static_cast<int64_t>(imageMap.at(normal_info.normal_map_texture));
                }

                // If not found, add the image
                if (image_id < 0) {
                  image_id = static_cast<int64_t>(images.size());
                  imageMap.add(normal_info.normal_map_texture, uint64_t(image_id));
                  images.push_back(tex_image);
                  DCOUT("Added normal map image: " << normal_info.normal_map_texture << " as image_id " << image_id);
                }

                // Create UVTexture entry
                UVTexture uvtex;
                uvtex.texture_image_id = static_cast<int32_t>(image_id);
                uvtex.varname_uv = env.mesh_config.default_texcoords_primvar_name;
                uvtex.connectedOutputChannel = UVTexture::Channel::RGB;
                uvtex.wrapS = UVTexture::WrapMode::REPEAT;
                uvtex.wrapT = UVTexture::WrapMode::REPEAT;

                int32_t tex_id = static_cast<int32_t>(textures.size());
                textures.push_back(uvtex);

                // Set the texture_id on the normal parameter
                openpbr_shader.normal.texture_id = tex_id;

                DCOUT("Created normal map UVTexture with tex_id: " << tex_id);
              }
            }
          }
        }

        // Check if geometry_tangent has connections (links to NodeGraph with ND_rotate3d_vector3 node)
        const auto &tangent_conns = mtlx_openpbr->geometry_tangent.get_connections();
        if (!tangent_conns.empty()) {
          auto tangent_info_result = ExtractMtlxNodeGraphInfo(
              env.stage, material_prim, tangent_conns, &err);
          if (tangent_info_result) {
            const auto &tangent_info = tangent_info_result.value();
            if (tangent_info.has_tangent_rotation) {
              openpbr_shader.tangent_rotation = tangent_info.tangent_rotation;
              DCOUT("Extracted tangent_rotation: " << tangent_info.tangent_rotation);
            }
          }
        }
      }

      rmat.openPBRShader = openpbr_shader;
    }

    if (!psurface && !openpbr && !mtlx_openpbr) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Shader's info:id must be UsdPreviewSurface, OpenPBRSurface, or ND_open_pbr_surface_surfaceshader, but got {}",
                      shader->info_id));
    }
  }

  //
  // Process MaterialX-specific surface shader when MaterialXConfigAPI is present
  // When MaterialXConfigAPI is authored, we look for MaterialX shaders
  {
    // Check if MaterialXConfigAPI is applied (via materialXConfig field)
    // For now, we only check the materialXConfig field as apiSchemas checking would need
    // proper MaterialXConfigAPI enum support in APISchemas::APIName
    bool has_materialx_api = material.materialXConfig.has_value();

    PUSH_WARN(fmt::format("Material {}: materialXConfig.has_value = {}",
                          mat_abs_path.full_path_name(), has_materialx_api));

    if (has_materialx_api) {
      DCOUT("Material has MaterialXConfigAPI, looking for MaterialX shaders");
      PUSH_WARN("Material has MaterialXConfigAPI, looking for MaterialX shaders");

      // First try to parse outputs:mtlx:surface connection
      Path mtlxSurfacePath;
      bool has_mtlx_surface = false;

      // Try to find the connection in various forms
      for (const auto& prop_name : {"outputs:mtlx:surface.connect", "outputs:mtlx:surface"}) {
        auto it = material.props.find(prop_name);
        if (it != material.props.end()) {
          if (it->second.is_relationship()) {
            auto targets = it->second.get_relationTargets();
            if (!targets.empty()) {
              mtlxSurfacePath = targets[0];
              has_mtlx_surface = true;
              DCOUT("Found MaterialX surface connection via relationship: " << mtlxSurfacePath);
              break;
            }
          } else if (it->second.is_attribute()) {
            // Try to extract path from attribute
            auto attr = it->second.get_attribute();
            if (auto token_val = attr.get_value<value::token>()) {
              std::string path_str = token_val.value().str();
              if (!path_str.empty()) {
                // Remove brackets if present
                if (path_str.front() == '<' && path_str.back() == '>') {
                  path_str = path_str.substr(1, path_str.size() - 2);
                }
                // Parse the path
                size_t pos = path_str.find(".outputs:");
                if (pos != std::string::npos) {
                  std::string prim_path = path_str.substr(0, pos);
                  mtlxSurfacePath = Path(prim_path, "");
                  has_mtlx_surface = true;
                  DCOUT("Found MaterialX surface connection via token: " << mtlxSurfacePath);
                  break;
                }
              }
            }
          }
        }
      }

      // If direct connection parsing failed, look for child Shader prims with OpenPBR info:id
      if (!has_mtlx_surface) {
        DCOUT("Direct connection not found, searching for child shaders with OpenPBR info:id");
        PUSH_WARN("Direct connection not found, searching for child shaders with OpenPBR info:id");

        // Get the material prim from the stage to access its children
        const Prim* mat_prim = nullptr;
        if (env.stage.find_prim_at_path(mat_abs_path, mat_prim, &err)) {
          if (mat_prim) {
            // Iterate through children to find OpenPBR shader
            for (const auto& child : mat_prim->children()) {
              const Shader* shader = child.as<Shader>();
              if (shader) {
                // Check if this is an OpenPBR shader by its info:id
                if (shader->info_id == kNdOpenPbrSurfaceSurfaceshader ||
                    shader->info_id == "ND_open_pbr_surface_surfaceshader") {
                  Path child_path = mat_abs_path;
                  child_path = child_path.append_element(child.element_name());
                  mtlxSurfacePath = child_path;
                  has_mtlx_surface = true;
                  DCOUT("Found OpenPBR shader child: " << child_path);
                  PUSH_WARN(fmt::format("Found OpenPBR shader child: {}", child_path.full_path_name()));
                  break;
                }
              }
            }
          }
        }
      }

      // Process the found MaterialX shader
      if (has_mtlx_surface) {
        const Prim *mtlxShaderPrim{nullptr};
        if (!env.stage.find_prim_at_path(
                Path(mtlxSurfacePath.prim_part(), /* prop part */ ""), mtlxShaderPrim,
                &err)) {
          PUSH_WARN(fmt::format(
              "MaterialX shader path {} not found in stage",
              mtlxSurfacePath.full_path_name()));
        } else if (mtlxShaderPrim) {
          const Shader *mtlxShader = mtlxShaderPrim->as<Shader>();

          if (mtlxShader) {
            // Check if it's an OpenPBR shader
            const MtlxOpenPBRSurface *mtlx_openpbr = mtlxShader->value.as<MtlxOpenPBRSurface>();

            if (mtlx_openpbr) {
              DCOUT("Converting MtlxOpenPBRSurface to RenderMaterial");

              // Convert MtlxOpenPBRSurface to OpenPBRSurface
              OpenPBRSurface converted_openpbr;

              // Copy base layer properties
              converted_openpbr.base_weight = mtlx_openpbr->base_weight;
              converted_openpbr.base_color = mtlx_openpbr->base_color;
              converted_openpbr.base_roughness = mtlx_openpbr->base_diffuse_roughness;
              converted_openpbr.base_metalness = mtlx_openpbr->base_metalness;
              converted_openpbr.base_diffuse_roughness = mtlx_openpbr->base_diffuse_roughness;

              // Copy specular properties
              converted_openpbr.specular_weight = mtlx_openpbr->specular_weight;
              converted_openpbr.specular_color = mtlx_openpbr->specular_color;
              converted_openpbr.specular_roughness = mtlx_openpbr->specular_roughness;
              converted_openpbr.specular_ior = mtlx_openpbr->specular_ior;
              converted_openpbr.specular_anisotropy = mtlx_openpbr->specular_anisotropy;
              converted_openpbr.specular_rotation = mtlx_openpbr->specular_rotation;

              // Copy transmission properties
              converted_openpbr.transmission_weight = mtlx_openpbr->transmission_weight;
              converted_openpbr.transmission_color = mtlx_openpbr->transmission_color;
              converted_openpbr.transmission_depth = mtlx_openpbr->transmission_depth;
              converted_openpbr.transmission_scatter = mtlx_openpbr->transmission_scatter;
              converted_openpbr.transmission_scatter_anisotropy = mtlx_openpbr->transmission_scatter_anisotropy;
              converted_openpbr.transmission_dispersion = mtlx_openpbr->transmission_dispersion;

              // Copy subsurface properties
              converted_openpbr.subsurface_weight = mtlx_openpbr->subsurface_weight;
              converted_openpbr.subsurface_color = mtlx_openpbr->subsurface_color;
              converted_openpbr.subsurface_scale = mtlx_openpbr->subsurface_scale;
              converted_openpbr.subsurface_anisotropy = mtlx_openpbr->subsurface_anisotropy;

              // Copy coat properties
              converted_openpbr.coat_weight = mtlx_openpbr->coat_weight;
              converted_openpbr.coat_color = mtlx_openpbr->coat_color;
              converted_openpbr.coat_roughness = mtlx_openpbr->coat_roughness;
              converted_openpbr.coat_anisotropy = mtlx_openpbr->coat_anisotropy;
              converted_openpbr.coat_rotation = mtlx_openpbr->coat_rotation;
              converted_openpbr.coat_ior = mtlx_openpbr->coat_ior;
              converted_openpbr.coat_affect_roughness = mtlx_openpbr->coat_affect_roughness;

              // Copy fuzz properties (velvet/fabric-like appearance)
              converted_openpbr.fuzz_weight = mtlx_openpbr->fuzz_weight;
              converted_openpbr.fuzz_color = mtlx_openpbr->fuzz_color;
              converted_openpbr.fuzz_roughness = mtlx_openpbr->fuzz_roughness;

              // Copy thin film properties (iridescence)
              converted_openpbr.thin_film_weight = mtlx_openpbr->thin_film_weight;
              converted_openpbr.thin_film_thickness = mtlx_openpbr->thin_film_thickness;
              converted_openpbr.thin_film_ior = mtlx_openpbr->thin_film_ior;

              // Copy emission properties
              converted_openpbr.emission_luminance = mtlx_openpbr->emission_luminance;
              converted_openpbr.emission_color = mtlx_openpbr->emission_color;

              // Copy geometry properties
              converted_openpbr.opacity = mtlx_openpbr->geometry_opacity;
              // Copy normal and tangent if they have values
              if (mtlx_openpbr->geometry_normal.has_value()) {
                auto normal_val = mtlx_openpbr->geometry_normal.get_value();
                if (normal_val) {
                  converted_openpbr.normal = normal_val.value();
                }
              }
              if (mtlx_openpbr->geometry_tangent.has_value()) {
                auto tangent_val = mtlx_openpbr->geometry_tangent.get_value();
                if (tangent_val) {
                  converted_openpbr.tangent = tangent_val.value();
                }
              }

              // Convert to OpenPBRSurfaceShader
              OpenPBRSurfaceShader openpbr_shader;
              if (!ConvertOpenPBRSurfaceShader(env, mtlxSurfacePath, converted_openpbr, &openpbr_shader)) {
                PUSH_WARN(fmt::format(
                    "Failed to convert MtlxOpenPBRSurface : {}", mtlxSurfacePath.prim_part()));
              } else {
                // Extract normal map texture from NodeGraph connections
                PUSH_WARN("DEBUG: MaterialXConfigAPI path - extracting normal map texture");

                // Get the material prim to access NodeGraph children
                const Prim* material_prim_for_ng = nullptr;
                if (!env.stage.find_prim_at_path(mat_abs_path, material_prim_for_ng, &err)) {
                  PUSH_WARN(fmt::format("DEBUG: Could not find material prim at {}", mat_abs_path.full_path_name()));
                  material_prim_for_ng = nullptr;
                }

                const auto &normal_conns = mtlx_openpbr->geometry_normal.get_connections();
                PUSH_WARN(fmt::format("DEBUG: geometry_normal has {} connections", normal_conns.size()));
                if (!normal_conns.empty() && material_prim_for_ng) {
                  PUSH_WARN(fmt::format("DEBUG: First connection path: {}", normal_conns[0].full_path_name()));
                  std::string extract_debug;
                  auto normal_info_result = ExtractMtlxNodeGraphInfo(
                      env.stage, material_prim_for_ng, normal_conns, &extract_debug);
                  if (!extract_debug.empty()) {
                    PUSH_WARN(fmt::format("ExtractMtlxNodeGraphInfo debug:\n{}", extract_debug));
                  }
                  if (normal_info_result) {
                    const auto &normal_info = normal_info_result.value();
                    PUSH_WARN(fmt::format("DEBUG: ExtractMtlxNodeGraphInfo returned: has_normal_map={}, normal_map_scale={}, normal_map_texture='{}'",
                                          normal_info.has_normal_map, normal_info.normal_map_scale, normal_info.normal_map_texture));
                    if (normal_info.has_normal_map) {
                      openpbr_shader.normal_map_scale = normal_info.normal_map_scale;
                      PUSH_WARN(fmt::format("DEBUG: Extracted normal_map_scale: {}", normal_info.normal_map_scale));

                      // If a normal map texture was found, create a UVTexture entry
                      if (!normal_info.normal_map_texture.empty()) {
                        PUSH_WARN(fmt::format("DEBUG: Found normal_map_texture: {}", normal_info.normal_map_texture));
                        // Create a texture image entry
                        TextureImage tex_image;
                        tex_image.asset_identifier = normal_info.normal_map_texture;
                        tex_image.colorSpace = ColorSpace::Raw;  // Normal maps are always raw/linear
                        tex_image.usdColorSpace = ColorSpace::Raw;

                        // Check if this image already exists via imageMap lookup
                        int64_t image_id = -1;
                        if (imageMap.count(normal_info.normal_map_texture)) {
                          image_id = static_cast<int64_t>(imageMap.at(normal_info.normal_map_texture));
                        }

                        // If not found, add the image
                        if (image_id < 0) {
                          image_id = static_cast<int64_t>(images.size());
                          imageMap.add(normal_info.normal_map_texture, uint64_t(image_id));
                          images.push_back(tex_image);
                          PUSH_WARN(fmt::format("DEBUG: Added normal map image: {} as image_id {}", normal_info.normal_map_texture, image_id));
                        }

                        // Create UVTexture entry
                        UVTexture uvtex;
                        uvtex.texture_image_id = static_cast<int32_t>(image_id);
                        uvtex.varname_uv = env.mesh_config.default_texcoords_primvar_name;
                        uvtex.connectedOutputChannel = UVTexture::Channel::RGB;
                        uvtex.wrapS = UVTexture::WrapMode::REPEAT;
                        uvtex.wrapT = UVTexture::WrapMode::REPEAT;

                        int32_t tex_id = static_cast<int32_t>(textures.size());
                        textures.push_back(uvtex);

                        // Set the texture_id on the normal parameter
                        openpbr_shader.normal.texture_id = tex_id;

                        PUSH_WARN(fmt::format("DEBUG: Created normal map UVTexture with tex_id: {}", tex_id));
                      }
                    }
                  } else {
                    PUSH_WARN(fmt::format("DEBUG: ExtractMtlxNodeGraphInfo failed: {}", err));
                  }
                }

                // Extract tangent rotation from NodeGraph connections
                const auto &tangent_conns = mtlx_openpbr->geometry_tangent.get_connections();
                if (!tangent_conns.empty() && material_prim_for_ng) {
                  auto tangent_info_result = ExtractMtlxNodeGraphInfo(
                      env.stage, material_prim_for_ng, tangent_conns, &err);
                  if (tangent_info_result) {
                    const auto &tangent_info = tangent_info_result.value();
                    if (tangent_info.has_tangent_rotation) {
                      openpbr_shader.tangent_rotation = tangent_info.tangent_rotation;
                      PUSH_WARN(fmt::format("DEBUG: Extracted tangent_rotation: {}", tangent_info.tangent_rotation));
                    }
                  }
                }

                rmat.openPBRShader = openpbr_shader;
                DCOUT("Successfully attached MaterialX OpenPBR shader to RenderMaterial");
                PUSH_WARN(fmt::format("Successfully attached MaterialX OpenPBR shader to RenderMaterial: {}",
                                      mtlxSurfacePath.full_path_name()));
              }
            } else {
              PUSH_WARN(fmt::format(
                  "Found shader {} but it's not ND_open_pbr_surface_surfaceshader (got {})",
                  mtlxSurfacePath.prim_part(), mtlxShader->info_id));
            }
          }
        }
      } else {
        DCOUT("No MaterialX OpenPBR shader found for material with MaterialXConfigAPI");
      }
    }
  }

  DCOUT("Converted Material: " << mat_abs_path);

  (*rmat_out) = rmat;
  return true;
}

namespace {

struct MeshVisitorEnv {
  RenderSceneConverter *converter{nullptr};
  const RenderSceneConverterEnv *env{nullptr};

  // Progress tracking for detailed progress reporting
  size_t meshes_processed{0};
  size_t meshes_total{0};
  size_t materials_processed{0};
  size_t materials_total{0};

  // Pre-discovered skeleton/animation prims for ancestor-based discovery
  const PathPrimMap<Skeleton> *allSkeletons{nullptr};
  const PathPrimMap<SkelRoot> *allSkelRoots{nullptr};
  const PathPrimMap<SkelAnimation> *allAnimations{nullptr};
};

bool MeshVisitor(const tinyusdz::Path &abs_path, const tinyusdz::Prim &prim,
                 const int32_t level, void *userdata, std::string *err) {
  if (!userdata) {
    if (err) {
      (*err) += "userdata pointer must be filled.";
    }
    return false;
  }

  MeshVisitorEnv *visitorEnv = reinterpret_cast<MeshVisitorEnv *>(userdata);

  if (level > 1024 * 1024) {
    if (err) {
      (*err) += "Scene graph is too deep.\n";
    }
    // Too deep
    return false;
  }

  // Lambda to convert and cache bound materials - shared by all geometry types
  auto ConvertBoundMaterial = [&](const Path &bound_material_path,
                                  const tinyusdz::Material *bound_material,
                                  int64_t &rmaterial_id) -> bool {
    std::vector<RenderMaterial> &rmaterials =
        visitorEnv->converter->materials;

    const auto matIt = visitorEnv->converter->materialMap.find(
        bound_material_path.full_path_name());

    if (matIt != visitorEnv->converter->materialMap.s_end()) {
      // Got material in the cache.
      uint64_t mat_id = matIt->second;
      if (mat_id >= visitorEnv->converter->materials
                        .size()) {  // this should not happen though
        if (err) {
          (*err) += "Material index out-of-range.\n";
        }
        return false;
      }

      if (mat_id >= size_t((std::numeric_limits<int32_t>::max)())) {
        if (err) {
          (*err) += "Material index too large.\n";
        }
        return false;
      }

      rmaterial_id = int64_t(mat_id);

    } else {
      RenderMaterial rmat;
      if (!visitorEnv->converter->ConvertMaterial(*visitorEnv->env,
                                                  bound_material_path,
                                                  *bound_material, &rmat)) {
        if (err) {
          (*err) += fmt::format("Material conversion failed: {}",
                                bound_material_path);
        }
        return false;
      }

      // Assign new material ID
      uint64_t mat_id = rmaterials.size();

      if (mat_id >= uint64_t((std::numeric_limits<int32_t>::max)())) {
        if (err) {
          (*err) += "Material index too large.\n";
        }
        return false;
      }
      rmaterial_id = int64_t(mat_id);

      visitorEnv->converter->materialMap.add(
          bound_material_path.full_path_name(), uint64_t(rmaterial_id));
      DCOUT("Added renderMaterial: " << mat_id << " " << rmat.abs_path
                                     << " ( " << rmat.name << " ) ");

      rmaterials.push_back(rmat);
    }

    return true;
  };

  if (const tinyusdz::GeomMesh *pmesh = prim.as<tinyusdz::GeomMesh>()) {
    // Collect GeomSubsets
    // std::vector<const tinyusdz::GeomSubset *> subsets = GetGeomSubsets(;

    DCOUT("Mesh: " << abs_path);

    if (!pmesh->points.authored()) {
      // Maybe Collider mesh? Ignore for now.
      DCOUT(fmt::format("Mesh {} does not author `points` attribute(Maybe Collider mesh?). Ignore it for now", abs_path));
      return true;
    }

    //
    // First convert Material assigned to GeomMesh.
    //
    // - If prim has GeomSubset with materialBind, convert it to per-face
    // material.
    // - If prim has materialBind, convert it to RenderMesh's material.
    //

    // Convert bound materials in GeomSubsets
    //
    // key: subset Prim name
    std::map<std::string, MaterialPath> subset_material_path_map;
    std::vector<const GeomSubset *> material_subsets;
    {
      material_subsets = GetMaterialBindGeomSubsets(prim);

      for (const auto &psubset : material_subsets) {
        MaterialPath mpath;
        mpath.default_texcoords_primvar_name =
            visitorEnv->env->mesh_config.default_texcoords_primvar_name;

        Path subset_abs_path = abs_path.AppendElement(psubset->name);

        // front and back
        {
          tinyusdz::Path bound_material_path;
          const tinyusdz::Material *bound_material{nullptr};
          bool ret = tinyusdz::tydra::GetBoundMaterial(
              visitorEnv->env->stage,
              /* GeomSubset prim path */ subset_abs_path,
              /* purpose */ "", &bound_material_path, &bound_material, err);

          if (ret && bound_material) {
            int64_t rmaterial_id = -1;  // not used.

            if (!ConvertBoundMaterial(bound_material_path, bound_material,
                                      rmaterial_id)) {
              if (err) {
                (*err) += "Convert boundMaterial failed: " + bound_material_path.full_path_name();
              }
              return false;
            }

            mpath.material_path = bound_material_path.full_path_name();
            DCOUT("GeomSubset " << subset_abs_path << " : Bound material path: "
                                << mpath.backface_material_path);
          }
        }

        std::string backface_purpose =
            visitorEnv->env->material_config
                .default_backface_material_purpose_name;

        if (!backface_purpose.empty() &&
            psubset->has_materialBinding(value::token(backface_purpose))) {
          DCOUT("backface_material_purpose "
                << visitorEnv->env->material_config
                       .default_backface_material_purpose_name);
          tinyusdz::Path bound_material_path;
          const tinyusdz::Material *bound_material{nullptr};
          bool ret = tinyusdz::tydra::GetBoundMaterial(
              visitorEnv->env->stage,
              /* GeomSubset prim path */ subset_abs_path,
              /* purpose */
              visitorEnv->env->material_config
                  .default_backface_material_purpose_name,
              &bound_material_path, &bound_material, err);

          if (ret && bound_material) {
            int64_t rmaterial_id = -1;  // not used

            if (!ConvertBoundMaterial(bound_material_path, bound_material,
                                      rmaterial_id)) {
              if (err) {
                (*err) += "Convert boundMaterial failed: " + bound_material_path.full_path_name();
              }
              return false;
            }

            mpath.backface_material_path = bound_material_path.full_path_name();
            DCOUT("GeomSubset " << subset_abs_path
                                << " : Bound backface material path: "
                                << mpath.backface_material_path);
          }
        }

        subset_material_path_map[psubset->name] = mpath;
      }
    }

    MaterialPath material_path;
    material_path.default_texcoords_primvar_name =
        visitorEnv->env->mesh_config.default_texcoords_primvar_name;
    // TODO: Implement feature to assign default material
    // id(MaterialPath::default_material_id) when no bound material found.

    {
      const std::string mesh_path_str = abs_path.full_path_name();

      // Front and back material.
      {
        tinyusdz::Path bound_material_path;
        const tinyusdz::Material *bound_material{nullptr};
        bool ret = tinyusdz::tydra::GetBoundMaterial(
            visitorEnv->env->stage, /* GeomMesh prim path */ abs_path,
            /* purpose */ "", &bound_material_path, &bound_material, err);

        DCOUT("Bound material found: " << ret);
        if (ret && bound_material) {
          int64_t rmaterial_id = -1;  // not used

          if (!ConvertBoundMaterial(bound_material_path, bound_material,
                                    rmaterial_id)) {
            if (err) {
              (*err) += "Convert boundMaterial failed: " + bound_material_path.full_path_name();
            }
            return false;
          }

          material_path.material_path = bound_material_path.full_path_name();
          DCOUT("Bound material path: " << material_path.material_path);
        }
      }

      std::string backface_purpose =
          visitorEnv->env->material_config
              .default_backface_material_purpose_name;

      if (!backface_purpose.empty() &&
          pmesh->has_materialBinding(value::token(backface_purpose))) {
        tinyusdz::Path bound_material_path;
        const tinyusdz::Material *bound_material{nullptr};
        bool ret = tinyusdz::tydra::GetBoundMaterial(
            visitorEnv->env->stage, /* GeomMesh prim path */ abs_path,
            /* purpose */
            visitorEnv->env->material_config
                .default_backface_material_purpose_name,
            &bound_material_path, &bound_material, err);

        if (ret && bound_material) {
          int64_t rmaterial_id = -1;  // not used

          if (!ConvertBoundMaterial(bound_material_path, bound_material,
                                    rmaterial_id)) {
            if (err) {
              (*err) += "Convert boundMaterial failed: " + bound_material_path.full_path_name();
            }
            return false;
          }

          material_path.backface_material_path =
              bound_material_path.full_path_name();
          DCOUT("Bound backface material path: "
                << material_path.backface_material_path);
        }
      }

      // BlendShapes
      std::vector<std::pair<std::string, const BlendShape *>> blendshapes;
      {
        std::string local_err;
        blendshapes = GetBlendShapes(visitorEnv->env->stage, prim, &local_err);
        if (local_err.size()) {
          if (err) {
            (*err) += fmt::format("Failed to get BlendShapes prims. err = {}", local_err);
          }
        }
      }
      DCOUT("# of blendshapes : " << blendshapes.size());

      RenderMesh rmesh;

      if (!visitorEnv->converter->ConvertMesh(
              *visitorEnv->env, abs_path, *pmesh, material_path,
              subset_material_path_map, visitorEnv->converter->materialMap,
              material_subsets, blendshapes, &rmesh)) {
        if (err) {
          (*err) += fmt::format("Mesh conversion failed: {}",
                                abs_path.full_path_name());
          (*err) += "\n" + visitorEnv->converter->GetError() + "\n";

        }
        return false;
      }

      uint64_t mesh_id = uint64_t(visitorEnv->converter->meshes.size());
      if (mesh_id >= size_t((std::numeric_limits<int32_t>::max)())) {
        if (err) {
          (*err) += "Mesh index too large.\n";
        }
        return false;
      }
      visitorEnv->converter->meshMap.add(abs_path.full_path_name(), mesh_id);

      visitorEnv->converter->meshes.emplace_back(std::move(rmesh));

      // Report mesh progress
      visitorEnv->meshes_processed++;
      std::string msg = "Converting mesh " +
          std::to_string(visitorEnv->meshes_processed) + "/" +
          std::to_string(visitorEnv->meshes_total);
      visitorEnv->converter->ReportMeshProgress(
          visitorEnv->meshes_processed, visitorEnv->meshes_total,
          abs_path.full_path_name(), msg);
      DCOUT("[Tydra] Mesh " << visitorEnv->meshes_processed << "/" << visitorEnv->meshes_total
            << ": " << abs_path.full_path_name());
    }
  }

  // Handle GeomCube primitives by converting to mesh
  if (const tinyusdz::GeomCube *pcube = prim.as<tinyusdz::GeomCube>()) {
    DCOUT("Cube: " << abs_path);

    // Get material binding (same logic as GeomMesh)
    MaterialPath material_path;
    std::map<std::string, MaterialPath> subset_material_path_map;

    {
      const Material *bound_material{nullptr};
      Path bound_material_path;

      bool ret = GetBoundMaterial(
          visitorEnv->env->stage, abs_path,
          /* purpose */ "",
          &bound_material_path, &bound_material, err);

      if (ret && bound_material) {
        int64_t rmaterial_id = -1;

        if (!ConvertBoundMaterial(
                bound_material_path, bound_material, rmaterial_id)) {
          if (err) {
            (*err) += "Convert boundMaterial failed: " +
                      bound_material_path.full_path_name();
          }
          return false;
        }

        material_path.material_path = bound_material_path.full_path_name();
        DCOUT("Bound material path: " << material_path.material_path);
      }
    }

    RenderMesh rmesh;
    std::vector<const tinyusdz::GeomSubset *> material_subsets;  // Cubes don't have subsets
    std::vector<std::pair<std::string, const tinyusdz::BlendShape *>> blendshapes;  // Cubes don't have blendshapes

    if (!visitorEnv->converter->ConvertCube(
            *visitorEnv->env, abs_path, *pcube, material_path,
            subset_material_path_map, visitorEnv->converter->materialMap,
            material_subsets, blendshapes, &rmesh)) {
      if (err) {
        (*err) += fmt::format("Cube conversion failed: {}",
                              abs_path.full_path_name());
        (*err) += "\n" + visitorEnv->converter->GetError() + "\n";
      }
      return false;
    }

    uint64_t mesh_id = uint64_t(visitorEnv->converter->meshes.size());
    if (mesh_id >= size_t((std::numeric_limits<int32_t>::max)())) {
      if (err) {
        (*err) += "Mesh index too large.\n";
      }
      return false;
    }
    visitorEnv->converter->meshMap.add(abs_path.full_path_name(), mesh_id);
    visitorEnv->converter->meshes.emplace_back(std::move(rmesh));

    // Report mesh progress (cube)
    visitorEnv->meshes_processed++;
    std::string msg = "Converting cube " +
        std::to_string(visitorEnv->meshes_processed) + "/" +
        std::to_string(visitorEnv->meshes_total);
    visitorEnv->converter->ReportMeshProgress(
        visitorEnv->meshes_processed, visitorEnv->meshes_total,
        abs_path.full_path_name(), msg);
    DCOUT("[Tydra] Mesh " << visitorEnv->meshes_processed << "/" << visitorEnv->meshes_total
          << " (cube): " << abs_path.full_path_name());
  }

  // Handle GeomSphere primitives by converting to mesh
  if (const tinyusdz::GeomSphere *psphere = prim.as<tinyusdz::GeomSphere>()) {
    DCOUT("Sphere: " << abs_path);

    // Get material binding (same logic as GeomMesh)
    MaterialPath material_path;
    std::map<std::string, MaterialPath> subset_material_path_map;

    {
      const Material *bound_material{nullptr};
      Path bound_material_path;

      bool ret = GetBoundMaterial(
          visitorEnv->env->stage, abs_path,
          /* purpose */ "",
          &bound_material_path, &bound_material, err);

      if (ret && bound_material) {
        int64_t rmaterial_id = -1;

        if (!ConvertBoundMaterial(
                bound_material_path, bound_material, rmaterial_id)) {
          if (err) {
            (*err) += "Convert boundMaterial failed: " +
                      bound_material_path.full_path_name();
          }
          return false;
        }

        material_path.material_path = bound_material_path.full_path_name();
        DCOUT("Bound material path: " << material_path.material_path);
      }
    }

    RenderMesh rmesh;
    std::vector<const tinyusdz::GeomSubset *> material_subsets;  // Spheres don't have subsets
    std::vector<std::pair<std::string, const tinyusdz::BlendShape *>> blendshapes;  // Spheres don't have blendshapes

    if (!visitorEnv->converter->ConvertSphere(
            *visitorEnv->env, abs_path, *psphere, material_path,
            subset_material_path_map, visitorEnv->converter->materialMap,
            material_subsets, blendshapes, &rmesh)) {
      if (err) {
        (*err) += fmt::format("Sphere conversion failed: {}",
                              abs_path.full_path_name());
        (*err) += "\n" + visitorEnv->converter->GetError() + "\n";
      }
      return false;
    }

    uint64_t mesh_id = uint64_t(visitorEnv->converter->meshes.size());
    if (mesh_id >= size_t((std::numeric_limits<int32_t>::max)())) {
      if (err) {
        (*err) += "Mesh index too large.\n";
      }
      return false;
    }
    visitorEnv->converter->meshMap.add(abs_path.full_path_name(), mesh_id);
    visitorEnv->converter->meshes.emplace_back(std::move(rmesh));

    // Report mesh progress (sphere)
    visitorEnv->meshes_processed++;
    std::string msg = "Converting sphere " +
        std::to_string(visitorEnv->meshes_processed) + "/" +
        std::to_string(visitorEnv->meshes_total);
    visitorEnv->converter->ReportMeshProgress(
        visitorEnv->meshes_processed, visitorEnv->meshes_total,
        abs_path.full_path_name(), msg);
    DCOUT("[Tydra] Mesh " << visitorEnv->meshes_processed << "/" << visitorEnv->meshes_total
          << " (sphere): " << abs_path.full_path_name());
  }

  return true;  // continue traversal
}

}  // namespace

bool RenderSceneConverter::ConvertSkelAnimation(const RenderSceneConverterEnv &env,
                                            const Path &abs_path,
                                            const SkelAnimation &skelAnim,
                                            int32_t skeleton_id,
                                            AnimationClip *anim_out) {
  // The spec says:
  // "An animation source is only valid if its translation, rotation, and scale components
  //  are all authored, storing arrays sized to the same size as the authored joints array."
  //
  // Convert USD SkelAnimation to glTF/Three.js compatible AnimationClip structure
  // with flat sampler arrays and channel bindings

  std::vector<value::token> joints;

  if (skelAnim.joints.authored()) {
    if (!EvaluateTypedAttribute(env.stage, skelAnim.joints, "joints", &joints, &_err)) {
      PUSH_ERROR_AND_RETURN(fmt::format("Failed to evaluate `joints` in SkelAnimation Prim : {}", abs_path));
    }

    if (!skelAnim.rotations.authored() ||
        !skelAnim.translations.authored() ||
        !skelAnim.scales.authored()) {

      PUSH_ERROR_AND_RETURN(fmt::format("`translations`, `rotations` and `scales` must be all authored for SkelAnimation Prim {}. authored flags: translations {}, rotations {}, scales {}", abs_path, skelAnim.translations.authored() ? "yes" : "no",
      skelAnim.rotations.authored() ? "yes" : "no",
      skelAnim.scales.authored() ? "yes" : "no"));
    }
  }

  // TODO: inbetweens BlendShape
  std::vector<value::token> blendShapes;
  if (skelAnim.blendShapes.authored()) {
    if (!EvaluateTypedAttribute(env.stage, skelAnim.blendShapes, "blendShapes", &blendShapes, &_err)) {
      PUSH_ERROR_AND_RETURN(fmt::format("Failed to evaluate `blendShapes` in SkelAnimation Prim : {}", abs_path));
    }

    if (!skelAnim.blendShapeWeights.authored()) {
      PUSH_ERROR_AND_RETURN(fmt::format("`blendShapeWeights` must be authored for SkelAnimation Prim {}", abs_path));
    }
  }

  // Setup basic metadata
  anim_out->abs_path = abs_path.full_path_name();
  anim_out->prim_name = skelAnim.name;
  anim_out->name = skelAnim.name;
  anim_out->display_name = skelAnim.metas().has_displayName() ? skelAnim.metas().get_displayName() : "";
  anim_out->duration = 0.0f;  // Will be computed below
  anim_out->source_type = AnimationSourceType::SkelAnimation;
  anim_out->num_animated_joints = int32_t(joints.size());

  // Joint animations - convert to glTF-style flat arrays
  // Strategy: Pre-allocate output samplers, then scatter data directly from
  // TypedTimeSamples into per-joint samplers. Avoids copying all frame data
  // into intermediate vectors.
  if (joints.size()) {
    if (skeleton_id < 0 || skeleton_id >= int32_t(skeletons.size())) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "Invalid skeleton_id {} for SkelAnimation {}",
          skeleton_id, abs_path.full_path_name()));
    }

    // SkelAnimation::joints ordering may differ from Skeleton::joints ordering.
    // Build an explicit animation-joint -> skeleton-joint remap once, then use
    // canonical skeleton joint IDs in all emitted channels.
    auto cache_it = _skelNameToIndexCache.find(skeleton_id);
    if (cache_it == _skelNameToIndexCache.end()) {
      cache_it = _skelNameToIndexCache
                     .emplace(skeleton_id,
                              BuildSkelNameToIndexMap(skeletons[size_t(skeleton_id)]))
                     .first;
    }
    const auto &token_to_index_map = cache_it->second;

    auto normalize_joint_token = [](const std::string &token) {
      if (!token.empty() && token[0] == '/') {
        return token.substr(1);
      }
      return token;
    };

    std::unordered_map<std::string, int32_t> normalized_to_index;
    normalized_to_index.reserve(token_to_index_map.size() * 2);
    for (const auto &item : token_to_index_map) {
      normalized_to_index[normalize_joint_token(item.first)] = int32_t(item.second);
    }

    Animatable<std::vector<value::float3>> translations;
    if (!skelAnim.translations.get_value(&translations)) {
      PUSH_ERROR_AND_RETURN(fmt::format("Failed to get `translations` attribute of SkelAnimation: {}", abs_path));
    }

    Animatable<std::vector<value::quatf>> rotations;
    if (!skelAnim.rotations.get_value(&rotations)) {
      PUSH_ERROR_AND_RETURN(fmt::format("Failed to get `rotations` attribute of SkelAnimation: {}", abs_path));
    }

    Animatable<std::vector<value::half3>> scales;
    if (!skelAnim.scales.get_value(&scales)) {
      PUSH_ERROR_AND_RETURN(fmt::format("Failed to get `scales` attribute of SkelAnimation: {}", abs_path));
    }

    size_t nJoints = joints.size();
    std::vector<int32_t> anim_joint_to_skel_joint(nJoints, -1);
    for (size_t j = 0; j < nJoints; j++) {
      const std::string joint_token = joints[j].str();

      auto token_it = token_to_index_map.find(joint_token);
      if (token_it != token_to_index_map.end()) {
        anim_joint_to_skel_joint[j] = int32_t(token_it->second);
        continue;
      }

      auto norm_it = normalized_to_index.find(normalize_joint_token(joint_token));
      if (norm_it != normalized_to_index.end()) {
        anim_joint_to_skel_joint[j] = norm_it->second;
        continue;
      }

      PUSH_ERROR_AND_RETURN(fmt::format(
          "SkelAnimation joint token '{}' is not found in Skeleton {} (id={})",
          joint_token, skeletons[size_t(skeleton_id)].abs_path, skeleton_id));
    }

    // Count frames for each property (first pass - no data copy)
    size_t nTransTimes = 0, nRotTimes = 0, nScaleTimes = 0;
    if (translations.has_timesamples()) {
      nTransTimes = translations.get_timesamples().size();
    } else if (translations.has_value()) {
      nTransTimes = 1;
    }
    if (rotations.has_timesamples()) {
      nRotTimes = rotations.get_timesamples().size();
    } else if (rotations.has_value()) {
      nRotTimes = 1;
    }
    if (scales.has_timesamples()) {
      nScaleTimes = scales.get_timesamples().size();
    } else if (scales.has_value()) {
      nScaleTimes = 1;
    }

    // Pre-allocate all output samplers and channels
    size_t nProps = (nTransTimes ? 1 : 0) + (nRotTimes ? 1 : 0) + (nScaleTimes ? 1 : 0);
    size_t totalSamplers = nJoints * nProps;
    size_t baseSamplerIdx = anim_out->samplers.size();
    anim_out->samplers.resize(baseSamplerIdx + totalSamplers);
    size_t baseChannelIdx = anim_out->channels.size();
    anim_out->channels.resize(baseChannelIdx + totalSamplers);

    // Allocate flat bulk buffers for scatter writes (avoids per-sampler resize zero-fill).
    // Layout: [joint0_frame0, joint0_frame1, ..., joint1_frame0, ...] (joint-major)
    // Scatter writes in frame-major order; final copy to per-sampler vectors is joint-major memcpy.
    size_t transBufTimesSize = nTransTimes ? nJoints * nTransTimes : 0;
    size_t transBufValsSize  = nTransTimes ? nJoints * nTransTimes * 3 : 0;
    size_t rotBufTimesSize   = nRotTimes   ? nJoints * nRotTimes : 0;
    size_t rotBufValsSize    = nRotTimes   ? nJoints * nRotTimes * 4 : 0;
    size_t scaleBufTimesSize = nScaleTimes ? nJoints * nScaleTimes : 0;
    size_t scaleBufValsSize  = nScaleTimes ? nJoints * nScaleTimes * 3 : 0;

    // Single allocation for all bulk data
    size_t totalFloats = transBufTimesSize + transBufValsSize
                       + rotBufTimesSize + rotBufValsSize
                       + scaleBufTimesSize + scaleBufValsSize;
    std::unique_ptr<float[]> bulkBuf(new float[totalFloats]);
    float *ptr = bulkBuf.get();

    float *transTimesBuf = ptr; ptr += transBufTimesSize;
    float *transValsBuf  = ptr; ptr += transBufValsSize;
    float *rotTimesBuf   = ptr; ptr += rotBufTimesSize;
    float *rotValsBuf    = ptr; ptr += rotBufValsSize;
    float *scaleTimesBuf = ptr; ptr += scaleBufTimesSize;
    float *scaleValsBuf  = ptr; ptr += scaleBufValsSize;

    // Setup channels (no value arrays yet — will be assigned after scatter)
    for (size_t j = 0; j < nJoints; j++) {
      const int32_t resolved_joint_id = anim_joint_to_skel_joint[j];
      size_t samplerOff = baseSamplerIdx + j * nProps;
      size_t channelOff = baseChannelIdx + j * nProps;
      size_t pi = 0;
      if (nTransTimes) {
        anim_out->samplers[samplerOff + pi].interpolation = AnimationInterpolation::Linear;
        auto &ch = anim_out->channels[channelOff + pi];
        ch.target_type = ChannelTargetType::SkeletonJoint;
        ch.path = AnimationPath::Translation;
        ch.skeleton_id = skeleton_id;
        ch.joint_id = resolved_joint_id;
        ch.sampler = int32_t(samplerOff + pi);
        pi++;
      }
      if (nRotTimes) {
        anim_out->samplers[samplerOff + pi].interpolation = AnimationInterpolation::Linear;
        auto &ch = anim_out->channels[channelOff + pi];
        ch.target_type = ChannelTargetType::SkeletonJoint;
        ch.path = AnimationPath::Rotation;
        ch.skeleton_id = skeleton_id;
        ch.joint_id = resolved_joint_id;
        ch.sampler = int32_t(samplerOff + pi);
        pi++;
      }
      if (nScaleTimes) {
        anim_out->samplers[samplerOff + pi].interpolation = AnimationInterpolation::Linear;
        auto &ch = anim_out->channels[channelOff + pi];
        ch.target_type = ChannelTargetType::SkeletonJoint;
        ch.path = AnimationPath::Scale;
        ch.skeleton_id = skeleton_id;
        ch.joint_id = resolved_joint_id;
        ch.sampler = int32_t(samplerOff + pi);
        pi++;
      }
    }

    // Scatter into bulk buffers (joint-major layout: [j0_f0, j0_f1, ..., j1_f0, ...])
    // This avoids per-sampler vector::resize() zero-fill overhead.

    // Scatter translations into bulk buffer
    if (nTransTimes) {
      auto scatterTransFrame = [&](size_t frameIdx, float time, const std::vector<value::float3> &frameData) {
        if (frameData.size() != nJoints) {
          _err = fmt::format("Array length mismatch: translations.size {} != joints.size {} at frame {}",
            frameData.size(), nJoints, frameIdx);
          return false;
        }
        if (time > anim_out->duration) anim_out->duration = time;
        for (size_t j = 0; j < nJoints; j++) {
          transTimesBuf[j * nTransTimes + frameIdx] = time;
          memcpy(&transValsBuf[(j * nTransTimes + frameIdx) * 3], frameData[j].data(), 3 * sizeof(float));
        }
        return true;
      };

      if (translations.has_timesamples()) {
        const auto &ts = translations.get_timesamples();
        size_t frameIdx = 0;
        FOREACH_TIMESAMPLES_BEGIN(ts, sample_t, sample_value, sample_blocked)
          if (sample_blocked) {
            continue;
          }
          if (!scatterTransFrame(frameIdx, float(sample_t), sample_value)) {
            PUSH_ERROR_AND_RETURN(_err);
          }
          frameIdx++;
        FOREACH_TIMESAMPLES_END()
      } else {
        std::vector<value::float3> default_value;
        if (!translations.get_scalar(&default_value)) {
          PUSH_ERROR_AND_RETURN(fmt::format("Failed to get default translations: {}", abs_path));
        }
        if (!scatterTransFrame(0, 0.0f, default_value)) {
          PUSH_ERROR_AND_RETURN(_err);
        }
      }
    }

    // Scatter rotations into bulk buffer
    // quatf layout: { float3 imag; float real; } = 4 contiguous floats
    if (nRotTimes) {
      auto scatterRotFrame = [&](size_t frameIdx, float time, const std::vector<value::quatf> &frameData) {
        if (frameData.size() != nJoints) {
          _err = fmt::format("Array length mismatch: rotations.size {} != joints.size {} at frame {}",
            frameData.size(), nJoints, frameIdx);
          return false;
        }
        if (time > anim_out->duration) anim_out->duration = time;
        for (size_t j = 0; j < nJoints; j++) {
          rotTimesBuf[j * nRotTimes + frameIdx] = time;
          memcpy(&rotValsBuf[(j * nRotTimes + frameIdx) * 4], &frameData[j], 4 * sizeof(float));
        }
        return true;
      };

      if (rotations.has_timesamples()) {
        const auto &ts = rotations.get_timesamples();
        size_t frameIdx = 0;
        FOREACH_TIMESAMPLES_BEGIN(ts, sample_t, sample_value, sample_blocked)
          if (sample_blocked) {
            continue;
          }
          if (!scatterRotFrame(frameIdx, float(sample_t), sample_value)) {
            PUSH_ERROR_AND_RETURN(_err);
          }
          frameIdx++;
        FOREACH_TIMESAMPLES_END()
      } else {
        std::vector<value::quatf> default_value;
        if (!rotations.get_scalar(&default_value)) {
          PUSH_ERROR_AND_RETURN(fmt::format("Failed to get default rotations: {}", abs_path));
        }
        if (!scatterRotFrame(0, 0.0f, default_value)) {
          PUSH_ERROR_AND_RETURN(_err);
        }
      }
    }

    // Scatter scales into bulk buffer (with half->float conversion)
    if (nScaleTimes) {
      auto scatterScaleFrame = [&](size_t frameIdx, float time, const std::vector<value::half3> &frameData) {
        if (frameData.size() != nJoints) {
          _err = fmt::format("Array length mismatch: scales.size {} != joints.size {} at frame {}",
            frameData.size(), nJoints, frameIdx);
          return false;
        }
        if (time > anim_out->duration) anim_out->duration = time;
        for (size_t j = 0; j < nJoints; j++) {
          scaleTimesBuf[j * nScaleTimes + frameIdx] = time;
          float *dst = &scaleValsBuf[(j * nScaleTimes + frameIdx) * 3];
          const auto &v = frameData[j];
          dst[0] = value::half_to_float(v[0]);
          dst[1] = value::half_to_float(v[1]);
          dst[2] = value::half_to_float(v[2]);
        }
        return true;
      };

      if (scales.has_timesamples()) {
        const auto &ts = scales.get_timesamples();
        size_t frameIdx = 0;
        FOREACH_TIMESAMPLES_BEGIN(ts, sample_t, sample_value, sample_blocked)
          if (sample_blocked) {
            continue;
          }
          if (!scatterScaleFrame(frameIdx, float(sample_t), sample_value)) {
            PUSH_ERROR_AND_RETURN(_err);
          }
          frameIdx++;
        FOREACH_TIMESAMPLES_END()
      } else {
        std::vector<value::half3> default_value;
        if (!scales.get_scalar(&default_value)) {
          PUSH_ERROR_AND_RETURN(fmt::format("Failed to get default scales: {}", abs_path));
        }
        if (!scatterScaleFrame(0, 0.0f, default_value)) {
          PUSH_ERROR_AND_RETURN(_err);
        }
      }
    }

    // Copy from bulk buffers into per-sampler vectors (single memcpy per sampler,
    // using assign() which allocates + copies without zero-fill overhead)
    for (size_t j = 0; j < nJoints; j++) {
      size_t samplerOff = baseSamplerIdx + j * nProps;
      size_t pi = 0;
      if (nTransTimes) {
        auto &s = anim_out->samplers[samplerOff + pi];
        const float *tBase = &transTimesBuf[j * nTransTimes];
        const float *vBase = &transValsBuf[j * nTransTimes * 3];
        s.times.assign(tBase, tBase + nTransTimes);
        s.values.assign(vBase, vBase + nTransTimes * 3);
        pi++;
      }
      if (nRotTimes) {
        auto &s = anim_out->samplers[samplerOff + pi];
        const float *tBase = &rotTimesBuf[j * nRotTimes];
        const float *vBase = &rotValsBuf[j * nRotTimes * 4];
        s.times.assign(tBase, tBase + nRotTimes);
        s.values.assign(vBase, vBase + nRotTimes * 4);
        pi++;
      }
      if (nScaleTimes) {
        auto &s = anim_out->samplers[samplerOff + pi];
        const float *tBase = &scaleTimesBuf[j * nScaleTimes];
        const float *vBase = &scaleValsBuf[j * nScaleTimes * 3];
        s.times.assign(tBase, tBase + nScaleTimes);
        s.values.assign(vBase, vBase + nScaleTimes * 3);
        pi++;
      }
    }
  }

  // BlendShape animations currently need mesh-node target resolution.
  // The current conversion stage does not have stable node indices yet, so
  // emitting Weights channels here would produce invalid target_node values.
  if (blendShapes.size()) {
    PUSH_WARN(fmt::format(
        "Skipping blendShapeWeights conversion for SkelAnimation {} "
        "(mesh target resolution not implemented yet)",
        abs_path.full_path_name()));
  }

  return true;
}

// Helper function: Quaternion multiplication using direct member access
// (avoids operator[] pointer arithmetic overhead)
// q1 * q2, Hamilton convention
[[maybe_unused]] static inline value::quatf quat_mul(const value::quatf &q1, const value::quatf &q2) {
  const float x1 = q1.imag[0], y1 = q1.imag[1], z1 = q1.imag[2], w1 = q1.real;
  const float x2 = q2.imag[0], y2 = q2.imag[1], z2 = q2.imag[2], w2 = q2.real;
  value::quatf r;
  r.imag[0] = w1*x2 + x1*w2 + y1*z2 - z1*y2;
  r.imag[1] = w1*y2 - x1*z2 + y1*w2 + z1*x2;
  r.imag[2] = w1*z2 + x1*y2 - y1*x2 + z1*w2;
  r.real    = w1*w2 - x1*x2 - y1*y2 - z1*z2;
  return r;
}

// Specialized single-axis angle-to-quaternion (avoids multiply-by-zero for the
// two unused axis components). Keeps sin_pi/cos_pi for accuracy.
static inline value::quatf to_quaternion_x(float angle) {
  float s = float(math::sin_pi(double(angle) / 360.0));
  float c = float(math::cos_pi(double(angle) / 360.0));
  value::quatf q;
  q.imag[0] = s;  q.imag[1] = 0.0f;  q.imag[2] = 0.0f;  q.real = c;
  return q;
}

static inline value::quatf to_quaternion_y(float angle) {
  float s = float(math::sin_pi(double(angle) / 360.0));
  float c = float(math::cos_pi(double(angle) / 360.0));
  value::quatf q;
  q.imag[0] = 0.0f;  q.imag[1] = s;  q.imag[2] = 0.0f;  q.real = c;
  return q;
}

static inline value::quatf to_quaternion_z(float angle) {
  float s = float(math::sin_pi(double(angle) / 360.0));
  float c = float(math::cos_pi(double(angle) / 360.0));
  value::quatf q;
  q.imag[0] = 0.0f;  q.imag[1] = 0.0f;  q.imag[2] = s;  q.real = c;
  return q;
}

// Direct Euler-to-quaternion conversion using closed-form formulas.
// Computes the combined quaternion from 3 axis-aligned rotations in one step,
// avoiding intermediate quaternion objects and 2 quaternion multiplications.
// All 6 rotation orders are supported.
// angles[0] = X angle, angles[1] = Y angle, angles[2] = Z angle (degrees)
static inline value::quatf euler_to_quatf(
    const value::double3 &angles, XformOp::OpType rot_order) {
  // Half-angle trig values (using sin_pi/cos_pi for accuracy)
  const float sx = float(math::sin_pi(angles[0] / 360.0));
  const float cx = float(math::cos_pi(angles[0] / 360.0));
  const float sy = float(math::sin_pi(angles[1] / 360.0));
  const float cy = float(math::cos_pi(angles[1] / 360.0));
  const float sz = float(math::sin_pi(angles[2] / 360.0));
  const float cz = float(math::cos_pi(angles[2] / 360.0));

  value::quatf q;

  switch (rot_order) {
    case XformOp::OpType::RotateXYZ:
      // Q = Qz * Qy * Qx
      q.imag[0] = cz*cy*sx - sz*sy*cx;
      q.imag[1] = cz*sy*cx + sz*cy*sx;
      q.imag[2] = sz*cy*cx - cz*sy*sx;
      q.real    = cz*cy*cx + sz*sy*sx;
      break;
    case XformOp::OpType::RotateXZY:
      // Q = Qy * Qz * Qx
      q.imag[0] = cy*cz*sx + sy*sz*cx;
      q.imag[1] = cy*sz*sx + sy*cz*cx;
      q.imag[2] = cy*sz*cx - sy*cz*sx;
      q.real    = cy*cz*cx - sy*sz*sx;
      break;
    case XformOp::OpType::RotateYXZ:
      // Q = Qz * Qx * Qy
      q.imag[0] = cz*sx*cy - sz*cx*sy;
      q.imag[1] = cz*cx*sy + sz*sx*cy;
      q.imag[2] = cz*sx*sy + sz*cx*cy;
      q.real    = cz*cx*cy - sz*sx*sy;
      break;
    case XformOp::OpType::RotateYZX:
      // Q = Qx * Qz * Qy
      q.imag[0] = sx*cz*cy - cx*sz*sy;
      q.imag[1] = cx*cz*sy - sx*sz*cy;
      q.imag[2] = cx*sz*cy + sx*cz*sy;
      q.real    = cx*cz*cy + sx*sz*sy;
      break;
    case XformOp::OpType::RotateZXY:
      // Q = Qy * Qx * Qz
      q.imag[0] = cy*sx*cz + sy*cx*sz;
      q.imag[1] = sy*cx*cz - cy*sx*sz;
      q.imag[2] = cy*cx*sz - sy*sx*cz;
      q.real    = cy*cx*cz + sy*sx*sz;
      break;
    case XformOp::OpType::RotateZYX:
      // Q = Qx * Qy * Qz
      q.imag[0] = cx*sy*sz + sx*cy*cz;
      q.imag[1] = cx*sy*cz - sx*cy*sz;
      q.imag[2] = cx*cy*sz + sx*sy*cz;
      q.real    = cx*cy*cz - sx*sy*sz;
      break;
    default:
      // Fallback: treat as XYZ
      q.imag[0] = cz*cy*sx - sz*sy*cx;
      q.imag[1] = cz*sy*cx + sz*cy*sx;
      q.imag[2] = sz*cy*cx - cz*sy*sx;
      q.real    = cz*cy*cx + sz*sy*sx;
      break;
  }

  return q;
}

bool RenderSceneConverter::ExtractXformOpAnimation(
    const RenderSceneConverterEnv &env,
    const Path &abs_path,
    const std::string &prim_name,
    const Xformable &xformable,
    int32_t target_node_index,
    AnimationClip *anim_out) {

  (void)env;  // Unused parameter

  if (!anim_out) {
    PUSH_ERROR_AND_RETURN("anim_out is nullptr");
  }

  // Check if xformable has any animated xformOps
  if (!xformable.has_timesamples()) {
    return false;  // No animation data
  }

  // Setup basic metadata
  anim_out->abs_path = abs_path.full_path_name();
  anim_out->prim_name = prim_name;
  anim_out->name = prim_name + "_xform";
  anim_out->duration = 0.0f;  // Will be computed below
  anim_out->source_type = AnimationSourceType::XformOp;
  anim_out->num_animated_nodes = 1;

  // Process each xformOp that has time samples
  for (size_t xform_idx = 0; xform_idx < xformable.xformOps.size(); xform_idx++) {
    const XformOp &xformOp = xformable.xformOps[xform_idx];

    if (xformOp.op_type == XformOp::OpType::ResetXformStack) {
      continue;  // Skip reset operations
    }

    if (!xformOp.has_timesamples()) {
      continue;  // Skip non-animated ops
    }

    // Get the time samples
    auto ts_opt = xformOp.get_timesamples();
    if (!ts_opt) {
      continue;
    }

    const value::TimeSamples &ts = ts_opt.value();
    if (ts.size() == 0) {
      continue;
    }

    // Determine the animation path based on xformOp type
    AnimationPath anim_path = AnimationPath::Translation;  // Default initialization
    bool is_supported = false;

    switch (xformOp.op_type) {
      case XformOp::OpType::Translate:
        anim_path = AnimationPath::Translation;
        is_supported = true;
        break;

      case XformOp::OpType::Scale:
        anim_path = AnimationPath::Scale;
        is_supported = true;
        break;

      case XformOp::OpType::Orient:
        anim_path = AnimationPath::Rotation;
        is_supported = true;
        break;

      case XformOp::OpType::RotateX:
      case XformOp::OpType::RotateY:
      case XformOp::OpType::RotateZ:
      case XformOp::OpType::RotateXYZ:
      case XformOp::OpType::RotateXZY:
      case XformOp::OpType::RotateYXZ:
      case XformOp::OpType::RotateYZX:
      case XformOp::OpType::RotateZXY:
      case XformOp::OpType::RotateZYX:
        anim_path = AnimationPath::Rotation;
        is_supported = true;
        break;

      case XformOp::OpType::Transform:
        // Full matrix transform - decompose into TRS
        // We'll handle this specially below since it produces multiple animation channels
        is_supported = true;
        break;

      case XformOp::OpType::ResetXformStack:
        // Not animatable - skip
        is_supported = false;
        break;
    }

    if (!is_supported) {
      continue;
    }

    // Special handling for Transform (matrix) - decompose into TRS
    if (xformOp.op_type == XformOp::OpType::Transform) {
      std::vector<double> times;
      std::vector<value::double3> translations;
      std::vector<value::quatd> rotations;
      std::vector<value::double3> scales;

      // Extract and decompose matrix time samples
      FOREACH_TIMESAMPLES_BEGIN(ts, sample_t, sample_value, sample_blocked)
        if (sample_blocked) {
          continue;
        }

        value::matrix4d mat;
        bool got_value = false;

        if (auto v = sample_value.as<value::matrix4d>()) {
          mat = *v;
          got_value = true;
        } else if (auto vf = sample_value.as<value::matrix4f>()) {
          // Convert float matrix to double
          const auto &m = *vf;
          for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
              mat.m[i][j] = double(m.m[i][j]);
            }
          }
          got_value = true;
        }

        if (got_value) {
          value::double3 translation, scale;
          value::quatd rotation;

          // Decompose the matrix
          if (decompose(mat, &translation, &rotation, &scale)) {
            times.push_back(sample_t);
            translations.push_back(translation);
            rotations.push_back(rotation);
            scales.push_back(scale);

            if (float(sample_t) > anim_out->duration) {
              anim_out->duration = float(sample_t);
            }
          } else {
            PUSH_WARN(fmt::format("Failed to decompose matrix at time {} for xformOp:transform at {}",
                                 sample_t, abs_path.full_path_name()));
          }
        }
      FOREACH_TIMESAMPLES_END()

      // Create three separate animation channels for T, R, S
      if (!times.empty()) {
        // Translation channel
        {
          KeyframeSampler sampler;
          sampler.interpolation = AnimationInterpolation::Linear;
          sampler.times.reserve(times.size());
          sampler.values.reserve(times.size() * 3);

          for (size_t i = 0; i < times.size(); i++) {
            sampler.times.push_back(float(times[i]));
            sampler.values.push_back(float(translations[i][0]));
            sampler.values.push_back(float(translations[i][1]));
            sampler.values.push_back(float(translations[i][2]));
          }

          int32_t sampler_idx = int32_t(anim_out->samplers.size());
          anim_out->samplers.push_back(sampler);

          AnimationChannel channel;
          channel.target_type = ChannelTargetType::SceneNode;
          channel.path = AnimationPath::Translation;
          channel.target_node = target_node_index;
          channel.sampler = sampler_idx;
          anim_out->channels.push_back(channel);
        }

        // Rotation channel
        {
          KeyframeSampler sampler;
          sampler.interpolation = AnimationInterpolation::Linear;
          sampler.times.reserve(times.size());
          sampler.values.reserve(times.size() * 4);

          for (size_t i = 0; i < times.size(); i++) {
            sampler.times.push_back(float(times[i]));
            sampler.values.push_back(float(rotations[i].imag[0]));
            sampler.values.push_back(float(rotations[i].imag[1]));
            sampler.values.push_back(float(rotations[i].imag[2]));
            sampler.values.push_back(float(rotations[i].real));
          }

          int32_t sampler_idx = int32_t(anim_out->samplers.size());
          anim_out->samplers.push_back(sampler);

          AnimationChannel channel;
          channel.target_type = ChannelTargetType::SceneNode;
          channel.path = AnimationPath::Rotation;
          channel.target_node = target_node_index;
          channel.sampler = sampler_idx;
          anim_out->channels.push_back(channel);
        }

        // Scale channel
        {
          KeyframeSampler sampler;
          sampler.interpolation = AnimationInterpolation::Linear;
          sampler.times.reserve(times.size());
          sampler.values.reserve(times.size() * 3);

          for (size_t i = 0; i < times.size(); i++) {
            sampler.times.push_back(float(times[i]));
            sampler.values.push_back(float(scales[i][0]));
            sampler.values.push_back(float(scales[i][1]));
            sampler.values.push_back(float(scales[i][2]));
          }

          int32_t sampler_idx = int32_t(anim_out->samplers.size());
          anim_out->samplers.push_back(sampler);

          AnimationChannel channel;
          channel.target_type = ChannelTargetType::SceneNode;
          channel.path = AnimationPath::Scale;
          channel.target_node = target_node_index;
          channel.sampler = sampler_idx;
          anim_out->channels.push_back(channel);
        }
      }

      // Skip the regular processing below
      continue;
    }

    // Create a keyframe sampler
    KeyframeSampler sampler;
    sampler.interpolation = AnimationInterpolation::Linear;

    // Extract time samples based on the operation type
    if (anim_path == AnimationPath::Translation || anim_path == AnimationPath::Scale) {
      // Handle vec3 types (translation, scale)
      std::vector<double> times;
      std::vector<value::float3> values;

      FOREACH_TIMESAMPLES_BEGIN(ts, sample_t, sample_value, sample_blocked)
        if (sample_blocked) {
          continue;
        }

        // Try to get value as various vec3 types
        value::float3 vec;
        bool got_value = false;

        if (auto v = sample_value.as<value::float3>()) {
          vec = *v;
          got_value = true;
        } else if (auto vd = sample_value.as<value::double3>()) {
          vec[0] = float((*vd)[0]);
          vec[1] = float((*vd)[1]);
          vec[2] = float((*vd)[2]);
          got_value = true;
        } else if (auto vh = sample_value.as<value::half3>()) {
          vec[0] = value::half_to_float((*vh)[0]);
          vec[1] = value::half_to_float((*vh)[1]);
          vec[2] = value::half_to_float((*vh)[2]);
          got_value = true;
        }

        if (got_value) {
          times.push_back(sample_t);
          values.push_back(vec);
          if (float(sample_t) > anim_out->duration) {
            anim_out->duration = float(sample_t);
          }
        }
      FOREACH_TIMESAMPLES_END()

      // Build sampler data
      if (!times.empty()) {
        sampler.times.reserve(times.size());
        sampler.values.reserve(times.size() * 3);

        for (size_t i = 0; i < times.size(); i++) {
          sampler.times.push_back(float(times[i]));
          sampler.values.push_back(values[i][0]);
          sampler.values.push_back(values[i][1]);
          sampler.values.push_back(values[i][2]);
        }
      }

    } else if (anim_path == AnimationPath::Rotation) {
      // Handle rotation types
      std::vector<double> times;
      std::vector<value::quatf> values;

      // For Orient operations, we have quaternions
      if (xformOp.op_type == XformOp::OpType::Orient) {
        FOREACH_TIMESAMPLES_BEGIN(ts, sample_t, sample_value, sample_blocked)
          if (sample_blocked) {
            continue;
          }

          value::quatf quat;
          bool got_value = false;

          if (auto v = sample_value.as<value::quatf>()) {
            quat = *v;
            got_value = true;
          } else if (auto vd = sample_value.as<value::quatd>()) {
            quat.imag[0] = float(vd->imag[0]);
            quat.imag[1] = float(vd->imag[1]);
            quat.imag[2] = float(vd->imag[2]);
            quat.real     = float(vd->real);
            got_value = true;
          } else if (auto vh = sample_value.as<value::quath>()) {
            quat.imag[0] = value::half_to_float(vh->imag[0]);
            quat.imag[1] = value::half_to_float(vh->imag[1]);
            quat.imag[2] = value::half_to_float(vh->imag[2]);
            quat.real     = value::half_to_float(vh->real);
            got_value = true;
          }

          if (got_value) {
            times.push_back(sample_t);
            values.push_back(quat);
            if (float(sample_t) > anim_out->duration) {
              anim_out->duration = float(sample_t);
            }
          }
        FOREACH_TIMESAMPLES_END()

      } else {
        // For Rotate operations, we have angles that need to be converted to quaternions
        // We'll extract the angle values and convert them to quaternions
        std::vector<double> angle_times;
        std::vector<double> angle_values;

        if (xformOp.op_type == XformOp::OpType::RotateX ||
            xformOp.op_type == XformOp::OpType::RotateY ||
            xformOp.op_type == XformOp::OpType::RotateZ) {
          // Single-axis rotation (scalar angle)
          FOREACH_TIMESAMPLES_BEGIN(ts, sample_t, sample_value, sample_blocked)
            if (sample_blocked) {
              continue;
            }

            double angle = 0.0;
            bool got_value = false;

            if (auto v = sample_value.as<double>()) {
              angle = *v;
              got_value = true;
            } else if (auto vf = sample_value.as<float>()) {
              angle = double(*vf);
              got_value = true;
            }

            if (got_value) {
              angle_times.push_back(sample_t);
              angle_values.push_back(angle);
              if (float(sample_t) > anim_out->duration) {
                anim_out->duration = float(sample_t);
              }
            }
          FOREACH_TIMESAMPLES_END()

          // Convert angles to quaternions using specialized single-axis functions
          for (size_t i = 0; i < angle_times.size(); i++) {
            times.push_back(angle_times[i]);
            if (xformOp.op_type == XformOp::OpType::RotateX) {
              values.push_back(to_quaternion_x(float(angle_values[i])));
            } else if (xformOp.op_type == XformOp::OpType::RotateY) {
              values.push_back(to_quaternion_y(float(angle_values[i])));
            } else {  // RotateZ
              values.push_back(to_quaternion_z(float(angle_values[i])));
            }
          }

        } else {
          // Multi-axis rotation (vec3 of angles)
          // For RotateXYZ and similar, we need to compute the combined quaternion
          std::vector<value::double3> euler_angles;

          FOREACH_TIMESAMPLES_BEGIN(ts, sample_t, sample_value, sample_blocked)
            if (sample_blocked) {
              continue;
            }

            value::double3 angles;
            bool got_value = false;

            if (auto v = sample_value.as<value::float3>()) {
              angles[0] = double((*v)[0]);
              angles[1] = double((*v)[1]);
              angles[2] = double((*v)[2]);
              got_value = true;
            } else if (auto vd = sample_value.as<value::double3>()) {
              angles = *vd;
              got_value = true;
            } else if (auto vh = sample_value.as<value::half3>()) {
              angles[0] = double(value::half_to_float((*vh)[0]));
              angles[1] = double(value::half_to_float((*vh)[1]));
              angles[2] = double(value::half_to_float((*vh)[2]));
              got_value = true;
            }

            if (got_value) {
              angle_times.push_back(sample_t);
              euler_angles.push_back(angles);
              if (float(sample_t) > anim_out->duration) {
                anim_out->duration = float(sample_t);
              }
            }
          FOREACH_TIMESAMPLES_END()

          // Convert Euler angles to quaternions using direct closed-form formula
          // (handles all rotation orders correctly)
          for (size_t i = 0; i < angle_times.size(); i++) {
            times.push_back(angle_times[i]);
            values.push_back(euler_to_quatf(euler_angles[i], xformOp.op_type));
          }
        }
      }

      // Build sampler data for rotations (quaternions)
      if (!times.empty()) {
        sampler.times.reserve(times.size());
        sampler.values.reserve(times.size() * 4);

        for (size_t i = 0; i < times.size(); i++) {
          sampler.times.push_back(float(times[i]));
          sampler.values.push_back(values[i].imag[0]);
          sampler.values.push_back(values[i].imag[1]);
          sampler.values.push_back(values[i].imag[2]);
          sampler.values.push_back(values[i].real);
        }
      }
    }

    // Only add if we have valid sampler data
    if (!sampler.times.empty()) {
      int32_t sampler_idx = int32_t(anim_out->samplers.size());
      anim_out->samplers.push_back(sampler);

      AnimationChannel channel;
      channel.target_type = ChannelTargetType::SceneNode;
      channel.path = anim_path;
      channel.target_node = target_node_index;
      channel.sampler = sampler_idx;
      anim_out->channels.push_back(channel);
    }
  }

  // Return true if we extracted any animation data
  return !anim_out->channels.empty();
}

// Helper to get NodeCategory from NodeType
static NodeCategory GetNodeCategoryFromType(NodeType nodeType) {
  switch (nodeType) {
    case NodeType::Xform:
      return NodeCategory::Group;
    case NodeType::Mesh:
      return NodeCategory::Geom;
    case NodeType::Camera:
      return NodeCategory::Camera;
    case NodeType::SkelRoot:
    case NodeType::Skeleton:
      return NodeCategory::Skeleton;
    case NodeType::PointLight:
    case NodeType::DirectionalLight:
    case NodeType::EnvmapLight:
    case NodeType::RectLight:
    case NodeType::DiskLight:
    case NodeType::CylinderLight:
    case NodeType::GeometryLight:
      return NodeCategory::Light;
  }
  return NodeCategory::Group;  // Default
}

bool RenderSceneConverter::BuildNodeHierarchyImpl(
    const RenderSceneConverterEnv &env, const std::string &parentPrimPath,
    const XformNode &node, Node &out_rnode) {
  Node rnode;

  std::string primPath;
  if (parentPrimPath.empty()) {
    primPath = "/" + node.element_name;
  } else {
    primPath = parentPrimPath + "/" + node.element_name;
  }

  const tinyusdz::Prim *prim = node.prim;
  if (prim) {
    rnode.prim_name = prim->element_name();
    rnode.abs_path = primPath;
    rnode.display_name = prim->metas().has_displayName() ? prim->metas().get_displayName() : "";

    DCOUT("rnode.prim_name " << rnode.prim_name);
    DCOUT("node.local_mat " << node.get_local_matrix());
    DCOUT("node.has_resetXform " << node.has_resetXformStack());
    DCOUT("prim.type_name " << prim->type_name());
    DCOUT("prim.type_id " << prim->type_id());
    DCOUT("xform " << value::TYPE_ID_GEOM_XFORM);

    if (prim->type_id() == value::TYPE_ID_GEOM_MESH) {
      // GeomMesh(GPrim) also has xform.
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.nodeType = NodeType::Mesh;
      rnode.has_resetXform = node.has_resetXformStack();

      if (meshMap.count(primPath)) {
        rnode.id = int32_t(meshMap.at(primPath));
      } else {
        rnode.id = -1;
      }

      // Note: MeshLightAPI is now handled in ConvertMesh, which sets
      // mesh.is_area_light = true and stores light properties directly in RenderMesh
    } else if (prim->type_id() == value::TYPE_ID_GEOM_CAMERA) {
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.nodeType = NodeType::Mesh;
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Camera;
      rnode.id = -1;  // TODO: Assign index to cameras
    } else if (prim->type_id() == value::TYPE_ID_GEOM_XFORM) {
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      DCOUT("rnode.local_matrix " << rnode.local_matrix);
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Xform;
    } else if (prim->type_id() == value::TYPE_ID_SCOPE) {
      // NOTE: get_local_matrix() should return identity matrix.
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Xform;
    } else if (prim->type_id() == value::TYPE_ID_MODEL) {
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Xform;
    } else if (prim->type_id() == value::TYPE_ID_GEOM_CUBE || prim->type_id() == value::TYPE_ID_GEOM_SPHERE) {
      // GeomCube and GeomSphere are converted to meshes
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.nodeType = NodeType::Mesh;
      rnode.has_resetXform = node.has_resetXformStack();

      if (meshMap.count(primPath)) {
        rnode.id = int32_t(meshMap.at(primPath));
      } else {
        rnode.id = -1;
      }
    } else if ((prim->type_id() > value::TYPE_ID_MODEL_BEGIN) && (prim->type_id() < value::TYPE_ID_GEOM_END)) {
      // Other Geom prims (e.g. GeomCone, GeomCylinder) - not yet converted to meshes
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Xform;
    } else if (IsLightPrim(*prim)) {
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();

      // Convert USD light to RenderLight and add to scene
      RenderLight rlight;
      bool light_converted = false;
      std::string light_abs_path = primPath;
      Path lightPath(light_abs_path, /* prop_part */ "");

      if (prim->type_id() == value::TYPE_ID_LUX_SPHERE) {
        const SphereLight *sphereLight = prim->as<SphereLight>();
        if (sphereLight && ConvertSphereLight(env, lightPath, *sphereLight, &rlight)) {
          rnode.nodeType = NodeType::PointLight;
          light_converted = true;
        }
      } else if (prim->type_id() == value::TYPE_ID_LUX_DISTANT) {
        const DistantLight *distantLight = prim->as<DistantLight>();
        if (distantLight && ConvertDistantLight(env, lightPath, *distantLight, &rlight)) {
          rnode.nodeType = NodeType::DirectionalLight;
          light_converted = true;
        }
      } else if (prim->type_id() == value::TYPE_ID_LUX_DOME) {
        const DomeLight *domeLight = prim->as<DomeLight>();
        if (domeLight && ConvertDomeLight(env, lightPath, *domeLight, &rlight)) {
          rnode.nodeType = NodeType::EnvmapLight;
          light_converted = true;
        }
      } else if (prim->type_id() == value::TYPE_ID_LUX_RECT) {
        const RectLight *rectLight = prim->as<RectLight>();
        if (rectLight && ConvertRectLight(env, lightPath, *rectLight, &rlight)) {
          rnode.nodeType = NodeType::RectLight;
          light_converted = true;
        }
      } else if (prim->type_id() == value::TYPE_ID_LUX_DISK) {
        const DiskLight *diskLight = prim->as<DiskLight>();
        if (diskLight && ConvertDiskLight(env, lightPath, *diskLight, &rlight)) {
          rnode.nodeType = NodeType::DiskLight;
          light_converted = true;
        }
      } else if (prim->type_id() == value::TYPE_ID_LUX_CYLINDER) {
        const CylinderLight *cylinderLight = prim->as<CylinderLight>();
        if (cylinderLight && ConvertCylinderLight(env, lightPath, *cylinderLight, &rlight)) {
          rnode.nodeType = NodeType::CylinderLight;
          light_converted = true;
        }
      } else if (prim->type_id() == value::TYPE_ID_LUX_GEOMETRY) {
        const GeometryLight *geometryLight = prim->as<GeometryLight>();
        if (geometryLight && ConvertGeometryLight(env, lightPath, *geometryLight, &rlight)) {
          rnode.nodeType = NodeType::GeometryLight;
          light_converted = true;
        }
      } else {
        // Unsupported light type
        DCOUT("Unsupported light type: " << prim->type_name());
        rnode.nodeType = NodeType::Xform;
      }

      if (light_converted) {
        // Copy world transform to the light
        // rnode.global_matrix is a matrix4d, rlight.transform is mat4 (float)
        const auto &m = rnode.global_matrix;
        rlight.transform.m[0][0] = float(m.m[0][0]);
        rlight.transform.m[0][1] = float(m.m[0][1]);
        rlight.transform.m[0][2] = float(m.m[0][2]);
        rlight.transform.m[0][3] = float(m.m[0][3]);
        rlight.transform.m[1][0] = float(m.m[1][0]);
        rlight.transform.m[1][1] = float(m.m[1][1]);
        rlight.transform.m[1][2] = float(m.m[1][2]);
        rlight.transform.m[1][3] = float(m.m[1][3]);
        rlight.transform.m[2][0] = float(m.m[2][0]);
        rlight.transform.m[2][1] = float(m.m[2][1]);
        rlight.transform.m[2][2] = float(m.m[2][2]);
        rlight.transform.m[2][3] = float(m.m[2][3]);
        rlight.transform.m[3][0] = float(m.m[3][0]);
        rlight.transform.m[3][1] = float(m.m[3][1]);
        rlight.transform.m[3][2] = float(m.m[3][2]);
        rlight.transform.m[3][3] = float(m.m[3][3]);

        // Extract position from transform (translation column)
        rlight.position[0] = float(m.m[3][0]);
        rlight.position[1] = float(m.m[3][1]);
        rlight.position[2] = float(m.m[3][2]);

        // Extract direction from transform (light faces -Z in local space)
        // Direction is the negative of the Z column (third column) of the rotation part
        rlight.direction[0] = -float(m.m[2][0]);
        rlight.direction[1] = -float(m.m[2][1]);
        rlight.direction[2] = -float(m.m[2][2]);

        // Add light to the lights array
        size_t light_id = lights.size();
        lightMap.add(light_abs_path, light_id);
        lights.push_back(std::move(rlight));
        rnode.id = int32_t(light_id);
      } else {
        rnode.id = -1;
      }
    } else if (prim->type_id() == value::TYPE_ID_SKEL_ROOT) {
      // UsdSkelRoot: encapsulation prim for skinned subtree.
      // SkelRoot is Xformable and its world transform (skelLocalToWorld)
      // positions the skinned result in world space.
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::SkelRoot;
    } else if (prim->type_id() == value::TYPE_ID_SKELETON) {
      // UsdSkeleton: joint hierarchy with bindTransforms and restTransforms.
      // Skeleton is Xformable; its world transform contributes to
      // skelLocalToWorld for positioning skinned results.
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Skeleton;
    } else {
      // ignore other node types.
      DCOUT("Unknown/Unsupported prim. " << prim->type_name());

      // Setup as xform for now.
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Xform;
    }

    // Set category based on nodeType
    rnode.category = GetNodeCategoryFromType(rnode.nodeType);
  }

  for (const auto &child : node.children) {
    Node child_rnode;
    if (!BuildNodeHierarchyImpl(env, primPath, child, child_rnode)) {
      return false;
    }

    rnode.children.emplace_back(std::move(child_rnode));
  }

  out_rnode = std::move(rnode);

  return true;
}

//
// Light conversion implementations
//

// Helper to extract common light properties
template<typename LightType>
static bool ExtractCommonLightProperties(
    const RenderSceneConverterEnv &env,
    const LightType &light,  // BoundableLight or NonboundableLight
    RenderLight *rlight) {

  // Extract color
  if (light.color.authored() && !light.color.is_blocked()) {
    value::color3f col;
    if (light.color.get_value().get(env.timecode, &col)) {
      rlight->color[0] = col[0];
      rlight->color[1] = col[1];
      rlight->color[2] = col[2];
    }
  }

  // Extract intensity
  if (light.intensity.authored() && !light.intensity.is_blocked()) {
    float val;
    if (light.intensity.get_value().get(env.timecode, &val)) {
      rlight->intensity = val;
    }
  }

  // Extract exposure
  if (light.exposure.authored() && !light.exposure.is_blocked()) {
    float val;
    if (light.exposure.get_value().get(env.timecode, &val)) {
      rlight->exposure = val;
    }
  }

  // Extract diffuse multiplier
  if (light.diffuse.authored() && !light.diffuse.is_blocked()) {
    float val;
    if (light.diffuse.get_value().get(env.timecode, &val)) {
      rlight->diffuse = val;
    }
  }

  // Extract specular multiplier
  if (light.specular.authored() && !light.specular.is_blocked()) {
    float val;
    if (light.specular.get_value().get(env.timecode, &val)) {
      rlight->specular = val;
    }
  }

  // Extract normalize flag
  if (light.normalize.authored() && !light.normalize.is_blocked()) {
    bool val;
    if (light.normalize.get_value().get(env.timecode, &val)) {
      rlight->normalize = val;
    }
  }

  // Extract color temperature
  if (light.enableColorTemperature.authored() && !light.enableColorTemperature.is_blocked()) {
    bool val;
    if (light.enableColorTemperature.get_value().get(env.timecode, &val)) {
      rlight->enableColorTemperature = val;
    }
  }

  if (light.colorTemperature.authored() && !light.colorTemperature.is_blocked()) {
    float val;
    if (light.colorTemperature.get_value().get(env.timecode, &val)) {
      rlight->colorTemperature = val;
    }
  }

  // Extract shadow properties
  if (light.shadowEnable.authored() && !light.shadowEnable.is_blocked()) {
    bool val;
    if (light.shadowEnable.get_value().get(env.timecode, &val)) {
      rlight->shadowEnable = val;
    }
  }

  if (light.shadowColor.authored() && !light.shadowColor.is_blocked()) {
    value::color3f col;
    if (light.shadowColor.get_value().get(env.timecode, &col)) {
      rlight->shadowColor[0] = col[0];
      rlight->shadowColor[1] = col[1];
      rlight->shadowColor[2] = col[2];
    }
  }

  if (light.shadowDistance.authored() && !light.shadowDistance.is_blocked()) {
    float val;
    if (light.shadowDistance.get_value().get(env.timecode, &val)) {
      rlight->shadowDistance = val;
    }
  }

  if (light.shadowFalloff.authored() && !light.shadowFalloff.is_blocked()) {
    float val;
    if (light.shadowFalloff.get_value().get(env.timecode, &val)) {
      rlight->shadowFalloff = val;
    }
  }

  if (light.shadowFalloffGamma.authored() && !light.shadowFalloffGamma.is_blocked()) {
    float val;
    if (light.shadowFalloffGamma.get_value().get(env.timecode, &val)) {
      rlight->shadowFalloffGamma = val;
    }
  }

  return true;
}

// Helper to extract shaping properties (for SphereLight and RectLight)
template<typename LightType>
static bool ExtractShapingProperties(
    const RenderSceneConverterEnv &env,
    const LightType &light,  // BoundableLight with shapingFocus, etc.
    RenderLight *rlight) {

  if (light.shapingFocus.authored() && !light.shapingFocus.is_blocked()) {
    float val;
    if (light.shapingFocus.get_value().get(env.timecode, &val)) {
      rlight->shapingFocus = val;
    }
  }

  if (light.shapingFocusTint.authored() && !light.shapingFocusTint.is_blocked()) {
    value::color3f col;
    if (light.shapingFocusTint.get_value().get(env.timecode, &col)) {
      rlight->shapingFocusTint[0] = col[0];
      rlight->shapingFocusTint[1] = col[1];
      rlight->shapingFocusTint[2] = col[2];
    }
  }

  if (light.shapingConeAngle.authored() && !light.shapingConeAngle.is_blocked()) {
    float val;
    if (light.shapingConeAngle.get_value().get(env.timecode, &val)) {
      rlight->shapingConeAngle = val;
    }
  }

  if (light.shapingConeSoftness.authored() && !light.shapingConeSoftness.is_blocked()) {
    float val;
    if (light.shapingConeSoftness.get_value().get(env.timecode, &val)) {
      rlight->shapingConeSoftness = val;
    }
  }

  return true;
}

bool RenderSceneConverter::ConvertSphereLight(
    const RenderSceneConverterEnv &env,
    const Path &light_abs_path,
    const SphereLight &light,
    RenderLight *rlight_out) {

  if (!rlight_out) {
    PUSH_ERROR_AND_RETURN("rlight_out arg is nullptr.");
  }

  RenderLight rlight;
  rlight.name = light.name;
  rlight.abs_path = light_abs_path.full_path_name();
  rlight.type = RenderLight::Type::Sphere;

  // Extract common properties
  if (!ExtractCommonLightProperties(env, light, &rlight)) {
    return false;
  }

  // Extract shaping properties
  if (!ExtractShapingProperties(env, light, &rlight)) {
    return false;
  }

  // Extract radius
  if (light.radius.authored() && !light.radius.is_blocked()) {
    float val;
    if (light.radius.get_value().get(env.timecode, &val)) {
      rlight.radius = val;
    }
  }

  (*rlight_out) = std::move(rlight);
  return true;
}

bool RenderSceneConverter::ConvertDistantLight(
    const RenderSceneConverterEnv &env,
    const Path &light_abs_path,
    const DistantLight &light,
    RenderLight *rlight_out) {

  if (!rlight_out) {
    PUSH_ERROR_AND_RETURN("rlight_out arg is nullptr.");
  }

  RenderLight rlight;
  rlight.name = light.name;
  rlight.abs_path = light_abs_path.full_path_name();
  rlight.type = RenderLight::Type::Distant;

  // Extract common properties
  if (!ExtractCommonLightProperties(env, light, &rlight)) {
    return false;
  }

  // Extract angle (angular diameter in degrees)
  if (light.angle.authored() && !light.angle.is_blocked()) {
    float val;
    if (light.angle.get_value().get(env.timecode, &val)) {
      rlight.angle = val;
    }
  }

  (*rlight_out) = std::move(rlight);
  return true;
}

bool RenderSceneConverter::ConvertDomeLight(
    const RenderSceneConverterEnv &env,
    const Path &light_abs_path,
    const DomeLight &light,
    RenderLight *rlight_out) {

  if (!rlight_out) {
    PUSH_ERROR_AND_RETURN("rlight_out arg is nullptr.");
  }

  RenderLight rlight;
  rlight.name = light.name;
  rlight.abs_path = light_abs_path.full_path_name();
  rlight.type = RenderLight::Type::Dome;

  // Extract common properties
  if (!ExtractCommonLightProperties(env, light, &rlight)) {
    return false;
  }

  // Extract texture file and load envmap image
  if (light.file.authored() && !light.file.is_blocked()) {
    value::AssetPath assetPath;
    if (light.file.get_value()->get(env.timecode, &assetPath)) {
      rlight.textureFile = assetPath.GetAssetPath();

      // Load the envmap texture if scene config allows
      if (env.scene_config.load_texture_assets && !assetPath.GetAssetPath().empty()) {
        TextureImage texImage;
        BufferData imageBuffer;
        imageBuffer.componentType = ComponentType::UInt8;

        std::string warn, err;

        TextureImageLoaderFunction tex_loader_fun =
            env.material_config.texture_image_loader_function;
        if (!tex_loader_fun) {
          tex_loader_fun = DefaultTextureImageLoaderFunction;
        }

        AssetInfo assetInfo;  // Empty asset info for now
        bool tex_loaded = tex_loader_fun(
            assetPath, assetInfo, env.asset_resolver, &texImage,
            &imageBuffer.data,
            env.material_config.texture_image_loader_function_userdata,
            &warn, &err);

        if (warn.size()) {
          PushWarn(warn);
        }

        if (tex_loaded) {
          texImage.asset_identifier = assetPath.GetAssetPath();
          texImage.decoded = true;

          // HDR images (like EXR) should be treated as linear/Raw colorspace
          // Most envmaps are HDR and should not have sRGB gamma
          texImage.usdColorSpace = ColorSpace::Raw;
          texImage.colorSpace = ColorSpace::Lin_sRGB;

          // Add buffer
          texImage.buffer_id = int64_t(buffers.size());
          buffers.emplace_back(imageBuffer);

          // Add image and set envmap_texture_id
          rlight.envmap_texture_id = int32_t(images.size());
          images.emplace_back(texImage);

          DCOUT("Loaded envmap texture: " << assetPath.GetAssetPath()
                << " width=" << texImage.width
                << " height=" << texImage.height
                << " channels=" << texImage.channels);
        } else {
          // Fallback: store raw asset when decoding fails (e.g., EXR/HDR not supported)
          if (err.size()) {
            PushWarn(fmt::format("Failed to decode envmap texture: `{}`. reason = {}. Falling back to raw asset storage.",
                                 assetPath.GetAssetPath(), err));
          }

          // Try to store the raw asset for later decoding (e.g., in JS layer)
          Asset asset;
          std::string resolvedPath;
          std::string readErr;
          AssetInfo fallbackAssetInfo;

          if (RawAssetRead(assetPath, fallbackAssetInfo, env.asset_resolver, &asset,
                           resolvedPath, nullptr, nullptr, &readErr)) {
            TextureImage fallbackTexImage;
            BufferData fallbackImageBuffer;
            fallbackImageBuffer.componentType = ComponentType::UInt8;

            fallbackTexImage.asset_identifier = resolvedPath;

            fallbackImageBuffer.data.resize(asset.size());
            memcpy(fallbackImageBuffer.data.data(), asset.data(), asset.size());

            fallbackTexImage.buffer_id = int64_t(buffers.size());
            buffers.emplace_back(fallbackImageBuffer);

            fallbackTexImage.decoded = false;
            fallbackTexImage.usdColorSpace = ColorSpace::Raw;

            rlight.envmap_texture_id = int32_t(images.size());
            images.emplace_back(fallbackTexImage);

            DCOUT("Stored envmap asset (fallback): " << resolvedPath);
          } else {
            PushWarn(fmt::format("Failed to read envmap asset: `{}`. reason = {}",
                                 assetPath.GetAssetPath(), readErr));
          }
        }
      } else if (!env.scene_config.load_texture_assets) {
        // Store asset path only without decoding
        Asset asset;
        std::string resolvedPath;
        std::string err;
        AssetInfo assetInfo;

        if (RawAssetRead(assetPath, assetInfo, env.asset_resolver, &asset,
                         resolvedPath, nullptr, nullptr, &err)) {
          TextureImage texImage;
          BufferData imageBuffer;
          imageBuffer.componentType = ComponentType::UInt8;

          texImage.asset_identifier = resolvedPath;

          imageBuffer.data.resize(asset.size());
          memcpy(imageBuffer.data.data(), asset.data(), asset.size());

          texImage.buffer_id = int64_t(buffers.size());
          buffers.emplace_back(imageBuffer);

          texImage.decoded = false;
          texImage.usdColorSpace = ColorSpace::Raw;

          rlight.envmap_texture_id = int32_t(images.size());
          images.emplace_back(texImage);

          DCOUT("Stored envmap asset: " << resolvedPath);
        } else {
          PushWarn(fmt::format("Failed to read envmap asset (load_texture_assets=false): `{}`. reason = {}",
                               assetPath.GetAssetPath(), err));
        }
      }
    }
  }

  // Extract texture format
  // Note: textureFormat is typically not time-sampled, use fallback/default
  if (light.textureFormat.authored() && !light.textureFormat.is_blocked()) {
    const auto& fmt_animatable = light.textureFormat.get_value();
    // Get default value directly from Animatable (not time-sampled)
    if (fmt_animatable.is_scalar()) {
      DomeLight::TextureFormat fmt;
      if (fmt_animatable.get_scalar(&fmt)) {
        switch (fmt) {
          case DomeLight::TextureFormat::Automatic:
            rlight.domeTextureFormat = RenderLight::DomeTextureFormat::Automatic;
            break;
          case DomeLight::TextureFormat::Latlong:
            rlight.domeTextureFormat = RenderLight::DomeTextureFormat::Latlong;
            break;
          case DomeLight::TextureFormat::MirroredBall:
            rlight.domeTextureFormat = RenderLight::DomeTextureFormat::MirroredBall;
            break;
          case DomeLight::TextureFormat::Angular:
            rlight.domeTextureFormat = RenderLight::DomeTextureFormat::Angular;
            break;
        }
      }
    }
  }

  // Extract guide radius
  if (light.guideRadius.authored() && !light.guideRadius.is_blocked()) {
    float val;
    if (light.guideRadius.get_value().get(env.timecode, &val)) {
      rlight.guideRadius = val;
    }
  }

  (*rlight_out) = std::move(rlight);
  return true;
}

bool RenderSceneConverter::ConvertRectLight(
    const RenderSceneConverterEnv &env,
    const Path &light_abs_path,
    const RectLight &light,
    RenderLight *rlight_out) {

  if (!rlight_out) {
    PUSH_ERROR_AND_RETURN("rlight_out arg is nullptr.");
  }

  RenderLight rlight;
  rlight.name = light.name;
  rlight.abs_path = light_abs_path.full_path_name();
  rlight.type = RenderLight::Type::Rect;

  // Extract common properties
  if (!ExtractCommonLightProperties(env, light, &rlight)) {
    return false;
  }

  // Extract shaping properties
  if (!ExtractShapingProperties(env, light, &rlight)) {
    return false;
  }

  // Extract width
  if (light.width.authored() && !light.width.is_blocked()) {
    float val;
    if (light.width.get_value().get(env.timecode, &val)) {
      rlight.width = val;
    }
  }

  // Extract height
  if (light.height.authored() && !light.height.is_blocked()) {
    float val;
    if (light.height.get_value().get(env.timecode, &val)) {
      rlight.height = val;
    }
  }

  // Extract texture file (optional)
  if (light.file.authored() && !light.file.is_blocked()) {
    value::AssetPath asset;
    if (light.file.get_value()->get(env.timecode, &asset)) {
      rlight.textureFile = asset.GetAssetPath();
    }
  }

  (*rlight_out) = std::move(rlight);
  return true;
}

bool RenderSceneConverter::ConvertDiskLight(
    const RenderSceneConverterEnv &env,
    const Path &light_abs_path,
    const DiskLight &light,
    RenderLight *rlight_out) {

  if (!rlight_out) {
    PUSH_ERROR_AND_RETURN("rlight_out arg is nullptr.");
  }

  RenderLight rlight;
  rlight.name = light.name;
  rlight.abs_path = light_abs_path.full_path_name();
  rlight.type = RenderLight::Type::Disk;

  // Extract common properties
  if (!ExtractCommonLightProperties(env, light, &rlight)) {
    return false;
  }

  // Extract radius
  if (light.radius.authored() && !light.radius.is_blocked()) {
    float val;
    if (light.radius.get_value().get(env.timecode, &val)) {
      rlight.radius = val;
    }
  }

  (*rlight_out) = std::move(rlight);
  return true;
}

bool RenderSceneConverter::ConvertCylinderLight(
    const RenderSceneConverterEnv &env,
    const Path &light_abs_path,
    const CylinderLight &light,
    RenderLight *rlight_out) {

  if (!rlight_out) {
    PUSH_ERROR_AND_RETURN("rlight_out arg is nullptr.");
  }

  RenderLight rlight;
  rlight.name = light.name;
  rlight.abs_path = light_abs_path.full_path_name();
  rlight.type = RenderLight::Type::Cylinder;

  // Extract common properties
  if (!ExtractCommonLightProperties(env, light, &rlight)) {
    return false;
  }

  // Extract length
  if (light.length.authored() && !light.length.is_blocked()) {
    float val;
    if (light.length.get_value().get(env.timecode, &val)) {
      rlight.length = val;
    }
  }

  // Extract radius
  if (light.radius.authored() && !light.radius.is_blocked()) {
    float val;
    if (light.radius.get_value().get(env.timecode, &val)) {
      rlight.radius = val;
    }
  }

  (*rlight_out) = std::move(rlight);
  return true;
}

bool RenderSceneConverter::ConvertGeometryLight(
    const RenderSceneConverterEnv &env,
    const Path &light_abs_path,
    const GeometryLight &light,
    RenderLight *rlight_out) {

  if (!rlight_out) {
    PUSH_ERROR_AND_RETURN("rlight_out arg is nullptr.");
  }

  RenderLight rlight;
  rlight.name = light.name;
  rlight.abs_path = light_abs_path.full_path_name();
  rlight.type = RenderLight::Type::Geometry;

  // Extract common properties
  if (!ExtractCommonLightProperties(env, light, &rlight)) {
    return false;
  }

  // Extract geometry relationship to find the target mesh
  // GeometryLight uses a relationship to point to the mesh geometry
  if (light.geometry.authored() && !light.geometry.is_blocked()) {
    const std::vector<Path> targets = light.geometry.get_targetPaths();
    if (!targets.empty()) {
      // Use the first target path
      const Path &target_path = targets[0];
      std::string geometry_path = target_path.full_path_name();

      // Try to find the mesh in the meshMap
      // Note: The actual mesh_id will be resolved during scene building
      // For now, we store the path and mark geometry_mesh_id as unresolved (-1)
      // The renderer should resolve this later by looking up the mesh by path
      rlight.geometry_mesh_id = -1;  // Will be resolved during BuildNodeHierarchy

      DCOUT("GeometryLight " << rlight.abs_path << " references geometry: " << geometry_path);
    } else {
      PUSH_WARN("GeometryLight " << rlight.abs_path << " has no geometry targets");
    }
  } else {
    PUSH_WARN("GeometryLight " << rlight.abs_path << " missing geometry relationship");
  }

  // Default material sync mode for GeometryLight
  rlight.material_sync_mode = "materialGlowTintsLight";

  (*rlight_out) = std::move(rlight);
  return true;
}

bool RenderSceneConverter::BuildNodeHierarchy(
    const RenderSceneConverterEnv &env, const XformNode &root) {
  std::string defaultRootNode = env.stage.metas().defaultPrim.str();

  default_node = -1;

  for (const auto &rootNode : root.children) {
    Node root_node;
    if (!BuildNodeHierarchyImpl(env, /* root */ "", rootNode, root_node)) {
      return false;
    }

    if (defaultRootNode == rootNode.element_name) {
      default_node = int(root_nodes.size());
    }

    root_nodeMap.add("/" + rootNode.element_name, root_nodes.size());
    root_nodes.push_back(root_node);
  }

  return true;
}

bool RenderSceneConverter::ConvertToRenderScene(
    const RenderSceneConverterEnv &env, RenderScene *scene) {
  if (!scene) {
    PUSH_ERROR_AND_RETURN("nullptr for RenderScene argument.");
  }

  // Reset progress state
  _progress_info = DetailedProgressInfo{};

  // Clear lookup caches from previous conversion
  _skelPathToIndex.clear();
  _animPathToIndex.clear();
  _skelNameToIndexCache.clear();
  _skelRootToSkeleton.clear();
  _uvNameCache.clear();
  ResetConnectionResolveCache(env.stage);

  // Report initial progress
  if (!CallProgressCallback(0.0f)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  // Count meshes and materials before conversion for accurate progress reporting
  // Single-pass traversal: walk the stage tree once and classify prims by type_id
  DCOUT("[Tydra] Counting primitives...");
  PathPrimMap<GeomMesh> meshPrimMap;
  PathPrimMap<GeomCube> cubePrimMap;
  PathPrimMap<GeomSphere> spherePrimMap;
  PathPrimMap<Material> materialPrimMap;
  PathPrimMap<Skeleton> allSkeletons;
  PathPrimMap<SkelRoot> allSkelRoots;
  PathPrimMap<SkelAnimation> allAnimations;

  {
    // Iterative stack-based traversal visiting each prim exactly once
    struct StackEntry {
      const Prim *parent;
      size_t child_idx;
      size_t parent_path_len;
    };
    std::vector<StackEntry> stack;
    stack.reserve(64);
    std::string path_buf;
    path_buf.reserve(256);

    auto classifyPrim = [&](const Prim &prim) {
      switch (prim.type_id()) {
        case value::TYPE_ID_GEOM_MESH:
          if (const auto *p = prim.as<GeomMesh>()) meshPrimMap[path_buf] = p;
          break;
        case value::TYPE_ID_GEOM_CUBE:
          if (const auto *p = prim.as<GeomCube>()) cubePrimMap[path_buf] = p;
          break;
        case value::TYPE_ID_GEOM_SPHERE:
          if (const auto *p = prim.as<GeomSphere>()) spherePrimMap[path_buf] = p;
          break;
        case value::TYPE_ID_MATERIAL:
          if (const auto *p = prim.as<Material>()) materialPrimMap[path_buf] = p;
          break;
        case value::TYPE_ID_SKELETON:
          if (const auto *p = prim.as<Skeleton>()) allSkeletons[path_buf] = p;
          break;
        case value::TYPE_ID_SKEL_ROOT:
          if (const auto *p = prim.as<SkelRoot>()) allSkelRoots[path_buf] = p;
          break;
        case value::TYPE_ID_SKELANIMATION:
          if (const auto *p = prim.as<SkelAnimation>()) allAnimations[path_buf] = p;
          break;
        default:
          break;
      }
    };

    for (const auto &root_prim : env.stage.root_prims()) {
      path_buf = "/" + root_prim.local_path().full_path_name();
      classifyPrim(root_prim);

      if (!root_prim.children().empty()) {
        stack.push_back({&root_prim, 0, 0});
      }

      while (!stack.empty()) {
        auto &top = stack.back();
        if (top.child_idx >= top.parent->children().size()) {
          path_buf.resize(top.parent_path_len);
          stack.pop_back();
          continue;
        }

        const Prim &child = top.parent->children()[top.child_idx];
        ++top.child_idx;

        size_t cur_len = path_buf.size();
        path_buf += "/";
        path_buf += child.local_path().full_path_name();

        classifyPrim(child);

        if (!child.children().empty()) {
          stack.push_back({&child, 0, cur_len});
        } else {
          path_buf.resize(cur_len);
        }
      }
    }
  }
  DCOUT("[Tydra] Pre-discovered " << allSkeletons.size() << " skeletons, "
        << allSkelRoots.size() << " skelroots, " << allAnimations.size() << " animations");

  SkelRootSkeletonResolver::BuildMap(allSkeletons, allSkelRoots,
                                     &_skelRootToSkeleton);
  DCOUT("Precomputed SkelRoot->Skeleton entries: " << _skelRootToSkeleton.size());

  // Total meshes includes GeomMesh, GeomCube, and GeomSphere (all converted to meshes)
  const size_t total_meshes = meshPrimMap.size() + cubePrimMap.size() + spherePrimMap.size();
  const size_t total_materials = materialPrimMap.size();
  DCOUT("[Tydra] Found " << total_meshes << " meshes ("
        << meshPrimMap.size() << " mesh, " << cubePrimMap.size() << " cube, "
        << spherePrimMap.size() << " sphere), " << total_materials << " materials");

  // Report counting complete via detailed progress
  _progress_info.stage = DetailedProgressInfo::Stage::CountingPrims;
  _progress_info.meshes_total = total_meshes;
  _progress_info.materials_total = total_materials;
  _progress_info.message = "Counted " + std::to_string(total_meshes) + " meshes, " +
                           std::to_string(total_materials) + " materials";
  CallDetailedProgressCallback(_progress_info);

  // 1. Convert Xform
  // 2. Convert Material/Texture
  // 3. Convert Mesh/SkinWeights/BlendShapes
  // 4. Convert Skeleton(bones)
  // 5. Build node hierarchy (includes lights and cameras)

  //
  // 1. Build Xform at specified time.
  //    Each Prim in Stage is converted to XformNode.
  //
  _progress_info.stage = DetailedProgressInfo::Stage::ConvertingXforms;
  _progress_info.progress = 0.1f;
  _progress_info.message = "Building xform hierarchy";
  CallDetailedProgressCallback(_progress_info);

  XformNode xform_node;
  if (!BuildXformNodeFromStage(env.stage, &xform_node, env.timecode)) {
    PUSH_ERROR_AND_RETURN("Failed to build Xform node hierarchy.\n");
  }

  // Report progress after xform building (20%)
  if (!CallProgressCallback(0.2f)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  std::string err;

  //
  // 2. Convert Material/Texture
  // 3. Convert Mesh/SkinWeights/BlendShapes
  // 4. Convert Skeleton(bones) and SkelAnimation
  //
  // Material conversion will be done in MeshVisitor.
  //
  _progress_info.stage = DetailedProgressInfo::Stage::ConvertingMeshes;
  _progress_info.progress = 0.2f;
  _progress_info.message = "Converting meshes and materials";
  CallDetailedProgressCallback(_progress_info);

  MeshVisitorEnv menv;
  menv.env = &env;
  menv.converter = this;
  menv.meshes_total = total_meshes;
  menv.materials_total = total_materials;
  menv.allSkeletons = &allSkeletons;
  menv.allSkelRoots = &allSkelRoots;
  menv.allAnimations = &allAnimations;

  // Store pre-discovered maps in converter for use by ConvertMesh
  _allSkeletons = &allSkeletons;
  _allSkelRoots = &allSkelRoots;
  _allAnimations = &allAnimations;

  bool ret = tydra::VisitPrims(env.stage, MeshVisitor, &menv, &err);

  if (!ret) {
    PUSH_ERROR_AND_RETURN(err);
  }

  // Convert all SkelAnimation prims now that all skeletons have been discovered.
  // This supports multiple animations per skeleton (when animationSource is a pathvector).
  DCOUT("Converting all SkelAnimation prims...");
  if (!ConvertAllSkelAnimations(env)) {
    PUSH_ERROR_AND_RETURN("Failed to convert SkelAnimation prims");
  }
  DCOUT("SkelAnimation conversion complete");

  // Clear temporary pointers
  _allSkeletons = nullptr;
  _allSkelRoots = nullptr;
  _allAnimations = nullptr;
  _skelRootToSkeleton.clear();

  // Report progress after mesh/material conversion (70%)
  _progress_info.stage = DetailedProgressInfo::Stage::BuildingHierarchy;
  _progress_info.progress = 0.7f;
  _progress_info.meshes_processed = menv.meshes_processed;
  _progress_info.message = "Mesh conversion complete (" +
      std::to_string(menv.meshes_processed) + " meshes)";
  CallDetailedProgressCallback(_progress_info);

  if (!CallProgressCallback(0.7f)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  //
  // 5. Build node hierarchy from XformNode and meshes, materials, skeletons,
  // etc.
  //
  _progress_info.message = "Building node hierarchy";
  CallDetailedProgressCallback(_progress_info);

  if (!BuildNodeHierarchy(env, xform_node)) {
    return false;
  }

  // Report progress after node hierarchy building (85%)
  _progress_info.stage = DetailedProgressInfo::Stage::ExtractingAnimations;
  _progress_info.progress = 0.85f;
  _progress_info.message = "Hierarchy complete, extracting animations";
  CallDetailedProgressCallback(_progress_info);

  if (!CallProgressCallback(0.85f)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  //
  // 6. Extract xformOp animations from nodes with time-sampled transforms
  //
  {
    // Single-pass depth-first traversal with stable node indices.
    // This avoids repeatedly counting subtree sizes.
    std::function<void(const XformNode&, int32_t&)> extractAnimationsFromNode;
    extractAnimationsFromNode = [&](const XformNode& node, int32_t& next_node_index) {
      const int32_t node_index = next_node_index++;

      // Check if this node has a prim with xformOps
      if (node.prim && IsXformablePrim(*node.prim)) {
        const Xformable *xformable = nullptr;
        if (CastToXformable(*node.prim, &xformable) && xformable) {
          // Check if xformable has time-sampled transforms
          if (xformable->has_timesamples()) {
            AnimationClip anim;
            // node.absolute_path is already a Path object
            const Path &prim_path = node.absolute_path;

            // Extract xformOp animation
            if (ExtractXformOpAnimation(env, prim_path, node.element_name,
                                       *xformable, node_index, &anim)) {
              // Check if animation with this path already exists via O(1) lookup
              const auto &anim_abs_path = anim.abs_path;
              if (_animPathToIndex.find(anim_abs_path) == _animPathToIndex.end()) {
                DCOUT("Extracted xformOp animation from: " << anim_abs_path);
                _animPathToIndex[anim_abs_path] = int32_t(animations.size());
                animations.emplace_back(std::move(anim));
              }
            }
          }
        }
      }

      for (const auto& child : node.children) {
        extractAnimationsFromNode(child, next_node_index);
      }
    };

    int32_t current_node_index = 0;
    for (const auto& root : xform_node.children) {
      extractAnimationsFromNode(root, current_node_index);
    }
  }

  // Report progress after animation extraction (90%)
  if (!CallProgressCallback(0.9f)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  //
  // 7. Merge meshes with same material (optional optimization)
  //
  if (env.scene_config.merge_meshes) {
    if (!MergeMeshesImpl(env)) {
      PushWarn("Mesh merging encountered issues, but conversion continues.\n");
    }
  }

  // Report progress after mesh merging (95%)
  if (!CallProgressCallback(0.95f)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  // render_scene.meshMap = std::move(meshMap);
  // render_scene.materialMap = std::move(materialMap);
  // render_scene.textureMap = std::move(textureMap);
  // render_scene.imageMap = std::move(imageMap);
  // render_scene.bufferMap = std::move(bufferMap);

  RenderScene render_scene;
  render_scene.usd_filename = env.usd_filename;
  render_scene.default_root_node = 0;
  if (default_node > -1) {
    if (size_t(default_node) >= root_nodes.size()) {
      PushWarn("Invalid default_node id. Use 0 for default_node id.");
    } else {
      render_scene.default_root_node = uint32_t(default_node);
    }
  }

  render_scene.nodes = std::move(root_nodes);
  render_scene.meshes = std::move(meshes);
  render_scene.textures = std::move(textures);
  render_scene.images = std::move(images);
  render_scene.buffers = std::move(buffers);
  render_scene.materials = std::move(materials);
  render_scene.cameras = std::move(cameras);
  render_scene.lights = std::move(lights);
  render_scene.skeletons = std::move(skeletons);
  render_scene.animations = std::move(animations);

  // Populate scene metadata from Stage
  {
    const auto &stage_metas = env.stage.metas();

    // upAxis
    if (stage_metas.upAxis.authored()) {
      render_scene.meta.upAxis = to_string(stage_metas.upAxis.get_value());
    }

    // metersPerUnit
    if (stage_metas.metersPerUnit.authored()) {
      render_scene.meta.metersPerUnit = stage_metas.metersPerUnit.get_value();
    }

    // framesPerSecond
    if (stage_metas.framesPerSecond.authored()) {
      render_scene.meta.framesPerSecond = stage_metas.framesPerSecond.get_value();
    }

    // timeCodesPerSecond
    if (stage_metas.timeCodesPerSecond.authored()) {
      render_scene.meta.timeCodesPerSecond = stage_metas.timeCodesPerSecond.get_value();
    }

    // startTimeCode
    if (stage_metas.startTimeCode.authored()) {
      render_scene.meta.startTimeCode = stage_metas.startTimeCode.get_value();
    }

    // endTimeCode
    if (stage_metas.endTimeCode.authored()) {
      render_scene.meta.endTimeCode = stage_metas.endTimeCode.get_value();
    }

    // autoPlay
    if (stage_metas.autoPlay.authored()) {
      render_scene.meta.autoPlay = stage_metas.autoPlay.get_value();
    }

    // comment
    if (!stage_metas.comment.value.empty()) {
      render_scene.meta.comment = stage_metas.comment.value;
    }

    // copyright - Check if customLayerData contains copyright info
    auto it = stage_metas.customLayerData.find("copyright");
    if (it != stage_metas.customLayerData.end()) {
      // Try to extract string value from MetaVariable
      auto copyright_val = it->second.get_value<std::string>();
      if (copyright_val) {
        render_scene.meta.copyright = copyright_val.value();
      }
    }
  }

  (*scene) = std::move(render_scene);

  // Report completion (100%)
  _progress_info.stage = DetailedProgressInfo::Stage::Complete;
  _progress_info.progress = 1.0f;
  _progress_info.message = "Conversion complete";
  CallDetailedProgressCallback(_progress_info);
  CallProgressCallback(1.0f);

  DCOUT("[Tydra] Conversion complete: " << scene->meshes.size() << " meshes, "
        << scene->materials.size() << " materials, " << scene->textures.size() << " textures");

  return true;
}

bool RenderSceneConverter::ConvertSkeletonFromPtr(const RenderSceneConverterEnv &env,
                       const Path &skelPath,
                       const Skeleton &skel,
                       const std::string &primName,
                       SkelHierarchy *out_skel) {
  (void)env;

  if (!out_skel) {
    return false;
  }

  SkelHierarchy dst;
  SkelNode root;
  if (!BuildSkelHierarchy(skel, root, &_err)) {
    return false;
  }
  dst.abs_path = skelPath.prim_part();
  dst.prim_name = primName;
  dst.display_name = skel.metas().has_displayName() ? skel.metas().get_displayName() : "";
  dst.root_node = root;

  (*out_skel) = std::move(dst);
  return true;
}

bool RenderSceneConverter::ConvertSkeletonImplWithPath(const RenderSceneConverterEnv &env, const Path &skelPath,
                       SkelHierarchy *out_skel) {

  if (!out_skel) {
    return false;
  }

  if (skelPath.is_valid()) {
    const Prim *skelPrim{nullptr};
    if (!env.stage.find_prim_at_path(skelPath, skelPrim, &_err)) {
      return false;
    }

    SkelHierarchy dst;
    if (const auto pskel = skelPrim->as<Skeleton>()) {
      SkelNode root;
      if (!BuildSkelHierarchy((*pskel), root, &_err)) {
        return false;
      }
      dst.abs_path = skelPath.prim_part();
      dst.prim_name = skelPrim->element_name();
      dst.display_name = pskel->metas().has_displayName() ? pskel->metas().get_displayName() : "";
      dst.root_node = root;
    } else {
      PUSH_ERROR_AND_RETURN("Prim is not Skeleton.");
    }

    (*out_skel) = std::move(dst);
    return true;
  }

  PUSH_ERROR_AND_RETURN("`skel:skeleton` path is invalid.");
}

bool RenderSceneConverter::ConvertAllSkelAnimations(const RenderSceneConverterEnv &env) {
  // This method processes all SkelAnimation prims discovered during pre-processing.
  // For each SkelAnimation, we find which Skeleton it belongs to via:
  //   1. Skeleton's skel:animationSource relationship
  //   2. SkelRoot's skel:animationSource relationship (inherited per USD spec)
  //   3. Parent path hierarchy (SkelAnimation as child of Skeleton)

  if (!_allAnimations || _allAnimations->empty()) {
    return true; // No animations to process
  }

  DCOUT("ConvertAllSkelAnimations: processing " << _allAnimations->size() << " SkelAnimation prims");

  // Build reverse map: animationPath -> list of skeleton_ids that reference it
  std::map<std::string, std::vector<int32_t>> animPathToSkelIds;

  // Helper: extract animation paths from a Relationship
  auto extractAnimPaths = [](const Relationship &rel, std::vector<Path> &out) {
    if (rel.is_path()) {
      out.push_back(rel.targetPath);
    } else if (rel.is_pathvector()) {
      out.insert(out.end(), rel.targetPathVector.begin(), rel.targetPathVector.end());
    }
  };

  // 1. Check Skeleton prims for skel:animationSource
  for (const auto &skelEntry : _skelPathToIndex) {
    const std::string &skelPathStr = skelEntry.first;
    const int32_t skel_id = skelEntry.second;

    Path skelPath(skelPathStr, "");
    const Prim *skelPrim{nullptr};
    if (!env.stage.find_prim_at_path(skelPath, skelPrim, &_err)) {
      continue;
    }

    const auto *pskel = skelPrim->as<Skeleton>();
    if (!pskel) continue;

    std::vector<Path> animPaths;

    if (pskel->animationSource.has_value()) {
      extractAnimPaths(pskel->animationSource.value(), animPaths);
    }

    // 2. If Skeleton has no animationSource, check ancestor SkelRoot prims
    //    (implements USD SkelBindingAPI inheritance)
    if (animPaths.empty() && _allSkelRoots) {
      // Walk up the path hierarchy to find a SkelRoot with animationSource
      std::string parentPath = skelPathStr;
      while (!parentPath.empty()) {
        size_t lastSlash = parentPath.rfind('/');
        if (lastSlash == 0 || lastSlash == std::string::npos) {
          parentPath = "/";  // root
        } else {
          parentPath = parentPath.substr(0, lastSlash);
        }

        auto rootIt = _allSkelRoots->find(parentPath);
        if (rootIt != _allSkelRoots->end() && rootIt->second) {
          const SkelRoot *pskelRoot = rootIt->second;
          if (pskelRoot->animationSource.has_value()) {
            extractAnimPaths(pskelRoot->animationSource.value(), animPaths);
            DCOUT("Inherited animationSource from SkelRoot " << parentPath
                  << " for Skeleton " << skelPathStr);
            break;
          }
        }
        if (parentPath == "/") break;
      }
    }

    for (const Path &animPath : animPaths) {
      std::string ap = animPath.prim_part();
      animPathToSkelIds[ap].push_back(skel_id);
    }
  }

  DCOUT("Built reverse map: " << animPathToSkelIds.size() << " animations referenced by skeletons");

  // 3. For SkelAnimation prims not referenced by any animationSource,
  //    associate them with a parent Skeleton by path hierarchy.
  //    This enables multi-clip workflows where SkelAnimation prims are children
  //    of a Skeleton but not all are the active animationSource.
  for (const auto &animEntry : *_allAnimations) {
    const std::string &animPathStr = animEntry.first;

    // Skip if already referenced
    if (animPathToSkelIds.find(animPathStr) != animPathToSkelIds.end()) {
      continue;
    }

    // Walk up parent path to find a Skeleton
    std::string parentPath = animPathStr;
    while (!parentPath.empty()) {
      size_t lastSlash = parentPath.rfind('/');
      if (lastSlash == 0 || lastSlash == std::string::npos) {
        parentPath.clear();
        break;
      }
      parentPath = parentPath.substr(0, lastSlash);

      auto skelIt = _skelPathToIndex.find(parentPath);
      if (skelIt != _skelPathToIndex.end()) {
        animPathToSkelIds[animPathStr].push_back(skelIt->second);
        DCOUT("Associated SkelAnimation " << animPathStr
              << " with parent Skeleton " << parentPath
              << " (skeleton_id=" << skelIt->second << ")");
        break;
      }
    }

    if (animPathToSkelIds.find(animPathStr) == animPathToSkelIds.end()) {
      DCOUT("SkelAnimation " << animPathStr << " has no associated skeleton (skipping)");
    }
  }

  // Now convert each SkelAnimation prim
  for (const auto &animEntry : *_allAnimations) {
    const std::string &animPathStr = animEntry.first;
    const SkelAnimation *panimPtr = animEntry.second;

    if (!panimPtr) {
      PUSH_WARN("Null SkelAnimation pointer for path: " + animPathStr);
      continue;
    }

    auto it = animPathToSkelIds.find(animPathStr);
    if (it == animPathToSkelIds.end() || it->second.empty()) {
      DCOUT("SkelAnimation " << animPathStr << " not associated with any skeleton (skipping)");
      continue;
    }

    // Convert the animation for each skeleton that references it
    for (int32_t skeleton_id : it->second) {
      std::string cacheKey = animPathStr + ":" + std::to_string(skeleton_id);
      if (_animPathToIndex.find(cacheKey) != _animPathToIndex.end()) {
        DCOUT("Animation " << animPathStr << " already converted for skeleton " << skeleton_id);
        continue;
      }

      Path animPath(animPathStr, "");
      AnimationClip anim;

      if (!ConvertSkelAnimation(env, animPath, *panimPtr, skeleton_id, &anim)) {
        PUSH_WARN("Failed to convert SkelAnimation: " + animPathStr + " for skeleton " + std::to_string(skeleton_id));
        continue;
      }

      DCOUT("Converted SkelAnimation " << animPathStr << " for skeleton " << skeleton_id);

      // Add to animations vector
      int32_t anim_id = int32_t(animations.size());
      _animPathToIndex[cacheKey] = anim_id;
      animations.emplace_back(std::move(anim));

      // Update skeleton's animation IDs.
      if (skeleton_id >= 0 && skeleton_id < int32_t(skeletons.size())) {
        auto &skel = skeletons[static_cast<size_t>(skeleton_id)];
        if (std::find(skel.anim_ids.begin(), skel.anim_ids.end(), anim_id) ==
            skel.anim_ids.end()) {
          skel.anim_ids.push_back(anim_id);
        }

        // Keep legacy default animation field for backward compatibility.
        if (skeletons[static_cast<size_t>(skeleton_id)].anim_id < 0) {
          skeletons[static_cast<size_t>(skeleton_id)].anim_id = anim_id;
          DCOUT("Set skeleton " << skeleton_id << " anim_id to " << anim_id);
        }
      }
    }
  }

  DCOUT("ConvertAllSkelAnimations: converted " << animations.size() << " animation clips");
  return true;
}

bool DefaultTextureImageLoaderFunction(
    const value::AssetPath &assetPath, const AssetInfo &assetInfo,
    const AssetResolutionResolver &assetResolver, TextureImage *texImageOut,
    std::vector<uint8_t> *imageData, void *userdata, std::string *warn,
    std::string *err) {
  if (!texImageOut) {
    if (err) {
      (*err) = "`imageOut` argument is nullptr\n";
    }
    return false;
  }

  if (!imageData) {
    if (err) {
      (*err) = "`imageData` argument is nullptr\n";
    }
    return false;
  }

  // TODO: assetInfo
  (void)assetInfo;
  (void)userdata;
  (void)warn;

  std::string resolvedPath = assetResolver.resolve(assetPath.GetAssetPath());

  if (resolvedPath.empty()) {
    if (err) {
      (*err) += fmt::format("Failed to resolve asset path: {}\n",
                            assetPath.GetAssetPath());
    }
    return false;
  }

  Asset asset;
  bool ret = assetResolver.open_asset(resolvedPath, assetPath.GetAssetPath(),
                                      &asset, warn, err);
  if (!ret) {
    if (err) {
      (*err) += fmt::format("Failed to open asset: {}", resolvedPath);
    }
    return false;
  }

  DCOUT("Resolved asset path = " << resolvedPath);

  // TODO: user-defined image loader handler.
  auto result = tinyusdz::image::LoadImageFromMemory(asset.data(), asset.size(),
                                                     resolvedPath);
  if (!result) {
    if (err) {
      (*err) += "Failed to load image file: " + result.error() + "\n";
    }
    return false;
  }

  TextureImage texImage;

  texImage.asset_identifier = resolvedPath;
  texImage.channels = result.value().image.channels;

  const auto &imgret = result.value();

  if (imgret.image.bpp == 8) {
    // assume uint8
    texImage.assetTexelComponentType = ComponentType::UInt8;
  } else if (imgret.image.bpp == 16) {
    if (imgret.image.format == Image::PixelFormat::UInt) {
      texImage.assetTexelComponentType = ComponentType::UInt16;
    } else if (imgret.image.format == Image::PixelFormat::Int) {
      texImage.assetTexelComponentType = ComponentType::Int16;
    } else if (imgret.image.format == Image::PixelFormat::Float) {
      texImage.assetTexelComponentType = ComponentType::Half;
    } else {
      if (err) {
        (*err) += "Invalid image.pixelformat: " + tinyusdz::to_string(imgret.image.format) + "\n";
      }
      return false;
    }

  } else if (imgret.image.bpp == 32) {
    if (imgret.image.format == Image::PixelFormat::UInt) {
      texImage.assetTexelComponentType = ComponentType::UInt32;
    } else if (imgret.image.format == Image::PixelFormat::Int) {
      texImage.assetTexelComponentType = ComponentType::Int32;
    } else if (imgret.image.format == Image::PixelFormat::Float) {
      texImage.assetTexelComponentType = ComponentType::Float;
    } else {
      if (err) {
        (*err) += "Invalid image.pixelformat: " + tinyusdz::to_string(imgret.image.format) + "\n";
      }
      return false;
    }
  } else {
    DCOUT("TODO: bpp = " << result.value().image.bpp);
    if (err) {
      (*err) += "TODO or unsupported bpp: " +
               std::to_string(result.value().image.bpp) + "\n";
    }
    return false;
  }

  texImage.channels = result.value().image.channels;
  texImage.width = result.value().image.width;
  texImage.height = result.value().image.height;

  (*texImageOut) = texImage;

  // raw image data
  (*imageData) = result.value().image.data;

  return true;
}

bool InferColorSpace(const value::token &tok, ColorSpace *cty) {
  if (!cty) {
    return false;
  }

  if (tok.str() == "raw") {
    (*cty) = ColorSpace::Raw;
  } else if (tok.str() == "Raw") {
    (*cty) = ColorSpace::Raw;
  } else if (tok.str() == "srgb") {
    (*cty) = ColorSpace::sRGB;
  } else if (tok.str() == "sRGB") {
    (*cty) = ColorSpace::sRGB;
  } else if (tok.str() == "srgb_texture") {  // MaterialX texture colorspace
    (*cty) = ColorSpace::sRGB_Texture;
  } else if (tok.str() == "linear") { // guess linear_srgb
    (*cty) = ColorSpace::Lin_sRGB;
  } else if (tok.str() == "lin_srgb") {
    (*cty) = ColorSpace::Lin_sRGB;
  } else if (tok.str() == "rec709") {
    (*cty) = ColorSpace::Rec709;
  } else if (tok.str() == "lin_rec709") {  // MaterialX linear Rec.709
    (*cty) = ColorSpace::Lin_Rec709;
  } else if (tok.str() == "g22_rec709") {  // MaterialX gamma 2.2 Rec.709
    (*cty) = ColorSpace::g22_Rec709;
  } else if (tok.str() == "g18_rec709") {  // MaterialX gamma 1.8 Rec.709
    (*cty) = ColorSpace::g18_Rec709;
  } else if (tok.str() == "lin_rec2020") {  // Linear Rec.2020
    (*cty) = ColorSpace::Lin_Rec2020;
  } else if (tok.str() == "acescg") {  // Alternative ACES CG naming
    (*cty) = ColorSpace::Lin_ACEScg;
  } else if (tok.str() == "lin_ap1") {  // Linear AP1 (same as ACEScg)
    (*cty) = ColorSpace::Lin_ACEScg;
  } else if (tok.str() == "aces2065-1") {  // ACES 2065-1
    (*cty) = ColorSpace::ACES2065_1;
  } else if (tok.str() == "ocio") {
    (*cty) = ColorSpace::OCIO;
  } else if (tok.str() == "lin_displayp3") {
    (*cty) = ColorSpace::Lin_DisplayP3;
  } else if (tok.str() == "srgb_displayp3") {
    (*cty) = ColorSpace::sRGB_DisplayP3;

    //
    // seen in Apple's USDZ model(or OCIO?)
    //

  } else if (tok.str() == "ACES - ACEScg") {
    (*cty) = ColorSpace::Lin_ACEScg;
  } else if (tok.str() == "Input - Texture - sRGB - Display P3") {
    (*cty) = ColorSpace::sRGB_DisplayP3;
  } else if (tok.str() == "Input - Texture - sRGB - sRGB") {
    (*cty) = ColorSpace::sRGB;
  } else if (tok.str() == "custom") {
    (*cty) = ColorSpace::Custom;
  } else {
    return false;
  }

  return true;
}


// Memory usage estimation implementations

size_t RenderMesh::estimate_memory_usage() const {
  size_t total = sizeof(RenderMesh);

  // String storage
  total += prim_name.capacity();
  total += abs_path.capacity();
  total += display_name.capacity();

  // Vertex data
  total += points.capacity() * sizeof(vec3);

  // Index data
  total += usdFaceVertexIndices.capacity() * sizeof(uint32_t);
  total += usdFaceVertexCounts.capacity() * sizeof(uint32_t);
  total += triangulatedFaceVertexIndices.capacity() * sizeof(uint32_t);
  total += triangulatedFaceVertexCounts.capacity() * sizeof(uint32_t);
  total += triangulatedToOrigFaceVertexIndexMap.capacity() * sizeof(size_t);
  total += triangulatedFaceCounts.capacity() * sizeof(uint32_t);

  // Vertex attributes helper
  auto estimate_vertex_attr = [](const VertexAttribute& attr) -> size_t {
    size_t size = sizeof(VertexAttribute);
    size += attr.name.capacity();
    size += attr.data.capacity();
    size += attr.indices.capacity() * sizeof(uint32_t);
    return size;
  };

  total += estimate_vertex_attr(normals);
  total += estimate_vertex_attr(tangents);
  total += estimate_vertex_attr(binormals);
  total += estimate_vertex_attr(vertex_colors);
  total += estimate_vertex_attr(vertex_opacities);

  // Texcoords map
  for (const auto& texcoord_pair : texcoords) {
    total += sizeof(uint32_t) + estimate_vertex_attr(texcoord_pair.second);
  }

  // StringAndIdMap for texcoords
  total += texcoordSlotIdMap.size() * (sizeof(uint64_t) + sizeof(std::string));
  for (auto it = texcoordSlotIdMap.s_begin(); it != texcoordSlotIdMap.s_end(); ++it) {
    total += it->first.capacity();
  }

  // Joint and weights (basic estimate)
  total += sizeof(JointAndWeight);
  // TODO: Add detailed JointAndWeight internal memory estimation

  // Blend shapes
  for (const auto& blend_shape_pair : targets) {
    total += blend_shape_pair.first.capacity() + sizeof(ShapeTarget);
    // TODO: Add detailed ShapeTarget internal memory estimation
  }

  // Material subset map
  for (const auto& subset_pair : material_subsetMap) {
    total += subset_pair.first.capacity() + sizeof(MaterialSubset);
    // TODO: Add detailed MaterialSubset internal memory estimation
  }

  return total;
}

size_t RenderScene::estimate_memory_usage() const {
  size_t total = sizeof(RenderScene);

  // Scene metadata and filename
  total += usd_filename.capacity();
  // Note: SceneMetadata memory would need detailed estimation
  total += sizeof(SceneMetadata);

  // Estimate containers
  total += nodes.capacity() * sizeof(Node);
  (void)nodes; // Suppress unused variable warning

  total += images.capacity() * sizeof(TextureImage);
  (void)images; // Suppress unused variable warning

  total += materials.capacity() * sizeof(RenderMaterial);
  (void)materials; // Suppress unused variable warning

  total += cameras.capacity() * sizeof(RenderCamera);
  total += lights.capacity() * sizeof(RenderLight);

  total += textures.capacity() * sizeof(UVTexture);
  for (const auto& texture : textures) {
    total += texture.prim_name.capacity();
    total += texture.abs_path.capacity();
    total += texture.display_name.capacity();
  }

  // Meshes - use the detailed estimation
  total += meshes.capacity() * sizeof(RenderMesh);
  for (const auto& mesh : meshes) {
    total += mesh.estimate_memory_usage() - sizeof(RenderMesh); // Avoid double-counting base size
  }

  total += animations.capacity() * sizeof(AnimationClip);
  (void)animations; // Suppress unused variable warning

  total += skeletons.capacity() * sizeof(SkelHierarchy);
  (void)skeletons; // Suppress unused variable warning

  total += buffers.capacity() * sizeof(BufferData);
  for (const auto& buffer : buffers) {
    total += buffer.data.capacity();
  }

  return total;
}

void RenderSceneConverter::SetProgressCallback(ProgressCallback callback, void *userptr) {
  _progress_callback = callback;
  _progress_userptr = userptr;
}

void RenderSceneConverter::SetDetailedProgressCallback(DetailedProgressCallback callback, void *userptr) {
  _detailed_progress_callback = callback;
  _detailed_progress_userptr = userptr;
}

bool RenderSceneConverter::CallProgressCallback(float progress) {
  if (_progress_callback) {
    return _progress_callback(progress, _progress_userptr);
  }
  return true; // Continue if no callback set
}

bool RenderSceneConverter::CallDetailedProgressCallback(const DetailedProgressInfo &info) {
  if (_detailed_progress_callback) {
    return _detailed_progress_callback(info, _detailed_progress_userptr);
  }
  return true; // Continue if no callback set
}

bool RenderSceneConverter::ReportMeshProgress(size_t meshes_processed, size_t meshes_total,
                                               const std::string& mesh_name, const std::string& message) {
  _progress_info.stage = DetailedProgressInfo::Stage::ConvertingMeshes;
  _progress_info.meshes_processed = meshes_processed;
  _progress_info.meshes_total = meshes_total;
  _progress_info.current_mesh_name = mesh_name;
  _progress_info.message = message;

  // Calculate progress: meshes are 20%-70% of total progress (50% range)
  float mesh_progress = 0.2f + (0.5f * float(meshes_processed) / float(std::max(size_t(1), meshes_total)));
  _progress_info.progress = mesh_progress;

  return CallDetailedProgressCallback(_progress_info);
}

bool RenderSceneConverter::IsMeshMergeable(const RenderMesh &mesh) const {
  // Mesh cannot be merged if:
  // 1. Has skeletal animation
  if (mesh.skel_id >= 0) {
    return false;
  }

  // 2. Has blend shapes
  if (!mesh.targets.empty()) {
    return false;
  }

  // 3. Has per-face materials (GeomSubset)
  if (!mesh.material_subsetMap.empty()) {
    return false;
  }

  // 4. Is an area light (special rendering)
  if (mesh.is_area_light) {
    return false;
  }

  return true;
}

// Helper function to transform a vec3 point by a matrix4d
static vec3 TransformPoint(const value::matrix4d &m, const vec3 &p) {
  // Apply full 4x4 transform (position)
  double x = m.m[0][0] * double(p[0]) + m.m[1][0] * double(p[1]) + m.m[2][0] * double(p[2]) + m.m[3][0];
  double y = m.m[0][1] * double(p[0]) + m.m[1][1] * double(p[1]) + m.m[2][1] * double(p[2]) + m.m[3][1];
  double z = m.m[0][2] * double(p[0]) + m.m[1][2] * double(p[1]) + m.m[2][2] * double(p[2]) + m.m[3][2];
  double w = m.m[0][3] * double(p[0]) + m.m[1][3] * double(p[1]) + m.m[2][3] * double(p[2]) + m.m[3][3];

  if (std::abs(w) > 1e-10) {
    x /= w;
    y /= w;
    z /= w;
  }

  return vec3{float(x), float(y), float(z)};
}

// Helper function to transform a vec3 direction (normal) by a matrix4d
// Uses the upper-left 3x3 of the inverse-transpose for correct normal transformation
static vec3 TransformNormal(const value::matrix4d &m, const vec3 &n) {
  // For normals, we need the inverse transpose of the upper-left 3x3
  // For now, we use the upper-left 3x3 directly (correct for uniform scale and rotation only)
  // TODO: Proper inverse-transpose for non-uniform scale
  double x = m.m[0][0] * double(n[0]) + m.m[1][0] * double(n[1]) + m.m[2][0] * double(n[2]);
  double y = m.m[0][1] * double(n[0]) + m.m[1][1] * double(n[1]) + m.m[2][1] * double(n[2]);
  double z = m.m[0][2] * double(n[0]) + m.m[1][2] * double(n[1]) + m.m[2][2] * double(n[2]);

  // Normalize the result
  double len = std::sqrt(x*x + y*y + z*z);
  if (len > 1e-10) {
    x /= len;
    y /= len;
    z /= len;
  }

  return vec3{float(x), float(y), float(z)};
}

bool RenderSceneConverter::MergeMeshData(const RenderMesh &src,
                                          const value::matrix4d &src_transform,
                                          RenderMesh &dst) {
  // Check if transform is identity using tinyusdz::is_identity function
  bool transform_is_identity = tinyusdz::is_identity(src_transform);

  // Get the vertex offset for index adjustment
  uint32_t vertex_offset = static_cast<uint32_t>(dst.points.size());

  // Merge points (with transform if needed)
  if (transform_is_identity) {
    dst.points.insert(dst.points.end(), src.points.begin(), src.points.end());
  } else {
    for (const auto &p : src.points) {
      dst.points.push_back(TransformPoint(src_transform, p));
    }
  }

  // Merge face vertex indices (adjust by vertex offset)
  for (uint32_t idx : src.usdFaceVertexIndices) {
    dst.usdFaceVertexIndices.push_back(idx + vertex_offset);
  }

  // Merge face vertex counts
  dst.usdFaceVertexCounts.insert(dst.usdFaceVertexCounts.end(),
                                  src.usdFaceVertexCounts.begin(),
                                  src.usdFaceVertexCounts.end());

  // Merge triangulated indices if present
  if (!src.triangulatedFaceVertexIndices.empty()) {
    for (uint32_t idx : src.triangulatedFaceVertexIndices) {
      dst.triangulatedFaceVertexIndices.push_back(idx + vertex_offset);
    }
    dst.triangulatedFaceVertexCounts.insert(dst.triangulatedFaceVertexCounts.end(),
                                             src.triangulatedFaceVertexCounts.begin(),
                                             src.triangulatedFaceVertexCounts.end());
  }

  // Merge normals (transform direction if needed)
  if (!src.normals.empty()) {
    size_t src_normal_count = src.normals.vertex_count();

    // Ensure dst normals has same format
    if (dst.normals.empty()) {
      dst.normals = src.normals;
      if (!transform_is_identity) {
        // Transform the normals we just copied
        vec3 *normals_data = reinterpret_cast<vec3*>(dst.normals.data.data());
        for (size_t i = 0; i < src_normal_count; i++) {
          normals_data[i] = TransformNormal(src_transform, normals_data[i]);
        }
      }
    } else {
      // Append normals
      size_t old_size = dst.normals.data.size();
      dst.normals.data.resize(old_size + src.normals.data.size());

      if (transform_is_identity) {
        memcpy(dst.normals.data.data() + old_size, src.normals.data.data(), src.normals.data.size());
      } else {
        const vec3 *src_normals = reinterpret_cast<const vec3*>(src.normals.data.data());
        vec3 *dst_normals = reinterpret_cast<vec3*>(dst.normals.data.data() + old_size);
        for (size_t i = 0; i < src_normal_count; i++) {
          dst_normals[i] = TransformNormal(src_transform, src_normals[i]);
        }
      }
    }
  }

  // Merge texcoords (no transform needed)
  for (const auto &src_tc : src.texcoords) {
    uint32_t slot = src_tc.first;
    const auto &src_attr = src_tc.second;

    if (dst.texcoords.count(slot) == 0) {
      dst.texcoords[slot] = src_attr;
    } else {
      auto &dst_attr = dst.texcoords[slot];
      size_t old_size = dst_attr.data.size();
      dst_attr.data.resize(old_size + src_attr.data.size());
      memcpy(dst_attr.data.data() + old_size, src_attr.data.data(), src_attr.data.size());
    }
  }

  // Merge tangents (transform direction if needed)
  if (!src.tangents.empty()) {
    if (dst.tangents.empty()) {
      dst.tangents = src.tangents;
      if (!transform_is_identity) {
        vec3 *tangents_data = reinterpret_cast<vec3*>(dst.tangents.data.data());
        size_t count = dst.tangents.vertex_count();
        for (size_t i = 0; i < count; i++) {
          tangents_data[i] = TransformNormal(src_transform, tangents_data[i]);
        }
      }
    } else {
      size_t old_size = dst.tangents.data.size();
      size_t src_count = src.tangents.vertex_count();
      dst.tangents.data.resize(old_size + src.tangents.data.size());

      if (transform_is_identity) {
        memcpy(dst.tangents.data.data() + old_size, src.tangents.data.data(), src.tangents.data.size());
      } else {
        const vec3 *src_tangents = reinterpret_cast<const vec3*>(src.tangents.data.data());
        vec3 *dst_tangents = reinterpret_cast<vec3*>(dst.tangents.data.data() + old_size);
        for (size_t i = 0; i < src_count; i++) {
          dst_tangents[i] = TransformNormal(src_transform, src_tangents[i]);
        }
      }
    }
  }

  // Merge binormals (transform direction if needed)
  if (!src.binormals.empty()) {
    if (dst.binormals.empty()) {
      dst.binormals = src.binormals;
      if (!transform_is_identity) {
        vec3 *binormals_data = reinterpret_cast<vec3*>(dst.binormals.data.data());
        size_t count = dst.binormals.vertex_count();
        for (size_t i = 0; i < count; i++) {
          binormals_data[i] = TransformNormal(src_transform, binormals_data[i]);
        }
      }
    } else {
      size_t old_size = dst.binormals.data.size();
      size_t src_count = src.binormals.vertex_count();
      dst.binormals.data.resize(old_size + src.binormals.data.size());

      if (transform_is_identity) {
        memcpy(dst.binormals.data.data() + old_size, src.binormals.data.data(), src.binormals.data.size());
      } else {
        const vec3 *src_binormals = reinterpret_cast<const vec3*>(src.binormals.data.data());
        vec3 *dst_binormals = reinterpret_cast<vec3*>(dst.binormals.data.data() + old_size);
        for (size_t i = 0; i < src_count; i++) {
          dst_binormals[i] = TransformNormal(src_transform, src_binormals[i]);
        }
      }
    }
  }

  // Merge vertex colors
  if (!src.vertex_colors.empty()) {
    if (dst.vertex_colors.empty()) {
      dst.vertex_colors = src.vertex_colors;
    } else {
      size_t old_size = dst.vertex_colors.data.size();
      dst.vertex_colors.data.resize(old_size + src.vertex_colors.data.size());
      memcpy(dst.vertex_colors.data.data() + old_size, src.vertex_colors.data.data(), src.vertex_colors.data.size());
    }
  }

  // Merge vertex opacities
  if (!src.vertex_opacities.empty()) {
    if (dst.vertex_opacities.empty()) {
      dst.vertex_opacities = src.vertex_opacities;
    } else {
      size_t old_size = dst.vertex_opacities.data.size();
      dst.vertex_opacities.data.resize(old_size + src.vertex_opacities.data.size());
      memcpy(dst.vertex_opacities.data.data() + old_size, src.vertex_opacities.data.data(), src.vertex_opacities.data.size());
    }
  }

  return true;
}

bool RenderSceneConverter::MergeMeshesImpl(const RenderSceneConverterEnv &env) {
  if (!env.scene_config.merge_meshes) {
    return true;  // Merging disabled, nothing to do
  }

  DCOUT("MergeMeshesImpl: Starting mesh merge...");

  // Build a map from mesh to its node and global transform
  // Structure: mesh_index -> (node_ptr, global_matrix)
  struct MeshNodeInfo {
    Node *node{nullptr};
    value::matrix4d global_matrix;
    size_t mesh_index{0};
  };

  std::vector<MeshNodeInfo> mesh_node_infos;
  mesh_node_infos.resize(meshes.size());
  std::vector<std::vector<Node *>> mesh_nodes_by_id(meshes.size());

  // Helper to traverse nodes and collect mesh info
  std::function<void(Node &)> collectMeshNodes = [&](Node &node) {
    if (node.nodeType == NodeType::Mesh && node.id >= 0 &&
        size_t(node.id) < meshes.size()) {
      mesh_node_infos[size_t(node.id)].node = &node;
      mesh_node_infos[size_t(node.id)].global_matrix = node.global_matrix;
      mesh_node_infos[size_t(node.id)].mesh_index = size_t(node.id);
      mesh_nodes_by_id[size_t(node.id)].push_back(&node);
    }
    for (auto &child : node.children) {
      collectMeshNodes(child);
    }
  };

  for (auto &root : root_nodes) {
    collectMeshNodes(root);
  }

  // Group meshes by material_id
  // Only include meshes that are mergeable
  std::map<int, std::vector<size_t>> material_to_meshes;

  for (size_t i = 0; i < meshes.size(); i++) {
    const auto &mesh = meshes[i];
    if (!IsMeshMergeable(mesh)) {
      continue;
    }

    // Skip meshes that don't have a node (shouldn't happen but be safe)
    if (!mesh_node_infos[i].node) {
      continue;
    }

    material_to_meshes[mesh.material_id].push_back(i);
  }

  // For each material group with 2+ meshes, merge them
  std::vector<RenderMesh> merged_meshes;
  std::vector<std::pair<int32_t, std::vector<size_t>>> merged_groups;
  [[maybe_unused]] size_t merged_source_mesh_count{0};

  for (auto &kv : material_to_meshes) {
    int material_id = kv.first;
    auto &mesh_indices = kv.second;

    if (mesh_indices.size() < 2) {
      // Only one mesh with this material, no merging needed
      continue;
    }

    DCOUT("Merging " << mesh_indices.size() << " meshes with material_id=" << material_id);

    // Check if all meshes have the same global transform (when bake_transform is false)
    bool can_merge = true;
    if (!env.scene_config.merge_meshes_bake_transform) {
      const auto &first_matrix = mesh_node_infos[mesh_indices[0]].global_matrix;
      for (size_t i = 1; i < mesh_indices.size(); i++) {
        const auto &matrix = mesh_node_infos[mesh_indices[i]].global_matrix;
        // Compare matrices (with epsilon)
        bool same_transform = true;
        for (int r = 0; r < 4 && same_transform; r++) {
          for (int c = 0; c < 4 && same_transform; c++) {
            if (std::abs(first_matrix.m[r][c] - matrix.m[r][c]) > 1e-6) {
              same_transform = false;
            }
          }
        }
        if (!same_transform) {
          can_merge = false;
          break;
        }
      }
    }

    if (!can_merge) {
      DCOUT("Cannot merge meshes with material_id=" << material_id << " - different transforms");
      continue;
    }

    // Create merged mesh
    RenderMesh merged;
    merged.prim_name = "merged_material_" + std::to_string(material_id);
    merged.abs_path = "/merged/" + merged.prim_name;
    merged.display_name = "Merged mesh (material " + std::to_string(material_id) + ")";
    merged.material_id = material_id;

    // Copy properties from first mesh
    const auto &first_mesh = meshes[mesh_indices[0]];
    merged.doubleSided = first_mesh.doubleSided;
    merged.displayColor = first_mesh.displayColor;
    merged.displayOpacity = first_mesh.displayOpacity;
    merged.is_rightHanded = first_mesh.is_rightHanded;

    // If baking transforms, we transform all vertices to world space
    // The merged mesh will have identity transform

    std::vector<size_t> merged_sources;
    merged_sources.reserve(mesh_indices.size());

    for (size_t idx : mesh_indices) {
      const auto &src_mesh = meshes[idx];
      const auto &node_info = mesh_node_infos[idx];

      value::matrix4d relative_transform;
      if (env.scene_config.merge_meshes_bake_transform) {
        // Use world space transform
        relative_transform = node_info.global_matrix;
      } else {
        // All transforms should be the same (checked above)
        relative_transform = value::matrix4d::identity();
      }

      if (!MergeMeshData(src_mesh, relative_transform, merged)) {
        PUSH_WARN("Failed to merge mesh " + src_mesh.abs_path);
        continue;
      }

      merged_sources.push_back(idx);
    }

    if (merged_sources.size() < 2) {
      // Nothing useful to merge for this material group.
      continue;
    }

    merged_source_mesh_count += merged_sources.size();

    // The merged mesh is either in world space (if bake_transform) or
    // shares the transform of the first mesh
    merged.is_single_indexable = first_mesh.is_single_indexable;

    // Add merged mesh
    size_t new_mesh_index = meshes.size() + merged_meshes.size();
    merged_meshes.push_back(std::move(merged));

    merged_groups.emplace_back(static_cast<int32_t>(new_mesh_index),
                               std::move(merged_sources));
  }

  if (merged_meshes.empty()) {
    DCOUT("No meshes were merged");
    return true;
  }

  DCOUT("Created " << merged_meshes.size() << " merged meshes from "
                   << merged_source_mesh_count << " source meshes");

  // Add merged meshes to the mesh array
  for (auto &mm : merged_meshes) {
    meshes.push_back(std::move(mm));
  }

  // Update node references for merged sources only.
  // Keep only one node per merged mesh and invalidate the rest.
  for (const auto &group : merged_groups) {
    int32_t new_id = group.first;
    const auto &source_ids = group.second;
    bool first_assigned = false;

    for (size_t old_id : source_ids) {
      if (old_id >= mesh_nodes_by_id.size()) {
        continue;
      }

      for (Node *node_ptr : mesh_nodes_by_id[old_id]) {
        if (!node_ptr) {
          continue;
        }

        if (!first_assigned) {
          node_ptr->id = new_id;
          first_assigned = true;

          // If we baked transforms, reset the node's transform to identity
          if (env.scene_config.merge_meshes_bake_transform) {
            node_ptr->local_matrix = value::matrix4d::identity();
            node_ptr->global_matrix = value::matrix4d::identity();
          }
        } else {
          node_ptr->id = -1;
        }
      }
    }
  }

  return true;
}

}  // namespace tydra
}  // namespace tinyusdz
