#include "texture-util.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif

// stb_image_resize2 implementation define is in src/image-util.cc

#include "external/stb_image_resize2.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

namespace tinyusdz {
namespace tydra {

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
    size_t &dstHeight)	
{
	if (occlusionChannel > occlusionImageChannels) {
		return false;
	}
	if (roughnessChannel > roughnessImageChannels) {
		return false;
	}
	if (metallicChannel > metallicImageChannels) {
		return false;
	}

	size_t maxImageWidth = 1;
	size_t maxImageHeight = 1;
	if (!occlusionImageData.empty()) {
		maxImageWidth = (std::max)(maxImageWidth, occlusionImageWidth);
		maxImageHeight = (std::max)(maxImageHeight, occlusionImageHeight);
	}
	if (!roughnessImageData.empty()) {
		maxImageWidth = (std::max)(maxImageWidth,  roughnessImageWidth);
		maxImageHeight = (std::max)(maxImageHeight, roughnessImageHeight);
	}
	if (!metallicImageData.empty()) {
		maxImageWidth = (std::max)(maxImageWidth, metallicImageWidth);
		maxImageHeight = (std::max)(maxImageHeight, metallicImageHeight);
	}

	std::vector<uint8_t> occlusionBuf;
	std::vector<uint8_t> roughnessBuf;
	std::vector<uint8_t> metallicBuf;

	if (!occlusionImageData.empty()) {
		if ((maxImageWidth != occlusionImageWidth) || (maxImageHeight != occlusionImageHeight)) {
			stbir_pixel_layout layout;
			if (occlusionImageChannels == 1) {
				layout = STBIR_1CHANNEL;
			} else if (occlusionImageChannels == 2) {
				layout = STBIR_2CHANNEL;
			} else if (occlusionImageChannels == 3) {
				layout = STBIR_RGB;
			} else { // assume RGBA
				layout = STBIR_RGBA;
			}

			occlusionBuf.resize(maxImageWidth * maxImageHeight * occlusionImageChannels);

			stbir_resize_uint8_linear(occlusionImageData.data(), int(occlusionImageWidth), int(occlusionImageHeight), 0, occlusionBuf.data(), int(maxImageWidth), int(maxImageHeight), 0, layout); 
		}
	} else {
		occlusionBuf = occlusionImageData;
	} 

	if (!metallicImageData.empty()) {
		if ((maxImageWidth != metallicImageWidth) || (maxImageHeight != metallicImageHeight)) {
			stbir_pixel_layout layout;
			if (metallicImageChannels == 1) {
				layout = STBIR_1CHANNEL;
			} else if (metallicImageChannels == 2) {
				layout = STBIR_2CHANNEL;
			} else if (metallicImageChannels == 3) {
				layout = STBIR_RGB;
			} else { // assume RGBA
				layout = STBIR_RGBA;
			}

			metallicBuf.resize(maxImageWidth * maxImageHeight * metallicImageChannels);

			stbir_resize_uint8_linear(metallicImageData.data(), int(metallicImageWidth), int(metallicImageHeight), 0, metallicBuf.data(), int(maxImageWidth), int(maxImageHeight), 0, layout); 
		} else {
			metallicBuf = metallicImageData;
		}
	} 

	if (!roughnessImageData.empty()) {
		if ((maxImageWidth != roughnessImageWidth) || (maxImageHeight != roughnessImageHeight)) {
			stbir_pixel_layout layout;
			if (roughnessImageChannels == 1) {
				layout = STBIR_1CHANNEL;
			} else if (roughnessImageChannels == 2) {
				layout = STBIR_2CHANNEL;
			} else if (roughnessImageChannels == 3) {
				layout = STBIR_RGB;
			} else { // assume RGBA
				layout = STBIR_RGBA;
			}

			roughnessBuf.resize(maxImageWidth * maxImageHeight * roughnessImageChannels);

			stbir_resize_uint8_linear(roughnessImageData.data(), int(roughnessImageWidth), int(roughnessImageHeight), 0, roughnessBuf.data(), int(maxImageWidth), int(maxImageHeight), 0, layout); 
		} else {
			roughnessBuf = roughnessImageData;
		}
	} 

	uint8_t occlusionValue = uint8_t((std::max)((std::min)(255, int(occlusionFactor * 255.0f)), 0));
	uint8_t metallicValue = uint8_t((std::max)((std::min)(255, int(metallicFactor * 255.0f)), 0));
	uint8_t roughnessValue = uint8_t((std::max)((std::min)(255, int(roughnessFactor * 255.0f)), 0));

	dst.resize(maxImageWidth * maxImageHeight * 3);

	for (size_t i = 0; i < maxImageWidth * maxImageHeight; i++) {
		// Use the first component of texel when input is a texture.
		uint8_t r = occlusionBuf.size() ? occlusionBuf[i * occlusionImageChannels + occlusionChannel] : occlusionValue;
		uint8_t g = roughnessBuf.size() ? roughnessBuf[i * roughnessImageChannels + roughnessChannel] : roughnessValue;
		uint8_t b = metallicBuf.size() ? metallicBuf[i * metallicImageChannels + metallicChannel] : metallicValue;

		dst[3 * i + 0] = r;
		dst[3 * i + 1] = g;
		dst[3 * i + 2] = b;
	}

	dstWidth = maxImageWidth;
	dstHeight = maxImageHeight;

	return true;
}

} // namespace tydra
} // namespace tinyusdz
