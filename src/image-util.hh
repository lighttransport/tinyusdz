// SPDX-License-Identifier: Apache 2.0
// Copyright 2023-Present, Light Transport Entertainment Inc.
//
// Image utilities.
// Currently sRGB color space conversion feature is provided.
//
// TODO
// - [ ] Image resize
// - [ ] Rec.709 <-> Linear conversion
// - [ ] OIIO 3D LUT support through tinycolorio
//
#pragma once

#include <vector>
#include <cstdlib>
#include <cstdint>

namespace tinyusdz {

///
/// Convert fp32 image in linear space to 8bit image in sRGB color space.
///
/// @param[in] in_image Input image in linear color space. Image size =
/// [width_byte_stride/sizeof(float), height, channel_stride]
/// @param[in] width Width pixels
/// @param[in] width_byte_stride Width byte stride. 0 = Use `width` *
/// channel_stride * sizeof(float)
/// @param[in] height Height pixels
/// @param[in] chanels Pixel channels to apply conversion. must be less than or
/// equal to `channel_stride`
/// @param[in] chanel_stride channel stride. For example, channels=3 and
/// channel_stride=4 to convert RGB channel to sRGB but leave alpha channel
/// linear for RGBA image.
/// @param[out] out_image Image in sRGB colorspace. Image size is same with
/// `in_image`
///
/// @return true upon success. false when any parameter is invalid.
bool linear_to_srgb_8bit(const std::vector<float> &in_img, size_t width,
                         size_t width_byte_stride, size_t height,
                         size_t channels, size_t channel_stride,
                         std::vector<uint8_t> *out_img);

///
/// Convert 8bit image in sRGB to fp32 image in linear color space.
///
/// @param[in] in_image Input image in sRGB color space. Image size =
/// [width_byte_stride, height, channel_stride]
/// @param[in] width Width pixels
/// @param[in] width_byte_stride Width byte stride. 0 = Use `width` *
/// channel_stride
/// @param[in] height Height pixels
/// @param[in] chanels Pixel channels to apply conversion. must be less than or
/// equal to `channel_stride`
/// @param[in] chanel_stride channel stride. For example, channels=3 and
/// channel_stride=4 to apply inverse sRGB convertion to RGB channel but apply
/// linear conversion to alpha channel for RGBA image.
/// @param[out] out_image Image in linear color space. Image size is same with
/// `in_image`
///
/// @return true upon success. false when any parameter is invalid.
bool srgb_8bit_to_linear(const std::vector<uint8_t> &in_img, size_t width,
                         size_t width_byte_stride, size_t height,
                         size_t channels, size_t channel_stride,
                         std::vector<float> *out_img);

}  // namespace tinyusdz
