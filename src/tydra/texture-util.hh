// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - Present, Light Transport Entertainment, Inc.
//
// Texture utility

#pragma once

#include <vector>
#include <cstdint>
#include <cstdlib>

namespace tinyusdz {
namespace tydra {

/// Build glTF's occlusion, metallic and roughness texture
/// r: occlusion
/// g: roughness
/// b: metallic
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
