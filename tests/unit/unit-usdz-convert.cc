// SPDX-License-Identifier: Apache 2.0
// usdz-convert pipeline + texture op + fpnge unit tests

#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-usdz-convert.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "tinyusdz.hh"
#include "image-types.hh"
#include "image-loader.hh"
#include "image-writer.hh"
#include "io-util.hh"
#include "tydra/texture-util.hh"
#include "usdz-convert.hh"
#include "usdShade.hh"

namespace {

tinyusdz::Image MakeSolidImage(int w, int h, int channels, uint8_t r, uint8_t g,
                               uint8_t b, uint8_t a) {
  tinyusdz::Image img;
  img.width = w;
  img.height = h;
  img.channels = channels;
  img.bpp = 8;
  img.format = tinyusdz::Image::PixelFormat::UInt;
  img.data.resize(size_t(w) * size_t(h) * size_t(channels));
  for (int i = 0; i < w * h; i++) {
    uint8_t px[4] = {r, g, b, a};
    for (int c = 0; c < channels; c++) {
      img.data[size_t(i) * size_t(channels) + size_t(c)] = px[c];
    }
  }
  return img;
}

// Deterministic high-entropy image so PNG/JPEG sizes are non-trivial.
tinyusdz::Image MakeNoisyImage(int w, int h, int channels) {
  tinyusdz::Image img;
  img.width = w; img.height = h; img.channels = channels; img.bpp = 8;
  img.format = tinyusdz::Image::PixelFormat::UInt;
  img.data.resize(size_t(w) * size_t(h) * size_t(channels));
  uint32_t s = 0x12345678u;
  for (auto &b : img.data) {
    s = s * 1664525u + 1013904223u;  // LCG
    b = uint8_t(s >> 24);
  }
  return img;
}

std::vector<uint8_t> EncodePNG(const tinyusdz::Image &img) {
  tinyusdz::image::WriteOption wopt;
  wopt.format = tinyusdz::image::WriteImageFormat::PNG;
  auto enc = tinyusdz::image::WriteImageToMemory(img, wopt);
  return enc ? enc.value() : std::vector<uint8_t>{};
}

std::string TempDir() {
  namespace fs = std::filesystem;
  fs::path base = fs::temp_directory_path() / "tusdzconvert_test";
  std::error_code ec;
  fs::create_directories(base, ec);
  return base.string();
}

bool WriteTexturedUSDA(const std::string &path, const std::string &texture_path) {
  const std::string usda =
      "#usda 1.0\n"
      "(\n"
      "    defaultPrim = \"root\"\n"
      "    upAxis = \"Y\"\n"
      ")\n"
      "\n"
      "def Xform \"root\"\n"
      "{\n"
      "    def Material \"mat\"\n"
      "    {\n"
      "        token outputs:surface.connect = </root/mat/surface.outputs:surface>\n"
      "        def Shader \"surface\"\n"
      "        {\n"
      "            uniform token info:id = \"UsdPreviewSurface\"\n"
      "            color3f inputs:diffuseColor.connect = </root/mat/tex.outputs:rgb>\n"
      "            token outputs:surface\n"
      "        }\n"
      "        def Shader \"tex\"\n"
      "        {\n"
      "            uniform token info:id = \"UsdUVTexture\"\n"
      "            asset inputs:file = @" + texture_path + "@\n"
      "            float3 outputs:rgb\n"
      "        }\n"
      "    }\n"
      "}\n";
  std::string werr;
  return tinyusdz::io::WriteWholeFile(
      path, reinterpret_cast<const unsigned char *>(usda.data()), usda.size(),
      &werr);
}

bool WriteTwoTexturedUSDA(const std::string &path, const std::string &tex0,
                          const std::string &tex1) {
  const std::string usda =
      "#usda 1.0\n"
      "(\n"
      "    defaultPrim = \"root\"\n"
      "    upAxis = \"Y\"\n"
      ")\n"
      "\n"
      "def Xform \"root\"\n"
      "{\n"
      "    def Material \"mat\"\n"
      "    {\n"
      "        token outputs:surface.connect = </root/mat/surface.outputs:surface>\n"
      "        def Shader \"surface\"\n"
      "        {\n"
      "            uniform token info:id = \"UsdPreviewSurface\"\n"
      "            color3f inputs:diffuseColor.connect = </root/mat/tex0.outputs:rgb>\n"
      "            token outputs:surface\n"
      "        }\n"
      "        def Shader \"tex0\"\n"
      "        {\n"
      "            uniform token info:id = \"UsdUVTexture\"\n"
      "            asset inputs:file = @" + tex0 + "@\n"
      "            float3 outputs:rgb\n"
      "        }\n"
      "        def Shader \"tex1\"\n"
      "        {\n"
      "            uniform token info:id = \"UsdUVTexture\"\n"
      "            asset inputs:file = @" + tex1 + "@\n"
      "            float3 outputs:rgb\n"
      "        }\n"
      "    }\n"
      "}\n";
  std::string werr;
  return tinyusdz::io::WriteWholeFile(
      path, reinterpret_cast<const unsigned char *>(usda.data()), usda.size(),
      &werr);
}

bool FindTextureFilePath(const tinyusdz::Prim &prim, std::string *out) {
  const tinyusdz::Shader *shd = prim.as<tinyusdz::Shader>();
  if (shd && shd->info_id == "UsdUVTexture") {
    const tinyusdz::UsdUVTexture *tex = shd->value.as<tinyusdz::UsdUVTexture>();
    if (tex) {
      const auto av = tex->file.get_value();
      if (av && av.value().is_scalar()) {
        tinyusdz::value::AssetPath ap;
        if (av.value().get_scalar(&ap)) {
          *out = ap.GetAssetPath();
          return true;
        }
      }
    }
  }
  for (const auto &child : prim.children()) {
    if (FindTextureFilePath(child, out)) {
      return true;
    }
  }
  return false;
}

bool FindTextureFilePath(const tinyusdz::Stage &stage, std::string *out) {
  for (const auto &prim : stage.root_prims()) {
    if (FindTextureFilePath(prim, out)) {
      return true;
    }
  }
  return false;
}

std::string FirstUSDZEntryName(const std::vector<uint8_t> &data) {
  if (data.size() < 30 ||
      data[0] != 0x50 || data[1] != 0x4b ||
      data[2] != 0x03 || data[3] != 0x04) {
    return std::string();
  }
  const uint16_t name_len =
      static_cast<uint16_t>(data[26]) |
      static_cast<uint16_t>(static_cast<uint16_t>(data[27]) << 8);
  if (size_t(30) + size_t(name_len) > data.size()) {
    return std::string();
  }
  return std::string(reinterpret_cast<const char *>(data.data() + 30),
                     name_len);
}

}  // namespace

// fpnge/fpng PNG encode -> decode roundtrip preserves pixels.
void usdz_convert_png_roundtrip_test(void) {
  using namespace tinyusdz;

  Image img = MakeSolidImage(16, 12, 4, 10, 20, 30, 40);
  // Add a varying pixel to avoid trivial all-same encoding.
  img.data[0] = 200;
  img.data[1] = 100;

  image::WriteOption wopt;
  wopt.format = image::WriteImageFormat::PNG;

  auto enc = image::WriteImageToMemory(img, wopt);
  TEST_CHECK(enc.has_value());
  if (!enc) {
    TEST_MSG("encode error: %s", enc.error().c_str());
    return;
  }
  TEST_CHECK(enc.value().size() > 8);

  auto dec = image::LoadImageFromMemory(enc.value().data(), enc.value().size(),
                                        "roundtrip.png");
  TEST_CHECK(dec.has_value());
  if (!dec) {
    TEST_MSG("decode error: %s", dec.error().c_str());
    return;
  }
  const Image &out = dec.value().image;
  TEST_CHECK(out.width == 16);
  TEST_CHECK(out.height == 12);
  TEST_CHECK(out.channels == 4);
  TEST_CHECK(out.data.size() == img.data.size());
  if (out.data.size() == img.data.size()) {
    TEST_CHECK(out.data[0] == 200);
    TEST_CHECK(out.data[1] == 100);
    TEST_CHECK(out.data[4] == 10);  // second pixel R
  }
}

// ResizeImage downsamples and preserves channel count.
void usdz_convert_resize_test(void) {
  using namespace tinyusdz;

  Image img = MakeSolidImage(8, 8, 4, 50, 60, 70, 255);
  Image out;
  std::string err;
  bool ok = tydra::ResizeImage(img, 4, 4, &out, tydra::ResizeFilter::Linear, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("resize error: %s", err.c_str());
    return;
  }
  TEST_CHECK(out.width == 4);
  TEST_CHECK(out.height == 4);
  TEST_CHECK(out.channels == 4);
  TEST_CHECK(out.data.size() == 4u * 4u * 4u);
  // Solid color should be preserved after resize.
  if (out.data.size() >= 4) {
    TEST_CHECK(out.data[0] == 50);
    TEST_CHECK(out.data[1] == 60);
    TEST_CHECK(out.data[2] == 70);
  }
}

// PackChannels merges two single-source images into R/G.
void usdz_convert_pack_channels_test(void) {
  using namespace tinyusdz;

  Image red = MakeSolidImage(4, 4, 1, 111, 0, 0, 0);    // 1ch value 111
  Image grn = MakeSolidImage(4, 4, 1, 222, 0, 0, 0);    // 1ch value 222

  std::vector<Image> inputs = {red, grn};
  tydra::ChannelPackSpec spec;
  spec.out_channels = 3;
  spec.r.input_index = 0;
  spec.r.channel = 0;
  spec.g.input_index = 1;
  spec.g.channel = 0;
  spec.b.input_index = -1;
  spec.b.constant = 7;

  Image packed;
  std::string err;
  bool ok = tydra::PackChannels(inputs, spec, &packed, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("pack error: %s", err.c_str());
    return;
  }
  TEST_CHECK(packed.width == 4);
  TEST_CHECK(packed.height == 4);
  TEST_CHECK(packed.channels == 3);
  if (packed.data.size() >= 3) {
    TEST_CHECK(packed.data[0] == 111);  // R from red
    TEST_CHECK(packed.data[1] == 222);  // G from green
    TEST_CHECK(packed.data[2] == 7);    // B constant
  }
}

// Empty ORM inputs should produce a scalar fallback texture. Missing maps are
// represented with empty buffers and zero channels by callers.
void usdz_convert_orm_scalar_fallback_test(void) {
  using namespace tinyusdz;

  std::vector<uint8_t> out;
  size_t out_w = 0;
  size_t out_h = 0;
  bool ok = tydra::BuildOcclusionRoughnessMetallicTexture(
      0.25f, 0.5f, 0.75f,
      {}, 0, 0, 0, 0,
      {}, 0, 0, 0, 0,
      {}, 0, 0, 0, 0,
      out, out_w, out_h);

  TEST_CHECK(ok);
  TEST_CHECK(out_w == 1);
  TEST_CHECK(out_h == 1);
  TEST_CHECK(out.size() == 3);
  if (out.size() == 3) {
    TEST_CHECK(out[0] == 63);
    TEST_CHECK(out[1] == 127);
    TEST_CHECK(out[2] == 191);
  }
}

void usdz_convert_orm_scalar_nonfinite_test(void) {
  using namespace tinyusdz;

  std::vector<uint8_t> out;
  size_t out_w = 0;
  size_t out_h = 0;
  bool ok = tydra::BuildOcclusionRoughnessMetallicTexture(
      std::numeric_limits<float>::infinity(),
      -1000.0f,
      std::numeric_limits<float>::quiet_NaN(),
      {}, 0, 0, 0, 0,
      {}, 0, 0, 0, 0,
      {}, 0, 0, 0, 0,
      out, out_w, out_h);

  TEST_CHECK(ok);
  TEST_CHECK(out_w == 1);
  TEST_CHECK(out_h == 1);
  TEST_CHECK(out.size() == 3);
  if (out.size() == 3) {
    TEST_CHECK(out[0] == 0);
    TEST_CHECK(out[1] == 0);
    TEST_CHECK(out[2] == 0);
  }
}

void usdz_convert_archive_collision_name_test(void) {
  using namespace tinyusdz;
  namespace fs = std::filesystem;

  const std::string dir = TempDir();
  const fs::path a_dir = fs::path(dir) / "collision_a";
  const fs::path b_dir = fs::path(dir) / "collision_b";
  std::error_code ec;
  fs::create_directories(a_dir, ec);
  fs::create_directories(b_dir, ec);
  const std::string tex_a = (a_dir / "same.png").string();
  const std::string tex_b = (b_dir / "same.png").string();
  const std::string usda_path = (fs::path(dir) / "scene_collision.usda").string();
  const std::string usdz_path = (fs::path(dir) / "out_collision.usdz").string();

  auto write_png = [](const std::string &path, const Image &img) -> bool {
    image::WriteOption wopt;
    wopt.format = image::WriteImageFormat::PNG;
    auto enc = image::WriteImageToMemory(img, wopt);
    if (!enc) return false;
    std::string werr;
    return io::WriteWholeFile(path, enc.value().data(), enc.value().size(),
                              &werr);
  };

  TEST_CHECK(write_png(tex_a, MakeSolidImage(4, 4, 4, 255, 0, 0, 255)));
  TEST_CHECK(write_png(tex_b, MakeSolidImage(4, 4, 4, 0, 255, 0, 255)));
  TEST_CHECK(WriteTwoTexturedUSDA(usda_path, tex_a, tex_b));

  usdz::UsdzConvertOptions opts;
  opts.inputs.push_back(usda_path);
  opts.output = usdz_path;
  opts.flatten = true;

  usdz::UsdzConvertStats stats;
  std::string warn, err;
  bool ok = usdz::Convert(opts, &stats, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("convert error: %s", err.c_str());
    return;
  }
  TEST_CHECK(stats.num_textures == 2);

  USDZAsset asset;
  std::string awarn, aerr;
  bool read_ok = ReadUSDZAssetInfoFromFile(usdz_path, &asset, &awarn, &aerr);
  TEST_CHECK(read_ok);
  if (read_ok) {
    TEST_CHECK(asset.asset_map.count("textures/same.png") == 1);
    TEST_CHECK(asset.asset_map.count("textures/same_1.png") == 1);
  }
}

// FitTexturesToBudget shrinks a set of textures to a tight byte budget.
void usdz_convert_fit_budget_test(void) {
  using namespace tinyusdz;

  std::vector<tydra::FitTextureInput> inputs;
  for (auto dim : {256, 512, 128}) {
    tydra::FitTextureInput fi;
    fi.image = MakeNoisyImage(dim, dim, 3);
    fi.original_bytes = EncodePNG(fi.image);
    fi.ext = "png";
    fi.reencodable = true;
    TEST_CHECK(!fi.original_bytes.empty());
    inputs.push_back(std::move(fi));
  }

  size_t fullTotal = 0;
  for (const auto &fi : inputs) fullTotal += fi.original_bytes.size();
  TEST_CHECK(fullTotal > 0);

  auto sumOut = [](const std::vector<tydra::FitTextureOutput> &outs) {
    size_t s = 0;
    for (const auto &o : outs) s += o.bytes.size();
    return s;
  };

  // --- Size strategy: tight budget should shrink dimensions. ---
  {
    tydra::FitTextureOptions opt;
    opt.target_total_bytes = fullTotal / 4;
    opt.strategy = tydra::FitStrategy::Size;
    opt.min_texture_size = 16;

    std::vector<tydra::FitTextureOutput> outs;
    std::string warn, err;
    bool ok = tydra::FitTexturesToBudget(inputs, opt, &outs, &warn, &err);
    TEST_CHECK(ok);
    if (!ok) { TEST_MSG("fit(size) err: %s", err.c_str()); return; }
    TEST_CHECK(outs.size() == inputs.size());
    const size_t total = sumOut(outs);
    // Shrunk below full size, and ideally within budget.
    TEST_CHECK(total < fullTotal);
    for (const auto &o : outs) TEST_CHECK(o.ext == "png");
    // The largest (512) input must have been downscaled.
    TEST_CHECK(outs[1].width < 512);
  }

  // --- Quality strategy: outputs are JPEG and smaller than full PNG. ---
  {
    tydra::FitTextureOptions opt;
    opt.target_total_bytes = fullTotal / 4;
    opt.strategy = tydra::FitStrategy::Quality;
    opt.min_jpeg_quality = 20;

    std::vector<tydra::FitTextureOutput> outs;
    std::string warn, err;
    bool ok = tydra::FitTexturesToBudget(inputs, opt, &outs, &warn, &err);
    TEST_CHECK(ok);
    if (!ok) { TEST_MSG("fit(quality) err: %s", err.c_str()); return; }
    TEST_CHECK(outs.size() == inputs.size());
    for (const auto &o : outs) TEST_CHECK(o.ext == "jpg");
    TEST_CHECK(sumOut(outs) < fullTotal);
  }
}

// End-to-end: USDA + on-disk PNG -> resized/re-encoded -> USDZ (validated).
void usdz_convert_pipeline_test(void) {
  using namespace tinyusdz;
  namespace fs = std::filesystem;

  const std::string dir = TempDir();
  const std::string png_path = (fs::path(dir) / "tex.png").string();
  const std::string usda_path = (fs::path(dir) / "scene.usda").string();
  const std::string usdz_path = (fs::path(dir) / "out.usdz").string();

  // 1) Write a 64x64 PNG texture on disk.
  Image tex = MakeSolidImage(64, 64, 4, 200, 150, 100, 255);
  {
    image::WriteOption wopt;
    wopt.format = image::WriteImageFormat::PNG;
    auto enc = image::WriteImageToMemory(tex, wopt);
    TEST_CHECK(enc.has_value());
    if (!enc) return;
    std::string werr;
    bool wok = io::WriteWholeFile(png_path, enc.value().data(),
                                  enc.value().size(), &werr);
    TEST_CHECK(wok);
    if (!wok) {
      TEST_MSG("write png: %s", werr.c_str());
      return;
    }
  }

  // 2) Write a USDA referencing the texture.
  {
    const std::string usda =
        "#usda 1.0\n"
        "(\n"
        "    defaultPrim = \"root\"\n"
        "    upAxis = \"Y\"\n"
        ")\n"
        "\n"
        "def Xform \"root\"\n"
        "{\n"
        "    def Material \"mat\"\n"
        "    {\n"
        "        token outputs:surface.connect = </root/mat/surface.outputs:surface>\n"
        "\n"
        "        def Shader \"surface\"\n"
        "        {\n"
        "            uniform token info:id = \"UsdPreviewSurface\"\n"
        "            color3f inputs:diffuseColor.connect = </root/mat/tex.outputs:rgb>\n"
        "            token outputs:surface\n"
        "        }\n"
        "\n"
        "        def Shader \"tex\"\n"
        "        {\n"
        "            uniform token info:id = \"UsdUVTexture\"\n"
        "            asset inputs:file = @tex.png@\n"
        "            float3 outputs:rgb\n"
        "        }\n"
        "    }\n"
        "}\n";
    std::string werr;
    bool wok = io::WriteWholeFile(
        usda_path, reinterpret_cast<const unsigned char *>(usda.data()),
        usda.size(), &werr);
    TEST_CHECK(wok);
    if (!wok) {
      TEST_MSG("write usda: %s", werr.c_str());
      return;
    }
  }

  // 3) Convert: flatten + resize textures to 32px + force PNG (fpnge).
  usdz::UsdzConvertOptions opts;
  opts.inputs.push_back(usda_path);
  opts.output = usdz_path;
  opts.flatten = true;
  opts.arkit_compatible = true;
  opts.max_texture_size = 32;
  opts.texture_format = usdz::OutputTextureFormat::PNG;

  usdz::UsdzConvertStats stats;
  std::string warn, err;
  bool ok = usdz::Convert(opts, &stats, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("convert error: %s", err.c_str());
    return;
  }

  TEST_CHECK(stats.num_textures >= 1);
  TEST_CHECK(stats.num_textures_resized >= 1);

  // 4) Read back + validate the USDZ.
  std::vector<uint8_t> usdz_bytes;
  std::string ioerr;
  bool rok = io::ReadWholeFile(&usdz_bytes, &ioerr, usdz_path, 0);
  TEST_CHECK(rok);
  if (!rok) {
    TEST_MSG("read usdz: %s", ioerr.c_str());
    return;
  }
  std::string vwarn, verr;
  bool valid = tinyusdz::ValidateUSDZ(usdz_bytes.data(), usdz_bytes.size(),
                                      &vwarn, &verr);
  TEST_CHECK(valid);
  if (!valid) {
    TEST_MSG("validate: %s", verr.c_str());
  }
}

void usdz_convert_usdz_root_layer_format_test(void) {
  using namespace tinyusdz;
  namespace fs = std::filesystem;

  const std::string dir = TempDir();
  const std::string png_path = (fs::path(dir) / "root_format_tex.png").string();
  const std::string usda_path =
      (fs::path(dir) / "root_format_scene.usda").string();
  const std::string usdz_path =
      (fs::path(dir) / "root_format_out.usdz").string();

  {
    Image tex = MakeSolidImage(4, 4, 4, 20, 40, 60, 255);
    image::WriteOption wopt;
    wopt.format = image::WriteImageFormat::PNG;
    auto enc = image::WriteImageToMemory(tex, wopt);
    TEST_CHECK(enc.has_value());
    if (!enc) return;
    std::string werr;
    TEST_CHECK(io::WriteWholeFile(png_path, enc.value().data(),
                                  enc.value().size(), &werr));
  }

  TEST_CHECK(WriteTexturedUSDA(usda_path, "root_format_tex.png"));

  usdz::UsdzConvertOptions opts;
  opts.inputs.push_back(usda_path);
  opts.output = usdz_path;
  opts.flatten = true;
  opts.usdz_root_layer_format = usdz::USDZRootLayerFormat::USDA;

  usdz::UsdzConvertStats stats;
  std::string warn, err;
  bool ok = usdz::Convert(opts, &stats, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("convert error: %s", err.c_str());
    return;
  }
  TEST_CHECK(stats.num_textures == 1);

  std::vector<uint8_t> usdz_bytes;
  std::string ioerr;
  bool rok = io::ReadWholeFile(&usdz_bytes, &ioerr, usdz_path, 0);
  TEST_CHECK(rok);
  if (!rok) {
    TEST_MSG("read usdz: %s", ioerr.c_str());
    return;
  }

  TEST_CHECK(FirstUSDZEntryName(usdz_bytes) == "root.usda");
  std::string vwarn, verr;
  TEST_CHECK(ValidateUSDZ(usdz_bytes.data(), usdz_bytes.size(), &vwarn,
                          &verr));

  Stage loaded_stage;
  std::string lwarn, lerr;
  TEST_CHECK(LoadUSDFromFile(usdz_path, &loaded_stage, &lwarn, &lerr));
}

void usdz_convert_arkit_forces_flattened_usdc_root_test(void) {
  using namespace tinyusdz;
  namespace fs = std::filesystem;

  const std::string dir = TempDir();
  const std::string png_path = (fs::path(dir) / "arkit_tex.png").string();
  const std::string usda_path =
      (fs::path(dir) / "arkit_scene.usda").string();
  const std::string usdz_path =
      (fs::path(dir) / "arkit_out.usdz").string();

  {
    Image tex = MakeSolidImage(4, 4, 4, 80, 90, 100, 255);
    image::WriteOption wopt;
    wopt.format = image::WriteImageFormat::PNG;
    auto enc = image::WriteImageToMemory(tex, wopt);
    TEST_CHECK(enc.has_value());
    if (!enc) return;
    std::string werr;
    TEST_CHECK(io::WriteWholeFile(png_path, enc.value().data(),
                                  enc.value().size(), &werr));
  }

  TEST_CHECK(WriteTexturedUSDA(usda_path, "arkit_tex.png"));

  usdz::UsdzConvertOptions opts;
  opts.inputs.push_back(usda_path);
  opts.output = usdz_path;
  opts.flatten = false;
  opts.arkit_compatible = true;
  opts.usdz_root_layer_format = usdz::USDZRootLayerFormat::USDA;

  usdz::UsdzConvertStats stats;
  std::string warn, err;
  bool ok = usdz::Convert(opts, &stats, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("convert error: %s", err.c_str());
    return;
  }
  TEST_CHECK(warn.find("requires a flattened package") != std::string::npos);
  TEST_CHECK(warn.find("requires a USDC root layer") != std::string::npos);

  std::vector<uint8_t> usdz_bytes;
  std::string ioerr;
  bool rok = io::ReadWholeFile(&usdz_bytes, &ioerr, usdz_path, 0);
  TEST_CHECK(rok);
  if (!rok) {
    TEST_MSG("read usdz: %s", ioerr.c_str());
    return;
  }

  TEST_CHECK(FirstUSDZEntryName(usdz_bytes) == "root.usdc");
  std::string vwarn, verr;
  TEST_CHECK(ValidateUSDZ(usdz_bytes.data(), usdz_bytes.size(), &vwarn,
                          &verr));
}

// Standalone RepackTextureFiles: two grayscale PNGs -> packed RG image.
void usdz_convert_repack_files_test(void) {
  using namespace tinyusdz;
  namespace fs = std::filesystem;

  const std::string dir = TempDir();
  const std::string a_path = (fs::path(dir) / "gloss.png").string();
  const std::string b_path = (fs::path(dir) / "rough.png").string();
  const std::string out_path = (fs::path(dir) / "packed.png").string();

  auto write_png = [](const std::string &path, const Image &img) -> bool {
    image::WriteOption wopt;
    wopt.format = image::WriteImageFormat::PNG;
    auto enc = image::WriteImageToMemory(img, wopt);
    if (!enc) return false;
    std::string werr;
    return io::WriteWholeFile(path, enc.value().data(), enc.value().size(),
                              &werr);
  };

  TEST_CHECK(write_png(a_path, MakeSolidImage(8, 8, 1, 90, 0, 0, 0)));
  TEST_CHECK(write_png(b_path, MakeSolidImage(8, 8, 1, 180, 0, 0, 0)));

  usdz::RepackSpec spec;
  spec.out_channels = 3;
  spec.r.input_file = a_path;
  spec.r.channel = 0;
  spec.g.input_file = b_path;
  spec.g.channel = 0;
  spec.b.input_file.clear();
  spec.b.constant = 0;

  std::string warn, err;
  bool ok = usdz::RepackTextureFiles(spec, out_path, image::PngEncoder::Auto,
                                     &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("repack error: %s", err.c_str());
    return;
  }

  // Decode the packed output and verify channel values.
  auto dec = image::LoadImageFromFile(out_path);
  TEST_CHECK(dec.has_value());
  if (!dec) return;
  const Image &p = dec.value().image;
  // The decoder may expand a 3-channel PNG to RGBA; the first pixel's R/G/B
  // are what matter for correctness.
  TEST_CHECK(p.channels >= 3);
  if (p.data.size() >= 3) {
    TEST_CHECK(p.data[0] == 90);   // R from gloss
    TEST_CHECK(p.data[1] == 180);  // G from roughness
    TEST_CHECK(p.data[2] == 0);    // B constant
  }
}

// JPEG encode -> decode roundtrip preserves approximate pixel values.
void usdz_convert_jpeg_roundtrip_test(void) {
  using namespace tinyusdz;

  // Use a solid-color image (JPEG is lossy, so avoid exact pixel comparisons
  // for high-frequency content).
  Image img = MakeSolidImage(16, 12, 3, 100, 150, 200, 255);

  image::WriteOption wopt;
  wopt.format = image::WriteImageFormat::JPEG;
  wopt.jpeg_quality = 95;

  auto enc = image::WriteImageToMemory(img, wopt);
  TEST_CHECK(enc.has_value());
  if (!enc) {
    TEST_MSG("jpeg encode error: %s", enc.error().c_str());
    return;
  }
  TEST_CHECK(enc.value().size() > 8);

  auto dec = image::LoadImageFromMemory(enc.value().data(), enc.value().size(),
                                        "roundtrip.jpg");
  TEST_CHECK(dec.has_value());
  if (!dec) {
    TEST_MSG("jpeg decode error: %s", dec.error().c_str());
    return;
  }
  const Image &out = dec.value().image;
  TEST_CHECK(out.width == 16);
  TEST_CHECK(out.height == 12);
  TEST_CHECK(out.channels >= 3);
  // Solid color should survive JPEG roundtrip at high quality (within
  // tolerance for lossy codec).
  if (out.data.size() >= 3) {
    // Allow +-10 for JPEG quantization on a solid color at q95.
    int dr = std::abs(int(out.data[0]) - 100);
    int dg = std::abs(int(out.data[1]) - 150);
    int db = std::abs(int(out.data[2]) - 200);
    TEST_CHECK(dr <= 10);
    TEST_CHECK(dg <= 10);
    TEST_CHECK(db <= 10);
  }
}

// RemapTextureAssetPaths handles an empty stage gracefully (returns 0).
void usdz_convert_remap_asset_paths_test(void) {
  using namespace tinyusdz;

  Stage stage;
  std::map<std::string, std::string> remap;
  remap["old_tex.png"] = "new_tex.jpg";

  // An empty stage has no textures, so count should be 0.
  size_t count = usdz::RemapTextureAssetPaths(stage, remap);
  TEST_CHECK(count == 0);
}

// Error-path: invalid inputs should return errors, not crash.
void usdz_convert_error_path_test(void) {
  using namespace tinyusdz;

  // 1) Convert with nonexistent input file.
  {
    usdz::UsdzConvertOptions opts;
    opts.inputs.push_back("/nonexistent/path/to/file.usda");
    opts.output = "/tmp/should_not_exist.usdz";
    usdz::UsdzConvertStats stats;
    std::string warn, err;
    bool ok = usdz::Convert(opts, &stats, &warn, &err);
    TEST_CHECK(!ok);
    TEST_CHECK(!err.empty());
  }

  // 2) Repack with invalid channel count.
  {
    usdz::RepackSpec spec;
    spec.out_channels = 5;  // invalid
    std::string warn, err;
    bool ok = usdz::RepackTextureFiles(spec, "/tmp/should_not_exist.png",
                                 image::PngEncoder::Auto, &warn, &err);
    TEST_CHECK(!ok);
    TEST_CHECK(!err.empty());
  }

  // 3) Resize with zero dimensions.
  {
    Image img = MakeSolidImage(4, 4, 4, 0, 0, 0, 255);
    Image out;
    std::string err;
    bool ok = tydra::ResizeImage(img, 0, 0, &out, tydra::ResizeFilter::Linear,
                                 &err);
    TEST_CHECK(!ok);
  }
}

// Adversarial: feed truncated/corrupted image data to decoders; must not crash.
void usdz_convert_adversarial_image_test(void) {
  using namespace tinyusdz;

  // 1) Truncated PNG (valid header, then garbage).
  {
    const uint8_t truncated_png[] = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
      0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
      0x00, 0x00, 0x00, 0x01
    };
    auto dec = image::LoadImageFromMemory(truncated_png, sizeof(truncated_png),
                                          "truncated.png");
    TEST_CHECK(!dec.has_value());
  }

  // 2) Truncated JPEG.
  {
    const uint8_t truncated_jpg[] = {0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10};
    auto dec = image::LoadImageFromMemory(truncated_jpg, sizeof(truncated_jpg),
                                          "truncated.jpg");
    TEST_CHECK(!dec.has_value());
  }

  // 3) Zero-length input.
  {
    auto dec = image::LoadImageFromMemory(nullptr, 0, "empty.png");
    TEST_CHECK(!dec.has_value());
  }

  // 4) Completely random bytes.
  {
    uint8_t garbage[64];
    for (int i = 0; i < 64; i++) garbage[i] = uint8_t(i * 37 + 13);
    auto dec = image::LoadImageFromMemory(garbage, sizeof(garbage),
                                          "garbage.png");
    TEST_CHECK(!dec.has_value());
  }

  // 5) ResizeImage with oversized destination.
  {
    Image img = MakeSolidImage(4, 4, 4, 0, 0, 0, 255);
    Image out;
    std::string err;
    bool ok = tydra::ResizeImage(img, 20000, 20000, &out,
                                 tydra::ResizeFilter::Linear, &err);
    TEST_CHECK(!ok);
    TEST_CHECK(!err.empty());
  }

  // 6) ResizeImage with negative destination dimensions.
  {
    Image img = MakeSolidImage(4, 4, 4, 0, 0, 0, 255);
    Image out;
    std::string err;
    bool ok = tydra::ResizeImage(img, -1, -1, &out,
                                 tydra::ResizeFilter::Linear, &err);
    TEST_CHECK(!ok);
  }
}

// PackChannels error paths.
void usdz_convert_pack_channels_error_test(void) {
  using namespace tinyusdz;

  // 1) Empty inputs vector.
  {
    std::vector<Image> inputs;
    tydra::ChannelPackSpec spec;
    spec.out_channels = 3;
    spec.r.input_index = 0;
    spec.r.channel = 0;
    Image packed;
    std::string err;
    bool ok = tydra::PackChannels(inputs, spec, &packed, &err);
    TEST_CHECK(!ok);
    TEST_CHECK(!err.empty());
  }

  // 2) out_channels = 0.
  {
    std::vector<Image> inputs = {MakeSolidImage(4, 4, 1, 10, 0, 0, 0)};
    tydra::ChannelPackSpec spec;
    spec.out_channels = 0;
    Image packed;
    std::string err;
    bool ok = tydra::PackChannels(inputs, spec, &packed, &err);
    TEST_CHECK(!ok);
  }

  // 3) out_channels = 5.
  {
    std::vector<Image> inputs = {MakeSolidImage(4, 4, 1, 10, 0, 0, 0)};
    tydra::ChannelPackSpec spec;
    spec.out_channels = 5;
    Image packed;
    std::string err;
    bool ok = tydra::PackChannels(inputs, spec, &packed, &err);
    TEST_CHECK(!ok);
  }

  // 4) Input index out of range.
  {
    std::vector<Image> inputs = {MakeSolidImage(4, 4, 1, 10, 0, 0, 0)};
    tydra::ChannelPackSpec spec;
    spec.out_channels = 1;
    spec.r.input_index = 99;
    spec.r.channel = 0;
    Image packed;
    std::string err;
    bool ok = tydra::PackChannels(inputs, spec, &packed, &err);
    TEST_CHECK(!ok);
  }

  // 5) 4-channel output with alpha.
  {
    Image r = MakeSolidImage(4, 4, 1, 50, 0, 0, 0);
    Image g = MakeSolidImage(4, 4, 1, 100, 0, 0, 0);
    Image b = MakeSolidImage(4, 4, 1, 150, 0, 0, 0);
    Image a = MakeSolidImage(4, 4, 1, 200, 0, 0, 0);
    std::vector<Image> inputs = {r, g, b, a};
    tydra::ChannelPackSpec spec;
    spec.out_channels = 4;
    spec.r.input_index = 0; spec.r.channel = 0;
    spec.g.input_index = 1; spec.g.channel = 0;
    spec.b.input_index = 2; spec.b.channel = 0;
    spec.a.input_index = 3; spec.a.channel = 0;
    Image packed;
    std::string err;
    bool ok = tydra::PackChannels(inputs, spec, &packed, &err);
    TEST_CHECK(ok);
    if (ok) {
      TEST_CHECK(packed.channels == 4);
      TEST_CHECK(packed.width == 4);
      TEST_CHECK(packed.height == 4);
      if (packed.data.size() >= 4) {
        TEST_CHECK(packed.data[0] == 50);
        TEST_CHECK(packed.data[1] == 100);
        TEST_CHECK(packed.data[2] == 150);
        TEST_CHECK(packed.data[3] == 200);
      }
    }
  }

  // 6) Same-size input with undersized data must be rejected before sampling.
  {
    Image bad = MakeSolidImage(4, 4, 1, 10, 0, 0, 0);
    bad.data.resize(1);
    std::vector<Image> inputs = {bad};
    tydra::ChannelPackSpec spec;
    spec.out_channels = 1;
    spec.r.input_index = 0;
    spec.r.channel = 0;
    Image packed;
    std::string err;
    bool ok = tydra::PackChannels(inputs, spec, &packed, &err);
    TEST_CHECK(!ok);
    TEST_CHECK(!err.empty());
  }

  // 7) Constant-only output with excessive dimensions must be capped.
  {
    usdz::RepackSpec spec;
    spec.out_channels = 1;
    spec.out_width = 20000;
    spec.out_height = 20000;
    spec.r.input_file.clear();
    spec.r.constant = 128;
    std::string warn, err;
    bool ok = usdz::RepackTextureFiles(spec, "/tmp/should_not_exist.png",
                                       image::PngEncoder::Auto, &warn, &err);
    TEST_CHECK(!ok);
    TEST_CHECK(!err.empty());
  }
}

// FitTexturesToBudget error paths and edge cases.
void usdz_convert_fit_budget_error_test(void) {
  using namespace tinyusdz;

  // 1) Empty inputs.
  {
    std::vector<tydra::FitTextureInput> inputs;
    tydra::FitTextureOptions opt;
    opt.target_total_bytes = 1000;
    opt.strategy = tydra::FitStrategy::Size;
    opt.min_texture_size = 16;
    std::vector<tydra::FitTextureOutput> outs;
    std::string warn, err;
    bool ok = tydra::FitTexturesToBudget(inputs, opt, &outs, &warn, &err);
    TEST_CHECK(ok);
    TEST_CHECK(outs.empty());
  }

  // 2) Non-reencodable textures only.
  {
    std::vector<tydra::FitTextureInput> inputs;
    for (int i = 0; i < 3; i++) {
      tydra::FitTextureInput fi;
      fi.image = MakeSolidImage(32, 32, 3, 100, 100, 100, 255);
      fi.original_bytes = EncodePNG(fi.image);
      fi.ext = "png";
      fi.reencodable = false;
      inputs.push_back(std::move(fi));
    }
    tydra::FitTextureOptions opt;
    opt.target_total_bytes = 100;
    opt.strategy = tydra::FitStrategy::Size;
    opt.min_texture_size = 8;
    std::vector<tydra::FitTextureOutput> outs;
    std::string warn, err;
    bool ok = tydra::FitTexturesToBudget(inputs, opt, &outs, &warn, &err);
    TEST_CHECK(ok);
    TEST_CHECK(outs.size() == inputs.size());
  }

  // 3) ResizeFilter::SRGB path.
  {
    Image img = MakeSolidImage(16, 8, 3, 128, 128, 128, 255);
    Image out;
    std::string err;
    bool ok = tydra::ResizeImage(img, 8, 4, &out,
                                 tydra::ResizeFilter::SRGB, &err);
    TEST_CHECK(ok);
    if (ok) {
      TEST_CHECK(out.width == 8);
      TEST_CHECK(out.height == 4);
      TEST_CHECK(out.channels == 3);
    }
  }

  // 4) ResizeFilter::Auto path.
  {
    Image img = MakeSolidImage(16, 16, 4, 128, 128, 128, 255);
    img.colorspace = "sRGB";
    Image out;
    std::string err;
    bool ok = tydra::ResizeImage(img, 8, 8, &out,
                                 tydra::ResizeFilter::Auto, &err);
    TEST_CHECK(ok);
    if (ok) {
      TEST_CHECK(out.width == 8);
      TEST_CHECK(out.height == 8);
    }
  }

  // 5) Fixed overhead over budget should still shrink reencodable textures
  // to the configured floor instead of keeping the largest setting.
  {
    std::vector<tydra::FitTextureInput> inputs;
    tydra::FitTextureInput shrinkable;
    shrinkable.image = MakeNoisyImage(128, 128, 3);
    shrinkable.ext = "png";
    shrinkable.reencodable = true;
    inputs.push_back(std::move(shrinkable));

    tydra::FitTextureInput fixed;
    fixed.original_bytes.assign(1024, uint8_t(7));
    fixed.ext = "exr";
    fixed.reencodable = false;
    inputs.push_back(std::move(fixed));

    tydra::FitTextureOptions opt;
    opt.target_total_bytes = 16;
    opt.strategy = tydra::FitStrategy::Size;
    opt.min_texture_size = 16;

    std::vector<tydra::FitTextureOutput> outs;
    std::string warn, err;
    bool ok = tydra::FitTexturesToBudget(inputs, opt, &outs, &warn, &err);
    TEST_CHECK(ok);
    if (ok) {
      TEST_CHECK(outs.size() == 2);
      TEST_CHECK(outs[0].width == 16);
      TEST_CHECK(outs[0].height == 16);
      TEST_CHECK(!warn.empty());
    }
  }
}

void usdz_convert_missing_texture_reference_test(void) {
  using namespace tinyusdz;
  namespace fs = std::filesystem;

  const std::string dir = TempDir();

  // A safe relative missing texture should remain unchanged, not be rewritten
  // to a sanitized archive path that is not actually present.
  {
    const std::string usda_path =
        (fs::path(dir) / "scene_missing_safe.usda").string();
    const std::string usdz_path =
        (fs::path(dir) / "out_missing_safe.usdz").string();
    TEST_CHECK(WriteTexturedUSDA(usda_path, "missing.png"));

    usdz::UsdzConvertOptions opts;
    opts.inputs.push_back(usda_path);
    opts.output = usdz_path;
    opts.flatten = true;

    usdz::UsdzConvertStats stats;
    std::string warn, err;
    bool ok = usdz::Convert(opts, &stats, &warn, &err);
    TEST_CHECK(ok);
    TEST_CHECK(!warn.empty());
    TEST_CHECK(stats.num_textures == 0);
    if (!ok) {
      TEST_MSG("convert error: %s", err.c_str());
      return;
    }

    Stage stage;
    std::string lwarn, lerr;
    bool loaded = LoadUSDFromFile(usdz_path, &stage, &lwarn, &lerr);
    TEST_CHECK(loaded);
    if (loaded) {
      std::string texture_path;
      TEST_CHECK(FindTextureFilePath(stage, &texture_path));
      TEST_CHECK(texture_path == "missing.png");
    }
  }

  // A missing path that escapes the package root should fail instead of being
  // preserved or rewritten to a broken internal reference.
  {
    const std::string usda_path =
        (fs::path(dir) / "scene_missing_unsafe.usda").string();
    const std::string usdz_path =
        (fs::path(dir) / "out_missing_unsafe.usdz").string();
    TEST_CHECK(WriteTexturedUSDA(usda_path, "../missing.png"));

    usdz::UsdzConvertOptions opts;
    opts.inputs.push_back(usda_path);
    opts.output = usdz_path;
    opts.flatten = true;

    usdz::UsdzConvertStats stats;
    std::string warn, err;
    bool ok = usdz::Convert(opts, &stats, &warn, &err);
    TEST_CHECK(!ok);
    TEST_CHECK(err.find("Unsafe texture path") != std::string::npos);
  }
}

// Pipeline test with JPEG output format.
void usdz_convert_pipeline_jpeg_test(void) {
  using namespace tinyusdz;
  namespace fs = std::filesystem;

  const std::string dir = TempDir();
  const std::string png_path = (fs::path(dir) / "tex_jpg.png").string();
  const std::string usda_path = (fs::path(dir) / "scene_jpg.usda").string();
  const std::string usdz_path = (fs::path(dir) / "out_jpg.usdz").string();

  Image tex = MakeSolidImage(64, 64, 4, 200, 150, 100, 255);
  {
    image::WriteOption wopt;
    wopt.format = image::WriteImageFormat::PNG;
    auto enc = image::WriteImageToMemory(tex, wopt);
    TEST_CHECK(enc.has_value());
    if (!enc) return;
    std::string werr;
    bool wok = io::WriteWholeFile(png_path, enc.value().data(),
                                  enc.value().size(), &werr);
    TEST_CHECK(wok);
    if (!wok) return;
  }

  {
    const std::string usda =
        "#usda 1.0\n"
        "(\n"
        "    defaultPrim = \"root\"\n"
        "    upAxis = \"Y\"\n"
        ")\n"
        "\n"
        "def Xform \"root\"\n"
        "{\n"
        "    def Material \"mat\"\n"
        "    {\n"
        "        token outputs:surface.connect = </root/mat/surface.outputs:surface>\n"
        "        def Shader \"surface\"\n"
        "        {\n"
        "            uniform token info:id = \"UsdPreviewSurface\"\n"
        "            color3f inputs:diffuseColor.connect = </root/mat/tex.outputs:rgb>\n"
        "            token outputs:surface\n"
        "        }\n"
        "        def Shader \"tex\"\n"
        "        {\n"
        "            uniform token info:id = \"UsdUVTexture\"\n"
        "            asset inputs:file = @tex_jpg.png@\n"
        "            float3 outputs:rgb\n"
        "        }\n"
        "    }\n"
        "}\n";
    std::string werr;
    bool wok = io::WriteWholeFile(
        usda_path, reinterpret_cast<const unsigned char *>(usda.data()),
        usda.size(), &werr);
    TEST_CHECK(wok);
    if (!wok) return;
  }

  usdz::UsdzConvertOptions opts;
  opts.inputs.push_back(usda_path);
  opts.output = usdz_path;
  opts.flatten = true;
  opts.arkit_compatible = true;
  opts.max_texture_size = 32;
  opts.texture_format = usdz::OutputTextureFormat::JPEG;
  opts.jpeg_quality = 80;

  usdz::UsdzConvertStats stats;
  std::string warn, err;
  bool ok = usdz::Convert(opts, &stats, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("convert error: %s", err.c_str());
    return;
  }
  TEST_CHECK(stats.num_textures >= 1);

  std::vector<uint8_t> usdz_bytes;
  std::string ioerr;
  bool rok = io::ReadWholeFile(&usdz_bytes, &ioerr, usdz_path, 0);
  TEST_CHECK(rok);
  if (!rok) return;
  std::string vwarn, verr;
  bool valid = tinyusdz::ValidateUSDZ(usdz_bytes.data(), usdz_bytes.size(),
                                      &vwarn, &verr);
  TEST_CHECK(valid);
  if (!valid) {
    TEST_MSG("validate: %s", verr.c_str());
  }
}

// Cleanup: remove temp files created by earlier tests.
void usdz_convert_cleanup_test(void) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path base = fs::temp_directory_path() / "tusdzconvert_test";
  if (fs::exists(base, ec)) {
    fs::remove_all(base, ec);
    // Not a test failure if cleanup fails (e.g. file in use).
  }
  TEST_CHECK(true);  // Always passes; this is a bookkeeping test.
}
