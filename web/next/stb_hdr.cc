// SPDX-License-Identifier: MIT
// stb_image HDR (Radiance RGBE) decoder implementation for the lean next
// wasm module. Only the HDR path is compiled (STBI_ONLY_HDR) to keep it small;
// EXR is handled by the tinyexr v3 C backend.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_HDR
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#include "external/stb_image.h"
