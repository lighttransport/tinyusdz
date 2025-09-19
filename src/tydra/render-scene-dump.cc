// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.

#include "render-scene-dump.hh"
#include "render-data.hh"

#include <sstream>
#include <iomanip>
#include <algorithm>

#include "value-pprint.hh"
#include "pprinter.hh"
#include "tiny-format.hh"

namespace tinyusdz {
namespace tydra {

namespace {

inline std::string Indent(uint32_t n) {
  return std::string(n * 2, ' ');
}

template <typename T>
std::string DumpVertexAttributeDataImpl(const T* data, const size_t nbytes,
                                       uint32_t offset, uint32_t count) {
  std::stringstream ss;
  
  const size_t n = nbytes / sizeof(T);
  const size_t start_idx = std::min(size_t(offset), n);
  const size_t end_idx = (count == ~0u) ? n : std::min(size_t(offset + count), n);
  
  ss << "[";
  for (size_t i = start_idx; i < end_idx; i++) {
    if (i > start_idx) ss << ", ";
    if ((i - start_idx) > 16) {
      ss << "...";
      break;
    }
    ss << data[i];
  }
  ss << "]";
  
  return ss.str();
}

template <typename T>
void PrintAnimationSamples(std::stringstream& ss, const AnimationSampler<T>& sampler,
                          uint32_t indent) {
  if (sampler.static_value) {
    ss << Indent(indent) << "static_value: " << sampler.static_value.value() << "\n";
  }
  
  ss << Indent(indent) << "samples: " << sampler.samples.size() << "\n";
  if (!sampler.samples.empty()) {
    ss << Indent(indent) << "  first sample: t=" << sampler.samples[0].t 
       << ", value=" << sampler.samples[0].value << "\n";
    if (sampler.samples.size() > 1) {
      auto& last = sampler.samples[sampler.samples.size() - 1];
      ss << Indent(indent) << "  last sample: t=" << last.t 
         << ", value=" << last.value << "\n";
    }
  }
}

void DumpSkelNode(std::stringstream& ss, const SkelNode& node, uint32_t indent) {
  ss << Indent(indent) << node.joint_name;
  
  if (!node.joint_path.empty()) {
    ss << " (path: " << node.joint_path << ")";
  }
  
  ss << " [id: " << node.joint_id << "]\n";
  
  if (!node.children.empty()) {
    for (const auto& child : node.children) {
      DumpSkelNode(ss, child, indent + 1);
    }
  }
}

void DumpAnimChannel(std::stringstream& ss, const AnimationChannel& channel,
                    uint32_t indent) {
  ss << Indent(indent);
  switch (channel.type) {
    case AnimationChannel::ChannelType::Transform:
      ss << "transform";
      break;
    case AnimationChannel::ChannelType::Translation:
      ss << "translation";
      break;
    case AnimationChannel::ChannelType::Rotation:
      ss << "rotation";
      break;
    case AnimationChannel::ChannelType::Scale:
      ss << "scale";
      break;
    case AnimationChannel::ChannelType::Weight:
      ss << "weight";
      break;
  }
  ss << ":\n";
  
  // Print sampler info based on type
  if (channel.type == AnimationChannel::ChannelType::Transform) {
    PrintAnimationSamples(ss, channel.transforms, indent + 1);
  } else if (channel.type == AnimationChannel::ChannelType::Translation) {
    PrintAnimationSamples(ss, channel.translations, indent + 1);
  } else if (channel.type == AnimationChannel::ChannelType::Rotation) {
    PrintAnimationSamples(ss, channel.rotations, indent + 1);
  } else if (channel.type == AnimationChannel::ChannelType::Scale) {
    PrintAnimationSamples(ss, channel.scales, indent + 1);
  } else if (channel.type == AnimationChannel::ChannelType::Weight) {
    PrintAnimationSamples(ss, channel.weights, indent + 1);
  }
}

} // anonymous namespace

std::string DumpVertexAttributeData(const VertexAttribute& vattr,
                                   uint32_t offset, uint32_t count) {
  if (vattr.data.empty()) {
    return "[]";
  }
  
  // Use format to determine the type
  switch (vattr.format) {
    case VertexAttributeFormat::Float:
      return DumpVertexAttributeDataImpl(
          reinterpret_cast<const float*>(vattr.data.data()),
          vattr.data.size(), offset, count);
    case VertexAttributeFormat::Float2:
    case VertexAttributeFormat::Vec2:
      return DumpVertexAttributeDataImpl(
          reinterpret_cast<const value::float2*>(vattr.data.data()),
          vattr.data.size(), offset, count);
    case VertexAttributeFormat::Float3:
    case VertexAttributeFormat::Vec3:
      return DumpVertexAttributeDataImpl(
          reinterpret_cast<const value::float3*>(vattr.data.data()),
          vattr.data.size(), offset, count);
    case VertexAttributeFormat::Float4:
    case VertexAttributeFormat::Vec4:
      return DumpVertexAttributeDataImpl(
          reinterpret_cast<const value::float4*>(vattr.data.data()),
          vattr.data.size(), offset, count);
    default:
      return "[unsupported format]";
  }
}

std::string DumpVertexAttribute(const VertexAttribute& vattr, uint32_t indent) {
  std::stringstream ss;
  
  ss << Indent(indent) << vattr.name << ":\n";
  ss << Indent(indent + 1) << "format: " << to_string(vattr.format) << "\n";
  ss << Indent(indent + 1) << "variability: " << to_string(vattr.variability) << "\n";
  ss << Indent(indent + 1) << "vertex_count: " << vattr.vertex_count() << "\n";
  ss << Indent(indent + 1) << "elementSize: " << vattr.elementSize << "\n";
  
  if (vattr.vertex_count() > 0) {
    ss << Indent(indent + 1) << "data (first 16): "
       << DumpVertexAttributeData(vattr, 0, 16) << "\n";
  }
  
  return ss.str();
}

std::string DumpNode(const Node& node, uint32_t indent) {
  std::stringstream ss;
  
  ss << Indent(indent) << "Node: " << node.prim_name << "\n";
  ss << Indent(indent + 1) << "abs_path: " << node.abs_path << "\n";
  
  if (!node.display_name.empty()) {
    ss << Indent(indent + 1) << "display_name: " << node.display_name << "\n";
  }
  
  ss << Indent(indent + 1) << "nodeType: " << to_string(node.nodeType) << "\n";
  
  if (node.id != -1) {
    ss << Indent(indent + 1) << "id: " << node.id << "\n";
  }
  
  ss << Indent(indent + 1) << "local_matrix:\n";
  for (int i = 0; i < 4; i++) {
    ss << Indent(indent + 2) << "[";
    for (int j = 0; j < 4; j++) {
      if (j > 0) ss << ", ";
      ss << std::fixed << std::setprecision(3) << node.local_matrix.m[i][j];
    }
    ss << "]\n";
  }
  
  if (!node.children.empty()) {
    ss << Indent(indent + 1) << "children: " << node.children.size() << " nodes\n";
  }
  
  return ss.str();
}

std::string DumpMaterialSubset(const MaterialSubset& msubset, uint32_t indent) {
  std::stringstream ss;
  
  ss << Indent(indent) << "MaterialSubset:\n";
  ss << Indent(indent + 1) << "prim_name: " << msubset.prim_name << "\n";
  ss << Indent(indent + 1) << "material_id: " << msubset.material_id << "\n";
  
  const auto& indices_vec = msubset.indices();
  ss << Indent(indent + 1) << "num_faces: " << indices_vec.size() << "\n";
  
  if (!indices_vec.empty()) {
    ss << Indent(indent + 1) << "indices (first 16): [";
    size_t n = std::min(size_t(16), indices_vec.size());
    for (size_t i = 0; i < n; i++) {
      if (i > 0) ss << ", ";
      ss << indices_vec[i];
    }
    if (indices_vec.size() > 16) {
      ss << ", ...";
    }
    ss << "]\n";
  }
  
  return ss.str();
}

std::string DumpMesh(const RenderMesh& mesh, uint32_t indent) {
  std::stringstream ss;
  
  ss << Indent(indent) << "Mesh: " << mesh.prim_name << "\n";
  
  // Basic info
  ss << Indent(indent + 1) << "num_points: " << mesh.points.size() << "\n";
  ss << Indent(indent + 1) << "num_faces: " << mesh.faceVertexCounts().size() << "\n";
  ss << Indent(indent + 1) << "num_face_vertex_indices: " << mesh.faceVertexIndices().size() << "\n";
  
  if (!mesh.points.empty()) {
    ss << Indent(indent + 1) << "points (first 4): [";
    size_t n = std::min(size_t(4), mesh.points.size());
    for (size_t i = 0; i < n; i++) {
      if (i > 0) ss << ", ";
      ss << mesh.points[i];
    }
    if (mesh.points.size() > 4) {
      ss << ", ...";
    }
    ss << "]\n";
  }
  
  // Vertex attributes
  if (mesh.vertex_colors.vertex_count() > 0) {
    ss << DumpVertexAttribute(mesh.vertex_colors, indent + 1);
  }
  
  if (mesh.vertex_opacities.vertex_count() > 0) {
    ss << DumpVertexAttribute(mesh.vertex_opacities, indent + 1);
  }
  
  if (mesh.normals.vertex_count() > 0) {
    ss << DumpVertexAttribute(mesh.normals, indent + 1);
  }
  
  // Texcoords
  if (!mesh.texcoords.empty()) {
    ss << Indent(indent + 1) << "texcoords:\n";
    for (const auto& tc : mesh.texcoords) {
      ss << Indent(indent + 2) << "slot " << tc.first << ": "
         << tc.second.vertex_count() << " coords\n";
    }
  }
  
  // Material subsets
  if (!mesh.material_subsets.empty()) {
    ss << Indent(indent + 1) << "material_subsets: " << mesh.material_subsets.size() << "\n";
    for (size_t i = 0; i < std::min(size_t(3), mesh.material_subsets.size()); i++) {
      ss << DumpMaterialSubset(mesh.material_subsets[i], indent + 2);
    }
  }
  
  return ss.str();
}

std::string DumpPreviewSurface(const PreviewSurfaceShader& shader, uint32_t indent) {
  std::stringstream ss;
  
  ss << Indent(indent) << "PreviewSurface:\n";
  
  auto dump_param = [&](const std::string& name, const auto& param) {
    ss << Indent(indent + 1) << name << ": ";
    if (param.texture_id >= 0) {
      ss << "[texture: " << param.texture_id << "]";
    } else {
      ss << param.value;
    }
    ss << "\n";
  };
  
  dump_param("diffuseColor", shader.diffuseColor);
  dump_param("emissiveColor", shader.emissiveColor);
  dump_param("normal", shader.normal);
  dump_param("roughness", shader.roughness);
  dump_param("metallic", shader.metallic);
  dump_param("opacity", shader.opacity);
  dump_param("ior", shader.ior);
  dump_param("clearcoat", shader.clearcoat);
  dump_param("clearcoatRoughness", shader.clearcoatRoughness);
  
  return ss.str();
}

std::string DumpMaterial(const RenderMaterial& material, uint32_t indent) {
  std::stringstream ss;
  
  ss << Indent(indent) << "Material: " << material.name << "\n";
  ss << Indent(indent + 1) << "id: " << material.id << "\n";
  
  ss << DumpPreviewSurface(material.surfaceShader, indent + 1);
  
  return ss.str();
}

std::string DumpCamera(const RenderCamera& camera, uint32_t indent) {
  std::stringstream ss;
  
  ss << Indent(indent) << "Camera:\n";
  ss << Indent(indent + 1) << "projection: "
     << (camera.projection == CameraProjection::Perspective ? "perspective" : "orthographic") << "\n";
  
  if (camera.projection == CameraProjection::Perspective) {
    ss << Indent(indent + 1) << "fov: " << camera.fov << "\n";
  } else {
    ss << Indent(indent + 1) << "orthographic_size: "
       << camera.orthographic_size[0] << " x " << camera.orthographic_size[1] << "\n";
  }
  
  ss << Indent(indent + 1) << "near: " << camera.znear << "\n";
  ss << Indent(indent + 1) << "far: " << camera.zfar << "\n";
  
  return ss.str();
}

std::string DumpAnimation(const Animation& anim, uint32_t indent) {
  std::stringstream ss;
  
  ss << Indent(indent) << "Animation:\n";
  ss << Indent(indent + 1) << "prim_name: " << anim.prim_name << "\n";
  ss << Indent(indent + 1) << "abs_path: " << anim.abs_path << "\n";
  
  if (!anim.display_name.empty()) {
    ss << Indent(indent + 1) << "display_name: " << anim.display_name << "\n";
  }
  
  // Dump channels
  if (!anim.channels_map.empty()) {
    ss << Indent(indent + 1) << "channels:\n";
    for (const auto& joint_channels : anim.channels_map) {
      ss << Indent(indent + 2) << joint_channels.first << ":\n";
      for (const auto& ch : joint_channels.second) {
        DumpAnimChannel(ss, ch.second, indent + 3);
      }
    }
  }
  
  // Dump blendshape weights
  if (!anim.blendshape_weights_map.empty()) {
    ss << Indent(indent + 1) << "blendshape_weights: " 
       << anim.blendshape_weights_map.size() << " targets\n";
  }
  
  return ss.str();
}

std::string DumpSkeleton(const SkelHierarchy& skel, uint32_t indent) {
  std::stringstream ss;
  
  ss << Indent(indent) << "Skeleton:\n";
  ss << Indent(indent + 1) << "num_joints: " << skel.joints.size() << "\n";
  
  if (!skel.root_nodes.empty()) {
    ss << Indent(indent + 1) << "hierarchy:\n";
    for (const auto& root : skel.root_nodes) {
      DumpSkelNode(ss, root, indent + 2);
    }
  }
  
  return ss.str();
}

std::string DumpImage(const TextureImage& image, uint32_t indent) {
  std::stringstream ss;
  
  ss << Indent(indent) << "TextureImage:\n";
  ss << Indent(indent + 1) << "name: " << image.image_name << "\n";
  ss << Indent(indent + 1) << "asset_path: " << image.asset_path << "\n";
  ss << Indent(indent + 1) << "size: " << image.width << " x " << image.height << "\n";
  ss << Indent(indent + 1) << "channels: " << image.channels << "\n";
  ss << Indent(indent + 1) << "bits_per_channel: " << image.bits_per_channel << "\n";
  ss << Indent(indent + 1) << "colorspace: " << to_string(image.colorspace) << "\n";
  
  return ss.str();
}

std::string DumpUVTexture(const UVTexture& texture, uint32_t indent) {
  std::stringstream ss;
  
  ss << Indent(indent) << "UVTexture:\n";
  ss << Indent(indent + 1) << "texture_image_id: " << texture.texture_image_id << "\n";
  ss << Indent(indent + 1) << "uv_slot: " << texture.uv_slot_id << "\n";
  ss << Indent(indent + 1) << "varname_uv: " << texture.varname_uv << "\n";
  
  if (texture.has_transform2d) {
    ss << Indent(indent + 1) << "transform2d:\n";
    ss << Indent(indent + 2) << "translation: " << texture.tx_translation << "\n";
    ss << Indent(indent + 2) << "rotation: " << texture.tx_rotation << "\n";
    ss << Indent(indent + 2) << "scale: " << texture.tx_scale << "\n";
  }
  
  return ss.str();
}

std::string DumpBuffer(const BufferData& buffer, uint32_t indent) {
  std::stringstream ss;
  
  ss << Indent(indent) << "Buffer:\n";
  ss << Indent(indent + 1) << "size: " << buffer.data.size() << " bytes\n";
  
  if (!buffer.uri.empty()) {
    ss << Indent(indent + 1) << "uri: " << buffer.uri << "\n";
  }
  
  return ss.str();
}

std::string DumpRenderScene(const RenderScene& scene,
                           bool dumpMesh,
                           bool dumpMaterial,
                           bool dumpTexture,
                           bool dumpCamera,
                           bool dumpSkeleton,
                           bool dumpAnimation) {
  std::stringstream ss;
  
  ss << "RenderScene:\n";
  
  // Scene metadata
  if (!scene.meta.name.empty()) {
    ss << "  name: " << scene.meta.name << "\n";
  }
  
  // Node hierarchy
  if (!scene.nodes.empty()) {
    ss << "  nodes: " << scene.nodes.size() << "\n";
    for (size_t i = 0; i < std::min(size_t(3), scene.nodes.size()); i++) {
      ss << DumpNode(scene.nodes[i], 2);
    }
  }
  
  // Meshes
  if (dumpMesh && !scene.meshes.empty()) {
    ss << "  meshes: " << scene.meshes.size() << "\n";
    for (size_t i = 0; i < std::min(size_t(3), scene.meshes.size()); i++) {
      ss << DumpMesh(scene.meshes[i], 2);
    }
  }
  
  // Materials
  if (dumpMaterial && !scene.materials.empty()) {
    ss << "  materials: " << scene.materials.size() << "\n";
    for (size_t i = 0; i < std::min(size_t(3), scene.materials.size()); i++) {
      ss << DumpMaterial(scene.materials[i], 2);
    }
  }
  
  // Textures
  if (dumpTexture) {
    if (!scene.images.empty()) {
      ss << "  images: " << scene.images.size() << "\n";
      for (size_t i = 0; i < std::min(size_t(3), scene.images.size()); i++) {
        ss << DumpImage(scene.images[i], 2);
      }
    }
    
    if (!scene.textures.empty()) {
      ss << "  textures: " << scene.textures.size() << "\n";
      for (size_t i = 0; i < std::min(size_t(3), scene.textures.size()); i++) {
        ss << DumpUVTexture(scene.textures[i], 2);
      }
    }
  }
  
  // Cameras
  if (dumpCamera && !scene.cameras.empty()) {
    ss << "  cameras: " << scene.cameras.size() << "\n";
    for (size_t i = 0; i < scene.cameras.size(); i++) {
      ss << DumpCamera(scene.cameras[i], 2);
    }
  }
  
  // Skeletons
  if (dumpSkeleton && !scene.skeletons.empty()) {
    ss << "  skeletons: " << scene.skeletons.size() << "\n";
    for (size_t i = 0; i < scene.skeletons.size(); i++) {
      ss << DumpSkeleton(scene.skeletons[i], 2);
    }
  }
  
  // Animations
  if (dumpAnimation && !scene.animations.empty()) {
    ss << "  animations: " << scene.animations.size() << "\n";
    for (size_t i = 0; i < scene.animations.size(); i++) {
      ss << DumpAnimation(scene.animations[i], 2);
    }
  }
  
  // Buffers
  if (!scene.buffers.empty()) {
    ss << "  buffers: " << scene.buffers.size() << "\n";
  }
  
  return ss.str();
}

} // namespace tydra
} // namespace tinyusdz