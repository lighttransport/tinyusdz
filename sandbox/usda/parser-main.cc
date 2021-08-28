#include <iostream>
#include <fstream>

#include "stream-reader.hh"
#include "usda-parser.hh"
#include "io-util.hh"


int main(int argc, char **argv) {
  if (argc < 2) {
    std::cout << "Need input.usda\n";
    exit(-1);
  }

  std::string filename = argv[1];

  std::string base_dir;
  base_dir = tinyusdz::io::GetBaseDir(filename);

  std::vector<uint8_t> data;
  std::string err;
  if (!tinyusdz::io::ReadWholeFile(&data, &err, filename, /* filesize_max */0)) {
    std::cerr << "Failed to open file: " << filename << ":" << err << "\n";
  }

  tinyusdz::StreamReader sr(data.data(), data.size(), /* swap endian */ false);
  tinyusdz::usda::USDAParser parser(&sr);

  std::cout << "Basedir = " << base_dir << "\n";
  parser.SetBaseDir(base_dir);

  {
    bool ret = parser.Parse();

    if (!ret) {
      std::cerr << "Failed to parse .usda: \n";
      std::cerr << parser.GetError() << "\n";
      return -1;
    } else {
      std::cout << "ok\n";
    }
  }

  return 0;
}
