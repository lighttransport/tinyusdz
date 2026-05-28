// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - High-level API Implementation

#include "tinyusdz-next.hh"
#include "reader/usdz-reader.hh"
#include <fstream>
#include <cstring>

namespace tinyusdz {
namespace next {

namespace {

// File format detection
enum class FileFormat {
  Unknown,
  USDA,
  USDC,
  USDZ
};

FileFormat DetectFormat(const uint8_t* data, size_t size) {
  if (size < 8) return FileFormat::Unknown;

  // USDC (Crate) magic: "PXR-USDC"
  if (std::memcmp(data, "PXR-USDC", 8) == 0) {
    return FileFormat::USDC;
  }

  // USDZ (ZIP archive) magic: "PK\x03\x04"
  if (data[0] == 'P' && data[1] == 'K' && data[2] == 0x03 && data[3] == 0x04) {
    return FileFormat::USDZ;
  }

  // USDA is text - check for ASCII/UTF-8
  // Look for #usda header or common patterns
  bool is_text = true;
  for (size_t i = 0; i < std::min(size, size_t(1024)); ++i) {
    uint8_t c = data[i];
    if (c < 0x09 || (c > 0x0D && c < 0x20 && c != 0x1B)) {
      is_text = false;
      break;
    }
  }

  if (is_text) {
    return FileFormat::USDA;
  }

  return FileFormat::Unknown;
}

FileFormat DetectFormatFromExtension(const std::string& filename) {
  size_t dot = filename.rfind('.');
  if (dot == std::string::npos) return FileFormat::Unknown;

  std::string ext = filename.substr(dot);
  // Convert to lowercase
  for (char& c : ext) {
    if (c >= 'A' && c <= 'Z') c += 32;
  }

  if (ext == ".usda") return FileFormat::USDA;
  if (ext == ".usdc") return FileFormat::USDC;
  if (ext == ".usdz") return FileFormat::USDZ;
  if (ext == ".usd") return FileFormat::Unknown;  // Need content detection

  return FileFormat::Unknown;
}

bool ReadFile(const std::string& filename, std::vector<uint8_t>* data, std::string* err) {
  std::ifstream ifs(filename, std::ios::binary | std::ios::ate);
  if (!ifs) {
    if (err) *err = "Failed to open file: " + filename;
    return false;
  }

  std::streamsize size = ifs.tellg();
  ifs.seekg(0, std::ios::beg);

  data->resize(static_cast<size_t>(size));
  if (!ifs.read(reinterpret_cast<char*>(data->data()), size)) {
    if (err) *err = "Failed to read file: " + filename;
    return false;
  }

  return true;
}

}  // namespace

bool LoadUSD(const std::string& filename, Stage* stage,
             std::string* warn, std::string* err) {
  if (!stage) {
    if (err) *err = "stage is null";
    return false;
  }

  // Read file to detect format
  std::vector<uint8_t> data;
  if (!ReadFile(filename, &data, err)) {
    return false;
  }

  // Detect format
  FileFormat format = DetectFormatFromExtension(filename);
  if (format == FileFormat::Unknown) {
    format = DetectFormat(data.data(), data.size());
  }

  switch (format) {
    case FileFormat::USDA: {
      LoadResult result = LoadUSDAFromFile(filename);
      if (!result.success) {
        if (err) *err = result.error_summary;
        return false;
      }
      if (warn && !result.warnings.empty()) {
        for (const auto& w : result.warnings) {
          *warn += w + "\n";
        }
      }
      *stage = std::move(result.stage);
      return true;
    }

    case FileFormat::USDC: {
      USDCLoadResult result = LoadUSDCFromFile(filename);
      if (!result.success) {
        if (err) *err = result.error_summary;
        return false;
      }
      if (warn && !result.warnings.empty()) {
        for (const auto& w : result.warnings) {
          *warn += w + "\n";
        }
      }
      *stage = std::move(result.stage);
      return true;
    }

    case FileFormat::USDZ: {
      USDZReader usdz;
      if (!usdz.OpenFile(filename)) {
        if (err) *err = "Failed to open USDZ file";
        return false;
      }
      // Try USDC first, then USDA
      int idx = usdz.FindUSDCFile();
      FileFormat inner_fmt = FileFormat::USDC;
      if (idx < 0) {
        idx = usdz.FindUSDAFile();
        inner_fmt = FileFormat::USDA;
      }
      if (idx < 0) {
        if (err) *err = "No .usdc or .usda entry found in USDZ archive";
        return false;
      }
      const uint8_t* entry_data = usdz.EntryData(idx);
      size_t entry_size = usdz.EntrySize(idx);
      if (!entry_data || entry_size == 0) {
        if (err) *err = "Empty USD entry in USDZ";
        return false;
      }

      // Delegate to USDC or USDA reader
      if (inner_fmt == FileFormat::USDC) {
        USDCLoadResult result = LoadUSDCFromMemory(entry_data, entry_size);
        if (!result.success) {
          if (err) *err = result.error_summary;
          return false;
        }
        if (warn && !result.warnings.empty()) {
          for (const auto& w : result.warnings) *warn += w + "\n";
        }
        *stage = std::move(result.stage);
      } else {
        std::string usda_str(reinterpret_cast<const char*>(entry_data), entry_size);
        LoadResult result = LoadUSDAFromString(usda_str);
        if (!result.success) {
          if (err) *err = result.error_summary;
          return false;
        }
        if (warn && !result.warnings.empty()) {
          for (const auto& w : result.warnings) *warn += w + "\n";
        }
        *stage = std::move(result.stage);
      }
      return true;
    }

    default:
      if (err) *err = "Unknown file format";
      return false;
  }
}

bool LoadUSDA(const std::string& filename, Stage* stage,
              std::string* warn, std::string* err) {
  if (!stage) {
    if (err) *err = "stage is null";
    return false;
  }

  LoadResult result = LoadUSDAFromFile(filename);
  if (!result.success) {
    if (err) *err = result.error_summary;
    return false;
  }

  if (warn && !result.warnings.empty()) {
    for (const auto& w : result.warnings) {
      *warn += w + "\n";
    }
  }

  *stage = std::move(result.stage);
  return true;
}

bool LoadUSDC(const std::string& filename, Stage* stage,
              std::string* warn, std::string* err) {
  if (!stage) {
    if (err) *err = "stage is null";
    return false;
  }

  USDCLoadResult result = LoadUSDCFromFile(filename);
  if (!result.success) {
    if (err) *err = result.error_summary;
    return false;
  }

  if (warn && !result.warnings.empty()) {
    for (const auto& w : result.warnings) {
      *warn += w + "\n";
    }
  }

  *stage = std::move(result.stage);
  return true;
}

bool WriteUSDA(const Stage& stage, const std::string& filename,
               std::string* err) {
  USDAWriteResult result = WriteUSDAToFile(filename, stage);
  if (!result.success) {
    if (err) *err = result.error;
    return false;
  }
  return true;
}

bool WriteUSDC(const Stage& stage, const std::string& filename,
               std::string* err) {
  USDCWriteResult result = WriteUSDCToFile(filename, stage);
  if (!result.success) {
    if (err) *err = result.error;
    return false;
  }
  return true;
}

}  // namespace next
}  // namespace tinyusdz
