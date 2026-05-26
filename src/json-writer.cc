#include "json-writer.hh"
#include "layer.hh"
#include "str-util.hh"
#include <string>

// NOTE: dtos() from str-util.hh uses dragonbox algorithm for
// shortest float-to-string conversion

#include "common-macros.inc"

namespace tinyusdz {
namespace json {


namespace detail {

// NOTE: Use tinyusdz::dtos() from str-util.hh instead



} // namespace detal

bool JsonWriter::to_json(const tinyusdz::Layer &layer, std::string *out_json) {

  (void)layer;
  (void)out_json;

  // TODO
  return false;
}

} // namespace json
}  // namespace tinyusdz
