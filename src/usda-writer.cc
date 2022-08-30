// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
//
// USDA(Ascii) writer
//

#include "usda-writer.hh"

#if !defined(TINYUSDZ_DISABLE_MODULE_USDA_WRITER)

#include <fstream>
#include <iostream>
#include <sstream>

#include "pprinter.hh"
#include "value-pprint.hh"
#include "tinyusdz.hh"

namespace tinyusdz {
namespace usda {

namespace {


}  // namespace

bool SaveAsUSDA(const std::string &filename, const Stage &stage,
                std::string *warn, std::string *err) {

  // TODO: warn and err on export.
  std::string s = stage.ExportToString();

  (void)warn;

  std::ofstream ofs(filename);
  if (!ofs) {
    if (err) {
      (*err) += "Failed to open file [" + filename + "] to write.\n";
    }
    return false;
  }

  ofs << s;

  std::cout << "Wrote to [" << filename << "]\n";

  return true;
}

} // namespace usda
}  // namespace tinyusdz

#else

namespace tinyusdz {
namespace usda {

bool SaveAsUSDA(const std::string &filename, const Stage &stage, std::string *warn, std::string *err) {
  (void)filename;
  (void)stage;
  (void)warn;

  if (err) {
    (*err) = "USDA Writer feature is disabled in this build.\n";
  }
  return false;
}



} // namespace usda
}  // namespace tinyusdz
#endif


