// SPDX-License-Identifier: Apache-2.0
// MaterialX graph texture-cache image decoder for tusdview.
// Keep this separate from LightRT's combined image/image-write TU: lightusd
// already owns the image-writer symbols, while this viewer target needs only
// stb_image for asset-relative MaterialX evaluation.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
