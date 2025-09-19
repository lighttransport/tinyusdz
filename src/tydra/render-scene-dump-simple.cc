// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.

// Simplified render-scene-dump implementation that compiles with current structures

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

} // anonymous namespace

std::string DumpVertexAttributeData(const VertexAttribute& vattr,
                                   uint32_t offset, uint32_t count) {
  // Simple implementation - just return size info
  return fmt::format("[{} bytes]", vattr.data.size());
}

std::string DumpVertexAttribute(const VertexAttribute& vattr, uint32_t indent) {
  std::stringstream ss;
  ss << Indent(indent) << vattr.name << ":\n";
  ss << Indent(indent + 1) << "format: " << to_string(vattr.format) << "\n";
  ss << Indent(indent + 1) << "variability: " << to_string(vattr.variability) << "\n";
  ss << Indent(indent + 1) << "vertex_count: " << vattr.vertex_count() << "\n";
  return ss.str();
}

std::string DumpNode(const Node& node, uint32_t indent) {
  std::stringstream ss;
  ss << Indent(indent) << "Node: " << node.prim_name << "\n";
  ss << Indent(indent + 1) << "type: " << to_string(node.nodeType) << "\n";
  if (node.id != -1) {
    ss << Indent(indent + 1) << "id: " << node.id << "\n";
  }
  if (!node.children.empty()) {
    ss << Indent(indent + 1) << "children: " << node.children.size() << "\n";
  }
  return ss.str();
}

std::string DumpMaterialSubset(const MaterialSubset& msubset, uint32_t indent) {
  std::stringstream ss;
  ss << Indent(indent) << "MaterialSubset: " << msubset.prim_name << "\n";
  ss << Indent(indent + 1) << "material_id: " << msubset.material_id << "\n";
  return ss.str();
}

std::string DumpMesh(const RenderMesh& mesh, uint32_t indent) {
  std::stringstream ss;
  ss << Indent(indent) << "Mesh: " << mesh.prim_name << "\n";
  ss << Indent(indent + 1) << "points: " << mesh.points.size() << "\n";
  ss << Indent(indent + 1) << "faces: " << mesh.faceVertexCounts().size() << "\n";
  
  // Dump vertex attributes
  if (mesh.normals.vertex_count() > 0) {
    ss << DumpVertexAttribute(mesh.normals, indent + 1);
  }
  
  // Dump texcoords
  if (!mesh.texcoords.empty()) {
    ss << Indent(indent + 1) << "texcoords: " << mesh.texcoords.size() << " sets\n";
  }
  
  // Dump material subsets
  if (!mesh.material_subsetMap.empty()) {
    ss << Indent(indent + 1) << "material_subsets: " << mesh.material_subsetMap.size() << "\n";
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
  dump_param("roughness", shader.roughness);
  dump_param("metallic", shader.metallic);
  
  return ss.str();
}

std::string DumpMaterial(const RenderMaterial& material, uint32_t indent) {
  std::stringstream ss;
  ss << Indent(indent) << "Material: " << material.name << "\n";
  ss << DumpPreviewSurface(material.surfaceShader, indent + 1);
  return ss.str();
}

std::string DumpCamera(const RenderCamera& camera, uint32_t indent) {
  std::stringstream ss;
  ss << Indent(indent) << "Camera:\n";
  ss << Indent(indent + 1) << "projection: " << to_string(camera.projection) << "\n";
  if (camera.yfov() > 0) {
    ss << Indent(indent + 1) << "yfov: " << camera.yfov() << "\n";
  }
  ss << Indent(indent + 1) << "znear: " << camera.znear << "\n";
  ss << Indent(indent + 1) << "zfar: " << camera.zfar << "\n";
  return ss.str();
}

std::string DumpAnimation(const Animation& anim, uint32_t indent) {
  std::stringstream ss;
  ss << Indent(indent) << "Animation: " << anim.prim_name << "\n";
  if (!anim.channels_map.empty()) {
    ss << Indent(indent + 1) << "channels: " << anim.channels_map.size() << " joints\n";
  }
  if (!anim.blendshape_weights_map.empty()) {
    ss << Indent(indent + 1) << "blendshapes: " << anim.blendshape_weights_map.size() << "\n";
  }
  return ss.str();
}

std::string DumpSkeleton(const SkelHierarchy& skel, uint32_t indent) {
  std::stringstream ss;
  ss << Indent(indent) << "Skeleton:\n";
  // SkelHierarchy doesn't have a simple joints member, so just basic info
  return ss.str();
}

std::string DumpImage(const TextureImage& image, uint32_t indent) {
  std::stringstream ss;
  ss << Indent(indent) << "TextureImage:\n";
  ss << Indent(indent + 1) << "size: " << image.width << " x " << image.height << "\n";
  ss << Indent(indent + 1) << "channels: " << image.channels << "\n";
  return ss.str();
}

std::string DumpUVTexture(const UVTexture& texture, uint32_t indent) {
  std::stringstream ss;
  ss << Indent(indent) << "UVTexture:\n";
  ss << Indent(indent + 1) << "texture_image_id: " << texture.texture_image_id << "\n";
  ss << Indent(indent + 1) << "varname_uv: " << texture.varname_uv << "\n";
  return ss.str();
}

std::string DumpBuffer(const BufferData& buffer, uint32_t indent) {
  std::stringstream ss;
  ss << Indent(indent) << "Buffer:\n";
  ss << Indent(indent + 1) << "size: " << buffer.data.size() << " bytes\n";
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
  if (!scene.meta.comment.empty()) {
    ss << "  comment: " << scene.meta.comment << "\n";
  }
  if (!scene.meta.copyright.empty()) {
    ss << "  copyright: " << scene.meta.copyright << "\n";
  }
  
  // Node hierarchy
  if (!scene.nodes.empty()) {
    ss << "  nodes: " << scene.nodes.size() << "\n";
    if (dumpMesh) {
      for (size_t i = 0; i < std::min(size_t(3), scene.nodes.size()); i++) {
        ss << DumpNode(scene.nodes[i], 2);
      }
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
    }
    if (!scene.textures.empty()) {
      ss << "  textures: " << scene.textures.size() << "\n";
    }
  }
  
  // Cameras
  if (dumpCamera && !scene.cameras.empty()) {
    ss << "  cameras: " << scene.cameras.size() << "\n";
  }
  
  // Animations
  if (dumpAnimation && !scene.animations.empty()) {
    ss << "  animations: " << scene.animations.size() << "\n";
  }
  
  return ss.str();
}

} // namespace tydra
} // namespace tinyusdz