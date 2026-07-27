#define TEST_NO_MAIN
#include "acutest.h"
#include "unit-ptx.h"

#include <cstdlib>
#include "ptx-loader.hh"

void ptx_reader_rejects_invalid_input_test(void) {
  const uint8_t bad[] = {'P', 't', 'e', 'x', 1, 0, 0};
  tinyusdz::ptx::Reader reader;
  std::string err;
  TEST_CHECK(!tinyusdz::ptx::Reader::OpenMemory(bad, sizeof(bad), &reader, &err));
  TEST_CHECK(!err.empty());
}

void ptx_reader_island_fixture_test(void) {
  const char* path = std::getenv("TINYUSDZ_PTX_FIXTURE");
  if (!path || !*path) {
    TEST_MSG("TINYUSDZ_PTX_FIXTURE is unset (fixture check skipped)");
    return;
  }
  tinyusdz::ptx::Reader reader;
  std::string err;
  TEST_CHECK(tinyusdz::ptx::Reader::OpenFile(path, &reader, &err));
  if (!err.empty()) TEST_MSG("Ptex open error: %s", err.c_str());
  if (reader.info().faces == 0) return;
  tinyusdz::ptx::FaceImage image;
  TEST_CHECK(reader.ReadFace(0, 0, 64u * 1024u * 1024u, &image, &err));
  TEST_CHECK(image.width > 0 && image.height > 0);
  TEST_CHECK(!image.data.empty());
  for (uint32_t level = 1; level < reader.info().levels; ++level) {
    err.clear();
    tinyusdz::ptx::FaceImage mip;
    TEST_CHECK(reader.ReadFace(0, level, 64u * 1024u * 1024u, &mip, &err));
    TEST_CHECK(mip.width > 0 && mip.height > 0);
    TEST_CHECK(!mip.data.empty());
  }
}
