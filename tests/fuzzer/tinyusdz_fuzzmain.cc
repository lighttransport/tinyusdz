#include <cstdint>

#include "tinyusdz.hh"

static void parse_usdz(const uint8_t *data, size_t size)
{


}

extern "C"
int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
    parse_usdz(data, size);
    return 0;
}
