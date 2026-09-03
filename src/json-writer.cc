#include "json-writer.hh"
#include "layer.hh"
#include "str-util.hh"
#include <string>

// NOTE: dtos() from str-util.hh uses dragonbox algorithm for
// shortest float-to-string conversion

#include "common-macros.inc"

namespace lightusd {
namespace json {


namespace detail {

// NOTE: Use lightusd::dtos() from str-util.hh instead



} // namespace detal

bool JsonWriter::to_json(const lightusd::Layer &layer, std::string *out_json) {

  (void)layer;
  (void)out_json;

  // TODO
  return false;
}

} // namespace json
}  // namespace lightusd
