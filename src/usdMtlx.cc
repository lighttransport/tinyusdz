
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/pugixml.hpp"
//#include "external/jsonhpp/nlohmann/json.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <sstream>

#include "io-util.hh"
#include "tiny-format.hh"
#include "ascii-parser.hh" // To parse color3f value
#include "common-macros.inc"
#include "usdMtlx.hh"

#define PushError(msg) do { \
  if (err) { \
    (*err) += msg; \
  } \
} while(0);


namespace tinyusdz {

// defined in ascii-parser-base-types.cc
namespace ascii {

extern template bool AsciiParser::SepBy1BasicType<float>(const char sep, std::vector<float> *ret);

} // namespace ascii

namespace detail {

bool is_supported_type(const std::string &typeName);

bool is_supported_type(const std::string &typeName) {

  if (typeName.compare("integer") == 0) return true;
  if (typeName.compare("boolean") == 0) return true;
  if (typeName.compare("float") == 0) return true;
  if (typeName.compare("color3") == 0) return true;
  if (typeName.compare("color4") == 0) return true;
  if (typeName.compare("vector2") == 0) return true;
  if (typeName.compare("vector3") == 0) return true;
  if (typeName.compare("vector4") == 0) return true;
  if (typeName.compare("matrix33") == 0) return true;
  if (typeName.compare("matrix44") == 0) return true;
  if (typeName.compare("string") == 0) return true;
  if (typeName.compare("filename") == 0) return true;

  if (typeName.compare("integerarray") == 0) return true;
  if (typeName.compare("floatarray") == 0) return true;
  if (typeName.compare("vector2array") == 0) return true;
  if (typeName.compare("vector3array") == 0) return true;
  if (typeName.compare("vector4array") == 0) return true;
  if (typeName.compare("color3array") == 0) return true;
  if (typeName.compare("color4array") == 0) return true;
  if (typeName.compare("stringarray") == 0) return true;

  // No matrixarray

  // TODO
  //if (typeName.compare("color") == 0) return true;
  //if (typeName.compare("geomname") == 0) return true;
  //if (typeName.compare("geomnamearray") == 0) return true;
  
  return false;
}

template<typename T>
bool ParseValue(tinyusdz::ascii::AsciiParser &parser, T &ret, std::string *err);

template<>
bool ParseValue<int>(tinyusdz::ascii::AsciiParser &parser, int &ret, std::string *err) {

  int val;
  if (!parser.ReadBasicType(&val)) {
    PUSH_ERROR_AND_RETURN(fmt::format("Failed to parse a value of type `{}`", value::TypeTraits<int>::type_name()));
  }

  ret = val;

  return true;
}

template<>
bool ParseValue<bool>(tinyusdz::ascii::AsciiParser &parser, bool &ret, std::string *err) {

  bool val;
  if (!parser.ReadBasicType(&val)) {
    PUSH_ERROR_AND_RETURN(fmt::format("Failed to parse a value of type `{}`", value::TypeTraits<bool>::type_name()));
  }

  ret = val;

  return true;
}

template<>
bool ParseValue<float>(tinyusdz::ascii::AsciiParser &parser, float &ret, std::string *err) {

  float val;
  if (!parser.ReadBasicType(&val)) {
    PUSH_ERROR_AND_RETURN(fmt::format("Failed to parse a value of type `{}`", value::TypeTraits<float>::type_name()));
  }

  ret = val;

  return true;
}

template<>
bool ParseValue<value::float2>(tinyusdz::ascii::AsciiParser &parser, value::float2 &ret, std::string *err) {


  std::vector<float> values;
  if (!parser.SepBy1BasicType(',', &values)) {
    PUSH_ERROR_AND_RETURN(fmt::format("Failed to parse a value of type `{}`", value::TypeTraits<value::float3>::type_name()));
  }

  if (values.size() != 2) {
    PUSH_ERROR_AND_RETURN(fmt::format("type `{}` expects the number of elements is 2, but got {}",
       value::TypeTraits<value::float2>::type_name(), values.size()));
  }

  ret[0] = values[0];
  ret[1] = values[1];

  return true;
}

template<>
bool ParseValue<value::float3>(tinyusdz::ascii::AsciiParser &parser, value::float3 &ret, std::string *err) {


  std::vector<float> values;
  if (!parser.SepBy1BasicType(',', &values)) {
    PUSH_ERROR_AND_RETURN(fmt::format("Failed to parse a value of type `{}`", value::TypeTraits<value::float3>::type_name()));
  }

  if (values.size() != 3) {
    PUSH_ERROR_AND_RETURN(fmt::format("type `{}` expects the number of elements is 3, but got {}",
       value::TypeTraits<value::float3>::type_name(), values.size()));
  }

  ret[0] = values[0];
  ret[1] = values[1];
  ret[2] = values[2];

  return true;
}

template<>
bool ParseValue<value::float4>(tinyusdz::ascii::AsciiParser &parser, value::float4 &ret, std::string *err) {

  std::vector<float> values;
  if (!parser.SepBy1BasicType(',', &values)) {
    PUSH_ERROR_AND_RETURN(fmt::format("Failed to parse a value of type `{}`", value::TypeTraits<value::float4>::type_name()));
  }

  if (values.size() != 4) {
    PUSH_ERROR_AND_RETURN(fmt::format("type `{}` expects the number of elements is 4, but got {}",
       value::TypeTraits<value::float4>::type_name(), values.size()));
  }

  ret[0] = values[0];
  ret[1] = values[1];
  ret[2] = values[2];
  ret[3] = values[3];

  return true;
}

///
/// For MaterialX XML.
/// Parse string representation of Attribute value.
/// e.g. "0.0, 1.1" for vector2 type
/// NOTE: no parenthesis('(', '[') for vector and array type.
///
/// @param[in] typeName typeName(e.g. "vector2")
/// @param[in] str Ascii representation of value.
/// @param[out] value Ascii representation of value.
/// @param[out] err Parse error message when returning false.
///
///
/// Supported data type: boolean, float, color3, color4, vector2, vector3,
/// vector4, matrix33, matrix44, string, filename, integerarray, floatarray,
/// color3array, color4array, vector2array, vector3array, vector4array,
/// stringarray. Unsupported data type: geomname, geomnamearray
///
bool ParseMaterialXValue(const std::string &typeName, const std::string &str,
                           value::Value *value, std::string *err);


bool ParseMaterialXValue(const std::string &typeName, const std::string &str,
                           value::Value *value, std::string *err) {

  (void)value;

  if (!is_supported_type(typeName)) {
    PUSH_ERROR_AND_RETURN(fmt::format("Invalid/unsupported type: {}", typeName));
  }

  tinyusdz::StreamReader sr(reinterpret_cast<const uint8_t *>(str.data()), str.size(), /* swap endian */ false);
  tinyusdz::ascii::AsciiParser parser(&sr);

  if (typeName.compare("integer") == 0) {
    int val;
    if (!ParseValue(parser, val, err)) {
      return false;
    }
  } else if (typeName.compare("boolean") == 0) {
    bool val;
    if (!ParseValue(parser, val, err)) {
      return false;
    }
  } else if (typeName.compare("vector2") == 0) {
    value::float2 val;
    if (!ParseValue(parser, val, err)) {
      return false;
    }
  } else if (typeName.compare("vector3") == 0) {
    value::float3 val;
    if (!ParseValue(parser, val, err)) {
      return false;
    }
  } else if (typeName.compare("vector4") == 0) {
    value::float4 val;
    if (!ParseValue(parser, val, err)) {
      return false;
    }
  } else {
    PUSH_ERROR_AND_RETURN("TODO: " + typeName);
  }

  // TODO
  return false;
}


} // namespace detail


bool ReadMaterialXFromString(const std::string &str, const std::string &asset_path, MtlxModel *mtlx, std::string *err) {


  pugi::xml_document doc;
  pugi::xml_parse_result result = doc.load_string(str.c_str());
  if (!result) {
    std::string msg(result.description());
    PUSH_ERROR_AND_RETURN("Failed to parse XML: " + msg);
  }

  pugi::xml_node root = doc.child("materialx");
  if (!root) {
    PUSH_ERROR_AND_RETURN("<materialx> tag not found: " + asset_path);
  }

  // Attributes for a <materialx> element:
  //
  // - [x] version(string, required)
  //   - TODO: validate version string
  // - [ ] cms(string, optional)
  // - [ ] cmsconfig(filename, optional)
  // - [ ] colorspace(string, optional)
  // - [ ] namespace(string, optional)

  pugi::xml_attribute ver_attr = root.attribute("version");
  if (!ver_attr) {
    PUSH_ERROR_AND_RETURN("version attribute not found in <materialx>:" + asset_path);
  }

  //value::Value v;
  //if (!detail::ParseMaterialXValue("float", ver_attr.as_string(), &v, err)) {
  //  return false;
  //}
  //DCOUT("version = " << ver_attr.as_string());


  pugi::xml_attribute cms_attr = root.attribute("cms");
  if (cms_attr) {
  }

  pugi::xml_attribute cmsconfig_attr = root.attribute("cms");
  if (cmsconfig_attr) {
  }
  pugi::xml_attribute colorspace_attr = root.attribute("colorspace");
  if (colorspace_attr) {
  }

  pugi::xml_attribute namespace_attr = root.attribute("namespace");
  if (namespace_attr) {
  }


  // TODO
  (void)mtlx;

  return true;
}

bool ReadMaterialXFromFile(const AssetResolutionResolver &resolver, const std::string &asset_path, MtlxModel *mtlx, std::string *err) {

  std::string filepath = resolver.resolve(asset_path);
  if (filepath.empty()) {
    PUSH_ERROR_AND_RETURN("Asset not found: " + asset_path);
  }

  // up to 16MB xml
  size_t max_bytes = 1024 * 1024 * 16;

  std::vector<uint8_t> data;
  if (!io::ReadWholeFile(&data, err, filepath, max_bytes, /* userdata */nullptr)) {
    PUSH_ERROR_AND_RETURN("Read file failed.");
  }

  std::string str(reinterpret_cast<const char *>(&data[0]), data.size());
  return ReadMaterialXFromString(str, asset_path, mtlx, err);
}



//} // namespace usdMtlx
} // namespace tinyusdz
