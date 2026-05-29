// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.

///
/// @file render-index-builder.cc
/// @brief Vertex index building functions for RenderSceneConverter
///

#include "render-data.hh"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <unordered_map>

#include "common-utils.hh"
#include "safe-arithmetic.hh"
#include "tiny-format.hh"
#include "value-types.hh"

#include "common-macros.inc"

namespace tinyusdz {
namespace tydra {

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
      // Overflow-safe allocation sizing (see render-data-mesh.cc for the
      // rationale: elementSize/numPoints products can overflow size_t).
      size_t tmp_count;
      if (!safe::mul(size_t(numPoints), size_t(elementSize), &tmp_count)) {
        PUSH_ERROR_AND_RETURN("Skin weights buffer size overflow.");
      }
      std::vector<int> tmp_indices(tmp_count);
      std::vector<float> tmp_weights(tmp_count);
      for (size_t i = 0; i < out_point_indices.size(); i++) {
        // Compute offsets in size_t. `elementSize * out_point_indices[i]` would
        // multiply two uint32_t in 32-bit arithmetic and could wrap, bypassing
        // the bounds check while the 64-bit access reads OOB. out_indices[i] is
        // bounded by numPoints, so dst_off stays within tmp_count.
        size_t src_off;
        if (!safe::mul(size_t(elementSize), size_t(out_point_indices[i]),
                       &src_off)) {
          PUSH_ERROR_AND_RETURN("Skin index offset overflow.");
        }
        const size_t dst_off = size_t(elementSize) * size_t(out_indices[i]);

        if ((src_off >= mesh.joint_and_weights.jointIndices.size()) ||
            ((mesh.joint_and_weights.jointIndices.size() - src_off) <
             size_t(elementSize))) {
          PUSH_ERROR_AND_RETURN(
              "Internal error. point index exceeds jointIndices.size.");
        }
        for (size_t k = 0; k < elementSize; k++) {
          tmp_indices[dst_off + k] =
              mesh.joint_and_weights.jointIndices[src_off + k];
        }

        if ((src_off >= mesh.joint_and_weights.jointWeights.size()) ||
            ((mesh.joint_and_weights.jointWeights.size() - src_off) <
             size_t(elementSize))) {
          PUSH_ERROR_AND_RETURN(
              "Internal error. point index exceeds jointWeights.size.");
        }

        for (size_t k = 0; k < elementSize; k++) {
          tmp_weights[dst_off + k] =
              mesh.joint_and_weights.jointWeights[src_off + k];
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
      // Overflow-safe allocation sizing. numPoints == fvIndices.size() here, so
      // dst_off (elementSize * i, i < fvIndices.size()) stays within tmp_count.
      size_t tmp_count;
      if (!safe::mul(size_t(numPoints), size_t(elementSize), &tmp_count)) {
        PUSH_ERROR_AND_RETURN("Skin weights buffer size overflow.");
      }
      std::vector<int> tmp_indices(tmp_count);
      std::vector<float> tmp_weights(tmp_count);
      for (size_t i = 0; i < fvIndices.size(); i++) {
        // Compute the source base offset in size_t: `elementSize * fvIndices[i]`
        // is a 32-bit product that can wrap, bypassing the bounds check while
        // the 64-bit access below reads OOB. Validate the full span.
        size_t src_off;
        if (!safe::mul(size_t(elementSize), size_t(fvIndices[i]), &src_off)) {
          PUSH_ERROR_AND_RETURN("Skin index offset overflow.");
        }
        const size_t dst_off = size_t(elementSize) * i;

        if ((src_off >= mesh.joint_and_weights.jointIndices.size()) ||
            ((mesh.joint_and_weights.jointIndices.size() - src_off) <
             size_t(elementSize))) {
          PUSH_ERROR_AND_RETURN(
              "Internal error. point index exceeds jointIndices.size.");
        }
        for (size_t k = 0; k < elementSize; k++) {
          tmp_indices[dst_off + k] =
              mesh.joint_and_weights.jointIndices[src_off + k];
        }

        if ((src_off >= mesh.joint_and_weights.jointWeights.size()) ||
            ((mesh.joint_and_weights.jointWeights.size() - src_off) <
             size_t(elementSize))) {
          PUSH_ERROR_AND_RETURN(
              "Internal error. point index exceeds jointWeights.size.");
        }

        for (size_t k = 0; k < elementSize; k++) {
          tmp_weights[dst_off + k] =
              mesh.joint_and_weights.jointWeights[src_off + k];
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

}  // namespace tydra
}  // namespace tinyusdz
