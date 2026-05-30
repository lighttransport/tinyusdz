// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment, Inc.
//
// usdz-convert: high-level USD -> USDZ conversion pipeline.
//
// Capabilities:
//   - Load USD (USDA/USDC/USDZ), optionally compose/flatten multiple layers.
//   - Resize textures (cap the longest edge).
//   - Re-encode textures (fast PNG via fpnge when available).
//   - Normalize texture asset paths and pack everything into an ARKit-friendly
//     USDZ archive, then validate it (AOUSD Core Spec section 17).
//   - Standalone generic channel repacking (merge separate maps into a single
//     texture, e.g. R=gloss, G=roughness) via RepackTextureFiles().
//
#pragma once

#include <string>
#include <vector>

#include "core/prim-enums.hh"  // Axis
#include "image-writer.hh"     // image::PngEncoder

namespace tinyusdz {
namespace usdz {

///
/// Output texture format policy.
/// KeepOriginal : re-encode in the source format (PNG -> PNG via fpnge, etc).
/// PNG          : transcode every texture to PNG (fpnge when available).
/// JPEG         : transcode every texture to JPEG.
///
enum class OutputTextureFormat { KeepOriginal, PNG, JPEG };

struct UsdzConvertOptions {
  // Input USD file(s). The first entry is the root layer; additional entries
  // are composed in as sublayers (weaker) when `flatten` is enabled.
  std::vector<std::string> inputs;

  // Output .usdz path.
  std::string output;

  // Compose/flatten the stage before writing. Recommended for USDZ delivery.
  bool flatten{true};

  // Apply ARKit-friendly stage metadata fixups (metersPerUnit/upAxis defaults).
  bool arkit_compatible{false};

  // Stage metadata overrides. metersPerUnit <= 0 => leave as-is.
  double metersPerUnit{0.0};
  // Axis::Invalid => leave as-is.
  Axis upAxis{Axis::Invalid};

  // Resize: cap the longest texture edge to this value (0 => no resize).
  int max_texture_size{0};

  // Texture output format policy.
  OutputTextureFormat texture_format{OutputTextureFormat::KeepOriginal};

  // PNG encoder backend (Auto prefers fpnge when compiled in).
  image::PngEncoder png_encoder{image::PngEncoder::Auto};

  // JPEG quality (1-100) when (re-)encoding JPEG.
  int jpeg_quality{90};

  // Re-encode textures even when no resize/transcode is required. When false,
  // unmodified textures are copied through byte-for-byte.
  bool reencode{true};

  // Verbose logging to stdout.
  bool verbose{false};

  // Optional metadata.
  std::string url;
  std::string copyright;
};

struct UsdzConvertStats {
  size_t num_textures{0};
  size_t num_textures_resized{0};
  size_t num_textures_reencoded{0};
  size_t num_textures_passthrough{0};
  size_t output_size{0};
};

///
/// Run the conversion pipeline. Returns true on success.
///
bool Convert(const UsdzConvertOptions &options, UsdzConvertStats *stats,
             std::string *warn, std::string *err);

// ---------------------------------------------------------------------------
// Standalone generic channel repacking.
// ---------------------------------------------------------------------------

///
/// One output channel source for repacking. When `input_file` is empty, the
/// constant value is written into that output channel.
///
struct RepackChannel {
  std::string input_file;  // image file to sample; empty => use `constant`
  int channel{0};          // channel to sample (0=R,1=G,2=B,3=A)
  uint8_t constant{0};     // value used when input_file is empty
};

struct RepackSpec {
  int out_channels{4};  // number of channels in the packed output (1-4)
  int out_width{0};     // 0 => max width among referenced inputs
  int out_height{0};    // 0 => max height among referenced inputs
  RepackChannel r;
  RepackChannel g;
  RepackChannel b;
  RepackChannel a{/*input_file*/ "", /*channel*/ 0, /*constant*/ 255};
};

///
/// Load the referenced image files, pack channels per `spec`, and write the
/// packed image to `outputFile`. The output format is inferred from
/// `outputFile`'s extension (PNG uses fpnge when available).
///
bool RepackTextureFiles(const RepackSpec &spec, const std::string &outputFile,
                        image::PngEncoder png_encoder, std::string *warn,
                        std::string *err);

}  // namespace usdz
}  // namespace tinyusdz
