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

#ifndef __wasi__
#include <thread>
#endif

#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>


#include "integerCoding.h"
#include "lz4-compression.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"
#include "io-util.hh"
#include "pprinter.hh"
#include "usda-reader.hh"
#include "usdc-reader.hh"
#include "image-loader.hh"
#include "usdShade.hh"
#include "value-pprint.hh"
#include "str-util.hh"

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

  //NodeType GetNodeType() const { return _node_type; }

  const std::unordered_set<std::string> &GetPrimChildren() const {
    return _primChildren;
  }

  void SetAssetInfo(const value::dict &dict) {
    _assetInfo = dict;
  }

  const value::dict &GetAssetInfo() const { return _assetInfo; }

 private:
  int64_t
      _parent;  // -1 = this node is the root node. -2 = invalid or leaf node
  std::vector<size_t> _children;                  // index to child nodes.
  std::unordered_set<std::string> _primChildren;  // List of name of child nodes

  Path _path;  // local path
  value::dict _assetInfo;

  //NodeType _node_type;

};

}  // namespace

bool LoadUSDCFromMemory(const uint8_t *addr, const size_t length, Stage *stage,
                        std::string *warn, std::string *err,
                        const USDLoadOptions &options) {
  if (stage == nullptr) {
    if (err) {
      (*err) = "null pointer for `stage` argument.\n";
    }
    return false;
  }

  bool swap_endian = false;  // @FIXME

  if (length > size_t(1024 * 1024 * options.max_memory_limit_in_mb)) {
    if (err) {
      (*err) += "USDZ data is too large(size = " + std::to_string(length) +
                ", which exceeds memory limit " +
                std::to_string(options.max_memory_limit_in_mb) + " [mb]).\n";
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

  //DCOUT("# of paths: " << reader.NumPaths());
  //DCOUT("num_paths: " << std::to_string(parser.NumPaths()));

  //for (size_t i = 0; i < parser.NumPaths(); i++) {
    //Path path = parser.GetPath(crate::Index(uint32_t(i)));
    //DCOUT("path[" << i << "].name = " << path.full_path_name());
  //}

  // Create `Stage` object
  // std::cout << "reconstruct scene:\n";
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

  return true;
}

bool LoadUSDCFromFile(const std::string &_filename, Stage *stage,
                      std::string *warn, std::string *err,
                      const USDLoadOptions &options) {
  std::string filepath = io::ExpandFilePath(_filename, /* userdata */nullptr);

  std::vector<uint8_t> data;
  size_t max_bytes = size_t(1024 * 1024 * options.max_memory_limit_in_mb);
  if (!io::ReadWholeFile(&data, err, filepath, max_bytes, /* userdata */nullptr)) {
    return false;
  }

  DCOUT("File size: " + std::to_string(data.size()) + " bytes.");

  if (data.size() < (11 * 8)) {
    // ???
    if (err) {
      (*err) +=
          "File size too short. Looks like this file is not a USDC : \"" +
          filepath + "\"\n";
    }
    return false;
  }

  return LoadUSDCFromMemory(data.data(), data.size(), stage, warn, err,
                            options);
}

namespace {

static std::string GetFileExtension(const std::string &filename) {
  if (filename.find_last_of('.') != std::string::npos)
    return filename.substr(filename.find_last_of('.') + 1);
  return "";
}

static std::string str_tolower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); }
  );
  return s;
}

}  // namespace

bool LoadUSDZFromFile(const std::string &_filename, Stage *stage,
                      std::string *warn, std::string *err,
                      const USDLoadOptions &options) {
  // <filename, byte_begin, byte_end>
  std::vector<std::tuple<std::string, size_t, size_t>> assets;

  std::string filepath = io::ExpandFilePath(_filename, /* userdata */nullptr);

  std::vector<uint8_t> data;
  size_t max_bytes = size_t(1024 * 1024 * options.max_memory_limit_in_mb);
  if (!io::ReadWholeFile(&data, err, filepath, max_bytes, /* userdata */nullptr)) {
    return false;
  }

  if (data.size() < (11 * 8) + 30) { // 88 for USDC header, 30 for ZIP header
    // ???
    if (err) {
      (*err) +=
          "File size too short. Looks like this file is not a USDZ : \"" +
          filepath + "\"\n";
    }
    return false;
  }


  size_t offset = 0;
  while ((offset + 30) < data.size()) {
    //
    // PK zip format:
    // https://users.cs.jmu.edu/buchhofp/forensics/formats/pkzip.html
    //
    std::vector<char> local_header(30);
    memcpy(local_header.data(), data.data() + offset, 30);

    // if we've reached the global header, stop reading
    if (local_header[2] != 0x03 || local_header[3] != 0x04) break;

    offset += 30;

    // read in the variable name
    uint16_t name_len;
    memcpy(&name_len, &local_header[26], sizeof(uint16_t));
    if ((offset + name_len) > data.size()) {
      if (err) {
        (*err) += "Invalid ZIP data\n";
      }
      return false;
    }

    std::string varname(name_len, ' ');
    memcpy(&varname[0], data.data() + offset, name_len);

    offset += name_len;

    // read in the extra field
    uint16_t extra_field_len;
    memcpy(&extra_field_len, &local_header[28], sizeof(uint16_t));
    if (extra_field_len > 0) {
      if (offset + extra_field_len > data.size()) {
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

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  for (size_t i = 0; i < assets.size(); i++) {
    DCOUT("[" << i << "] " << std::get<0>(assets[i]) << " : byte range ("
              << std::get<1>(assets[i]) << ", " << std::get<2>(assets[i])
              << ")");
  }
#endif

  int32_t usdc_index = -1;
  {
    bool warned = false;  // to report single warning message.
    for (size_t i = 0; i < assets.size(); i++) {
      std::string ext = str_tolower(GetFileExtension(std::get<0>(assets[i])));
      if (ext.compare("usdc") == 0) {
        if ((usdc_index > -1) && (!warned)) {
          if (warn) {
            (*warn) +=
                "Multiple USDC files were found in USDZ. Use the first found "
                "one: " +
                std::get<0>(assets[size_t(usdc_index)]) + "]\n";
          }
          warned = true;
        }

        if (usdc_index == -1) {
          usdc_index = int32_t(i);
        }
      }
    }
  }

  if (usdc_index == -1) {
    if (err) {
      (*err) += "USDC file not found in USDZ\n";
    }
    return false;
  }

  {
    const size_t start_addr = std::get<1>(assets[size_t(usdc_index)]);
    const size_t end_addr = std::get<2>(assets[size_t(usdc_index)]);
    const size_t usdc_size = end_addr - start_addr;
    const uint8_t *usdc_addr = &data[start_addr];
    bool ret =
        LoadUSDCFromMemory(usdc_addr, usdc_size, stage, warn, err, options);

    if (!ret) {
      if (err) {
        (*err) += "Failed to load USDC.\n";
      }

      return false;
    }
  }

  // Decode images
  for (size_t i = 0; i < assets.size(); i++) {
    const std::string &uri = std::get<0>(assets[i]);
    const std::string ext = GetFileExtension(uri);

    if ((ext.compare("png") == 0) || (ext.compare("jpg") == 0) ||
        (ext.compare("jpeg") == 0)) {
      const size_t start_addr = std::get<1>(assets[i]);
      const size_t end_addr = std::get<2>(assets[i]);
      const size_t usdc_size = end_addr - start_addr;
      const uint8_t *usdc_addr = &data[start_addr];

      Image image;
      nonstd::expected<image::ImageResult, std::string> ret = image::LoadImageFromMemory(usdc_addr, usdc_size, uri);
      //bool ret = DecodeImage(usdc_addr, usdc_size, uri, &image, &_warn, &_err);

      if (!ret) {
        (*err) += ret.error();
      } else {
        image = (*ret).image;
        if (!(*ret).warning.empty()) {
          (*warn) += (*ret).warning;
        }
      }
    }
  }

  return true;
}

#ifdef _WIN32
bool LoadUSDZFromFile(const std::wstring &_filename, Stage *stage,
                      std::string *warn, std::string *err,
                      const USDLoadOptions &options) {
  std::string filename = io::WcharToUTF8(_filename);
  return LoadUSDZFromFile(filename, stage, warn, err, options);
}
#endif

bool LoadUSDAFromMemory(const uint8_t *addr, const size_t length, const std::string &base_dir, Stage *stage,
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

  std::string filepath = io::ExpandFilePath(_filename, /* userdata */nullptr);
  std::string base_dir = io::GetBaseDir(_filename);

  std::vector<uint8_t> data;
  size_t max_bytes = size_t(1024 * 1024 * options.max_memory_limit_in_mb);
  if (!io::ReadWholeFile(&data, err, filepath, max_bytes, /* userdata */nullptr)) {
    return false;
  }


  return LoadUSDAFromMemory(data.data(), data.size(), base_dir, stage, warn, err,
                            options);
}

///
/// Stage
///

namespace {

nonstd::optional<Path> GetPath(const value::Value &v)
{
  //
  // TODO: Find a better C++ way... use a std::function?
  //
  if (auto pv = v.get_value<Model>()) { return Path(pv.value().name, ""); }
  if (auto pv = v.get_value<Scope>()) { return Path(pv.value().name, ""); }
  if (auto pv = v.get_value<Xform>()) { return Path(pv.value().name, ""); }
  if (auto pv = v.get_value<GPrim>()) { return Path(pv.value().name, ""); }
  if (auto pv = v.get_value<GeomMesh>()) { return Path(pv.value().name, ""); }
  if (auto pv = v.get_value<GeomBasisCurves>()) { return Path(pv.value().name, ""); }
  if (auto pv = v.get_value<GeomSphere>()) { return Path(pv.value().name, ""); }
  if (auto pv = v.get_value<GeomCube>()) { return Path(pv.value().name, ""); }
  if (auto pv = v.get_value<GeomCylinder>()) { return Path(pv.value().name, ""); }
  if (auto pv = v.get_value<GeomCapsule>()) { return Path(pv.value().name, ""); }
  if (auto pv = v.get_value<GeomCone>()) { return Path(pv.value().name, ""); }
  if (auto pv = v.get_value<GeomSubset>()) { return Path(pv.value().name, ""); }
  if (auto pv = v.get_value<GeomCamera>()) { return Path(pv.value().name, ""); }

  if (auto pv = v.get_value<LuxDomeLight>()) { return Path(pv.value().name, ""); }
  if (auto pv = v.get_value<LuxSphereLight>()) { return Path(pv.value().name, ""); }
  //if (auto pv = v.get_value<LuxCylinderLight>()) { return Path(pv.value().name); }
  //if (auto pv = v.get_value<LuxDiskLight>()) { return Path(pv.value().name); }

  if (auto pv = v.get_value<Material>()) { return Path(pv.value().name, ""); }
  if (auto pv = v.get_value<Shader>()) { return Path(pv.value().name, ""); }
  //if (auto pv = v.get_value<UVTexture>()) { return Path(pv.value().name); }
  //if (auto pv = v.get_value<PrimvarReader()) { return Path(pv.value().name); }

  return nonstd::nullopt;
}

} // namespace

Prim::Prim(const value::Value &rhs)
{
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

Prim::Prim(value::Value &&rhs)
{
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

nonstd::optional<const Prim*> GetPrimAtPathRec(const Prim *parent, const Path &path) {

  //// TODO: Find better way to get path name from any value.
  //if (auto pv = parent.get_value<Xform>)
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


} // namespace

nonstd::expected<const Prim*, std::string> Stage::GetPrimAtPath(const Path &path)
{
  if (!path.IsValid()) {
    return nonstd::make_unexpected("Path is invalid.\n");
  }

  if (path.IsRelativePath()) {
    // TODO:
    return nonstd::make_unexpected("Relative path is TODO.\n");
  }

  if (!path.IsAbsolutePath()) {
    return nonstd::make_unexpected("Path is not absolute. Non-absolute Path is TODO.\n");
  }

  // Brute-force search.
  // TODO: Build path -> Node lookup table
  for (const auto &parent : root_nodes) {
    if (auto pv = GetPrimAtPathRec(&parent, path)) {
      return pv.value();
    }
  }

  return nonstd::make_unexpected("Cannot find path <" + path.full_path_name() + "> int the Stage.\n");
}

namespace {

void PrimPrintRec(std::stringstream &ss, const Prim &prim, uint32_t indent)
{
  ss << "\n";
  ss << pprint_value(prim.data, indent, /* closing_brace */false);

  DCOUT("num_children = " << prim.children.size());
  for (const auto &child : prim.children) {
    PrimPrintRec(ss, child, indent+1);
  }

  ss << pprint::Indent(indent) << "}\n";
}

} // namespace

std::string Stage::ExportToString() const {

  std::stringstream ss;

  ss << "#usda 1.0\n";
  ss << "(\n";
  if (stage_metas.doc.value.empty()) {
    ss << "  doc = \"Exporterd from TinyUSDZ v" << tinyusdz::version_major << "."
       << tinyusdz::version_minor << "." << tinyusdz::version_micro << "\"\n";
  } else {
    ss << "  doc = " << to_string(stage_metas.doc) << "\n";
  }

  if (stage_metas.metersPerUnit.authored()) {
    ss << "  metersPerUnit = " << stage_metas.metersPerUnit.GetValue() << "\n";
  }

  if (stage_metas.upAxis.authored()) {
    ss << "  upAxis = " << quote(to_string(stage_metas.upAxis.GetValue())) << "\n";
  }

  if (stage_metas.timeCodesPerSecond.authored()) {
    ss << "  timeCodesPerSecond = " << stage_metas.timeCodesPerSecond.GetValue() << "\n";
  }

  if (stage_metas.startTimeCode.authored()) {
    ss << "  startTimeCode = " << stage_metas.startTimeCode.GetValue() << "\n";
  }

  if (stage_metas.endTimeCode.authored()) {
    ss << "  endTimeCode = " << stage_metas.endTimeCode.GetValue() << "\n";
  }

  if (stage_metas.defaultPrim.str().size()) {
    ss << "  defaultPrim = " << tinyusdz::quote(stage_metas.defaultPrim.str()) << "\n";
  }
  if (!stage_metas.comment.value.empty()) {
    ss << "  doc = " << to_string(stage_metas.comment) << "\n";
  }

  if (stage_metas.customLayerData.size()) {
    ss << print_customData(stage_metas.customLayerData, "customLayerData", /* indent */1);
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

}  // namespace tinyusdz
