// SPDX-License-Identifier: Apache-2.0
// Texture pipeline test: tir resize, texcomp block compression, texpipe mip
// chains, and the FinalizeDrawTextures usage classification, all through the
// tusdview wrapper (texture_tools.hh) + BuildDrawScene. Compiled with and
// without TUSDVIEW_WITH_TEXTOOLS; most checks skip in the OFF build.
#include "mesh_build.hh"
#include "texture_tools.hh"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "tydra/render-data.hh"

#if defined(TUSDVIEW_WITH_TEXTOOLS)
#include "texcomp.h"  // tc_bc7_decompress_rgba8 (reference round-trip)
#endif

namespace {

namespace tydra = tinyusdz::tydra;

int g_fail = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
      ++g_fail;                                                        \
    }                                                                  \
  } while (0)

light3d::Image MakeImage(int w, int h,
                         void (*gen)(int, int, uint8_t*)) {
  light3d::Image img;
  img.width = w;
  img.height = h;
  img.channels = 4;
  img.data.resize(static_cast<size_t>(w) * h * 4);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      gen(x, y, img.data.data() + (static_cast<size_t>(y) * w + x) * 4);
    }
  }
  return img;
}

int AddImage(tydra::RenderScene* scene, tydra::ColorSpace colorSpace, int w,
             int h, const light3d::Image& src) {
  tydra::BufferData buf;
  buf.data = src.data;
  const int bufferId = static_cast<int>(scene->buffers.size());
  scene->buffers.push_back(std::move(buf));

  tydra::TextureImage img;
  img.buffer_id = bufferId;
  img.decoded = true;
  img.width = w;
  img.height = h;
  img.channels = 4;
  img.texelComponentType = tydra::ComponentType::UInt8;
  img.colorSpace = colorSpace;
  img.asset_identifier =
      "asset_" + std::to_string(scene->images.size()) + ".png";
  const int imageId = static_cast<int>(scene->images.size());
  scene->images.push_back(std::move(img));
  return imageId;
}

int AddTexture(tydra::RenderScene* scene, int imageId,
               tydra::UVTexture::Channel channel) {
  tydra::UVTexture tex;
  tex.prim_name = "tex" + std::to_string(scene->textures.size());
  tex.texture_image_id = imageId;
  tex.connectedOutputChannel = channel;
  tex.wrapS = tydra::UVTexture::WrapMode::REPEAT;
  tex.wrapT = tydra::UVTexture::WrapMode::REPEAT;
  tex.scale = {1.0f, 1.0f, 1.0f, 1.0f};
  tex.bias = {0.0f, 0.0f, 0.0f, 0.0f};
  const int texId = static_cast<int>(scene->textures.size());
  scene->textures.push_back(std::move(tex));
  return texId;
}

void TestSolidResize() {
  // A constant sRGB image must survive decode->filter->re-encode within 1 LSB.
  light3d::Image img = MakeImage(64, 64, [](int, int, uint8_t* px) {
    px[0] = 200; px[1] = 100; px[2] = 50; px[3] = 255;
  });
  std::string err;
  CHECK(tusdview::TexToolsResizeRGBA8(&img, 16, 16, /*srgb=*/true, &err));
  CHECK(img.width == 16 && img.height == 16);
  for (size_t i = 0; i < img.data.size(); i += 4) {
    CHECK(std::abs(int(img.data[i + 0]) - 200) <= 1);
    CHECK(std::abs(int(img.data[i + 1]) - 100) <= 1);
    CHECK(std::abs(int(img.data[i + 2]) - 50) <= 1);
    CHECK(img.data[i + 3] == 255);
    if (g_fail) break;
  }
}

void TestMipDims() {
  light3d::Image img = MakeImage(8, 4, [](int x, int y, uint8_t* px) {
    px[0] = uint8_t(x * 32); px[1] = uint8_t(y * 64); px[2] = 0; px[3] = 255;
  });
  tusdview::TexUsage usage;
  std::vector<light3d::Image> mips;
  CHECK(tusdview::TexToolsBuildMips(img, usage, &mips));
  CHECK(mips.size() == 3);  // 4x2, 2x1, 1x1
  if (mips.size() == 3) {
    CHECK(mips[0].width == 4 && mips[0].height == 2);
    CHECK(mips[1].width == 2 && mips[1].height == 1);
    CHECK(mips[2].width == 1 && mips[2].height == 1);
  }
}

void TestSrgbCorrectMip() {
  // 2x2 half-black/half-white: the 1x1 level must average in LINEAR light
  // (=> sRGB byte ~188), not in sRGB bytes (=> ~128).
  light3d::Image img = MakeImage(2, 2, [](int x, int, uint8_t* px) {
    const uint8_t v = (x == 0) ? 0 : 255;
    px[0] = v; px[1] = v; px[2] = v; px[3] = 255;
  });
  tusdview::TexUsage usage;
  usage.srgb = true;
  std::vector<light3d::Image> mips;
  CHECK(tusdview::TexToolsBuildMips(img, usage, &mips));
  CHECK(mips.size() == 1);
  if (mips.size() == 1) {
    CHECK(mips[0].width == 1 && mips[0].height == 1);
    const int v = mips[0].data[0];
    CHECK(v > 170 && v < 200);  // linear avg 0.5 -> sRGB ~188
  }
}

void TestCompressedSizes() {
  // NPOT 7x5 image -> 2x2 blocks; BC1 8 B/block, BC3/BC7 16 B/block.
  light3d::Image img = MakeImage(7, 5, [](int x, int y, uint8_t* px) {
    px[0] = uint8_t(x * 36); px[1] = uint8_t(y * 51); px[2] = 128; px[3] = 255;
  });
  tusdview::DrawCompressedImageCPU c;
  CHECK(tusdview::TexToolsCompress(img, false, tusdview::DrawCompressedFormat::BC1, &c));
  CHECK(c.data.size() == 2 * 2 * 8);
  CHECK(tusdview::TexToolsCompress(img, false, tusdview::DrawCompressedFormat::BC3, &c));
  CHECK(c.data.size() == 2 * 2 * 16);
  CHECK(tusdview::TexToolsCompress(img, true, tusdview::DrawCompressedFormat::BC7, &c));
  CHECK(c.data.size() == 2 * 2 * 16);
}

#if defined(TUSDVIEW_WITH_TEXTOOLS)
void TestBc7Roundtrip() {
  light3d::Image img = MakeImage(16, 16, [](int x, int y, uint8_t* px) {
    px[0] = uint8_t(x * 16); px[1] = uint8_t(y * 16);
    px[2] = uint8_t((x + y) * 8); px[3] = 255;
  });
  tusdview::DrawCompressedImageCPU c;
  CHECK(tusdview::TexToolsCompress(img, false, tusdview::DrawCompressedFormat::BC7, &c));
  std::vector<uint8_t> back(img.data.size());
  CHECK(tc_bc7_decompress_rgba8(c.data.data(), 16, 16, 16 * 4, back.data(),
                                back.size()) == TC_SUCCESS);
  double mse = 0.0;
  for (size_t i = 0; i < back.size(); ++i) {
    const double d = double(back[i]) - double(img.data[i]);
    mse += d * d;
  }
  mse /= double(back.size());
  const double psnr = 10.0 * std::log10(255.0 * 255.0 / (mse > 0 ? mse : 1e-9));
  if (psnr <= 30.0) std::printf("BC7 roundtrip PSNR = %.2f dB\n", psnr);
  CHECK(psnr > 30.0);  // FAST-quality BC7 on this gradient measures ~32 dB
}
#endif

void TestAlphaCoverage() {
  // 16x16 with exactly half the texels opaque. Alpha-tested mips must keep
  // roughly half the texels above the cutoff instead of averaging to gray.
  light3d::Image img = MakeImage(16, 16, [](int x, int y, uint8_t* px) {
    const bool solid = ((x / 2) + (y / 2)) % 2 == 0;
    px[0] = 255; px[1] = 255; px[2] = 255;
    px[3] = solid ? 255 : 0;
  });
  tusdview::TexUsage usage;
  usage.alphaTested = true;
  usage.alphaCutoff = 0.5f;
  std::vector<light3d::Image> mips;
  CHECK(tusdview::TexToolsBuildMips(img, usage, &mips));
  CHECK(mips.size() == 4);
  if (mips.size() >= 2) {
    const light3d::Image& m = mips[1];  // 4x4
    int above = 0, total = m.width * m.height;
    for (int i = 0; i < total; ++i) {
      if (m.data[static_cast<size_t>(i) * 4 + 3] >= 128) ++above;
    }
    const float coverage = float(above) / float(total);
    CHECK(coverage > 0.3f && coverage < 0.7f);
  }
}

void TestPipelineClassification() {
  // Full BuildDrawScene: normal-map texture classified + mips built + BCn
  // payloads per level with a single format.
  tydra::RenderScene scene;
  light3d::Image base = MakeImage(16, 16, [](int x, int y, uint8_t* px) {
    px[0] = uint8_t(x * 16); px[1] = uint8_t(y * 16); px[2] = 200; px[3] = 255;
  });
  light3d::Image normal = MakeImage(16, 16, [](int, int, uint8_t* px) {
    px[0] = 128; px[1] = 128; px[2] = 255; px[3] = 255;
  });
  const int baseImage = AddImage(&scene, tydra::ColorSpace::sRGB_Texture, 16, 16, base);
  const int normalImage = AddImage(&scene, tydra::ColorSpace::Raw, 16, 16, normal);
  const int baseTex = AddTexture(&scene, baseImage, tydra::UVTexture::Channel::RGB);
  const int normalTex = AddTexture(&scene, normalImage, tydra::UVTexture::Channel::RGB);

  tydra::OpenPBRSurfaceShader shader;
  shader.base_color.value = {1.0f, 1.0f, 1.0f};
  shader.base_color.texture_id = baseTex;
  shader.normal.texture_id = normalTex;

  tydra::RenderMaterial material;
  material.name = "textured";
  material.openPBRShader = shader;
  material.computeMaterialTag();
  scene.materials.push_back(std::move(material));

  tusdview::TextureRuntimeOptions opt;
  opt.generateMips = true;
  opt.compression = tusdview::TextureCompressionMode::BCn;

  tusdview::DrawScene out;
  tusdview::BuildDrawScene(scene, &out, nullptr, nullptr, opt);

  CHECK(out.materials.size() == 1);
  if (out.materials.size() != 1) return;
  const tusdview::DrawMaterialCPU& m = out.materials[0];
  CHECK(m.baseColorTex >= 0 && m.normalTex >= 0);
  if (m.baseColorTex < 0 || m.normalTex < 0) return;

  const tusdview::DrawTextureCPU& bt =
      out.textures[static_cast<size_t>(m.baseColorTex)];
  const tusdview::DrawTextureCPU& nt =
      out.textures[static_cast<size_t>(m.normalTex)];
  CHECK(!bt.isNormalMap);
  CHECK(nt.isNormalMap);
  if (!tusdview::TexToolsAvailable()) return;

  CHECK(bt.mipImages.size() == 4);  // 8,4,2,1
  CHECK(nt.mipImages.size() == 4);
  // Flat +Z normal map must stay flat at every level.
  for (const light3d::Image& mip : nt.mipImages) {
    CHECK(std::abs(int(mip.data[0]) - 128) <= 2);
    CHECK(std::abs(int(mip.data[1]) - 128) <= 2);
    CHECK(int(mip.data[2]) >= 252);
  }
  // Compressed chain: one format, one payload per level.
  CHECK(bt.requestedCompressed);
  CHECK(bt.compressed.format != tusdview::DrawCompressedFormat::None);
  CHECK(bt.compressed.mips.size() == bt.mipImages.size());
  for (size_t l = 0; l < bt.compressed.mips.size(); ++l) {
    CHECK(bt.compressed.mips[l].width == bt.mipImages[l].width);
    CHECK(!bt.compressed.mips[l].data.empty());
  }
}

}  // namespace

int main() {
  if (!tusdview::TexToolsAvailable()) {
    std::printf("textools disabled; running classification-only checks\n");
    TestPipelineClassification();
    if (g_fail) return 1;
    std::printf("OK (textools OFF)\n");
    return 0;
  }
  TestSolidResize();
  TestMipDims();
  TestSrgbCorrectMip();
  TestCompressedSizes();
#if defined(TUSDVIEW_WITH_TEXTOOLS)
  TestBc7Roundtrip();
#endif
  TestAlphaCoverage();
  TestPipelineClassification();
  if (g_fail) {
    std::printf("%d check(s) FAILED\n", g_fail);
    return 1;
  }
  std::printf("OK\n");
  return 0;
}
