//
// Refactored implementation of ParseTimeSampleValueOfArrayType using runtime dispatch
// This replaces the template-heavy PARSE_TYPE macro approach
//

#include <sstream>
#include "ascii-parser.hh"
#include "ascii-parser-runtime-dispatch.hh"
#include "value-types.hh"

namespace tinyusdz {
namespace ascii {

bool AsciiParser::ParseTimeSampleValueOfArrayTypeRuntimeDispatch(const uint32_t type_id, value::Value *result) {
  if (!result) {
    return false;
  }

  if (MaybeNone()) {
    (*result) = value::ValueBlock();
    return true;
  }

  // Create temporary storage for the parsed array
  // We'll use a type-erased approach to handle different vector types
  bool parse_success = false;
  value::Value val;

  // Use a lambda to handle the parsing and value creation
  auto parse_and_create_value = [&](uint32_t base_type_id) -> bool {
    // Allocate storage for the specific vector type based on type ID
    void* array_storage = nullptr;
    
    #define ALLOCATE_AND_PARSE(type) \
      if (base_type_id == value::TypeTraits<type>::type_id()) { \
        auto* typed_vector = new std::vector<type>(); \
        array_storage = typed_vector; \
        uint32_t array_type_id = RuntimeArrayParser::GetArrayTypeId(base_type_id); \
        if (RuntimeArrayParser::ParseArrayByTypeId(this, array_type_id, array_storage)) { \
          if (RuntimeArrayParser::CreateValueFromArray(base_type_id, array_storage, &val)) { \
            parse_success = true; \
          } \
        } \
        delete typed_vector; \
        return parse_success; \
      }

    // Handle all the types that were in the original PARSE_TYPE macro chain
    ALLOCATE_AND_PARSE(value::AssetPath)
    ALLOCATE_AND_PARSE(value::token)
    ALLOCATE_AND_PARSE(std::string)
    ALLOCATE_AND_PARSE(float)
    ALLOCATE_AND_PARSE(int32_t)
    ALLOCATE_AND_PARSE(uint32_t)
    ALLOCATE_AND_PARSE(int64_t)
    ALLOCATE_AND_PARSE(uint64_t)
    ALLOCATE_AND_PARSE(value::half)
    ALLOCATE_AND_PARSE(value::half2)
    ALLOCATE_AND_PARSE(value::half3)
    ALLOCATE_AND_PARSE(value::half4)
    ALLOCATE_AND_PARSE(value::float2)
    ALLOCATE_AND_PARSE(value::float3)
    ALLOCATE_AND_PARSE(value::float4)
    ALLOCATE_AND_PARSE(double)
    ALLOCATE_AND_PARSE(value::double2)
    ALLOCATE_AND_PARSE(value::double3)
    ALLOCATE_AND_PARSE(value::double4)
    ALLOCATE_AND_PARSE(value::quath)
    ALLOCATE_AND_PARSE(value::quatf)
    ALLOCATE_AND_PARSE(value::quatd)
    ALLOCATE_AND_PARSE(value::color3f)
    ALLOCATE_AND_PARSE(value::color4f)
    ALLOCATE_AND_PARSE(value::color3d)
    ALLOCATE_AND_PARSE(value::color4d)
    ALLOCATE_AND_PARSE(value::vector3f)
    ALLOCATE_AND_PARSE(value::normal3f)
    ALLOCATE_AND_PARSE(value::point3f)
    ALLOCATE_AND_PARSE(value::texcoord2f)
    ALLOCATE_AND_PARSE(value::texcoord3f)
    ALLOCATE_AND_PARSE(value::matrix2f)
    ALLOCATE_AND_PARSE(value::matrix3f)
    ALLOCATE_AND_PARSE(value::matrix4f)
    ALLOCATE_AND_PARSE(value::matrix2d)
    ALLOCATE_AND_PARSE(value::matrix3d)
    ALLOCATE_AND_PARSE(value::matrix4d)
    ALLOCATE_AND_PARSE(value::color3h)
    ALLOCATE_AND_PARSE(value::color4h)
    ALLOCATE_AND_PARSE(value::vector3h)
    ALLOCATE_AND_PARSE(value::vector3d)
    ALLOCATE_AND_PARSE(value::normal3h)
    ALLOCATE_AND_PARSE(value::normal3d)
    ALLOCATE_AND_PARSE(value::point3h)
    ALLOCATE_AND_PARSE(value::point3d)
    ALLOCATE_AND_PARSE(value::texcoord2h)
    ALLOCATE_AND_PARSE(value::texcoord2d)
    ALLOCATE_AND_PARSE(value::texcoord3h)
    ALLOCATE_AND_PARSE(value::texcoord3d)
    ALLOCATE_AND_PARSE(Reference)
    ALLOCATE_AND_PARSE(Payload)
    ALLOCATE_AND_PARSE(Path)
    
    #undef ALLOCATE_AND_PARSE

    return false; // Type not handled
  };

  if (parse_and_create_value(type_id)) {
    *result = val;
    return true;
  }

  PUSH_ERROR_AND_RETURN("Failed to parse value with requested type `" + value::GetTypeName(type_id) + "[]`");
}

} // namespace ascii
} // namespace tinyusdz
