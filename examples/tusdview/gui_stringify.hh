// SPDX-License-Identifier: Apache-2.0
// tusdview - render a tinyusdz Property to a one-line display string for the
// inspector table. No exceptions are used (the project builds -fno-exceptions);
// every access is guarded.
#pragma once

#include <string>

#include "core/property.hh"

namespace tusdview {

std::string PropertyToString(const tinyusdz::Property& prop);

}  // namespace tusdview
