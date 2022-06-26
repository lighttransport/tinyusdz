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


// local debug flag(now defined in tinyusdz.hh)
#define TINYUSDZ_LOCAL_DEBUG_PRINT

#include "integerCoding.h"
#include "lz4-compression.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"
#include "io-util.hh"
#include "pprinter.hh"
#include "usda-parser.hh"

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

#define PUSH_ERROR(s) { \
  std::ostringstream ss; \
  ss << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
  ss << s; \
  _err += ss.str() + "\n"; \
} while (0)

#define PUSH_WARN(s) { \
  std::ostringstream ss; \
  ss << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
  ss << s; \
  _warn += ss.str() + "\n"; \
} while (0)

#if defined(TINYUSDZ_LOCAL_DEBUG_PRINT)
#define DCOUT(x) do { std::cout << __FILE__ << ":" << __func__ << ":" << std::to_string(__LINE__) << " " << x << "\n"; } while (false)
#else
#define DCOUT(x) do { (void)(x); } while(false)
#endif

namespace tinyusdz {

namespace {

///
/// Represent value(TODO: Use PrimVar).
/// array is up to 1D array.
///
class Value {
 public:
  typedef std::map<std::string, Value> Dictionary;

  Value() = default;

  Value(const ValueType &_dtype, const std::vector<uint8_t> &_data)
      : dtype(_dtype), data(_data), array_length(-1) {}
  Value(const ValueType &_dtype, const std::vector<uint8_t> &_data,
        uint64_t _array_length)
      : dtype(_dtype), data(_data), array_length(int64_t(_array_length)) {}

  bool IsArray() const {
    if ((array_length > 0) || string_array.size() ||
        (dtype.id == VALUE_TYPE_PATH_LIST_OP)) {
      return true;
    }
    return false;
  }

  // Setter for primitive types.
  void SetBool(const bool d) {
    dtype.name = "Bool";
    dtype.id = VALUE_TYPE_BOOL;

    uint8_t value = d ? 1 : 0;
    data.resize(1);
    data[0] = value;
  }

  void SetUChar(const unsigned char d) {
    dtype.name = "UChar";
    dtype.id = VALUE_TYPE_UCHAR;

    data.resize(1);
    data[0] = d;
  }

  void SetInt(const int32_t i) {
    static_assert(sizeof(int32_t) == 4, "");
    dtype.name = "Int";
    dtype.id = VALUE_TYPE_INT;
    data.resize(sizeof(int32_t));
    memcpy(data.data(), reinterpret_cast<const void *>(&i), sizeof(int32_t));
  }

  void SetUInt(const uint32_t i) {
    static_assert(sizeof(uint32_t) == 4, "");
    dtype.name = "UInt";
    dtype.id = VALUE_TYPE_UINT;
    data.resize(sizeof(uint32_t));
    memcpy(data.data(), reinterpret_cast<const void *>(&i), sizeof(uint32_t));
  }

  void SetInt64(const int64_t i) {
    static_assert(sizeof(int64_t) == 8, "");
    dtype.name = "Int64";
    dtype.id = VALUE_TYPE_INT64;
    data.resize(sizeof(int64_t));
    memcpy(data.data(), reinterpret_cast<const void *>(&i), sizeof(int64_t));
  }

  void SetUInt64(const uint64_t i) {
    static_assert(sizeof(uint64_t) == 8, "");
    dtype.name = "UInt64";
    dtype.id = VALUE_TYPE_UINT64;
    data.resize(sizeof(uint64_t));
    memcpy(data.data(), reinterpret_cast<const void *>(&i), sizeof(uint64_t));
  }

  void SetDouble(const double d) {
    static_assert(sizeof(double) == 8, "");
    dtype.name = "Double";
    dtype.id = VALUE_TYPE_DOUBLE;
    data.resize(sizeof(double));
    memcpy(data.data(), reinterpret_cast<const void *>(&d), sizeof(double));
  }

  void SetFloat(const float d) {
    static_assert(sizeof(float) == 4, "");
    dtype.name = "Float";
    dtype.id = VALUE_TYPE_FLOAT;
    data.resize(sizeof(float));
    memcpy(data.data(), reinterpret_cast<const void *>(&d), sizeof(float));
  }

  void SetHalf(const float16 d) {
    dtype.name = "Half";
    dtype.id = VALUE_TYPE_HALF;
    data.resize(sizeof(uint16_t));
    memcpy(data.data(), reinterpret_cast<const void *>(&d), sizeof(uint16_t));
  }

  void SetVec2i(const Vec2i v) {
    static_assert(sizeof(Vec2i) == 8, "");
    dtype.name = "Vec2i";
    dtype.id = VALUE_TYPE_VEC2I;
    data.resize(sizeof(Vec2i));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec2i));
  }

  void SetVec2f(const Vec2f v) {
    static_assert(sizeof(Vec2f) == 8, "");
    dtype.name = "Vec2f";
    dtype.id = VALUE_TYPE_VEC2F;
    data.resize(sizeof(Vec2f));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec2f));
  }

  void SetVec2d(const Vec2d v) {
    static_assert(sizeof(Vec2d) == 16, "");
    dtype.name = "Vec2d";
    dtype.id = VALUE_TYPE_VEC2D;
    data.resize(sizeof(Vec2d));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec2d));
  }

  void SetVec2h(const Vec2h v) {
    static_assert(sizeof(Vec2h) == 4, "");
    dtype.name = "Vec2h";
    dtype.id = VALUE_TYPE_VEC2H;
    data.resize(sizeof(Vec2h));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec2h));
  }

  void SetVec3i(const Vec3i v) {
    static_assert(sizeof(Vec3i) == 12, "");
    dtype.name = "Vec3i";
    dtype.id = VALUE_TYPE_VEC3I;
    data.resize(sizeof(Vec3i));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec3i));
  }

  void SetVec3f(const Vec3f v) {
    static_assert(sizeof(Vec3f) == 12, "");
    dtype.name = "Vec3f";
    dtype.id = VALUE_TYPE_VEC3F;
    data.resize(sizeof(Vec3f));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec3f));
  }

  void SetVec3d(const Vec3d v) {
    static_assert(sizeof(Vec3d) == 24, "");
    dtype.name = "Vec3d";
    dtype.id = VALUE_TYPE_VEC3D;
    data.resize(sizeof(Vec3d));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec3d));
  }

  void SetVec3h(const Vec3h v) {
    static_assert(sizeof(Vec3h) == 6, "");
    dtype.name = "Vec3h";
    dtype.id = VALUE_TYPE_VEC3H;
    data.resize(sizeof(Vec3h));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec3h));
  }

  void SetVec4i(const Vec4i v) {
    static_assert(sizeof(Vec4i) == 16, "");
    dtype.name = "Vec4i";
    dtype.id = VALUE_TYPE_VEC4I;
    data.resize(sizeof(Vec4i));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec4i));
  }

  void SetVec4f(const Vec4f v) {
    static_assert(sizeof(Vec4f) == 16, "");
    dtype.name = "Vec4f";
    dtype.id = VALUE_TYPE_VEC4F;
    data.resize(sizeof(Vec4f));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec4f));
  }

  void SetVec4d(const Vec4d v) {
    static_assert(sizeof(Vec4d) == 32, "");
    dtype.name = "Vec4d";
    dtype.id = VALUE_TYPE_VEC4D;
    data.resize(sizeof(Vec4d));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec4d));
  }

  void SetVec4h(const Vec4h v) {
    static_assert(sizeof(Vec4h) == 8, "");
    dtype.name = "Vec4h";
    dtype.id = VALUE_TYPE_VEC4H;
    data.resize(sizeof(Vec4h));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec3h));
  }

  void SetQuath(const Quath v) {
    static_assert(sizeof(Quath) == (2 * 4), "");
    dtype.name = "Quath";
    dtype.id = VALUE_TYPE_QUATH;
    data.resize(sizeof(Quath));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Quath));
  }

  void SetQuatf(const Quatf v) {
    static_assert(sizeof(Quatf) == (4 * 4), "");
    dtype.name = "Quatf";
    dtype.id = VALUE_TYPE_QUATF;
    data.resize(sizeof(Quatf));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Quatf));
  }

  void SetQuatd(const Quatd v) {
    static_assert(sizeof(Quatd) == (8 * 4), "");
    dtype.name = "Quatd";
    dtype.id = VALUE_TYPE_QUATD;
    data.resize(sizeof(Quatd));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Quatd));
  }

  void SetMatrix2d(const Matrix2d v) {
    static_assert(sizeof(Matrix2d) == (2 * 2 * 8), "");
    dtype.name = "Matrix2d";
    dtype.id = VALUE_TYPE_MATRIX2D;
    data.resize(sizeof(Matrix2d));
    memcpy(data.data(), reinterpret_cast<const void *>(v.m), sizeof(Matrix2d));
  }

  void SetMatrix3d(const Matrix3d v) {
    static_assert(sizeof(Matrix3d) == (3 * 3 * 8), "");
    dtype.name = "Matrix3d";
    dtype.id = VALUE_TYPE_MATRIX3D;
    data.resize(sizeof(Matrix3d));
    memcpy(data.data(), reinterpret_cast<const void *>(v.m), sizeof(Matrix3d));
  }

  void SetMatrix4d(const Matrix4d v) {
    static_assert(sizeof(Matrix4d) == (4 * 4 * 8), "");
    dtype.name = "Matrix4d";
    dtype.id = VALUE_TYPE_MATRIX4D;
    data.resize(sizeof(Matrix4d));
    memcpy(data.data(), reinterpret_cast<const void *>(v.m), sizeof(Matrix4d));
  }

  void SetMatrix4dArray(const Matrix4d *d, const size_t n) {

    dtype.name = "Matrix4dArray";
    dtype.id = VALUE_TYPE_MATRIX4D;
    array_length = int64_t(n);
    data.resize(n * sizeof(Matrix4d));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(Matrix4d));
  }

  void SetToken(const std::string &s) {
    dtype.name = "Token";
    dtype.id = VALUE_TYPE_TOKEN;
    data.resize(s.size());  // No '\0'
    memcpy(data.data(), reinterpret_cast<const void *>(&s[0]), s.size());
  }

  void SetString(const std::string &s) {
    dtype.name = "String";
    dtype.id =
        VALUE_TYPE_STRING;  // we treat String as std::string, not StringIndex
    data.resize(s.size());  // No '\0'
    memcpy(data.data(), reinterpret_cast<const void *>(&s[0]), s.size());
  }

  void SetAssetPath(const std::string &s) {
    dtype.name = "AssetPath";
    dtype.id = VALUE_TYPE_ASSET_PATH;  // we treat AssetPath as std::string, not
                                       // TokenIndex
    data.resize(s.size());             // No '\0'
    memcpy(data.data(), reinterpret_cast<const void *>(&s[0]), s.size());
  }

  void SetPermission(const uint32_t d) {
    // TODO(syoyo): range check
    dtype.name = "Permission";
    dtype.id = VALUE_TYPE_PERMISSION;
    data.resize(sizeof(uint32_t));
    memcpy(data.data(), reinterpret_cast<const void *>(&d), sizeof(uint32_t));
  }

  void SetSpecifier(const uint32_t d) {
    // TODO(syoyo): range check
    dtype.name = "Specifier";
    dtype.id = VALUE_TYPE_SPECIFIER;
    data.resize(sizeof(uint32_t));
    memcpy(data.data(), reinterpret_cast<const void *>(&d), sizeof(uint32_t));
  }

  void SetVariability(const uint32_t d) {
    // TODO(syoyo): range check
    dtype.name = "Variability";
    dtype.id = VALUE_TYPE_VARIABILITY;
    data.resize(sizeof(uint32_t));
    memcpy(data.data(), reinterpret_cast<const void *>(&d), sizeof(uint32_t));
  }

  void SetIntArray(const int *d, const size_t n) {
    dtype.name = "IntArray";
    dtype.id = VALUE_TYPE_INT;
    array_length = int64_t(n);
    data.resize(n * sizeof(uint32_t));
    memcpy(data.data(), reinterpret_cast<const void *>(d),
           n * sizeof(uint32_t));
  }

  void SetHalfArray(const uint16_t *d, const size_t n) {
    dtype.name = "HalfArray";
    dtype.id = VALUE_TYPE_HALF;
    array_length = int64_t(n);
    data.resize(n * sizeof(uint16_t));
    memcpy(data.data(), reinterpret_cast<const void *>(d),
           n * sizeof(uint16_t));
  }

  void SetFloatArray(const float *d, const size_t n) {
    dtype.name = "FloatArray";
    dtype.id = VALUE_TYPE_FLOAT;
    array_length = int64_t(n);
    data.resize(n * sizeof(float));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(float));
  }

  void SetDoubleArray(const double *d, const size_t n) {
    dtype.name = "DoubleArray";
    dtype.id = VALUE_TYPE_DOUBLE;
    array_length = int64_t(n);
    data.resize(n * sizeof(double));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(double));
  }

  void SetVec2fArray(const Vec2f *d, const size_t n) {
    static_assert(sizeof(Vec2f) == 8, "");
    dtype.name = "Vec2fArray";
    dtype.id = VALUE_TYPE_VEC2F;
    array_length = int64_t(n);
    data.resize(n * sizeof(Vec2f));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(Vec2f));
  }

  void SetVec3fArray(const Vec3f *d, const size_t n) {
    static_assert(sizeof(Vec3f) == 12, "");
    dtype.name = "Vec3fArray";
    dtype.id = VALUE_TYPE_VEC3F;
    array_length = int64_t(n);
    data.resize(n * sizeof(Vec3f));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(Vec3f));
  }

  void SetVec4fArray(const Vec4f *d, const size_t n) {
    static_assert(sizeof(Vec4f) == 16, "");
    dtype.name = "Vec4fArray";
    dtype.id = VALUE_TYPE_VEC4F;
    array_length = int64_t(n);
    data.resize(n * sizeof(Vec4f));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(Vec4f));
  }

  void SetVec2dArray(const Vec2d *d, const size_t n) {
    static_assert(sizeof(Vec2d) == 16, "");
    dtype.name = "Vec2dArray";
    dtype.id = VALUE_TYPE_VEC2D;
    array_length = int64_t(n);
    data.resize(n * sizeof(Vec2d));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(Vec2d));
  }

  void SetVec3dArray(const Vec3d *d, const size_t n) {
    static_assert(sizeof(Vec3d) == 24, "");
    dtype.name = "Vec3dArray";
    dtype.id = VALUE_TYPE_VEC3D;
    array_length = int64_t(n);
    data.resize(n * sizeof(Vec3d));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(Vec3d));
  }

  void SetVec4dArray(const Vec4d *d, const size_t n) {
    static_assert(sizeof(Vec4d) == 32, "");
    dtype.name = "Vec4dArray";
    dtype.id = VALUE_TYPE_VEC4D;
    array_length = int64_t(n);
    data.resize(n * sizeof(Vec4d));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(Vec4d));
  }

  void SetQuathArray(const Quath *d, const size_t n) {
    static_assert(sizeof(Quath) == 8, "");
    dtype.name = "QuathArray";
    dtype.id = VALUE_TYPE_QUATH;
    array_length = int64_t(n);
    data.resize(n * sizeof(Quath));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(Quath));
  }

  void SetQuatfArray(const Quatf *d, const size_t n) {
    static_assert(sizeof(Quatf) == 16, "");
    dtype.name = "QuatfArray";
    dtype.id = VALUE_TYPE_QUATF;
    array_length = int64_t(n);
    data.resize(n * sizeof(Quatf));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(Quatf));
  }

  void SetQuatdArray(const Quatd *d, const size_t n) {
    static_assert(sizeof(Quatd) == 32, "");
    dtype.name = "QuatdArray";
    dtype.id = VALUE_TYPE_QUATD;
    array_length = int64_t(n);
    data.resize(n * sizeof(Quatd));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(Quatd));
  }

  void SetTokenArray(const std::vector<std::string> &d) {
    dtype.name = "TokenArray";
    dtype.id = VALUE_TYPE_TOKEN_VECTOR;
    array_length = int64_t(d.size());
    string_array = d;
  }

  void SetPathVector(const std::vector<Path> &d) {
    dtype.name = "PathVector";
    dtype.id = VALUE_TYPE_PATH_VECTOR;
    array_length = int64_t(d.size());
    path_vector = d;
  }

  void SetPathListOp(const ListOp<Path> &d) {
    dtype.name = "PathListOp";
    dtype.id = VALUE_TYPE_PATH_LIST_OP;
    // FIXME(syoyo): How to determine array length?
    // array_length = int64_t(d.size());
    path_list_op = d;
  }

  void SetTokenListOp(const ListOp<std::string> &d) {
    dtype.name = "TokenListOp";
    dtype.id = VALUE_TYPE_TOKEN_LIST_OP;
    // FIXME(syoyo): How to determine array length?
    // array_length = int64_t(d.size());
    token_list_op = d;
  }

  void SetTimeSamples(const TimeSamples &d) {
    dtype.name = "TimeSamples";
    dtype.id = VALUE_TYPE_TIME_SAMPLES;
    // FIXME(syoyo): How to determine array length?
    // array_length = int64_t(d.size());
    time_samples = d;
  }

  const ListOp<Path> &GetPathListOp() const { return path_list_op; }
  const ListOp<std::string> &GetTokenListOp() const { return token_list_op; }

  // Getter for frequently used types.
  Specifier GetSpecifier() const {
    if (dtype.id == VALUE_TYPE_SPECIFIER) {
      uint32_t d = *reinterpret_cast<const uint32_t *>(data.data());
      return static_cast<Specifier>(d);
    }
    return Specifier::Invalid;  // invalid
  }

  Variability GetVariability() const {
    if (dtype.id == VALUE_TYPE_VARIABILITY) {
      uint32_t d = *reinterpret_cast<const uint32_t *>(data.data());
      return static_cast<Variability>(d);
    }
    return Variability::Invalid;  // invalid
  }

  bool GetBool(bool *ret) const {
    if (ret == nullptr) {
      return false;
    }

    if (dtype.id == VALUE_TYPE_BOOL) {
      uint8_t d = *reinterpret_cast<const uint8_t *>(data.data());
      (*ret) = bool(d);
      return true;
    }

    return false;
  }

  double GetDouble() const {
    if (dtype.id == VALUE_TYPE_DOUBLE) {
      double d = *reinterpret_cast<const double *>(data.data());
      return static_cast<double>(d);
    } else if (dtype.id == VALUE_TYPE_FLOAT) {
      float d = *reinterpret_cast<const float *>(data.data());
      return static_cast<double>(d);
    }
    return std::numeric_limits<double>::quiet_NaN();  // invalid
  }

  bool GetInt(int *ret) const {
    if (ret == nullptr) {
      return false;
    }

    if (dtype.id == VALUE_TYPE_INT) {
      int d = *reinterpret_cast<const int *>(data.data());
      (*ret) = d;
      return true;
    }
    return false;
  }

  bool GetFloat(float *ret) const {
    if (dtype.id == VALUE_TYPE_FLOAT) {
      float d = *reinterpret_cast<const float *>(data.data());
      (*ret) = d;
      return true;
    }
    return false;
  }

  std::string GetToken() const {
    if (dtype.id == VALUE_TYPE_TOKEN) {
      std::string s(reinterpret_cast<const char *>(data.data()), data.size());
      return s;
    }
    return std::string();
  }

  std::vector<std::string> GetTokenArray() const {
    if (dtype.id == VALUE_TYPE_TOKEN_VECTOR) {
      return string_array;
    }
    std::vector<std::string> empty;
    return empty;
  }

  std::string GetString() const {
    if (dtype.id == VALUE_TYPE_STRING) {
      std::string s(reinterpret_cast<const char *>(data.data()), data.size());
      return s;
    }
    return std::string();
  }

  size_t GetArrayLength() const { return size_t(array_length); }

  const std::vector<std::string> &GetStringArray() const {
    return string_array;
  }

  const std::vector<uint8_t> &GetData() const {
    // TODO(syoyo): Report error for Dictionary type.
    return data;
  }

  const std::string &GetTypeName() const { return dtype.name; }

  const ValueTypeId &GetTypeId() const { return dtype.id; }

  bool IsDictionary() const { return dtype.id == VALUE_TYPE_DICTIONARY; }

  void SetDictionary(const Dictionary &d) {
    dtype.name = "Dictionary";
    dtype.id = VALUE_TYPE_DICTIONARY;
    dict = d;
  }

  std::vector<int> GetIntArray() const {
    std::vector<int> ret;

    if ((dtype.name == "IntArray") && (array_length > 0)) {
      size_t n = size_t(array_length);
      ret.resize(n);
      memcpy(ret.data(), data.data(), n * sizeof(uint32_t));
    } else {
      // TODO: Report an error
    }

    return ret; // Empty
  }

  std::vector<float> GetFloatArray() const {
    std::vector<float> ret;

    if ((dtype.name == "FloatArray") && (array_length > 0)) {
      size_t n = size_t(array_length);
      ret.resize(n);
      memcpy(ret.data(), data.data(), n * sizeof(float));
    } else {
      // TODO: Report an error
    }

    return ret; // Empty
  }

  Dictionary &GetDictionary() { return dict; }
  const Dictionary &GetDictionary() const { return dict; }

 private:
  ValueType dtype;
  std::string string_value;
  std::vector<uint8_t> data;  // value as opaque binary data.
  int64_t array_length{-1};

  // Dictionary, ListOp and array of string has separated storage
  std::vector<std::string> string_array; // also TokenArray
  std::vector<Path> path_vector;
  Dictionary dict;
  ListOp<Path> path_list_op;
  ListOp<std::string> token_list_op;
  // TODO(syoyo): Reference

  // TODO(syoyo): Use single representation for integral types
  ListOp<int32_t> int_list_op;
  ListOp<uint32_t> int64_list_op;
  ListOp<int64_t> uint_list_op;
  ListOp<uint64_t> uint64_list_op;

  TimeSamples time_samples;
};

constexpr size_t kMinCompressedArraySize = 16;
constexpr size_t kSectionNameMaxLength = 15;

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

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
float to_float(uint16_t h) {
  float16 f;
  f = h;
  return half_to_float(f);
}
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#endif
const ValueType &GetValueType(int32_t type_id) {
  static std::map<uint32_t, ValueType> table;
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "type_id = " << type_id << "\n";
#endif
  if (table.size() == 0) {
    // Register data types
    // NOTE(syoyo): We can use C++11 template to create compile-time table for
    // data types, but this way(use std::map) is easier to read and maintain, I
    // think.

    // reference: crateDataTypes.h

#define ADD_VALUE_TYPE(NAME_STR, TYPE_ID, SUPPORTS_ARRAY)          \
  {                                                                \
    assert(table.count(TYPE_ID) == 0);                             \
    table[TYPE_ID] = ValueType(NAME_STR, TYPE_ID, SUPPORTS_ARRAY); \
  }

    ADD_VALUE_TYPE("InvaldOrUnsupported", 0, false)

    // Array types.
    ADD_VALUE_TYPE("Bool", VALUE_TYPE_BOOL, true)

    ADD_VALUE_TYPE("UChar", VALUE_TYPE_UCHAR, true)
    ADD_VALUE_TYPE("Int", VALUE_TYPE_INT, true)
    ADD_VALUE_TYPE("UInt", VALUE_TYPE_UINT, true)
    ADD_VALUE_TYPE("Int64", VALUE_TYPE_INT64, true)
    ADD_VALUE_TYPE("UInt64", VALUE_TYPE_UINT64, true)

    ADD_VALUE_TYPE("Half", VALUE_TYPE_HALF, true)
    ADD_VALUE_TYPE("Float", VALUE_TYPE_FLOAT, true)
    ADD_VALUE_TYPE("Double", VALUE_TYPE_DOUBLE, true)

    ADD_VALUE_TYPE("String", VALUE_TYPE_STRING, true)
    ADD_VALUE_TYPE("Token", VALUE_TYPE_TOKEN, true)
    ADD_VALUE_TYPE("AssetPath", VALUE_TYPE_ASSET_PATH, true)

    ADD_VALUE_TYPE("Quatd", VALUE_TYPE_QUATD, true)
    ADD_VALUE_TYPE("Quatf", VALUE_TYPE_QUATF, true)
    ADD_VALUE_TYPE("Quath", VALUE_TYPE_QUATH, true)

    ADD_VALUE_TYPE("Vec2d", VALUE_TYPE_VEC2D, true)
    ADD_VALUE_TYPE("Vec2f", VALUE_TYPE_VEC2F, true)
    ADD_VALUE_TYPE("Vec2h", VALUE_TYPE_VEC2H, true)
    ADD_VALUE_TYPE("Vec2i", VALUE_TYPE_VEC2I, true)

    ADD_VALUE_TYPE("Vec3d", VALUE_TYPE_VEC3D, true)
    ADD_VALUE_TYPE("Vec3f", VALUE_TYPE_VEC3F, true)
    ADD_VALUE_TYPE("Vec3h", VALUE_TYPE_VEC3H, true)
    ADD_VALUE_TYPE("Vec3i", VALUE_TYPE_VEC3I, true)

    ADD_VALUE_TYPE("Vec4d", VALUE_TYPE_VEC4D, true)
    ADD_VALUE_TYPE("Vec4f", VALUE_TYPE_VEC4F, true)
    ADD_VALUE_TYPE("Vec4h", VALUE_TYPE_VEC4H, true)
    ADD_VALUE_TYPE("Vec4i", VALUE_TYPE_VEC4I, true)

    ADD_VALUE_TYPE("Matrix2d", VALUE_TYPE_MATRIX2D, true)
    ADD_VALUE_TYPE("Matrix3d", VALUE_TYPE_MATRIX3D, true)
    ADD_VALUE_TYPE("Matrix4d", VALUE_TYPE_MATRIX4D, true)

    // Non-array types.
    ADD_VALUE_TYPE("Dictionary", VALUE_TYPE_DICTIONARY,
                   false)  // std::map<std::string, Value>

    ADD_VALUE_TYPE("TokenListOp", VALUE_TYPE_TOKEN_LIST_OP, false)
    ADD_VALUE_TYPE("StringListOp", VALUE_TYPE_STRING_LIST_OP, false)
    ADD_VALUE_TYPE("PathListOp", VALUE_TYPE_PATH_LIST_OP, false)
    ADD_VALUE_TYPE("ReferenceListOp", VALUE_TYPE_REFERENCE_LIST_OP, false)
    ADD_VALUE_TYPE("IntListOp", VALUE_TYPE_INT_LIST_OP, false)
    ADD_VALUE_TYPE("Int64ListOp", VALUE_TYPE_INT64_LIST_OP, false)
    ADD_VALUE_TYPE("UIntListOp", VALUE_TYPE_UINT_LIST_OP, false)
    ADD_VALUE_TYPE("UInt64ListOp", VALUE_TYPE_UINT64_LIST_OP, false)

    ADD_VALUE_TYPE("PathVector", VALUE_TYPE_PATH_VECTOR, false)
    ADD_VALUE_TYPE("TokenVector", VALUE_TYPE_TOKEN_VECTOR, false)

    ADD_VALUE_TYPE("Specifier", VALUE_TYPE_SPECIFIER, false)
    ADD_VALUE_TYPE("Permission", VALUE_TYPE_PERMISSION, false)
    ADD_VALUE_TYPE("Variability", VALUE_TYPE_VARIABILITY, false)

    ADD_VALUE_TYPE("VariantSelectionMap", VALUE_TYPE_VARIANT_SELECTION_MAP,
                   false)
    ADD_VALUE_TYPE("TimeSamples", VALUE_TYPE_TIME_SAMPLES, false)
    ADD_VALUE_TYPE("Payload", VALUE_TYPE_PAYLOAD, false)
    ADD_VALUE_TYPE("DoubleVector", VALUE_TYPE_DOUBLE_VECTOR, false)
    ADD_VALUE_TYPE("LayerOffsetVector", VALUE_TYPE_LAYER_OFFSET_VECTOR, false)
    ADD_VALUE_TYPE("StringVector", VALUE_TYPE_STRING_VECTOR, false)
    ADD_VALUE_TYPE("ValueBlock", VALUE_TYPE_VALUE_BLOCK, false)
    ADD_VALUE_TYPE("Value", VALUE_TYPE_VALUE, false)
    ADD_VALUE_TYPE("UnregisteredValue", VALUE_TYPE_UNREGISTERED_VALUE, false)
    ADD_VALUE_TYPE("UnregisteredValueListOp",
                   VALUE_TYPE_UNREGISTERED_VALUE_LIST_OP, false)
    ADD_VALUE_TYPE("PayloadListOp", VALUE_TYPE_PAYLOAD_LIST_OP, false)
    ADD_VALUE_TYPE("TimeCode", VALUE_TYPE_TIME_CODE, true)
  }
#undef ADD_VALUE_TYPE

  if (type_id < 0) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cerr << "Unknonw type id: " << type_id << "\n";
#endif
    return table.at(0);
  }

  if (!table.count(uint32_t(type_id))) {
    // Invalid or unsupported.
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cerr << "Unknonw type id: " << type_id << "\n";
#endif
    return table.at(0);
  }

  return table.at(uint32_t(type_id));
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
std::string GetValueTypeRepr(int32_t type_id) {
  ValueType dty = GetValueType(type_id);

  std::stringstream ss;
  ss << "ValueType: " << dty.name << "(" << dty.id
     << "), supports_array = " << dty.supports_array;
  return ss.str();
}
#endif


#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
std::string GetSpecifierString(Specifier ty) {
  if (Specifier::Def == ty) {
    return "SpecifierDef";
  } else if (Specifier::Over == ty) {
    return "SpecifierOver";
  } else if (Specifier::Class == ty) {
    return "SpecifierClass";
  }
  return "??? Specifier " + to_string(ty);
}

std::string GetPermissionString(Permission ty) {
  if (Permission::Public == ty) {
    return "PermissionPublic";
  } else if (Permission::Private == ty) {
    return "PermissionPrivate";
  }
  return "??? Permission " + to_string(ty);
}

std::string GetVariabilityString(Variability ty) {
  if (Variability::Varying == ty) {
    return "VariabilityVarying";
  } else if (Variability::Uniform == ty) {
    return "VariabilityUniform";
  } else if (Variability::Config == ty) {
    return "VariabilityConfig";
  }
  return "??? Variability " + to_string(ty);
}
#endif

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

  void SetAssetInfo(const primvar::dict &dict) {
    _assetInfo = dict;
  }

  const primvar::dict &GetAssetInfo() const { return _assetInfo; }

 private:
  int64_t
      _parent;  // -1 = this node is the root node. -2 = invalid or leaf node
  std::vector<size_t> _children;                  // index to child nodes.
  std::unordered_set<std::string> _primChildren;  // List of name of child nodes

  Path _path;  // local path
  primvar::dict _assetInfo;

  NodeType _node_type;
};

// -- from USD ----------------------------------------------------------------

//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.

// Index base class.  Used to index various tables.  Deriving adds some
// type-safety so we don't accidentally use one kind of index with the wrong
// kind of table.
struct Index {
  Index() : value(~0u) {}
  explicit Index(uint32_t v) : value(v) {}
  bool operator==(const Index &other) const { return value == other.value; }
  bool operator!=(const Index &other) const { return !(*this == other); }
  bool operator<(const Index &other) const { return value < other.value; }
  uint32_t value;
};

// Value in file representation.  Consists of a 2 bytes of type information
// (type enum value, array bit, and inlined-value bit) and 6 bytes of data.
// If possible, we attempt to store certain values directly in the local
// data, such as ints, floats, enums, and special-case values of other types
// (zero vectors, identity matrices, etc).  For values that aren't stored
// inline, the 6 data bytes are the offset from the start of the file to the
// value's location.
struct ValueRep {
  friend class CrateFile;

  ValueRep() = default;

  explicit constexpr ValueRep(uint64_t d) : data(d) {}

  constexpr ValueRep(int32_t t, bool isInlined, bool isArray, uint64_t payload)
      : data(Combine(t, isInlined, isArray, payload)) {}

  static const uint64_t IsArrayBit_ = 1ull << 63;
  static const uint64_t IsInlinedBit_ = 1ull << 62;
  static const uint64_t IsCompressedBit_ = 1ull << 61;

  static const uint64_t PayloadMask_ = ((1ull << 48) - 1);

  inline bool IsArray() const { return data & IsArrayBit_; }
  inline void SetIsArray() { data |= IsArrayBit_; }

  inline bool IsInlined() const { return data & IsInlinedBit_; }
  inline void SetIsInlined() { data |= IsInlinedBit_; }

  inline bool IsCompressed() const { return data & IsCompressedBit_; }
  inline void SetIsCompressed() { data |= IsCompressedBit_; }

  inline int32_t GetType() const {
    return static_cast<int32_t>((data >> 48) & 0xFF);
  }
  inline void SetType(int32_t t) {
    data &= ~(0xFFull << 48);                  // clear type byte in data.
    data |= (static_cast<uint64_t>(t) << 48);  // set it.
  }

  inline uint64_t GetPayload() const { return data & PayloadMask_; }

  inline void SetPayload(uint64_t payload) {
    data &= ~PayloadMask_;  // clear existing payload.
    data |= payload & PayloadMask_;
  }

  inline uint64_t GetData() const { return data; }

  bool operator==(ValueRep other) const { return data == other.data; }
  bool operator!=(ValueRep other) const { return !(*this == other); }

  // friend inline size_t hash_value(ValueRep v) {
  //  return static_cast<size_t>(v.data);
  //}

  std::string GetStringRepr() const {
    std::stringstream ss;
    ss << "ty: " << static_cast<int>(GetType()) << ", isArray: " << IsArray()
       << ", isInlined: " << IsInlined() << ", isCompressed: " << IsCompressed()
       << ", payload: " << GetPayload();

    return ss.str();
  }

 private:
  static constexpr uint64_t Combine(int32_t t, bool isInlined, bool isArray,
                                     uint64_t payload) {
    return (isArray ? IsArrayBit_ : 0) | (isInlined ? IsInlinedBit_ : 0) |
           (static_cast<uint64_t>(t) << 48) | (payload & PayloadMask_);
  }

  uint64_t data;
};

// ----------------------------------------------------------------------------

struct Field {
  Index token_index;
  ValueRep value_rep;
};

//
// Spec describes the relation of a path(i.e. node) and field(e.g. vertex data)
//
struct Spec {
  Index path_index;
  Index fieldset_index;
  SpecType spec_type;
};

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

template <class Int>
static inline bool ReadCompressedInts(const StreamReader *sr, Int *out,
                                       size_t size) {
  // TODO(syoyo): Error check
  using Compressor =
      typename std::conditional<sizeof(Int) == 4, Usd_IntegerCompression,
                                Usd_IntegerCompression64>::type;
  std::vector<char> compBuffer(Compressor::GetCompressedBufferSize(size));
  uint64_t compSize;
  if (!sr->read8(&compSize)) {
    return false;
  }

  if (!sr->read(size_t(compSize), size_t(compSize),
                reinterpret_cast<uint8_t *>(compBuffer.data()))) {
    return false;
  }
  std::string err;
  bool ret = Compressor::DecompressFromBuffer(
      compBuffer.data(), size_t(compSize), out, size, &err);
  (void)err;

  return ret;
}

static inline bool ReadIndices(const StreamReader *sr,
                               std::vector<Index> *indices) {
  // TODO(syoyo): Error check
  uint64_t n;
  if (!sr->read8(&n)) {
    return false;
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "ReadIndices: n = " << n << "\n";
#endif

  indices->resize(size_t(n));
  size_t datalen = size_t(n) * sizeof(Index);

  if (datalen != sr->read(datalen, datalen,
                          reinterpret_cast<uint8_t *>(indices->data()))) {
    return false;
  }

  return true;
}

class Parser {
 public:
  Parser(StreamReader *sr, int num_threads) : _sr(sr) {
    if (num_threads == -1) {
#if defined(__wasi__)
      num_threads = 1;
#else
      num_threads = std::max(1, int(std::thread::hardware_concurrency()));
#endif
    }

    // Limit to 1024 threads.
    _num_threads = std::min(1024, num_threads);
  }

  bool ReadBootStrap();
  bool ReadTOC();

  // Read known sections
  bool ReadPaths();
  bool ReadTokens();
  bool ReadStrings();
  bool ReadFields();
  bool ReadFieldSets();
  bool ReadSpecs();

  ///
  /// Read TOC section
  ///
  bool ReadSection(Section *s);

  const std::string GetToken(Index token_index) {
    if (token_index.value <= _tokens.size()) {
      return _tokens[token_index.value];
    } else {
      _err += "Token index out of range: " + std::to_string(token_index.value) +
              "\n";
      return std::string();
    }
  }

  const std::string GetToken(Index token_index) const {
    if (token_index.value <= _tokens.size()) {
      return _tokens[token_index.value];
    } else {
      return std::string();
    }
  }

  // Get string from string index.
  std::string GetString(Index string_index) {
    if (string_index.value <= _string_indices.size()) {
      Index s_idx = _string_indices[string_index.value];
      return GetToken(s_idx);
    } else {
      _err +=
          "String index out of range: " + std::to_string(string_index.value) +
          "\n";
      return std::string();
    }
  }

  bool HasField(const std::string &key) const {
    // Simple linear search
    for (const auto &field : _fields) {
      const std::string field_name = GetToken(field.token_index);
      if (field_name.compare(key) == 0) {
        return true;
      }
    }
    return false;
  }

  bool GetField(Index index, Field &&field) const {
    if (index.value <= _fields.size()) {
      field = _fields[index.value];
      return true;
    } else {
      return false;
    }
  }

  std::string GetFieldString(Index index) {
    if (index.value <= _fields.size()) {
      // ok
    } else {
      return "#INVALID field index#";
    }

    const Field &f = _fields[index.value];

    std::string s = GetToken(f.token_index) + ":" + f.value_rep.GetStringRepr();

    return s;
  }

  Path GetPath(Index index) {
    if (index.value <= _paths.size()) {
      // ok
    } else {
      PUSH_ERROR("Invalid path index?");
      return Path();
    }

    const Path &p = _paths[index.value];

    return p;
  }

  std::string GetPathString(Index index) {
    if (index.value <= _paths.size()) {
      // ok
    } else {
      PUSH_ERROR("Invalid path index");
      return "#INVALID path index#";
    }

    const Path &p = _paths[index.value];

    return p.full_path_name();
  }

  std::string GetSpecString(Index index) {
    if (index.value <= _specs.size()) {
      // ok
    } else {
      PUSH_ERROR("Invalid path index");
      return "#INVALID spec index#";
    }

    const Spec &spec = _specs[index.value];

    std::string path_str = GetPathString(spec.path_index);
    std::string specty_str = to_string(spec.spec_type);

    return "[Spec] path: " + path_str +
           ", fieldset id: " + std::to_string(spec.fieldset_index.value) +
           ", spec_type: " + specty_str;
  }

  ///
  /// Methods for reconstructing `Scene` object
  ///

  // In-memory storage for a single "spec" -- prim, property, etc.
  typedef std::pair<std::string, Value> FieldValuePair;
  typedef std::vector<FieldValuePair> FieldValuePairVector;

  // `_live_fieldsets` contains unpacked value keyed by fieldset index.
  // Used for reconstructing Scene object
  // TODO(syoyo): Use unordered_map(need hash function)
  std::map<Index, FieldValuePairVector>
      _live_fieldsets;  // <fieldset index, List of field with unpacked Values>

  bool BuildLiveFieldSets();

  ///
  /// Parse node's attribute from FieldValuePairVector.
  ///
  bool ParseAttribute(const FieldValuePairVector &fvs, PrimAttrib *attr,
                       const std::string &prop_name);

  bool ReconstructXform(const Node &node,
                         const FieldValuePairVector &fields,
                         const std::unordered_map<uint32_t, uint32_t>
                             &path_index_to_spec_index_map,
                         Xform *xform);

  bool ReconstructGeomSubset(const Node &node,
                            const FieldValuePairVector &fields,
                            const std::unordered_map<uint32_t, uint32_t>
                                &path_index_to_spec_index_map,
                            GeomSubset *mesh);

  bool ReconstructGeomMesh(const Node &node,
                            const FieldValuePairVector &fields,
                            const std::unordered_map<uint32_t, uint32_t>
                                &path_index_to_spec_index_map,
                            GeomMesh *mesh);

  bool ReconstructGeomBasisCurves(const Node &node,
                            const FieldValuePairVector &fields,
                            const std::unordered_map<uint32_t, uint32_t>
                                &path_index_to_spec_index_map,
                            GeomBasisCurves *curves);

  bool ReconstructMaterial(const Node &node,
                            const FieldValuePairVector &fields,
                            const std::unordered_map<uint32_t, uint32_t>
                                &path_index_to_spec_index_map,
                            Material *material);

  bool ReconstructShader(const Node &node, const FieldValuePairVector &fields,
                          const std::unordered_map<uint32_t, uint32_t>
                              &path_index_to_spec_index_map,
                          Shader *shader);

  bool ReconstructPreviewSurface(const Node &node, const FieldValuePairVector &fields,
                         const std::unordered_map<uint32_t, uint32_t>
                             &path_index_to_spec_index_map,
                         PreviewSurface *surface);

  bool ReconstructUVTexture(const Node &node, const FieldValuePairVector &fields,
                         const std::unordered_map<uint32_t, uint32_t>
                             &path_index_to_spec_index_map,
                         UVTexture *uvtex);

  bool ReconstructPrimvarReader_float2(const Node &node,
                            const FieldValuePairVector &fields,
                            const std::unordered_map<uint32_t, uint32_t>
                                &path_index_to_spec_index_map,
                            PrimvarReader_float2 *preader);

  bool ReconstructSkelRoot(const Node &node, const FieldValuePairVector &fields,
                          const std::unordered_map<uint32_t, uint32_t>
                              &path_index_to_spec_index_map,
                          SkelRoot *skelRoot);

  bool ReconstructSkeleton(const Node &node, const FieldValuePairVector &fields,
                          const std::unordered_map<uint32_t, uint32_t>
                              &path_index_to_spec_index_map,
                          Skeleton *skeleton);

  bool ReconstructSceneRecursively(int parent_id, int level,
                                    const std::unordered_map<uint32_t, uint32_t>
                                        &path_index_to_spec_index_map,
                                    Scene *scene);

  bool ReconstructScene(Scene *scene);

  ///
  /// --------------------------------------------------
  ///

  std::string GetError() { return _err; }

  std::string GetWarning() { return _warn; }

  // Approximated memory usage in [mb]
  size_t GetMemoryUsage() const { return memory_used / (1024 * 1024); }

  //
  // APIs valid after successfull Parse()
  //

  size_t NumPaths() const { return _paths.size(); }

 private:
  bool ReadCompressedPaths(const uint64_t ref_num_paths);

  const StreamReader *_sr = nullptr;
  std::string _err;
  std::string _warn;

  int _num_threads{1};

  // Tracks the memory used(In advisorily manner since counting memory usage is
  // done by manually, so not all memory consumption could be tracked)
  size_t memory_used{0};  // in bytes.

  // Header(bootstrap)
  uint8_t _version[3] = {0, 0, 0};

  TableOfContents _toc;

  int64_t _toc_offset{0};

  // index to _toc.sections
  int64_t _tokens_index{-1};
  int64_t _paths_index{-1};
  int64_t _strings_index{-1};
  int64_t _fields_index{-1};
  int64_t _fieldsets_index{-1};
  int64_t _specs_index{-1};

  std::vector<std::string> _tokens;
  std::vector<Index> _string_indices;
  std::vector<Field> _fields;
  std::vector<Index> _fieldset_indices;
  std::vector<Spec> _specs;
  std::vector<Path> _paths;

  std::vector<Node> _nodes;  // [0] = root node

  bool BuildDecompressedPathsImpl(
      std::vector<uint32_t> const &pathIndexes,
      std::vector<int32_t> const &elementTokenIndexes,
      std::vector<int32_t> const &jumps, size_t curIndex, Path parentPath);

  bool UnpackValueRep(const ValueRep &rep, Value *value);

  //
  // Construct node hierarchy.
  //
  bool BuildNodeHierarchy(std::vector<uint32_t> const &pathIndexes,
                           std::vector<int32_t> const &elementTokenIndexes,
                           std::vector<int32_t> const &jumps, size_t curIndex,
                           int64_t parentNodeIndex);

  //
  // Reader util functions
  //
  bool ReadIndex(Index *i);

  // bool ReadToken(std::string *s);
  bool ReadString(std::string *s);

  bool ReadValueRep(ValueRep *rep);

  bool ReadPathArray(std::vector<Path> *d);

  // Dictionary
  bool ReadDictionary(Value::Dictionary *d);

  bool ReadTimeSamples(TimeSamples *d);

  // integral array
  template <typename T>
  bool ReadIntArray(bool is_compressed, std::vector<T> *d);

  bool ReadHalfArray(bool is_compressed, std::vector<uint16_t> *d);
  bool ReadFloatArray(bool is_compressed, std::vector<float> *d);
  bool ReadDoubleArray(bool is_compressed, std::vector<double> *d);

  // PathListOp
  bool ReadPathListOp(ListOp<Path> *d);
  bool ReadTokenListOp(ListOp<std::string> *d); // TODO(syoyo): Use `Token` type
};

bool Parser::ReadIndex(Index *i) {
  // string is serialized as StringIndex
  uint32_t value;
  if (!_sr->read4(&value)) {
    _err += "Failed to read Index\n";
    return false;
  }
  (*i) = Index(value);
  return true;
}

/* Currently unused
bool Parser::ReadToken(std::string *s) {
  Index token_index;
  if (!ReadIndex(&token_index)) {
    _err += "Failed to read Index for token data.\n";
    return false;
  }

  (*s) = GetToken(token_index);

  return true;
}
*/

bool Parser::ReadString(std::string *s) {
  // string is serialized as StringIndex
  Index string_index;
  if (!ReadIndex(&string_index)) {
    _err += "Failed to read Index for string data.\n";
    return false;
  }

  (*s) = GetString(string_index);

  return true;
}

bool Parser::ReadValueRep(ValueRep *rep) {
  if (!_sr->read8(reinterpret_cast<uint64_t *>(rep))) {
    _err += "Failed to read ValueRep.\n";
    return false;
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "value = " << rep->GetData() << "\n";
#endif

  return true;
}

template <typename T>
bool Parser::ReadIntArray(bool is_compressed, std::vector<T> *d) {
  if (!is_compressed) {
    size_t length;
    // < ver 0.7.0  use 32bit
    if ((_version[0] == 0) && ((_version[1] < 7))) {
      uint32_t n;
      if (!_sr->read4(&n)) {
        _err += "Failed to read the number of array elements.\n";
        return false;
      }
      length = size_t(n);
    } else {
      uint64_t n;
      if (!_sr->read8(&n)) {
        _err += "Failed to read the number of array elements.\n";
        return false;
      }

      length = size_t(n);
    }

    d->resize(length);

    // TODO(syoyo): Zero-copy
    if (!_sr->read(sizeof(T) * length, sizeof(T) * length,
                   reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read integer array data.\n";
      return false;
    }

    return true;

  } else {
    size_t length;
    // < ver 0.7.0  use 32bit
    if ((_version[0] == 0) && ((_version[1] < 7))) {
      uint32_t n;
      if (!_sr->read4(&n)) {
        _err += "Failed to read the number of array elements.\n";
        return false;
      }
      length = size_t(n);
    } else {
      uint64_t n;
      if (!_sr->read8(&n)) {
        _err += "Failed to read the number of array elements.\n";
        return false;
      }

      length = size_t(n);
    }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "array.len = " << length << "\n";
#endif

    d->resize(length);

    if (length < kMinCompressedArraySize) {
      size_t sz = sizeof(T) * length;
      // Not stored in compressed.
      // reader.ReadContiguous(odata, osize);
      if (!_sr->read(sz, sz, reinterpret_cast<uint8_t *>(d->data()))) {
        _err += "Failed to read uncompressed array data.\n";
        return false;
      }
      return true;
    }

    return ReadCompressedInts(_sr, d->data(), d->size());
  }
}

bool Parser::ReadHalfArray(bool is_compressed, std::vector<uint16_t> *d) {
  if (!is_compressed) {
    size_t length;
    // < ver 0.7.0  use 32bit
    if ((_version[0] == 0) && ((_version[1] < 7))) {
      uint32_t n;
      if (!_sr->read4(&n)) {
        _err += "Failed to read the number of array elements.\n";
        return false;
      }
      length = size_t(n);
    } else {
      uint64_t n;
      if (!_sr->read8(&n)) {
        _err += "Failed to read the number of array elements.\n";
        return false;
      }

      length = size_t(n);
    }

    d->resize(length);

    // TODO(syoyo): Zero-copy
    if (!_sr->read(sizeof(uint16_t) * length, sizeof(uint16_t) * length,
                   reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read half array data.\n";
      return false;
    }

    return true;
  }

  //
  // compressed data is represented by integers or look-up table.
  //

  size_t length;
  // < ver 0.7.0  use 32bit
  if ((_version[0] == 0) && ((_version[1] < 7))) {
    uint32_t n;
    if (!_sr->read4(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }
    length = size_t(n);
  } else {
    uint64_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }

    length = size_t(n);
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "array.len = " << length << "\n";
#endif

  d->resize(length);

  if (length < kMinCompressedArraySize) {
    size_t sz = sizeof(uint16_t) * length;
    // Not stored in compressed.
    // reader.ReadContiguous(odata, osize);
    if (!_sr->read(sz, sz, reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read uncompressed array data.\n";
      return false;
    }
    return true;
  }

  // Read the code
  char code;
  if (!_sr->read1(&code)) {
    _err += "Failed to read the code.\n";
    return false;
  }

  if (code == 'i') {
    // Compressed integers.
    std::vector<int32_t> ints(length);
    if (!ReadCompressedInts(_sr, ints.data(), ints.size())) {
      _err += "Failed to read compressed ints in ReadHalfArray.\n";
      return false;
    }
    for (size_t i = 0; i < length; i++) {
      float f = float(ints[i]);
      float16 h = float_to_half_full(f);
      (*d)[i] = h;
    }
  } else if (code == 't') {
    // Lookup table & indexes.
    uint32_t lutSize;
    if (!_sr->read4(&lutSize)) {
      _err += "Failed to read lutSize in ReadHalfArray.\n";
      return false;
    }

    std::vector<uint16_t> lut(lutSize);
    if (!_sr->read(sizeof(uint16_t) * lutSize, sizeof(uint16_t) * lutSize,
                   reinterpret_cast<uint8_t *>(lut.data()))) {
      _err += "Failed to read lut table in ReadHalfArray.\n";
      return false;
    }

    std::vector<uint32_t> indexes(length);
    if (!ReadCompressedInts(_sr, indexes.data(), indexes.size())) {
      _err += "Failed to read lut indices in ReadHalfArray.\n";
      return false;
    }

    auto o = d->data();
    for (auto index : indexes) {
      *o++ = lut[index];
    }
  } else {
    _err += "Invalid code. Data is currupted\n";
    return false;
  }

  return true;
}

bool Parser::ReadFloatArray(bool is_compressed, std::vector<float> *d) {
  if (!is_compressed) {
    size_t length;
    // < ver 0.7.0  use 32bit
    if ((_version[0] == 0) && ((_version[1] < 7))) {
      uint32_t n;
      if (!_sr->read4(&n)) {
        _err += "Failed to read the number of array elements.\n";
        return false;
      }
      length = size_t(n);
    } else {
      uint64_t n;
      if (!_sr->read8(&n)) {
        _err += "Failed to read the number of array elements.\n";
        return false;
      }

      length = size_t(n);
    }

    d->resize(length);

    // TODO(syoyo): Zero-copy
    if (!_sr->read(sizeof(float) * length, sizeof(float) * length,
                   reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read float array data.\n";
      return false;
    }

    return true;
  }

  //
  // compressed data is represented by integers or look-up table.
  //

  size_t length;
  // < ver 0.7.0  use 32bit
  if ((_version[0] == 0) && ((_version[1] < 7))) {
    uint32_t n;
    if (!_sr->read4(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }
    length = size_t(n);
  } else {
    uint64_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }

    length = size_t(n);
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "array.len = " << length << "\n";
#endif

  d->resize(length);

  if (length < kMinCompressedArraySize) {
    size_t sz = sizeof(float) * length;
    // Not stored in compressed.
    // reader.ReadContiguous(odata, osize);
    if (!_sr->read(sz, sz, reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read uncompressed array data.\n";
      return false;
    }
    return true;
  }

  // Read the code
  char code;
  if (!_sr->read1(&code)) {
    _err += "Failed to read the code.\n";
    return false;
  }

  if (code == 'i') {
    // Compressed integers.
    std::vector<int32_t> ints(length);
    if (!ReadCompressedInts(_sr, ints.data(), ints.size())) {
      _err += "Failed to read compressed ints in ReadFloatArray.\n";
      return false;
    }
    std::copy(ints.begin(), ints.end(), d->data());
  } else if (code == 't') {
    // Lookup table & indexes.
    uint32_t lutSize;
    if (!_sr->read4(&lutSize)) {
      _err += "Failed to read lutSize in ReadFloatArray.\n";
      return false;
    }

    std::vector<float> lut(lutSize);
    if (!_sr->read(sizeof(float) * lutSize, sizeof(float) * lutSize,
                   reinterpret_cast<uint8_t *>(lut.data()))) {
      _err += "Failed to read lut table in ReadFloatArray.\n";
      return false;
    }

    std::vector<uint32_t> indexes(length);
    if (!ReadCompressedInts(_sr, indexes.data(), indexes.size())) {
      _err += "Failed to read lut indices in ReadFloatArray.\n";
      return false;
    }

    auto o = d->data();
    for (auto index : indexes) {
      *o++ = lut[index];
    }
  } else {
    _err += "Invalid code. Data is currupted\n";
    return false;
  }

  return true;
}

bool Parser::ReadDoubleArray(bool is_compressed, std::vector<double> *d) {
  if (!is_compressed) {
    size_t length;
    // < ver 0.7.0  use 32bit
    if ((_version[0] == 0) && ((_version[1] < 7))) {
      uint32_t n;
      if (!_sr->read4(&n)) {
        _err += "Failed to read the number of array elements.\n";
        return false;
      }
      length = size_t(n);
    } else {
      uint64_t n;
      if (!_sr->read8(&n)) {
        _err += "Failed to read the number of array elements.\n";
        return false;
      }

      length = size_t(n);
    }

    d->resize(length);

    // TODO(syoyo): Zero-copy
    if (!_sr->read(sizeof(double) * length, sizeof(double) * length,
                   reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read double array data.\n";
      return false;
    }

    return true;
  }

  //
  // compressed data is represented by integers or look-up table.
  //

  size_t length;
  // < ver 0.7.0  use 32bit
  if ((_version[0] == 0) && ((_version[1] < 7))) {
    uint32_t n;
    if (!_sr->read4(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }
    length = size_t(n);
  } else {
    uint64_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }

    length = size_t(n);
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "array.len = " << length << "\n";
#endif

  d->resize(length);

  if (length < kMinCompressedArraySize) {
    size_t sz = sizeof(double) * length;
    // Not stored in compressed.
    // reader.ReadContiguous(odata, osize);
    if (!_sr->read(sz, sz, reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read uncompressed array data.\n";
      return false;
    }
    return true;
  }

  // Read the code
  char code;
  if (!_sr->read1(&code)) {
    _err += "Failed to read the code.\n";
    return false;
  }

  if (code == 'i') {
    // Compressed integers.
    std::vector<int32_t> ints(length);
    if (!ReadCompressedInts(_sr, ints.data(), ints.size())) {
      _err += "Failed to read compressed ints in ReadDoubleArray.\n";
      return false;
    }
    std::copy(ints.begin(), ints.end(), d->data());
  } else if (code == 't') {
    // Lookup table & indexes.
    uint32_t lutSize;
    if (!_sr->read4(&lutSize)) {
      _err += "Failed to read lutSize in ReadDoubleArray.\n";
      return false;
    }

    std::vector<double> lut(lutSize);
    if (!_sr->read(sizeof(double) * lutSize, sizeof(double) * lutSize,
                   reinterpret_cast<uint8_t *>(lut.data()))) {
      _err += "Failed to read lut table in ReadDoubleArray.\n";
      return false;
    }

    std::vector<uint32_t> indexes(length);
    if (!ReadCompressedInts(_sr, indexes.data(), indexes.size())) {
      _err += "Failed to read lut indices in ReadDoubleArray.\n";
      return false;
    }

    auto o = d->data();
    for (auto index : indexes) {
      *o++ = lut[index];
    }
  } else {
    _err += "Invalid code. Data is currupted\n";
    return false;
  }

  return true;
}

bool Parser::ReadTimeSamples(TimeSamples *d) {
  (void)d;

  // TODO(syoyo): Deferred loading of TimeSamples?(See USD's implementation)

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "ReadTimeSamples: offt before tell = " << _sr->tell() << "\n";
#endif

  // 8byte for the offset for recursive value. See RecursiveRead() in
  // crateFile.cpp for details.
  int64_t offset{0};
  if (!_sr->read8(&offset)) {
    _err += "Failed to read the offset for value in Dictionary.\n";
    return false;
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "TimeSample times value offset = " << offset << "\n";
  std::cout << "TimeSample tell = " << _sr->tell() << "\n";
#endif

  // -8 to compensate sizeof(offset)
  if (!_sr->seek_from_current(offset - 8)) {
    _err += "Failed to seek to TimeSample times. Invalid offset value: " +
            std::to_string(offset) + "\n";
    return false;
  }

  // TODO(syoyo): Deduplicate times?

  ValueRep rep{0};
  if (!ReadValueRep(&rep)) {
    _err += "Failed to read ValueRep for TimeSample' times element.\n";
    return false;
  }

  // Save offset
  size_t values_offset = _sr->tell();

  Value value;
  if (!UnpackValueRep(rep, &value)) {
    _err += "Failed to unpack value of TimeSample's times element.\n";
    return false;
  }

  // must be an array of double.
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "TimeSample times:" << value.GetTypeName() << "\n";
  std::cout << "TODO: Parse TimeSample values\n";
#endif

  //
  // Parse values for TimeSamples.
  // TODO(syoyo): Delayed loading of values.
  //

  // seek position will be changed in `_UnpackValueRep`, so revert it.
  if (!_sr->seek_set(values_offset)) {
    _err += "Failed to seek to TimeSamples values.\n";
    return false;
  }

  // 8byte for the offset for recursive value. See RecursiveRead() in
  // crateFile.cpp for details.
  if (!_sr->read8(&offset)) {
    _err += "Failed to read the offset for value in TimeSamples.\n";
    return false;
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "TimeSample value offset = " << offset << "\n";
  std::cout << "TimeSample tell = " << _sr->tell() << "\n";
#endif

  // -8 to compensate sizeof(offset)
  if (!_sr->seek_from_current(offset - 8)) {
    _err += "Failed to seek to TimeSample values. Invalid offset value: " +
            std::to_string(offset) + "\n";
    return false;
  }

  uint64_t num_values{0};
  if (!_sr->read8(&num_values)) {
    _err += "Failed to read the number of values from TimeSamples.\n";
    return false;
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "Number of values = " << num_values << "\n";
  std::cout << "TODO: TimeSamples Implement\n";
#endif

  _warn += "TODO: Decode TimeSample's values\n";

  // Move to next location.
  // sizeof(uint64) = sizeof(ValueRep)
  if (!_sr->seek_from_current(int64_t(sizeof(uint64_t) * num_values))) {
    _err += "Failed to seek over TimeSamples's values.\n";
    return false;
  }

  return true;
}

bool Parser::ReadPathArray(std::vector<Path> *d) {

  // array data is not compressed
  auto ReadFn = [this](std::vector<Path> &result) -> bool {
    uint64_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read # of elements in ListOp.\n";
      return false;
    }

    std::vector<Index> ivalue(static_cast<size_t>(n));

    if (!_sr->read(size_t(n) * sizeof(Index), size_t(n) * sizeof(Index),
                   reinterpret_cast<uint8_t *>(ivalue.data()))) {
      _err += "Failed to read ListOp data.\n";
      return false;
    }

    // reconstruct
    result.resize(static_cast<size_t>(n));
    for (size_t i = 0; i < n; i++) {
      result[i] = GetPath(ivalue[i]);
    }

    return true;
  };

  std::vector<Path> items;
  if (!ReadFn(items)) {
    _err += "Failed to read Path vector.\n";
    return false;
  }

  (*d) = items;

  return true;
}

bool Parser::ReadTokenListOp(ListOp<std::string> *d) {
  // read ListOpHeader
  ListOpHeader h;
  if (!_sr->read1(&h.bits)) {
    _err += "Failed to read ListOpHeader\n";
    return false;
  }

  if (h.IsExplicit()) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "Explicit\n";
#endif
    d->ClearAndMakeExplicit();
  }

  // array data is not compressed
  auto ReadFn = [this](std::vector<std::string> &result) -> bool {
    uint64_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read # of elements in ListOp.\n";
      return false;
    }

    std::vector<Index> ivalue(static_cast<size_t>(n));

    if (!_sr->read(size_t(n) * sizeof(Index), size_t(n) * sizeof(Index),
                   reinterpret_cast<uint8_t *>(ivalue.data()))) {
      _err += "Failed to read ListOp data.\n";
      return false;
    }

    // reconstruct
    result.resize(static_cast<size_t>(n));
    for (size_t i = 0; i < n; i++) {
      result[i] = GetToken(ivalue[i]);
    }

    return true;
  };

  if (h.HasExplicitItems()) {
    std::vector<std::string> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::ExplicitItems.\n";
      return false;
    }

    d->SetExplicitItems(items);
  }

  if (h.HasAddedItems()) {
    std::vector<std::string> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::AddedItems.\n";
      return false;
    }

    d->SetAddedItems(items);
  }

  if (h.HasPrependedItems()) {
    std::vector<std::string> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::PrependedItems.\n";
      return false;
    }

    d->SetPrependedItems(items);
  }

  if (h.HasAppendedItems()) {
    std::vector<std::string> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::AppendedItems.\n";
      return false;
    }

    d->SetAppendedItems(items);
  }

  if (h.HasDeletedItems()) {
    std::vector<std::string> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::DeletedItems.\n";
      return false;
    }

    d->SetDeletedItems(items);
  }

  if (h.HasOrderedItems()) {
    std::vector<std::string> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::OrderedItems.\n";
      return false;
    }

    d->SetOrderedItems(items);
  }

  return true;
}


bool Parser::ReadPathListOp(ListOp<Path> *d) {
  // read ListOpHeader
  ListOpHeader h;
  if (!_sr->read1(&h.bits)) {
    _err += "Failed to read ListOpHeader\n";
    return false;
  }

  if (h.IsExplicit()) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "Explicit\n";
#endif
    d->ClearAndMakeExplicit();
  }

  // array data is not compressed
  auto ReadFn = [this](std::vector<Path> &result) -> bool {
    uint64_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read # of elements in ListOp.\n";
      return false;
    }

    std::vector<Index> ivalue(static_cast<size_t>(n));

    if (!_sr->read(size_t(n) * sizeof(Index), size_t(n) * sizeof(Index),
                   reinterpret_cast<uint8_t *>(ivalue.data()))) {
      _err += "Failed to read ListOp data.\n";
      return false;
    }

    // reconstruct
    result.resize(static_cast<size_t>(n));
    for (size_t i = 0; i < n; i++) {
      result[i] = GetPath(ivalue[i]);
    }

    return true;
  };

  if (h.HasExplicitItems()) {
    std::vector<Path> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::ExplicitItems.\n";
      return false;
    }

    d->SetExplicitItems(items);
  }

  if (h.HasAddedItems()) {
    std::vector<Path> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::AddedItems.\n";
      return false;
    }

    d->SetAddedItems(items);
  }

  if (h.HasPrependedItems()) {
    std::vector<Path> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::PrependedItems.\n";
      return false;
    }

    d->SetPrependedItems(items);
  }

  if (h.HasAppendedItems()) {
    std::vector<Path> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::AppendedItems.\n";
      return false;
    }

    d->SetAppendedItems(items);
  }

  if (h.HasDeletedItems()) {
    std::vector<Path> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::DeletedItems.\n";
      return false;
    }

    d->SetDeletedItems(items);
  }

  if (h.HasOrderedItems()) {
    std::vector<Path> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::OrderedItems.\n";
      return false;
    }

    d->SetOrderedItems(items);
  }

  return true;
}

bool Parser::ReadDictionary(Value::Dictionary *d) {
  Value::Dictionary dict;
  uint64_t sz;
  if (!_sr->read8(&sz)) {
    _err += "Failed to read the number of elements for Dictionary data.\n";
    return false;
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "# of elements in dict " << sz << "\n";
#endif

  while (sz--) {
    // key(StringIndex)
    std::string key;
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "key before tell = " << _sr->tell() << "\n";
#endif
    if (!ReadString(&key)) {
      _err += "Failed to read key string for Dictionary element.\n";
      return false;
    }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "offt before tell = " << _sr->tell() << "\n";
#endif

    // 8byte for the offset for recursive value. See RecursiveRead() in
    // crateFile.cpp for details.
    int64_t offset{0};
    if (!_sr->read8(&offset)) {
      _err += "Failed to read the offset for value in Dictionary.\n";
      return false;
    }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "value offset = " << offset << "\n";
    std::cout << "tell = " << _sr->tell() << "\n";
#endif

    // -8 to compensate sizeof(offset)
    if (!_sr->seek_from_current(offset - 8)) {
      _err +=
          "Failed to seek. Invalid offset value: " + std::to_string(offset) +
          "\n";
      return false;
    }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "+offset tell = " << _sr->tell() << "\n";

    std::cout << "key = " << key << "\n";
#endif

    ValueRep rep{0};
    if (!ReadValueRep(&rep)) {
      _err += "Failed to read value for Dictionary element.\n";
      return false;
    }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "vrep.ty = " << rep.GetType() << "\n";
    std::cout << "vrep = " << GetValueTypeRepr(rep.GetType()) << "\n";
#endif

    size_t saved_position = _sr->tell();

    Value value;
    if (!UnpackValueRep(rep, &value)) {
      _err += "Failed to unpack value of Dictionary element.\n";
      return false;
    }

    dict[key] = value;

    if (!_sr->seek_set(saved_position)) {
      _err +=
          "Failed to set seek in ReadDict\n";
      return false;
    }
  }

  (*d) = std::move(dict);
  return true;
}

bool Parser::UnpackValueRep(const ValueRep &rep, Value *value) {
  ValueType ty = GetValueType(rep.GetType());
  DCOUT(GetValueTypeRepr(rep.GetType()));
  if (rep.IsInlined()) {
    uint32_t d = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));

    DCOUT("d = " << d << ", ty.id = " << ty.id);
    if (ty.id == VALUE_TYPE_BOOL) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));

      value->SetBool(d ? true : false);

      return true;

    } else if (ty.id == VALUE_TYPE_ASSET_PATH) {
      // AssetPath = std::string(storage format is TokenIndex).

      std::string str = GetToken(Index(d));

      value->SetAssetPath(str);

      return true;

    } else if (ty.id == VALUE_TYPE_SPECIFIER) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "Specifier: "
                << GetSpecifierString(static_cast<Specifier>(d)) << "\n";
#endif

      if (d >= static_cast<int>(Specifier::Invalid)) {
        _err += "Invalid value for Specifier\n";
        return false;
      }

      value->SetSpecifier(d);

      return true;
    } else if (ty.id == VALUE_TYPE_PERMISSION) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "Permission: "
                << GetPermissionString(static_cast<Permission>(d)) << "\n";
#endif

      if (d >= static_cast<int>(Permission::Invalid)) {
        _err += "Invalid value for Permission\n";
        return false;
      }

      value->SetPermission(d);

      return true;
    } else if (ty.id == VALUE_TYPE_VARIABILITY) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "Variability: "
                << GetVariabilityString(static_cast<Variability>(d)) << "\n";
#endif

      if (d >= static_cast<int>(Variability::Invalid)) {
        _err += "Invalid value for Variability\n";
        return false;
      }

      value->SetVariability(d);

      return true;
    } else if (ty.id == VALUE_TYPE_TOKEN) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));
      std::string str = GetToken(Index(d));
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "value.token = " << str << "\n";
#endif

      value->SetToken(str);

      return true;

    } else if (ty.id == VALUE_TYPE_STRING) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));
      std::string str = GetString(Index(d));
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "value.string = " << str << "\n";
#endif

      value->SetString(str);

      return true;

    } else if (ty.id == VALUE_TYPE_INT) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));
      int ival;
      memcpy(&ival, &d, sizeof(int));

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "value.int = " << ival << "\n";
#endif

      value->SetInt(ival);

      return true;

    } else if (ty.id == VALUE_TYPE_FLOAT) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));
      float f;
      memcpy(&f, &d, sizeof(float));

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "value.float = " << f << "\n";
#endif

      value->SetFloat(f);

      return true;

    } else if (ty.id == VALUE_TYPE_DOUBLE) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));
      // Value is saved as float
      float f;
      memcpy(&f, &d, sizeof(float));
      double v = double(f);

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "value.double = " << v << "\n";
#endif

      value->SetDouble(v);

      return true;

    } else if (ty.id == VALUE_TYPE_VEC3I) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));

      // Value is represented in int8
      int8_t data[3];
      memcpy(&data, &d, 3);

      Vec3i v;
      v[0] = static_cast<int32_t>(data[0]);
      v[1] = static_cast<int32_t>(data[1]);
      v[2] = static_cast<int32_t>(data[2]);

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "value.vec3i = " << v[0] << ", " << v[1] << ", " << v[2]
                << "\n";
#endif

      value->SetVec3i(v);

      return true;
    } else if (ty.id == VALUE_TYPE_VEC4I) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));

      // Value is represented in int8
      int8_t data[4];
      memcpy(&data, &d, 4);

      Vec4i v;
      v[0] = static_cast<int32_t>(data[0]);
      v[1] = static_cast<int32_t>(data[1]);
      v[2] = static_cast<int32_t>(data[2]);
      v[3] = static_cast<int32_t>(data[3]);

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "value.vec4i = " << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3]
                << "\n";
#endif

      value->SetVec4i(v);

      return true;

    } else if (ty.id == VALUE_TYPE_VEC3F) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));

      // Value is represented in int8
      int8_t data[3];
      memcpy(&data, &d, 3);

      Vec3f v;
      v[0] = static_cast<float>(data[0]);
      v[1] = static_cast<float>(data[1]);
      v[2] = static_cast<float>(data[2]);

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "value.vec3f = " << v[0] << ", " << v[1] << ", " << v[2]
                << "\n";
#endif

      value->SetVec3f(v);

      return true;
    } else if (ty.id == VALUE_TYPE_VEC4F) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));

      // Value is represented in int8
      int8_t data[4];
      memcpy(&data, &d, 4);

      Vec4f v;
      v[0] = static_cast<float>(data[0]);
      v[1] = static_cast<float>(data[1]);
      v[2] = static_cast<float>(data[2]);
      v[3] = static_cast<float>(data[3]);

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "value.vec3f = " << v[1] << ", " << v[1] << ", " << v[2] << ", " << v[3]
                << "\n";
#endif

      value->SetVec4f(v);

      return true;

    } else if (ty.id == VALUE_TYPE_VEC3D) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));

      // Value is represented in int8
      int8_t data[3];
      memcpy(&data, &d, 3);

      Vec3d v;
      v[0] = static_cast<double>(data[0]);
      v[1] = static_cast<double>(data[1]);
      v[2] = static_cast<double>(data[2]);

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "value.vec3d = " << v[0] << ", " << v[1] << ", " << v[2]
                << "\n";
#endif

      value->SetVec3d(v);

      return true;
    } else if (ty.id == VALUE_TYPE_VEC4D) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));

      // Value is represented in int8
      int8_t data[4];
      memcpy(&data, &d, 4);

      Vec4d v;
      v[0] = static_cast<double>(data[0]);
      v[1] = static_cast<double>(data[1]);
      v[2] = static_cast<double>(data[2]);
      v[3] = static_cast<double>(data[3]);

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "value.vec4d = " << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3]
                << "\n";
#endif

      value->SetVec4d(v);

      return true;

    } else if (ty.id == VALUE_TYPE_MATRIX2D) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));

      // Matrix contains diagnonal components only, and values are represented
      // in int8
      int8_t data[2];
      memcpy(&data, &d, 2);

      Matrix2d v;
      memset(v.m, 0, sizeof(Matrix2d));
      v.m[0][0] = static_cast<double>(data[0]);
      v.m[1][1] = static_cast<double>(data[1]);

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "value.matrix(diag) = " << v.m[0][0] << ", " << v.m[1][1]
                << "\n";
#endif

      value->SetMatrix2d(v);

      return true;

    } else if (ty.id == VALUE_TYPE_MATRIX3D) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));

      // Matrix contains diagnonal components only, and values are represented
      // in int8
      int8_t data[3];
      memcpy(&data, &d, 3);

      Matrix3d v;
      memset(v.m, 0, sizeof(Matrix3d));
      v.m[0][0] = static_cast<double>(data[0]);
      v.m[1][1] = static_cast<double>(data[1]);
      v.m[2][2] = static_cast<double>(data[2]);

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "value.matrix(diag) = " << v.m[0][0] << ", " << v.m[1][1]
                << ", " << v.m[2][2] << "\n";
#endif

      value->SetMatrix3d(v);

      return true;

    } else if (ty.id == VALUE_TYPE_MATRIX4D) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));

      // Matrix contains diagnonal components only, and values are represented
      // in int8
      int8_t data[4];
      memcpy(&data, &d, 4);

      Matrix4d v;
      memset(v.m, 0, sizeof(Matrix4d));
      v.m[0][0] = static_cast<double>(data[0]);
      v.m[1][1] = static_cast<double>(data[1]);
      v.m[2][2] = static_cast<double>(data[2]);
      v.m[3][3] = static_cast<double>(data[3]);

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "value.matrix(diag) = " << v.m[0][0] << ", " << v.m[1][1]
                << ", " << v.m[2][2] << ", " << v.m[3][3] << "\n";
#endif

      value->SetMatrix4d(v);

      return true;
    } else {
      // TODO(syoyo)
      PUSH_ERROR("TODO: Inlined Value: " + GetValueTypeRepr(rep.GetType()));

      return false;
    }
    // ====================================================
  } else {
    // payload is the offset to data.
    uint64_t offset = rep.GetPayload();
    if (!_sr->seek_set(offset)) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cerr << "Invalid offset\n";
#endif
      return false;
    }

    // printf("rep = 0x%016lx\n", rep.GetData());

    if (ty.id == VALUE_TYPE_TOKEN) {
      // Guess array of Token
      assert(!rep.IsCompressed());
      assert(rep.IsArray());

      uint64_t n;
      if (!_sr->read8(&n)) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cerr << "Failed to read the number of array elements\n";
#endif
        return false;
      }

      std::vector<Index> v(static_cast<size_t>(n));
      if (!_sr->read(size_t(n) * sizeof(Index), size_t(n) * sizeof(Index),
                     reinterpret_cast<uint8_t *>(v.data()))) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cerr << "Failed to read TokenIndex array\n";
#endif
        return false;
      }

      std::vector<std::string> tokens(static_cast<size_t>(n));

      for (size_t i = 0; i < n; i++) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cout << "Token[" << i << "] = " << GetToken(v[i]) << " ("
                  << v[i].value << ")\n";
#endif
        tokens[i] = GetToken(v[i]);
      }

      value->SetTokenArray(tokens);

      return true;
    } else if (ty.id == VALUE_TYPE_STRING) {
      assert(!rep.IsCompressed());
      assert(rep.IsArray());

      uint64_t n;
      if (!_sr->read8(&n)) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cerr << "Failed to read the number of array elements\n";
#endif
        return false;
      }

      std::vector<Index> v(static_cast<size_t>(n));
      if (!_sr->read(size_t(n) * sizeof(Index), size_t(n) * sizeof(Index),
                     reinterpret_cast<uint8_t *>(v.data()))) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cerr << "Failed to read TokenIndex array\n";
#endif
        return false;
      }

      std::vector<std::string> stringArray(static_cast<size_t>(n));

      for (size_t i = 0; i < n; i++) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cout << "String[" << i << "] = " << GetString(v[i]) << " ("
                  << v[i].value << ")\n";
#endif
        stringArray[i] = GetString(v[i]);
      }

      // In TinyUSDZ, token == string
      value->SetTokenArray(stringArray);

      return true;

    } else if (ty.id == VALUE_TYPE_INT) {
      assert(rep.IsArray());

      std::vector<int32_t> v;
      if (!ReadIntArray(rep.IsCompressed(), &v)) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cerr << "Failed to read Int array\n";
#endif
        return false;
      }

      if (v.empty()) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cerr << "Empty Int array\n";
#endif
        return false;
      }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      for (size_t i = 0; i < v.size(); i++) {
        std::cout << "Int[" << i << "] = " << v[i] << "\n";
      }
#endif

      if (rep.IsArray()) {
        value->SetIntArray(v.data(), v.size());
      } else {
        value->SetInt(v[0]);
      }

      return true;

    } else if (ty.id == VALUE_TYPE_VEC2F) {
      assert(!rep.IsCompressed());

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cerr << "Failed to read the number of array elements\n";
#endif
          return false;
        }

        std::vector<Vec2f> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(Vec2f), size_t(n) * sizeof(Vec2f),
                       reinterpret_cast<uint8_t *>(v.data()))) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cerr << "Failed to read Vec2f array\n";
#endif
          return false;
        }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        for (size_t i = 0; i < v.size(); i++) {
          std::cout << "Vec2f[" << i << "] = " << v[i][0] << ", " << v[i][1]
                    << "\n";
        }
#endif

        value->SetVec2fArray(v.data(), v.size());

      } else {
        Vec2f v;
        if (!_sr->read(sizeof(Vec2f), sizeof(Vec2f),
                       reinterpret_cast<uint8_t *>(&v))) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cerr << "Failed to read Vec2f\n";
#endif
          return false;
        }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cout << "Vec2f = " << v[0] << ", " << v[1] << "\n";
#endif

        value->SetVec2f(v);
      }

      return true;
    } else if (ty.id == VALUE_TYPE_VEC3F) {
      assert(!rep.IsCompressed());

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cerr << "Failed to read the number of array elements\n";
#endif
          return false;
        }

        std::vector<Vec3f> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(Vec3f), size_t(n) * sizeof(Vec3f),
                       reinterpret_cast<uint8_t *>(v.data()))) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cerr << "Failed to read Vec3f array\n";
#endif
          return false;
        }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        for (size_t i = 0; i < v.size(); i++) {
          std::cout << "Vec3f[" << i << "] = " << v[i][0] << ", " << v[i][1]
                    << ", " << v[i][2] << "\n";
        }
#endif
        value->SetVec3fArray(v.data(), v.size());

      } else {
        Vec3f v;
        if (!_sr->read(sizeof(Vec3f), sizeof(Vec3f),
                       reinterpret_cast<uint8_t *>(&v))) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cerr << "Failed to read Vec3f\n";
#endif
          return false;
        }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cout << "Vec3f = " << v[0] << ", " << v[1] << ", " << v[2] << "\n";
#endif

        value->SetVec3f(v);
      }

      return true;

    } else if (ty.id == VALUE_TYPE_VEC4F) {
      assert(!rep.IsCompressed());

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cerr << "Failed to read the number of array elements\n";
#endif
          return false;
        }

        std::vector<Vec4f> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(Vec4f), size_t(n) * sizeof(Vec4f),
                       reinterpret_cast<uint8_t *>(v.data()))) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cerr << "Failed to read Vec4f array\n";
#endif
          return false;
        }

        value->SetVec4fArray(v.data(), v.size());

      } else {
        Vec4f v;
        if (!_sr->read(sizeof(Vec4f), sizeof(Vec4f),
                       reinterpret_cast<uint8_t *>(&v))) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cerr << "Failed to read Vec4f\n";
#endif
          return false;
        }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cout << "Vec4f = " << v[0] << ", " << v[1] << ", " << v[2] << ", "
                  << v[3] << "\n";
#endif

        value->SetVec4f(v);
      }

      return true;

    } else if (ty.id == VALUE_TYPE_TOKEN_VECTOR) {
      assert(!rep.IsCompressed());
      // std::vector<Index>
      uint64_t n;
      if (!_sr->read8(&n)) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cerr << "Failed to read TokenVector value\n";
#endif
        return false;
      }
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "n = " << n << "\n";
#endif

      std::vector<Index> indices(static_cast<size_t>(n));
      if (!_sr->read(static_cast<size_t>(n) * sizeof(Index),
                     static_cast<size_t>(n) * sizeof(Index),
                     reinterpret_cast<uint8_t *>(indices.data()))) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cerr << "Failed to read TokenVector value\n";
#endif
        return false;
      }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      for (size_t i = 0; i < indices.size(); i++) {
        std::cout << "tokenIndex[" << i << "] = " << int(indices[i].value)
                  << "\n";
      }
#endif

      std::vector<std::string> tokens(indices.size());
      for (size_t i = 0; i < indices.size(); i++) {
        tokens[i] = GetToken(indices[i]);
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cout << "tokenVector[" << i << "] = " << tokens[i] << ", ("
                  << int(indices[i].value) << ")\n";
#endif
      }

      value->SetTokenArray(tokens);

      return true;
    } else if (ty.id == VALUE_TYPE_HALF) {
      if (rep.IsArray()) {
        std::vector<uint16_t> v;
        if (!ReadHalfArray(rep.IsCompressed(), &v)) {
          _err += "Failed to read half array value\n";
          return false;
        }

        value->SetHalfArray(v.data(), v.size());

        return true;
      } else {
        assert(!rep.IsCompressed());

        // ???
        _err += "Non-inlined, non-array Half value is not supported.\n";
        return false;
      }
    } else if (ty.id == VALUE_TYPE_FLOAT) {
      if (rep.IsArray()) {
        std::vector<float> v;
        if (!ReadFloatArray(rep.IsCompressed(), &v)) {
          _err += "Failed to read float array value\n";
          return false;
        }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        for (size_t i = 0; i < v.size(); i++) {
          std::cout << "Float[" << i << "] = " << v[i] << "\n";
        }
#endif

        value->SetFloatArray(v.data(), v.size());

        return true;
      } else {
        assert(!rep.IsCompressed());

        // ???
        _err += "Non-inlined, non-array Float value is not supported.\n";
        return false;
      }

    } else if (ty.id == VALUE_TYPE_DOUBLE) {
      if (rep.IsArray()) {
        std::vector<double> v;
        if (!ReadDoubleArray(rep.IsCompressed(), &v)) {
          _err += "Failed to read Double value\n";
          return false;
        }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        for (size_t i = 0; i < v.size(); i++) {
          std::cout << "Double[" << i << "] = " << v[i] << "\n";
        }
#endif

        value->SetDoubleArray(v.data(), v.size());

        return true;
      } else {
        assert(!rep.IsCompressed());

        double v;
        if (!_sr->read_double(&v)) {
          _err += "Failed to read Double value\n";
          return false;
        }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cout << "Double " << v << "\n";
#endif

        value->SetDouble(v);

        return true;
      }
    } else if (ty.id == VALUE_TYPE_VEC3I) {
      assert(!rep.IsCompressed());
      assert(rep.IsArray());

      Vec3i v;
      if (!_sr->read(sizeof(Vec3i), sizeof(Vec3i),
                     reinterpret_cast<uint8_t *>(&v))) {
        _err += "Failed to read Vec3i value\n";
        return false;
      }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "value.vec3i = " << v[0] << ", " << v[1] << ", " << v[2]
                << "\n";
#endif
      value->SetVec3i(v);

      return true;
    } else if (ty.id == VALUE_TYPE_VEC3F) {
      assert(!rep.IsCompressed());
      assert(rep.IsArray());

      Vec3f v;
      if (!_sr->read(sizeof(Vec3f), sizeof(Vec3f),
                     reinterpret_cast<uint8_t *>(&v))) {
        _err += "Failed to read Vec3f value\n";
        return false;
      }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "value.vec3f = " << v[0] << ", " << v[1] << ", " << v[2]
                << "\n";
#endif
      value->SetVec3f(v);

      return true;
    } else if (ty.id == VALUE_TYPE_VEC3D) {
      assert(!rep.IsCompressed());

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cerr << "Failed to read the number of array elements\n";
#endif
          return false;
        }

        std::vector<Vec3d> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(Vec3d), size_t(n) * sizeof(Vec3d),
                       reinterpret_cast<uint8_t *>(v.data()))) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cerr << "Failed to read Vec3d array\n";
#endif
          return false;
        }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        for (size_t i = 0; i < v.size(); i++) {
          std::cout << "Vec3d[" << i << "] = " << v[i][0] << ", " << v[i][1]
                    << ", " << v[i][2] << "\n";
        }
#endif
        value->SetVec3dArray(v.data(), v.size());

      } else {
        Vec3d v;
        if (!_sr->read(sizeof(Vec3d), sizeof(Vec3d),
                       reinterpret_cast<uint8_t *>(&v))) {
          _err += "Failed to read Vec3d value\n";
          return false;
        }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cout << "value.vec3d = " << v[0] << ", " << v[1] << ", " << v[2]
                  << "\n";
#endif
        value->SetVec3d(v);
      }

      return true;
    } else if (ty.id == VALUE_TYPE_VEC3H) {
      assert(!rep.IsCompressed());
      assert(rep.IsArray());

      Vec3h v;
      if (!_sr->read(sizeof(Vec3h), sizeof(Vec3h),
                     reinterpret_cast<uint8_t *>(&v))) {
        _err += "Failed to read Vec3h value\n";
        return false;
      }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "value.vec3d = " << to_float(v[0]) << ", " << to_float(v[1])
                << ", " << to_float(v[2]) << "\n";
#endif
      value->SetVec3h(v);

      return true;
    } else if (ty.id == VALUE_TYPE_QUATF) {
      assert(!rep.IsCompressed());

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cerr << "Failed to read the number of array elements\n";
#endif
          return false;
        }

        std::vector<Quatf> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(Quatf), size_t(n) * sizeof(Quatf),
                       reinterpret_cast<uint8_t *>(v.data()))) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cerr << "Failed to read Quatf array\n";
#endif
          return false;
        }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        for (size_t i = 0; i < v.size(); i++) {
          std::cout << "Quatf[" << i << "] = " << v[i].v[0] << ", " << v[i].v[1]
                    << ", " << v[i].v[2] << ", " << v[i].v[3] << "\n";
        }
#endif
        value->SetQuatfArray(v.data(), v.size());

      } else {
        Quatf v;
        if (!_sr->read(sizeof(Quatf), sizeof(Quatf),
                       reinterpret_cast<uint8_t *>(&v))) {
          _err += "Failed to read Quatf value\n";
          return false;
        }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cout << "value.quatf = " << v.v[0] << ", " << v.v[1] << ", " << v.v[2]
                  << ", " << v.v[3] << "\n";
#endif
        value->SetQuatf(v);
      }

      return true;
    } else if (ty.id == VALUE_TYPE_MATRIX4D) {
      assert(!rep.IsCompressed());

      std::cout << "Matrix4d: IsCompressed = " << rep.IsCompressed() << "\n";
      std::cout << "Matrix4d: IsArray = " << rep.IsArray() << "\n";

      if (rep.IsArray()) {

        uint64_t n;
        if (!_sr->read8(&n)) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cerr << "Failed to read the number of array elements\n";
#endif
          return false;
        }

        std::vector<Matrix4d> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(Matrix4d), size_t(n) * sizeof(Matrix4d),
                       reinterpret_cast<uint8_t *>(v.data()))) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cerr << "Failed to read Matrix4d array\n";
#endif
          return false;
        }

        value->SetMatrix4dArray(v.data(), v.size());

      } else {

        static_assert(sizeof(Matrix4d) == (8 * 16), "");

        Matrix4d v;
        if (!_sr->read(sizeof(Matrix4d), sizeof(Matrix4d),
                       reinterpret_cast<uint8_t *>(v.m))) {
          _err += "Failed to read Matrix4d value\n";
          return false;
        }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cout << "value.matrix4d = ";
        for (size_t i = 0; i < 4; i++) {
          for (size_t j = 0; j < 4; j++) {
            std::cout << v.m[i][j];
            if ((i == 3) && (j == 3)) {
            } else {
              std::cout << ", ";
            }
          }
        }
        std::cout << "\n";
#endif

        value->SetMatrix4d(v);
      }

      return true;

    } else if (ty.id == VALUE_TYPE_DICTIONARY) {
      assert(!rep.IsCompressed());
      assert(!rep.IsArray());

      Value::Dictionary dict;

      if (!ReadDictionary(&dict)) {
        _err += "Failed to read Dictionary value\n";
        return false;
      }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "Dict. nelems = " << dict.size() << "\n";
#endif

      value->SetDictionary(dict);

      return true;

    } else if (ty.id == VALUE_TYPE_PATH_LIST_OP) {
      // SdfListOp<class SdfPath>
      // => underliying storage is the array of ListOp[PathIndex]
      ListOp<Path> lst;

      if (!ReadPathListOp(&lst)) {
        _err += "Failed to read PathListOp data\n";
        return false;
      }

      value->SetPathListOp(lst);

      return true;

    } else if (ty.id == VALUE_TYPE_TIME_SAMPLES) {
      TimeSamples ts;
      if (!ReadTimeSamples(&ts)) {
        _err += "Failed to read TimeSamples data\n";
        return false;
      }

      value->SetTimeSamples(ts);

      return true;

    } else if (ty.id == VALUE_TYPE_DOUBLE_VECTOR) {
      std::vector<double> v;
      if (!ReadDoubleArray(rep.IsCompressed(), &v)) {
        _err += "Failed to read DoubleVector value\n";
        return false;
      }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      for (size_t i = 0; i < v.size(); i++) {
        std::cout << "Double[" << i << "] = " << v[i] << "\n";
      }
#endif

      value->SetDoubleArray(v.data(), v.size());

      return true;

    } else if (ty.id == VALUE_TYPE_PATH_VECTOR) {
      std::vector<Path> v;
      assert(!rep.IsCompressed());
      if (!ReadPathArray(&v)) {
        _err += "Failed to read PathVector value\n";
        return false;
      }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      for (size_t i = 0; i < v.size(); i++) {
        std::cout << "Path[" << i << "] = " << v[i].full_path_name() << "\n";
      }
#endif

      value->SetPathVector(v);

      return true;


    } else if (ty.id == VALUE_TYPE_TOKEN_LIST_OP) {
      ListOp<std::string> lst;

      if (!ReadTokenListOp(&lst)) {
        PUSH_ERROR("Failed to read TokenListOp data");
        return false;
      }

      value->SetTokenListOp(lst);
      return true;
    } else {
      // TODO(syoyo)
      PUSH_ERROR("TODO: " + GetValueTypeRepr(rep.GetType()));
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cerr << "[" << __LINE__
                << "] TODO: " << GetValueTypeRepr(rep.GetType()) << "\n";
#endif
      return false;
    }
  }

  // Never should reach here.
}

bool Parser::BuildDecompressedPathsImpl(
    std::vector<uint32_t> const &pathIndexes,
    std::vector<int32_t> const &elementTokenIndexes,
    std::vector<int32_t> const &jumps, size_t curIndex, Path parentPath) {
  bool hasChild = false, hasSibling = false;
  do {
    auto thisIndex = curIndex++;
    if (parentPath.IsEmpty()) {
      // root node.
      // Assume single root node in the scene.
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "paths[" << pathIndexes[thisIndex]
                << "] is parent. name = " << parentPath.full_path_name()
                << "\n";
#endif
      parentPath = Path::AbsoluteRootPath();
      _paths[pathIndexes[thisIndex]] = parentPath;
    } else {
      int32_t tokenIndex = elementTokenIndexes[thisIndex];
      bool isPrimPropertyPath = tokenIndex < 0;
      tokenIndex = std::abs(tokenIndex);

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "tokenIndex = " << tokenIndex << "\n";
#endif
      if (tokenIndex >= int32_t(_tokens.size())) {
        _err += "Invalid tokenIndex in BuildDecompressedPathsImpl.\n";
        return false;
      }
      auto const &elemToken = _tokens[size_t(tokenIndex)];
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "elemToken = " << elemToken << "\n";
      std::cout << "[" << pathIndexes[thisIndex] << "].append = " << elemToken
                << "\n";
#endif

      // full path
      _paths[pathIndexes[thisIndex]] =
          isPrimPropertyPath ? parentPath.AppendProperty(elemToken)
                             : parentPath.AppendElement(elemToken);

      // also set local path for 'primChildren' check
      _paths[pathIndexes[thisIndex]].SetLocalPath(elemToken);
    }

    // If we have either a child or a sibling but not both, then just
    // continue to the neighbor.  If we have both then spawn a task for the
    // sibling and do the child ourself.  We think that our path trees tend
    // to be broader more often than deep.

    hasChild = (jumps[thisIndex] > 0) || (jumps[thisIndex] == -1);
    hasSibling = (jumps[thisIndex] >= 0);

    if (hasChild) {
      if (hasSibling) {
        // NOTE(syoyo): This recursive call can be parallelized
        auto siblingIndex = thisIndex + size_t(jumps[thisIndex]);
        if (!BuildDecompressedPathsImpl(pathIndexes, elementTokenIndexes,
                                         jumps, siblingIndex, parentPath)) {
          return false;
        }
      }
      // Have a child (may have also had a sibling). Reset parent path.
      parentPath = _paths[pathIndexes[thisIndex]];
    }
    // If we had only a sibling, we just continue since the parent path is
    // unchanged and the next thing in the reader stream is the sibling's
    // header.
  } while (hasChild || hasSibling);

  return true;
}

// TODO(syoyo): Refactor
bool Parser::BuildNodeHierarchy(
    std::vector<uint32_t> const &pathIndexes,
    std::vector<int32_t> const &elementTokenIndexes,
    std::vector<int32_t> const &jumps, size_t curIndex,
    int64_t parentNodeIndex) {
  bool hasChild = false, hasSibling = false;

  // NOTE: Need to indirectly lookup index through pathIndexes[] when accessing
  // `_nodes`
  do {
    auto thisIndex = curIndex++;
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "thisIndex = " << thisIndex << ", curIndex = " << curIndex
              << "\n";
#endif
    if (parentNodeIndex == -1) {
      // root node.
      // Assume single root node in the scene.
      assert(thisIndex == 0);

      Node root(parentNodeIndex, _paths[pathIndexes[thisIndex]]);

      _nodes[pathIndexes[thisIndex]] = root;

      parentNodeIndex = int64_t(thisIndex);

    } else {
      if (parentNodeIndex >= int64_t(_nodes.size())) {
        return false;
      }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "Hierarhy. parent[" << pathIndexes[size_t(parentNodeIndex)]
                << "].add_child = " << pathIndexes[thisIndex] << "\n";
#endif

      Node node(parentNodeIndex, _paths[pathIndexes[thisIndex]]);

      assert(_nodes[size_t(pathIndexes[thisIndex])].GetParent() == -2);

      _nodes[size_t(pathIndexes[thisIndex])] = node;

      std::string name = _paths[pathIndexes[thisIndex]].local_path_name();
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "childName = " << name << "\n";
#endif
      _nodes[size_t(pathIndexes[size_t(parentNodeIndex)])].AddChildren(
          name, pathIndexes[thisIndex]);
    }

    hasChild = (jumps[thisIndex] > 0) || (jumps[thisIndex] == -1);
    hasSibling = (jumps[thisIndex] >= 0);

    if (hasChild) {
      if (hasSibling) {
        auto siblingIndex = thisIndex + size_t(jumps[thisIndex]);
        if (!BuildNodeHierarchy(pathIndexes, elementTokenIndexes, jumps,
                                 siblingIndex, parentNodeIndex)) {
          return false;
        }
      }
      // Have a child (may have also had a sibling). Reset parent node index
      parentNodeIndex = int64_t(thisIndex);
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "parentNodeIndex = " << parentNodeIndex << "\n";
#endif
    }
    // If we had only a sibling, we just continue since the parent path is
    // unchanged and the next thing in the reader stream is the sibling's
    // header.
  } while (hasChild || hasSibling);

  return true;
}

bool Parser::ReadCompressedPaths(const uint64_t ref_num_paths) {
  std::vector<uint32_t> pathIndexes;
  std::vector<int32_t> elementTokenIndexes;
  std::vector<int32_t> jumps;

  // Read number of encoded paths.
  uint64_t numPaths;
  if (!_sr->read8(&numPaths)) {
    _err += "Failed to read the number of paths.\n";
    return false;
  }

  if (ref_num_paths != numPaths) {
    _err += "Size mismatch of numPaths at `PATHS` section.\n";
    return false;
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "numPaths : " << numPaths << "\n";
#endif

  pathIndexes.resize(static_cast<size_t>(numPaths));
  elementTokenIndexes.resize(static_cast<size_t>(numPaths));
  jumps.resize(static_cast<size_t>(numPaths));

  // Create temporary space for decompressing.
  std::vector<char> compBuffer(Usd_IntegerCompression::GetCompressedBufferSize(
      static_cast<size_t>(numPaths)));
  std::vector<char> workingSpace(
      Usd_IntegerCompression::GetDecompressionWorkingSpaceSize(
          static_cast<size_t>(numPaths)));

  // pathIndexes.
  {
    uint64_t pathIndexesSize;
    if (!_sr->read8(&pathIndexesSize)) {
      _err += "Failed to read pathIndexesSize.\n";
      return false;
    }

    if (pathIndexesSize !=
        _sr->read(size_t(pathIndexesSize), size_t(pathIndexesSize),
                  reinterpret_cast<uint8_t *>(compBuffer.data()))) {
      _err += "Failed to read pathIndexes data.\n";
      return false;
    }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "comBuffer.size = " << compBuffer.size() << "\n";
    std::cout << "pathIndexesSize = " << pathIndexesSize << "\n";
#endif

    std::string err;
    Usd_IntegerCompression::DecompressFromBuffer(
        compBuffer.data(), size_t(pathIndexesSize), pathIndexes.data(),
        size_t(numPaths), &err, workingSpace.data());
    if (!err.empty()) {
      _err += "Failed to decode pathIndexes\n" + err;
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
        _sr->read(size_t(elementTokenIndexesSize),
                  size_t(elementTokenIndexesSize),
                  reinterpret_cast<uint8_t *>(compBuffer.data()))) {
      _err += "Failed to read elementTokenIndexes data.\n";
      return false;
    }

    std::string err;
    Usd_IntegerCompression::DecompressFromBuffer(
        compBuffer.data(), size_t(elementTokenIndexesSize),
        elementTokenIndexes.data(), size_t(numPaths), &err,
        workingSpace.data());

    if (!err.empty()) {
      _err += "Failed to decode elementTokenIndexes\n" + err;
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
        _sr->read(size_t(jumpsSize), size_t(jumpsSize),
                  reinterpret_cast<uint8_t *>(compBuffer.data()))) {
      _err += "Failed to read jumps data.\n";
      return false;
    }

    std::string err;
    Usd_IntegerCompression::DecompressFromBuffer(
        compBuffer.data(), size_t(jumpsSize), jumps.data(), size_t(numPaths),
        &err, workingSpace.data());

    if (!err.empty()) {
      _err += "Failed to decode jumps\n" + err;
      return false;
    }
  }

  _paths.resize(static_cast<size_t>(numPaths));

  _nodes.resize(static_cast<size_t>(numPaths));

  // Now build the paths.
  if (!BuildDecompressedPathsImpl(pathIndexes, elementTokenIndexes, jumps,
                                   /* curIndex */ 0, Path())) {
    return false;
  }

  // Now build node hierarchy.
  if (!BuildNodeHierarchy(pathIndexes, elementTokenIndexes, jumps,
                           /* curIndex */ 0, /* parent node index */ -1)) {
    return false;
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  for (size_t i = 0; i < pathIndexes.size(); i++) {
    std::cout << "pathIndexes[" << i << "] = " << pathIndexes[i] << "\n";
  }

  for (auto item : elementTokenIndexes) {
    std::cout << "elementTokenIndexes " << item << "\n";
  }

  for (auto item : jumps) {
    std::cout << "jumps " << item << "\n";
  }
#endif

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

  const Section &sec = _toc.sections[size_t(_tokens_index)];
  if (!_sr->seek_set(uint64_t(sec.start))) {
    _err += "Failed to move to `TOKENS` section.\n";
    return false;
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "sec.start = " << sec.start << "\n";
#endif

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

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "# of tokens = " << n
            << ", uncompressedSize = " << uncompressedSize
            << ", compressedSize = " << compressedSize << "\n";
#endif

  std::vector<char> chars(static_cast<size_t>(uncompressedSize));
  std::vector<char> compressed(static_cast<size_t>(compressedSize));

  if (compressedSize !=
      _sr->read(size_t(compressedSize), size_t(compressedSize),
                reinterpret_cast<uint8_t *>(compressed.data()))) {
    _err += "Failed to read compressed data at `TOKENS` section.\n";
    return false;
  }

  if (uncompressedSize !=
      LZ4Compression::DecompressFromBuffer(compressed.data(), chars.data(),
                                           size_t(compressedSize),
                                           size_t(uncompressedSize), &_err)) {
    _err += "Failed to decompress data of Tokens.\n";
    return false;
  }

  // Split null terminated string into _tokens.
  const char *ps = chars.data();
  const char *pe = chars.data() + chars.size();
  const char *p = ps;
  size_t n_remain = size_t(n);

  auto my_strnlen = [](const char *s, const size_t max_length) -> size_t {
    if (!s) return 0;

    size_t i = 0;
    for (; i < max_length; i++) {
      if (s[i] == '\0') {
        return i;
      }
    }

    // null character not found.
    return i;
  };

  // TODO(syoyo): Check if input string has exactly `n` tokens(`n` null
  // characters)
  for (size_t i = 0; i < n; i++) {
    size_t len = my_strnlen(p, n_remain);

    if ((p + len) > pe) {
      _err += "Invalid token string array.\n";
      return false;
    }

    std::string token;
    if (len > 0) {
      token = std::string(p, len);
    }

    p += len + 1;  // +1 = '\0'
    n_remain = size_t(pe - p);
    assert(p <= pe);
    if (p > pe) {
      _err += "Invalid token string array.\n";
      return false;
    }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "token[" << i << "] = " << token << "\n";
#endif
    _tokens.push_back(token);
  }

  return true;
}

bool Parser::ReadStrings() {
  if ((_strings_index < 0) ||
      (_strings_index >= int64_t(_toc.sections.size()))) {
    _err += "Invalid index for `STRINGS` section.\n";
    return false;
  }

  const Section &s = _toc.sections[size_t(_strings_index)];

  if (!_sr->seek_set(uint64_t(s.start))) {
    _err += "Failed to move to `STRINGS` section.\n";
    return false;
  }

  if (!ReadIndices(_sr, &_string_indices)) {
    _err += "Failed to read StringIndex array.\n";
    return false;
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  for (size_t i = 0; i < _string_indices.size(); i++) {
    std::cout << "StringIndex[" << i << "] = " << _string_indices[i].value
              << "\n";
  }
#endif

  return true;
}

bool Parser::ReadFields() {
  if ((_fields_index < 0) || (_fields_index >= int64_t(_toc.sections.size()))) {
    _err += "Invalid index for `FIELDS` section.\n";
    return false;
  }

  if ((_version[0] == 0) && (_version[1] < 4)) {
    _err += "Version must be 0.4.0 or later, but got " +
            std::to_string(_version[0]) + "." + std::to_string(_version[1]) +
            "." + std::to_string(_version[2]) + "\n";
    return false;
  }

  const Section &s = _toc.sections[size_t(_fields_index)];

  if (!_sr->seek_set(uint64_t(s.start))) {
    _err += "Failed to move to `FIELDS` section.\n";
    return false;
  }

  uint64_t num_fields;
  if (!_sr->read8(&num_fields)) {
    _err += "Failed to read # of fields at `FIELDS` section.\n";
    return false;
  }

  _fields.resize(static_cast<size_t>(num_fields));

  // indices
  {
    std::vector<char> comp_buffer(
        Usd_IntegerCompression::GetCompressedBufferSize(
            static_cast<size_t>(num_fields)));
    // temp buffer for decompress
    std::vector<uint32_t> tmp;
    tmp.resize(static_cast<size_t>(num_fields));

    uint64_t fields_size;
    if (!_sr->read8(&fields_size)) {
      _err += "Failed to read field legnth at `FIELDS` section.\n";
      return false;
    }

    if (fields_size !=
        _sr->read(size_t(fields_size), size_t(fields_size),
                  reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
      _err += "Failed to read field data at `FIELDS` section.\n";
      return false;
    }

    std::string err;
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "fields_size = " << fields_size
              << ", tmp.size = " << tmp.size()
              << ", num_fields = " << num_fields << "\n";
#endif
    Usd_IntegerCompression::DecompressFromBuffer(
        comp_buffer.data(), size_t(fields_size), tmp.data(), size_t(num_fields),
        &err);

    if (!err.empty()) {
      _err += err;
      return false;
    }

    for (size_t i = 0; i < num_fields; i++) {
      _fields[i].token_index.value = tmp[i];
    }
  }

  // Value reps
  {
    uint64_t reps_size;
    if (!_sr->read8(&reps_size)) {
      _err += "Failed to read reps legnth at `FIELDS` section.\n";
      return false;
    }

    std::vector<char> comp_buffer(static_cast<size_t>(reps_size));

    if (reps_size !=
        _sr->read(size_t(reps_size), size_t(reps_size),
                  reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
      _err += "Failed to read reps data at `FIELDS` section.\n";
      return false;
    }

    // reps datasize = LZ4 compressed. uncompressed size = num_fields * 8 bytes
    std::vector<uint64_t> reps_data;
    reps_data.resize(static_cast<size_t>(num_fields));

    size_t uncompressed_size = size_t(num_fields) * sizeof(uint64_t);

    if (uncompressed_size != LZ4Compression::DecompressFromBuffer(
                                 comp_buffer.data(),
                                 reinterpret_cast<char *>(reps_data.data()),
                                 size_t(reps_size), uncompressed_size, &_err)) {
      return false;
    }

    for (size_t i = 0; i < num_fields; i++) {
      _fields[i].value_rep = ValueRep(reps_data[i]);
    }
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "num_fields = " << num_fields << "\n";
  for (size_t i = 0; i < num_fields; i++) {
    std::cout << "field[" << i
              << "] name = " << GetToken(_fields[i].token_index)
              << ", value = " << _fields[i].value_rep.GetStringRepr() << "\n";
  }
#endif

  return true;
}

bool Parser::ReadFieldSets() {
  if ((_fieldsets_index < 0) ||
      (_fieldsets_index >= int64_t(_toc.sections.size()))) {
    _err += "Invalid index for `FIELDSETS` section.\n";
    return false;
  }

  if ((_version[0] == 0) && (_version[1] < 4)) {
    _err += "Version must be 0.4.0 or later, but got " +
            std::to_string(_version[0]) + "." + std::to_string(_version[1]) +
            "." + std::to_string(_version[2]) + "\n";
    return false;
  }

  const Section &s = _toc.sections[size_t(_fieldsets_index)];

  if (!_sr->seek_set(uint64_t(s.start))) {
    _err += "Failed to move to `FIELDSETS` section.\n";
    return false;
  }

  uint64_t num_fieldsets;
  if (!_sr->read8(&num_fieldsets)) {
    _err += "Failed to read # of fieldsets at `FIELDSETS` section.\n";
    return false;
  }

  _fieldset_indices.resize(static_cast<size_t>(num_fieldsets));

  // Create temporary space for decompressing.
  std::vector<char> comp_buffer(Usd_IntegerCompression::GetCompressedBufferSize(
      static_cast<size_t>(num_fieldsets)));

  std::vector<uint32_t> tmp(static_cast<size_t>(num_fieldsets));
  std::vector<char> working_space(
      Usd_IntegerCompression::GetDecompressionWorkingSpaceSize(
          static_cast<size_t>(num_fieldsets)));

  uint64_t fsets_size;
  if (!_sr->read8(&fsets_size)) {
    _err += "Failed to read fieldsets size at `FIELDSETS` section.\n";
    return false;
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "num_fieldsets = " << num_fieldsets
            << ", fsets_size = " << fsets_size
            << ", comp_buffer.size = " << comp_buffer.size() << "\n";
#endif

  assert(fsets_size < comp_buffer.size());

  if (fsets_size !=
      _sr->read(size_t(fsets_size), size_t(fsets_size),
                reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
    _err += "Failed to read fieldsets data at `FIELDSETS` section.\n";
    return false;
  }

  std::string err;
  Usd_IntegerCompression::DecompressFromBuffer(
      comp_buffer.data(), size_t(fsets_size), tmp.data(), size_t(num_fieldsets),
      &err, working_space.data());

  if (!err.empty()) {
    _err += err;
    return false;
  }

  for (size_t i = 0; i != num_fieldsets; ++i) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "fieldset_index[" << i << "] = " << tmp[i] << "\n";
#endif
    _fieldset_indices[i].value = tmp[i];
  }

  return true;
}

bool Parser::BuildLiveFieldSets() {
  for (auto fsBegin = _fieldset_indices.begin(),
            fsEnd = std::find(fsBegin, _fieldset_indices.end(), Index());
       fsBegin != _fieldset_indices.end(); fsBegin = fsEnd + 1,
            fsEnd = std::find(fsBegin, _fieldset_indices.end(), Index())) {
    auto &pairs =
        _live_fieldsets[Index(uint32_t(fsBegin - _fieldset_indices.begin()))];

    pairs.resize(size_t(fsEnd - fsBegin));
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "range size = " << (fsEnd - fsBegin) << "\n";
#endif
    // TODO(syoyo): Parallelize.
    for (size_t i = 0; fsBegin != fsEnd; ++fsBegin, ++i) {
      assert((fsBegin->value >= 0) && (fsBegin->value < _fields.size()));
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "fieldIndex = " << (fsBegin->value) << "\n";
#endif
      auto const &field = _fields[fsBegin->value];
      pairs[i].first = GetToken(field.token_index);
      if (!UnpackValueRep(field.value_rep, &pairs[i].second)) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cerr << "BuildLiveFieldSets: Failed to unpack ValueRep : "
                  << field.value_rep.GetStringRepr() << "\n";
#endif
        return false;
      }
    }
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "# of live fieldsets = " << _live_fieldsets.size() << "\n";
#endif

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  size_t sum = 0;
  for (const auto &item : _live_fieldsets) {
    std::cout << "livefieldsets[" << item.first.value
              << "].count = " << item.second.size() << "\n";
    sum += item.second.size();

    for (size_t i = 0; i < item.second.size(); i++) {
      std::cout << " [" << i << "] name = " << item.second[i].first << "\n";
    }
  }
  std::cout << "Total fields used = " << sum << "\n";
#endif

  return true;
}

bool Parser::ParseAttribute(const FieldValuePairVector &fvs, PrimAttrib *attr,
                             const std::string &prop_name) {
  bool success = false;

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "fvs.size = " << fvs.size() << "\n";
#endif

  bool has_connection{false};

  Variability variability{Variability::Varying};
  Interpolation interpolation{Interpolation::Invalid};

  //
  // Parse properties
  //
  for (const auto &fv : fvs) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "===  fvs.first " << fv.first
              << ", second: " << fv.second.GetTypeName() << "\n";
#endif
    if ((fv.first == "typeName") && (fv.second.GetTypeName() == "Token")) {
      attr->type_name = fv.second.GetToken();
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "aaa: typeName: " << attr->type_name << "\n";
#endif
    } else if (fv.first == "targetPaths") {
      // e.g. connection to Material.
      const ListOp<Path> paths = fv.second.GetPathListOp();

      #ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      paths.Print();
#endif
      // Currently we only support single explicit path.
      if ((paths.GetExplicitItems().size() == 1)) {
        const Path &path = paths.GetExplicitItems()[0];
        (void)path;

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cout << "full path: " << path.full_path_name() << "\n";
        std::cout << "local path: " << path.local_path_name() << "\n";
#endif

        attr->var.set_scalar(path.full_path_name());  // TODO: store `Path`

        has_connection = true;

      } else {
        return false;
      }
    } else if (fv.first == "connectionPaths") {
      // e.g. connection to texture file.
      const ListOp<Path> paths = fv.second.GetPathListOp();

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      paths.Print();
#endif

      // Currently we only support single explicit path.
      if ((paths.GetExplicitItems().size() == 1)) {
        const Path &path = paths.GetExplicitItems()[0];
        (void)path;

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cout << "full path: " << path.full_path_name() << "\n";
        std::cout << "local path: " << path.local_path_name() << "\n";
#endif

        attr->var.set_scalar(path.full_path_name()); // TODO: store `Path`

        has_connection = true;

      } else {
        return false;
      }
    } else if ((fv.first == "variablity") &&
               (fv.second.GetTypeName() == "Variability")) {
      variability = fv.second.GetVariability();
    } else if ((fv.first == "interpolation") &&
               (fv.second.GetTypeName() == "Token")) {
      interpolation = InterpolationFromString(fv.second.GetToken());
    }
  }

  attr->variability = variability;

  //
  // Decode value(stored in "default" field)
  //
  for (const auto &fv : fvs) {
    if (fv.first == "default") {
      attr->name = prop_name;

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "fv.second.GetTypeName = " << fv.second.GetTypeName()
                << "\n";
#endif

      if (fv.second.GetTypeName() == "Float") {
        float value;

        if (!fv.second.GetFloat(&value)) {
          _err += "Failed to decode Float value.";
          return false;
        }
        attr->var.set_scalar(value);
        success = true;

      } else if (fv.second.GetTypeName() == "Bool") {
        bool boolVal;
        if (!fv.second.GetBool(&boolVal)) {
          _err += "Failed to decode Int data";
          return false;
        }

        attr->var.set_scalar(boolVal);
        success = true;

      } else if (fv.second.GetTypeName() == "Int") {
        int value;
        if (!fv.second.GetInt(&value)) {
          _err += "Failed to decode Int data";
          return false;
        }

        attr->var.set_scalar(value);
        success = true;
      } else if (fv.second.GetTypeName() == "Vec3f") {
        Vec3f value = *reinterpret_cast<const Vec3f*>(fv.second.GetData().data());
        (void)value;
        attr->var.set_scalar(value);

        attr->variability = variability;
        attr->interpolation = interpolation;
        success = true;

      } else if (fv.second.GetTypeName() == "FloatArray") {
        std::vector<float> value;
        value.resize(fv.second.GetData().size() / sizeof(float));
        memcpy(value.data(), fv.second.GetData().data(), fv.second.GetData().size());
        attr->var.set_scalar(value);

        attr->variability = variability;
        attr->interpolation = interpolation;
        success = true;
      } else if (fv.second.GetTypeName() == "Vec2fArray") {
        std::vector<Vec2f> value;
        value.resize(fv.second.GetData().size() / sizeof(Vec2f));
        memcpy(value.data(), fv.second.GetData().data(), fv.second.GetData().size());
        attr->var.set_scalar(value);

        attr->variability = variability;
        attr->interpolation = interpolation;
        success = true;
      } else if (fv.second.GetTypeName() == "Vec3fArray") {


        // role-type?
        if (attr->type_name == "point3f[]") {
          std::vector<primvar::point3f> value;
          value.resize(fv.second.GetData().size() / sizeof(Vec3f));
          memcpy(value.data(), fv.second.GetData().data(),
                 fv.second.GetData().size());
          attr->var.set_scalar(value);
        } else if (attr->type_name == "normal3f[]") {
          std::vector<primvar::normal3f> value;
          value.resize(fv.second.GetData().size() / sizeof(Vec3f));
          memcpy(value.data(), fv.second.GetData().data(),
                 fv.second.GetData().size());
          attr->var.set_scalar(value);
        } else {
          std::vector<Vec3f> value;
          value.resize(fv.second.GetData().size() / sizeof(Vec3f));
          memcpy(value.data(), fv.second.GetData().data(),
                 fv.second.GetData().size());
          attr->var.set_scalar(value);
        }
        attr->variability = variability;
        attr->interpolation = interpolation;
        success = true;
      } else if (fv.second.GetTypeName() == "Vec4fArray") {

        std::vector<Vec4f> value;
        value.resize(fv.second.GetData().size() / sizeof(Vec4f));
        memcpy(value.data(), fv.second.GetData().data(), fv.second.GetData().size());

        attr->var.set_scalar(value);
        attr->variability = variability;
        attr->interpolation = interpolation;
        success = true;
      } else if (fv.second.GetTypeName() == "IntArray") {

        std::vector<int> value;
        value.resize(fv.second.GetData().size() / sizeof(int));
        memcpy(value.data(), fv.second.GetData().data(), fv.second.GetData().size());

        attr->var.set_scalar(value);
        attr->variability = variability;
        attr->interpolation = interpolation;
        success = true;
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cout << "IntArray"
                  << "\n";

        //const int32_t *ptr =
        //    reinterpret_cast<const int32_t *>(attr->buffer.data.data());
        //for (size_t i = 0; i < attr->buffer.GetNumElements(); i++) {
        //  std::cout << "[" << i << "] = " << ptr[i] << "\n";
        //}
#endif
      } else if (fv.second.GetTypeName() == "Token") {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cout << "bbb: token: " << fv.second.GetToken() << "\n";
#endif

        attr->var.set_scalar(fv.second.GetToken());
        attr->variability = variability;
        //attr->interpolation = interpolation;
        success = true;
      } else if (fv.second.GetTypeName() == "TokenArray") {


        std::vector<std::string> value = fv.second.GetTokenArray();

        attr->var.set_scalar(value);
        attr->variability = variability;
        attr->interpolation = interpolation;
        success = true;

      } else {
        PUSH_ERROR("TODO: " + fv.second.GetTypeName());
      }
    }
  }

  if (!success && has_connection) {
    // Attribute has a connection(has a path and no `default` field)
    success = true;
  }

  return success;
}

bool Parser::ReconstructXform(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    Xform *xform) {

  // TODO:
  //  * [ ] !invert! suffix
  //  * [ ] !resetXformStack! suffix
  //  * [ ] maya:pivot support?

  (void)xform;

  for (const auto &fv : fields) {
    if (fv.first == "properties") {
      if (fv.second.GetTypeName() != "TokenArray") {
        _err += "`properties` attribute must be TokenArray type\n";
        return false;
      }
    }
  }

  //
  // NOTE: Currently we assume one deeper node has Xform's attribute
  //
  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      _err += "Invalid child node id: " + std::to_string(child_index) +
              ". Must be in range [0, " + std::to_string(_nodes.size()) + ")\n";
      return false;
    }

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      // Should we report an error?
      continue;
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      _err += "Invalid specifier id: " + std::to_string(spec_index) +
              ". Must be in range [0, " + std::to_string(_specs.size()) + ")\n";
      return false;
    }

    const Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);

    DCOUT("Path prim part: " << path.GetPrimPart()
              << ", prop part: " << path.GetPropPart()
              << ", spec_index = " << spec_index);

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      _err += "FieldSet id: " + std::to_string(spec.fieldset_index.value) +
              " must exist in live fieldsets.\n";
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;
      bool ret = ParseAttribute(child_fields, &attr, prop_name);
      DCOUT("Xform: prop: " << prop_name << ", ret = " << ret);
      if (ret) {
        // TODO(syoyo): Implement
        PUSH_ERROR("TODO: Implemen Xform prop: " + prop_name);
      }
    }
  }

  return true;
}

bool Parser::ReconstructGeomBasisCurves(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    GeomBasisCurves *curves) {

  bool has_position{false};

  for (const auto &fv : fields) {
    if (fv.first == "properties") {
      if (fv.second.GetTypeName() != "TokenArray") {
        _err += "`properties` attribute must be TokenArray type\n";
        return false;
      }
      assert(fv.second.IsArray());
      for (size_t i = 0; i < fv.second.GetStringArray().size(); i++) {
        if (fv.second.GetStringArray()[i] == "points") {
          has_position = true;
        }
      }
    }
  }

  (void)has_position;

  //
  // NOTE: Currently we assume one deeper node has GeomMesh's attribute
  //
  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      _err += "Invalid child node id: " + std::to_string(child_index) +
              ". Must be in range [0, " + std::to_string(_nodes.size()) + ")\n";
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      // Should we report an error?
#if 0
      _err += "GeomBasisCurves: No specifier found for node id: " + std::to_string(child_index) +
              "\n";
      return false;
#else
      continue;
#endif
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      _err += "Invalid specifier id: " + std::to_string(spec_index) +
              ". Must be in range [0, " + std::to_string(_specs.size()) + ")\n";
      return false;
    }

    const Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "Path prim part: " << path.GetPrimPart()
              << ", prop part: " << path.GetPropPart()
              << ", spec_index = " << spec_index << "\n";
#endif

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      _err += "FieldSet id: " + std::to_string(spec.fieldset_index.value) +
              " must exist in live fieldsets.\n";
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;
      bool ret = ParseAttribute(child_fields, &attr, prop_name);
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "prop: " << prop_name << ", ret = " << ret << "\n";
#endif
      if (ret) {
        // TODO(syoyo): Support more prop names
        if (prop_name == "points") {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cout << "got point\n";
#endif
          //if (auto p = primvar::as_vector<Vec3f>(&attr.var)) {
          //  curves->points = *p;
          //}
        } else if (prop_name == "extent") {
          // vec3f[2]
          //if (auto p = primvar::as_vector<Vec3f>(&attr.var)) {
          //  if (p->size() == 2) {
          //    curves->extent.value.lower = (*p)[0];
          //    curves->extent.value.upper = (*p)[1];
          //  }
          //}
        } else if (prop_name == "normals") {
          //if (auto p = primvar::as_vector<Vec3f>(&attr.var)) {
          //  curves->normals = (*p);
          //}
        } else if (prop_name == "widths") {
          //if (auto p = primvar::as_vector<float>(&attr.var)) {
          //  curves->widths = (*p);
          //}
        } else if (prop_name == "curveVertexCounts") {
          //if (auto p = primvar::as_vector<int>(&attr.var)) {
          //  curves->curveVertexCounts = (*p);
          //}
        } else if (prop_name == "type") {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          //std::cout << "type:" << attr.stringVal << "\n";
#endif
          //if (auto p = primvar::as_basic<std::string>(&attr.var)) {
          //  if (p->compare("cubic") == 0) {
          //    curves->type = "cubic";
          //  } else if (p->compare("linear") == 0) {
          //    curves->type = "linear";
          //  } else {
          //    _err += "Unknown type: " + (*p) + "\n";
          //    return false;
          //  }
          //}
        } else if (prop_name == "basis") {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          //std::cout << "basis:" << attr.stringVal << "\n";
#endif
#if 0
          if (auto p = nonstd::get_if<std::string>(&attr.var)) {
            if (p->compare("bezier") == 0) {
              curves->type = "bezier";
            } else if (p->compare("catmullRom") == 0) {
              curves->type = "catmullRom";
            } else if (p->compare("bspline") == 0) {
              curves->type = "bspline";
            } else if (p->compare("hermite") == 0) {
              _err += "`hermite` basis for BasisCurves is not supported in TinyUSDZ\n";
              return false;
            } else if (p->compare("power") == 0) {
              _err += "`power` basis for BasisCurves is not supported in TinyUSDZ\n";
              return false;
            } else {
              _err += "Unknown basis: " + (*p) + "\n";
              return false;
            }
          }
#endif
        } else if (prop_name == "wrap") {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          //std::cout << "wrap:" << attr.stringVal << "\n";
#endif
#if 0
          if (auto p = nonstd::get_if<std::string>(&attr.var)) {
            if (p->compare("nonperiodic") == 0) {
              curves->type = "nonperiodic";
            } else if (p->compare("periodic") == 0) {
              curves->type = "periodic";
            } else if (p->compare("pinned") == 0) {
              curves->type = "pinned";
            } else {
              _err += "Unknown wrap: " + (*p) + "\n";
              return false;
            }
          }
#endif
        } else {
          // Assume Primvar.
          if (curves->attribs.count(prop_name)) {
            _err += "Duplicated property name found: " + prop_name + "\n";
            return false;
          }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cout << "add [" << prop_name << "] to generic attrs\n";
#endif

          curves->attribs[prop_name] = std::move(attr);
        }
      }
    }
  }

  return true;
}

bool Parser::ReconstructGeomSubset(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    GeomSubset *geom_subset) {

  for (const auto &fv : fields) {
    if (fv.first == "properties") {
      if (fv.second.GetTypeName() != "TokenArray") {
        _err += "`properties` attribute must be TokenArray type\n";
        return false;
      }
      assert(fv.second.IsArray());
      for (size_t i = 0; i < fv.second.GetStringArray().size(); i++) {
        //if (fv.second.GetStringArray()[i] == "points") {
        //}
      }
    }
  }

  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      _err += "Invalid child node id: " + std::to_string(child_index) +
              ". Must be in range [0, " + std::to_string(_nodes.size()) + ")\n";
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      // TODO: Should we report an error?
      continue;
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      _err += "Invalid specifier id: " + std::to_string(spec_index) +
              ". Must be in range [0, " + std::to_string(_specs.size()) + ")\n";
      return false;
    }

    const Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "Path prim part: " << path.GetPrimPart()
              << ", prop part: " << path.GetPropPart()
              << ", spec_index = " << spec_index << "\n";
#endif

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      _err += "FieldSet id: " + std::to_string(spec.fieldset_index.value) +
              " must exist in live fieldsets.\n";
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;
      bool ret = ParseAttribute(child_fields, &attr, prop_name);
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "prop: " << prop_name << ", ret = " << ret << "\n";
#endif
      std::cout << "prop: " << prop_name << ", ret = " << ret << "\n";

      if (ret) {
        // TODO(syoyo): Support more prop names
        if (prop_name == "elementType") {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cout << "got elementType\n";
#endif
          auto p = attr.var.get_value<tinyusdz::primvar::token>();
          if (p) {
            std::string str = p->str();
            if (str == "face") {
              geom_subset->elementType = GeomSubset::ElementType::Face;
            } else {
              _err += "`elementType` must be `face`, but got `" + str + "`";
              return false;
            }
          } else {
            _err += "`elementType` must be token type, but got " +
                    primvar::GetTypeName(attr.var.type_id());
            return false;
          }
        } else if (prop_name == "faces") {
          auto p = attr.var.get_value<std::vector<int>>();
          if (p) {
            geom_subset->faces = (*p);
          }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
            // aaa: typeName: int[]
            std::cout << "got faces\n";
            std::cout << "  num = " << geom_subset->faces.size() << "\n";
#endif
          //}

        } else {
          // Assume Primvar.
          if (geom_subset->attribs.count(prop_name)) {
            _err += "Duplicated property name found: " + prop_name + "\n";
            return false;
          }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cout << "add [" << prop_name << "] to generic attrs\n";
#endif

          geom_subset->attribs[prop_name] = std::move(attr);
        }
      }
    }
  }

  return true;
}

bool Parser::ReconstructGeomMesh(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    GeomMesh *mesh) {

  bool has_position{false};

  for (const auto &fv : fields) {
    if (fv.first == "properties") {
      if (fv.second.GetTypeName() != "TokenArray") {
        _err += "`properties` attribute must be TokenArray type\n";
        return false;
      }
      assert(fv.second.IsArray());
      for (size_t i = 0; i < fv.second.GetStringArray().size(); i++) {
        if (fv.second.GetStringArray()[i] == "points") {
          has_position = true;
        }
      }
    }
  }

  (void)has_position;

  // Disable has_position check for a while, since Mesh may not have "points",
  // but "position"

  // if (!has_position) {
  //  _err += "No `position` field exist for Mesh node: " + node.GetLocalPath()
  //  +
  //          ".\n";
  //  return false;
  //}

  //
  // NOTE: Currently we assume one deeper node has GeomMesh's attribute
  //
  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      _err += "Invalid child node id: " + std::to_string(child_index) +
              ". Must be in range [0, " + std::to_string(_nodes.size()) + ")\n";
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      // Should we report an error?
#if 0
      _err += "GeomMesh: No specifier found for node id: " + std::to_string(child_index) +
              "\n";
      return false;
#else
      continue;
#endif
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      _err += "Invalid specifier id: " + std::to_string(spec_index) +
              ". Must be in range [0, " + std::to_string(_specs.size()) + ")\n";
      return false;
    }

    const Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
    DCOUT("Path prim part: " << path.GetPrimPart()
              << ", prop part: " << path.GetPropPart()
              << ", spec_index = " << spec_index);

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      _err += "FieldSet id: " + std::to_string(spec.fieldset_index.value) +
              " must exist in live fieldsets.\n";
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;
      bool ret = ParseAttribute(child_fields, &attr, prop_name);
      DCOUT("prop: " << prop_name << ", ret = " << ret);

      if (ret) {
        // TODO(syoyo): Support more prop names
        if (prop_name == "points") {
          auto p = attr.var.get_value<std::vector<primvar::point3f>>();
          if (p) {
            mesh->points = (*p);
          } else {
            _err += "`points` must be point3[] type, but got " +
                    primvar::GetTypeName(attr.var.type_id());
            return false;
          }
          //if (auto p = primvar::as_vector<Vec3f>(&attr.var)) {
          //  mesh->points = (*p);
          //}
        } else if (prop_name == "doubleSided") {
          auto p = attr.var.get_value<bool>();
          if (p) {
            mesh->doubleSided = (*p);
          }
        } else if (prop_name == "extent") {
          // vec3f[2]
          auto p = attr.var.get_value<std::vector<Vec3f>>();
          if (p && p->size() == 2) {
              mesh->extent.value.lower = (*p)[0];
             mesh->extent.value.upper = (*p)[1];
          }
        } else if (prop_name == "normals") {
          mesh->normals = attr;
        } else if ((prop_name == "primvars:UVMap") &&
                   (attr.type_name == "texCoord2f[]")) {
          // Explicit UV coord attribute.
          // TODO(syoyo): Write PrimVar parser

          // Currently we only support vec2f for uv coords.
          //if (auto p = primvar::as_vector<Vec2f>(&attr.var)) {
          //  mesh->st.buffer = (*p);
          //  mesh->st.variability = attr.variability;
          //}
        } else if (prop_name == "faceVertexCounts") {
          auto p = attr.var.get_value<std::vector<int>>();
          if (p) {
            mesh->faceVertexCounts = (*p);
          }
          //}
        } else if (prop_name == "faceVertexIndices") {
          auto p = attr.var.get_value<std::vector<int>>();
          if (p) {
            mesh->faceVertexIndices = (*p);
          }


        } else if (prop_name == "holeIndices") {
          //if (auto p = primvar::as_vector<int>(&attr.var)) {
          //    mesh->holeIndices = (*p);
          //}
        } else if (prop_name == "cornerIndices") {
          //if (auto p = primvar::as_vector<int>(&attr.var)) {
          //    mesh->cornerIndices = (*p);
          //}
        } else if (prop_name == "cornerSharpnesses") {
          //if (auto p = primvar::as_vector<float>(&attr.var)) {
          //    mesh->cornerSharpnesses = (*p);
          //}
        } else if (prop_name == "creaseIndices") {
          //if (auto p = primvar::as_vector<int>(&attr.var)) {
          //    mesh->creaseIndices = (*p);
          //}
        } else if (prop_name == "creaseLengths") {
          //if (auto p = primvar::as_vector<int>(&attr.var)) {
          //  mesh->creaseLengths = (*p);
          //}
        } else if (prop_name == "creaseSharpnesses") {
          //if (auto p = primvar::as_vector<float>(&attr.var)) {
          //    mesh->creaseSharpnesses = (*p);
          //}
        } else if (prop_name == "subdivisionScheme") {
          auto p = attr.var.get_value<primvar::token>();
          //if (auto p = primvar::as_basic<std::string>(&attr.var)) {
          //  if (p->compare("none") == 0) {
          //    mesh->subdivisionScheme = SubdivisionScheme::None;
          //  } else if (p->compare("catmullClark") == 0) {
          //    mesh->subdivisionScheme = SubdivisionScheme::CatmullClark;
          //  } else if (p->compare("bilinear") == 0) {
          //    mesh->subdivisionScheme = SubdivisionScheme::Bilinear;
          //  } else if (p->compare("loop") == 0) {
          //    mesh->subdivisionScheme = SubdivisionScheme::Loop;
          //  } else {
          //    _err += "Unknown subdivision scheme: " + (*p) + "\n";
          //    return false;
          //  }
          //}
        } else if (prop_name.compare("material:binding") == 0) {
          // rel
          auto p = attr.var.get_value<std::string>(); // rel, but treat as sting
          if (p) {
            mesh->materialBinding.materialBinding = (*p);
          }
        } else {
          // Assume Primvar.
          if (mesh->attribs.count(prop_name)) {
            _err += "Duplicated property name found: " + prop_name + "\n";
            return false;
          }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cout << "add [" << prop_name << "] to generic attrs\n";
#endif

          mesh->attribs[prop_name] = std::move(attr);
        }
      }
    }
  }

  return true;
}

bool Parser::ReconstructMaterial(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    Material *material) {

  (void)material;

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "Parse mateiral\n";
#endif

  for (const auto &fv : fields) {
    if (fv.first == "properties") {
      if (fv.second.GetTypeName() != "TokenArray") {
        _err += "`properties` attribute must be TokenArray type\n";
        return false;
      }
      assert(fv.second.IsArray());

      for (size_t i = 0; i < fv.second.GetStringArray().size(); i++) {
      }
    }
  }

  //
  // NOTE: Currently we assume one deeper node has Material's attribute
  //
  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      _err += "Invalid child node id: " + std::to_string(child_index) +
              ". Must be in range [0, " + std::to_string(_nodes.size()) + ")\n";
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
#if 0
      _err += "Material: No specifier found for node id: " + std::to_string(child_index) +
              "\n";
      return false;
#else
      continue;
#endif
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      _err += "Invalid specifier id: " + std::to_string(spec_index) +
              ". Must be in range [0, " + std::to_string(_specs.size()) + ")\n";
      return false;
    }

    const Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "Path prim part: " << path.GetPrimPart()
              << ", prop part: " << path.GetPropPart()
              << ", spec_index = " << spec_index << "\n";
#endif

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      _err += "FieldSet id: " + std::to_string(spec.fieldset_index.value) +
              " must exist in live fieldsets.\n";
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    (void)child_fields;
    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;

      bool ret = ParseAttribute(child_fields, &attr, prop_name);
      if (ret) {
        if (prop_name.compare("outputs:surface") == 0) {
          auto p = attr.var.get_value<std::string>();
          if (p) {
            material->outputs_surface = (*p);
          }
        }
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cout << "prop: " << prop_name << "\n";
#endif
      }
    }
  }

  return true;
}

bool Parser::ReconstructShader(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    Shader *shader) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "Parse shader\n";
#endif

  (void)shader;

  for (const auto &fv : fields) {
    if (fv.first == "properties") {
      if (fv.second.GetTypeName() != "TokenArray") {
        _err += "`properties` attribute must be TokenArray type\n";
        return false;
      }
      assert(fv.second.IsArray());

      for (size_t i = 0; i < fv.second.GetStringArray().size(); i++) {
      }
    }
  }


  //
  // Find shader type.
  //
  std::string shader_type;

  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      _err += "Invalid child node id: " + std::to_string(child_index) +
              ". Must be in range [0, " + std::to_string(_nodes.size()) + ")\n";
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      _err += "No specifier found for node id: " + std::to_string(child_index) +
              "\n";
      return false;
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      _err += "Invalid specifier id: " + std::to_string(spec_index) +
              ". Must be in range [0, " + std::to_string(_specs.size()) + ")\n";
      return false;
    }

    const Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "Path prim part: " << path.GetPrimPart()
              << ", prop part: " << path.GetPropPart()
              << ", spec_index = " << spec_index << "\n";
#endif

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      _err += "FieldSet id: " + std::to_string(spec.fieldset_index.value) +
              " must exist in live fieldsets.\n";
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;

      bool ret = ParseAttribute(child_fields, &attr, prop_name);
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "prop: " << prop_name << ", ret = " << ret << "\n";
#endif

      if (ret) {
        // Currently we only support predefined PBR attributes.

        if (prop_name.compare("info:id") == 0) {
          auto p = attr.var.get_value<std::string>(); // `token` type, but treat it as string
          if (p) {
            shader_type = (*p);
          }
        }
      }
    }
  }

  if (shader_type.empty()) {
    _err += "`info:id` is missing in Shader.\n";
    return false;
  }

  return true;
}

bool Parser::ReconstructPreviewSurface(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    PreviewSurface *shader) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "Parse shader\n";
#endif

  (void)shader;

  for (const auto &fv : fields) {
    if (fv.first == "properties") {
      if (fv.second.GetTypeName() != "TokenArray") {
        _err += "`properties` attribute must be TokenArray type\n";
        return false;
      }
      assert(fv.second.IsArray());

      for (size_t i = 0; i < fv.second.GetStringArray().size(); i++) {
      }
    }
  }

  //
  // NOTE: Currently we assume one deeper node has Shader's attribute
  //
  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      _err += "Invalid child node id: " + std::to_string(child_index) +
              ". Must be in range [0, " + std::to_string(_nodes.size()) + ")\n";
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      _err += "No specifier found for node id: " + std::to_string(child_index) +
              "\n";
      return false;
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      _err += "Invalid specifier id: " + std::to_string(spec_index) +
              ". Must be in range [0, " + std::to_string(_specs.size()) + ")\n";
      return false;
    }

    const Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "Path prim part: " << path.GetPrimPart()
              << ", prop part: " << path.GetPropPart()
              << ", spec_index = " << spec_index << "\n";
#endif

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      _err += "FieldSet id: " + std::to_string(spec.fieldset_index.value) +
              " must exist in live fieldsets.\n";
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;

      bool ret = ParseAttribute(child_fields, &attr, prop_name);
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "prop: " << prop_name << ", ret = " << ret << "\n";
#endif

      if (ret) {
        // Currently we only support predefined PBR attributes.

        if (prop_name.compare("info:id") == 0) {
          auto p = attr.var.get_value<std::string>();  // `token` type, but
                                                       // treat it as string
          if (p) {
            if (p->compare("UsdPreviewSurface") != 0) {
              _err += "`info:id` must be `UsdPreviewSurface`.\n";
              return false;
            }
          }
        } else if (prop_name.compare("outputs:surface") == 0) {
          // Surface shader output available
        } else if (prop_name.compare("outputs:displacement") == 0) {
          // Displacement shader output available
        } else if (prop_name.compare("inputs:roughness") == 0) {
          // type: float
          auto p = attr.var.get_value<float>();
          if (p) {
            shader->roughness.value = (*p);
          }
        } else if (prop_name.compare("inputs:specular") == 0) {
          // type: float
          auto p = attr.var.get_value<float>();
          if (p) {
            shader->specular.value = (*p);
          }
        } else if (prop_name.compare("inputs:ior") == 0) {
          // type: float
          auto p = attr.var.get_value<float>();
          if (p) {
            shader->ior.value = (*p);
          }
        } else if (prop_name.compare("inputs:opacity") == 0) {
          // type: float
          auto p = attr.var.get_value<float>();
          if (p) {
            shader->opacity.value = (*p);
          }
        } else if (prop_name.compare("inputs:clearcoat") == 0) {
          // type: float
          auto p = attr.var.get_value<float>();
          if (p) {
            shader->clearcoat.value = (*p);
          }
        } else if (prop_name.compare("inputs:clearcoatRoughness") == 0) {
          // type: float
          auto p = attr.var.get_value<float>();
          if (p) {
            shader->clearcoatRoughness.value = (*p);
          }
        } else if (prop_name.compare("inputs:metallic") == 0) {
          // type: float
          auto p = attr.var.get_value<float>();
          if (p) {
            shader->metallic.value = (*p);
          }
        } else if (prop_name.compare("inputs:metallic.connect") == 0) {
          // Currently we assume texture is assigned to this attribute.
          auto p = attr.var.get_value<std::string>();
          if (p) {
            shader->metallic.path = *p;
          }
        } else if (prop_name.compare("inputs:diffuseColor") == 0) {
          auto p = attr.var.get_value<primvar::float3>();
          if (p) {
            shader->diffuseColor.color = (*p);

            DCOUT("diffuseColor: " << shader->diffuseColor.color[0]
                      << ", " << shader->diffuseColor.color[1] << ", "
                      << shader->diffuseColor.color[2]);
          }
        } else if (prop_name.compare("inputs:diffuseColor.connect") == 0) {
          // Currently we assume texture is assigned to this attribute.
          auto p = attr.var.get_value<std::string>();
          if (p) {
            shader->diffuseColor.path = *p;
          }
        } else if (prop_name.compare("inputs:emissiveColor") == 0) {
          // if (auto p = primvar::as_basic<Vec3f>(&attr.var)) {
          //  shader->emissiveColor.color = (*p);

          //}
        } else if (prop_name.compare("inputs:emissiveColor.connect") == 0) {
          // Currently we assume texture is assigned to this attribute.
          // if (auto p = primvar::as_basic<std::string>(&attr.var)) {
          //  shader->emissiveColor.path = *p;
          //}
        }
      }
    }
  }

  return true;
}

bool Parser::ReconstructSkelRoot(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    SkelRoot *skelRoot) {
  DCOUT("Parse skelRoot");
  (void)skelRoot;

  for (const auto &fv : fields) {
    if (fv.first == "properties") {
      if (fv.second.GetTypeName() != "TokenArray") {
        _err += "`properties` attribute must be TokenArray type\n";
        return false;
      }
      assert(fv.second.IsArray());

      for (size_t i = 0; i < fv.second.GetStringArray().size(); i++) {
      }
    }
  }


  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      _err += "Invalid child node id: " + std::to_string(child_index) +
              ". Must be in range [0, " + std::to_string(_nodes.size()) + ")\n";
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      _err += "No specifier found for node id: " + std::to_string(child_index) +
              "\n";
      return false;
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      _err += "Invalid specifier id: " + std::to_string(spec_index) +
              ". Must be in range [0, " + std::to_string(_specs.size()) + ")\n";
      return false;
    }

    const Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
    DCOUT("Path prim part: " << path.GetPrimPart()
              << ", prop part: " << path.GetPropPart()
              << ", spec_index = " << spec_index);

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      _err += "FieldSet id: " + std::to_string(spec.fieldset_index.value) +
              " must exist in live fieldsets.\n";
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;

      bool ret = ParseAttribute(child_fields, &attr, prop_name);
      DCOUT("prop:" << prop_name << ", ret = " << ret);

      if (ret) {
        // Currently we only support predefined PBR attributes.

        if (prop_name.compare("info:id") == 0) {
          auto p = attr.var.get_value<std::string>(); // `token` type, but treat it as string
          if (p) {
            //shader_type = (*p);
          }
        }
      }
    }
  }

  return true;
}

bool Parser::ReconstructSkeleton(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    Skeleton *skeleton) {
  DCOUT("Parse skeleton");
  (void)skeleton;

  for (const auto &fv : fields) {
    if (fv.first == "properties") {
      if (fv.second.GetTypeName() != "TokenArray") {
        PUSH_ERROR("`properties` attribute must be TokenArray type");
        return false;
      }
      assert(fv.second.IsArray());

      for (size_t i = 0; i < fv.second.GetStringArray().size(); i++) {
      }
    }
  }


  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      _err += "Invalid child node id: " + std::to_string(child_index) +
              ". Must be in range [0, " + std::to_string(_nodes.size()) + ")\n";
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      _err += "No specifier found for node id: " + std::to_string(child_index) +
              "\n";
      return false;
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      _err += "Invalid specifier id: " + std::to_string(spec_index) +
              ". Must be in range [0, " + std::to_string(_specs.size()) + ")\n";
      return false;
    }

    const Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
    DCOUT("Path prim part: " << path.GetPrimPart()
              << ", prop part: " << path.GetPropPart()
              << ", spec_index = " << spec_index);

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      _err += "FieldSet id: " + std::to_string(spec.fieldset_index.value) +
              " must exist in live fieldsets.\n";
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;

      bool ret = ParseAttribute(child_fields, &attr, prop_name);
      DCOUT("prop:" << prop_name << ", ret = " << ret);

      if (ret) {
        // Currently we only support predefined PBR attributes.

        if (prop_name.compare("info:id") == 0) {
          auto p = attr.var.get_value<std::string>(); // `token` type, but treat it as string
          if (p) {
            //shader_type = (*p);
          }
        }
      }
    }
  }

  return true;
}

bool Parser::ReconstructSceneRecursively(
    int parent, int level,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    Scene *scene) {
  if ((parent < 0) || (parent >= int(_nodes.size()))) {
    _err += "Invalid parent node id: " + std::to_string(parent) +
            ". Must be in range [0, " + std::to_string(_nodes.size()) + ")\n";
    return false;
  }

  const Node &node = _nodes[size_t(parent)];

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  auto IndentStr = [](int l) -> std::string {
    std::string indent;
    for (size_t i = 0; i < size_t(l); i++) {
      indent += "  ";
    }

    return indent;
  };
  std::cout << IndentStr(level) << "lv[" << level << "] node_index[" << parent
            << "] " << node.GetLocalPath() << " ==\n";
  std::cout << IndentStr(level) << " childs = [";
  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    std::cout << node.GetChildren()[i];
    if (i != (node.GetChildren().size() - 1)) {
      std::cout << ", ";
    }
  }
  std::cout << "]\n";
#endif

  if (!path_index_to_spec_index_map.count(uint32_t(parent))) {
    // No specifier assigned to this node.
#if 0
    _err += "Scene: No specifier found for node id: " + std::to_string(parent) + "\n";
    return false;
#else
    return true; // would be OK.
#endif
  }

  uint32_t spec_index = path_index_to_spec_index_map.at(uint32_t(parent));
  if (spec_index >= _specs.size()) {
    _err += "Invalid specifier id: " + std::to_string(spec_index) +
            ". Must be in range [0, " + std::to_string(_specs.size()) + ")\n";
    return false;
  }

  const Spec &spec = _specs[spec_index];
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << IndentStr(level)
            << "  specTy = " << tinyusdz::to_string(spec.spec_type) << "\n";
  std::cout << IndentStr(level)
            << "  fieldSetIndex = " << spec.fieldset_index.value << "\n";
#endif

  if (!_live_fieldsets.count(spec.fieldset_index)) {
    _err += "FieldSet id: " + std::to_string(spec.fieldset_index.value) +
            " must exist in live fieldsets.\n";
    return false;
  }

  const FieldValuePairVector &fields = _live_fieldsets.at(spec.fieldset_index);

  // root only attributes.
  if (parent == 0) {
    for (const auto &fv : fields) {
      if ((fv.first == "upAxis") &&
          (fv.second.GetTypeId() == VALUE_TYPE_TOKEN)) {
        std::string v = fv.second.GetToken();
        if ((v != "Y") && (v != "Z") && (v != "X")) {
          _err += "Currently `upAxis` must be 'X', 'Y' or 'Z' but got '" + v +
                  "'\n";
          return false;
        }
        scene->upAxis = std::move(v);
      } else if (fv.first == "metersPerUnit") {
        if ((fv.second.GetTypeId() == VALUE_TYPE_DOUBLE) ||
            (fv.second.GetTypeId() == VALUE_TYPE_FLOAT)) {
          scene->metersPerUnit = fv.second.GetDouble();
        } else {
          _err +=
              "Currently `metersPerUnit` value must be double or float type, "
              "but got '" +
              fv.second.GetTypeName() + "'\n";
          return false;
        }
      } else if (fv.first == "timeCodesPerSecond") {
        if ((fv.second.GetTypeId() == VALUE_TYPE_DOUBLE) ||
            (fv.second.GetTypeId() == VALUE_TYPE_FLOAT)) {
          scene->timeCodesPerSecond = fv.second.GetDouble();
        } else {
          _err +=
              "Currently `timeCodesPerSecond` value must be double or float "
              "type, but got '" +
              fv.second.GetTypeName() + "'\n";
          return false;
        }
      } else if ((fv.first == "defaultPrim") &&
                 (fv.second.GetTypeId() == VALUE_TYPE_TOKEN)) {
        scene->defaultPrim = fv.second.GetToken();
      } else if (fv.first == "customLayerData") {
        if (fv.second.GetTypeId() == VALUE_TYPE_DICTIONARY) {
          PUSH_WARN("TODO: Store customLayerData.");
          //scene->customLayerData = fv.second.GetDictionary();
        } else {
          PUSH_ERROR("customLayerData must be `dict` type.");
        }
      } else if (fv.first == "primChildren") {
        if (fv.second.GetTypeId() != VALUE_TYPE_TOKEN_VECTOR) {
          PUSH_ERROR("Type must be TokenArray for `primChildren`, but got " + fv.second.GetTypeName() + "\n");
          return false;
        }

        scene->primChildren = fv.second.GetTokenArray();
      } else if (fv.first == "documentation") { // 'doc'
        if (fv.second.GetTypeId() != VALUE_TYPE_STRING) {
          PUSH_ERROR("Type must be String for `documentation`, but got " + fv.second.GetTypeName() + "\n");
          return false;
        }
        scene->doc = fv.second.GetString();
      } else {
        PUSH_ERROR("TODO: " + fv.first + "\n");
        //_err += "TODO: " + fv.first + "\n";
        return false;
        // TODO(syoyo):
      }
    }
  }

  std::string node_type;
  Value::Dictionary assetInfo;

  for (const auto &fv : fields) {
    DCOUT(IndentStr(level) << "  \"" << fv.first
              << "\" : ty = " << fv.second.GetTypeName());
    if (fv.second.GetTypeId() == VALUE_TYPE_SPECIFIER) {
      DCOUT(IndentStr(level) << "    specifier = "
                << GetSpecifierString(fv.second.GetSpecifier()));
    } else if (fv.second.GetTypeId() == VALUE_TYPE_TOKEN) {
      if (fv.first == "typeName") {
        node_type = fv.second.GetToken();
      }
      // std::cout << IndentStr(level) << "    token = " << fv.second.GetToken()
      // << "\n";
    } else if ((fv.first == "primChildren") &&
               (fv.second.GetTypeName() == "TokenArray")) {
      // Check if TokenArray contains known child nodes
      const auto &tokens = fv.second.GetStringArray();

      //bool valid = true;
      for (const auto &token : tokens) {
        if (!node.GetPrimChildren().count(token)) {
          _err += "primChild '" + token + "' not found in node '" +
                  node.GetPath().full_path_name() + "'\n";
          //valid = false;
          break;
        }
      }
    } else if (fv.second.GetTypeName() == "TokenArray") {
      assert(fv.second.IsArray());
      const auto &strs = fv.second.GetStringArray();
      for (const auto &str : strs) {
        (void)str;
        DCOUT(IndentStr(level + 2) << str);
      }
    } else if ((fv.first == "customLayerData") && (fv.second.GetTypeName() == "Dictionary")) {
      const auto &dict = fv.second.GetDictionary();

      for (const auto &item : dict) {
        if (item.second.GetTypeName() == "String") {
          scene->customLayerData[item.first] = item.second.GetString();
        } else if (item.second.GetTypeName() == "IntArray") {

          const auto arr = item.second.GetIntArray();
          scene->customLayerData[item.first] = arr;
        } else {
          PUSH_WARN("TODO(customLayerData): name " + item.first + ", type " + item.second.GetTypeName());
        }
      }

    } else if (fv.second.GetTypeName() == "TokenListOp") {
      PUSH_WARN("TODO: name " + fv.first + ", type TokenListOp.");
    } else if (fv.second.GetTypeName() == "Vec3fArray") {
      PUSH_WARN("TODO: name: " + fv.first + ", type: " + fv.second.GetTypeName());

    } else if ((fv.first == "assetInfo") && (fv.second.GetTypeName() == "Dictionary")) {

      node_type = "assetInfo";
      assetInfo = fv.second.GetDictionary();

    } else {
      PUSH_WARN("TODO: name: " + fv.first + ", type: " + fv.second.GetTypeName());
      //return false;
    }
  }

  if (node_type == "Xform") {
    Xform xform;
    if (!ReconstructXform(node, fields, path_index_to_spec_index_map,
                           &xform)) {
      _err += "Failed to reconstruct Xform.\n";
      return false;
    }
    scene->xforms.push_back(xform);
  } else if (node_type == "BasisCurves") {
    GeomBasisCurves curves;
    if (!ReconstructGeomBasisCurves(node, fields, path_index_to_spec_index_map,
                              &curves)) {
      _err += "Failed to reconstruct GeomBasisCurves.\n";
      return false;
    }
    curves.name = node.GetLocalPath(); // FIXME
    scene->geom_basis_curves.push_back(curves);
  } else if (node_type == "GeomSubset") {
    GeomSubset geom_subset;
    // TODO(syoyo): Pass Parent 'Geom' node.
    if (!ReconstructGeomSubset(node, fields, path_index_to_spec_index_map,
                              &geom_subset)) {
      _err += "Failed to reconstruct GeomSubset.\n";
      return false;
    }
    geom_subset.name = node.GetLocalPath(); // FIXME
    // TODO(syoyo): add GeomSubset to parent `Mesh`.
    _err += "TODO: Add GeomSubset to Mesh.\n";
    return false;

  } else if (node_type == "Mesh") {
    GeomMesh mesh;
    if (!ReconstructGeomMesh(node, fields, path_index_to_spec_index_map,
                              &mesh)) {
      _err += "Failed to reconstruct GeomMesh.\n";
      return false;
    }
    mesh.name = node.GetLocalPath(); // FIXME
    scene->geom_meshes.push_back(mesh);
  } else if (node_type == "Material") {
    Material material;
    if (!ReconstructMaterial(node, fields, path_index_to_spec_index_map,
                              &material)) {
      _err += "Failed to reconstruct Material.\n";
      return false;
    }
    material.name = node.GetLocalPath(); // FIXME
    scene->materials.push_back(material);
  } else if (node_type == "Shader") {
    Shader shader;
    if (!ReconstructShader(node, fields, path_index_to_spec_index_map,
                           &shader)) {
      _err += "Failed to reconstruct PreviewSurface(Shader).\n";
      return false;
    }

    shader.name = node.GetLocalPath(); // FIXME

    scene->shaders.push_back(shader);
  } else if (node_type == "Scope") {
    std::cout << "TODO: Scope\n";
  } else if (node_type == "assetInfo") {

    PUSH_WARN("TODO: Reconstruct dictionary value of `assetInfo`");
    //_nodes[size_t(parent)].SetAssetInfo(assetInfo);
  } else if (node_type == "Skeleton") {
    Skeleton skeleton;
    if (!ReconstructSkeleton(node, fields, path_index_to_spec_index_map,
                           &skeleton)) {
      _err += "Failed to reconstruct Skeleton.\n";
      return false;
    }

    skeleton.name = node.GetLocalPath(); // FIXME

    scene->skeletons.push_back(skeleton);

  } else if (node_type == "SkelRoot") {
    SkelRoot skelRoot;
    if (!ReconstructSkelRoot(node, fields, path_index_to_spec_index_map,
                           &skelRoot)) {
      _err += "Failed to reconstruct SkelRoot.\n";
      return false;
    }

    skelRoot.name = node.GetLocalPath(); // FIXME

    scene->skel_roots.push_back(skelRoot);
  } else {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "TODO or we can ignore this node: node_type: " << node_type
              << "\n";
#endif
    PUSH_WARN("TODO: Reconstruct node_type " + node_type);
  }

  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    if (!ReconstructSceneRecursively(int(node.GetChildren()[i]), level + 1,
                                      path_index_to_spec_index_map, scene)) {
      return false;
    }
  }

  return true;
}

bool Parser::ReconstructScene(Scene *scene) {
  if (_nodes.empty()) {
    _warn += "Empty scene.\n";
    return true;
  }

  std::unordered_map<uint32_t, uint32_t>
      path_index_to_spec_index_map;  // path_index -> spec_index

  {
    for (size_t i = 0; i < _specs.size(); i++) {
      if (_specs[i].path_index.value == ~0u) {
        continue;
      }

      // path_index should be unique.
      assert(path_index_to_spec_index_map.count(_specs[i].path_index.value) ==
             0);
      path_index_to_spec_index_map[_specs[i].path_index.value] = uint32_t(i);
    }
  }

  int root_node_id = 0;

  bool ret = ReconstructSceneRecursively(root_node_id, /* level */ 0,
                                      path_index_to_spec_index_map, scene);

  if (!ret) {
    _err += "Failed to reconstruct scene.\n";
    return false;
  }

  return true;
}

bool Parser::ReadSpecs() {
  if ((_specs_index < 0) || (_specs_index >= int64_t(_toc.sections.size()))) {
    _err += "Invalid index for `SPECS` section.\n";
    return false;
  }

  if ((_version[0] == 0) && (_version[1] < 4)) {
    _err += "Version must be 0.4.0 or later, but got " +
            std::to_string(_version[0]) + "." + std::to_string(_version[1]) +
            "." + std::to_string(_version[2]) + "\n";
    return false;
  }

  const Section &s = _toc.sections[size_t(_specs_index)];

  if (!_sr->seek_set(uint64_t(s.start))) {
    _err += "Failed to move to `SPECS` section.\n";
    return false;
  }

  uint64_t num_specs;
  if (!_sr->read8(&num_specs)) {
    _err += "Failed to read # of specs size at `SPECS` section.\n";
    return false;
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "num_specs " << num_specs << "\n";
#endif

  _specs.resize(static_cast<size_t>(num_specs));

  // Create temporary space for decompressing.
  std::vector<char> comp_buffer(Usd_IntegerCompression::GetCompressedBufferSize(
      static_cast<size_t>(num_specs)));

  std::vector<uint32_t> tmp(static_cast<size_t>(num_specs));
  std::vector<char> working_space(
      Usd_IntegerCompression::GetDecompressionWorkingSpaceSize(
          static_cast<size_t>(num_specs)));

  // path indices
  {
    uint64_t path_indexes_size;
    if (!_sr->read8(&path_indexes_size)) {
      _err += "Failed to read path indexes size at `SPECS` section.\n";
      return false;
    }

    assert(path_indexes_size < comp_buffer.size());

    if (path_indexes_size !=
        _sr->read(size_t(path_indexes_size), size_t(path_indexes_size),
                  reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
      _err += "Failed to read path indexes data at `SPECS` section.\n";
      return false;
    }

    std::string err;
    if (!Usd_IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), size_t(path_indexes_size), tmp.data(),
            size_t(num_specs), &err, working_space.data())) {
      _err += "Failed to decode pathIndexes at `SPECS` section.\n";
      _err += err;
      return false;
    }

    for (size_t i = 0; i < num_specs; ++i) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "spec[" << i << "].path_index = " << tmp[i] << "\n";
#endif
      _specs[i].path_index.value = tmp[i];
    }
  }

  // fieldset indices
  {
    uint64_t fset_indexes_size;
    if (!_sr->read8(&fset_indexes_size)) {
      _err += "Failed to read fieldset indexes size at `SPECS` section.\n";
      return false;
    }

    assert(fset_indexes_size < comp_buffer.size());

    if (fset_indexes_size !=
        _sr->read(size_t(fset_indexes_size), size_t(fset_indexes_size),
                  reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
      _err += "Failed to read fieldset indexes data at `SPECS` section.\n";
      return false;
    }

    std::string err;
    if (!Usd_IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), size_t(fset_indexes_size), tmp.data(),
            size_t(num_specs), &err, working_space.data())) {
      _err += "Failed to decode fieldset indices at `SPECS` section.\n";
      _err += err;
      return false;
    }

    for (size_t i = 0; i != num_specs; ++i) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "specs[" << i << "].fieldset_index = " << tmp[i] << "\n";
#endif
      _specs[i].fieldset_index.value = tmp[i];
    }
  }

  // spec types
  {
    uint64_t spectype_size;
    if (!_sr->read8(&spectype_size)) {
      _err += "Failed to read spectype size at `SPECS` section.\n";
      return false;
    }

    assert(spectype_size < comp_buffer.size());

    if (spectype_size !=
        _sr->read(size_t(spectype_size), size_t(spectype_size),
                  reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
      _err += "Failed to read spectype data at `SPECS` section.\n";
      return false;
    }

    std::string err;
    if (!Usd_IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), size_t(spectype_size), tmp.data(),
            size_t(num_specs), &err, working_space.data())) {
      _err += "Failed to decode fieldset indices at `SPECS` section.\n";
      _err += err;
      return false;
    }

    for (size_t i = 0; i != num_specs; ++i) {
      // std::cout << "spectype = " << tmp[i] << "\n";
      _specs[i].spec_type = static_cast<SpecType>(tmp[i]);
    }
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  for (size_t i = 0; i != num_specs; ++i) {
    std::cout << "spec[" << i << "].pathIndex  = " << _specs[i].path_index.value
              << ", fieldset_index = " << _specs[i].fieldset_index.value
              << ", spec_type = " << tinyusdz::to_string(_specs[i].spec_type) << "\n";
    std::cout << "spec[" << i
              << "] string_repr = " << GetSpecString(Index(uint32_t(i)))
              << "\n";
  }
#endif

  return true;
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

  const Section &s = _toc.sections[size_t(_paths_index)];

  if (!_sr->seek_set(uint64_t(s.start))) {
    _err += "Failed to move to `PATHS` section.\n";
    return false;
  }

  uint64_t num_paths;
  if (!_sr->read8(&num_paths)) {
    _err += "Failed to read # of paths at `PATHS` section.\n";
    return false;
  }

  if (!ReadCompressedPaths(num_paths)) {
    _err += "Failed to read compressed paths.\n";
    return false;
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "# of paths " << _paths.size() << "\n";

  for (size_t i = 0; i < _paths.size(); i++) {
    std::cout << "path[" << i << "] = " << _paths[i].full_path_name() << "\n";
  }
#endif

  return true;
}

bool Parser::ReadBootStrap() {
  // parse header.
  uint8_t magic[8];
  if (8 != _sr->read(/* req */ 8, /* dst len */ 8, magic)) {
    _err += "Failed to read magic number.\n";
    return false;
  }

  // std::cout << int(magic[0]) << "\n";
  // std::cout << int(magic[1]) << "\n";
  // std::cout << int(magic[2]) << "\n";
  // std::cout << int(magic[3]) << "\n";
  // std::cout << int(magic[4]) << "\n";
  // std::cout << int(magic[5]) << "\n";
  // std::cout << int(magic[6]) << "\n";
  // std::cout << int(magic[7]) << "\n";

  if (memcmp(magic, "PXR-USDC", 8)) {
    _err += "Invalid magic number. Expected 'PXR-USDC' but got '" +
            std::string(magic, magic + 8) + "'\n";
    return false;
  }

  // parse version(first 3 bytes from 8 bytes)
  uint8_t version[8];
  if (8 != _sr->read(8, 8, version)) {
    _err += "Failed to read magic number.\n";
    return false;
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "version = " << int(version[0]) << "." << int(version[1]) << "."
            << int(version[2]) << "\n";
#endif

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

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "toc offset = " << _toc_offset << "\n";
#endif

  return true;
}

bool Parser::ReadTOC() {
  if ((_toc_offset <= 88) || (_toc_offset >= int64_t(_sr->size()))) {
    _err += "Invalid toc offset\n";
    return false;
  }

  if (!_sr->seek_set(uint64_t(_toc_offset))) {
    _err += "Failed to move to TOC offset\n";
    return false;
  }

  // read # of sections.
  uint64_t num_sections{0};
  if (!_sr->read8(&num_sections)) {
    _err += "Failed to read TOC(# of sections)\n";
    return false;
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "toc sections = " << num_sections << "\n";
#endif

  _toc.sections.resize(static_cast<size_t>(num_sections));

  for (size_t i = 0; i < num_sections; i++) {
    if (!ReadSection(&_toc.sections[i])) {
      _err += "Failed to read TOC section at " + std::to_string(i) + "\n";
      return false;
    }
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "section[" << i << "] name = " << _toc.sections[i].name
              << ", start = " << _toc.sections[i].start
              << ", size = " << _toc.sections[i].size << "\n";
#endif

    // find index
    if (0 == strncmp(_toc.sections[i].name, "TOKENS", kSectionNameMaxLength)) {
      _tokens_index = int64_t(i);
    } else if (0 == strncmp(_toc.sections[i].name, "STRINGS",
                            kSectionNameMaxLength)) {
      _strings_index = int64_t(i);
    } else if (0 == strncmp(_toc.sections[i].name, "FIELDS",
                            kSectionNameMaxLength)) {
      _fields_index = int64_t(i);
    } else if (0 == strncmp(_toc.sections[i].name, "FIELDSETS",
                            kSectionNameMaxLength)) {
      _fieldsets_index = int64_t(i);
    } else if (0 ==
               strncmp(_toc.sections[i].name, "SPECS", kSectionNameMaxLength)) {
      _specs_index = int64_t(i);
    } else if (0 ==
               strncmp(_toc.sections[i].name, "PATHS", kSectionNameMaxLength)) {
      _paths_index = int64_t(i);
    }
  }

  return true;
}

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

  Parser parser(&sr, options.num_threads);

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

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "num_paths: " << parser.NumPaths() << "\n";
#endif

  for (size_t i = 0; i < parser.NumPaths(); i++) {
    Path path = parser.GetPath(Index(uint32_t(i)));
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "path[" << i << "].name = " << path.full_path_name() << "\n";
#endif
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

  if (data.size() < (11 * 8)) {
    // ???
    if (err) {
      (*err) +=
          "File size too short. Looks like this file is not a USDC : \"" +
          filepath + "\"\n";
    }
    return false;
  }

#if 0
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

    if (sz > size_t(1024 * 1024 * options.max_memory_limit_in_mb)) {
      if (err) {
        (*err) += "USDZ file is too large(size = " + std::to_string(sz) +
                  ", which exceeds memory limit " +
                  std::to_string(options.max_memory_limit_in_mb) + " [mb]).\n";
      }

      return false;
    }

    data.resize(sz);

    ifs.seekg(0, ifs.beg);
    ifs.read(reinterpret_cast<char *>(&data.at(0)),
             static_cast<std::streamsize>(sz));
  }
#endif

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
                 // static_cast<int(*)(int)>(std::tolower)         // wrong
                 // [](int c){ return std::tolower(c); }           // wrong
                 // [](char c){ return std::tolower(c); }          // wrong
                 [](unsigned char c) { return std::tolower(c); }  // correct
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

#if 0
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

    if (sz < (11 * 8) + 30) {  // 88 for USDC header, 30 for ZIP header
      // ???
      if (err) {
        (*err) +=
            "File size too short. Looks like this file is not a USDZ : \"" +
            filename + "\"\n";
      }
      return false;
    }

    data.resize(sz);

    ifs.seekg(0, ifs.beg);
    ifs.read(reinterpret_cast<char *>(&data.at(0)),
             static_cast<std::streamsize>(sz));
  }
#endif

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
    std::cout << "[" << i << "] " << std::get<0>(assets[i]) << " : byte range ("
              << std::get<1>(assets[i]) << ", " << std::get<2>(assets[i])
              << ")\n";
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

Matrix4d GetTransform(XformOp xform)
{
  Matrix4d m;
  Identity(&m);

  if (xform.op == XformOp::OpType::TRANSFORM) {
    m = nonstd::get<Matrix4d>(xform.value);
  } else if (xform.op == XformOp::OpType::TRANSLATE) {
    if (xform.precision == XformOp::PRECISION_DOUBLE) {
      auto s = nonstd::get<Vec3f>(xform.value);
      m.m[0][0] = double(s[0]);
      m.m[1][1] = double(s[1]);
      m.m[2][2] = double(s[2]);
    } else {
      auto s = nonstd::get<Vec3d>(xform.value);
      m.m[0][0] = s[0];
      m.m[1][1] = s[1];
      m.m[2][2] = s[2];
    }
  } else if (xform.op == XformOp::OpType::SCALE) {
    if (xform.precision == XformOp::PRECISION_DOUBLE) {
      auto s = nonstd::get<Vec3f>(xform.value);
      m.m[0][0] = double(s[0]);
      m.m[1][1] = double(s[1]);
      m.m[2][2] = double(s[2]);
    } else {
      auto s = nonstd::get<Vec3d>(xform.value);
      m.m[0][0] = s[0];
      m.m[1][1] = s[1];
      m.m[2][2] = s[2];
    }
  }
  // TODO: rotation, orient

  return m;
}

bool Xform::EvaluateXformOps(Matrix4d *out_matrix) const {
    Identity(out_matrix);

    Matrix4d cm;

    // Concat matrices
    for (const auto &x : xformOps) {
      Matrix4d m;
      Identity(&m);
      if (x.op == XformOp::TRANSLATE) {
        if (x.precision == XformOp::PRECISION_FLOAT) {
          Vec3f tx = nonstd::get<Vec3f>(x.value);
          m.m[3][0] = double(tx[0]);
          m.m[3][1] = double(tx[1]);
          m.m[3][2] = double(tx[2]);
        } else if (x.precision == XformOp::PRECISION_DOUBLE) {
          Vec3d tx = nonstd::get<Vec3d>(x.value);
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

      cm = Mult(cm, m);
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
      //if (auto p = primvar::as_vector<Vec3f>(&attr.var)) {
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
      //if (auto p = primvar::as_vector<Vec3f>(&attr.var)) {
      //  normals.var = *p;
      //  normals.interpolation = attr.interpolation;
      //}
    } else if (attr_name == "velocitiess") {
      //if (auto p = primvar::as_vector<Vec3f>(&attr.var)) {
      //  velocitiess.var = (*p);
      //  velocitiess.interpolation = attr.interpolation;
      //}
    } else if (attr_name == "primvars:uv") {
      //if (auto pv2f = primvar::as_vector<Vec2f>(&attr.var)) {
      //  st.buffer = (*pv2f);
      //  st.interpolation = attr.interpolation;
      //} else if (auto pv3f = primvar::as_vector<Vec3f>(&attr.var)) {
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

static_assert(sizeof(Index) == 4, "");
static_assert(sizeof(Field) == 16, "");
static_assert(sizeof(Spec) == 12, "");
static_assert(sizeof(Vec4h) == 8, "");
static_assert(sizeof(Vec2f) == 8, "");
static_assert(sizeof(Vec3f) == 12, "");
static_assert(sizeof(Vec4f) == 16, "");
static_assert(sizeof(Vec2d) == 16, "");
static_assert(sizeof(Vec3d) == 24, "");
static_assert(sizeof(Vec4d) == 32, "");
static_assert(sizeof(Matrix4d) == (8 * 16), "");

}  // namespace tinyusdz
