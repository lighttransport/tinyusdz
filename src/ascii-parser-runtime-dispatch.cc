#include "ascii-parser-runtime-dispatch.hh"
#include "ascii-parser.hh"
#include "value-types.hh"

namespace tinyusdz {
namespace ascii {

// Template implementations (moved from header to avoid forward declaration issues)
template<typename T>
RuntimeArrayParser::ArrayParserFunc RuntimeArrayParser::CreateArrayParser() {
  return [](AsciiParser* parser, void* result_ptr) -> bool {
    auto* typed_result = static_cast<std::vector<T>*>(result_ptr);
    return parser->ParseBasicTypeArrayImpl(typed_result);
  };
}

template<typename T>
RuntimeArrayParser::OptionalArrayParserFunc RuntimeArrayParser::CreateOptionalArrayParser() {
  return [](AsciiParser* parser, void* result_ptr) -> bool {
    auto* typed_result = static_cast<std::vector<nonstd::optional<T>>*>(result_ptr);
    return parser->ParseBasicTypeArrayImpl(typed_result);
  };
}

// Static member definitions - use function static to avoid global constructors
std::unordered_map<uint32_t, RuntimeArrayParser::ArrayParserFunc>& RuntimeArrayParser::_array_parsers = []() -> auto& {
  static std::unordered_map<uint32_t, RuntimeArrayParser::ArrayParserFunc> instance;
  return instance;
}();
std::unordered_map<uint32_t, RuntimeArrayParser::OptionalArrayParserFunc>& RuntimeArrayParser::_optional_array_parsers = []() -> auto& {
  static std::unordered_map<uint32_t, RuntimeArrayParser::OptionalArrayParserFunc> instance;
  return instance;
}();
bool RuntimeArrayParser::_parsers_registered = false;

void RuntimeArrayParser::RegisterAllParsers() {
  if (_parsers_registered) {
    return; // Already registered
  }

  // Register all the types that were previously template instantiations
  
  // Basic types
  RegisterType<bool>();
  RegisterType<int32_t>();
  RegisterType<uint32_t>();
  RegisterType<int64_t>();
  RegisterType<uint64_t>();
  RegisterType<float>();
  RegisterType<double>();

  // Half precision types
  RegisterType<value::half>();
  RegisterType<value::half2>();
  RegisterType<value::half3>();
  RegisterType<value::half4>();

  // Float vector types
  RegisterType<value::float2>();
  RegisterType<value::float3>();
  RegisterType<value::float4>();

  // Double vector types  
  RegisterType<value::double2>();
  RegisterType<value::double3>();
  RegisterType<value::double4>();

  // Integer vector types
  RegisterType<value::int2>();
  RegisterType<value::int3>();
  RegisterType<value::int4>();
  RegisterType<value::uint2>();
  RegisterType<value::uint3>();
  RegisterType<value::uint4>();

  // Quaternions
  RegisterType<value::quath>();
  RegisterType<value::quatf>();
  RegisterType<value::quatd>();

  // Colors
  RegisterType<value::color3h>();
  RegisterType<value::color3f>();
  RegisterType<value::color3d>();
  RegisterType<value::color4h>();
  RegisterType<value::color4f>();
  RegisterType<value::color4d>();

  // Texture coordinates
  RegisterType<value::texcoord2h>();
  RegisterType<value::texcoord2f>();
  RegisterType<value::texcoord2d>();
  RegisterType<value::texcoord3h>();
  RegisterType<value::texcoord3f>();
  RegisterType<value::texcoord3d>();

  // Points
  RegisterType<value::point3h>();
  RegisterType<value::point3f>();
  RegisterType<value::point3d>();

  // Normals
  RegisterType<value::normal3h>();
  RegisterType<value::normal3f>();
  RegisterType<value::normal3d>();

  // Vectors
  RegisterType<value::vector3h>();
  RegisterType<value::vector3f>();
  RegisterType<value::vector3d>();

  // Matrices
  RegisterType<value::matrix2f>();
  RegisterType<value::matrix3f>();
  RegisterType<value::matrix4f>();
  RegisterType<value::matrix2d>();
  RegisterType<value::matrix3d>();
  RegisterType<value::matrix4d>();

  // String types
  RegisterType<std::string>();
  RegisterType<value::token>();
  RegisterType<value::StringData>();

  // USD specific types
  RegisterType<Reference>();
  RegisterType<Payload>();
  RegisterType<Path>();
  RegisterType<value::AssetPath>();

  _parsers_registered = true;
}

bool RuntimeArrayParser::ParseArrayByTypeId(AsciiParser* parser, uint32_t type_id, void* result_ptr) {
  RegisterAllParsers(); // Ensure parsers are registered

  auto it = _array_parsers.find(type_id);
  if (it != _array_parsers.end()) {
    return it->second(parser, result_ptr);
  }

  // Type not found
  return false;
}

bool RuntimeArrayParser::ParseOptionalArrayByTypeId(AsciiParser* parser, uint32_t type_id, void* result_ptr) {
  RegisterAllParsers(); // Ensure parsers are registered

  auto it = _optional_array_parsers.find(type_id);
  if (it != _optional_array_parsers.end()) {
    return it->second(parser, result_ptr);
  }

  // Type not found
  return false;
}

uint32_t RuntimeArrayParser::GetArrayTypeId(uint32_t base_type_id) {
  return base_type_id | value::TYPE_ID_1D_ARRAY_BIT;
}

bool RuntimeArrayParser::CreateValueFromArray(uint32_t base_type_id, void* array_ptr, value::Value* out_value) {
  if (!array_ptr || !out_value) {
    return false;
  }

  // Use macro to handle all types systematically
  #define CREATE_VALUE_FROM_ARRAY(type) \
    if (base_type_id == value::TypeTraits<type>::type_id()) { \
      auto* typed_array = static_cast<std::vector<type>*>(array_ptr); \
      *out_value = value::Value(*typed_array); \
      return true; \
    }

  // Handle all supported types
  CREATE_VALUE_FROM_ARRAY(bool)
  CREATE_VALUE_FROM_ARRAY(int32_t)
  CREATE_VALUE_FROM_ARRAY(uint32_t)
  CREATE_VALUE_FROM_ARRAY(int64_t)
  CREATE_VALUE_FROM_ARRAY(uint64_t)
  CREATE_VALUE_FROM_ARRAY(float)
  CREATE_VALUE_FROM_ARRAY(double)
  CREATE_VALUE_FROM_ARRAY(value::half)
  CREATE_VALUE_FROM_ARRAY(value::half2)
  CREATE_VALUE_FROM_ARRAY(value::half3)
  CREATE_VALUE_FROM_ARRAY(value::half4)
  CREATE_VALUE_FROM_ARRAY(value::float2)
  CREATE_VALUE_FROM_ARRAY(value::float3)
  CREATE_VALUE_FROM_ARRAY(value::float4)
  CREATE_VALUE_FROM_ARRAY(value::double2)
  CREATE_VALUE_FROM_ARRAY(value::double3)
  CREATE_VALUE_FROM_ARRAY(value::double4)
  CREATE_VALUE_FROM_ARRAY(value::int2)
  CREATE_VALUE_FROM_ARRAY(value::int3)
  CREATE_VALUE_FROM_ARRAY(value::int4)
  CREATE_VALUE_FROM_ARRAY(value::uint2)
  CREATE_VALUE_FROM_ARRAY(value::uint3)
  CREATE_VALUE_FROM_ARRAY(value::uint4)
  CREATE_VALUE_FROM_ARRAY(value::quath)
  CREATE_VALUE_FROM_ARRAY(value::quatf)
  CREATE_VALUE_FROM_ARRAY(value::quatd)
  CREATE_VALUE_FROM_ARRAY(value::color3h)
  CREATE_VALUE_FROM_ARRAY(value::color3f)
  CREATE_VALUE_FROM_ARRAY(value::color3d)
  CREATE_VALUE_FROM_ARRAY(value::color4h)
  CREATE_VALUE_FROM_ARRAY(value::color4f)
  CREATE_VALUE_FROM_ARRAY(value::color4d)
  CREATE_VALUE_FROM_ARRAY(value::texcoord2h)
  CREATE_VALUE_FROM_ARRAY(value::texcoord2f)
  CREATE_VALUE_FROM_ARRAY(value::texcoord2d)
  CREATE_VALUE_FROM_ARRAY(value::texcoord3h)
  CREATE_VALUE_FROM_ARRAY(value::texcoord3f)
  CREATE_VALUE_FROM_ARRAY(value::texcoord3d)
  CREATE_VALUE_FROM_ARRAY(value::point3h)
  CREATE_VALUE_FROM_ARRAY(value::point3f)
  CREATE_VALUE_FROM_ARRAY(value::point3d)
  CREATE_VALUE_FROM_ARRAY(value::normal3h)
  CREATE_VALUE_FROM_ARRAY(value::normal3f)
  CREATE_VALUE_FROM_ARRAY(value::normal3d)
  CREATE_VALUE_FROM_ARRAY(value::vector3h)
  CREATE_VALUE_FROM_ARRAY(value::vector3f)
  CREATE_VALUE_FROM_ARRAY(value::vector3d)
  CREATE_VALUE_FROM_ARRAY(value::matrix2f)
  CREATE_VALUE_FROM_ARRAY(value::matrix3f)
  CREATE_VALUE_FROM_ARRAY(value::matrix4f)
  CREATE_VALUE_FROM_ARRAY(value::matrix2d)
  CREATE_VALUE_FROM_ARRAY(value::matrix3d)
  CREATE_VALUE_FROM_ARRAY(value::matrix4d)
  CREATE_VALUE_FROM_ARRAY(std::string)
  CREATE_VALUE_FROM_ARRAY(value::token)
  CREATE_VALUE_FROM_ARRAY(value::StringData)
  CREATE_VALUE_FROM_ARRAY(Reference)
  CREATE_VALUE_FROM_ARRAY(Payload)
  CREATE_VALUE_FROM_ARRAY(Path)
  CREATE_VALUE_FROM_ARRAY(value::AssetPath)

  #undef CREATE_VALUE_FROM_ARRAY

  // Type not found
  return false;
}

} // namespace ascii
} // namespace tinyusdz
