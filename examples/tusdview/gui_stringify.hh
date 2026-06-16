// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "core/attribute.hh"
#include "core/prim.hh"
#include "core/property.hh"

namespace tusdview {

std::string PropertyToString(const tinyusdz::Property& prop);

std::string PrimMetaSummary(const tinyusdz::Prim& prim);
std::string AttrMetaSummary(const tinyusdz::Attribute& attr);
std::string GPrimPropertySummary(const tinyusdz::Prim& prim);
std::string SubdivisionSchemeName(const tinyusdz::Prim& prim);
std::string VisibilityState(const tinyusdz::Prim& prim);
std::string VariantSetDetail(const tinyusdz::Prim& prim);
std::string GeometrySummary(const tinyusdz::Prim& prim);

}  // namespace tusdview
