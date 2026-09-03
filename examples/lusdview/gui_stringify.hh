// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "core/attribute.hh"
#include "core/prim.hh"
#include "core/property.hh"

namespace lusdview {

std::string PropertyToString(const lightusd::Property& prop);

std::string PrimMetaSummary(const lightusd::Prim& prim);
std::string AttrMetaSummary(const lightusd::Attribute& attr);
std::string GPrimPropertySummary(const lightusd::Prim& prim);
std::string SubdivisionSchemeName(const lightusd::Prim& prim);
std::string VisibilityState(const lightusd::Prim& prim);
std::string VariantSetDetail(const lightusd::Prim& prim);
std::string GeometrySummary(const lightusd::Prim& prim);

}  // namespace lusdview
