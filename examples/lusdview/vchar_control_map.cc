// SPDX-License-Identifier: Apache-2.0
#include "vchar_control_map.hh"

#include <algorithm>

#include "next/lightusd-next.hh"
#include "stage.hh"
#include "tydra/scene-access.hh"

namespace lusdview {
namespace {
struct ControlVisit { std::vector<VcharControl> controls; };

template <typename T>
bool CustomValue(const lightusd::Dictionary& data, const std::string& key,
                 T* value) {
  lightusd::MetaVariable variable;
  return lightusd::GetCustomDataByKey(data, key, &variable) &&
         variable.get_value<T>(value);
}

bool VisitControls(const lightusd::Path&, const lightusd::Prim& prim,
                   const int32_t, void* userdata, std::string*) {
  auto* visit = static_cast<ControlVisit*>(userdata);
  if (!visit || !visit->controls.empty() || !prim.metas().has_customData()) return true;
  const lightusd::Dictionary data = prim.metas().get_customData();
  std::vector<std::string> names, mappings;
  std::vector<lightusd::value::float2> ranges;
  std::vector<float> defaults;
  if (!CustomValue(data, "vchar:controlNames", &names)) return true;
  CustomValue(data, "vchar:controlMappings", &mappings);
  CustomValue(data, "vchar:controlRanges", &ranges);
  CustomValue(data, "vchar:controlDefaults", &defaults);
  for (size_t i = 0; i < names.size(); ++i) {
    VcharControl c;
    c.name = names[i];
    c.blendshape = i < mappings.size() ? mappings[i] : names[i];
    if (i < ranges.size()) {
      c.minimum = ranges[i][0]; c.maximum = ranges[i][1];
      if (c.minimum > c.maximum) std::swap(c.minimum, c.maximum);
    }
    if (i < defaults.size()) c.defaultValue = defaults[i];
    visit->controls.push_back(std::move(c));
  }
  return true;
}

const lightusd::next::Value* NextValue(const lightusd::next::Value& root,
                                       const std::string& name) {
  const auto* rootDict = root.as_dictionary();
  const auto* group = rootDict ? rootDict->find("vchar") : nullptr;
  const auto* dict = group ? group->as_dictionary() : nullptr;
  return dict ? dict->find(name) : nullptr;
}
}  // namespace

std::vector<VcharControl> ReadVcharControls(const lightusd::Stage& stage) {
  ControlVisit visit;
  std::string ignored;
  lightusd::tydra::VisitPrims(stage, VisitControls, &visit, &ignored);
  return visit.controls;
}

std::vector<VcharControl> ReadVcharControls(const lightusd::next::Stage& stage) {
  std::vector<VcharControl> controls;
  stage.Traverse([&](const lightusd::next::UsdPrim& prim) {
    if (!controls.empty()) return true;
    const auto& data = prim.GetMeta().customData();
    const auto* namesValue = NextValue(data, "controlNames");
    const auto* names = namesValue ? namesValue->as_token_array() : nullptr;
    if (!names) return true;
    const auto* mapValue = NextValue(data, "controlMappings");
    const auto* mappings = mapValue ? mapValue->as_token_array() : nullptr;
    const auto* ranges = NextValue(data, "controlRanges");
    const auto* defaultsValue = NextValue(data, "controlDefaults");
    const float* rangeData = ranges ? static_cast<const float*>(ranges->raw_data()) : nullptr;
    const auto* defaults = defaultsValue ? defaultsValue->as_float_array() : nullptr;
    for (size_t i = 0; i < names->size(); ++i) {
      VcharControl c;
      c.name = (*names)[i];
      c.blendshape = mappings && i < mappings->size() ? (*mappings)[i] : c.name;
      if (rangeData && ranges->array_size() > i) {
        c.minimum = rangeData[i * 2u]; c.maximum = rangeData[i * 2u + 1u];
        if (c.minimum > c.maximum) std::swap(c.minimum, c.maximum);
      }
      if (defaults && i < defaults->size()) c.defaultValue = (*defaults)[i];
      controls.push_back(std::move(c));
    }
    return true;
  });
  return controls;
}
}  // namespace lusdview
