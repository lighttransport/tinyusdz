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
#include <cassert>
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
#include "tinyusdz.hh"
#include "usdShade.hh"
#include "usda-reader.hh"
#include "usdc-reader.hh"
#include "value-pprint.hh"
#include "tiny-format.hh"

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

//constexpr auto kTagUSDA = "[USDA]";
//constexpr auto kTagUSDC = "[USDC]";
constexpr auto kTagUSDZ = "[USDZ]";

// For PUSH_ERROR_AND_RETURN
#define PushError(s) if (err) { (*err) += s; }
//#define PushWarn(s) if (warn) { (*warn) += s; }

namespace {

///
/// Node represents scene graph node.
/// This does not contain leaf node inormation.
///
class Node {
 public:
  // -2 = initialize as invalid node
  Node() : _parent(-2) {}

  Node(int64_t parent, Path &path) : _parent(parent), _path(path) {}

  int64_t GetParent() const { return _parent; }

  const std::vector<size_t> &GetChildren() const { return _children; }

  ///
  /// child_name is used when reconstructing scene graph.
  ///
  void AddChildren(const std::string &child_name, size_t node_index) {
    assert(_primChildren.count(child_name) == 0);
    _primChildren.emplace(child_name);
    _children.push_back(node_index);
  }

  ///
  /// Get full path(e.g. `/muda/dora/bora` when the parent is `/muda/dora` and
  /// this node is `bora`)
  ///
  // std::string GetFullPath() const { return _path.full_path_name(); }

  ///
  /// Get local path
  ///
  std::string GetLocalPath() const { return _path.full_path_name(); }

  const Path &GetPath() const { return _path; }

  // NodeType GetNodeType() const { return _node_type; }

  const std::unordered_set<std::string> &GetPrimChildren() const {
    return _primChildren;
  }

  void SetAssetInfo(const value::dict &dict) { _assetInfo = dict; }

  const value::dict &GetAssetInfo() const { return _assetInfo; }

 private:
  int64_t
      _parent;  // -1 = this node is the root node. -2 = invalid or leaf node
  std::vector<size_t> _children;                  // index to child nodes.
  std::unordered_set<std::string> _primChildren;  // List of name of child nodes

  Path _path;  // local path
  value::dict _assetInfo;

  // NodeType _node_type;
};

}  // namespace

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
    if (options.max_memory_limit_in_mb > 4096) { // exceeds 4GB
      max_length = std::numeric_limits<uint32_t>::max();
    } else {
      max_length = size_t(1024) * size_t(1024) * size_t(options.max_memory_limit_in_mb);
    }
  } else {
    // TODO: Set hard limit?
    max_length = size_t(1024) * size_t(1024) * size_t(options.max_memory_limit_in_mb);
  }


  DCOUT("Max length = " << max_length);

  if (length > max_length) {
    if (err) {
      (*err) += "USDC data [" + filename +
                "] is too large(size = " + std::to_string(length) +
                ", which exceeds memory limit " +
                std::to_string(max_length) + ".\n";
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
  size_t max_bytes = size_t(1024 * 1024 * options.max_memory_limit_in_mb);
  if (!io::ReadWholeFile(&data, err, filepath, max_bytes,
                         /* userdata */ nullptr)) {
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

bool ParseUSDZHeader(const uint8_t *addr, const size_t length, std::vector<USDZAssetInfo> *assets, std::string *warn, std::string *err)
{
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


}

bool LoadUSDZFromMemory(const uint8_t *addr, const size_t length,
                        const std::string &filename, Stage *stage,
                        std::string *warn, std::string *err,
                        const USDLoadOptions &options) {
#if 0
  // <filename, byte_begin, byte_end>
  std::vector<std::tuple<std::string, size_t, size_t>> assets;

  if (!addr) {
    if (err) {
      (*err) += "null for `addr` argument.\n";
    }
    return false;
  }

  if (length < (11 * 8) + 30) {  // 88 for USDC header, 30 for ZIP header
    // ???
    if (err) {
      (*err) += "File size too short. Looks like this file is not a USDZ : \"" +
                filename + "\"\n";
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

    // std::cout << "offset = " << offset << "\n";

    // [offset, uncompr_bytes]
    assets.push_back(std::make_tuple(varname, offset, offset + uncompr_bytes));

    offset += uncompr_bytes;
  }
#else

  std::vector<USDZAssetInfo> assets;
  if (!ParseUSDZHeader(addr, length, &assets, warn, err)) {
    return false;
  }
#endif

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  for (size_t i = 0; i < assets.size(); i++) {
    DCOUT("[" << i << "] " << assets[i].filename << " : byte range ("
              << assets[i].byte_begin << ", " << assets[i].byte_end
              << ")");
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
                "one: " + assets[size_t(usdc_index)].filename + "]\n";
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
      (*warn) +=
          "Both USDA and USDC file found. Use USDC file [" +
          assets[size_t(usdc_index)].filename + "]\n";
    }
  }

  if (usdc_index >= 0) {
    const size_t start_addr_offset = assets[size_t(usdc_index)].byte_begin;
    const size_t end_addr_offset = assets[size_t(usdc_index)].byte_end;
    if (end_addr_offset < start_addr_offset) {
      if (err) {
        (*err) += "Invalid start/end offset to USDC data: [" + filename + "].\n";
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
        (*err) += "Invalid start/end offset to USDA data: [" + filename + "].\n";
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
          (*err) += "Invalid start/end offset of asset #" + std::to_string(i) + " in USDZ data: [" + filename + "].\n";
        }
        return false;
      }

      if (start_addr_offset > length) {
        if (err) {
          (*err) += "Invalid start offset of asset #" + std::to_string(i) + " in USDZ data: [" + filename + "].\n";
        }
        return false;
      }

      if (end_addr_offset > length) {
        if (err) {
          (*err) += "Invalid end offset of asset #" + std::to_string(i) + " in USDZ data: [" + filename + "].\n";
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
            PUSH_ERROR_AND_RETURN_TAG(kTagUSDZ, fmt::format("Asset no[{}] Image width too large", i));
          }

          if (info->height == 0) {
            PUSH_ERROR_AND_RETURN_TAG(kTagUSDZ, "Image has zero height.");
          }

          if (info->height > options.max_image_height) {
            PUSH_ERROR_AND_RETURN_TAG(kTagUSDZ, fmt::format("Asset no[{}] Image height too large", i));
          }

          if (info->channels == 0) {
            PUSH_ERROR_AND_RETURN_TAG(kTagUSDZ, "Image has zero channels.");
          }

          if (info->channels > options.max_image_channels) {
            PUSH_ERROR_AND_RETURN_TAG(kTagUSDZ, fmt::format("Asset no[{}] Image channels too much", i));
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
  size_t max_bytes = size_t(1024 * 1024 * options.max_memory_limit_in_mb);
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
  size_t max_bytes = size_t(1024 * 1024 * options.max_memory_limit_in_mb);
  if (!io::ReadWholeFile(&data, err, filepath, max_bytes,
                         /* userdata */ nullptr)) {
    return false;
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
  size_t max_bytes = size_t(1024 * 1024 * options.max_memory_limit_in_mb);
  if (!io::ReadWholeFile(&data, err, filepath, max_bytes,
                         /* userdata */ nullptr)) {
    return false;
  }

  return LoadUSDFromMemory(data.data(), data.size(), base_dir, stage, warn,
                            err, options);

}

bool LoadUSDFromMemory(const uint8_t *addr, const size_t length,
                        const std::string &base_dir, Stage *stage,
                        std::string *warn, std::string *err,
                        const USDLoadOptions &options) {

  if (IsUSDC(addr, length)) {
    DCOUT("Detected as USDC.");
    return LoadUSDCFromMemory(addr, length, base_dir, stage, warn, err, options);
  } else if (IsUSDA(addr, length)) {
    DCOUT("Detected as USDA.");
    return LoadUSDAFromMemory(addr, length, base_dir, stage, warn, err, options);
  } else {
    DCOUT("Detected as USDZ.");
    // Guess USDZ
    return LoadUSDZFromMemory(addr, length, base_dir, stage, warn, err, options);
  }
}

///
/// Stage
///

namespace {

nonstd::optional<Path> GetPath(const value::Value &v) {
  // Since multiple get_value() call consumes lots of stack size(depends on sizeof(T)?),
  // Following code would produce 100KB of stack in debug build.
  // So use as() instead(as() => roughly 2000 bytes for stack size).
#if 0
  //
  // TODO: Find a better C++ way... use a std::function?
  //
  if (auto pv = v.get_value<Model>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<Scope>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<Xform>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<GPrim>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<GeomMesh>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<GeomBasisCurves>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<GeomSphere>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<GeomCube>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<GeomCylinder>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<GeomCapsule>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<GeomCone>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<GeomSubset>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<GeomCamera>()) {
    return Path(pv.value().name, "");
  }

  if (auto pv = v.get_value<LuxDomeLight>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<LuxSphereLight>()) {
    return Path(pv.value().name, "");
  }
  // if (auto pv = v.get_value<LuxCylinderLight>()) { return
  // Path(pv.value().name); } if (auto pv = v.get_value<LuxDiskLight>()) {
  // return Path(pv.value().name); }

  if (auto pv = v.get_value<Material>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<Shader>()) {
    return Path(pv.value().name, "");
  }
  // if (auto pv = v.get_value<UVTexture>()) { return Path(pv.value().name); }
  // if (auto pv = v.get_value<PrimvarReader()) { return Path(pv.value().name);
  // }
#else

#define EXTRACT_NAME_AND_RETURN_PATH(__ty) if (v.as<__ty>()) { return Path(v.as<__ty>()->name, ""); }

  EXTRACT_NAME_AND_RETURN_PATH(Model)
  EXTRACT_NAME_AND_RETURN_PATH(Scope)
  EXTRACT_NAME_AND_RETURN_PATH(Xform)
  EXTRACT_NAME_AND_RETURN_PATH(GPrim)
  EXTRACT_NAME_AND_RETURN_PATH(GeomMesh)
  EXTRACT_NAME_AND_RETURN_PATH(GeomPoints)
  EXTRACT_NAME_AND_RETURN_PATH(GeomCube)
  EXTRACT_NAME_AND_RETURN_PATH(GeomCapsule)
  EXTRACT_NAME_AND_RETURN_PATH(GeomCylinder)
  EXTRACT_NAME_AND_RETURN_PATH(GeomSphere)
  EXTRACT_NAME_AND_RETURN_PATH(GeomCone)
  EXTRACT_NAME_AND_RETURN_PATH(GeomSubset)
  EXTRACT_NAME_AND_RETURN_PATH(GeomCamera)
  EXTRACT_NAME_AND_RETURN_PATH(GeomBasisCurves)
  EXTRACT_NAME_AND_RETURN_PATH(LuxDomeLight)
  EXTRACT_NAME_AND_RETURN_PATH(LuxSphereLight)
  EXTRACT_NAME_AND_RETURN_PATH(LuxCylinderLight)
  EXTRACT_NAME_AND_RETURN_PATH(LuxDiskLight)
  EXTRACT_NAME_AND_RETURN_PATH(LuxRectLight)
  EXTRACT_NAME_AND_RETURN_PATH(Material)
  EXTRACT_NAME_AND_RETURN_PATH(Shader)
  EXTRACT_NAME_AND_RETURN_PATH(UsdPreviewSurface)
  EXTRACT_NAME_AND_RETURN_PATH(UsdUVTexture)

  // TODO: primvar reader
  //EXTRACT_NAME_AND_RETURN_PATH(UsdPrimvarReader_float);

#undef EXTRACT_NAME_AND_RETURN_PATH


#endif

  return nonstd::nullopt;
}

}  // namespace

Prim::Prim(const value::Value &rhs) {
  // Check if Prim type is Model(GPrim)
  if ((value::TypeId::TYPE_ID_MODEL_BEGIN <= rhs.type_id()) &&
      (value::TypeId::TYPE_ID_MODEL_END > rhs.type_id())) {
    if (auto pv = GetPath(rhs)) {
      path = pv.value();
    }

    data = rhs;
  } else {
    // TODO: Raise an error if rhs is not an Prim
  }
}

Prim::Prim(value::Value &&rhs) {
  // Check if Prim type is Model(GPrim)
  if ((value::TypeId::TYPE_ID_MODEL_BEGIN <= rhs.type_id()) &&
      (value::TypeId::TYPE_ID_MODEL_END > rhs.type_id())) {
    data = std::move(rhs);

    if (auto pv = GetPath(data)) {
      path = pv.value();
    }

  } else {
    // TODO: Raise an error if rhs is not an Prim
  }
}

namespace {

nonstd::optional<const Prim *> GetPrimAtPathRec(const Prim *parent,
                                                const Path &path) {
  //// TODO: Find better way to get path name from any value.
  // if (auto pv = parent.get_value<Xform>)
  if (auto pv = GetPath(parent->data)) {
    if (path == pv.value()) {
      return parent;
    }
  }

  for (const auto &child : parent->children) {
    if (auto pv = GetPrimAtPathRec(&child, path)) {
      return pv.value();
    }
  }

  return nonstd::nullopt;
}

}  // namespace

nonstd::expected<const Prim *, std::string> Stage::GetPrimAtPath(
    const Path &path) {
  if (!path.IsValid()) {
    return nonstd::make_unexpected("Path is invalid.\n");
  }

  if (path.IsRelativePath()) {
    // TODO:
    return nonstd::make_unexpected("Relative path is TODO.\n");
  }

  if (!path.IsAbsolutePath()) {
    return nonstd::make_unexpected(
        "Path is not absolute. Non-absolute Path is TODO.\n");
  }

  // Brute-force search.
  // TODO: Build path -> Node lookup table
  for (const auto &parent : root_nodes) {
    if (auto pv = GetPrimAtPathRec(&parent, path)) {
      return pv.value();
    }
  }

  return nonstd::make_unexpected("Cannot find path <" + path.full_path_name() +
                                 "> int the Stage.\n");
}

namespace {

void PrimPrintRec(std::stringstream &ss, const Prim &prim, uint32_t indent) {
  ss << "\n";
  ss << pprint_value(prim.data, indent, /* closing_brace */ false);

  DCOUT("num_children = " << prim.children.size());
  for (const auto &child : prim.children) {
    PrimPrintRec(ss, child, indent + 1);
  }

  ss << pprint::Indent(indent) << "}\n";
}

}  // namespace

std::string Stage::ExportToString() const {
  std::stringstream ss;

  ss << "#usda 1.0\n";
  ss << "(\n";
  if (stage_metas.doc.value.empty()) {
    ss << "  doc = \"Exporterd from TinyUSDZ v" << tinyusdz::version_major
       << "." << tinyusdz::version_minor << "." << tinyusdz::version_micro
       << "\"\n";
  } else {
    ss << "  doc = " << to_string(stage_metas.doc) << "\n";
  }

  if (stage_metas.metersPerUnit.authored()) {
    ss << "  metersPerUnit = " << stage_metas.metersPerUnit.GetValue() << "\n";
  }

  if (stage_metas.upAxis.authored()) {
    ss << "  upAxis = " << quote(to_string(stage_metas.upAxis.GetValue()))
       << "\n";
  }

  if (stage_metas.timeCodesPerSecond.authored()) {
    ss << "  timeCodesPerSecond = " << stage_metas.timeCodesPerSecond.GetValue()
       << "\n";
  }

  if (stage_metas.startTimeCode.authored()) {
    ss << "  startTimeCode = " << stage_metas.startTimeCode.GetValue() << "\n";
  }

  if (stage_metas.endTimeCode.authored()) {
    ss << "  endTimeCode = " << stage_metas.endTimeCode.GetValue() << "\n";
  }

  if (stage_metas.defaultPrim.str().size()) {
    ss << "  defaultPrim = " << tinyusdz::quote(stage_metas.defaultPrim.str())
       << "\n";
  }
  if (!stage_metas.comment.value.empty()) {
    ss << "  doc = " << to_string(stage_metas.comment) << "\n";
  }

  if (stage_metas.customLayerData.size()) {
    ss << print_customData(stage_metas.customLayerData, "customLayerData",
                           /* indent */ 1);
  }

  // TODO: Sort by line_no?(preserve appearance in read USDA)
  for (const auto &item : stage_metas.stringData) {
    ss << "  " << to_string(item) << "\n";
  }

  // TODO: write other header data.
  ss << ")\n";
  ss << "\n";

  for (const auto &item : root_nodes) {
    PrimPrintRec(ss, item, 0);
  }

  return ss.str();
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
  const char header[9+1] = "#usda 1.0";

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
  if (!io::ReadFileHeader(&data, &err, filename, /* header bytes */88,
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
  const char header[8+1] = "PXR-USDC";

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

  return ParseUSDZHeader(addr, length, /* [out] assets */nullptr, &warn, &err);
}

}  // namespace tinyusdz
