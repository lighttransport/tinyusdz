// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - Present, Light Transport Entertainment, Inc.

///
/// @file texture-util.hh  
/// @brief Texture processing utilities for material workflows
///
/// Provides utilities for combining and processing textures commonly used
/// in physically-based rendering workflows, particularly for glTF and 
/// USD material conversion.
///
/// Key functions:
/// - BuildOcclusionRoughnessMetallicTexture(): Combines separate texture 
///   channels into a single ORM (Occlusion-Roughness-Metallic) texture
///   following glTF conventions
///
/// Channel packing follows glTF 2.0 specification:
/// - R channel: Occlusion
/// - G channel: Roughness  
/// - B channel: Metallic
/// - A channel: Not used (set to 1.0)
///
#pragma once

#include <vector>
#include <cstdint>
#include <cstdlib>

namespace tinyusdz {
namespace tydra {

///
/// Build combined ORM (Occlusion-Roughness-Metallic) texture for glTF workflow.
/// 
/// Combines separate occlusion, roughness, and metallic texture channels
/// into a single RGB texture following glTF 2.0 specification:
/// - R channel: Occlusion factor and texture
/// - G channel: Roughness factor and texture  
/// - B channel: Metallic factor and texture
/// - A channel: Set to 1.0 (opaque)
///
/// @param[in] occlusionFactor Occlusion multiplier (typically 1.0)
/// @param[in] roughnessFactor Roughness multiplier (typically 1.0) 
/// @param[in] metallicFactor Metallic multiplier (typically 1.0)
/// @param[in] occlusionImageData Source occlusion texture data
/// @param[in] occlusionImageWidth Width of occlusion texture
/// @param[in] occlusionImageHeight Height of occlusion texture
/// @param[in] occlusionImageChannels Channels in occlusion texture
/// @param[in] occlusionChannel Which channel to use from occlusion texture
/// @param[in] roughnessImageData Source roughness texture data
/// @param[in] roughnessImageWidth Width of roughness texture
/// @param[in] roughnessImageHeight Height of roughness texture
/// @param[in] roughnessImageChannels Channels in roughness texture
/// @param[in] roughnessChannel Which channel to use from roughness texture
/// @param[in] metallicImageData Source metallic texture data
/// @param[in] metallicImageWidth Width of metallic texture
/// @param[in] metallicImageHeight Height of metallic texture
/// @param[in] metallicImageChannels Channels in metallic texture
/// @param[in] metallicChannel Which channel to use from metallic texture
/// @param[out] dst Combined RGBA texture data
/// @param[out] dstWidth Width of output texture
/// @param[out] dstHeight Height of output texture
/// @return true on success, false on error
///
bool BuildOcclusionRoughnessMetallicTexture(
	const float occlusionFactor,
	const float roughnessFactor,
	const float metallicFactor,
	const std::vector<uint8_t> &occlusionImageData,
	const size_t occlusionImageWidth,
	const size_t occlusionImageHeight,
	const size_t occlusionImageChannels,
	const size_t occlusionChannel,
	const std::vector<uint8_t> &roughnessImageData,
	const size_t roughnessImageWidth,
	const size_t roughnessImageHeight,
	const size_t roughnessImageChannels,
	const size_t roughnessChannel,
	const std::vector<uint8_t> &metallicImageData,
	const size_t metallicImageWidth,
	const size_t metallicImageHeight,
	const size_t metallicImageChannels,
	const size_t metallicChannel,
	std::vector<uint8_t> &dst, // RGBA
  size_t &dstWidth,	
  size_t &dstHeight);


} // namespace tydra
} // namespace tinyusdz
