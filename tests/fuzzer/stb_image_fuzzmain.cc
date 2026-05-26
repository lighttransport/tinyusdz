#include <cstdint>

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"

static void parse_stb_image(const uint8_t *data, size_t size) {
  // Cap input size to avoid memory exhaustion
  if (size > 1024 * 1024 * 128) {  // 128 MB
    return;
  }

  int w = 0, h = 0, comp = 0;
  unsigned char *img = stbi_load_from_memory(data, int(size), &w, &h, &comp, 0);
  if (img) {
    stbi_image_free(img);
  }
}

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const *data,
                                       std::size_t size) {
  parse_stb_image(data, size);
  return 0;
}
