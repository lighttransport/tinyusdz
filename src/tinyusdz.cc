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
#include "usda-parser.hh"
#include "usdc-parser.hh"

#if defined(TINYUSDZ_SUPPORT_AUDIO)

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

#endif  // TINYUSDZ_SUPPORT_AUDIO

#if defined(TINYUSDZ_USE_OPENSUBDIV)

#include "subdiv.hh"

#endif

#if defined(TINYUSDZ_SUPPORT_EXR)
#include "external/tinyexr.h"
#endif

#ifndef TINYUSDZ_NO_STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/stb_image.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#if defined(TINYUSDZ_PRODUCTION_BUILD)
#define TINYUSDZ_LOCAL_DEBUG_PRINT
#endif

#if defined(TINYUSDZ_LOCAL_DEBUG_PRINT)
#define DCOUT(x) do { std::cout << __FILE__ << ":" << __func__ << ":" << std::to_string(__LINE__) << " " << x << "\n"; } while (false)
#else
#define DCOUT(x) do { (void)(x); } while(false)
#endif

namespace tinyusdz {

namespace {

// Decode image(png, jpg, ...)
static bool DecodeImage(const uint8_t *bytes, const size_t size,
                        const std::string &uri, Image *image, std::string *warn,
                        std::string *err) {
  (void)warn;

  int w = 0, h = 0, comp = 0, req_comp = 0;

  unsigned char *data = nullptr;

  // force 32-bit textures for common Vulkan compatibility. It appears that
  // some GPU drivers do not support 24-bit images for Vulkan
  req_comp = 4;
  int bits = 8;

  // It is possible that the image we want to load is a 16bit per channel image
  // We are going to attempt to load it as 16bit per channel, and if it worked,
  // set the image data accodingly. We are casting the returned pointer into
  // unsigned char, because we are representing "bytes". But we are updating
  // the Image metadata to signal that this image uses 2 bytes (16bits) per
  // channel:
  if (stbi_is_16_bit_from_memory(bytes, int(size))) {
    data = reinterpret_cast<unsigned char *>(
        stbi_load_16_from_memory(bytes, int(size), &w, &h, &comp, req_comp));
    if (data) {
      bits = 16;
    }
  }

  // at this point, if data is still NULL, it means that the image wasn't
  // 16bit per channel, we are going to load it as a normal 8bit per channel
  // mage as we used to do:
  // if image cannot be decoded, ignore parsing and keep it by its path
  // don't break in this case
  // FIXME we should only enter this function if the image is embedded. If
  // `uri` references an image file, it should be left as it is. Image loading
  // should not be mandatory (to support other formats)
  if (!data)
    data = stbi_load_from_memory(bytes, int(size), &w, &h, &comp, req_comp);
  if (!data) {
    // NOTE: you can use `warn` instead of `err`
    if (err) {
      (*err) +=
          "Unknown image format. STB cannot decode image data for image: " +
          uri + "\".\n";
    }
    return false;
  }

  if ((w < 1) || (h < 1)) {
    stbi_image_free(data);
    if (err) {
      (*err) += "Invalid image data for image: " + uri + "\"\n";
    }
    return false;
  }

  image->width = w;
  image->height = h;
  image->channels = req_comp;
  image->bpp = bits;
  image->data.resize(static_cast<size_t>(w * h * req_comp) * size_t(bits / 8));
  std::copy(data, data + w * h * req_comp * (bits / 8), image->data.begin());
  stbi_image_free(data);

  return true;
}


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

  NodeType GetNodeType() const { return _node_type; }

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

  NodeType _node_type;
};

}  // namespace

bool LoadUSDCFromMemory(const uint8_t *addr, const size_t length, Scene *scene,
                        std::string *warn, std::string *err,
                        const USDLoadOptions &options) {
  if (scene == nullptr) {
    if (err) {
      (*err) = "null pointer for `scene` argument.\n";
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

  usdc::Parser parser(&sr, options.num_threads);

  if (!parser.ReadBootStrap()) {
    if (warn) {
      (*warn) = parser.GetWarning();
    }

    if (err) {
      (*err) = parser.GetError();
    }
    return false;
  }

  if (!parser.ReadTOC()) {
    if (warn) {
      (*warn) = parser.GetWarning();
    }

    if (err) {
      (*err) = parser.GetError();
    }
    return false;
  }

  // Read known sections

  if (!parser.ReadTokens()) {
    if (warn) {
      (*warn) = parser.GetWarning();
    }

    if (err) {
      (*err) = parser.GetError();
    }
    return false;
  }

  if (!parser.ReadStrings()) {
    if (warn) {
      (*warn) = parser.GetWarning();
    }

    if (err) {
      (*err) = parser.GetError();
    }
    return false;
  }

  if (!parser.ReadFields()) {
    if (warn) {
      (*warn) = parser.GetWarning();
    }

    if (err) {
      (*err) = parser.GetError();
    }
    return false;
  }

  if (!parser.ReadFieldSets()) {
    if (warn) {
      (*warn) = parser.GetWarning();
    }

    if (err) {
      (*err) = parser.GetError();
    }
    return false;
  }

  if (!parser.ReadPaths()) {
    if (warn) {
      (*warn) = parser.GetWarning();
    }

    if (err) {
      (*err) = parser.GetError();
    }
    return false;
  }

  if (!parser.ReadSpecs()) {
    if (warn) {
      (*warn) = parser.GetWarning();
    }

    if (err) {
      (*err) = parser.GetError();
    }
    return false;
  }

  // TODO(syoyo): Read unknown sections

  ///
  /// Reconstruct C++ representation of USD scene graph.
  ///
  if (!parser.BuildLiveFieldSets()) {
    if (warn) {
      (*warn) = parser.GetWarning();
    }

    if (err) {
      (*err) = parser.GetError();
    }
  }

  //DCOUT("num_paths: " << std::to_string(parser.NumPaths()));

  for (size_t i = 0; i < parser.NumPaths(); i++) {
    Path path = parser.GetPath(crate::Index(uint32_t(i)));
    //DCOUT("path[" << i << "].name = " << path.full_path_name());
  }

  // Create `Scene` object
  // std::cout << "reconstruct scene:\n";
  {
    if (!parser.ReconstructScene(scene)) {
      if (warn) {
        (*warn) = parser.GetWarning();
      }

      if (err) {
        (*err) = parser.GetError();
      }
      return false;
    }
  }

  if (warn) {
    (*warn) = parser.GetWarning();
  }

  return true;
}

bool LoadUSDCFromFile(const std::string &_filename, Scene *scene,
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

  return LoadUSDCFromMemory(data.data(), data.size(), scene, warn, err,
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

bool LoadUSDZFromFile(const std::string &_filename, Scene *scene,
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

    // std::cout << "varname = " << varname << "\n";

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
        LoadUSDCFromMemory(usdc_addr, usdc_size, scene, warn, err, options);

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
      std::string _warn, _err;
      bool ret = DecodeImage(usdc_addr, usdc_size, uri, &image, &_warn, &_err);

      if (!_warn.empty()) {
        if (warn) {
          (*warn) += _warn;
        }
      }

      if (!_err.empty()) {
        if (err) {
          (*err) += _err;
        }
      }

      if (!ret) {
      }
    }
  }

  return true;
}

#ifdef _WIN32
bool LoadUSDZFromFile(const std::wstring &_filename, Scene *scene,
                      std::string *warn, std::string *err,
                      const USDLoadOptions &options) {
  std::string filename = io::WcharToUTF8(_filename);
  return LoadUSDZFromFile(filename, scene, warn, err, options);
}
#endif

bool LoadUSDAFromMemory(const uint8_t *addr, const size_t length, const std::string &base_dir, Scene *scene,
                        std::string *warn, std::string *err,
                        const USDLoadOptions &options) {
  (void)warn;

  if (addr == nullptr) {
    if (err) {
      (*err) = "null pointer for `addr` argument.\n";
    }
    return false;
  }

  if (scene == nullptr) {
    if (err) {
      (*err) = "null pointer for `scene` argument.\n";
    }
    return false;
  }

  tinyusdz::StreamReader sr(addr, length, /* swap endian */ false);
  tinyusdz::usda::USDAParser parser(&sr);

  parser.SetBaseDir(base_dir);

  (void)options;

  {
    bool ret = parser.Parse();

    if (!ret) {
      if (err) {
        (*err) += "Failed to parse USDA\n";
        (*err) += parser.GetError();
      }

      return false;
    }
  }
  // TODO: Reconstruct Scene
    if (err) {
      (*err) += "USDA parsing success, but reconstructing Scene is TODO.\n";
    }
  return false;
}

bool LoadUSDAFromFile(const std::string &_filename, Scene *scene,
                      std::string *warn, std::string *err,
                      const USDLoadOptions &options) {

  std::string filepath = io::ExpandFilePath(_filename, /* userdata */nullptr);
  std::string base_dir = io::GetBaseDir(_filename);

  std::vector<uint8_t> data;
  size_t max_bytes = size_t(1024 * 1024 * options.max_memory_limit_in_mb);
  if (!io::ReadWholeFile(&data, err, filepath, max_bytes, /* userdata */nullptr)) {
    return false;
  }


  return LoadUSDAFromMemory(data.data(), data.size(), base_dir, scene, warn, err,
                            options);
}

size_t GeomMesh::GetNumPoints() const {
  size_t n = points.size() / 3;

  return n;
}

bool GeomMesh::GetFacevaryingNormals(std::vector<float> *v) const {
  (void)v;

  if (normals.variability != Variability::Varying) {
    return false;
  }

  //if (auto p = primvar::as_vector<Vec3f>(&normals.var)) {
  //  v->resize(p->size() * 3);
  //  memcpy(v->data(), p->data(), v->size() * sizeof(float));

  //  return true;
  //}

  return false;

}

bool GeomMesh::GetFacevaryingTexcoords(std::vector<float> *v) const {
  (void)v;
  if (st.variability != Variability::Varying) {
    return false;
  }

  // TODO
#if 0
  if (auto p = nonstd::get_if<std::vector<Vec3f>>(&st.buffer)) {
    v->resize(p->size() * 3);
    memcpy(v->data(), p->data(), v->size() * sizeof(float));

    return true;
  }

  if (auto p = nonstd::get_if<std::vector<Vec2f>>(&st.buffer)) {
    v->resize(p->size() * 2);
    memcpy(v->data(), p->data(), v->size() * sizeof(float));

    return true;
  }
#endif

  return false;
}

value::matrix4d GetTransform(XformOp xform)
{
  value::matrix4d m;
  Identity(&m);

  if (xform.op == XformOp::OpType::TRANSFORM) {
    m = nonstd::get<value::matrix4d>(xform.value);
  } else if (xform.op == XformOp::OpType::TRANSLATE) {
    if (xform.precision == XformOp::PRECISION_FLOAT) {
      auto s = nonstd::get<value::float3>(xform.value);
      m.m[0][0] = double(s[0]);
      m.m[1][1] = double(s[1]);
      m.m[2][2] = double(s[2]);
    } else {
      auto s = nonstd::get<value::double3>(xform.value);
      m.m[0][0] = s[0];
      m.m[1][1] = s[1];
      m.m[2][2] = s[2];
    }
  } else if (xform.op == XformOp::OpType::SCALE) {
    if (xform.precision == XformOp::PRECISION_FLOAT) {
      auto s = nonstd::get<value::float3>(xform.value);
      m.m[0][0] = double(s[0]);
      m.m[1][1] = double(s[1]);
      m.m[2][2] = double(s[2]);
    } else {
      auto s = nonstd::get<value::double3>(xform.value);
      m.m[0][0] = s[0];
      m.m[1][1] = s[1];
      m.m[2][2] = s[2];
    }
  }
  // TODO: rotation, orient

  return m;
}

bool Xform::EvaluateXformOps(value::matrix4d *out_matrix) const {
    Identity(out_matrix);

    value::matrix4d cm;

    // Concat matrices
    for (const auto &x : xformOps) {
      value::matrix4d m;
      Identity(&m);
      if (x.op == XformOp::TRANSLATE) {
        if (x.precision == XformOp::PRECISION_FLOAT) {
          value::float3 tx = nonstd::get<value::float3>(x.value);
          m.m[3][0] = double(tx[0]);
          m.m[3][1] = double(tx[1]);
          m.m[3][2] = double(tx[2]);
        } else if (x.precision == XformOp::PRECISION_DOUBLE) {
          auto tx = nonstd::get<value::double3>(x.value);
          m.m[3][0] = tx[0];
          m.m[3][1] = tx[1];
          m.m[3][2] = tx[2];
        } else {
          return false;
        }
      // FIXME: Validate ROTATE_X, _Y, _Z implementation
      } else if (x.op == XformOp::ROTATE_X) {
        double theta;
        if (x.precision == XformOp::PRECISION_FLOAT) {
          theta = double(nonstd::get<float>(x.value));
        } else if (x.precision == XformOp::PRECISION_DOUBLE) {
          theta = nonstd::get<double>(x.value);
        } else {
          return false;
        }

        m.m[1][1] = std::cos(theta);
        m.m[1][2] = std::sin(theta);
        m.m[2][1] = -std::sin(theta);
        m.m[2][2] = std::cos(theta);
      } else if (x.op == XformOp::ROTATE_Y) {
        double theta;
        if (x.precision == XformOp::PRECISION_FLOAT) {
          theta = double(nonstd::get<float>(x.value));
        } else if (x.precision == XformOp::PRECISION_DOUBLE) {
          theta = nonstd::get<double>(x.value);
        } else {
          return false;
        }

        m.m[0][0] = std::cos(theta);
        m.m[0][2] = -std::sin(theta);
        m.m[2][0] = std::sin(theta);
        m.m[2][2] = std::cos(theta);
      } else if (x.op == XformOp::ROTATE_Z) {
        double theta;
        if (x.precision == XformOp::PRECISION_FLOAT) {
          theta = double(nonstd::get<float>(x.value));
        } else if (x.precision == XformOp::PRECISION_DOUBLE) {
          theta = nonstd::get<double>(x.value);
        } else {
          return false;
        }

        m.m[0][0] = std::cos(theta);
        m.m[0][1] = std::sin(theta);
        m.m[1][0] = -std::sin(theta);
        m.m[1][1] = std::cos(theta);
      } else {
        // TODO
        return false;
      }

      cm = Mult<value::matrix4d, double, 4>(cm, m);
    }

    (*out_matrix) = cm;

  return true;
}

void GeomMesh::Initialize(const GPrim &gprim)
{
  name = gprim.name;
  parent_id = gprim.parent_id;

  for (auto &prop_item : gprim.props) {
    std::string attr_name = std::get<0>(prop_item);
    const Property &prop = std::get<1>(prop_item);
    if (prop.is_rel) {
      //LOG_INFO("TODO: Rel property:" + attr_name);
      continue;
    }

    const PrimAttrib &attr = prop.attrib;

    if (attr_name == "points") {
      //if (auto p = primvar::as_vector<value::float3>(&attr.var)) {
      //  points = *p;
      //}
    } else if (attr_name == "faceVertexIndices") {
      //if (auto p = primvar::as_vector<int>(&attr.var)) {
      //  faceVertexIndices = *p;
      //}
    } else if (attr_name == "faceVertexCounts") {
      //if (auto p = primvar::as_vector<int>(&attr.var)) {
      //  faceVertexCounts = *p;
      //}
    } else if (attr_name == "normals") {
      //if (auto p = primvar::as_vector<value::float3>(&attr.var)) {
      //  normals.var = *p;
      //  normals.interpolation = attr.interpolation;
      //}
    } else if (attr_name == "velocitiess") {
      //if (auto p = primvar::as_vector<value::float3>(&attr.var)) {
      //  velocitiess.var = (*p);
      //  velocitiess.interpolation = attr.interpolation;
      //}
    } else if (attr_name == "primvars:uv") {
      //if (auto pv2f = primvar::as_vector<Vec2f>(&attr.var)) {
      //  st.buffer = (*pv2f);
      //  st.interpolation = attr.interpolation;
      //} else if (auto pv3f = primvar::as_vector<value::float3>(&attr.var)) {
      //  st.buffer = (*pv3f);
      //  st.interpolation = attr.interpolation;
      //}
    } else {
      // Generic PrimAtrr
      attribs[attr_name] = attr;
    }

  }

  doubleSided = gprim.doubleSided;
  orientation = gprim.orientation;
  visibility = gprim.visibility;
  extent = gprim.extent;
  purpose = gprim.purpose;

  displayColor = gprim.displayColor;
  displayOpacity = gprim.displayOpacity;

#if 0 // TODO


  // PrimVar(TODO: Remove)
  UVCoords st;

  //
  // Properties
  //


#endif

};

nonstd::expected<bool, std::string> GeomMesh::ValidateGeomSubset() {
  if (geom_subset_children.empty()) {
    return true;
  }

  // TODO
  return nonstd::make_unexpected("TODO: Implement GeomMesh::ValidateGeomSubset()");

}

static_assert(sizeof(crate::Index) == 4, "");
static_assert(sizeof(crate::Field) == 16, "");
static_assert(sizeof(crate::Spec) == 12, "");
//static_assert(sizeof(Vec4h) == 8, "");
//static_assert(sizeof(Vec2f) == 8, "");
//static_assert(sizeof(Vec3f) == 12, "");
//static_assert(sizeof(Vec4f) == 16, "");
//static_assert(sizeof(Vec2d) == 16, "");
//static_assert(sizeof(Vec3d) == 24, "");
//static_assert(sizeof(Vec4d) == 32, "");
//static_assert(sizeof(value::matrix4d) == (8 * 16), "");

}  // namespace tinyusdz
