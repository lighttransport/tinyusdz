#include <cassert>
#include <fstream>
#include <vector>

#include "integerCoding.h"
#include "lz4-compression.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"

#include <iostream>  // dbg

namespace tinyusdz {

namespace {

constexpr size_t kSectionNameMaxLength = 15;

#if 0
// Compile time constant section names, enforces max length.
struct SectionName {
    template <size_t N>
    constexpr SectionName(char const (&a)[N]) : _p(a), _size(N-1) {
        static_assert(N <= kSectionNameMaxLength,
                      "Section name cannot exceed SectionNameMaxLength");
    }
    constexpr size_t size() const { return _size; }
    constexpr char const *c_str() const { return _p; }
    constexpr bool operator==(SectionName other) const {
        return _p == other._p; }
    constexpr bool operator!=(SectionName other) const {
        return _p != other._p; }
    bool operator==(char const *otherStr) const {
        return !strcmp(_p, otherStr); }
private:
    const char * const _p;
    const size_t _size;
};
#endif

//
// Describes connection of a path(i.e. node) and field(e.g. vertex data)
struct Spec {
  int64_t path_index;
  int64_t fieldset_index;
  std::string spec_type;  
};

struct Section {
  Section() { memset(this, 0, sizeof(*this)); }
  Section(char const *name, int64_t start, int64_t size);
  char name[kSectionNameMaxLength + 1];
  int64_t start, size;
};

struct TableOfContents {
  // Section const *GetSection(SectionName) const;
  // int64_t GetMinimumSectionStart() const;
  std::vector<Section> sections;
};

#if 0
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
#endif

template <class Int>
static inline void _ReadCompressedInts(const StreamReader *sr, Int *out,
                                       size_t size) {
  // TODO(syoyo): Error check
  using Compressor =
      typename std::conditional<sizeof(Int) == 4, Usd_IntegerCompression,
                                Usd_IntegerCompression64>::type;
  std::vector<char> compBuffer(Compressor::GetCompressedBufferSize(size));
  uint64_t compSize;
  sr->read8(&compSize);

  sr->read(compSize, compSize, reinterpret_cast<uint8_t *>(compBuffer.data()));
  Compressor::DecompressFromBuffer(compBuffer.data(), compSize, out, size);
}

class Parser {
 public:
  Parser(StreamReader *sr) : _sr(sr) {}

  bool ReadBootStrap();
  bool ReadTOC();

  bool ReadPaths();
  bool ReadTokens();
  bool ReadStrings();

  ///
  /// Read TOC section
  ///
  bool ReadSection(Section *s);

  // TODO PrefetchStructuralSections

  std::string GetError() { return _err; }

 private:
  bool ReadCompressedPaths();

  const StreamReader *_sr = nullptr;
  std::string _err;

  // Header(bootstrap)
  uint8_t _version[3] = {0, 0, 0};

  TableOfContents _toc;

  int64_t _toc_offset{0};

  // index to _toc.sections
  int64_t _tokens_index{-1};
  int64_t _paths_index{-1};

  std::vector<std::string> _tokens;
};

bool Parser::ReadCompressedPaths() {
  std::vector<uint32_t> pathIndexes;
  std::vector<int32_t> elementTokenIndexes;
  std::vector<int32_t> jumps;

  // Read number of encoded paths.
  uint64_t numPaths;
  if (!_sr->read8(&numPaths)) {
    _err += "Failed to read the number of paths.\n";
    return false;
  }

  pathIndexes.resize(numPaths);
  elementTokenIndexes.resize(numPaths);
  jumps.resize(numPaths);

  // Create temporary space for decompressing.
  std::vector<char> compBuffer(
      Usd_IntegerCompression::GetCompressedBufferSize(numPaths));
  std::vector<char> workingSpace(
      Usd_IntegerCompression::GetDecompressionWorkingSpaceSize(numPaths));

  // pathIndexes.
  {
    uint64_t pathIndexesSize;
    if (!_sr->read8(&pathIndexesSize)) {
      _err += "Failed to read pathIndexesSize.\n";
      return false;
    }

    if (pathIndexesSize !=
        _sr->read(pathIndexesSize, pathIndexesSize,
                  reinterpret_cast<uint8_t *>(compBuffer.data()))) {
      _err += "Failed to read pathIndexes data.\n";
      return false;
    }

    std::string err;
    Usd_IntegerCompression::DecompressFromBuffer(
        compBuffer.data(), pathIndexesSize, pathIndexes.data(), numPaths, &err,
        workingSpace.data());
    if (!err.empty()) {
      _err += err;
      return false;
    }
  }

  // elementTokenIndexes.
  {
    uint64_t elementTokenIndexesSize;
    if (!_sr->read8(&elementTokenIndexesSize)) {
      _err += "Failed to read elementTokenIndexesSize.\n";
      return false;
    }

    if (elementTokenIndexesSize !=
        _sr->read(elementTokenIndexesSize, elementTokenIndexesSize,
                  reinterpret_cast<uint8_t *>(compBuffer.data()))) {
      _err += "Failed to read elementTokenIndexes data.\n";
      return false;
    }

    std::string err;
    Usd_IntegerCompression::DecompressFromBuffer(
        compBuffer.data(), elementTokenIndexesSize, elementTokenIndexes.data(),
        numPaths, &err, workingSpace.data());

    if (!err.empty()) {
      _err += err;
      return false;
    }
  }

  // jumps.
  {
    uint64_t jumpsSize;
    if (!_sr->read8(&jumpsSize)) {
      _err += "Failed to read jumpsSize.\n";
      return false;
    }

    if (jumpsSize !=
        _sr->read(jumpsSize, jumpsSize,
                  reinterpret_cast<uint8_t *>(compBuffer.data()))) {
      _err += "Failed to read jumps data.\n";
      return false;
    }

    std::string err;
    Usd_IntegerCompression::DecompressFromBuffer(compBuffer.data(), jumpsSize,
                                                 jumps.data(), numPaths, &err,
                                                 workingSpace.data());

    if (!err.empty()) {
      _err += err;
      return false;
    }
  }

#if 0  // TODO
    // Now build the paths.
    _BuildDecompressedPathsImpl(pathIndexes, elementTokenIndexes, jumps, 0,
                                SdfPath(), dispatcher);

    //dispatcher.Wait();
#endif

  for (uint32_t item : pathIndexes) {
    std::cout << "pathIndexes " << item << "\n";
  }

  for (uint32_t item : elementTokenIndexes) {
    std::cout << "elementTokenIndexes " << item << "\n";
  }

  for (uint32_t item : jumps) {
    std::cout << "jumps " << item << "\n";
  }
    
  return true;
}

bool Parser::ReadSection(Section *s) {
  size_t name_len = kSectionNameMaxLength + 1;

  if (name_len !=
      _sr->read(name_len, name_len, reinterpret_cast<uint8_t *>(s->name))) {
    _err += "Failed to read section.name.\n";
    return false;
  }

  if (!_sr->read8(&s->start)) {
    _err += "Failed to read section.start.\n";
    return false;
  }

  if (!_sr->read8(&s->size)) {
    _err += "Failed to read section.size.\n";
    return false;
  }

  return true;
}

bool Parser::ReadTokens() {
  if ((_tokens_index < 0) || (_tokens_index >= int64_t(_toc.sections.size()))) {
    _err += "Invalid index for `TOKENS` section.\n";
    return false;
  }

  if ((_version[0] == 0) && (_version[1] < 4)) {
    _err += "Version must be 0.4.0 or later, but got " +
            std::to_string(_version[0]) + "." + std::to_string(_version[1]) +
            "." + std::to_string(_version[2]) + "\n";
    return false;
  }

  const Section &s = _toc.sections[_tokens_index];
  if (!_sr->seek_set(s.start)) {
    _err += "Failed to move to `TOKENS` section.\n";
    return false;
  }

  std::cout << "s.start = " << s.start << "\n";

  // # of tokens.
  uint64_t n;
  if (!_sr->read8(&n)) {
    _err += "Failed to read # of tokens at `TOKENS` section.\n";
    return false;
  }

  // Tokens are lz4 compressed starting from version 0.4.0

  // Compressed token data.
  uint64_t uncompressedSize;
  if (!_sr->read8(&uncompressedSize)) {
    _err += "Failed to read uncompressedSize at `TOKENS` section.\n";
    return false;
  }

  uint64_t compressedSize;
  if (!_sr->read8(&compressedSize)) {
    _err += "Failed to read compressedSize at `TOKENS` section.\n";
    return false;
  }

  std::cout << "# of tokens = " << n
            << ", uncompressedSize = " << uncompressedSize
            << ", compressedSize = " << compressedSize << "\n";

  std::vector<char> chars(uncompressedSize);
  std::vector<char> compressed(compressedSize);

  if (compressedSize !=
      _sr->read(compressedSize, compressedSize,
                reinterpret_cast<uint8_t *>(compressed.data()))) {
    _err += "Failed to read compressed data at `TOKENS` section.\n";
    return false;
  }

  if (uncompressedSize != LZ4Compression::DecompressFromBuffer(
                              compressed.data(), chars.data(), compressedSize,
                              uncompressedSize, &_err)) {
    return false;
  }

  // Split null terminated string into _tokens.
  const char *p = chars.data();
  const char *pe = chars.data() + chars.size();
  for (size_t i = 0; i < n; i++) {
    // TODO(syoyo): Range check
    std::string token = std::string(p, strlen(p));
    p += strlen(p) + 1;
    assert(p <= pe);

    std::cout << "token[" << i << "] = " << token << "\n";
    _tokens.push_back(token);
  }

  return true;
}

bool Parser::ReadStrings() {
  if ((_strings_index < 0) || (_strings_index >= int64_t(_toc.sections.size()))) {
    _err += "Invalid index for `STRINGS` section.\n";
    return false;
  }

  const Section &s = _toc.sections[_strings_index];

  if (!_sr->seek_set(s.start)) {
    _err += "Failed to move to `STRINGS` section.\n";
    return false;
  }

  // # of strings.
  uint64_t n;
  if (!_sr->read8(&n)) {
    _err += "Failed to read # of strings.\n";
    return false;
  }

bool Parser::ReadPaths() {
  if ((_paths_index < 0) || (_paths_index >= int64_t(_toc.sections.size()))) {
    _err += "Invalid index for `PATHS` section.\n";
    return false;
  }

  if ((_version[0] == 0) && (_version[1] < 4)) {
    _err += "Version must be 0.4.0 or later, but got " +
            std::to_string(_version[0]) + "." + std::to_string(_version[1]) +
            "." + std::to_string(_version[2]) + "\n";
    return false;
  }

  const Section &s = _toc.sections[_paths_index];

  if (!_sr->seek_set(s.start)) {
    _err += "Failed to move to `PATHS` section.\n";
    return false;
  }

  // # of paths.
  uint64_t n;
  if (!_sr->read8(&n)) {
    _err += "Failed to read # of paths.\n";
    return false;
  }

  if (!ReadCompressedPaths()) {
    _err += "Failed to read compressed paths.\n";
    return false;
  }

  std::cout << "# of paths " << n << "\n";

  return true;
}

bool Parser::ReadBootStrap() {
  // parse header.
  uint8_t magic[8];
  if (8 != _sr->read(/* req */ 8, /* dst len */ 8, magic)) {
    _err += "Failed to read magic number.\n";
    return false;
  }

  if (memcmp(magic, "PXR-USDC", 8)) {
    _err += "Invalid magic number.\n";
    return false;
  }

  // parse version(first 3 bytes from 8 bytes)
  uint8_t version[8];
  if (8 != _sr->read(8, 8, version)) {
    _err += "Failed to read magic number.\n";
    return false;
  }

  std::cout << "version = " << int(version[0]) << "." << int(version[1]) << "."
            << int(version[2]) << "\n";

  _version[0] = version[0];
  _version[1] = version[1];
  _version[2] = version[2];

  // We only support version 0.4.0 or later.
  if ((version[0] == 0) && (version[1] < 4)) {
    _err += "Version must be 0.4.0 or later, but got " +
            std::to_string(version[0]) + "." + std::to_string(version[1]) +
            "." + std::to_string(version[2]) + "\n";
    return false;
  }

  _toc_offset = 0;
  if (!_sr->read8(&_toc_offset)) {
    _err += "Failed to read TOC offset.\n";
    return false;
  }

  if ((_toc_offset <= 88) || (_toc_offset >= int64_t(_sr->size()))) {
    _err += "Invalid TOC offset value: " + std::to_string(_toc_offset) +
            ", filesize = " + std::to_string(_sr->size()) + ".\n";
    return false;
  }

  std::cout << "toc offset = " << _toc_offset << "\n";

  return true;
}

bool Parser::ReadTOC() {
  if ((_toc_offset <= 88) || (_toc_offset >= int64_t(_sr->size()))) {
    _err += "Invalid toc offset\n";
    return false;
  }

  if (!_sr->seek_set(_toc_offset)) {
    _err += "Failed to move to TOC offset\n";
    return false;
  }

  // read # of sections.
  uint64_t num_sections{0};
  if (!_sr->read8(&num_sections)) {
    _err += "Failed to read TOC(# of sections)\n";
    return false;
  }

  std::cout << "toc sections = " << num_sections << "\n";

  _toc.sections.resize(num_sections);

  for (size_t i = 0; i < num_sections; i++) {
    if (!ReadSection(&_toc.sections[i])) {
      _err += "Failed to read TOC section at " + std::to_string(i) + "\n";
      return false;
    }
    std::cout << "section[" << i << "] name = " << _toc.sections[i].name
              << ", start = " << _toc.sections[i].start
              << ", size = " << _toc.sections[i].size << "\n";

    // find index
    if (0 == strncmp(_toc.sections[i].name, "TOKENS", kSectionNameMaxLength)) {
      _tokens_index = i;
    } else if (0 ==
               strncmp(_toc.sections[i].name, "PATHS", kSectionNameMaxLength)) {
      _paths_index = i;
    }
  }

  return true;
}

}  // namespace

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

    if (sz < (11 * 8)) {
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

  Parser parser(&sr);

  if (!parser.ReadBootStrap()) {
    if (err) {
      (*err) = parser.GetError();
    }
    return false;
  }

  if (!parser.ReadTOC()) {
    if (err) {
      (*err) = parser.GetError();
    }
    return false;
  }

  // Read known sections

  if (!parser.ReadPaths()) {
    if (err) {
      (*err) = parser.GetError();
    }
    return false;
  }
  if (!parser.ReadTokens()) {
    if (err) {
      (*err) = parser.GetError();
    }
    return false;
  }

  // TODO(syoyo): Read unknown sections
  return true;
}

}  // namespace tinyusdz
