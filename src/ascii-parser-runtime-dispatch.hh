#pragma once

#include <vector>
#include <functional>
#include <unordered_map>
#include "value-types.hh"
#include "nonstd/optional.hpp"

namespace tinyusdz {
namespace ascii {

class AsciiParser; // forward declaration

//
// Runtime dispatch system to replace template-heavy ParseBasicTypeArray
//
class RuntimeArrayParser {
public:
  // Type-erased parsing function signature
  using ArrayParserFunc = std::function<bool(AsciiParser*, void* result_ptr)>;
  
  // Parse function for optional arrays  
  using OptionalArrayParserFunc = std::function<bool(AsciiParser*, void* result_ptr)>;

  // Register parsing functions for all supported types
  static void RegisterAllParsers();

  // Parse array using runtime dispatch based on type ID
  static bool ParseArrayByTypeId(AsciiParser* parser, uint32_t type_id, void* result_ptr);
  
  // Parse optional array using runtime dispatch based on type ID
  static bool ParseOptionalArrayByTypeId(AsciiParser* parser, uint32_t type_id, void* result_ptr);

  // Helper to get array type ID from base type ID
  static uint32_t GetArrayTypeId(uint32_t base_type_id);

  // Create Value object from parsed array data
  static bool CreateValueFromArray(uint32_t base_type_id, void* array_ptr, value::Value* out_value);

private:
  // Registry for array parsers (type_id -> parser function)
  static std::unordered_map<uint32_t, ArrayParserFunc>& _array_parsers;
  
  // Registry for optional array parsers
  static std::unordered_map<uint32_t, OptionalArrayParserFunc>& _optional_array_parsers;
  
  // Flag to track if parsers have been registered
  static bool _parsers_registered;

  // Template helper to create type-specific parser
  template<typename T>
  static ArrayParserFunc CreateArrayParser();

  // Template helper to create type-specific optional parser
  template<typename T>
  static OptionalArrayParserFunc CreateOptionalArrayParser();

  // Register a single type
  template<typename T>
  static void RegisterType() {
    uint32_t type_id = value::TypeTraits<T>::type_id();
    uint32_t array_type_id = type_id | value::TYPE_ID_1D_ARRAY_BIT;
    
    _array_parsers[array_type_id] = CreateArrayParser<T>();
    _optional_array_parsers[array_type_id] = CreateOptionalArrayParser<T>();
  }
};

} // namespace ascii
} // namespace tinyusdz
