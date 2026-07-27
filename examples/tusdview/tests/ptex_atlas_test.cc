// SPDX-License-Identifier: Apache-2.0
#include "displacement_bake.hh"
#include "ptex_atlas.hh"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "external/miniz.h"

namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

void Put16(std::vector<uint8_t>* out, uint16_t v) {
  out->push_back(static_cast<uint8_t>(v));
  out->push_back(static_cast<uint8_t>(v >> 8));
}
void Put32(std::vector<uint8_t>* out, uint32_t v) {
  for (int i = 0; i < 4; ++i)
    out->push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void Put64(std::vector<uint8_t>* out, uint64_t v) {
  Put32(out, static_cast<uint32_t>(v));
  Put32(out, static_cast<uint32_t>(v >> 32));
}
std::vector<uint8_t> Deflate(const std::vector<uint8_t>& source) {
  mz_ulong capacity = mz_compressBound(static_cast<mz_ulong>(source.size()));
  std::vector<uint8_t> result(static_cast<size_t>(capacity));
  CHECK(mz_compress(result.data(), &capacity, source.data(),
                    static_cast<mz_ulong>(source.size())) == MZ_OK);
  result.resize(static_cast<size_t>(capacity));
  return result;
}

void PutSample(std::vector<uint8_t>* out, uint32_t type, float value) {
  if (type == 0) {
    out->push_back(static_cast<uint8_t>(std::lround(value * 255.0f)));
  } else if (type == 1) {
    Put16(out, static_cast<uint16_t>(std::lround(value * 65535.0f)));
  } else if (type == 2) {
    Put16(out, value < 0.5f ? uint16_t{0x3400} : uint16_t{0x3a00});
  } else {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    Put32(out, bits);
  }
}

std::vector<uint8_t> SyntheticPtex(uint32_t dataType) {
  const uint32_t bytesPerComponent[] = {1, 2, 2, 4};
  std::vector<uint8_t> faceInfo;
  for (uint32_t face = 0; face < 2; ++face) {
    faceInfo.push_back(face == 0 ? 2 : 1);  // 4x2, then 2x4
    faceInfo.push_back(face == 0 ? 1 : 2);
    faceInfo.push_back(0);
    faceInfo.push_back(0);
    for (int edge = 0; edge < 4; ++edge) Put32(&faceInfo, 0xffffffffu);
  }
  const std::vector<uint8_t> faceInfoZ = Deflate(faceInfo);
  const std::vector<uint8_t> constants(2u * bytesPerComponent[dataType], 0);
  const std::vector<uint8_t> constantsZ = Deflate(constants);

  std::vector<std::vector<uint8_t>> blocks;
  std::vector<uint32_t> headerSizes;
  uint64_t levelDataSize = 0;
  for (uint32_t level = 0; level < 2; ++level) {
    std::vector<uint8_t> headers, payload;
    for (uint32_t face = 0; face < 2; ++face) {
      const uint32_t width = std::max(1u, (face == 0 ? 4u : 2u) >> level);
      const uint32_t height = std::max(1u, (face == 0 ? 2u : 4u) >> level);
      std::vector<uint8_t> planar;
      for (uint32_t pixel = 0; pixel < width * height; ++pixel)
        PutSample(&planar, dataType, face == 0 ? 0.25f : 0.75f);
      const std::vector<uint8_t> compressed = Deflate(planar);
      Put32(&headers, (1u << 30) | static_cast<uint32_t>(compressed.size()));
      payload.insert(payload.end(), compressed.begin(), compressed.end());
    }
    const std::vector<uint8_t> headersZ = Deflate(headers);
    headerSizes.push_back(static_cast<uint32_t>(headersZ.size()));
    blocks.push_back(headersZ);
    blocks.back().insert(blocks.back().end(), payload.begin(), payload.end());
    levelDataSize += blocks.back().size();
  }
  std::vector<uint8_t> levelInfo;
  for (size_t level = 0; level < blocks.size(); ++level) {
    Put64(&levelInfo, blocks[level].size());
    Put32(&levelInfo, headerSizes[level]);
    Put32(&levelInfo, 2);
  }

  std::vector<uint8_t> out;
  out.push_back('P'); out.push_back('t'); out.push_back('e'); out.push_back('x');
  Put32(&out, 1); Put32(&out, 1); Put32(&out, dataType);
  Put32(&out, 0xffffffffu); Put16(&out, 1); Put16(&out, 2); Put32(&out, 2);
  Put32(&out, 0); Put32(&out, faceInfoZ.size()); Put32(&out, constantsZ.size());
  Put32(&out, levelInfo.size()); Put32(&out, 0); Put64(&out, levelDataSize);
  Put32(&out, 0); Put32(&out, 0);
  out.insert(out.end(), faceInfoZ.begin(), faceInfoZ.end());
  out.insert(out.end(), constantsZ.begin(), constantsZ.end());
  out.insert(out.end(), levelInfo.begin(), levelInfo.end());
  for (const auto& block : blocks) out.insert(out.end(), block.begin(), block.end());
  return out;
}

void RunType(uint32_t type) {
  const std::vector<uint8_t> bytes = SyntheticPtex(type);
  tinyusdz::ptx::Reader reader;
  std::string error;
  CHECK(tinyusdz::ptx::Reader::OpenMemory(bytes.data(), bytes.size(), &reader,
                                          &error));
  tusdview::PtexAtlasOptions options;
  options.maxFaceEdge = 4;
  options.maxAtlasEdge = 64;
  options.gutter = 1;
  light3d::Image atlas;
  std::vector<tusdview::DrawPtexFaceRectCPU> rects;
  tusdview::PtexAtlasBuildStats stats;
  CHECK(tusdview::BuildPtexAtlas(reader, options, false, &atlas, &rects,
                                 &stats, &error));
  CHECK(rects.size() == 2);
  if (rects.size() != 2) return;
  CHECK(rects[0].width == 4 && rects[0].height == 2);
  CHECK(rects[1].width == 2 && rects[1].height == 4);
  CHECK(stats.rectTexelOffset + rects.size() * 8u <=
        static_cast<size_t>(atlas.width) * atlas.height);
  for (uint32_t face = 0; face < rects.size(); ++face) {
    const size_t base = stats.rectTexelOffset + face * 8u;
    auto value = [&](size_t component) {
      const uint32_t lo = atlas.data[(base + component * 2u) * 4u + 3u];
      const uint32_t hi = atlas.data[(base + component * 2u + 1u) * 4u + 3u];
      return lo | (hi << 8u);
    };
    CHECK(value(0) == rects[face].x && value(1) == rects[face].y);
    CHECK(value(2) == rects[face].width && value(3) == rects[face].height);
  }
  tusdview::DrawScene scene;
  tusdview::DrawTextureCPU texture;
  texture.image = atlas;
  texture.isPtex = true;
  texture.ptexFaceRects = rects;
  scene.textures.push_back(std::move(texture));
  CHECK(std::fabs(tusdview::SampleTextureRed(scene, 0, 0.37f, 0.61f, 0) -
                  0.25f) < 0.02f);
  CHECK(std::fabs(tusdview::SampleTextureRed(scene, 0, 0.37f, 0.61f, 1) -
                  0.75f) < 0.02f);
}
}  // namespace

int main() {
  for (uint32_t type = 0; type < 4; ++type) RunType(type);
  if (failures) return 1;
  std::printf("OK: rectangular UInt8/UInt16/Half/Float Ptex atlases\n");
  return 0;
}
