// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.

#include "usdc-writer.hh"

#if !defined(TINYUSDZ_DISABLE_MODULE_USDC_WRITER)

#if defined(_MSC_VER) || defined(__MINGW32__)
#if defined(__clang__)
// No need to define NOMINMAX for llvm-mingw
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif


#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>  // include API for expanding a file path

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif


#include <fstream>
#include <iostream>
#include <sstream>
#include <cstdio>    // for remove
#include <cstring>   // for strerror
#include <cerrno>    // for errno

#ifndef _WIN32
#include <unistd.h>  // for close, mkstemp
#endif

#include "crate-format.hh"
#include "crate-writer.hh"  // experimental CrateWriter
#include "io-util.hh"
#include "lz4-compression.hh"
#include "zstd-compression.hh"
#include "token-type.hh"

#include "common-macros.inc"

namespace tinyusdz {
namespace usdc {

namespace {

// Check if filename ends with ".zst" extension (case-insensitive)
bool HasZstdExtension(const std::string &filename) {
  if (filename.size() < 4) return false;
  std::string ext = filename.substr(filename.size() - 4);
  return (ext == ".zst" || ext == ".ZST");
}

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

//struct Field {
//  // FIXME(syoyo): Do we need 4 bytes padding as done in pxrUSD?
//  // uint32_t padding_;
//
//  crate::TokenIndex token_index;
//  crate::ValueRep value_rep;
//};


class Packer {
 public:
  crate::TokenIndex AddToken(const Token &token);
  crate::StringIndex AddString(const std::string &str);
  crate::PathIndex AddPath(const Path &path);
  crate::FieldIndex AddField(const crate::Field &field);
  crate::FieldSetIndex AddFieldSet(
      const std::vector<crate::FieldIndex> &field_indices);

  const std::vector<Token> &GetTokens() const { return tokens_; }

 private:
  std::unordered_map<Token, crate::TokenIndex, TokenHasher, TokenKeyEqual>
      token_to_index_map;
  std::unordered_map<std::string, crate::StringIndex> string_to_index_map;
  std::unordered_map<Path, crate::PathIndex, crate::PathHasher, crate::PathKeyEqual>
      path_to_index_map;
  std::unordered_map<crate::Field, crate::FieldIndex, crate::FieldHasher, crate::FieldKeyEqual>
      field_to_index_map;
  std::unordered_map<std::vector<crate::FieldIndex>, crate::FieldSetIndex,
                     crate::FieldSetHasher>
      fieldset_to_index_map;

  std::vector<Token> tokens_;
  std::vector<std::string> strings_;
  std::vector<Path> paths_;
  std::vector<crate::Field> fields_;
  std::vector<crate::FieldIndex>
      fieldsets_;  // flattened 1D array of FieldSets. Each span is terminated
                   // by Index()(= ~0)
};


class Writer {
 public:
  Writer(const Stage &stage) : stage_(stage) {}

  const Stage &stage_;

  const std::string &GetError() const { return err_; }
  const std::string &GetWarning() const { return warn_; }

  void PushError(const std::string &s) {
    err_ += s;
  }

  void PushWarn(const std::string &s) {
    warn_ += s;
  }

  bool WriteHeader(uint64_t toc_offset) {
    char magic[8];
    magic[0] = 'P';
    magic[1] = 'X';
    magic[2] = 'R';
    magic[3] = '-';
    magic[4] = 'U';
    magic[5] = 'S';
    magic[6] = 'D';
    magic[7] = 'C';

    uint8_t version[8];  // Only first 3 bytes are used.
    version[0] = 0;
    version[1] = 8;
    version[2] = 0;

    std::array<uint8_t, 88> header;
    memset(&header, 0, 88);

    memcpy(&header[0], magic, 8);
    memcpy(&header[8], version, 8);
    memcpy(&header[16], &toc_offset, 8);

    oss_.write(reinterpret_cast<const char *>(&header[0]), 88);

    return true;
  }

  bool WriteTokens() {
    // Build single string separated by '\0', then compress it with lz4
    std::ostringstream oss;

    auto tokens = packer_.GetTokens();

    for (size_t i = 0; i < tokens.size(); i++) {
      oss << tokens[i].str();

      if (i != (tokens.size() - 1)) {
        oss.put('\0');  // separator
      }
    }
    // Last string does not terminated with `\0'

    // compress
    size_t input_bytes = oss.str().size();
    if (input_bytes == 0) {
      PUSH_ERROR("Invalid data size.");
      return false;
    }

    std::vector<char> buf;
    buf.resize(LZ4Compression::GetCompressedBufferSize(input_bytes));

    std::string err;
    size_t n = LZ4Compression::CompressToBuffer(oss.str().data(), buf.data(),
                                                input_bytes, &err);

    (void)n;

    if (!err.empty()) {
      PUSH_ERROR(err);
      return false;
    }

    return true;
  }

  bool WriteStrings() { return false; }

  bool WriteFields() { return false; }

  bool WriteFieldSets() { return false; }

  bool WritePaths() { return false; }

  bool WriteSpecs() { return false; }

  bool WriteTOC() {
    uint64_t num_sections = toc_.sections.size();

    DCOUT("# of sections = " << std::to_string(num_sections));

    if (num_sections == 0) {
      err_ += "Zero sections in TOC.\n";
      return false;
    }

    // # of sections
    oss_.write(reinterpret_cast<const char *>(&num_sections), 8);

    return true;
  }

  bool Write() {
    //
    //  - TOC
    //  - Tokens
    //  - Strings
    //  - Fields
    //  - FieldSets
    //  - Paths
    //  - Specs
    //

    if (!WriteTokens()) {
      PUSH_ERROR("Failed to write Tokens.");
      return false;
    }

    if (!WriteStrings()) {
      PUSH_ERROR("Failed to write Strings.");
      return false;
    }

    if (!WriteFields()) {
      PUSH_ERROR("Failed to write Fields.");
      return false;
    }

    if (!WriteFieldSets()) {
      PUSH_ERROR("Failed to write FieldSets.");
      return false;
    }

    if (!WritePaths()) {
      PUSH_ERROR("Failed to write Paths.");
      return false;
    }

    if (!WriteSpecs()) {
      PUSH_ERROR("Failed to write Specs.");
      return false;
    }

    // TODO(syoyo): Add feature to support writing unknown section(custom user
    // data)
    // if (!WriteUnknownSections()) {
    //  PUSH_ERROR("Failed to write custom sections.");
    //  return false;
    //}

    const uint64_t toc_offset = static_cast<uint64_t>(oss_.tellp());
    if (!WriteTOC()) {
      PUSH_ERROR("Failed to write TOC.");
      return false;
    }

    // write header
    oss_.seekp(0, std::ios::beg);
    if (!WriteHeader(toc_offset)) {
      PUSH_ERROR("Failed to write Header.");
      return false;
    }

    return true;
  }

  // Get serialized USDC binary data
  bool GetOutput(std::vector<uint8_t> *output) {
    if (!err_.empty()) {
      return false;
    }

    (void)output;

    return false;
  }

 private:
  Writer() = delete;
  Writer(const Writer &) = delete;

  TableOfContents toc_;

  Packer packer_;

  //
  // Serialized data
  //
  std::ostringstream oss_;

  std::string err_;
  std::string warn_;
};

}  // namespace

bool SaveAsUSDCToFile(const std::string &filename, const Stage &stage,
                      std::string *warn, std::string *err,
                      const USDWriteOptions &options) {
#ifdef __ANDROID__
  (void)filename;
  (void)stage;
  (void)warn;
  (void)options;

  if (err) {
    (*err) += "Saving USDC to a file is not supported for Android platform(at the moment).\n";
  }
  return false;
#else

  std::vector<uint8_t> output;

  if (!SaveAsUSDCToMemory(stage, &output, warn, err)) {
    return false;
  }

  // Check if we should use zstd compression
  bool use_compression = options.use_zstd_compression || HasZstdExtension(filename);

  const uint8_t *write_data = output.data();
  size_t write_size = output.size();
  std::vector<uint8_t> compressed;

  if (use_compression) {
#ifdef TINYUSDZ_WITH_ZSTD_COMPRESSION
    if (!ZstdCompression::Compress(output.data(), output.size(),
                                   &compressed, options.zstd_compression_level, err)) {
      return false;
    }
    write_data = compressed.data();
    write_size = compressed.size();
    std::cout << "Compressing USDC with zstd (" << output.size() << " -> " << compressed.size() << " bytes)\n";
#else
    if (err) {
      (*err) = "zstd compression requested but TINYUSDZ_WITH_ZSTD_COMPRESSION is not enabled.\n";
    }
    return false;
#endif
  }

#ifdef _WIN32
#if defined(_MSC_VER) || defined(__GLIBCXX__) || defined(__clang__)
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
  errno_t fperr = fopen_s(&fp, filename.c_str(), "wb");
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

  size_t n = fwrite(write_data, /* size */ 1, /* count */ write_size, fp);
  fclose(fp);

  if (n < write_size) {
    // TODO: Retry writing data when n < write_size

    if (err) {
      (*err) += "Failed to write data to a file.\n";
    }
    return false;
  }

  return true;
#endif
}

bool SaveAsUSDCToMemory(const Stage &stage, std::vector<uint8_t> *output,
                        std::string *warn, std::string *err) {
  (void)warn;

  if (!output) {
    if (err) {
      (*err) += "Output buffer is null.\n";
    }
    return false;
  }

  // Write directly to memory via MemoryOutputStream
  auto mem_stream = std::unique_ptr<experimental::MemoryOutputStream>(
      new experimental::MemoryOutputStream());
  auto* mem_ptr = mem_stream.get();

  experimental::CrateWriter writer(
      std::unique_ptr<experimental::IOutputStream>(std::move(mem_stream)));

  experimental::CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;
  opts.enable_compression = true;
  opts.enable_deduplication = true;
  writer.SetOptions(opts);

  std::string open_err;
  if (!writer.Open(&open_err)) {
    if (err) {
      (*err) += "Failed to open CrateWriter: " + open_err + "\n";
    }
    return false;
  }

  std::string convert_err;
  if (!writer.ConvertStageToSpecs(stage, &convert_err)) {
    if (err) {
      (*err) += "Failed to convert Stage to USDC: " + convert_err + "\n";
    }
    return false;
  }

  std::string finalize_err;
  if (!writer.Finalize(&finalize_err)) {
    if (err) {
      (*err) += "Failed to finalize USDC: " + finalize_err + "\n";
    }
    return false;
  }

  writer.Close();

  *output = mem_ptr->TakeBuffer();
  return true;
}

}  // namespace usdc
}  // namespace tinyusdz

#else

namespace tinyusdz {
namespace usdc {

bool SaveAsUSDCToFile(const std::string &filename, const Stage &stage,
                      std::string *warn, std::string *err,
                      const USDWriteOptions &options) {
  (void)filename;
  (void)stage;
  (void)warn;
  (void)options;

  if (err) {
    (*err) = "USDC writer feature is disabled in this build.\n";
  }

  return false;
}

bool SaveAsUSDCToMemory(const Stage &stage, std::vector<uint8_t> *output,
                        std::string *warn, std::string *err) {
  (void)stage;
  (void)output;
  (void)warn;

  if (err) {
    (*err) = "USDC writer feature is disabled in this build.\n";
  }

  return false;
}

}  // namespace usdc
}  // namespace tinyusdz

#endif
