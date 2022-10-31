#include <iostream>
#include <fstream>

#include "stream-reader.hh"
#include "usda-reader.hh"
#include "io-util.hh"


int main(int argc, char **argv) {
  if (argc < 2) {
    std::cout << "Need input.usda\n";
    exit(-1);
  }

  std::string filename = argv[1];

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

  {
    bool ret = reader.Read();

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
