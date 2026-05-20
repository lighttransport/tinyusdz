#include <cstdint>
#include <cstdlib>
#include <string>

static int parse_m4a_alac(const uint8_t* data, size_t size) {
  if (size > 1024 * 1024 * 128 * 4) {
    return -1;
  }

  // TODO: Wire up the actual ALAC decoder (src/external/alac/codec/ALACDecoder.cpp).
  // The ALAC sources are linked into this fuzzer executable (see meson.build:
  // fuzz_alac_decoding), but the harness currently only validates input size.
  // To complete: parse the MP4/M4A container, extract the ALAC magic cookie,
  // init ALACDecoder, then feed compressed frames through Decode().
  // For now, return 0 so the fuzzer exercises the size cap but not decode logic.

  (void)data;
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data,
                                      std::size_t size) {
  return parse_m4a_alac(data, size);
}
