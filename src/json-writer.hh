#pragma once

#include <cstdint>
#include <string>


namespace tinyusdz {

class Layer;
class Stage;

namespace json {

class JsonWriter {

 public:
  JsonWriter() = default;
  ~JsonWriter() = default;
  JsonWriter(const JsonWriter &) = delete;
  JsonWriter &operator=(const JsonWriter &) = delete;
  JsonWriter(JsonWriter &&) = default;
  JsonWriter &operator=(JsonWriter &&) = default;   

  void set_indent(uint32_t indent) { indent_ = indent; }

  bool to_json(const tinyusdz::Layer &layer, std::string *out_json);
  bool to_json(const tinyusdz::Stage &stage, std::string *out_json);

 private:
  uint32_t indent_ = 2;

};

} // namespace json 
} // namespace tinyusdz
