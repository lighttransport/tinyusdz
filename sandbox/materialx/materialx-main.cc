#include <cstdlib>
#include <cstdio>
#include <string>
#include <iostream>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "tinyxml2/tinyxml2.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

int main(int argc, char **argv)
{
  std::string filename = "../../data/materialx/UsdPreviewSurface/usd_preview_surface_default.mtlx";

  if (argc > 1) {
    filename = argv[1];
  }

  tinyxml2::XMLDocument doc;
  doc.LoadFile(filename.c_str());
  if (doc.ErrorID() != 0) {
    std::cerr << "XML Parising error: code = " << doc.ErrorID() << "\n";
    return -1;
  }

  std::cout << "Read OK\n";

  tinyxml2::XMLPrinter printer;
  doc.Print(&printer);

  std::cout << printer.CStr() << "\n";

  return 0;
}
