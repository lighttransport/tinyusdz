// SPDX-License-Identifier: MIT
// Copyright 2022 - Present Syoyo Fujita.
#pragma once

#include "tinyusdz.hh"

namespace tinyusdz {
namespace usdc {

///
/// Save scene as USDC(binary) to a file
///
/// @param[in] filename USDC filename
/// @param[in] scene Scene
/// @param[out] warn Warning message
/// @param[out] err Error message
///
/// @return true upon success.
///
bool SaveAsUSDCToFile(const std::string &filename, const Scene &scene,
                      std::string *warn, std::string *err);

///
/// Save scene as USDC(binary) to a memory
///
/// @param[in] scene Scene
/// @param[out] output Binary data
/// @param[out] warn Warning message
/// @param[out] err Error message
///
/// @return true upon success.
///
bool SaveAsUSDCToMemory(const Scene &scene, std::vector<uint8_t> *output,
                        std::string *warn, std::string *err);

}  // namespace usdc
}  // namespace tinyusdz
