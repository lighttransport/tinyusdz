#include <cstdint>
#include <cstring>
#include <vector>

#include "tinyusdz.hh"
#include "usda-reader.hh"
#include "usdc-writer.hh"

static void usd_write_roundtrip(const uint8_t *data, size_t size) {
  // Cap input size to avoid memory exhaustion
  if (size > 1024 * 1024 * 32) {  // 32 MB
    return;
  }

  // Prepend USDA magic header so the parser recognizes the input
  std::string content("#usda 1.0\n");
  std::vector<uint8_t> buf;
  buf.resize(content.size() + size);
  memcpy(buf.data(), content.data(), content.size());
  if (size > 0 && data) {
    memcpy(buf.data() + content.size(), data, size);
  }
  size_t total_size = content.size() + size;

  // Parse USDA
  tinyusdz::StreamReader sr(buf.data(), total_size, /* endianswap */ false);
  tinyusdz::usda::USDAReaderConfig config;
  config.max_memory_limit_in_mb = 1024;  // 1 GB for fuzzer
  tinyusdz::usda::USDAReader reader(&sr);
  reader.set_reader_config(config);
  if (!reader.Read()) {
    return;  // malformed input, skip write
  }
  if (!reader.ReconstructStage()) {
    return;
  }

  const tinyusdz::Stage &stage = reader.GetStage();

  // Write to USDC memory buffer
  std::vector<uint8_t> usdc_output;
  std::string warn, err;
  tinyusdz::USDWriteOptions write_opts;
  if (!tinyusdz::usdc::SaveAsUSDCToMemory(stage, &usdc_output, &warn, &err)) {
    return;
  }
  if (usdc_output.empty()) {
    return;
  }

  // Re-parse the USDC output to verify it doesn't crash
  tinyusdz::StreamReader usdc_sr(usdc_output.data(), usdc_output.size(),
                                  /* endianswap */ false);
  tinyusdz::usdc::USDCReader usdc_reader(&usdc_sr);
  tinyusdz::usdc::USDCReaderConfig usdc_config;
  usdc_config.max_memory_limit_in_mb = 1024;
  usdc_reader.SetConfig(usdc_config);
  (void)usdc_reader.ReadUSDC();
}

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const *data,
                                       std::size_t size) {
  usd_write_roundtrip(data, size);
  return 0;
}
