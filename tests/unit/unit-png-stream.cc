#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "image-loader.hh"
#include "image-writer.hh"
#include "imageio/png-stream.hh"
#include "tydra/texture-util.hh"

#include <cstdint>
#include <vector>

using namespace tinyusdz;

namespace {

// Build a deterministic test image with `ch` channels (8-bit).
tinyusdz::Image MakeImage(int w, int h, int ch) {
  tinyusdz::Image img;
  img.width = w;
  img.height = h;
  img.channels = ch;
  img.bpp = 8;
  img.format = tinyusdz::Image::PixelFormat::UInt;
  img.data.resize((size_t)w * h * ch);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      for (int c = 0; c < ch; ++c) {
        img.data[((size_t)y * w + x) * ch + c] =
            (uint8_t)((x * 7 + y * 13 + c * 53) & 0xFF);
      }
    }
  }
  return img;
}

std::vector<uint8_t> EncodePNG(const tinyusdz::Image &img) {
  tinyusdz::image::WriteOption opt;
  opt.format = tinyusdz::image::WriteImageFormat::PNG;
  auto r = tinyusdz::image::WriteImageToMemory(img, opt);
  TEST_CHECK(bool(r));
  if (!r) return {};
  return r.value();
}

// Decode `png` and compare RGBA-normalized pixels against `ref`.
void CheckPixelsEqual(const std::vector<uint8_t> &png,
                      const tinyusdz::Image &ref) {
  auto dec = tinyusdz::image::LoadImageFromMemory(png.data(), png.size(), "mem");
  TEST_CHECK(bool(dec));
  if (!dec) return;
  const auto &im = dec.value().image;
  TEST_CHECK(im.width == ref.width);
  TEST_CHECK(im.height == ref.height);
  // Compare only the channels present in the reference (decoders may expand to
  // RGBA). Sample per-pixel up to ref.channels.
  int rc = ref.channels;
  int dc = im.channels;
  bool ok = true;
  for (int y = 0; y < ref.height && ok; ++y) {
    for (int x = 0; x < ref.width && ok; ++x) {
      for (int c = 0; c < rc; ++c) {
        uint8_t a = ref.data[((size_t)y * ref.width + x) * rc + c];
        uint8_t b = im.data[((size_t)y * im.width + x) * dc + c];
        if (a != b) { ok = false; break; }
      }
    }
  }
  TEST_CHECK(ok);
}

void TranscodeRoundtrip(int w, int h, int ch) {
  tinyusdz::Image img = MakeImage(w, h, ch);
  std::vector<uint8_t> png = EncodePNG(img);
  TEST_CHECK(png.size() > 0);
  std::vector<uint8_t> out;
  bool ok = tinyusdz::imageio::TranscodePNG(png.data(), png.size(), out);
  TEST_CHECK(ok);
  if (!ok) return;
  TEST_CHECK(out.size() > 0);
  // Transcoded PNG must decode to the same pixels as the original image.
  CheckPixelsEqual(out, img);
}

}  // namespace

void png_stream_transcode_rgba_test(void) { TranscodeRoundtrip(67, 41, 4); }
void png_stream_transcode_rgb_test(void) { TranscodeRoundtrip(64, 64, 3); }
void png_stream_transcode_gray_test(void) { TranscodeRoundtrip(33, 50, 1); }

// Drive the reader/writer directly (fresh-mode) and confirm a pixel-faithful
// round-trip — this is the path the resize/colorspace pipeline will use.
void png_stream_reader_writer_roundtrip_test(void) {
  tinyusdz::Image img = MakeImage(40, 24, 4);
  std::vector<uint8_t> png = EncodePNG(img);
  TEST_CHECK(png.size() > 0);

  tinyusdz::imageio::PngScanlineReader reader;
  TEST_CHECK(reader.Open(png.data(), png.size()));
  tinyusdz::imageio::PngScanlineWriter writer;
  TEST_CHECK(writer.Begin(reader.info()));

  std::vector<uint8_t> row(reader.info().row_bytes);
  uint32_t rows = 0;
  while (reader.NextRow(row.data())) {
    TEST_CHECK(writer.WriteRow(row.data()));
    ++rows;
  }
  TEST_CHECK(reader.ok());
  TEST_CHECK(rows == img.height);
  std::vector<uint8_t> out;
  TEST_CHECK(writer.Finish(out));
  CheckPixelsEqual(out, img);
}

void png_stream_reject_nonpng_test(void) {
  const uint8_t junk[16] = {0xFF, 0xD8, 0xFF, 0xE0, 1, 2, 3, 4,
                            5,    6,    7,    8,    9, 10, 11, 12};  // JPEG sig
  std::vector<uint8_t> out;
  bool ok = tinyusdz::imageio::TranscodePNG(junk, sizeof(junk), out);
  TEST_CHECK(!ok);  // must reject -> caller falls back to whole-image codec
}

// Streaming resize must match the whole-image tydra::ResizeImage (same stbir lib
// + same linear filter) to within rounding.
static void ResizeParity(int w, int h, int ch, int tw, int th) {
  tinyusdz::Image img = MakeImage(w, h, ch);
  std::vector<uint8_t> png = EncodePNG(img);
  TEST_CHECK(png.size() > 0);

  // Streaming resize -> decode result.
  std::vector<uint8_t> rpng;
  bool ok = tinyusdz::imageio::ResizePNG(png.data(), png.size(), (uint32_t)tw,
                                         (uint32_t)th, /*srgb=*/false, rpng);
  TEST_CHECK(ok);
  if (!ok) return;
  auto dec = tinyusdz::image::LoadImageFromMemory(rpng.data(), rpng.size(), "m");
  TEST_CHECK(bool(dec));
  if (!dec) return;
  const auto &streamed = dec.value().image;
  TEST_CHECK(streamed.width == tw && streamed.height == th);

  // Whole-image reference.
  tinyusdz::Image ref;
  std::string err;
  bool rok = tinyusdz::tydra::ResizeImage(img, tw, th, &ref,
                                          tinyusdz::tydra::ResizeFilter::Linear,
                                          &err);
  TEST_CHECK(rok);
  if (!rok) return;

  // Compare ref channels against the (possibly RGBA-expanded) decoded result.
  int dc = streamed.channels;
  int maxdiff = 0;
  for (int y = 0; y < th; ++y)
    for (int x = 0; x < tw; ++x)
      for (int c = 0; c < ch; ++c) {
        int a = ref.data[((size_t)y * tw + x) * ch + c];
        int b = streamed.data[((size_t)y * tw + x) * dc + c];
        int d = a > b ? a - b : b - a;
        if (d > maxdiff) maxdiff = d;
      }
  TEST_CHECK(maxdiff <= 1);  // streaming vs whole-image: identical within 1 LSB
}

// 16-bit grayscale PNG transcode must be lossless (byte-wise codec). The writer
// (fresh mode) builds the 16-bit PNG so the test needs no external 16-bit codec.
void png_stream_transcode_16bit_test(void) {
  const uint32_t W = 50, H = 37;
  tinyusdz::imageio::PngImageInfo info;
  info.width = W;
  info.height = H;
  info.bit_depth = 16;
  info.color_type = 0;  // grayscale
  tinyusdz::imageio::PngScanlineWriter w;
  TEST_CHECK(w.Begin(info));
  std::vector<std::vector<uint8_t>> rows(H, std::vector<uint8_t>((size_t)W * 2));
  for (uint32_t y = 0; y < H; ++y) {
    for (uint32_t x = 0; x < W; ++x) {
      uint16_t v = (uint16_t)((x * 257 + y * 131) & 0xFFFF);
      rows[y][x * 2] = (uint8_t)(v >> 8);       // big-endian (PNG)
      rows[y][x * 2 + 1] = (uint8_t)(v & 0xFF);
    }
    TEST_CHECK(w.WriteRow(rows[y].data()));
  }
  std::vector<uint8_t> png16;
  TEST_CHECK(w.Finish(png16));

  std::vector<uint8_t> out;
  bool ok = tinyusdz::imageio::TranscodePNG(png16.data(), png16.size(), out);
  TEST_CHECK(ok);
  if (!ok) return;

  tinyusdz::imageio::PngScanlineReader r;
  TEST_CHECK(r.Open(out.data(), out.size()));
  TEST_CHECK(r.info().bit_depth == 16 && r.info().color_type == 0 &&
             r.info().width == W && r.info().height == H);
  std::vector<uint8_t> row(r.info().row_bytes);
  bool good = true;
  for (uint32_t y = 0; y < H && good; ++y) {
    if (!r.NextRow(row.data())) { good = false; break; }
    if (row != rows[y]) good = false;  // lossless: identical 16-bit samples
  }
  TEST_CHECK(good);
}

void png_stream_resize_rgb_test(void) { ResizeParity(128, 96, 3, 40, 33); }
void png_stream_resize_rgba_test(void) { ResizeParity(100, 100, 4, 50, 25); }

void png_stream_colorspace_test(void) {
  const int w = 64, h = 48, ch = 4;  // RGBA: convert RGB, preserve alpha
  tinyusdz::Image img = MakeImage(w, h, ch);
  std::vector<uint8_t> png = EncodePNG(img);
  TEST_CHECK(png.size() > 0);

  std::vector<uint8_t> out;
  bool ok = tinyusdz::imageio::ConvertColorspacePNG(
      png.data(), png.size(), tinyusdz::imageio::ColorspaceXform::SrgbToLinear,
      out);
  TEST_CHECK(ok);
  if (!ok) return;
  auto dec = tinyusdz::image::LoadImageFromMemory(out.data(), out.size(), "m");
  TEST_CHECK(bool(dec));
  if (!dec) return;
  const auto &res = dec.value().image;
  TEST_CHECK(res.width == w && res.height == h);

  // Reference LUT (same formula as the implementation).
  uint8_t lut[256];
  for (int i = 0; i < 256; ++i) {
    float c = float(i) / 255.0f;
    float o = (c <= 0.04045f) ? (c / 12.92f)
                              : std::pow((c + 0.055f) / 1.055f, 2.4f);
    int v = int(o * 255.0f + 0.5f);
    lut[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
  }
  int dc = res.channels;
  bool good = true;
  for (int p = 0; p < w * h && good; ++p) {
    for (int c = 0; c < 3; ++c) {  // RGB transformed
      if (res.data[p * dc + c] != lut[img.data[p * ch + c]]) { good = false; break; }
    }
    if (dc == 4 && res.data[p * dc + 3] != img.data[p * ch + 3]) good = false;  // alpha preserved
  }
  TEST_CHECK(good);
}
