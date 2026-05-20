#include <cstdint>
#include <cstring>

#include "mtlx-xml-parser.hh"

static void parse_mtlx_xml(const uint8_t *data, size_t size) {
  if (size > 1024 * 1024 * 128) {  // 128 MB cap
    return;
  }

  tinyusdz::mtlx::XMLDocument doc;
  doc.ParseMemory(reinterpret_cast<const char *>(data), size);
  (void)doc.GetRoot();  // exercise root access
}

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const *data,
                                       std::size_t size) {
  parse_mtlx_xml(data, size);
  return 0;
}
