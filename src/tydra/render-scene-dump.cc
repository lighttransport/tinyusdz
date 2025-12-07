// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Light Transport Entertainment Inc.
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
#include "value-pprint.hh"
#include "logger.hh"

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

template <typename T>
std::string DumpVertexAttributeDataImpl(const T *data, const size_t nbytes,
                                        const size_t stride_bytes,
                                        uint32_t indent) {
  size_t itemsize;

  if (stride_bytes != 0) {
    if ((nbytes % stride_bytes) != 0) {
      return fmt::format(
          "[Invalid VertexAttributeData. input bytes {} must be dividable by "
          "stride_bytes {}(Type {})]",
          nbytes, stride_bytes, value::TypeTraits<T>::type_name());
    }
    itemsize = stride_bytes;
  } else {
    if ((nbytes % sizeof(T)) != 0) {
      return fmt::format(
          "[Invalid VertexAttributeData. input bytes {} must be dividable by "
          "size {}(Type {})]",
          nbytes, sizeof(T), value::TypeTraits<T>::type_name());
    }
    itemsize = sizeof(T);
  }

  size_t nitems = nbytes / itemsize;
  std::string s;
  s += pprint::Indent(indent);
  s += value::print_strided_array_snipped<T>(
      reinterpret_cast<const uint8_t *>(data), stride_bytes, nitems);
  return s;
}

std::string DumpVertexAttributeData(const VertexAttribute &vattr,
                                    uint32_t indent) {
  // Ignore elementSize
#define APPLY_FUNC(__fmt, __basety)                            \
  if (__fmt == vattr.format) {                                 \
    return DumpVertexAttributeDataImpl(                        \
        reinterpret_cast<const __basety *>(vattr.data.data()), \
        vattr.data.size(), vattr.stride, indent);              \
  }

  APPLY_FUNC(VertexAttributeFormat::Bool, uint8_t)
  APPLY_FUNC(VertexAttributeFormat::Char, char)
  APPLY_FUNC(VertexAttributeFormat::Char2, value::char2)
  APPLY_FUNC(VertexAttributeFormat::Char3, value::char3)
  APPLY_FUNC(VertexAttributeFormat::Char4, value::char4)
  APPLY_FUNC(VertexAttributeFormat::Byte, uint8_t)
  APPLY_FUNC(VertexAttributeFormat::Byte2, value::uchar2)
  APPLY_FUNC(VertexAttributeFormat::Byte3, value::uchar3)
  APPLY_FUNC(VertexAttributeFormat::Byte4, value::uchar4)
  APPLY_FUNC(VertexAttributeFormat::Short, int16_t)
  APPLY_FUNC(VertexAttributeFormat::Short2, value::short2)
  APPLY_FUNC(VertexAttributeFormat::Short3, value::short3)
  APPLY_FUNC(VertexAttributeFormat::Short4, value::short4)
  APPLY_FUNC(VertexAttributeFormat::Ushort, uint16_t)
  APPLY_FUNC(VertexAttributeFormat::Ushort2, value::ushort2)
  APPLY_FUNC(VertexAttributeFormat::Ushort3, value::ushort3)
  APPLY_FUNC(VertexAttributeFormat::Ushort4, value::ushort4)
  APPLY_FUNC(VertexAttributeFormat::Half, value::half)
  APPLY_FUNC(VertexAttributeFormat::Half2, value::half2)
  APPLY_FUNC(VertexAttributeFormat::Half3, value::half3)
  APPLY_FUNC(VertexAttributeFormat::Half4, value::half4)
  APPLY_FUNC(VertexAttributeFormat::Float, float)
  APPLY_FUNC(VertexAttributeFormat::Vec2, value::float2)
  APPLY_FUNC(VertexAttributeFormat::Vec3, value::float3)
  APPLY_FUNC(VertexAttributeFormat::Vec4, value::float4)
  APPLY_FUNC(VertexAttributeFormat::Int, int)
  APPLY_FUNC(VertexAttributeFormat::Ivec2, value::int2)
  APPLY_FUNC(VertexAttributeFormat::Ivec3, value::int3)
  APPLY_FUNC(VertexAttributeFormat::Ivec4, value::int4)
  APPLY_FUNC(VertexAttributeFormat::Uint, uint32_t)
  APPLY_FUNC(VertexAttributeFormat::Uvec2, value::half)
  APPLY_FUNC(VertexAttributeFormat::Uvec3, value::half)
  APPLY_FUNC(VertexAttributeFormat::Uvec4, value::half)
  APPLY_FUNC(VertexAttributeFormat::Double, double)
  APPLY_FUNC(VertexAttributeFormat::Dvec2, value::double2)
  APPLY_FUNC(VertexAttributeFormat::Dvec3, value::double2)
  APPLY_FUNC(VertexAttributeFormat::Dvec4, value::double2)
  APPLY_FUNC(VertexAttributeFormat::Mat2, value::matrix2f)
  APPLY_FUNC(VertexAttributeFormat::Mat3, value::matrix3f)
  APPLY_FUNC(VertexAttributeFormat::Mat4, value::matrix4f)
  APPLY_FUNC(VertexAttributeFormat::Dmat2, value::matrix2d)
  APPLY_FUNC(VertexAttributeFormat::Dmat3, value::matrix3d)
  APPLY_FUNC(VertexAttributeFormat::Dmat4, value::matrix4d)
  else {
    return fmt::format("[InternalError. Invalid VertexAttributeFormat: Id{}]",
                       int(vattr.format));
  }

#undef APPLY_FUNC
}

std::string DumpVertexAttribute(const VertexAttribute &vattr, uint32_t indent) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "count " << vattr.get_data().size() << "\n";
  ss << pprint::Indent(indent) << "format " << quote(to_string(vattr.format))
     << "\n";
  ss << pprint::Indent(indent) << "variability "
     << quote(to_string(vattr.variability)) << "\n";
  ss << pprint::Indent(indent) << "elementSize " << vattr.elementSize << "\n";
  ss << pprint::Indent(indent) << "value "
     << quote(DumpVertexAttributeData(vattr, /* indent */ 0)) << "\n";
  if (vattr.indices.size()) {
    ss << pprint::Indent(indent) << "indices "
       << quote(value::print_array_snipped(vattr.indices)) << "\n";
  }

  return ss.str();
}


std::string DumpNode(const Node &node, uint32_t indent) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "node {\n";

  ss << pprint::Indent(indent + 1) << "category " << quote(to_string(node.category))
     << "\n";
  ss << pprint::Indent(indent + 1) << "type " << quote(to_string(node.nodeType))
     << "\n";

  ss << pprint::Indent(indent + 1) << "id " << node.id << "\n";

  ss << pprint::Indent(indent + 1) << "prim_name " << quote(node.prim_name)
     << "\n";
  ss << pprint::Indent(indent + 1) << "abs_path " << quote(node.abs_path)
     << "\n";
  ss << pprint::Indent(indent + 1) << "display_name "
     << quote(node.display_name) << "\n";
  ss << pprint::Indent(indent + 1) << "local_matrix "
     << quote(tinyusdz::to_string(node.local_matrix)) << "\n";
  ss << pprint::Indent(indent + 1) << "global_matrix "
     << quote(tinyusdz::to_string(node.global_matrix)) << "\n";

  if (node.children.size()) {
    ss << pprint::Indent(indent + 1) << "children {\n";
    for (const auto &child : node.children) {
      ss << DumpNode(child, indent + 1);
    }
    ss << pprint::Indent(indent + 1) << "}\n";
  }

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

void DumpMaterialSubset(std::stringstream &ss, const MaterialSubset &msubset,
                        uint32_t indent) {
  ss << pprint::Indent(indent) << "material_subset {\n";
  ss << pprint::Indent(indent + 1) << "material_id " << msubset.material_id
     << "\n";
  ss << pprint::Indent(indent + 1) << "indices "
     << quote(value::print_array_snipped(msubset.indices())) << "\n";
  ss << pprint::Indent(indent) << "}\n";
}

std::string DumpMesh(const RenderMesh &mesh, uint32_t indent) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "mesh {\n";

  ss << pprint::Indent(indent + 1) << "prim_name " << quote(mesh.prim_name)
     << "\n";
  ss << pprint::Indent(indent + 1) << "abs_path " << quote(mesh.abs_path)
     << "\n";
  ss << pprint::Indent(indent + 1) << "display_name "
     << quote(mesh.display_name) << "\n";
  ss << pprint::Indent(indent + 1) << "num_points "
     << std::to_string(mesh.points.size()) << "\n";
  ss << pprint::Indent(indent + 1) << "points \""
     << value::print_array_snipped(mesh.points) << "\"\n";
  ss << pprint::Indent(indent + 1) << "num_faceVertexCounts "
     << std::to_string(mesh.faceVertexCounts().size()) << "\n";
  ss << pprint::Indent(indent + 1) << "faceVertexCounts \""
     << value::print_array_snipped(mesh.faceVertexCounts()) << "\"\n";
  ss << pprint::Indent(indent + 1) << "num_faceVertexIndices "
     << std::to_string(mesh.faceVertexIndices().size()) << "\n";
  ss << pprint::Indent(indent + 1) << "faceVertexIndices \""
     << value::print_array_snipped(mesh.faceVertexIndices()) << "\"\n";
  ss << pprint::Indent(indent + 1) << "materialId "
     << std::to_string(mesh.material_id) << "\n";
  ss << pprint::Indent(indent + 1) << "normals {\n"
     << DumpVertexAttribute(mesh.normals, indent + 2) << "\n";
  ss << pprint::Indent(indent + 1) << "}\n";
  ss << pprint::Indent(indent + 1) << "num_texcoordSlots "
     << std::to_string(mesh.texcoords.size()) << "\n";
  for (const auto &uvs : mesh.texcoords) {
    ss << pprint::Indent(indent + 1) << "texcoords_"
       << std::to_string(uvs.first) << " {\n"
       << DumpVertexAttribute(uvs.second, indent + 2) << "\n";
    ss << pprint::Indent(indent + 1) << "}\n";
  }
  if (mesh.binormals.data.size()) {
    ss << pprint::Indent(indent + 1) << "binormals {\n"
       << DumpVertexAttribute(mesh.binormals, indent + 2) << "\n";
    ss << pprint::Indent(indent + 1) << "}\n";
  }
  if (mesh.tangents.data.size()) {
    ss << pprint::Indent(indent + 1) << "tangents {\n"
       << DumpVertexAttribute(mesh.tangents, indent + 2) << "\n";
    ss << pprint::Indent(indent + 1) << "}\n";
  }

  ss << pprint::Indent(indent + 1) << "skel_id " << mesh.skel_id << "\n";

  if (mesh.joint_and_weights.jointIndices.size()) {
    ss << pprint::Indent(indent + 1) << "skin {\n";
    ss << pprint::Indent(indent + 2) << "geomBindTransform "
       << quote(tinyusdz::to_string(mesh.joint_and_weights.geomBindTransform))
       << "\n";
    ss << pprint::Indent(indent + 2) << "elementSize "
       << mesh.joint_and_weights.elementSize << "\n";
    ss << pprint::Indent(indent + 2) << "jointIndices "
       << quote(value::print_array_snipped(mesh.joint_and_weights.jointIndices))
       << "\n";
    ss << pprint::Indent(indent + 2) << "jointWeights "
       << quote(value::print_array_snipped(mesh.joint_and_weights.jointWeights))
       << "\n";
    ss << pprint::Indent(indent + 1) << "}\n";
  }
  if (mesh.targets.size()) {
    ss << pprint::Indent(indent + 1) << "shapeTargets {\n";

    for (const auto &target : mesh.targets) {
      ss << pprint::Indent(indent + 2) << target.first << " {\n";
      ss << pprint::Indent(indent + 3) << "prim_name " << quote(target.second.prim_name) << "\n";
      ss << pprint::Indent(indent + 3) << "abs_path " << quote(target.second.abs_path) << "\n";
      ss << pprint::Indent(indent + 3) << "display_name " << quote(target.second.display_name) << "\n";
      ss << pprint::Indent(indent + 3) << "pointIndices " << quote(value::print_array_snipped(target.second.pointIndices)) << "\n";
      ss << pprint::Indent(indent + 3) << "pointOffsets " << quote(value::print_array_snipped(target.second.pointOffsets)) << "\n";
      ss << pprint::Indent(indent + 3) << "normalOffsets " << quote(value::print_array_snipped(target.second.normalOffsets)) << "\n";
      ss << pprint::Indent(indent + 2) << "}\n";
    }

    ss << pprint::Indent(indent + 1) << "}\n";

  }
  if (mesh.material_subsetMap.size()) {
    ss << pprint::Indent(indent + 1) << "material_subsets {\n";
    for (const auto &msubset : mesh.material_subsetMap) {
      DumpMaterialSubset(ss, msubset.second, indent + 2);
    }
    ss << pprint::Indent(indent + 1) << "}\n";
  }

  // TODO: primvars

  ss << "\n";

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

namespace detail {

void DumpSkelNode(std::stringstream &ss, const SkelNode &node, uint32_t indent) {

  ss << pprint::Indent(indent) << node.joint_name << " {\n";

  ss << pprint::Indent(indent + 1) << "joint_path " << quote(node.joint_path) << "\n";
  ss << pprint::Indent(indent + 1) << "joint_id " << node.joint_id << "\n";
  ss << pprint::Indent(indent + 1) << "bind_transform " << quote(tinyusdz::to_string(node.bind_transform)) << "\n";
  ss << pprint::Indent(indent + 1) << "rest_transform " << quote(tinyusdz::to_string(node.rest_transform)) << "\n";

  if (node.children.size()) {
    ss << pprint::Indent(indent + 1) << "children {\n";
    for (const auto &child : node.children) {
      DumpSkelNode(ss, child, indent + 2);
    }
    ss << pprint::Indent(indent + 1) << "}\n";
  }

  ss << pprint::Indent(indent) << "}\n";
}


} // namespace detail

std::string DumpSkeleton(const SkelHierarchy &skel, uint32_t indent) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "skeleton {\n";

  ss << pprint::Indent(indent + 1) << "name " << quote(skel.prim_name) << "\n";
  ss << pprint::Indent(indent + 1) << "abs_path " << quote(skel.abs_path)
     << "\n";
  ss << pprint::Indent(indent + 1) << "anim_id " << skel.anim_id
     << "\n";
  ss << pprint::Indent(indent + 1) << "display_name "
     << quote(skel.display_name) << "\n";

  detail::DumpSkelNode(ss, skel.root_node, indent + 1);

  ss << "\n";

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

namespace detail {

#if 0 // unused
template<typename T>
std::string PrintAnimationSamples(const std::vector<AnimationSample<T>> &samples) {
  std::stringstream ss;

  ss << "[";
  for (size_t i = 0; i < samples.size(); i++) {
    if (i > 0) {
      ss << ", ";
    }

    ss << "(" << samples[i].t << ", " << samples[i].value << ")";
  }
  ss << "]";

  return ss.str();
}
#endif

// void DumpAnimChannel(std::stringstream &ss, const std::string &name, const std::map<AnimationChannel::ChannelType, AnimationChannel> &channels, uint32_t indent) {
// 
//   ss << pprint::Indent(indent) << name << " {\n";
// 
//   for (const auto &channel : channels) {
//     if (channel.first == AnimationChannel::ChannelType::Translation) {
//       ss << pprint::Indent(indent + 1) << "translations " << quote(detail::PrintAnimationSamples(channel.second.translations.samples)) << "\n";
//     } else if (channel.first == AnimationChannel::ChannelType::Rotation) {
//       ss << pprint::Indent(indent + 1) << "rotations " << quote(detail::PrintAnimationSamples(channel.second.rotations.samples)) << "\n";
//     } else if (channel.first == AnimationChannel::ChannelType::Scale) {
//       ss << pprint::Indent(indent + 1) << "scales " << quote(detail::PrintAnimationSamples(channel.second.scales.samples)) << "\n";
//     }
//   }
// 
//   ss << pprint::Indent(indent) << "}\n";
// }


} // namespace detail

std::string DumpAnimation(const AnimationClip &anim, uint32_t indent) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "animation {\n";

  ss << pprint::Indent(indent + 1) << "name " << quote(anim.name) << "\n";
  ss << pprint::Indent(indent + 1) << "prim_name " << quote(anim.prim_name) << "\n";
  ss << pprint::Indent(indent + 1) << "abs_path " << quote(anim.abs_path) << "\n";
  ss << pprint::Indent(indent + 1) << "display_name " << quote(anim.display_name) << "\n";
  ss << pprint::Indent(indent + 1) << "duration " << anim.duration << "\n";
  ss << pprint::Indent(indent + 1) << "num_samplers " << anim.samplers.size() << "\n";
  ss << pprint::Indent(indent + 1) << "num_channels " << anim.channels.size() << "\n";

  // Dump channels
  for (size_t i = 0; i < anim.channels.size(); i++) {
    const auto &ch = anim.channels[i];
    ss << pprint::Indent(indent + 1) << "channel[" << i << "] {\n";
    ss << pprint::Indent(indent + 2) << "target_node: " << ch.target_node << "\n";
    ss << pprint::Indent(indent + 2) << "sampler: " << ch.sampler << "\n";
    ss << pprint::Indent(indent + 2) << "path: ";
    switch (ch.path) {
      case AnimationPath::Translation: ss << "Translation"; break;
      case AnimationPath::Rotation: ss << "Rotation"; break;
      case AnimationPath::Scale: ss << "Scale"; break;
      case AnimationPath::Weights: ss << "Weights"; break;
    }
    ss << "\n";
    ss << pprint::Indent(indent + 1) << "}\n";
  }

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}


std::string DumpCamera(const RenderCamera &camera, uint32_t indent) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "camera {\n";

  ss << pprint::Indent(indent + 1) << "name " << quote(camera.name) << "\n";
  ss << pprint::Indent(indent + 1) << "abs_path " << quote(camera.abs_path)
     << "\n";
  ss << pprint::Indent(indent + 1) << "display_name "
     << quote(camera.display_name) << "\n";
  ss << pprint::Indent(indent + 1) << "shutterOpen "
     << std::to_string(camera.shutterOpen) << "\n";
  ss << pprint::Indent(indent + 1) << "shutterClose "
     << std::to_string(camera.shutterClose) << "\n";

  ss << "\n";

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

std::string DumpOpenPBRSurface(const OpenPBRSurfaceShader &shader,
                               uint32_t indent) {
  std::stringstream ss;

  ss << "OpenPBRSurfaceShader {\n";

  // Base layer
  ss << pprint::Indent(indent + 1) << "base_weight = ";
  if (shader.base_weight.is_texture()) {
    ss << "texture_id[" << shader.base_weight.texture_id << "]";
  } else {
    ss << shader.base_weight.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "base_color = ";
  if (shader.base_color.is_texture()) {
    ss << "texture_id[" << shader.base_color.texture_id << "]";
  } else {
    ss << shader.base_color.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "base_roughness = ";
  if (shader.base_roughness.is_texture()) {
    ss << "texture_id[" << shader.base_roughness.texture_id << "]";
  } else {
    ss << shader.base_roughness.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "base_metalness = ";
  if (shader.base_metalness.is_texture()) {
    ss << "texture_id[" << shader.base_metalness.texture_id << "]";
  } else {
    ss << shader.base_metalness.value;
  }
  ss << "\n";

  // Specular layer
  ss << pprint::Indent(indent + 1) << "specular_weight = ";
  if (shader.specular_weight.is_texture()) {
    ss << "texture_id[" << shader.specular_weight.texture_id << "]";
  } else {
    ss << shader.specular_weight.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "specular_color = ";
  if (shader.specular_color.is_texture()) {
    ss << "texture_id[" << shader.specular_color.texture_id << "]";
  } else {
    ss << shader.specular_color.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "specular_roughness = ";
  if (shader.specular_roughness.is_texture()) {
    ss << "texture_id[" << shader.specular_roughness.texture_id << "]";
  } else {
    ss << shader.specular_roughness.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "specular_ior = ";
  if (shader.specular_ior.is_texture()) {
    ss << "texture_id[" << shader.specular_ior.texture_id << "]";
  } else {
    ss << shader.specular_ior.value;
  }
  ss << "\n";

  // Coat layer
  ss << pprint::Indent(indent + 1) << "coat_weight = ";
  if (shader.coat_weight.is_texture()) {
    ss << "texture_id[" << shader.coat_weight.texture_id << "]";
  } else {
    ss << shader.coat_weight.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "coat_color = ";
  if (shader.coat_color.is_texture()) {
    ss << "texture_id[" << shader.coat_color.texture_id << "]";
  } else {
    ss << shader.coat_color.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "coat_roughness = ";
  if (shader.coat_roughness.is_texture()) {
    ss << "texture_id[" << shader.coat_roughness.texture_id << "]";
  } else {
    ss << shader.coat_roughness.value;
  }
  ss << "\n";

  // Emission
  ss << pprint::Indent(indent + 1) << "emission_luminance = ";
  if (shader.emission_luminance.is_texture()) {
    ss << "texture_id[" << shader.emission_luminance.texture_id << "]";
  } else {
    ss << shader.emission_luminance.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "emission_color = ";
  if (shader.emission_color.is_texture()) {
    ss << "texture_id[" << shader.emission_color.texture_id << "]";
  } else {
    ss << shader.emission_color.value;
  }
  ss << "\n";

  // Transmission
  ss << pprint::Indent(indent + 1) << "transmission_weight = ";
  if (shader.transmission_weight.is_texture()) {
    ss << "texture_id[" << shader.transmission_weight.texture_id << "]";
  } else {
    ss << shader.transmission_weight.value;
  }
  ss << "\n";

  // Subsurface
  ss << pprint::Indent(indent + 1) << "subsurface_weight = ";
  if (shader.subsurface_weight.is_texture()) {
    ss << "texture_id[" << shader.subsurface_weight.texture_id << "]";
  } else {
    ss << shader.subsurface_weight.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "subsurface_color = ";
  if (shader.subsurface_color.is_texture()) {
    ss << "texture_id[" << shader.subsurface_color.texture_id << "]";
  } else {
    ss << shader.subsurface_color.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent) << "}";

  return ss.str();
}

std::string DumpPreviewSurface(const PreviewSurfaceShader &shader,
                               uint32_t indent) {
  std::stringstream ss;

  ss << "PreviewSurfaceShader {\n";

  ss << pprint::Indent(indent + 1)
     << "useSpecularWorkflow = " << std::to_string(shader.useSpecularWorkflow)
     << "\n";

  ss << pprint::Indent(indent + 1) << "diffuseColor = ";
  if (shader.diffuseColor.is_texture()) {
    ss << "texture_id[" << shader.diffuseColor.texture_id << "]";
  } else {
    ss << shader.diffuseColor.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "metallic = ";
  if (shader.metallic.is_texture()) {
    ss << "texture_id[" << shader.metallic.texture_id << "]";
  } else {
    ss << shader.metallic.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "roughness = ";
  if (shader.roughness.is_texture()) {
    ss << "texture_id[" << shader.roughness.texture_id << "]";
  } else {
    ss << shader.roughness.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "ior = ";
  if (shader.ior.is_texture()) {
    ss << "texture_id[" << shader.ior.texture_id << "]";
  } else {
    ss << shader.ior.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "clearcoat = ";
  if (shader.clearcoat.is_texture()) {
    ss << "texture_id[" << shader.clearcoat.texture_id << "]";
  } else {
    ss << shader.clearcoat.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "clearcoatRoughness = ";
  if (shader.clearcoatRoughness.is_texture()) {
    ss << "texture_id[" << shader.clearcoatRoughness.texture_id << "]";
  } else {
    ss << shader.clearcoatRoughness.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "opacity = ";
  if (shader.opacity.is_texture()) {
    ss << "texture_id[" << shader.opacity.texture_id << "]";
  } else {
    ss << shader.opacity.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "opacityThreshold = ";
  if (shader.opacityThreshold.is_texture()) {
    ss << "texture_id[" << shader.opacityThreshold.texture_id << "]";
  } else {
    ss << shader.opacityThreshold.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "normal = ";
  if (shader.normal.is_texture()) {
    ss << "texture_id[" << shader.normal.texture_id << "]";
  } else {
    ss << shader.normal.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "displacement = ";
  if (shader.displacement.is_texture()) {
    ss << "texture_id[" << shader.displacement.texture_id << "]";
  } else {
    ss << shader.displacement.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "occlusion = ";
  if (shader.occlusion.is_texture()) {
    ss << "texture_id[" << shader.occlusion.texture_id << "]";
  } else {
    ss << shader.occlusion.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent) << "}";

  return ss.str();
}

std::string DumpMaterial(const RenderMaterial &material, uint32_t indent) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "material {\n";

  ss << pprint::Indent(indent + 1) << "name " << quote(material.name) << "\n";
  ss << pprint::Indent(indent + 1) << "abs_path " << quote(material.abs_path)
     << "\n";
  ss << pprint::Indent(indent + 1) << "display_name "
     << quote(material.display_name) << "\n";

  ss << pprint::Indent(indent + 1) << "surfaceShader = ";
  if (material.surfaceShader.has_value()) {
    ss << DumpPreviewSurface(*material.surfaceShader, indent + 1);
  } else {
    ss << "null";
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "openPBRShader = ";
  if (material.openPBRShader.has_value()) {
    ss << DumpOpenPBRSurface(*material.openPBRShader, indent + 1);
  } else {
    ss << "null";
  }
  ss << "\n";

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

std::string DumpUVTexture(const UVTexture &texture, uint32_t indent) {
  std::stringstream ss;

  // TODO
  ss << "UVTexture {\n";
  ss << pprint::Indent(indent + 1) << "primvar_name " << texture.varname_uv
     << "\n";
  ss << pprint::Indent(indent + 1) << "connectedOutputChannel ";
     ss << to_string(texture.connectedOutputChannel) << "\n";

  ss << pprint::Indent(indent + 1) << "authoredOutputChannels ";

  for (const auto &c : texture.authoredOutputChannels) {
     ss << to_string(c) << " ";
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "bias " << texture.bias << "\n";
  ss << pprint::Indent(indent + 1) << "scale " << texture.scale << "\n";
  ss << pprint::Indent(indent + 1) << "wrapS " << to_string(texture.wrapS)
     << "\n";
  ss << pprint::Indent(indent + 1) << "wrapT " << to_string(texture.wrapT)
     << "\n";
  ss << pprint::Indent(indent + 1) << "fallback_uv " << texture.fallback_uv
     << "\n";
  ss << pprint::Indent(indent + 1) << "textureImageID "
     << std::to_string(texture.texture_image_id) << "\n";
  ss << pprint::Indent(indent + 1) << "has UsdTransform2d "
     << std::to_string(texture.has_transform2d) << "\n";
  if (texture.has_transform2d) {
    ss << pprint::Indent(indent + 2) << "rotation " << texture.tx_rotation
       << "\n";
    ss << pprint::Indent(indent + 2) << "scale " << texture.tx_scale << "\n";
    ss << pprint::Indent(indent + 2) << "translation " << texture.tx_translation
       << "\n";
    ss << pprint::Indent(indent + 2) << "computed_transform "
       << texture.transform << "\n";
  }

  ss << "\n";

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

std::string DumpImage(const TextureImage &image, uint32_t indent) {
  std::stringstream ss;

  ss << "TextureImage {\n";
  ss << pprint::Indent(indent + 1) << "asset_identifier \""
     << image.asset_identifier << "\"\n";
  ss << pprint::Indent(indent + 1) << "decoded \""
     << image.decoded << "\"\n";
  ss << pprint::Indent(indent + 1) << "channels "
     << std::to_string(image.channels) << "\n";
  ss << pprint::Indent(indent + 1) << "width " << std::to_string(image.width)
     << "\n";
  ss << pprint::Indent(indent + 1) << "height " << std::to_string(image.height)
     << "\n";
  ss << pprint::Indent(indent + 1) << "miplevel "
     << std::to_string(image.miplevel) << "\n";
  ss << pprint::Indent(indent + 1) << "colorSpace "
     << to_string(image.colorSpace) << "\n";
  ss << pprint::Indent(indent + 1) << "usdColorSpace "
     << to_string(image.usdColorSpace) << "\n";
  ss << pprint::Indent(indent + 1) << "bufferID "
     << std::to_string(image.buffer_id) << "\n";

  ss << "\n";

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

std::string DumpBuffer(const BufferData &buffer, uint32_t indent) {
  std::stringstream ss;

  ss << "Buffer {\n";
  ss << pprint::Indent(indent + 1) << "bytes " << buffer.data.size() << "\n";
  ss << pprint::Indent(indent + 1) << "componentType "
     << to_string(buffer.componentType) << "\n";

  ss << "\n";

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

}  // namespace

std::string DumpRenderScene(const RenderScene &scene,
                            const std::string &format) {
  std::stringstream ss;

  if (format == "json") {
    // TODO:
    // Currently kdl only.
    ss << "// `json` format is not supported yet. Use KDL format\n";
  }

  ss << "title " << quote(scene.usd_filename) << "\n";
  ss << "default_root_node " << scene.default_root_node << "\n";
  ss << "// # of Root Nodes : " << scene.nodes.size() << "\n";
  ss << "// # of Meshes : " << scene.meshes.size() << "\n";
  ss << "// # of Skeletons : " << scene.skeletons.size() << "\n";
  ss << "// # of Animations : " << scene.animations.size() << "\n";
  ss << "// # of Cameras : " << scene.cameras.size() << "\n";
  ss << "// # of Materials : " << scene.materials.size() << "\n";
  ss << "// # of UVTextures : " << scene.textures.size() << "\n";
  ss << "// # of TextureImages : " << scene.images.size() << "\n";
  ss << "// # of Buffers : " << scene.buffers.size() << "\n";

  ss << "\n";

  ss << "nodes {\n";
  for (size_t i = 0; i < scene.nodes.size(); i++) {
    ss << DumpNode(scene.nodes[i], 1);
  }
  ss << "}\n";

  ss << "meshes {\n";
  for (size_t i = 0; i < scene.meshes.size(); i++) {
    ss << "[" << i << "] " << DumpMesh(scene.meshes[i], 1);
  }
  ss << "}\n";

  ss << "skeletons {\n";
  for (size_t i = 0; i < scene.skeletons.size(); i++) {
    ss << "[" << i << "] " << DumpSkeleton(scene.skeletons[i], 1);
  }
  ss << "}\n";

  ss << "animations {\n";
  for (size_t i = 0; i < scene.animations.size(); i++) {
    ss << "[" << i << "] " << DumpAnimation(scene.animations[i], 1);
  }
  ss << "}\n";

  ss << "cameras {\n";
  for (size_t i = 0; i < scene.cameras.size(); i++) {
    ss << "[" << i << "] " << DumpCamera(scene.cameras[i], 1);
  }
  ss << "}\n";

  ss << "\n";
  ss << "materials {\n";
  for (size_t i = 0; i < scene.materials.size(); i++) {
    ss << "[" << i << "] " << DumpMaterial(scene.materials[i], 1);
  }
  ss << "}\n";

  ss << "\n";
  ss << "textures {\n";
  for (size_t i = 0; i < scene.textures.size(); i++) {
    ss << "[" << i << "] " << DumpUVTexture(scene.textures[i], 1);
  }
  ss << "}\n";

  ss << "\n";
  ss << "images {\n";
  for (size_t i = 0; i < scene.images.size(); i++) {
    ss << "[" << i << "] " << DumpImage(scene.images[i], 1);
  }
  ss << "}\n";

  ss << "\n";
  ss << "buffers {\n";
  for (size_t i = 0; i < scene.buffers.size(); i++) {
    ss << "[" << i << "] " << DumpBuffer(scene.buffers[i], 1);
  }
  ss << "}\n";

  // ss << "TODO: AnimationChannel, ...\n";

  return ss.str();
}

}  // namespace tydra
}  // namespace tinyusdz
