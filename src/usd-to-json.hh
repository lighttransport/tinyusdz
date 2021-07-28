// SPDX-License-Identifier: MIT
// Experimental USD to JSON converter

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "nonstd/expected.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace tinyusd {

nonstd::expected<std::string, std::string> ToJSON();


} // namespace tinyusd
