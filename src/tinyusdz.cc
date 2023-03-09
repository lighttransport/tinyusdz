/*
Copyright (c) 2019 - Present, Syoyo Fujita.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Syoyo Fujita nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <algorithm>
#include <atomic>
//#include <cassert>
#include <cctype>  // std::tolower
#include <chrono>
#include <fstream>
#include <map>
#include <sstream>

#include "usdLux.hh"

#ifndef __wasi__
#include <thread>
#endif

#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "image-loader.hh"
#include "integerCoding.h"
#include "io-util.hh"
#include "lz4-compression.hh"
#include "pprinter.hh"
#include "str-util.hh"
#include "stream-reader.hh"
#include "tiny-format.hh"
#include "tinyusdz.hh"
#include "usda-reader.hh"
#include "usdc-reader.hh"
#include "value-pprint.hh"

#if 0
#if defined(TINYUSDZ_WITH_AUDIO)

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#define DR_WAV_IMPLEMENTATION
#include "external/dr_wav.h"

#define DR_MP3_IMPLEMENTATION
#include "external/dr_mp3.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif  // TINYUSDZ_WITH_AUDIO

#if defined(TINYUSDZ_WITH_OPENSUBDIV)

#include "subdiv.hh"

#endif

#endif

#include "common-macros.inc"

namespace tinyusdz {

// constexpr auto kTagUSDA = "[USDA]";
// constexpr auto kTagUSDC = "[USDC]";
constexpr auto kTagUSDZ = "[USDZ]";

// For PUSH_ERROR_AND_RETURN
#define PushError(s) \
  if (err) {         \
    (*err) += s;     \
  }
//#define PushWarn(s) if (warn) { (*warn) += s; }

bool LoadUSDCFromMemory(const uint8_t *addr, const size_t length,
                        const std::string &filename, Stage *stage,
                        std::string *warn, std::string *err,
                        const USDLoadOptions &options) {
  if (stage == nullptr) {
    if (err) {
      (*err) = "null pointer for `stage` argument.\n";
    }
    return false;
  }

  bool swap_endian = false;  // @FIXME

  size_t max_length;

  // 32bit env
  if (sizeof(void *) == 4) {
    if (options.max_memory_limit_in_mb > 4096) {  // exceeds 4GB
      max_length = std::numeric_limits<uint32_t>::max();
    } else {
      max_length =
          size_t(1024) * size_t(1024) * size_t(options.max_memory_limit_in_mb);
    }
  } else {
    // TODO: Set hard limit?
    max_length =
        size_t(1024) * size_t(1024) * size_t(options.max_memory_limit_in_mb);
  }

  DCOUT("Max length = " << max_length);

  if (length > max_length) {
    if (err) {
      (*err) += "USDC data [" + filename +
                "] is too large(size = " + std::to_string(length) +
                ", which exceeds memory limit " + std::to_string(max_length) +
                ".\n";
    }

    return false;
  }

  StreamReader sr(addr, length, swap_endian);

  usdc::USDCReaderConfig config;
  config.numThreads = options.num_threads;
  usdc::USDCReader reader(&sr, config);

  if (!reader.ReadUSDC()) {
    if (warn) {
      (*warn) = reader.GetWarning();
    }

    if (err) {
      (*err) = reader.GetError();
    }
    return false;
  }

  DCOUT("Loaded USDC file.");

  // Reconstruct `Stage`(scene) object
  {
    if (!reader.ReconstructStage(stage)) {
      DCOUT("Failed to reconstruct Stage from Crate.");
      if (warn) {
        (*warn) = reader.GetWarning();
      }

      if (err) {
        (*err) = reader.GetError();
      }
      return false;
    }
  }

  if (warn) {
    (*warn) = reader.GetWarning();
  }

  // Reconstruct OK but may have some error.
  // TODO(syoyo): Return false in strict mode.
  if (err) {
    DCOUT(reader.GetError());
    (*err) = reader.GetError();
  }

  DCOUT("Reconstructed Stage from USDC file.");

  return true;
}

bool LoadUSDCFromFile(const std::string &_filename, Stage *stage,
                      std::string *warn, std::string *err,
                      const USDLoadOptions &options) {
  std::string filepath = io::ExpandFilePath(_filename, /* userdata */ nullptr);

  std::vector<uint8_t> data;
  size_t max_bytes = 1024 * 1024 * size_t(options.max_memory_limit_in_mb);
  if (!io::ReadWholeFile(&data, err, filepath, max_bytes,
                         /* userdata */ nullptr)) {
    if (err) {
      (*err) += "File not found or failed to read : \"" + filepath + "\"\n";
    }

    return false;
  }

  DCOUT("File size: " + std::to_string(data.size()) + " bytes.");

  if (data.size() < (11 * 8)) {
    // ???
    if (err) {
      (*err) += "File size too short. Looks like this file is not a USDC : \"" +
                filepath + "\"\n";
    }
    return false;
  }

  return LoadUSDCFromMemory(data.data(), data.size(), filepath, stage, warn,
                            err, options);
}

namespace {

static std::string GetFileExtension(const std::string &filename) {
  if (filename.find_last_of('.') != std::string::npos)
    return filename.substr(filename.find_last_of('.') + 1);
  return "";
}

static std::string str_tolower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

}  // namespace

namespace {

struct USDZAssetInfo {
  std::string filename;
  size_t byte_begin;
  size_t byte_end;
};

bool ParseUSDZHeader(const uint8_t *addr, const size_t length,
                     std::vector<USDZAssetInfo> *assets, std::string *warn,
                     std::string *err) {
  (void)warn;

  if (!addr) {
    if (err) {
      (*err) += "null for `addr` argument.\n";
    }
    return false;
  }

  if (length < (11 * 8) + 30) {  // 88 for USDC header, 30 for ZIP header
    // ???
    if (err) {
      (*err) += "File size too short. Looks like this file is not a USDZ\n";
    }
    return false;
  }

  size_t offset = 0;
  while ((offset + 30) < length) {
    //
    // PK zip format:
    // https://users.cs.jmu.edu/buchhofp/forensics/formats/pkzip.html
    //
    std::vector<char> local_header(30);
    memcpy(local_header.data(), addr + offset, 30);

    // if we've reached the global header, stop reading
    if (local_header[2] != 0x03 || local_header[3] != 0x04) break;

    offset += 30;

    // read in the variable name
    uint16_t name_len;
    memcpy(&name_len, &local_header[26], sizeof(uint16_t));
    if ((offset + name_len) > length) {
      if (err) {
        (*err) += "Invalid ZIP data\n";
      }
      return false;
    }

    std::string varname(name_len, ' ');
    memcpy(&varname[0], addr + offset, name_len);

    offset += name_len;

    // read in the extra field
    uint16_t extra_field_len;
    memcpy(&extra_field_len, &local_header[28], sizeof(uint16_t));
    if (extra_field_len > 0) {
      if (offset + extra_field_len > length) {
        if (err) {
          (*err) += "Invalid extra field length in ZIP data\n";
        }
        return false;
      }
    }

    offset += extra_field_len;

    // In usdz, data must be aligned at 64bytes boundary.
    if ((offset % 64) != 0) {
      if (err) {
        (*err) += "Data offset must be mulitple of 64bytes for USDZ, but got " +
                  std::to_string(offset) + ".\n";
      }
      return false;
    }

    uint16_t compr_method = *reinterpret_cast<uint16_t *>(&local_header[0] + 8);
    // uint32_t compr_bytes = *reinterpret_cast<uint32_t*>(&local_header[0]+18);
    uint32_t uncompr_bytes =
        *reinterpret_cast<uint32_t *>(&local_header[0] + 22);

    // USDZ only supports uncompressed ZIP
    if (compr_method != 0) {
      if (err) {
        (*err) += "Compressed ZIP is not supported for USDZ\n";
      }
      return false;
    }

    if (assets) {
      USDZAssetInfo info;
      info.filename = varname;
      info.byte_begin = offset;
      info.byte_end = offset + uncompr_bytes;

      assets->push_back(info);
    }

    offset += uncompr_bytes;
  }

  return true;
}

}  // namespace

bool LoadUSDZFromMemory(const uint8_t *addr, const size_t length,
                        const std::string &filename, Stage *stage,
                        std::string *warn, std::string *err,
                        const USDLoadOptions &options) {
  std::vector<USDZAssetInfo> assets;
  if (!ParseUSDZHeader(addr, length, &assets, warn, err)) {
    return false;
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  for (size_t i = 0; i < assets.size(); i++) {
    DCOUT("[" << i << "] " << assets[i].filename << " : byte range ("
              << assets[i].byte_begin << ", " << assets[i].byte_end << ")");
  }
#endif

  int32_t usdc_index = -1;
  int32_t usda_index = -1;
  {
    bool warned = false;  // to report single warning message.
    for (size_t i = 0; i < assets.size(); i++) {
      std::string ext = str_tolower(GetFileExtension(assets[i].filename));
      if (ext.compare("usdc") == 0) {
        if ((usdc_index > -1) && (!warned)) {
          if (warn) {
            (*warn) +=
                "Multiple USDC files were found in USDZ. Use the first found "
                "one: " +
                assets[size_t(usdc_index)].filename + "]\n";
          }
          warned = true;
        }

        if (usdc_index == -1) {
          usdc_index = int32_t(i);
        }
      } else if (ext.compare("usda") == 0) {
        if ((usda_index > -1) && (!warned)) {
          if (warn) {
            (*warn) +=
                "Multiple USDA files were found in USDZ. Use the first found "
                "one: " +
                assets[size_t(usda_index)].filename + "]\n";
          }
          warned = true;
        }
        if (usda_index == -1) {
          usda_index = int32_t(i);
        }
      }
    }
  }

  if ((usdc_index == -1) && (usda_index == -1)) {
    if (err) {
      (*err) += "Neither USDC nor USDA file found in USDZ\n";
    }
    return false;
  }

  if ((usdc_index >= 0) && (usda_index >= 0)) {
    if (warn) {
      (*warn) += "Both USDA and USDC file found. Use USDC file [" +
                 assets[size_t(usdc_index)].filename + "]\n";
    }
  }

  if (usdc_index >= 0) {
    const size_t start_addr_offset = assets[size_t(usdc_index)].byte_begin;
    const size_t end_addr_offset = assets[size_t(usdc_index)].byte_end;
    if (end_addr_offset < start_addr_offset) {
      if (err) {
        (*err) +=
            "Invalid start/end offset to USDC data: [" + filename + "].\n";
      }
      return false;
    }
    const size_t usdc_size = end_addr_offset - start_addr_offset;

    if (start_addr_offset > length) {
      if (err) {
        (*err) += "Invalid start offset to USDC data: [" + filename + "].\n";
      }
      return false;
    }

    if (end_addr_offset > length) {
      if (err) {
        (*err) += "Invalid end offset to USDC data: [" + filename + "].\n";
      }
      return false;
    }

    const uint8_t *usdc_addr = addr + start_addr_offset;
    bool ret = LoadUSDCFromMemory(usdc_addr, usdc_size, filename, stage, warn,
                                  err, options);

    if (!ret) {
      if (err) {
        (*err) += "Failed to load USDC: [" + filename + "].\n";
      }

      return false;
    }
  } else if (usda_index >= 0) {
    const size_t start_addr_offset = assets[size_t(usda_index)].byte_begin;
    const size_t end_addr_offset = assets[size_t(usda_index)].byte_end;
    if (end_addr_offset < start_addr_offset) {
      if (err) {
        (*err) +=
            "Invalid start/end offset to USDA data: [" + filename + "].\n";
      }
      return false;
    }
    const size_t usda_size = end_addr_offset - start_addr_offset;

    if (start_addr_offset > length) {
      if (err) {
        (*err) += "Invalid start offset to USDA data: [" + filename + "].\n";
      }
      return false;
    }

    if (end_addr_offset > length) {
      if (err) {
        (*err) += "Invalid end offset to USDA data: [" + filename + "].\n";
      }
      return false;
    }

    const uint8_t *usda_addr = addr + start_addr_offset;
    bool ret = LoadUSDAFromMemory(usda_addr, usda_size, filename, stage, warn,
                                  err, options);

    if (!ret) {
      if (err) {
        (*err) += "Failed to load USDA: [" + filename + "].\n";
      }

      return false;
    }
  }

  // Decode images
  for (size_t i = 0; i < assets.size(); i++) {
    const std::string &uri = assets[i].filename;
    const std::string ext = GetFileExtension(uri);

    if ((ext.compare("png") == 0) || (ext.compare("jpg") == 0) ||
        (ext.compare("jpeg") == 0)) {
      const size_t start_addr_offset = assets[i].byte_begin;
      const size_t end_addr_offset = assets[i].byte_end;
      const size_t asset_size = end_addr_offset - start_addr_offset;
      const uint8_t *asset_addr = addr + start_addr_offset;

      if (end_addr_offset < start_addr_offset) {
        if (err) {
          (*err) += "Invalid start/end offset of asset #" + std::to_string(i) +
                    " in USDZ data: [" + filename + "].\n";
        }
        return false;
      }

      if (start_addr_offset > length) {
        if (err) {
          (*err) += "Invalid start offset of asset #" + std::to_string(i) +
                    " in USDZ data: [" + filename + "].\n";
        }
        return false;
      }

      if (end_addr_offset > length) {
        if (err) {
          (*err) += "Invalid end offset of asset #" + std::to_string(i) +
                    " in USDZ data: [" + filename + "].\n";
        }
        return false;
      }

      if (asset_size > (options.max_allowed_asset_size_in_mb * 1024 * 1024)) {
        PUSH_ERROR_AND_RETURN_TAG(kTagUSDZ, "Asset file size too large.");
      }

      DCOUT("Image asset size: " << asset_size);

      {
        nonstd::expected<image::ImageInfoResult, std::string> info =
            image::GetImageInfoFromMemory(asset_addr, asset_size, uri);

        if (info) {
          if (info->width == 0) {
            PUSH_ERROR_AND_RETURN_TAG(kTagUSDZ, "Image has zero width.");
          }

          if (info->width > options.max_image_width) {
            PUSH_ERROR_AND_RETURN_TAG(
                kTagUSDZ, fmt::format("Asset no[{}] Image width too large", i));
          }

          if (info->height == 0) {
            PUSH_ERROR_AND_RETURN_TAG(kTagUSDZ, "Image has zero height.");
          }

          if (info->height > options.max_image_height) {
            PUSH_ERROR_AND_RETURN_TAG(
                kTagUSDZ,
                fmt::format("Asset no[{}] Image height too large", i));
          }

          if (info->channels == 0) {
            PUSH_ERROR_AND_RETURN_TAG(kTagUSDZ, "Image has zero channels.");
          }

          if (info->channels > options.max_image_channels) {
            PUSH_ERROR_AND_RETURN_TAG(
                kTagUSDZ,
                fmt::format("Asset no[{}] Image channels too much", i));
          }
        }
      }

      Image image;
      nonstd::expected<image::ImageResult, std::string> ret =
          image::LoadImageFromMemory(asset_addr, asset_size, uri);

      if (!ret) {
        (*err) += ret.error();
      } else {
        image = (*ret).image;
        if (!(*ret).warning.empty()) {
          (*warn) += (*ret).warning;
        }
      }
    } else {
      // TODO: Support other asserts(e.g. audio mp3)
    }
  }

  return true;
}

bool LoadUSDZFromFile(const std::string &_filename, Stage *stage,
                      std::string *warn, std::string *err,
                      const USDLoadOptions &options) {
  // <filename, byte_begin, byte_end>
  std::vector<std::tuple<std::string, size_t, size_t>> assets;

  std::string filepath = io::ExpandFilePath(_filename, /* userdata */ nullptr);

  std::vector<uint8_t> data;
  size_t max_bytes = 1024 * 1024 * size_t(options.max_memory_limit_in_mb);
  if (!io::ReadWholeFile(&data, err, filepath, max_bytes,
                         /* userdata */ nullptr)) {
    return false;
  }

  if (data.size() < (11 * 8) + 30) {  // 88 for USDC header, 30 for ZIP header
    // ???
    if (err) {
      (*err) += "File size too short. Looks like this file is not a USDZ : \"" +
                filepath + "\"\n";
    }
    return false;
  }

  return LoadUSDZFromMemory(data.data(), data.size(), filepath, stage, warn,
                            err, options);
}

#ifdef _WIN32
bool LoadUSDZFromFile(const std::wstring &_filename, Stage *stage,
                      std::string *warn, std::string *err,
                      const USDLoadOptions &options) {
  std::string filename = io::WcharToUTF8(_filename);
  return LoadUSDZFromFile(filename, stage, warn, err, options);
}
#endif

bool LoadUSDAFromMemory(const uint8_t *addr, const size_t length,
                        const std::string &base_dir, Stage *stage,
                        std::string *warn, std::string *err,
                        const USDLoadOptions &options) {
  (void)warn;

  if (addr == nullptr) {
    if (err) {
      (*err) = "null pointer for `addr` argument.\n";
    }
    return false;
  }

  if (stage == nullptr) {
    if (err) {
      (*err) = "null pointer for `stage` argument.\n";
    }
    return false;
  }

  tinyusdz::StreamReader sr(addr, length, /* swap endian */ false);
  tinyusdz::usda::USDAReader reader(&sr);

  reader.SetBaseDir(base_dir);

  (void)options;

  {
    bool ret = reader.Read();

    if (!ret) {
      if (err) {
        (*err) += "Failed to parse USDA\n";
        (*err) += reader.GetError();
      }

      return false;
    }
  }

  {
    bool ret = reader.ReconstructStage();
    if (!ret) {
      if (err) {
        (*err) += "Failed to reconstruct Stage from USDA:\n";
        (*err) += reader.GetError() + "\n";
      }
      return false;
    }
  }

  (*stage) = reader.GetStage();

  return true;
}

bool LoadUSDAFromFile(const std::string &_filename, Stage *stage,
                      std::string *warn, std::string *err,
                      const USDLoadOptions &options) {
  std::string filepath = io::ExpandFilePath(_filename, /* userdata */ nullptr);
  std::string base_dir = io::GetBaseDir(_filename);

  std::vector<uint8_t> data;
  size_t max_bytes = 1024 * 1024 * size_t(options.max_memory_limit_in_mb);
  if (!io::ReadWholeFile(&data, err, filepath, max_bytes,
                         /* userdata */ nullptr)) {
    if (err) {
      (*err) += "File not found or failed to read : \"" + filepath + "\"\n";
    }
  }

  return LoadUSDAFromMemory(data.data(), data.size(), base_dir, stage, warn,
                            err, options);
}

bool LoadUSDFromFile(const std::string &_filename, Stage *stage,
                     std::string *warn, std::string *err,
                     const USDLoadOptions &options) {
  std::string filepath = io::ExpandFilePath(_filename, /* userdata */ nullptr);
  std::string base_dir = io::GetBaseDir(_filename);

  std::vector<uint8_t> data;
  size_t max_bytes = 1024 * 1024 * size_t(options.max_memory_limit_in_mb);
  if (!io::ReadWholeFile(&data, err, filepath, max_bytes,
                         /* userdata */ nullptr)) {
    return false;
  }

  return LoadUSDFromMemory(data.data(), data.size(), base_dir, stage, warn, err,
                           options);
}

bool LoadUSDFromMemory(const uint8_t *addr, const size_t length,
                       const std::string &base_dir, Stage *stage,
                       std::string *warn, std::string *err,
                       const USDLoadOptions &options) {
  if (IsUSDC(addr, length)) {
    DCOUT("Detected as USDC.");
    return LoadUSDCFromMemory(addr, length, base_dir, stage, warn, err,
                              options);
  } else if (IsUSDA(addr, length)) {
    DCOUT("Detected as USDA.");
    return LoadUSDAFromMemory(addr, length, base_dir, stage, warn, err,
                              options);
  } else {
    DCOUT("Detected as USDZ.");
    // Guess USDZ
    return LoadUSDZFromMemory(addr, length, base_dir, stage, warn, err,
                              options);
  }
}

//
// File type detection
//

bool IsUSDA(const std::string &filename) {
  // TODO: Read first few bytes and check the magic number.
  //
  std::vector<uint8_t> data;
  std::string err;
  // 12 = enough storage for "#usda 1.0"
  if (!io::ReadFileHeader(&data, &err, filename, 12,
                          /* userdata */ nullptr)) {
    // TODO: return `err`
    return false;
  }

  return IsUSDA(data.data(), data.size());
}

bool IsUSDA(const uint8_t *addr, const size_t length) {
  if (length < 9) {
    return false;
  }
  const char header[9 + 1] = "#usda 1.0";

  if (memcmp(header, addr, 9) == 0) {
    return true;
  }

  return false;
}

bool IsUSDC(const std::string &filename) {
  // TODO: Read first few bytes and check the magic number.
  //
  std::vector<uint8_t> data;
  std::string err;
  // 88 bytes should enough
  if (!io::ReadFileHeader(&data, &err, filename, /* header bytes */ 88,
                          /* userdata */ nullptr)) {
    return false;
  }

  return IsUSDC(data.data(), data.size());
}

bool IsUSDC(const uint8_t *addr, const size_t length) {
  // must be 88bytes or more
  if (length < 88) {
    return false;
  }
  const char header[8 + 1] = "PXR-USDC";

  if (memcmp(header, addr, 8) == 0) {
    return true;
  }

  return false;
}

bool IsUSDZ(const std::string &filename) {
  // TODO: Read first few bytes and check the magic number.
  //
  std::vector<uint8_t> data;
  std::string err;
  // 256 bytes may be enough.
  if (!io::ReadFileHeader(&data, &err, filename, 256,
                          /* userdata */ nullptr)) {
    return false;
  }

  return IsUSDZ(data.data(), data.size());
}

bool IsUSDZ(const uint8_t *addr, const size_t length) {
  std::string warn;
  std::string err;

  return ParseUSDZHeader(addr, length, /* [out] assets */ nullptr, &warn, &err);
}

bool IsUSD(const std::string &filename, std::string *detected_format) {
  if (IsUSDA(filename)) {
    if (detected_format) {
      (*detected_format) = "usda";
    }
    return true;
  }

  if (IsUSDC(filename)) {
    if (detected_format) {
      (*detected_format) = "usdc";
    }
    return true;
  }

  if (IsUSDZ(filename)) {
    if (detected_format) {
      (*detected_format) = "usdz";
    }
    return true;
  }

  return false;
}

bool IsUSD(const uint8_t *addr, const size_t length, std::string *detected_format) {
  if (IsUSDA(addr, length)) {
    if (detected_format) {
      (*detected_format) = "usda";
    }
    return true;
  }

  if (IsUSDC(addr, length)) {
    if (detected_format) {
      (*detected_format) = "usdc";
    }
    return true;
  }

  if (IsUSDZ(addr, length)) {
    if (detected_format) {
      (*detected_format) = "usdz";
    }
    return true;
  }

  return false;
}

}  // namespace tinyusdz
