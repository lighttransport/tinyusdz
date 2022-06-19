#ifdef _MSC_VER
#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>  // include API for expanding a file path
#endif


#include "usdc-writer.hh"
#include "io-util.hh"
//#include "pprinter.hh"

#include <fstream>
#include <iostream>
#include <sstream>

namespace tinyusdz {
namespace usdc {

namespace {

constexpr size_t kSectionNameMaxLength = 15;

#ifdef _WIN32
std::wstring UTF8ToWchar(const std::string &str) {
  int wstr_size =
      MultiByteToWideChar(CP_UTF8, 0, str.data(), int(str.size()), nullptr, 0);
  std::wstring wstr(size_t(wstr_size), 0);
  MultiByteToWideChar(CP_UTF8, 0, str.data(), int(str.size()), &wstr[0],
                      int(wstr.size()));
  return wstr;
}

std::string WcharToUTF8(const std::wstring &wstr) {
  int str_size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), int(wstr.size()),
                                     nullptr, 0, nullptr, nullptr);
  std::string str(size_t(str_size), 0);
  WideCharToMultiByte(CP_UTF8, 0, wstr.data(), int(wstr.size()), &str[0],
                      int(str.size()), nullptr, nullptr);
  return str;
}
#endif


struct Section {
  Section() { memset(this, 0, sizeof(*this)); }
  Section(char const *name, int64_t start, int64_t size);
  char name[kSectionNameMaxLength + 1];
  int64_t start, size;  // byte offset to section info and its data size
};

//
// TOC = list of sections.
//
struct TableOfContents {
  // Section const *GetSection(SectionName) const;
  // int64_t GetMinimumSectionStart() const;
  std::vector<Section> sections;
};



class Writer {
 public:
  Writer(const Scene &scene) : scene_(scene) {}

  const Scene &scene_;

  const std::string &Error() const { return err_; }

  bool WriteHeader() {
    char magic[8];
    magic[0] = 'P';
    magic[1] = 'X';
    magic[2] = 'R';
    magic[3] = '-';
    magic[4] = 'U';
    magic[5] = 'S';
    magic[6] = 'D';
    magic[7] = 'C';


    uint8_t version[8]; // Only first 3 bytes are used.
    version[0] = 0;
    version[1] = 8;
    version[2] = 0;

    // TOC offset(8bytes)
    // Must be 89 or greater.
    uint64_t toc_offset;

    std::array<uint8_t, 88> header;
    memset(&header, 0, 88);

    memcpy(&header[0], magic, 8);
    memcpy(&header[8], version, 8);
    memcpy(&header[16], &toc_offset, 8);

    return true;
  }

  //
  // TODO: 
  //  - TOC
  //  - Tokens
  //  - Strings
  //  - Fields
  //  - FieldSets
  //  - Paths
  //  - Specs
  //

  bool SerializeTOC() {
    uint64_t num_sections = toc_.sections.size();

    if (num_sections == 0) {
      err_ += "Zero sections in TOC.\n";
      return false;
    }

    oss_toc_.clear();

    // # of sections
    oss_toc_.write(reinterpret_cast<const char *>(&num_sections), 8);
    
    
    return true;
  }


  // Get serialized USDC binary data
  bool GetOutput(std::vector<uint8_t> *output) {
    if (!err_.empty()) {
      return false;
    }

    (void)output;

    // TODO
    return false;
  }

 private:
  Writer() = delete;
  Writer(const Writer &) = delete;

  TableOfContents toc_;

  //
  // Serialized data
  //
  std::ostringstream oss_toc_;

  std::string err_;
};

}  // namespace

bool SaveAsUSDCToFile(const std::string &filename, const Scene &scene,
                std::string *warn, std::string *err) {
#ifdef __ANDROID__
  if (err) {
    (*err) += "Saving USDC to a file is not supported for Android platform.\n";
  }
  return false;
#else

  std::vector<uint8_t> output;

  if (!SaveAsUSDCToMemory(scene, &output, warn, err)) {
    return false;
  }

#ifdef _WIN32
#if defined(_MSC_VER) || defined(__GLIBCXX__)
  FILE *fp = nullptr;
  errno_t fperr = _wfopen_s(&fp, UTF8ToWchar(filename).c_str(), L"wb");
  if (fperr != 0) {
    if (err) {
      // TODO: WChar
      (*err) += "Failed to open file to write.\n";
    }
    return false;
  }
#else
  FILE *fp = nullptr;
  errno_t fperr = fopen_s(&fp, abs_filename.c_str(), "wb");
  if (fperr != 0) {
    if (err) {
      (*err) += "Failed to open file `" + filename + "` to write.\n";
    }
    return false;
  }
#endif

#else
  FILE *fp = fopen(filename.c_str(), "wb");
  if (fp == nullptr) {
    if (err) {
      (*err) += "Failed to open file `" + filename + "` to write.\n";
    }
    return false;
  }
#endif

  size_t n = fwrite(output.data(), /* size */1, /* count */output.size(), fp);
  if (n < output.size()) {
    // TODO: Retry writing data when n < output.size()

    if (err) {
      (*err) += "Failed to write data to a file.\n";
    }
    return false;
  }

  return true;
#endif
}

bool SaveAsUSDCToMemory(const Scene &scene, std::vector<uint8_t> *output,
                std::string *warn, std::string *err) {
  (void)warn;
  (void)output;

  // TODO
  Writer writer(scene);

  if (err) {
    (*err) += "USDC writer is not yet implemented.\n";
  }

  return false;
}

} // namespace usdc
}  // namespace tinyusdz
