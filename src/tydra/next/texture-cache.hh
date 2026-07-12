// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Tydra Next - shared texture decoding for the next path.
//
// tydra-next's converter records texture *metadata* (RenderTexture: asset path,
// wrap, value scale/bias, channel) but never decodes pixels -- applications
// supply their own decode so the converter stays filesystem- and
// archive-agnostic. Both tusdview and tusdrender need the same thing, and had
// grown two copies of it; this is the one implementation.
//
// The size cap and the byte budget are applied AT DECODE TIME, not as a
// post-pass over an already-decoded set: on a large scene the post-pass peak
// (every texture resident at full resolution before anything shrinks) is the
// spike we are trying to avoid.
//
// Scope note: simple box/sRGB-aware resize only, via tydra::ResizeImage. Mip
// chains, block compression, and content-aware filtering deliberately live
// elsewhere.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tinyusdz {
namespace next {
class USDZReader;
}  // namespace next

namespace tydra {
namespace next {

/// Decoded 8-bit texture pixels. `channels` is 4 unless the decoder was told to
/// keep the source channel count (see TextureDecodeOptions::force_rgba).
struct DecodedImage {
  uint32_t width = 0;
  uint32_t height = 0;
  uint8_t channels = 4;
  std::vector<uint8_t> pixels;  // width * height * channels

  bool empty() const { return pixels.empty(); }
  size_t byte_size() const { return pixels.size(); }
};

struct TextureDecodeOptions {
  /// Directory the source USD resolves relative asset paths against.
  std::string base_dir;
  /// Optional .usdz archive to look the asset up in before the filesystem.
  const ::tinyusdz::next::USDZReader* usdz = nullptr;
  /// Longest-edge cap in texels. 0 = keep the source resolution.
  uint32_t max_edge = 0;
  /// Best-effort cap on the total decoded bytes handed out by this decoder.
  /// Once the running total would exceed it, further images are shrunk to fit.
  /// 0 = unbounded.
  uint64_t budget_bytes = 0;
  /// Expand every image to 4 channels. Consumers whose GPU texture format is
  /// RGBA8 want this; a CPU sampler that honours the source channel count
  /// should turn it off rather than pay for a synthetic alpha channel.
  bool force_rgba = true;
};

/// Resolves an asset path (filesystem or .usdz entry), decodes it to RGBA8, and
/// shrinks it to fit the configured size cap and byte budget. Callers keep their
/// own dedupe map -- this only owns the budget accounting, so no pixels are held
/// alive here.
class TextureDecoder {
 public:
  explicit TextureDecoder(TextureDecodeOptions options)
      : options_(std::move(options)) {}

  /// Decode `asset` into `out`. `srgb` selects the resize filter's color space
  /// (sRGB data is filtered in linear light). Returns false when the asset
  /// cannot be resolved or decoded; `out` is left untouched.
  bool Decode(const std::string& asset, bool srgb, DecodedImage* out);

  /// Read `asset`'s raw bytes, resolving it exactly like Decode does (a .usdz
  /// entry when one is set, else `base_dir`-relative on disk). For assets a
  /// consumer must interpret itself rather than have decoded to pixels -- e.g. a
  /// GPU-compressed `.ktx2`, whose blocks are uploaded as-is. Returns false when
  /// the asset cannot be resolved or read.
  bool ReadAssetBytes(const std::string& asset, std::vector<uint8_t>* out) const;

  /// Running total of the decoded bytes handed out (post-shrink).
  uint64_t decoded_bytes() const { return decoded_bytes_; }
  /// How many images were shrunk by the size cap or the byte budget.
  uint64_t downscaled_count() const { return downscaled_; }

  const TextureDecodeOptions& options() const { return options_; }

 private:
  TextureDecodeOptions options_;
  uint64_t decoded_bytes_ = 0;
  uint64_t downscaled_ = 0;
};

/// Substitute the literal `<UDIM>` token in an asset path with a tile id.
/// tydra-next carries the token through verbatim, so UDIM expansion is the
/// application's job.
std::string ReplaceUdimToken(const std::string& asset, int udim);

}  // namespace next
}  // namespace tydra
}  // namespace tinyusdz
