// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.

#pragma once

#include <string>
#include <cstdint>

namespace tinyusdz {
namespace tydra {

// Forward declarations
struct RenderScene;
struct RenderMesh;
struct RenderMaterial;
struct RenderCamera;
struct Node;
struct Animation;
struct BufferData;
struct TextureImage;
struct UVTexture;
struct VertexAttribute;
struct PreviewSurfaceShader;
struct MaterialSubset;
struct SkelHierarchy;

///
/// Debug dump functions for RenderScene data structures
///

///
/// Dump vertex attribute data
///
std::string DumpVertexAttributeData(const VertexAttribute& vattr,
                                    uint32_t offset = 0,
                                    uint32_t count = ~0u);

///
/// Dump vertex attribute metadata
///
std::string DumpVertexAttribute(const VertexAttribute& vattr, uint32_t indent = 0);

///
/// Dump Node hierarchy
///
std::string DumpNode(const Node& node, uint32_t indent = 0);

///
/// Dump RenderMesh
///
std::string DumpMesh(const RenderMesh& mesh, uint32_t indent = 0);

///
/// Dump MaterialSubset
///
std::string DumpMaterialSubset(const MaterialSubset& msubset, uint32_t indent = 0);

///
/// Dump RenderMaterial
///
std::string DumpMaterial(const RenderMaterial& material, uint32_t indent = 0);

///
/// Dump PreviewSurfaceShader
///
std::string DumpPreviewSurface(const PreviewSurfaceShader& shader, uint32_t indent = 0);

///
/// Dump RenderCamera
///
std::string DumpCamera(const RenderCamera& camera, uint32_t indent = 0);

///
/// Dump Animation
///
std::string DumpAnimation(const Animation& anim, uint32_t indent = 0);

///
/// Dump SkelHierarchy
///
std::string DumpSkeleton(const SkelHierarchy& skel, uint32_t indent = 0);

///
/// Dump TextureImage
///
std::string DumpImage(const TextureImage& image, uint32_t indent = 0);

///
/// Dump UVTexture
///
std::string DumpUVTexture(const UVTexture& texture, uint32_t indent = 0);

///
/// Dump BufferData
///
std::string DumpBuffer(const BufferData& buffer, uint32_t indent = 0);

///
/// Dump entire RenderScene
/// @param scene The RenderScene to dump
/// @param dumpMesh Include mesh data in dump
/// @param dumpMaterial Include material data in dump
/// @param dumpTexture Include texture data in dump
/// @param dumpCamera Include camera data in dump
/// @param dumpSkeleton Include skeleton data in dump
/// @param dumpAnimation Include animation data in dump
///
std::string DumpRenderScene(const RenderScene& scene,
                           bool dumpMesh = true,
                           bool dumpMaterial = true,
                           bool dumpTexture = true,
                           bool dumpCamera = true,
                           bool dumpSkeleton = true,
                           bool dumpAnimation = true);

} // namespace tydra
} // namespace tinyusdz