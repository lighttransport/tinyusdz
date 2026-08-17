// SPDX-License-Identifier: Apache-2.0
// TinyDNG implementation translation unit.
//
// Keeping the header-only implementation here prevents every image-loader
// consumer from owning a separate implementation and makes the TinyDNG
// symbols link exactly once in the TinyUSDZ library.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define TINY_DNG_LOADER_IMPLEMENTATION
#define TINY_DNG_NO_EXCEPTION
#define TINY_DNG_LOADER_NO_STDIO
#ifndef TINY_DNG_LOADER_ENABLE_ZIP
#define TINY_DNG_LOADER_ENABLE_ZIP
#endif
#define TINY_DNG_LOADER_NO_STB_IMAGE_INCLUDE

#if defined(TINY_DNG_LOADER_USE_SYSTEM_ZLIB)
#include <zlib.h>
#endif

#include "tiny_dng_loader.h"
