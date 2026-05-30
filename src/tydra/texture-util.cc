#include "texture-util.hh"
#include "safe-arithmetic.hh"

#include <algorithm>
#include <cctype>

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
	if (occlusionChannel >= occlusionImageChannels) {
		return false;
	}
	if (roughnessChannel >= roughnessImageChannels) {
		return false;
	}
	if (metallicChannel >= metallicImageChannels) {
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

			size_t resize_size;
			if (!safe::mul3(maxImageWidth, maxImageHeight, occlusionImageChannels, &resize_size)) {
				return false;
			}
			occlusionBuf.resize(resize_size);

			stbir_resize_uint8_linear(occlusionImageData.data(), int(occlusionImageWidth), int(occlusionImageHeight), 0, occlusionBuf.data(), int(maxImageWidth), int(maxImageHeight), 0, layout);
		} else {
			occlusionBuf = occlusionImageData;
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

			size_t resize_size;
			if (!safe::mul3(maxImageWidth, maxImageHeight, metallicImageChannels, &resize_size)) {
				return false;
			}
			metallicBuf.resize(resize_size);

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

			size_t resize_size;
			if (!safe::mul3(maxImageWidth, maxImageHeight, roughnessImageChannels, &resize_size)) {
				return false;
			}
			roughnessBuf.resize(resize_size);

			stbir_resize_uint8_linear(roughnessImageData.data(), int(roughnessImageWidth), int(roughnessImageHeight), 0, roughnessBuf.data(), int(maxImageWidth), int(maxImageHeight), 0, layout);
		} else {
			roughnessBuf = roughnessImageData;
		}
	} 

	uint8_t occlusionValue = uint8_t((std::max)((std::min)(255, int(occlusionFactor * 255.0f)), 0));
	uint8_t metallicValue = uint8_t((std::max)((std::min)(255, int(metallicFactor * 255.0f)), 0));
	uint8_t roughnessValue = uint8_t((std::max)((std::min)(255, int(roughnessFactor * 255.0f)), 0));

	size_t resize_size;
	if (!safe::mul3(maxImageWidth, maxImageHeight, size_t(3), &resize_size)) {
		return false;
	}
	dst.resize(resize_size);

	size_t loop_bound;
	if (!safe::mul(maxImageWidth, maxImageHeight, &loop_bound)) {
		return false;
	}
	for (size_t i = 0; i < loop_bound; i++) {
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

namespace {

stbir_pixel_layout PixelLayoutFromChannels(int channels) {
  if (channels == 1) {
    return STBIR_1CHANNEL;
  } else if (channels == 2) {
    return STBIR_2CHANNEL;
  } else if (channels == 3) {
    return STBIR_RGB;
  }
  return STBIR_RGBA;  // assume RGBA for 4(or more)
}

bool LooksSRGB(const std::string &colorspace) {
  std::string s;
  s.reserve(colorspace.size());
  for (char c : colorspace) {
    s.push_back(char(std::tolower(static_cast<unsigned char>(c))));
  }
  // "sRGB", "srgb", "sRGB - Texture", "sRGB-texture" etc.
  return s.find("srgb") != std::string::npos;
}

}  // namespace

bool ResizeImage(const Image &src, int dstWidth, int dstHeight, Image *dst,
                 ResizeFilter filter, std::string *err) {
  if (dst == nullptr) {
    if (err) (*err) = "ResizeImage: dst is null.";
    return false;
  }
  if (src.width <= 0 || src.height <= 0 || src.channels <= 0) {
    if (err) (*err) = "ResizeImage: invalid source image.";
    return false;
  }
  if (src.bpp != 8) {
    if (err) (*err) = "ResizeImage: only 8-bit per channel images are supported.";
    return false;
  }
  if (dstWidth <= 0 || dstHeight <= 0) {
    if (err) (*err) = "ResizeImage: invalid destination size.";
    return false;
  }

  // Validate source buffer size.
  size_t src_expected;
  if (!safe::mul3(size_t(src.width), size_t(src.height), size_t(src.channels),
                  &src_expected)) {
    if (err) (*err) = "ResizeImage: source size overflow.";
    return false;
  }
  if (src.data.size() < src_expected) {
    if (err) (*err) = "ResizeImage: source buffer too small.";
    return false;
  }

  Image out;
  out.width = dstWidth;
  out.height = dstHeight;
  out.channels = src.channels;
  out.bpp = 8;
  out.format = src.format;
  out.colorspace = src.colorspace;
  out.uri = src.uri;

  size_t dst_size;
  if (!safe::mul3(size_t(dstWidth), size_t(dstHeight), size_t(src.channels),
                  &dst_size)) {
    if (err) (*err) = "ResizeImage: destination size overflow.";
    return false;
  }
  out.data.resize(dst_size);

  const stbir_pixel_layout layout = PixelLayoutFromChannels(src.channels);

  bool use_srgb = false;
  if (filter == ResizeFilter::SRGB) {
    use_srgb = true;
  } else if (filter == ResizeFilter::Auto) {
    use_srgb = LooksSRGB(src.colorspace);
  }

  // No-op fast path.
  if ((dstWidth == src.width) && (dstHeight == src.height)) {
    std::copy(src.data.begin(), src.data.begin() + std::ptrdiff_t(dst_size),
              out.data.begin());
    (*dst) = std::move(out);
    return true;
  }

  void *r = nullptr;
  if (use_srgb) {
    r = stbir_resize_uint8_srgb(src.data.data(), src.width, src.height, 0,
                                out.data.data(), dstWidth, dstHeight, 0, layout);
  } else {
    r = stbir_resize_uint8_linear(src.data.data(), src.width, src.height, 0,
                                  out.data.data(), dstWidth, dstHeight, 0,
                                  layout);
  }
  if (r == nullptr) {
    if (err) (*err) = "ResizeImage: stb resize failed.";
    return false;
  }

  (*dst) = std::move(out);
  return true;
}

bool PackChannels(const std::vector<Image> &inputs, const ChannelPackSpec &spec,
                  Image *dst, std::string *err) {
  if (dst == nullptr) {
    if (err) (*err) = "PackChannels: dst is null.";
    return false;
  }
  if (spec.out_channels < 1 || spec.out_channels > 4) {
    if (err) (*err) = "PackChannels: out_channels must be 1-4.";
    return false;
  }

  const ChannelSource *slots[4] = {&spec.r, &spec.g, &spec.b, &spec.a};

  // Validate referenced inputs and determine output dims.
  int out_w = spec.out_width;
  int out_h = spec.out_height;
  for (int c = 0; c < spec.out_channels; c++) {
    const ChannelSource &cs = *slots[c];
    if (cs.input_index < 0) {
      continue;
    }
    if (size_t(cs.input_index) >= inputs.size()) {
      if (err) (*err) = "PackChannels: input_index out of range.";
      return false;
    }
    const Image &im = inputs[size_t(cs.input_index)];
    if (im.bpp != 8 || im.width <= 0 || im.height <= 0 || im.channels <= 0) {
      if (err) (*err) = "PackChannels: referenced input must be a valid 8-bit image.";
      return false;
    }
    if (cs.channel < 0 || cs.channel >= im.channels) {
      if (err) (*err) = "PackChannels: channel index out of range for input.";
      return false;
    }
    if (out_w == 0) out_w = im.width;
    if (out_h == 0) out_h = im.height;
    out_w = (std::max)(out_w, im.width);
    out_h = (std::max)(out_h, im.height);
  }

  if (out_w <= 0 || out_h <= 0) {
    if (err) (*err) = "PackChannels: could not determine output size (no inputs and no explicit size).";
    return false;
  }

  // Resize each referenced input to (out_w, out_h) once and cache by index.
  std::vector<Image> resized(inputs.size());
  std::vector<bool> have_resized(inputs.size(), false);
  for (int c = 0; c < spec.out_channels; c++) {
    const ChannelSource &cs = *slots[c];
    if (cs.input_index < 0) continue;
    size_t idx = size_t(cs.input_index);
    if (have_resized[idx]) continue;
    const Image &im = inputs[idx];
    if (im.width == out_w && im.height == out_h) {
      resized[idx] = im;
    } else {
      // Data maps: resize in linear space to avoid gamma shifting data values.
      if (!ResizeImage(im, out_w, out_h, &resized[idx], ResizeFilter::Linear,
                       err)) {
        return false;
      }
    }
    have_resized[idx] = true;
  }

  Image out;
  out.width = out_w;
  out.height = out_h;
  out.channels = spec.out_channels;
  out.bpp = 8;
  out.format = Image::PixelFormat::UInt;

  size_t out_size;
  if (!safe::mul3(size_t(out_w), size_t(out_h), size_t(spec.out_channels),
                  &out_size)) {
    if (err) (*err) = "PackChannels: output size overflow.";
    return false;
  }
  out.data.resize(out_size);

  size_t npixels;
  if (!safe::mul(size_t(out_w), size_t(out_h), &npixels)) {
    if (err) (*err) = "PackChannels: pixel count overflow.";
    return false;
  }

  for (size_t i = 0; i < npixels; i++) {
    for (int c = 0; c < spec.out_channels; c++) {
      const ChannelSource &cs = *slots[c];
      uint8_t v = cs.constant;
      if (cs.input_index >= 0) {
        const Image &im = resized[size_t(cs.input_index)];
        v = im.data[i * size_t(im.channels) + size_t(cs.channel)];
      }
      out.data[i * size_t(spec.out_channels) + size_t(c)] = v;
    }
  }

  (*dst) = std::move(out);
  return true;
}

namespace {

// Encode one texture at a given dimension cap (Size strategy) or JPEG quality
// (Quality strategy). Returns the encoded bytes + final ext/dims.
bool EncodeFitTexture(const FitTextureInput &in, const FitTextureOptions &opts,
                      FitStrategy strategy, int dimCap, int quality,
                      FitTextureOutput *out) {
  Image img = in.image;

  // Apply a longest-edge cap when requested.
  if (dimCap > 0) {
    const int longest = (std::max)(img.width, img.height);
    if (longest > dimCap) {
      const double s = double(dimCap) / double(longest);
      const int nw = (std::max)(1, int(img.width * s + 0.5));
      const int nh = (std::max)(1, int(img.height * s + 0.5));
      Image resized;
      std::string rerr;
      if (ResizeImage(img, nw, nh, &resized, ResizeFilter::Auto, &rerr)) {
        img = std::move(resized);
      }
    }
  }

  image::WriteOption wopt;
  wopt.png_encoder = opts.png_encoder;

  std::string ext;
  if (strategy == FitStrategy::Quality) {
    // Transcode to JPEG.
    wopt.format = image::WriteImageFormat::JPEG;
    wopt.jpeg_quality = quality;
    ext = "jpg";
    if (img.channels == 4) {
      Image rgb;
      rgb.width = img.width; rgb.height = img.height; rgb.channels = 3;
      rgb.bpp = 8; rgb.format = img.format; rgb.colorspace = img.colorspace;
      rgb.data.resize(size_t(img.width) * size_t(img.height) * 3);
      for (size_t i = 0; i < size_t(img.width) * size_t(img.height); i++) {
        rgb.data[3 * i + 0] = img.data[4 * i + 0];
        rgb.data[3 * i + 1] = img.data[4 * i + 1];
        rgb.data[3 * i + 2] = img.data[4 * i + 2];
      }
      img = std::move(rgb);
    }
  } else {
    // Size strategy: keep the source format (png stays png, jpg stays jpg).
    if (in.ext == "jpg" || in.ext == "jpeg") {
      wopt.format = image::WriteImageFormat::JPEG;
      wopt.jpeg_quality = opts.jpeg_quality;
      ext = "jpg";
      if (img.channels == 4) {
        Image rgb;
        rgb.width = img.width; rgb.height = img.height; rgb.channels = 3;
        rgb.bpp = 8; rgb.format = img.format; rgb.colorspace = img.colorspace;
        rgb.data.resize(size_t(img.width) * size_t(img.height) * 3);
        for (size_t i = 0; i < size_t(img.width) * size_t(img.height); i++) {
          rgb.data[3 * i + 0] = img.data[4 * i + 0];
          rgb.data[3 * i + 1] = img.data[4 * i + 1];
          rgb.data[3 * i + 2] = img.data[4 * i + 2];
        }
        img = std::move(rgb);
      }
    } else {
      wopt.format = image::WriteImageFormat::PNG;
      ext = "png";
    }
  }

  auto enc = image::WriteImageToMemory(img, wopt);
  if (!enc) {
    return false;
  }
  out->bytes = std::move(enc.value());
  out->ext = ext;
  out->width = img.width;
  out->height = img.height;
  out->changed = true;
  return true;
}

}  // namespace

bool FitTexturesToBudget(const std::vector<FitTextureInput> &inputs,
                         const FitTextureOptions &opts,
                         std::vector<FitTextureOutput> *out, std::string *warn,
                         std::string *err) {
  if (out == nullptr) {
    if (err) (*err) = "FitTexturesToBudget: out is null.";
    return false;
  }
  out->assign(inputs.size(), FitTextureOutput{});

  // Indices of textures we can shrink, and the fixed overhead of the rest.
  std::vector<size_t> idx;
  size_t fixed = 0;
  int maxOrigDim = 1;
  for (size_t i = 0; i < inputs.size(); i++) {
    const auto &in = inputs[i];
    if (in.reencodable && in.image.bpp == 8 && in.image.width > 0 &&
        in.image.height > 0) {
      idx.push_back(i);
      maxOrigDim = (std::max)(maxOrigDim, (std::max)(in.image.width, in.image.height));
    } else {
      // Keep original bytes; count as fixed overhead.
      (*out)[i].bytes = in.original_bytes;
      (*out)[i].ext = in.ext;
      (*out)[i].width = in.image.width;
      (*out)[i].height = in.image.height;
      (*out)[i].changed = false;
      fixed += in.original_bytes.size();
    }
  }

  if (opts.start_max_size > 0) {
    maxOrigDim = (std::min)(maxOrigDim, opts.start_max_size);
  }

  const size_t budget =
      (opts.target_total_bytes > fixed) ? (opts.target_total_bytes - fixed) : 0;

  // Helper: encode all shrinkable textures at a knob, return total bytes.
  std::vector<FitTextureOutput> probe(idx.size());
  auto encodeAll = [&](int dimCap, int quality, size_t *total) -> bool {
    size_t sum = 0;
    for (size_t k = 0; k < idx.size(); k++) {
      if (!EncodeFitTexture(inputs[idx[k]], opts, opts.strategy, dimCap, quality,
                            &probe[k])) {
        return false;
      }
      sum += probe[k].bytes.size();
    }
    *total = sum;
    return true;
  };

  // Binary search the lever for the best value that fits `budget`.
  // Size:    knob = dimension cap in [min_texture_size, maxOrigDim] (higher = bigger).
  // Quality: knob = JPEG quality in [min_jpeg_quality, 95]      (higher = bigger).
  int lo, hi, bestKnob;
  if (opts.strategy == FitStrategy::Quality) {
    lo = (std::max)(1, opts.min_jpeg_quality);
    hi = 95;
    bestKnob = lo;
  } else {
    lo = (std::max)(1, opts.min_texture_size);
    hi = (std::max)(lo, maxOrigDim);
    bestKnob = lo;
  }

  size_t total = 0;
  // If the largest setting already fits, take it (no shrink needed beyond cap).
  {
    int dimCap = (opts.strategy == FitStrategy::Quality) ? opts.start_max_size : hi;
    int quality = (opts.strategy == FitStrategy::Quality) ? hi : opts.jpeg_quality;
    if (!encodeAll(dimCap, quality, &total)) {
      if (err) (*err) = "FitTexturesToBudget: texture encode failed.";
      return false;
    }
    if (budget == 0 || total <= budget) {
      bestKnob = hi;
    } else {
      // Search downward for the largest knob that fits.
      int a = lo, b = hi;
      bestKnob = lo;
      // Up to ~16 iterations (converges for any practical range).
      for (int it = 0; it < 16 && a <= b; it++) {
        int mid = a + (b - a) / 2;
        int dCap = (opts.strategy == FitStrategy::Quality) ? opts.start_max_size : mid;
        int q = (opts.strategy == FitStrategy::Quality) ? mid : opts.jpeg_quality;
        size_t t = 0;
        if (!encodeAll(dCap, q, &t)) {
          if (err) (*err) = "FitTexturesToBudget: texture encode failed.";
          return false;
        }
        if (t <= budget) {
          bestKnob = mid;  // fits; try larger
          a = mid + 1;
        } else {
          b = mid - 1;     // too big; go smaller
        }
      }
    }
  }

  // Final encode at the chosen knob and fill outputs.
  {
    int dimCap = (opts.strategy == FitStrategy::Quality) ? opts.start_max_size : bestKnob;
    int quality = (opts.strategy == FitStrategy::Quality) ? bestKnob : opts.jpeg_quality;
    if (!encodeAll(dimCap, quality, &total)) {
      if (err) (*err) = "FitTexturesToBudget: texture encode failed.";
      return false;
    }
    for (size_t k = 0; k < idx.size(); k++) {
      (*out)[idx[k]] = std::move(probe[k]);
    }
  }

  if (opts.target_total_bytes > 0 && (total + fixed) > opts.target_total_bytes) {
    if (warn) {
      (*warn) += "Could not fit textures within the target budget (" +
                 std::to_string(opts.target_total_bytes) +
                 " bytes); using the smallest allowed setting (~" +
                 std::to_string(total + fixed) + " bytes).\n";
    }
  }

  return true;
}

} // namespace tydra
} // namespace tinyusdz
