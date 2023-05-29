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
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
#include <iostream>  // dbg
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// TODO: Use std:: version for C++17
#include "nonstd/expected.hpp"
#include "nonstd/optional.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "image-types.hh"
#include "prim-types.hh"
#include "texture-types.hh"
#include "usdGeom.hh"
#include "usdLux.hh"
#include "usdShade.hh"
#include "usdSkel.hh"
//#include "usdVox.hh"
#include "stage.hh"

namespace tinyusdz {

constexpr int version_major = 0;
constexpr int version_minor = 8;
constexpr int version_micro = 0;
constexpr auto version_rev = "rc4";  // extra revision suffix

struct USDLoadOptions {
  ///
  /// Set the number of threads to use when parsing USD scene.
  /// -1 = use # of system threads(CPU cores/threads).
  ///
  int num_threads{-1};

  // Set the maximum memory limit advisorily(including image data).
  // This feature would be helpful if you want to load USDZ model in mobile
  // device.
  int32_t max_memory_limit_in_mb{16384};  // in [mb] Default 16GB

  ///
  /// Loads asset data(e.g. texture image, audio). Default is true.
  /// If you want to load asset data in your own way or don't need asset data to
  /// be loaded, Set this false.
  ///
  bool load_assets{true};

  ///
  /// (experimental)
  /// Do composition on load(Load sublayers, references, etc)
  /// For USDZ model, this should be false.
  ///
  bool do_composition{false};

  ///
  /// Following load flags are valid when `do_composition` is set `true`.
  ///
  bool load_sublayers{false}; // true: Load `subLayers`
  bool load_references{false}; // true: Load `references`
  bool load_payloads{false}; // true: Load `paylod` at top USD loading(no lazy loading).

  ///
  /// Max MBs allowed for each asset file(e.g. jpeg)
  ///
  uint32_t max_allowed_asset_size_in_mb{1024};  // [mb] default 1GB.

  ///
  /// For texture size
  ///
  uint32_t max_image_width = 2048;
  uint32_t max_image_height = 2048;
  uint32_t max_image_channels = 4;

  ///
  /// For usdSkel
  ///
  bool strict_usdSkel_check{false}; // Strict usdSkel parsing check when true.

  Axis upAxis{Axis::Y};
};

// TODO: Provide profiles for loader option.
// e.g.
// - Embedded(e.g. web, tigh resource size limit for security)
// - Realtime(moderate resource size limit)
// - DCC(for data conversion. Unlimited resource size)

#if 0  // TODO
//struct USDWriteOptions
//{
//
//
//};
#endif

//

///
/// Load USD(USDA/USDC/USDZ) from a file.
/// Automatically detect file format.
///
/// @param[in] filename USD filename(UTF-8)
/// @param[out] stage USD stage(scene graph).
/// @param[out] warn Warning message.
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Load options(optional)
///
/// @return true upon success
///
bool LoadUSDFromFile(const std::string &filename, Stage *stage,
                     std::string *warn, std::string *err,
                     const USDLoadOptions &options = USDLoadOptions());

///
/// Load USD(USDA/USDC/USDZ) from memory.
/// Automatically detect file format.
///
/// @param[in] addr Memory address of USDZ data
/// @param[in] length Byte length of USDZ data
/// @param[in] filename Filename(can be empty).
/// @param[out] stage USD stage(scene graph).
/// @param[out] warn Warning message.
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Load options(optional)
///
/// @return true upon success
///
bool LoadUSDFromMemory(const uint8_t *addr, const size_t length,
                       const std::string &filename, Stage *stage,
                       std::string *warn, std::string *err,
                       const USDLoadOptions &options = USDLoadOptions());

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
// WideChar(Unicode filename) version
bool LoadUSDZFromFile(const std::wstring &filename, Stage *stage,
                      std::string *warn, std::string *err,
                      const USDLoadOptions &options = USDLoadOptions());
#endif

///
/// Load USDZ(zip) from memory.
///
/// @param[in] addr Memory address of USDZ data
/// @param[in] length Byte length of USDZ data
/// @param[in] filename Filename(can be empty).
/// @param[out] stage USD stage(scene graph).
/// @param[out] warn Warning message.
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Load options(optional)
///
/// @return true upon success
///
bool LoadUSDZFromMemory(const uint8_t *addr, const size_t length,
                        const std::string &filename, Stage *stage,
                        std::string *warn, std::string *err,
                        const USDLoadOptions &options = USDLoadOptions());

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
/// @param[in] filename Filename(can be empty).
/// @param[out] stage USD stage.
/// @param[out] warn Warning message.
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Load options(optional)
///
/// @return true upon success
///
bool LoadUSDCFromMemory(const uint8_t *addr, const size_t length,
                        const std::string &filename, Stage *stage,
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
bool LoadUSDAFromMemory(const uint8_t *addr, const size_t length,
                        const std::string &base_dir, Stage *stage,
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

// Test if input is any of USDA/USDC/USDZ format.
// Optionally returns detected format("usda", "usdc", or "usdz") to
// `detected_format` when a given file/binary is a USD format.
bool IsUSD(const std::string &filename, std::string *detected_format = nullptr);
bool IsUSD(const uint8_t *addr, const size_t length,
            std::string *detected_format = nullptr);

// Test if input is USDA format.
bool IsUSDA(const std::string &filename);
bool IsUSDA(const uint8_t *addr, const size_t length);

// Test if input is USDC(Crate binary) format.
bool IsUSDC(const std::string &filename);
bool IsUSDC(const uint8_t *addr, const size_t length);

// Test if input is USDZ(Uncompressed ZIP) format.
bool IsUSDZ(const std::string &filename);
bool IsUSDZ(const uint8_t *addr, const size_t length);

}  // namespace tinyusdz

#endif  // TINYUSDZ_HH_
