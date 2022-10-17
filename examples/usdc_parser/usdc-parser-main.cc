// simple USDC parser
#include <iostream>
#include <fstream>

#include "stream-reader.hh"
#include "usdc-reader.hh"
#include "io-util.hh"


int main(int argc, char **argv) {
  if (argc < 2) {
    std::cout << "Need input.usdc\n";
    exit(-1);
  }

  std::string filename = argv[1];

  //std::string base_dir;
  //base_dir = tinyusdz::io::GetBaseDir(filename);

  if (!tinyusdz::IsUSDC(filename)) {
    std::cerr << "Input file isn't a USDC file.\n";
    return -1;
  }

  std::vector<uint8_t> data;
  std::string err;
  if (!tinyusdz::io::ReadWholeFile(&data, &err, filename, /* filesize_max */0)) {
    std::cerr << "Failed to open file: " << filename << ":" << err << "\n";
  }

  tinyusdz::StreamReader sr(data.data(), data.size(), /* swap endian */ false);
  tinyusdz::usdc::USDCReader reader(&sr);

  //std::cout << "Basedir = " << base_dir << "\n";
  //reader.SetBaseDir(base_dir);

  {
    bool ret = reader.ReadUSDC();

    if (!ret) {

      if (reader.GetWarning().size()) {
        std::cout << "WARN: " << reader.GetWarning() << "\n";
      }

      std::cerr << "Failed to parse .usdc: \n";
      std::cerr << reader.GetError() << "\n";
      return -1;
    }
  }

  // Dump
  {
    tinyusdz::Stage stage;
    bool ret = reader.ReconstructStage(&stage);
    if (!ret) {

      if (reader.GetWarning().size()) {
        std::cout << "WARN: " << reader.GetWarning() << "\n";
      }

      std::cerr << "Failed to reconstruct Stage: \n";
      std::cerr << reader.GetError() << "\n";
      return -1;
    }

    if (reader.GetWarning().size()) {
      std::cout << "WARN: " << reader.GetWarning() << "\n";
    }

    // There may be error but not fatal.
    if (reader.GetError().size()) {
      std::cerr << reader.GetError() << "\n";
    }

    std::cout << stage.ExportToString() << "\n";
  }

  return 0;
}
