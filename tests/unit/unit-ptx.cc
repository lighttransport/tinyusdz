#define TEST_NO_MAIN
#include "acutest.h"
#include "unit-ptx.h"
#include "ptx-loader.hh"

void ptx_reader_rejects_invalid_input_test(void) {
  const uint8_t bad[] = {'P', 't', 'e', 'x', 1, 0, 0};
  tinyusdz::ptx::Reader reader;
  std::string err;
  TEST_CHECK(!tinyusdz::ptx::Reader::OpenMemory(bad, sizeof(bad), &reader, &err));
  TEST_CHECK(!err.empty());
}
