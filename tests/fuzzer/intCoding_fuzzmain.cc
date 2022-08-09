#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include <iostream>

#include "integerCoding.h"

static void parse_intCoding4(const uint8_t *data, size_t size)
{
  if (size <= 8 + 4) return;

  // TODO: Use Compress() to compute nInts.
  uint32_t n; // nInts
  memcpy(&n, data, 4); 

  using Compressor = tinyusdz::Usd_IntegerCompression;
  std::vector<char> compBuffer(Compressor::GetCompressedBufferSize(size));
  uint64_t compSize;
  memcpy(&compSize, data + 4, 8); 

  if ((compSize + 8 + 4) > size) {
    return;
  }

  if (compSize > compBuffer.size()) {
    return;
  }

  //std::cout << "n = " << n << "\n";
  //std::cout << "compSize = " << compSize << "\n";
  memcpy(compBuffer.data(), data + 8 + 4, compSize);

  std::vector<uint32_t> output(n);

  std::string err;
  bool ret = Compressor::DecompressFromBuffer(compBuffer.data(), compSize, output.data(), n, &err);
  (void)ret;

}

extern "C"
int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
    parse_intCoding4(data, size);
    return 0;
}
