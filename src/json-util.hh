#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/jsonhpp/nlohmann/json.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#pragma once

namespace tinyusdz {

using json = nlohmann::json;

#if 0
namespace json {

struct JsonValue
{
 public:
  enum class ValueType {
    NullType,
    BooleanType,
    StringType,
    NumberType,
    ArrayType,
    ObjectType
  };
    
  typedef std::vector<JsonValue> Array;
  typedef std::map<std::string, JsonValue> Object;

  JsonValue() = default;

  explicit JsonValue(bool b) : type_{BoolType} {
    boolean_value_ = b;
  }
  explicit JsonValue(const std::string &s) : type_{StringType} {
    string_value_ = b;
  }

  template<typename T>
  

 private:
  ValueType type_{NullType};
  double number_value_{0.0};
  std::string string_value;
  Array array_value_;
  Object object_value_;
  bool boolean_value_{false};

};




} // namespace json
#endif
} // namespace tinyusdz
