#include <fstream>
#include <vector>

#include <stream-reader.hh>
#include <tinyusdz.hh>

namespace tinyusdz {

// sections

void Sections() {
  const std::string kTokens = "TOKENS";
  const std::string kStrings = "STRINGS";
  const std::string kFields = "FIELDS";
  const std::string kFieldSets = "FIELDSETS";
  const std::string kPaths = "PATHS";
  const std::string kSpecs = "SPECS";

  const std::string kKnownSections[] = {
    kTokens, kStrings, kFields, kFieldSets, kPaths, kSpecs };
}


bool LoadUSDCFromFile(const std::string &filename, std::string *err,
                      const USDCLoadOptions &options) {
  std::vector<uint8_t> data;
  {
    std::ifstream ifs(filename.c_str(), std::ifstream::binary);
    if (!ifs) {
      if (err) {
        (*err) = "File not found or cannot open file : " + filename;
      }
      return false;
    }

    // TODO(syoyo): Use mmap
    ifs.seekg(0, ifs.end);
    size_t sz = static_cast<size_t>(ifs.tellg());
    if (int64_t(sz) < 0) {
      // Looks reading directory, not a file.
      if (err) {
        (*err) += "Looks like filename is a directory : \"" + filename + "\"\n";
      }
      return false;
    }

    if (sz < 16) {
      // ???
      if (err) {
        (*err) +=
            "File size too short. Looks like this file is not a USDC : \"" +
            filename + "\"\n";
      }
      return false;
    }

    data.resize(sz);

    ifs.seekg(0, ifs.beg);
    ifs.read(reinterpret_cast<char *>(&data.at(0)),
             static_cast<std::streamsize>(sz));
  }

  bool swap_endian = false;  // @FIXME

  StreamReader sr(data.data(), data.size(), swap_endian);

  // parse header.
  uint8_t magic[8];
  if (!sr.read8(reinterpret_cast<uint64_t *>(magic))) {
    if (err) {
      (*err) += "Failed to read magic number.\"";
    }
    return false;
  }

  if (memcmp(magic, "PXR-USDC", 8)) {
    if (err) {
      (*err) += "Invalid magic number.\"";
    }
    return false;
  }

  return true;
}

}  // namespace tinyusdz
