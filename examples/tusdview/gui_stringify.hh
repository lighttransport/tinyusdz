// SPDX-License-Identifier: Apache-2.0
// tusdview - render a tinyusdz Property to a one-line display string for the
// inspector table. No exceptions are used (the project builds -fno-exceptions);
// every access is guarded.
#pragma once

#include <string>

#include "core/attribute.hh"
#include "core/prim.hh"
#include "core/property.hh"

namespace tusdview {

std::string PropertyToString(const tinyusdz::Property& prop);

// Multi-line summary of authored prim metadata (kind/active/hidden/etc.).
// Empty if nothing authored.
std::string PrimMetaSummary(const tinyusdz::Prim& prim);

// One-line summary of authored attribute metadata (interpolation/elementSize/etc.).
// Empty if nothing authored.
std::string AttrMetaSummary(const tinyusdz::Attribute& attr);

}  // namespace tusdview
