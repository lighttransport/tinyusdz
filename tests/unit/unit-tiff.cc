#define TEST_NO_MAIN
#include "acutest.h"
#include "unit-tiff.h"

#include "image-loader.hh"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

void Put16(std::vector<uint8_t>* out, uint16_t v) {
  out->push_back(static_cast<uint8_t>(v));
  out->push_back(static_cast<uint8_t>(v >> 8));
}
void Put32(std::vector<uint8_t>* out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out->push_back(static_cast<uint8_t>(v >> (i * 8)));
}
void Put64(std::vector<uint8_t>* out, uint64_t v) {
  for (int i = 0; i < 8; ++i) out->push_back(static_cast<uint8_t>(v >> (i * 8)));
}
std::vector<uint8_t> MakeRGBTiff(bool bigtiff) {
  constexpr uint16_t kEntries = 12;
  const size_t header = bigtiff ? 16u : 8u;
  const size_t entryBytes = bigtiff ? 20u : 12u;
  const size_t countBytes = bigtiff ? 8u : 2u;
  const size_t nextBytes = bigtiff ? 8u : 4u;
  const size_t ifd = header + countBytes;
  const size_t bitsOffset = ifd + kEntries * entryBytes + nextBytes;
  const size_t pixelsOffset = bitsOffset + 6u;
  std::vector<uint8_t> out;
  out.reserve(pixelsOffset + 12u);
  Put16(&out, 0x4949);
  Put16(&out, bigtiff ? 43u : 42u);
  if (bigtiff) {
    Put16(&out, 8);  // BigTIFF offset size.
    Put16(&out, 0);  // Reserved.
    Put64(&out, header);
  } else {
    Put32(&out, static_cast<uint32_t>(header));
  }
  if (bigtiff) Put64(&out, kEntries); else Put16(&out, kEntries);

  auto entry = [&](uint16_t tag, uint16_t type, uint64_t count,
                   uint64_t value, bool external = false) {
    Put16(&out, tag);
    Put16(&out, type);
    if (bigtiff) Put64(&out, count); else Put32(&out, static_cast<uint32_t>(count));
    if (external) {
      if (bigtiff) Put64(&out, value); else Put32(&out, static_cast<uint32_t>(value));
    } else {
      const size_t slot = bigtiff ? 8u : 4u;
      for (size_t i = 0; i < slot; ++i) out.push_back(static_cast<uint8_t>(value >> (i * 8)));
    }
  };
  entry(256, 3, 1, 2);                         // ImageWidth.
  entry(257, 3, 1, 2);                         // ImageLength.
  entry(258, 3, 3, bitsOffset, true);          // BitsPerSample.
  entry(259, 3, 1, 1);                         // Compression: none.
  entry(262, 3, 1, 2);                         // Photometric: RGB.
  entry(273, bigtiff ? 16 : 4, 1, pixelsOffset);  // StripOffsets.
  entry(277, 3, 1, 3);                         // SamplesPerPixel.
  entry(278, 4, 1, 2);                         // RowsPerStrip.
  entry(279, bigtiff ? 16 : 4, 1, 12);          // StripByteCounts.
  entry(284, 3, 1, 1);                         // PlanarConfiguration.
  entry(339, 3, 1, 1);                         // SampleFormat: uint.
  entry(274, 3, 1, 1);                         // Orientation.
  if (bigtiff) Put64(&out, 0); else Put32(&out, 0);
  Put16(&out, 8); Put16(&out, 8); Put16(&out, 8);
  const uint8_t pixels[] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
  out.insert(out.end(), pixels, pixels + sizeof(pixels));
  return out;
}

void CheckRGBTiff(const std::vector<uint8_t>& bytes) {
#if defined(TINYUSDZ_WITH_TIFF)
  auto info = tinyusdz::image::GetImageInfoFromMemory(bytes.data(), bytes.size(), "fixture.tif");
  TEST_CHECK(info.has_value());
  if (!info) return;
  TEST_CHECK(info->width == 2 && info->height == 2 && info->channels == 3);
  auto image = tinyusdz::image::LoadImageFromMemory(bytes.data(), bytes.size(), "fixture.tif");
  TEST_CHECK(image.has_value());
  if (!image) return;
  TEST_CHECK(image->image.width == 2 && image->image.height == 2);
  TEST_CHECK(image->image.channels == 3 || image->image.channels == 4);
#else
  (void)bytes;
  TEST_MSG("TINYUSDZ_WITH_TIFF is disabled; TIFF fixture skipped");
#endif
}

}  // namespace

void tinydng_classic_tiff_test(void) { CheckRGBTiff(MakeRGBTiff(false)); }
void tinydng_bigtiff_test(void) {
  const auto bytes = MakeRGBTiff(true);
  CheckRGBTiff(bytes);
#if defined(TINYUSDZ_WITH_TIFF)
  // Move only the StripOffsets metadata above 4 GiB. Metadata queries must
  // retain the 64-bit value without attempting to decode the unavailable
  // pixel payload.
  auto highOffset = bytes;
  const size_t ifd = 16u + 8u;
  const size_t stripOffsetEntry = ifd + 5u * 20u;
  const size_t value = stripOffsetEntry + 12u;
  const uint64_t high = 0x100000000ull;
  for (int i = 0; i < 8; ++i) highOffset[value + i] = static_cast<uint8_t>(high >> (i * 8));
  auto highInfo = tinyusdz::image::GetImageInfoFromMemory(
      highOffset.data(), highOffset.size(), "high-offset-bigtiff.tif");
  TEST_CHECK(highInfo.has_value());
  if (highInfo) TEST_CHECK(highInfo->width == 2 && highInfo->height == 2);

  const std::string path = "/tmp/tinyusdz-unit-bigtiff.tif";
  {
    std::ofstream file(path, std::ios::binary);
    TEST_CHECK(file.good());
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  }
  auto info = tinyusdz::image::GetImageInfoFromFile(path);
  TEST_CHECK(info.has_value());
  if (info) TEST_CHECK(info->width == 2 && info->height == 2 && info->channels == 3);
  std::remove(path.c_str());
#endif
}
