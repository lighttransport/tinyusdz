#pragma once

#include <sstream>
#include <string>
#include "tinyusdz.hh"

namespace variant_format {

// Convert Dictionary to YAML format
std::string dictionary_to_yaml(const tinyusdz::Dictionary& dict);

// Convert Dictionary to JSON format
std::string dictionary_to_json(const tinyusdz::Dictionary& dict);

}  // namespace variant_format
