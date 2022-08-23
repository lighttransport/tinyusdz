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

#ifndef TINYUSDZ_HH_
#define TINYUSDZ_HH_

#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <cmath>

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
#include <iostream>  // dbg
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// TODO: Use std:: version for C++17
#include "nonstd/optional.hpp"
#include "nonstd/expected.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "prim-types.hh"
#include "image-types.hh"
#include "texture-types.hh"
#include "usdGeom.hh"

namespace tinyusdz {

constexpr int version_major = 0;
constexpr int version_minor = 8;
constexpr int version_micro = 0;

#if 0
//
// Data structure for rendering pipeline.
//
// Similar to OpenGL BufferData
//
//
struct BufferData {
  enum DataType {
    BUFFER_DATA_TYPE_INVALID,
    BUFFER_DATA_TYPE_UNSIGNED_BYTE,
    BUFFER_DATA_TYPE_UNSIGNED_SHORT,
    BUFFER_DATA_TYPE_UNSIGNED_INT,
    BUFFER_DATA_TYPE_UNSIGNED_INT64,
    BUFFER_DATA_TYPE_BYTE,
    BUFFER_DATA_TYPE_SHORT,
    BUFFER_DATA_TYPE_INT,
    BUFFER_DATA_TYPE_INT64,
    BUFFER_DATA_TYPE_HALF,
    BUFFER_DATA_TYPE_FLOAT,
    BUFFER_DATA_TYPE_DOUBLE
  };

  std::vector<uint8_t> data;  // Opaque byte data.
  size_t stride{
      0};  // byte stride for each element. e.g. 12 for XYZXYZXYZ... data. 0 =
           // app should calculate byte stride from type and `num_coords`.
  int32_t num_coords{-1};  // The number of coordinates. e.g. 3 for XYZ, RGB
                           // data, 4 for RGBA. -1 = invalid
  DataType data_type{BUFFER_DATA_TYPE_INVALID};

  void Set(DataType ty, int32_t c, size_t _stride,
           const std::vector<uint8_t> &_data) {
    data_type = ty;
    num_coords = c;
    stride = _stride;
    data = _data;
  }

  bool Valid() {
    if (data_type == BUFFER_DATA_TYPE_INVALID) {
      return false;
    }

    return (data.size() > 0) && (num_coords > 0);
  }

  size_t GetDataTypeByteSize(DataType ty) const {
    switch (ty) {
      case BUFFER_DATA_TYPE_INVALID:
        return 0;
      case BUFFER_DATA_TYPE_BYTE:
        return 1;
      case BUFFER_DATA_TYPE_UNSIGNED_BYTE:
        return 1;
      case BUFFER_DATA_TYPE_SHORT:
        return 2;
      case BUFFER_DATA_TYPE_UNSIGNED_SHORT:
        return 2;
      case BUFFER_DATA_TYPE_INT:
        return 4;
      case BUFFER_DATA_TYPE_UNSIGNED_INT:
        return 4;
      case BUFFER_DATA_TYPE_INT64:
        return 8;
      case BUFFER_DATA_TYPE_UNSIGNED_INT64:
        return 8;
      case BUFFER_DATA_TYPE_HALF:
        return 2;
      case BUFFER_DATA_TYPE_FLOAT:
        return 4;
      case BUFFER_DATA_TYPE_DOUBLE:
        return 8;
    }
    return 0;  // Should not reach here.
  }

  size_t GetElementByteSize() const {
    if (num_coords <= 0) {
      // TODO(syoyo): Report error
      return 0;
    }

    return GetDataTypeByteSize(data_type) * size_t(num_coords);
  }

  size_t GetNumElements() const {
    if (num_coords <= 0) {
      // TODO(syoyo): Report error
      return 0;
    }

    size_t n = data.size() / GetElementByteSize();

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "num_coords = " << num_coords << "\n";
    std::cout << "ccc: num_elements = " << n << "\n";
#endif
    return n;
  }

  int32_t GetNumCoords() const { return num_coords; }

  DataType GetDataType() const { return data_type; }

  size_t GetStride() const { return stride; }

  //
  // Utility functions
  //

  void Set(const std::vector<bool> &v) {
    // Not supported
    (void)v;
  }

  void Set(const std::vector<float> &v) {
    data.resize(v.size() * sizeof(float));
    memcpy(data.data(), v.data(), sizeof(float) * v.size());

    data_type = BUFFER_DATA_TYPE_FLOAT;
    num_coords = 1;
    stride = sizeof(float);
  }

  void Set(const std::vector<double> &v) {
    data.resize(v.size() * sizeof(double));
    memcpy(data.data(), v.data(), sizeof(double) * v.size());

    data_type = BUFFER_DATA_TYPE_DOUBLE;
    num_coords = 1;
    stride = sizeof(double);
  }

  void Set(const std::vector<value::float2> &v) {
    data.resize(v.size() * sizeof(value::float2));
    memcpy(data.data(), v.data(), sizeof(value::float2) * v.size());

    data_type = BUFFER_DATA_TYPE_FLOAT;
    num_coords = 2;
    stride = sizeof(value::float2);
  }

  void Set(const std::vector<value::float3> &v) {
    data.resize(v.size() * sizeof(value::float3));
    memcpy(data.data(), v.data(), sizeof(value::float3) * v.size());

    data_type = BUFFER_DATA_TYPE_FLOAT;
    num_coords = 3;
    stride = sizeof(value::float3);
  }


  void Set(const std::vector<int> &v) {
    data.resize(v.size() * sizeof(int));
    memcpy(data.data(), v.data(), sizeof(int) * v.size());

    data_type = BUFFER_DATA_TYPE_INT;
    num_coords = 1;
    stride = sizeof(value::float3);
  }

  nonstd::optional<float> GetAsFloat() const {
    if (((GetStride() == 0) || (GetStride() == sizeof(float))) &&
        (GetNumCoords() == 1) && (GetDataType() == BUFFER_DATA_TYPE_FLOAT) &&
        (GetNumElements() == 1)) {
      return *(reinterpret_cast<const float *>(data.data()));
    }

    return nonstd::nullopt;
  }

  nonstd::optional<std::array<float, 3>> GetAsColor3f() const {

    if (((GetStride() == 0) || (GetStride() == 3 * sizeof(float))) &&
        (GetNumCoords() == 3) && (GetDataType() == BUFFER_DATA_TYPE_FLOAT)) {
      std::array<float, 3> buf{{0.0f, 0.0f, 0.0f}};
      memcpy(buf.data(), data.data(), buf.size() * sizeof(float));

      return buf;
    }

    return nonstd::nullopt;
  }

  // Return empty array when required type mismatches.
  //
  nonstd::optional<std::vector<uint32_t>> GetAsUInt32Array() const {

    if (((GetStride() == 0) || (GetStride() == sizeof(uint32_t))) &&
        (GetDataType() == BUFFER_DATA_TYPE_UNSIGNED_INT)) {
      std::vector<uint32_t> buf;
      buf.resize(GetNumElements() * size_t(GetNumCoords()));
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "buf.size = " << buf.size() << "\n";
#endif
      memcpy(buf.data(), data.data(), buf.size() * sizeof(uint32_t));
      return std::move(buf);
    }

    return nonstd::nullopt;
  }

  nonstd::optional<std::vector<int32_t>> GetAsInt32Array() const {

    if (((GetStride() == 0) || (GetStride() == sizeof(int32_t))) &&
        (GetDataType() == BUFFER_DATA_TYPE_INT)) {
      std::vector<int32_t> buf;
      buf.resize(GetNumElements() * size_t(GetNumCoords()));
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "buf.size = " << buf.size() << "\n";
#endif
      memcpy(buf.data(), data.data(), buf.size() * sizeof(int32_t));

      return std::move(buf);
    }

    return nonstd::nullopt;
  }

  // zero-copy version
  // tuple: <pointer, # of elements>
  nonstd::optional<std::tuple<const float *, size_t>> FloatArrayView() const {
    if (((GetStride() == 0) || (GetStride() == sizeof(float))) &&
        (GetDataType() == BUFFER_DATA_TYPE_FLOAT)) {
      return nonstd::optional<std::tuple<const float *, size_t>>(std::make_tuple(
          reinterpret_cast<const float *>(data.data()), data.size() / sizeof(float)));
    }

    return nonstd::nullopt;
  }

  // this creates new buffer.
  nonstd::optional<std::vector<float>> GetAsFloatArray() const {

    if (((GetStride() == 0) || (GetStride() == sizeof(float))) &&
        (GetDataType() == BUFFER_DATA_TYPE_FLOAT)) {
      std::vector<float> buf;
      buf.resize(GetNumElements() * size_t(GetNumCoords()));
      memcpy(buf.data(), data.data(), buf.size() * sizeof(float));

      return std::move(buf);
    }

    return nonstd::nullopt;
  }

  nonstd::optional<std::vector<value::float2>> GetAsVec2fArray() const {

    if (((GetStride() == 0) || (GetStride() == 2 * sizeof(float))) &&
        (GetNumCoords() == 2) && (GetDataType() == BUFFER_DATA_TYPE_FLOAT)) {
      std::vector<value::float2> buf;
      buf.resize(GetNumElements());
      memcpy(buf.data(), data.data(), buf.size() * sizeof(value::float2));

      return std::move(buf);
    }

    return nonstd::nullopt;
  }

  nonstd::optional<std::vector<value::float3>> GetAsVec3fArray() const {

    // std::cout << "stride = " << GetStride() << ", num_coords = " <<
    // GetNumCoords() << ", dtype = " << GetDataType() << ", num_elements = " <<
    // GetNumElements() << "\n";

    if (((GetStride() == 0) || (GetStride() == 3 * sizeof(float))) &&
        (GetNumCoords() == 3) && (GetDataType() == BUFFER_DATA_TYPE_FLOAT)) {
      std::vector<value::float3> buf;
      buf.resize(GetNumElements());
      memcpy(buf.data(), data.data(), buf.size() * sizeof(value::float3));

      return std::move(buf);
    }

    return nonstd::nullopt;
  }

  nonstd::optional<std::vector<value::float4>> GetAsVec4fArray() const {

    if (((GetStride() == 0) || (GetStride() == 4 * sizeof(float))) &&
        (GetNumCoords() == 4) && (GetDataType() == BUFFER_DATA_TYPE_FLOAT)) {
      std::vector<value::float4> buf;
      buf.resize(GetNumElements());
      memcpy(buf.data(), data.data(), buf.size() * sizeof(value::float4));

      return std::move(buf);
    }

    return nonstd::nullopt;
  }
};
#endif

value::matrix4d GetTransform(XformOp xform);


// Simple bidirectional Path(string) <-> index lookup
struct StringAndIdMap {
  void add(int32_t key, const std::string &val) {
    _i_to_s[key] = val;
    _s_to_i[val] = key;
  }

  void add(const std::string &key, int32_t val) {
    _s_to_i[key] = val;
    _i_to_s[val] = key;
  }

  size_t count(int32_t i) const { return _i_to_s.count(i); }

  size_t count(const std::string &s) const { return _s_to_i.count(s); }

  std::string at(int32_t i) const { return _i_to_s.at(i); }

  int32_t at(std::string s) const { return _s_to_i.at(s); }

  std::map<int32_t, std::string> _i_to_s;  // index -> string
  std::map<std::string, int32_t> _s_to_i;  // string -> index
};


//
// For `Stage` scene graph.
// Similar to `Prim` in pxrUSD.
// This class uses tree-representation of `Prim`. Easy to use, but may not be performant.
//
class Prim {
 public:
  Path path;

  Prim(const value::Value &rhs);

  Prim(value::Value &&rhs);

  value::Value data; // GPrim, Xform, ...

  std::vector<Prim> children;  // child nodes
};

//
// For low-level scene graph representation, something like Vulkan.
// Less abstraction, and scene graph is representated by indices.
//
struct Node {
  std::string name;

  value::TypeId type_id{value::TypeId::TYPE_ID_INVALID};

  //
  // index to a `Scene::node_indices`
  //
  int64_t index{-1};

  int64_t parent{-1};          // parent node index
  std::vector<Node> children;  // child nodes
};

struct StageMetas {
  // Scene global setting
  AttribWithFallback<Axis> upAxis{Axis::Y}; // This can be changed by plugInfo.json in USD: https://graphics.pixar.com/usd/dev/api/group___usd_geom_up_axis__group.html#gaf16b05f297f696c58a086dacc1e288b5
  std::string defaultPrim;           // prim node name
  AttribWithFallback<double> metersPerUnit{1.0};        // default [m]
  AttribWithFallback<double> timeCodesPerSecond {24.0};  // default 24 fps
  std::string doc; // `documentation`

  std::map<std::string, MetaVariable> customLayerData; // customLayerData
};

class PrimRange;

// Similar to UsdStage, but much more something like a Scene(scene graph)
struct Stage {
  std::string name;       // Scene name
  int64_t default_root_node{-1};  // index to default root node

  StageMetas stage_metas;

  // Root nodes
  std::vector<Prim> root_nodes;
  
  static Stage CreateInMemory() {
    return Stage();
  }

  ///
  /// Traverse by depth-first order.
  ///
  PrimRange Traverse();

  ///
  /// Get Prim at a Path.
  ///
  /// @returns pointer to Prim(to avoid a copy). Assume no scene item removal/addition until the end of use of the pointer of `Prim` data.
  ///
  nonstd::expected<const Prim *, std::string> GetPrimAtPath(const Path &path);

  ///
  /// Dump Stage as ASCII(USDA) representation.
  ///
  std::string ExportToString() const;


 private:
  mutable std::string _err;
  mutable std::string _warn;

};

struct NodeIndex {
  std::string name;

  // TypeTrait<T>::type_id
  value::TypeId type_id{value::TypeId::TYPE_ID_INVALID};

  int64_t
      index{-1};  // array index to `Scene::xforms`, `Scene::geom_cameras`, ...
                  // -1 = invlid(or not set)
};


struct USDLoadOptions {
  ///
  /// Set the number of threads to use when parsing USD scene.
  /// -1 = use # of system threads(CPU cores/threads).
  ///
  int num_threads{-1};

  // Set the maximum memory limit advisorily(including image data).
  // This feature would be helpful if you want to load USDZ model in mobile
  // device.
  int32_t max_memory_limit_in_mb{10000};  // in [mb] Default 10GB

  ///
  /// Loads asset data(e.g. texture image, audio). Default is true.
  /// If you want to load asset data in your own way or don't need asset data to
  /// be loaded, Set this false.
  ///
  bool load_assets{true};

  Axis upAxis{Axis::Y};
};

#if 0  // TODO
//struct USDWriteOptions
//{
//
//
//};
#endif

//

///
/// Load USDZ(zip) from a file.
///
/// @param[in] filename USDZ filename(UTF-8)
/// @param[out] stage USD stage(scene graph).
/// @param[out] warn Warning message.
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Load options(optional)
///
/// @return true upon success
///
bool LoadUSDZFromFile(const std::string &filename, Stage *stage,
                      std::string *warn, std::string *err,
                      const USDLoadOptions &options = USDLoadOptions());


#ifdef _WIN32
// WideChar version
bool LoadUSDZFromFile(const std::wstring &filename, Stage *stage,
                      std::string *warn, std::string *err,
                      const USDLoadOptions &options = USDLoadOptions());
#endif

///
/// Load USDC(binary) from a file.
///
/// @param[in] filename USDC filename(UTF-8)
/// @param[out] stage USD stage(scene graph).
/// @param[out] warn Warning message.
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Load options(optional)
///
/// @return true upon success
///
bool LoadUSDCFromFile(const std::string &filename, Stage *stage,
                      std::string *warn, std::string *err,
                      const USDLoadOptions &options = USDLoadOptions());

///
/// Load USDC(binary) from a memory.
///
/// @param[in] addr Memory address of USDC data
/// @param[in] length Byte length of USDC data
/// @param[out] stage USD stage.
/// @param[out] warn Warning message.
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Load options(optional)
///
/// @return true upon success
///
bool LoadUSDCFromMemory(const uint8_t *addr, const size_t length, Stage *stage,
                        std::string *warn, std::string *err,
                        const USDLoadOptions &options = USDLoadOptions());

///
/// Load USDA(ascii) from a file.
///
/// @param[in] filename USDA filename(UTF-8)
/// @param[out] stage USD stage.
/// @param[out] warn Warning message.
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Load options(optional)
///
/// @return true upon success
///
bool LoadUSDAFromFile(const std::string &filename, Stage *stage,
                      std::string *warn, std::string *err,
                      const USDLoadOptions &options = USDLoadOptions());

///
/// Load USDA(ascii) from a memory.
///
/// @param[in] addr Memory address of USDA data
/// @param[in] length Byte length of USDA data
/// @param[in[ base_dir Base directory(can be empty)
/// @param[out] stage USD stage.
/// @param[out] warn Warning message.
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Load options(optional)
///
/// @return true upon success
///
bool LoadUSDAFromMemory(const uint8_t *addr, const size_t length, const std::string &base_dir, Stage *stage,
                        std::string *warn, std::string *err,
                        const USDLoadOptions &options = USDLoadOptions());

#if 0  // TODO
///
/// Write stage as USDC to a file.
///
/// @param[in] filename USDC filename
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Write options(optional)
///
/// @return true upon success
///
bool WriteAsUSDCToFile(const std::string &filename, std::string *err, const USDCWriteOptions &options = USDCWriteOptions());

#endif

}  // namespace tinyusdz

#endif  // TINYUSDZ_HH_
