#include <iostream>
#include <fstream>

#include "stream-reader.hh"
#include "usda-reader.hh"
#include "io-util.hh"


int main(int argc, char **argv) {
  if (argc < 2) {
    std::cout << "usdaparser [--flatten] input.usda\n";
    std::cout << "  --flatten: Similar to --flatten in usdview from pxrUSD.\n";
    exit(-1);
  }

  std::string filename;
  // = argv[1];

  bool do_compose =false;
  size_t input_idx = 0; // 0 = invalid

  for (size_t i = 1; i < argc; i++) {
    std::string arg = argv[i];

    if (arg == "--flatten") {
      do_compose = true;
    } else if (input_idx == 0) {
      input_idx = i;
    }
  }

  if (input_idx == 0) {
    std::cerr << "No USD filename given.\n";
    exit(-1);
  }

  filename = argv[input_idx];

  std::string base_dir;
  base_dir = tinyusdz::io::GetBaseDir(filename);

  if (!tinyusdz::io::USDFileExists(filename)) {
    std::cerr << "Input file does not exist or invalid: " << filename << "\n";
    return -1;
  }

  if (!tinyusdz::IsUSDA(filename)) {
    std::cerr << "Input file isn't a USDA file: " << filename << "\n";
    return -1;
  }

  std::vector<uint8_t> data;
  std::string err;
  if (!tinyusdz::io::ReadWholeFile(&data, &err, filename, /* filesize_max */0)) {
    std::cerr << "Failed to open file: " << filename << ":" << err << "\n";
  }

  tinyusdz::StreamReader sr(data.data(), data.size(), /* swap endian */ false);
  tinyusdz::usda::USDAReader reader(&sr);

#if !defined(TINYUSDZ_PRODUCTION_BUILD)
  std::cout << "Basedir = " << base_dir << "\n";
#endif
  reader.SetBaseDir(base_dir);

  uint32_t load_states = static_cast<uint32_t>(tinyusdz::LoadState::Toplevel);
  if (do_compose) {
    load_states = static_cast<uint32_t>(tinyusdz::LoadState::Sublayer) | 
      static_cast<uint32_t>(tinyusdz::LoadState::Payload) | static_cast<uint32_t>(tinyusdz::LoadState::Reference);
    std::cout << "Enable composition.\n"; 
  }

  {
    // TODO: ReaderConfig.
    bool ret = reader.read(load_states);

    if (!ret) {
      std::cerr << "Failed to parse .usda: \n";
      std::cerr << reader.GetError() << "\n";
      return -1;
    } else {
#if !defined(TINYUSDZ_PRODUCTION_BUILD)
      std::cout << "ok\n";
#endif
    }
  }

  // Dump
  {
    bool ret = reader.ReconstructStage();
    if (!ret) {
      std::cerr << "Failed to reconstruct Stage: \n";
      std::cerr << reader.GetError() << "\n";
      return -1;
    }

    tinyusdz::Stage stage = reader.GetStage();
    std::cout << stage.ExportToString() << "\n";
  }

  return 0;
}
