// SPDX-License-Identifier: Apache 2.0
// Copyright 2023 - Present, Light Transport Entertainment, Inc.

#include <sstream>

#include "usdMtlx.hh"
#include "usdShade.hh"

// Use built-in MaterialX parser instead of pugixml
#include "mtlx-usd-adapter.hh"

#if defined(TINYUSDZ_USE_USDMTLX)

// ============================================================================
// Configuration flags for MaterialX support
// ============================================================================
// Currently only Blender-style OpenPBR + NodeGraph import is actively used.
// Other shader types (UsdPreviewSurface, StandardSurface) export paths are
// disabled until needed. Enable these flags to re-enable those code paths.
// ============================================================================
#define TINYUSDZ_MTLX_ENABLE_USDPREVIEWSURFACE_EXPORT 1
#define TINYUSDZ_MTLX_ENABLE_STANDARDSURFACE_EXPORT 1

#include "ascii-parser.hh"  // To parse color3f value
#include "common-macros.inc"
#include "io-util.hh"
#include "pprint-enum.hh"
#include "str-util.hh"  // For dragonbox-based dtos()
#include "tiny-format.hh"
#include "value-pprint.hh"

// Use dragonbox-based dtos from str-util.hh for shortest representation
// No need for local dtos() or float_to_xml_string() - dtos() already
// produces the shortest round-trip-correct representation without trailing zeros

#define PushWarn(msg) \
  do {                \
    if (warn) {       \
      (*warn) += msg; \
    }                 \
  } while (0);

#define PushError(msg) \
  do {                 \
    if (err) {         \
      (*err) += msg;   \
    }                  \
  } while (0);

namespace tinyusdz {

// defined in ascii-parser-base-types.cc
namespace ascii {

extern template bool AsciiParser::SepBy1BasicType<float>(
    const char sep, std::vector<float> *ret);

}  // namespace ascii

namespace detail {

template <typename T>
Property MakeProperty(const T &value) {
  Attribute attr(value);
  Property prop(attr, /* custom */ false);

  return prop;
}

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

  // if (typeName.compare("color") == 0) return true;
  // if (typeName.compare("geomname") == 0) return true;
  // if (typeName.compare("geomnamearray") == 0) return true;

  return false;
}

template <typename T>
bool ParseValue(tinyusdz::ascii::AsciiParser &parser, T &ret, std::string *err);

template <>
bool ParseValue<int>(tinyusdz::ascii::AsciiParser &parser, int &ret,
                     std::string *err) {
  int val;
  if (!parser.ReadBasicType(&val)) {
    PUSH_ERROR_AND_RETURN(fmt::format("Failed to parse a value of type `{}`",
                                      value::TypeTraits<int>::type_name()));
  }

  ret = val;

  return true;
}

template <>
bool ParseValue<bool>(tinyusdz::ascii::AsciiParser &parser, bool &ret,
                      std::string *err) {
  bool val;
  if (!parser.ReadBasicType(&val)) {
    PUSH_ERROR_AND_RETURN(fmt::format("Failed to parse a value of type `{}`",
                                      value::TypeTraits<bool>::type_name()));
  }

  ret = val;

  return true;
}

template <>
bool ParseValue<float>(tinyusdz::ascii::AsciiParser &parser, float &ret,
                       std::string *err) {
  float val;
  if (!parser.ReadBasicType(&val)) {
    PUSH_ERROR_AND_RETURN(fmt::format("Failed to parse a value of type `{}`",
                                      value::TypeTraits<float>::type_name()));
  }

  ret = val;

  return true;
}

template <>
bool ParseValue<std::string>(tinyusdz::ascii::AsciiParser &parser,
                             std::string &ret, std::string *err) {
  std::string val;
  if (!parser.ReadBasicType(&val)) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Failed to parse a value of type `{}`",
                    value::TypeTraits<std::string>::type_name()));
  }

  ret = val;

  return true;
}

template <>
bool ParseValue<value::float2>(tinyusdz::ascii::AsciiParser &parser,
                               value::float2 &ret, std::string *err) {
  std::vector<float> values;
  if (!parser.SepBy1BasicType(',', &values)) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Failed to parse a value of type `{}`",
                    value::TypeTraits<value::float3>::type_name()));
  }

  if (values.size() != 2) {
    PUSH_ERROR_AND_RETURN(fmt::format(
        "type `{}` expects the number of elements is 2, but got {}",
        value::TypeTraits<value::float2>::type_name(), values.size()));
  }

  ret[0] = values[0];
  ret[1] = values[1];

  return true;
}

template <>
bool ParseValue<value::float3>(tinyusdz::ascii::AsciiParser &parser,
                               value::float3 &ret, std::string *err) {
  std::vector<float> values;
  if (!parser.SepBy1BasicType(',', &values)) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Failed to parse a value of type `{}`",
                    value::TypeTraits<value::float3>::type_name()));
  }

  if (values.size() != 3) {
    PUSH_ERROR_AND_RETURN(fmt::format(
        "type `{}` expects the number of elements is 3, but got {}",
        value::TypeTraits<value::float3>::type_name(), values.size()));
  }

  ret[0] = values[0];
  ret[1] = values[1];
  ret[2] = values[2];

  return true;
}

template <>
bool ParseValue<value::vector3f>(tinyusdz::ascii::AsciiParser &parser,
                                 value::vector3f &ret, std::string *err) {
  std::vector<float> values;
  if (!parser.SepBy1BasicType(',', &values)) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Failed to parse a value of type `{}`",
                    value::TypeTraits<value::vector3f>::type_name()));
  }

  if (values.size() != 3) {
    PUSH_ERROR_AND_RETURN(fmt::format(
        "type `{}` expects the number of elements is 3, but got {}",
        value::TypeTraits<value::vector3f>::type_name(), values.size()));
  }

  ret[0] = values[0];
  ret[1] = values[1];
  ret[2] = values[2];

  return true;
}

template <>
bool ParseValue<value::normal3f>(tinyusdz::ascii::AsciiParser &parser,
                                 value::normal3f &ret, std::string *err) {
  std::vector<float> values;
  if (!parser.SepBy1BasicType(',', &values)) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Failed to parse a value of type `{}`",
                    value::TypeTraits<value::normal3f>::type_name()));
  }

  if (values.size() != 3) {
    PUSH_ERROR_AND_RETURN(fmt::format(
        "type `{}` expects the number of elements is 3, but got {}",
        value::TypeTraits<value::normal3f>::type_name(), values.size()));
  }

  ret[0] = values[0];
  ret[1] = values[1];
  ret[2] = values[2];

  return true;
}


template <>
bool ParseValue<value::color3f>(tinyusdz::ascii::AsciiParser &parser,
                                 value::color3f &ret, std::string *err) {
  std::vector<float> values;
  if (!parser.SepBy1BasicType(',', &values)) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Failed to parse a value of type `{}`",
                    value::TypeTraits<value::color3f>::type_name()));
  }

  if (values.size() != 3) {
    PUSH_ERROR_AND_RETURN(fmt::format(
        "type `{}` expects the number of elements is 3, but got {}",
        value::TypeTraits<value::color3f>::type_name(), values.size()));
  }

  ret[0] = values[0];
  ret[1] = values[1];
  ret[2] = values[2];

  return true;
}

template <>
bool ParseValue<value::float4>(tinyusdz::ascii::AsciiParser &parser,
                               value::float4 &ret, std::string *err) {
  std::vector<float> values;
  if (!parser.SepBy1BasicType(',', &values)) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Failed to parse a value of type `{}`",
                    value::TypeTraits<value::float4>::type_name()));
  }

  if (values.size() != 4) {
    PUSH_ERROR_AND_RETURN(fmt::format(
        "type `{}` expects the number of elements is 4, but got {}",
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
  if (!value) {
    PUSH_ERROR_AND_RETURN("value is nullptr");
  }

  if (!is_supported_type(typeName)) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Invalid/unsupported type: {}", typeName));
  }

  tinyusdz::StreamReader sr(reinterpret_cast<const uint8_t *>(str.data()),
                            str.size(), /* swap endian */ false);
  tinyusdz::ascii::AsciiParser parser(&sr);

  if (typeName.compare("integer") == 0) {
    int val;
    if (!ParseValue(parser, val, err)) {
      return false;
    }
    (*value) = val;
  } else if (typeName.compare("boolean") == 0) {
    bool val;
    if (!ParseValue(parser, val, err)) {
      return false;
    }
    (*value) = val;
  } else if (typeName.compare("float") == 0) {
    float val;
    if (!ParseValue(parser, val, err)) {
      return false;
    }
    (*value) = val;
  } else if (typeName.compare("string") == 0 || typeName.compare("filename") == 0) {
    // Strings are already unquoted from XML attribute parsing
    (*value) = str;
  } else if (typeName.compare("vector2") == 0) {
    value::float2 val;
    if (!ParseValue(parser, val, err)) {
      return false;
    }
    (*value) = val;
  } else if (typeName.compare("color3") == 0) {
    value::color3f val;
    if (!ParseValue(parser, val, err)) {
      return false;
    }
    (*value) = val;
  } else if (typeName.compare("vector3") == 0) {
    value::float3 val;
    if (!ParseValue(parser, val, err)) {
      return false;
    }
    (*value) = val;
  } else if (typeName.compare("color4") == 0) {
    value::float4 val;
    if (!ParseValue(parser, val, err)) {
      return false;
    }
    (*value) = val;
  } else if (typeName.compare("vector4") == 0) {
    value::float4 val;
    if (!ParseValue(parser, val, err)) {
      return false;
    }
    (*value) = val;
  } else if (typeName.compare("matrix33") == 0) {
    std::vector<float> values;
    if (!parser.SepBy1BasicType(',', &values)) {
      PUSH_ERROR_AND_RETURN("Failed to parse a value of type `matrix33`");
    }
    if (values.size() != 9) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "type `matrix33` expects 9 elements, but got {}", values.size()));
    }
    value::matrix3f val;
    memcpy(&val.m[0][0], values.data(), sizeof(float) * 9);
    (*value) = val;
  } else if (typeName.compare("matrix44") == 0) {
    std::vector<float> values;
    if (!parser.SepBy1BasicType(',', &values)) {
      PUSH_ERROR_AND_RETURN("Failed to parse a value of type `matrix44`");
    }
    if (values.size() != 16) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "type `matrix44` expects 16 elements, but got {}", values.size()));
    }
    value::matrix4f val;
    memcpy(&val.m[0][0], values.data(), sizeof(float) * 16);
    (*value) = val;
  } else if (typeName.compare("integerarray") == 0) {
    // Parse comma-separated integers via float parser then convert
    std::vector<float> fvalues;
    if (!parser.SepBy1BasicType(',', &fvalues)) {
      PUSH_ERROR_AND_RETURN("Failed to parse a value of type `integerarray`");
    }
    std::vector<int> values(fvalues.size());
    for (size_t i = 0; i < fvalues.size(); i++) {
      values[i] = int(fvalues[i]);
    }
    (*value) = values;
  } else if (typeName.compare("floatarray") == 0) {
    std::vector<float> values;
    if (!parser.SepBy1BasicType(',', &values)) {
      PUSH_ERROR_AND_RETURN("Failed to parse a value of type `floatarray`");
    }
    (*value) = values;
  } else if (typeName.compare("color3array") == 0 ||
             typeName.compare("vector3array") == 0) {
    std::vector<float> values;
    if (!parser.SepBy1BasicType(',', &values)) {
      PUSH_ERROR_AND_RETURN(fmt::format("Failed to parse a value of type `{}`", typeName));
    }
    if (values.size() % 3 != 0) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "type `{}` expects element count divisible by 3, but got {}", typeName, values.size()));
    }
    if (typeName.compare("color3array") == 0) {
      std::vector<value::color3f> arr(values.size() / 3);
      memcpy(arr.data(), values.data(), sizeof(float) * values.size());
      (*value) = arr;
    } else {
      std::vector<value::float3> arr(values.size() / 3);
      memcpy(arr.data(), values.data(), sizeof(float) * values.size());
      (*value) = arr;
    }
  } else if (typeName.compare("color4array") == 0 ||
             typeName.compare("vector4array") == 0) {
    std::vector<float> values;
    if (!parser.SepBy1BasicType(',', &values)) {
      PUSH_ERROR_AND_RETURN(fmt::format("Failed to parse a value of type `{}`", typeName));
    }
    if (values.size() % 4 != 0) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "type `{}` expects element count divisible by 4, but got {}", typeName, values.size()));
    }
    if (typeName.compare("color4array") == 0) {
      std::vector<value::color4f> arr(values.size() / 4);
      memcpy(arr.data(), values.data(), sizeof(float) * values.size());
      (*value) = arr;
    } else {
      std::vector<value::float4> arr(values.size() / 4);
      memcpy(arr.data(), values.data(), sizeof(float) * values.size());
      (*value) = arr;
    }
  } else if (typeName.compare("vector2array") == 0) {
    std::vector<float> values;
    if (!parser.SepBy1BasicType(',', &values)) {
      PUSH_ERROR_AND_RETURN("Failed to parse a value of type `vector2array`");
    }
    if (values.size() % 2 != 0) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "type `vector2array` expects element count divisible by 2, but got {}", values.size()));
    }
    std::vector<value::float2> arr(values.size() / 2);
    memcpy(arr.data(), values.data(), sizeof(float) * values.size());
    (*value) = arr;
  } else if (typeName.compare("stringarray") == 0) {
    // MaterialX string arrays are comma-separated strings
    // For now, store as a single string (arrays in XML attributes are rare)
    (*value) = str;
  } else {
    PUSH_ERROR_AND_RETURN("Unsupported type: " + typeName);
  }

  return true;
}

template <typename T>
bool ParseMaterialXValue(const std::string &str, T *value, std::string *err) {
  tinyusdz::StreamReader sr(reinterpret_cast<const uint8_t *>(str.data()),
                            str.size(), /* swap endian */ false);
  tinyusdz::ascii::AsciiParser parser(&sr);

  T val{};

  if (!ParseValue(parser, val, err)) {
    return false;
  }

  (*value) = val;
  return true;
}

// Specialization for std::string - MaterialX XML attributes are already unquoted
template <>
bool ParseMaterialXValue<std::string>(const std::string &str, std::string *value, std::string *err) {
  (void)err;
  (*value) = str;
  return true;
}

template <typename T>
std::string to_xml_string(const T &val);

// Forward declaration
static bool SerializeNodeGraphs(const std::map<std::string, PrimSpec> &nodegraphs,
                                std::stringstream &ss, std::string *warn, std::string *err);

template <>
std::string to_xml_string(const float &val) {
  return dtos(val);
}

template <>
std::string to_xml_string(const int &val) {
  return std::to_string(val);
}

template <>
std::string to_xml_string(const bool &val) {
  return val ? "true" : "false";
}

template <>
std::string to_xml_string(const value::color3f &val) {
  return dtos(val.r) + ", " + dtos(val.g) + ", " + dtos(val.b);
}

template <>
std::string to_xml_string(const value::normal3f &val) {
  return dtos(val.x) + ", " + dtos(val.y) + ", " + dtos(val.z);
}

template <typename T>
bool SerializeAttribute(const std::string &attr_name,
                        const TypedAttributeWithFallback<Animatable<T>> &attr,
                        std::string &value_str, std::string *err) {
  std::stringstream value_ss;

  if (attr.is_connection()) {
    PUSH_ERROR_AND_RETURN(fmt::format("TODO: connection attribute"));
  } else if (attr.is_blocked()) {
    // do nothing
    value_str = "";
    return true;
  } else {
    const Animatable<T> &animatable_value = attr.get_value();
    if (animatable_value.has_default()) {
      T value;
      if (animatable_value.get_scalar(&value)) {
        value_ss << "\"" << to_xml_string(value) << "\"";
      } else {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Failed to get the value at default time of `{}`", attr_name));
      }
    } else { 
      // no time-varying(timesamples) attribute in MaterialX.
      
      PUSH_ERROR_AND_RETURN(
          fmt::format("Failed to get the value of `{}`", attr_name));
    }
  }

  value_str = value_ss.str();
  return true;
}

#if TINYUSDZ_MTLX_ENABLE_USDPREVIEWSURFACE_EXPORT
static bool WriteMaterialXToString(const MtlxUsdPreviewSurface &shader,
                                   const std::string &shader_name,
                                   const std::vector<MtlxShaderConnection> &connections,
                                   const std::map<std::string, PrimSpec> &nodegraphs,
                                   const std::string &colorspace,
                                   std::string &xml_str, std::string *warn,
                                   std::string *err) {
  (void)warn;

  std::stringstream ss;

  std::string node_name = shader_name.empty() ? "SR_default" : shader_name;
  // Use provided colorspace or default to lin_rec709
  std::string cs = colorspace.empty() ? "lin_rec709" : colorspace;

  ss << "<?xml version=\"1.0\"?>\n";
  ss << "<materialx version=\"1.38\" colorspace=\"" << cs << "\">\n";

  // Serialize nodegraphs first
  if (!nodegraphs.empty()) {
    SerializeNodeGraphs(nodegraphs, ss, warn, err);
  }

  ss << pprint::Indent(1) << "<UsdPreviewSurface name=\"" << node_name
     << "\" type=\"surfaceshader\">\n";

  // Helper to check if an input has a connection
  auto has_connection = [&connections](const std::string &input_name) -> const MtlxShaderConnection* {
    for (const auto &conn : connections) {
      if (conn.input_name == input_name) {
        return &conn;
      }
    }
    return nullptr;
  };

#define EMIT_ATTRIBUTE(__name, __tyname, __attr)                            \
  {                                                                         \
    const MtlxShaderConnection *conn = has_connection(__name);              \
    if (conn) {                                                             \
      /* Emit connection */                                                 \
      ss << pprint::Indent(2) << "<input name=\"" << __name << "\" type=\"" \
         << __tyname << "\"";                                               \
      if (!conn->nodegraph.empty()) {                                       \
        ss << " nodegraph=\"" << conn->nodegraph << "\"";                   \
        if (!conn->output.empty()) {                                        \
          ss << " output=\"" << conn->output << "\"";                       \
        }                                                                   \
      } else if (!conn->nodename.empty()) {                                 \
        ss << " nodename=\"" << conn->nodename << "\"";                     \
      }                                                                     \
      ss << " />\n";                                                        \
    } else {                                                                \
      /* Emit value */                                                      \
      std::string value_str;                                                \
      if (!SerializeAttribute(__name, __attr, value_str, err)) {            \
        return false;                                                       \
      }                                                                     \
      if (value_str.size()) {                                               \
        ss << pprint::Indent(2) << "<input name=\"" << __name << "\" type=\"" \
           << __tyname << "\" value=" << value_str << " />\n";              \
      }                                                                     \
    }                                                                       \
  }

  EMIT_ATTRIBUTE("diffuseColor", "color3", shader.diffuseColor)
  EMIT_ATTRIBUTE("emissiveColor", "color3", shader.emissiveColor)
  EMIT_ATTRIBUTE("useSpecularWorkflow", "integer", shader.useSpecularWorkflow)
  EMIT_ATTRIBUTE("specularColor", "color3", shader.specularColor)
  EMIT_ATTRIBUTE("metallic", "float", shader.metallic)
  EMIT_ATTRIBUTE("roughness", "float", shader.roughness)
  EMIT_ATTRIBUTE("clearcoat", "float", shader.clearcoat)
  EMIT_ATTRIBUTE("clearcoatRoughness", "float", shader.clearcoatRoughness)
  EMIT_ATTRIBUTE("opacity", "float", shader.opacity)
  EMIT_ATTRIBUTE("opacityThreshold", "float", shader.opacityThreshold)
  EMIT_ATTRIBUTE("ior", "float", shader.ior)
  EMIT_ATTRIBUTE("normal", "vector3", shader.normal)
  EMIT_ATTRIBUTE("displacement", "float", shader.displacement)
  EMIT_ATTRIBUTE("occlusion", "float", shader.occlusion)

  ss << pprint::Indent(1) << "</UsdPreviewSurface>\n";

  ss << pprint::Indent(1)
     << "<surfacematerial name=\"USD_Default\" type=\"material\">\n";
  ss << pprint::Indent(2)
     << "<input name=\"surfaceshader\" type=\"surfaceshader\" nodename=\""
     << node_name << "\" />\n";
  ss << pprint::Indent(1) << "</surfacematerial>\n";

  ss << "</materialx>\n";

  xml_str = ss.str();

  return true;
}
#endif // TINYUSDZ_MTLX_ENABLE_USDPREVIEWSURFACE_EXPORT

#if TINYUSDZ_MTLX_ENABLE_STANDARDSURFACE_EXPORT
static bool WriteMaterialXToString(const MtlxAutodeskStandardSurface &shader,
                                   const std::string &shader_name,
                                   const std::vector<MtlxShaderConnection> &connections,
                                   const std::map<std::string, PrimSpec> &nodegraphs,
                                   const std::string &colorspace,
                                   std::string &xml_str, std::string *warn,
                                   std::string *err) {
  (void)warn;

  std::stringstream ss;

  std::string node_name = shader_name.empty() ? "SR_default" : shader_name;
  // Use provided colorspace or default to lin_rec709
  std::string cs = colorspace.empty() ? "lin_rec709" : colorspace;

  ss << "<?xml version=\"1.0\"?>\n";
  ss << "<materialx version=\"1.38\" colorspace=\"" << cs << "\">\n";

  // Serialize nodegraphs first
  if (!nodegraphs.empty()) {
    SerializeNodeGraphs(nodegraphs, ss, warn, err);
  }

  ss << pprint::Indent(1) << "<standard_surface name=\"" << node_name
     << "\" type=\"surfaceshader\">\n";

  // Helper to check if an input has a connection
  auto has_connection = [&connections](const std::string &input_name) -> const MtlxShaderConnection* {
    for (const auto &conn : connections) {
      if (conn.input_name == input_name) {
        return &conn;
      }
    }
    return nullptr;
  };

#define EMIT_ATTRIBUTE(__name, __tyname, __attr)                            \
  {                                                                         \
    const MtlxShaderConnection *conn = has_connection(__name);              \
    if (conn) {                                                             \
      /* Emit connection */                                                 \
      ss << pprint::Indent(2) << "<input name=\"" << __name << "\" type=\"" \
         << __tyname << "\"";                                               \
      if (!conn->nodegraph.empty()) {                                       \
        ss << " nodegraph=\"" << conn->nodegraph << "\"";                   \
        if (!conn->output.empty()) {                                        \
          ss << " output=\"" << conn->output << "\"";                       \
        }                                                                   \
      } else if (!conn->nodename.empty()) {                                 \
        ss << " nodename=\"" << conn->nodename << "\"";                     \
      }                                                                     \
      ss << " />\n";                                                        \
    } else {                                                                \
      /* Emit value */                                                      \
      std::string value_str;                                                \
      if (!SerializeAttribute(__name, __attr, value_str, err)) {            \
        return false;                                                       \
      }                                                                     \
      if (value_str.size()) {                                               \
        ss << pprint::Indent(2) << "<input name=\"" << __name << "\" type=\"" \
           << __tyname << "\" value=" << value_str << " />\n";              \
      }                                                                     \
    }                                                                       \
  }

  // Base properties
  EMIT_ATTRIBUTE("base", "float", shader.base)
  EMIT_ATTRIBUTE("base_color", "color3", shader.base_color)
  EMIT_ATTRIBUTE("diffuse_roughness", "float", shader.diffuse_roughness)
  EMIT_ATTRIBUTE("metalness", "float", shader.metalness)

  // Specular properties
  EMIT_ATTRIBUTE("specular", "float", shader.specular)
  EMIT_ATTRIBUTE("specular_color", "color3", shader.specular_color)
  EMIT_ATTRIBUTE("specular_roughness", "float", shader.specular_roughness)
  EMIT_ATTRIBUTE("specular_IOR", "float", shader.specular_IOR)
  EMIT_ATTRIBUTE("specular_anisotropy", "float", shader.specular_anisotropy)
  EMIT_ATTRIBUTE("specular_rotation", "float", shader.specular_rotation)

  // Transmission properties
  EMIT_ATTRIBUTE("transmission", "float", shader.transmission)
  EMIT_ATTRIBUTE("transmission_color", "color3", shader.transmission_color)
  EMIT_ATTRIBUTE("transmission_depth", "float", shader.transmission_depth)
  EMIT_ATTRIBUTE("transmission_scatter", "color3", shader.transmission_scatter)
  EMIT_ATTRIBUTE("transmission_scatter_anisotropy", "float", shader.transmission_scatter_anisotropy)
  EMIT_ATTRIBUTE("transmission_dispersion", "float", shader.transmission_dispersion)
  EMIT_ATTRIBUTE("transmission_extra_roughness", "float", shader.transmission_extra_roughness)

  // Subsurface properties
  EMIT_ATTRIBUTE("subsurface", "float", shader.subsurface)
  EMIT_ATTRIBUTE("subsurface_color", "color3", shader.subsurface_color)
  EMIT_ATTRIBUTE("subsurface_radius", "float", shader.subsurface_radius)
  EMIT_ATTRIBUTE("subsurface_scale", "float", shader.subsurface_scale)
  EMIT_ATTRIBUTE("subsurface_anisotropy", "float", shader.subsurface_anisotropy)

  // Sheen properties
  EMIT_ATTRIBUTE("sheen", "float", shader.sheen)
  EMIT_ATTRIBUTE("sheen_color", "color3", shader.sheen_color)
  EMIT_ATTRIBUTE("sheen_roughness", "float", shader.sheen_roughness)

  // Coat properties
  EMIT_ATTRIBUTE("coat", "float", shader.coat)
  EMIT_ATTRIBUTE("coat_color", "color3", shader.coat_color)
  EMIT_ATTRIBUTE("coat_roughness", "float", shader.coat_roughness)
  EMIT_ATTRIBUTE("coat_anisotropy", "float", shader.coat_anisotropy)
  EMIT_ATTRIBUTE("coat_rotation", "float", shader.coat_rotation)
  EMIT_ATTRIBUTE("coat_IOR", "float", shader.coat_IOR)
  EMIT_ATTRIBUTE("coat_affect_color", "float", shader.coat_affect_color)
  EMIT_ATTRIBUTE("coat_affect_roughness", "float", shader.coat_affect_roughness)

  // Thin film properties
  EMIT_ATTRIBUTE("thin_film_thickness", "float", shader.thin_film_thickness)
  EMIT_ATTRIBUTE("thin_film_IOR", "float", shader.thin_film_IOR)

  // Emission properties
  EMIT_ATTRIBUTE("emission", "float", shader.emission)
  EMIT_ATTRIBUTE("emission_color", "color3", shader.emission_color)

  // Opacity
  EMIT_ATTRIBUTE("opacity", "color3", shader.opacity)

  // Thin walled
  EMIT_ATTRIBUTE("thin_walled", "boolean", shader.thin_walled)

  // Normal and tangent - these are TypedAttribute (not TypedAttributeWithFallback)
  // Skip for now as they require different serialization
  // TODO: Add serialization support for TypedAttribute

#undef EMIT_ATTRIBUTE

  ss << pprint::Indent(1) << "</standard_surface>\n";

  ss << pprint::Indent(1)
     << "<surfacematerial name=\"StandardSurface_Material\" type=\"material\">\n";
  ss << pprint::Indent(2)
     << "<input name=\"surfaceshader\" type=\"surfaceshader\" nodename=\""
     << node_name << "\" />\n";
  ss << pprint::Indent(1) << "</surfacematerial>\n";

  ss << "</materialx>\n";

  xml_str = ss.str();

  return true;
}
#endif // TINYUSDZ_MTLX_ENABLE_STANDARDSURFACE_EXPORT

// ============================================================================
// OpenPBR Surface Export - ACTIVE PATH
// This is the primary export path used for Blender MaterialX exports.
// ============================================================================
static bool WriteMaterialXToString(const MtlxOpenPBRSurface &shader,
                                   const std::string &shader_name,
                                   const std::vector<MtlxShaderConnection> &connections,
                                   const std::map<std::string, PrimSpec> &nodegraphs,
                                   const std::string &colorspace,
                                   std::string &xml_str, std::string *warn,
                                   std::string *err) {
  (void)warn;

  std::stringstream ss;

  std::string node_name = shader_name.empty() ? "SR_default" : shader_name;
  // Use provided colorspace or default to lin_rec709
  std::string cs = colorspace.empty() ? "lin_rec709" : colorspace;

  ss << "<?xml version=\"1.0\"?>\n";
  ss << "<materialx version=\"1.38\" colorspace=\"" << cs << "\">\n";

  // Serialize nodegraphs first
  if (!nodegraphs.empty()) {
    SerializeNodeGraphs(nodegraphs, ss, warn, err);
  }

  ss << pprint::Indent(1) << "<open_pbr_surface name=\"" << node_name
     << "\" type=\"surfaceshader\">\n";

  // Helper to check if an input has a connection
  auto has_connection = [&connections](const std::string &input_name) -> const MtlxShaderConnection* {
    for (const auto &conn : connections) {
      if (conn.input_name == input_name) {
        return &conn;
      }
    }
    return nullptr;
  };

#define EMIT_ATTRIBUTE(__name, __tyname, __attr)                            \
  {                                                                         \
    const MtlxShaderConnection *conn = has_connection(__name);              \
    if (conn) {                                                             \
      /* Emit connection */                                                 \
      ss << pprint::Indent(2) << "<input name=\"" << __name << "\" type=\"" \
         << __tyname << "\"";                                               \
      if (!conn->nodegraph.empty()) {                                       \
        ss << " nodegraph=\"" << conn->nodegraph << "\"";                   \
        if (!conn->output.empty()) {                                        \
          ss << " output=\"" << conn->output << "\"";                       \
        }                                                                   \
      } else if (!conn->nodename.empty()) {                                 \
        ss << " nodename=\"" << conn->nodename << "\"";                     \
      }                                                                     \
      ss << " />\n";                                                        \
    } else {                                                                \
      /* Emit value */                                                      \
      std::string value_str;                                                \
      if (!SerializeAttribute(__name, __attr, value_str, err)) {            \
        return false;                                                       \
      }                                                                     \
      if (value_str.size()) {                                               \
        ss << pprint::Indent(2) << "<input name=\"" << __name << "\" type=\"" \
           << __tyname << "\" value=" << value_str << " />\n";              \
      }                                                                     \
    }                                                                       \
  }

  // Base properties
  EMIT_ATTRIBUTE("base_weight", "float", shader.base_weight)
  EMIT_ATTRIBUTE("base_color", "color3", shader.base_color)
  EMIT_ATTRIBUTE("base_metalness", "float", shader.base_metalness)
  EMIT_ATTRIBUTE("base_diffuse_roughness", "float", shader.base_diffuse_roughness)

  // Specular properties
  EMIT_ATTRIBUTE("specular_weight", "float", shader.specular_weight)
  EMIT_ATTRIBUTE("specular_color", "color3", shader.specular_color)
  EMIT_ATTRIBUTE("specular_roughness", "float", shader.specular_roughness)
  EMIT_ATTRIBUTE("specular_ior", "float", shader.specular_ior)
  EMIT_ATTRIBUTE("specular_anisotropy", "float", shader.specular_anisotropy)
  EMIT_ATTRIBUTE("specular_rotation", "float", shader.specular_rotation)

  // Transmission properties
  EMIT_ATTRIBUTE("transmission_weight", "float", shader.transmission_weight)
  EMIT_ATTRIBUTE("transmission_color", "color3", shader.transmission_color)
  EMIT_ATTRIBUTE("transmission_depth", "float", shader.transmission_depth)
  EMIT_ATTRIBUTE("transmission_scatter", "color3", shader.transmission_scatter)
  EMIT_ATTRIBUTE("transmission_scatter_anisotropy", "float", shader.transmission_scatter_anisotropy)
  EMIT_ATTRIBUTE("transmission_dispersion", "float", shader.transmission_dispersion)

  // Subsurface properties
  EMIT_ATTRIBUTE("subsurface_weight", "float", shader.subsurface_weight)
  EMIT_ATTRIBUTE("subsurface_color", "color3", shader.subsurface_color)
  EMIT_ATTRIBUTE("subsurface_radius", "float", shader.subsurface_radius)
  EMIT_ATTRIBUTE("subsurface_radius_scale", "color3", shader.subsurface_radius_scale)
  EMIT_ATTRIBUTE("subsurface_scale", "float", shader.subsurface_scale)
  EMIT_ATTRIBUTE("subsurface_anisotropy", "float", shader.subsurface_anisotropy)

  // Coat properties
  EMIT_ATTRIBUTE("coat_weight", "float", shader.coat_weight)
  EMIT_ATTRIBUTE("coat_color", "color3", shader.coat_color)
  EMIT_ATTRIBUTE("coat_roughness", "float", shader.coat_roughness)
  EMIT_ATTRIBUTE("coat_anisotropy", "float", shader.coat_anisotropy)
  EMIT_ATTRIBUTE("coat_rotation", "float", shader.coat_rotation)
  EMIT_ATTRIBUTE("coat_ior", "float", shader.coat_ior)
  EMIT_ATTRIBUTE("coat_affect_color", "float", shader.coat_affect_color)
  EMIT_ATTRIBUTE("coat_affect_roughness", "float", shader.coat_affect_roughness)

  // Thin film properties
  EMIT_ATTRIBUTE("thin_film_thickness", "float", shader.thin_film_thickness)
  EMIT_ATTRIBUTE("thin_film_ior", "float", shader.thin_film_ior)

  // Emission properties
  EMIT_ATTRIBUTE("emission_luminance", "float", shader.emission_luminance)
  EMIT_ATTRIBUTE("emission_color", "color3", shader.emission_color)

  // Geometry properties
  EMIT_ATTRIBUTE("geometry_opacity", "float", shader.geometry_opacity)
  EMIT_ATTRIBUTE("geometry_thin_walled", "boolean", shader.geometry_thin_walled)

  // Normal and tangent - these are TypedAttribute (not TypedAttributeWithFallback)
  // Skip for now as they require different serialization
  // TODO: Add serialization support for TypedAttribute

#undef EMIT_ATTRIBUTE

  ss << pprint::Indent(1) << "</open_pbr_surface>\n";

  ss << pprint::Indent(1)
     << "<surfacematerial name=\"OpenPBR_Material\" type=\"material\">\n";
  ss << pprint::Indent(2)
     << "<input name=\"surfaceshader\" type=\"surfaceshader\" nodename=\""
     << node_name << "\" />\n";
  ss << pprint::Indent(1) << "</surfacematerial>\n";

  ss << "</materialx>\n";

  xml_str = ss.str();

  return true;
}

// Helper to extract MaterialX node category from info:id (e.g., "ND_multiply_color3" -> "multiply")
static std::string ExtractNodeCategory(const std::string &info_id) {
  // info:id format: "ND_<category>_<type>" or just "<category>"
  if (info_id.substr(0, 3) == "ND_") {
    std::string rest = info_id.substr(3);
    size_t underscore = rest.rfind('_');
    if (underscore != std::string::npos) {
      rest = rest.substr(0, underscore);
    }
    return rest;
  }
  return info_id;
}

// Helper to extract MaterialX type from info:id (e.g., "ND_multiply_color3" -> "color3")
static std::string ExtractNodeType(const std::string &info_id) {
  size_t underscore = info_id.rfind('_');
  if (underscore != std::string::npos) {
    return info_id.substr(underscore + 1);
  }
  return "float";
}

// Helper to convert USD type to MaterialX type string
static std::string ToMtlxTypeString(const value::Value &val) {
  if (val.type_id() == value::TYPE_ID_FLOAT) return "float";
  if (val.type_id() == value::TYPE_ID_INT32) return "integer";
  if (val.type_id() == value::TYPE_ID_BOOL) return "boolean";
  if (val.type_id() == value::TYPE_ID_STRING) return "string";
  if (val.type_id() == value::TYPE_ID_FLOAT2) return "vector2";
  if (val.type_id() == value::TYPE_ID_FLOAT3) return "vector3";
  if (val.type_id() == value::TYPE_ID_FLOAT4) return "vector4";
  if (val.type_id() == value::TYPE_ID_COLOR3F) return "color3";
  if (val.type_id() == value::TYPE_ID_COLOR4F) return "color4";
  if (val.type_id() == value::TYPE_ID_NORMAL3F) return "vector3";
  if (val.type_id() == value::TYPE_ID_ASSET_PATH) return "filename";
  if (val.type_id() == value::TYPE_ID_TOKEN) return "string";
  return "float";
}

// Helper to convert USD value to MaterialX value string
static std::string ToMtlxValueString(const value::Value &val) {
  if (auto f = val.as<float>()) {
    return dtos(*f);
  }
  if (auto i = val.as<int>()) {
    return std::to_string(*i);
  }
  if (auto b = val.as<bool>()) {
    return *b ? "true" : "false";
  }
  if (auto s = val.as<std::string>()) {
    return *s;
  }
  if (auto v2 = val.as<value::float2>()) {
    return dtos((*v2)[0]) + ", " + dtos((*v2)[1]);
  }
  if (auto v3 = val.as<value::float3>()) {
    return dtos((*v3)[0]) + ", " + dtos((*v3)[1]) + ", " + dtos((*v3)[2]);
  }
  if (auto v4 = val.as<value::float4>()) {
    return dtos((*v4)[0]) + ", " + dtos((*v4)[1]) + ", " + dtos((*v4)[2]) + ", " + dtos((*v4)[3]);
  }
  if (auto c3 = val.as<value::color3f>()) {
    return dtos(c3->r) + ", " + dtos(c3->g) + ", " + dtos(c3->b);
  }
  if (auto c4 = val.as<value::color4f>()) {
    return dtos(c4->r) + ", " + dtos(c4->g) + ", " + dtos(c4->b) + ", " + dtos(c4->a);
  }
  if (auto n3 = val.as<value::normal3f>()) {
    return dtos(n3->x) + ", " + dtos(n3->y) + ", " + dtos(n3->z);
  }
  if (auto ap = val.as<value::AssetPath>()) {
    return ap->GetAssetPath();
  }
  if (auto t = val.as<value::token>()) {
    return t->str();
  }
  return "";
}

// Helper function to serialize nodegraphs to MaterialX XML
static bool SerializeNodeGraphs(const std::map<std::string, PrimSpec> &nodegraphs,
                                std::stringstream &ss, std::string *warn, std::string *err) {
  (void)warn;
  (void)err;

  for (const auto &ng_item : nodegraphs) {
    const std::string &ng_name = ng_item.first;
    const PrimSpec &ng_ps = ng_item.second;

    ss << pprint::Indent(1) << "<nodegraph name=\"" << ng_name << "\">\n";

    // Serialize child nodes
    for (const auto &child_ps : ng_ps.children()) {
      std::string node_name = child_ps.name();

      // Get info:id to determine node category
      std::string info_id;
      auto info_it = child_ps.props().find(kShaderInfoId);
      if (info_it != child_ps.props().end() && info_it->second.is_attribute()) {
        const Attribute &attr = info_it->second.get_attribute();
        if (auto tok = attr.get_value<value::token>()) {
          info_id = tok->str();
        }
      }

      if (info_id.empty()) {
        // Skip nodes without info:id
        continue;
      }

      std::string category = ExtractNodeCategory(info_id);
      std::string node_type = ExtractNodeType(info_id);

      ss << pprint::Indent(2) << "<" << category << " name=\"" << node_name
         << "\" type=\"" << node_type << "\">\n";

      // Serialize inputs
      for (const auto &prop_item : child_ps.props()) {
        const std::string &prop_name = prop_item.first;

        // Skip non-input properties
        if (prop_name.find("inputs:") != 0) continue;

        std::string input_name = prop_name.substr(7); // Remove "inputs:" prefix

        if (prop_item.second.is_attribute()) {
          const Attribute &attr = prop_item.second.get_attribute();

          // Check if it's a connection
          if (attr.has_connections() && !attr.connections().empty()) {
            // Extract nodename from connection path
            const Path &conn_path = attr.connections()[0];
            std::string full_path = conn_path.full_path_name();

            // Parse connection: "nodename.outputs:out" or just "nodename"
            size_t dot_pos = full_path.find('.');
            std::string nodename_ref = (dot_pos != std::string::npos) ?
                                       full_path.substr(0, dot_pos) : full_path;

            // Determine type from the connected node's output or use default
            ss << pprint::Indent(3) << "<input name=\"" << input_name
               << "\" type=\"" << node_type << "\" nodename=\"" << nodename_ref << "\" />\n";
          } else {
            // It's a value
            value::Value val;
            if (attr.get_var().has_value() && !attr.get_var().has_timesamples()) {
              val = attr.get_var().value_raw();
            }

            if (val.type_id() != value::TYPE_ID_NULL) {
              std::string type_str = ToMtlxTypeString(val);
              std::string value_str = ToMtlxValueString(val);

              if (!value_str.empty()) {
                ss << pprint::Indent(3) << "<input name=\"" << input_name
                   << "\" type=\"" << type_str << "\" value=\"" << value_str << "\" />\n";
              }
            }
          }
        }
      }

      ss << pprint::Indent(2) << "</" << category << ">\n";
    }

    // Serialize outputs
    for (const auto &prop_item : ng_ps.props()) {
      const std::string &prop_name = prop_item.first;

      // Check if this is an output property
      if (prop_name.find("outputs:") != 0) continue;

      std::string output_name = prop_name.substr(8); // Remove "outputs:" prefix

      if (prop_item.second.is_attribute()) {
        const Attribute &attr = prop_item.second.get_attribute();

        if (attr.has_connections() && !attr.connections().empty()) {
          const Path &conn_path = attr.connections()[0];
          std::string full_path = conn_path.full_path_name();

          // Parse connection path to extract nodename
          size_t dot_pos = full_path.find('.');
          std::string nodename_ref = (dot_pos != std::string::npos) ?
                                     full_path.substr(0, dot_pos) : full_path;

          // Try to determine type from the connected node
          std::string output_type = "color3";  // Default
          for (const auto &child_ps : ng_ps.children()) {
            if (child_ps.name() == nodename_ref) {
              auto info_it = child_ps.props().find(kShaderInfoId);
              if (info_it != child_ps.props().end() && info_it->second.is_attribute()) {
                if (auto tok = info_it->second.get_attribute().get_value<value::token>()) {
                  output_type = ExtractNodeType(tok->str());
                }
              }
              break;
            }
          }

          ss << pprint::Indent(2) << "<output name=\"" << output_name
             << "\" type=\"" << output_type << "\" nodename=\"" << nodename_ref << "\" />\n";
        }
      }
    }

    ss << pprint::Indent(1) << "</nodegraph>\n";
  }

  return true;
}

// ============================================================================
// Node Converters - ACTIVE PATH
// These functions convert MaterialX XML nodes to USD PrimSpec representations.
// Used for importing Blender MaterialX NodeGraphs.
// ============================================================================

// Initialize a PrimSpec as a Shader with the given info:id
static void InitializeShader(PrimSpec &ps, const std::string &info_id) {
  ps.specifier() = Specifier::Def;
  ps.typeName() = kShader;
  ps.props()[kShaderInfoId] = Property(Attribute::Uniform(value::token(info_id)));
}

static bool ConvertPlace2d(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                           std::string * /* warn */, std::string *err) {
  // texcoord(vector2). default index=0 uv coordinate
  // pivot(vector2). default (0, 0)
  // scale(vector2). default (1, 1)
  // rotate(float). in degrees, Counter-clockwise
  // offset(vector2)

  // Get node name
  tinyusdz::mtlx::pugi::xml_attribute name_attr = node.attribute("name");
  if (name_attr) {
    ps.name() = name_attr.as_string();
  }

  if (tinyusdz::mtlx::pugi::xml_attribute texcoord_attr = node.attribute("texcoord")) {
    // MaterialX texcoord attribute specifies the UV set name (e.g., "UV0", "st")
    // Map to USD's inputs:varname for UsdPrimvarReader
    std::string texcoord_name = texcoord_attr.as_string();
    if (!texcoord_name.empty()) {
      ps.props()["inputs:varname"] = Property(Attribute::Uniform(value::token(texcoord_name)));
    }
  }

  if (tinyusdz::mtlx::pugi::xml_attribute pivot_attr = node.attribute("pivot")) {
    value::float2 value{};
    if (ParseMaterialXValue(pivot_attr.as_string(), &value, err)) {
      ps.props()["inputs:pivot"] = Property(Attribute::Uniform(value));
    }
  }

  if (tinyusdz::mtlx::pugi::xml_attribute scale_attr = node.attribute("scale")) {
    value::float2 value{};
    if (!ParseMaterialXValue(scale_attr.as_string(), &value, err)) {
      PUSH_ERROR_AND_RETURN(
          "Failed to parse `scale` attribute of `place2d`.\n");
    }
    ps.props()["inputs:scale"] = Property(Attribute::Uniform(value));
  }

  if (tinyusdz::mtlx::pugi::xml_attribute rotate_attr = node.attribute("rotate")) {
    float value{};
    if (!ParseMaterialXValue(rotate_attr.as_string(), &value, err)) {
      PUSH_ERROR_AND_RETURN(
          "Failed to parse `rotate` attribute of `place2d`.\n");
    }
    ps.props()["inputs:rotation"] = Property(Attribute::Uniform(value));
  }

  tinyusdz::mtlx::pugi::xml_attribute offset_attr = node.attribute("offset");
  if (offset_attr) {
    value::float2 value{};
    if (!ParseMaterialXValue(offset_attr.as_string(), &value, err)) {
      PUSH_ERROR_AND_RETURN(
          "Failed to parse `offset` attribute of `place2d`.\n");
    }
    ps.props()["inputs:translation"] = Property(Attribute::Uniform(value));
  }

  InitializeShader(ps, kUsdTransform2d);

  return true;
}

// Convert MaterialX tiledimage node to USD UsdUVTexture
static bool ConvertTiledImage(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                              std::string *warn, std::string *err) {
  (void)warn;

  // Get node name and type
  tinyusdz::mtlx::pugi::xml_attribute name_attr = node.attribute("name");
  if (name_attr) {
    ps.name() = name_attr.as_string();
  }

  // Parse inputs
  for (auto inp : node.children("input")) {
    std::string input_name;
    std::string input_type;
    std::string input_value;

    tinyusdz::mtlx::pugi::xml_attribute inp_name_attr = inp.attribute("name");
    if (inp_name_attr) {
      input_name = inp_name_attr.as_string();
    }

    tinyusdz::mtlx::pugi::xml_attribute type_attr = inp.attribute("type");
    if (type_attr) {
      input_type = type_attr.as_string();
    }

    tinyusdz::mtlx::pugi::xml_attribute value_attr = inp.attribute("value");
    if (value_attr) {
      input_value = value_attr.as_string();
    }

    // Check for per-input colorspace attribute
    tinyusdz::mtlx::pugi::xml_attribute colorspace_attr = inp.attribute("colorspace");
    std::string input_colorspace;
    if (colorspace_attr) {
      input_colorspace = colorspace_attr.as_string();
    }

    // Map MaterialX inputs to USD inputs
    std::string prop_key;  // Track which property was set for colorspace propagation
    if (input_name == "file" && input_type == "filename") {
      // Convert filename to asset path
      prop_key = "inputs:file";
      ps.props()[prop_key] = Property(Attribute::Uniform(value::AssetPath(input_value)));
    } else if (input_name == "uvtiling" && input_type == "vector2") {
      value::float2 tiling;
      if (ParseMaterialXValue(input_value, &tiling, err)) {
        // Store for potential scale transformation
        ps.props()["inputs:scale"] = Property(Attribute::Uniform(tiling));
      }
    } else if (input_name == "uvoffset" && input_type == "vector2") {
      value::float2 offset;
      if (ParseMaterialXValue(input_value, &offset, err)) {
        ps.props()["inputs:translation"] = Property(Attribute::Uniform(offset));
      }
    } else if (input_name == "default") {
      // Fallback value
      if (input_type == "color3") {
        value::color3f fallback;
        if (ParseMaterialXValue(input_value, &fallback, err)) {
          ps.props()["inputs:fallback"] = Property(Attribute::Uniform(value::color4f{fallback.r, fallback.g, fallback.b, 1.0f}));
        }
      } else if (input_type == "float") {
        float fallback;
        if (ParseMaterialXValue(input_value, &fallback, err)) {
          ps.props()["inputs:fallback"] = Property(Attribute::Uniform(value::color4f{fallback, fallback, fallback, 1.0f}));
        }
      }
    }

    // Propagate per-input colorspace to the property's attribute metadata
    if (!input_colorspace.empty() && !prop_key.empty()) {
      Attribute *attr_ptr = ps.props()[prop_key].get_attribute_or_null();
      if (attr_ptr) {
        attr_ptr->metas().set_colorSpace(input_colorspace);
      }
    }
  }

  InitializeShader(ps, kUsdUVTexture);

  return true;
}

// Convert MaterialX texcoord node to USD UsdPrimvarReader_float2
static bool ConvertTexCoord(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                            const MtlxConfig &config,
                            std::string *warn, std::string *err) {
  (void)warn;

  // Get node name
  tinyusdz::mtlx::pugi::xml_attribute name_attr = node.attribute("name");
  if (name_attr) {
    ps.name() = name_attr.as_string();
  }

  // Parse index attribute (UV set index)
  int uv_index = 0;
  tinyusdz::mtlx::pugi::xml_attribute index_attr = node.attribute("index");
  if (index_attr) {
    std::string index_str = index_attr.as_string();
    if (!ParseMaterialXValue(index_str, &uv_index, err)) {
      PUSH_ERROR_AND_RETURN("Failed to parse `index` attribute of `texcoord`.\n");
    }
  }

  // Map to USD primvar name convention using MtlxConfig
  // Similar to OpenUSD's USDMTLX_PRIMARY_UV_NAME environment variable
  std::string varname;
  if (uv_index == 0) {
    // Primary UV: use config.primary_uv_name, fallback to "st" if empty
    varname = config.primary_uv_name.empty() ? "st" : config.primary_uv_name;
  } else {
    // Secondary UV: use config.secondary_uv_name_prefix + index, fallback to "st" + index
    std::string prefix = config.secondary_uv_name_prefix.empty() ? "st" : config.secondary_uv_name_prefix;
    varname = prefix + std::to_string(uv_index);
  }
  ps.props()["inputs:varname"] = Property(Attribute::Uniform(varname));

  // Set fallback to (0, 0)
  ps.props()["inputs:fallback"] = Property(Attribute::Uniform(value::float2{0.0f, 0.0f}));

  InitializeShader(ps, kUsdPrimvarReader_float2);

  return true;
}

// ConvertConstant/ConvertMultiply/ConvertAdd/ConvertMix/ConvertNoise are now
// handled by ConvertGenericNode via the dispatch table in ConvertSingleNode.

// ============================================================================
// Generic Node Converters
// These handle the common pattern of parsing inputs and setting info:id
// ============================================================================

// Helper to parse a single input element and store in PrimSpec
static bool ParseInputElement(const tinyusdz::mtlx::pugi::xml_node &inp, PrimSpec &ps,
                              std::string *err) {
  std::string input_name;
  tinyusdz::mtlx::pugi::xml_attribute inp_name_attr = inp.attribute("name");
  if (inp_name_attr) {
    input_name = inp_name_attr.as_string();
  }

  std::string prop_name = "inputs:" + input_name;

  // Check for per-input colorspace attribute (MaterialX allows per-input colorspace override)
  tinyusdz::mtlx::pugi::xml_attribute colorspace_attr = inp.attribute("colorspace");
  std::string input_colorspace;
  if (colorspace_attr) {
    input_colorspace = colorspace_attr.as_string();
  }

  // Check for connection first (nodename or nodegraph)
  tinyusdz::mtlx::pugi::xml_attribute nodename_attr = inp.attribute("nodename");
  tinyusdz::mtlx::pugi::xml_attribute nodegraph_attr = inp.attribute("nodegraph");
  tinyusdz::mtlx::pugi::xml_attribute output_attr = inp.attribute("output");

  if (nodename_attr || nodegraph_attr) {
    // This is a connection - store connection info
    Attribute attr;
    if (nodename_attr) {
      std::string target = std::string(nodename_attr.as_string()) + ".outputs:out";
      attr.set_connections({Path(target, "")});
    } else if (nodegraph_attr) {
      std::string output_name = output_attr ? output_attr.as_string() : "out";
      std::string target = std::string(nodegraph_attr.as_string()) + ".outputs:" + output_name;
      attr.set_connections({Path(target, "")});
    }
    ps.props()[prop_name] = Property(attr);
    if (!input_colorspace.empty()) {
      Attribute *attr_ptr = ps.props()[prop_name].get_attribute_or_null();
      if (attr_ptr) {
        attr_ptr->metas().set_colorSpace(input_colorspace);
      }
    }
    return true;
  }

  // Parse value
  tinyusdz::mtlx::pugi::xml_attribute value_attr = inp.attribute("value");
  if (!value_attr) {
    return true;  // No value, skip
  }

  std::string input_value = value_attr.as_string();
  tinyusdz::mtlx::pugi::xml_attribute type_attr = inp.attribute("type");
  std::string type_str = type_attr ? type_attr.as_string() : "float";

  if (type_str == "float") {
    float val;
    if (ParseMaterialXValue(input_value, &val, err)) {
      ps.props()[prop_name] = Property(Attribute::Uniform(val));
    }
  } else if (type_str == "color3") {
    value::color3f val;
    if (ParseMaterialXValue(input_value, &val, err)) {
      ps.props()[prop_name] = Property(Attribute::Uniform(val));
    }
  } else if (type_str == "vector2") {
    value::float2 val;
    if (ParseMaterialXValue(input_value, &val, err)) {
      ps.props()[prop_name] = Property(Attribute::Uniform(val));
    }
  } else if (type_str == "vector3") {
    value::float3 val;
    if (ParseMaterialXValue(input_value, &val, err)) {
      ps.props()[prop_name] = Property(Attribute::Uniform(val));
    }
  } else if (type_str == "vector4") {
    value::float4 val;
    if (ParseMaterialXValue(input_value, &val, err)) {
      ps.props()[prop_name] = Property(Attribute::Uniform(val));
    }
  } else if (type_str == "integer") {
    int val;
    if (ParseMaterialXValue(input_value, &val, err)) {
      ps.props()[prop_name] = Property(Attribute::Uniform(val));
    }
  } else if (type_str == "boolean") {
    bool val;
    if (ParseMaterialXValue(input_value, &val, err)) {
      ps.props()[prop_name] = Property(Attribute::Uniform(val));
    }
  } else if (type_str == "string") {
    ps.props()[prop_name] = Property(Attribute::Uniform(input_value));
  } else if (type_str == "filename") {
    ps.props()[prop_name] = Property(Attribute::Uniform(value::AssetPath(input_value)));
  }

  // Apply per-input colorspace if present
  if (!input_colorspace.empty()) {
    auto it = ps.props().find(prop_name);
    if (it != ps.props().end() && it->second.is_attribute()) {
      Attribute *attr_ptr = it->second.get_attribute_or_null();
      if (attr_ptr) {
        attr_ptr->metas().set_colorSpace(input_colorspace);
      }
    }
  }

  return true;
}

// Generic converter for any MaterialX node - parses all inputs and sets info:id
static bool ConvertGenericNode(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                               const std::string &info_id,
                               std::string *warn, std::string *err) {
  (void)warn;

  tinyusdz::mtlx::pugi::xml_attribute name_attr = node.attribute("name");
  if (name_attr) {
    ps.name() = name_attr.as_string();
  }

  // Parse all inputs
  for (auto inp : node.children("input")) {
    if (!ParseInputElement(inp, ps, err)) {
      return false;
    }
  }

  InitializeShader(ps, info_id);

  return true;
}

// ============================================================================
// Node dispatch table — maps MaterialX node names to info:id generation.
// Most nodes use "ND_<op_name>_<type>" where type comes from the node's
// "type" attribute. A few use a fixed info:id string.
// ============================================================================

// Helper: get the node's "type" attribute or a default
static std::string GetNodeTypeAttr(const tinyusdz::mtlx::pugi::xml_node &node,
                                   const char *default_type) {
  tinyusdz::mtlx::pugi::xml_attribute ta = node.attribute("type");
  return ta ? ta.as_string() : default_type;
}

// Helper: find the "type" attribute of the named input child
static std::string GetInputTypeAttr(const tinyusdz::mtlx::pugi::xml_node &node,
                                    const char *input_name, const char *default_type) {
  for (auto inp : node.children("input")) {
    tinyusdz::mtlx::pugi::xml_attribute n = inp.attribute("name");
    if (n && std::string(n.as_string()) == input_name) {
      tinyusdz::mtlx::pugi::xml_attribute t = inp.attribute("type");
      if (t) return t.as_string();
      break;
    }
  }
  return default_type;
}

// Dispatch entry: how to build info:id for a generic node
struct NodeDispatchEntry {
  const char *op_name;       // prefix for "ND_<op_name>_<type>" (nullptr → use fixed_id)
  const char *default_type;  // default type if node has no "type" attribute
  const char *fixed_id;      // if non-null, use this as the full info:id (ignores op_name)
};

// Table of nodes handled by ConvertGenericNode with computed info:id.
// Nodes NOT in this table are handled as special cases (place2d, tiledimage, etc.)
static const std::pair<const char*, NodeDispatchEntry> kNodeDispatchPairs[] = {
  // Arithmetic with custom info:id (preserving existing behavior)
  {"multiply",        {nullptr, nullptr, "MaterialXMultiply"}},
  {"add",             {nullptr, nullptr, "MaterialXMultiply"}},  // same as multiply
  {"subtract",        {nullptr, nullptr, "MaterialXMultiply"}},  // same as multiply
  {"mix",             {nullptr, nullptr, "MaterialXMix"}},
  {"noise2d",         {nullptr, nullptr, "MaterialXNoise"}},
  {"noise3d",         {nullptr, nullptr, "MaterialXNoise"}},
  {"cellnoise2d",     {nullptr, nullptr, "MaterialXNoise"}},
  {"cellnoise3d",     {nullptr, nullptr, "MaterialXNoise"}},
  {"worleynoise2d",   {nullptr, nullptr, "MaterialXNoise"}},
  {"worleynoise3d",   {nullptr, nullptr, "MaterialXNoise"}},
  {"fractal3d",       {nullptr, nullptr, "MaterialXNoise"}},
  // Binary ops: ND_<name>_<type>
  {"divide",       {"divide",       "float", nullptr}},
  {"power",        {"power",        "float", nullptr}},
  {"min",          {"min",          "float", nullptr}},
  {"max",          {"max",          "float", nullptr}},
  {"modulo",       {"modulo",       "float", nullptr}},
  {"atan2",        {"atan2",        "float", nullptr}},
  {"dotproduct",   {"dotproduct",   "float", nullptr}},
  {"crossproduct", {"crossproduct", "float", nullptr}},
  // Unary ops: ND_<name>_<type>
  {"sqrt",       {"sqrt",       "float", nullptr}},
  {"absval",     {"absval",     "float", nullptr}},
  {"sign",       {"sign",       "float", nullptr}},
  {"floor",      {"floor",      "float", nullptr}},
  {"ceil",       {"ceil",       "float", nullptr}},
  {"round",      {"round",      "float", nullptr}},
  {"sin",        {"sin",        "float", nullptr}},
  {"cos",        {"cos",        "float", nullptr}},
  {"tan",        {"tan",        "float", nullptr}},
  {"asin",       {"asin",       "float", nullptr}},
  {"acos",       {"acos",       "float", nullptr}},
  {"atan",       {"atan",       "float", nullptr}},
  {"exp",        {"exp",        "float", nullptr}},
  {"ln",         {"ln",         "float", nullptr}},
  {"log2",       {"log2",       "float", nullptr}},
  {"normalize",  {"normalize",  "float", nullptr}},
  {"magnitude",  {"magnitude",  "float", nullptr}},
  {"luminance",  {"luminance",  "float", nullptr}},
  {"invert",     {"invert",     "float", nullptr}},
  {"saturate",   {"saturate",   "float", nullptr}},
  {"hueshift",   {"hueshift",   "float", nullptr}},
  {"rgbtohsv",   {"rgbtohsv",   "float", nullptr}},
  {"hsvtorgb",   {"hsvtorgb",   "float", nullptr}},
  // Clamp/remap
  {"clamp",      {"clamp",      "float", nullptr}},
  {"remap",      {"remap",      "float", nullptr}},
  {"range",      {"remap",      "float", nullptr}},  // alias
  {"smoothstep", {"smoothstep", "float", nullptr}},
  // Channel ops
  {"combine2",   {"combine2",   "color3", nullptr}},
  {"combine3",   {"combine3",   "color3", nullptr}},
  {"combine4",   {"combine4",   "color3", nullptr}},
  {"swizzle",    {"swizzle",    "color3", nullptr}},
  // Color ops
  {"hsvadjust",  {nullptr, nullptr, "ND_hsvadjust_color3"}},
  // Geometry nodes: ND_<geom_type>_<type>
  {"position",      {"position",      "vector3", nullptr}},
  {"normal",        {"normal",        "vector3", nullptr}},
  {"tangent",       {"tangent",       "vector3", nullptr}},
  {"bitangent",     {"bitangent",     "vector3", nullptr}},
  {"geomcolor",     {"geomcolor",     "vector3", nullptr}},
  {"geompropvalue", {"geompropvalue", "vector3", nullptr}},
  // Rotation
  {"rotate3d",  {nullptr, nullptr, "ND_rotate3d_vector3"}},
  // Conditional ops: ND_<cond>_<type>
  {"ifgreater",   {"ifgreater",   "float", nullptr}},
  {"ifgreatereq", {"ifgreatereq", "float", nullptr}},
  {"ifless",      {"ifless",      "float", nullptr}},
  {"iflesseq",    {"iflesseq",    "float", nullptr}},
  {"ifequal",     {"ifequal",     "float", nullptr}},
  {"switch",      {"switch",      "float", nullptr}},
  // Constant node
  {"constant",    {nullptr, nullptr, "MaterialXConstant"}},
};

static const std::unordered_map<std::string, NodeDispatchEntry> &GetNodeDispatchTable() {
  static const std::unordered_map<std::string, NodeDispatchEntry> table(
      std::begin(kNodeDispatchPairs), std::end(kNodeDispatchPairs));
  return table;
}

// Helper to convert a single MaterialX node to PrimSpec
// Returns true if successful (including skip case), false on error
// Sets is_skip to true if the node should be skipped (input/output)
static bool ConvertSingleNode(const tinyusdz::mtlx::pugi::xml_node &node,
                              PrimSpec &ps, bool &is_skip,
                              const MtlxConfig &config,
                              std::string *warn, std::string *err) {
  is_skip = false;
  std::string node_name = node.name();

  // Skip input/output nodes — handled separately
  if (node_name == "input" || node_name == "output") {
    is_skip = true;
    return true;
  }

  // Special-case nodes with custom conversion logic
  if (node_name == "place2d") {
    return ConvertPlace2d(node, ps, warn, err);
  }
  if (node_name == "tiledimage" || node_name == "image") {
    return ConvertTiledImage(node, ps, warn, err);
  }
  if (node_name == "texcoord") {
    return ConvertTexCoord(node, ps, config, warn, err);
  }
  // "extract" needs the input "in" type, not the node type
  if (node_name == "extract") {
    std::string info_id = "ND_extract_" + GetInputTypeAttr(node, "in", "color3");
    return ConvertGenericNode(node, ps, info_id, warn, err);
  }

  // "convert" needs from-type (input "in") and to-type (node type)
  if (node_name == "convert") {
    std::string from = GetInputTypeAttr(node, "in", "color3");
    std::string to = GetNodeTypeAttr(node, "vector3");
    return ConvertGenericNode(node, ps, "ND_convert_" + from + "_" + to, warn, err);
  }

  // Table-driven dispatch for all standard nodes
  const auto &table = GetNodeDispatchTable();
  auto it = table.find(node_name);
  if (it != table.end()) {
    const auto &e = it->second;
    std::string info_id;
    if (e.fixed_id) {
      info_id = e.fixed_id;
    } else {
      info_id = "ND_" + std::string(e.op_name) + "_" + GetNodeTypeAttr(node, e.default_type);
    }
    return ConvertGenericNode(node, ps, info_id, warn, err);
  }

  // Unknown node — use generic conversion with warning
  std::string type_str = GetNodeTypeAttr(node, "float");
  std::string info_id = "ND_" + node_name + "_" + type_str;
  PUSH_WARN(fmt::format("Unknown node type '{}', using generic conversion with info:id='{}'.\n", node_name, info_id));
  return ConvertGenericNode(node, ps, info_id, warn, err);
}

// Iterative version of ConvertNodeGraph using explicit stack
static bool ConvertNodeGraphIterative(const tinyusdz::mtlx::pugi::xml_node &root_node,
                                      PrimSpec &ps_out,
                                      const MtlxConfig &config,
                                      std::string *warn, std::string *err) {
  constexpr size_t kMaxDepth = 1024 * 1024;

  // Stack entry for iterative processing
  // We need to collect children into a vector since the iterator is temporary
  struct StackEntry {
    tinyusdz::mtlx::pugi::xml_node xml_node;
    std::vector<tinyusdz::mtlx::pugi::xml_node> children;
    size_t child_idx;
    PrimSpec ps;
    bool is_skip;

    explicit StackEntry(const tinyusdz::mtlx::pugi::xml_node &n)
        : xml_node(n), child_idx(0), is_skip(false) {
      // Collect children into vector
      for (auto it = n.begin(); it != n.end(); ++it) {
        children.push_back(*it);
      }
    }
  };

  std::vector<StackEntry> stack;
  stack.reserve(64);

  // Initialize with root node
  stack.emplace_back(root_node);

  // Convert root node
  if (!ConvertSingleNode(root_node, stack.back().ps, stack.back().is_skip, config, warn, err)) {
    return false;
  }

  while (!stack.empty()) {
    if (stack.size() > kMaxDepth) {
      PUSH_ERROR_AND_RETURN("Network too deep.\n");
    }

    StackEntry &curr = stack.back();

    // Check if there are more children to process
    if (curr.child_idx < curr.children.size()) {
      // Get current child and advance index
      tinyusdz::mtlx::pugi::xml_node child = curr.children[curr.child_idx];
      curr.child_idx++;

      // Push child onto stack
      stack.emplace_back(child);

      // Convert the child node
      if (!ConvertSingleNode(child, stack.back().ps, stack.back().is_skip, config, warn, err)) {
        return false;
      }
    } else {
      // All children processed
      if (stack.size() > 1) {
        // Move completed node to parent's children if not skipped and has name
        PrimSpec completed = std::move(curr.ps);
        bool was_skip = curr.is_skip;
        stack.pop_back();

        if (!was_skip && !completed.name().empty()) {
          stack.back().ps.children().emplace_back(std::move(completed));
        }
      } else {
        // Root node - copy to output
        if (!curr.is_skip) {
          ps_out = std::move(curr.ps);
        }
        stack.pop_back();
      }
    }
  }

  return true;
}

// Legacy wrapper - forwards to iterative version
// TODO: Remove this wrapper once all callers are updated to use ConvertNodeGraphIterative directly
static bool ConvertNodeGraphRec(const uint32_t depth,
                                const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps_out,
                                const MtlxConfig &config,
                                std::string *warn, std::string *err) {
  (void)depth;  // Iterative version handles depth internally
  return ConvertNodeGraphIterative(node, ps_out, config, warn, err);
}

// Process <include filename="..."/> elements by reading and merging included files.
// Replaces each <include .../> with the children of the included document's <materialx> root.
// @param[in] base_dir Base directory for resolving relative include paths.
// @param[in,out] xml_str The XML string to process. Modified in-place.
// @param[out] err Error message.
// @param[in] max_depth Maximum include recursion depth.
// @return true on success.
static bool ProcessIncludes(const std::string &base_dir, std::string &xml_str,
                            std::string *warn, std::string *err,
                            uint32_t max_depth = 8) {
  // Simple iterative approach: find and replace <include filename="..."/> tags
  // We iterate because included files may themselves contain includes.
  for (uint32_t depth = 0; depth < max_depth; depth++) {
    // Find <include
    size_t pos = xml_str.find("<include");
    if (pos == std::string::npos) {
      return true;  // No more includes
    }

    // Find the closing /> or >
    size_t end = xml_str.find("/>", pos);
    size_t end2 = xml_str.find(">", pos);
    if (end == std::string::npos && end2 == std::string::npos) {
      PUSH_ERROR_AND_RETURN("Unterminated <include> element");
    }

    size_t tag_end;
    size_t replace_end;
    if (end != std::string::npos && (end2 == std::string::npos || end <= end2)) {
      tag_end = end;
      replace_end = end + 2; // past "/>"
    } else {
      tag_end = end2;
      replace_end = end2 + 1; // past ">"
    }

    // Extract the tag content to find filename attribute
    std::string tag = xml_str.substr(pos, tag_end - pos);

    // Find filename="..." or filename='...'
    size_t fn_pos = tag.find("filename=");
    if (fn_pos == std::string::npos) {
      PUSH_ERROR_AND_RETURN("<include> element missing 'filename' attribute");
    }

    fn_pos += 9; // past "filename="
    if (fn_pos >= tag.size()) {
      PUSH_ERROR_AND_RETURN("<include> element has empty filename");
    }

    char quote = tag[fn_pos];
    if (quote != '"' && quote != '\'') {
      PUSH_ERROR_AND_RETURN("<include> filename must be quoted");
    }

    size_t fn_start = fn_pos + 1;
    size_t fn_end_q = tag.find(quote, fn_start);
    if (fn_end_q == std::string::npos) {
      PUSH_ERROR_AND_RETURN("<include> filename has unterminated quote");
    }

    std::string include_filename = tag.substr(fn_start, fn_end_q - fn_start);
    if (include_filename.empty()) {
      PUSH_ERROR_AND_RETURN("<include> filename is empty");
    }

    // Resolve include path relative to base directory
    std::string include_path;
    if (include_filename[0] == '/' || include_filename[0] == '\\') {
      include_path = include_filename; // Absolute path
    } else {
      include_path = base_dir;
      if (!include_path.empty() && include_path.back() != '/' && include_path.back() != '\\') {
        include_path += '/';
      }
      include_path += include_filename;
    }

    // Read the included file
    size_t inc_max_bytes = 1024 * 1024 * 16;
    std::vector<uint8_t> inc_data;
    if (!io::ReadWholeFile(&inc_data, err, include_path, inc_max_bytes, nullptr)) {
      PUSH_ERROR_AND_RETURN("Failed to read included file: " + include_path);
    }

    if (inc_data.empty()) {
      PushWarn("Included file is empty: " + include_path + "\n")
      // Just remove the <include> tag
      xml_str.erase(pos, replace_end - pos);
      continue;
    }

    std::string inc_str(reinterpret_cast<const char *>(inc_data.data()), inc_data.size());

    // Extract the content between <materialx ...> and </materialx> from the included file
    size_t mtlx_start = inc_str.find("<materialx");
    if (mtlx_start == std::string::npos) {
      PushWarn("Included file has no <materialx> root: " + include_path + "\n")
      xml_str.erase(pos, replace_end - pos);
      continue;
    }

    // Find the closing > of <materialx ...>
    size_t mtlx_open_end = inc_str.find(">", mtlx_start);
    if (mtlx_open_end == std::string::npos) {
      PUSH_ERROR_AND_RETURN("Unterminated <materialx> in included file: " + include_path);
    }

    // Check for self-closing <materialx/>
    if (inc_str[mtlx_open_end - 1] == '/') {
      // Empty document, just remove the include tag
      xml_str.erase(pos, replace_end - pos);
      continue;
    }

    size_t content_start = mtlx_open_end + 1;

    // Find </materialx>
    size_t mtlx_close = inc_str.find("</materialx>", content_start);
    if (mtlx_close == std::string::npos) {
      PUSH_ERROR_AND_RETURN("Missing </materialx> in included file: " + include_path);
    }

    // Extract the inner content
    std::string inner_content = inc_str.substr(content_start, mtlx_close - content_start);

    // Replace the <include .../> tag with the inner content
    xml_str.replace(pos, replace_end - pos, inner_content);
  }

  // If we exhausted max_depth, warn about possible circular includes
  if (xml_str.find("<include") != std::string::npos) {
    PushWarn("Maximum include depth reached. Possible circular includes.\n")
  }

  return true;
}

}  // namespace detail

bool ReadMaterialXFromString(const std::string &str,
                             const std::string &asset_path, MtlxModel *mtlx,
                             std::string *warn, std::string *err,
                             const MtlxConfig &config) {
#define GET_ATTR_VALUE(__xml, __name, __ty, __var)                        \
  do {                                                                    \
    tinyusdz::mtlx::pugi::xml_attribute attr = __xml.attribute(__name);                   \
    if (!attr) {                                                          \
      PUSH_ERROR_AND_RETURN(                                              \
          fmt::format("Required XML Attribute `{}` not found.", __name)); \
    }                                                                     \
    __ty v;                                                               \
    if (!detail::ParseMaterialXValue(attr.as_string(), &v, err)) {        \
      return false;                                                       \
    }                                                                     \
    __var = v;                                                            \
  } while (0)

#define GET_SHADER_PARAM(__name, __typeName, __inp_name, __tyname, __ty, \
                         __valuestr, __attr)                             \
  if (__name == __inp_name) {                                            \
    if (__typeName != __tyname) {                                        \
      PUSH_ERROR_AND_RETURN(                                             \
          fmt::format("type `{}` expected for input `{}`, but got `{}`", \
                      __typeName, __inp_name, __tyname));                \
    }                                                                    \
    __ty v;                                                              \
    if (!detail::ParseMaterialXValue(__valuestr, &v, err)) {             \
      return false;                                                      \
    }                                                                    \
    __attr.set_value(v);                                                 \
  } else

  if (!mtlx) {
    PUSH_ERROR_AND_RETURN("mtlx output pointer is nullptr.");
  }

  if (str.empty()) {
    PUSH_ERROR_AND_RETURN("Input MaterialX string is empty.");
  }

  tinyusdz::mtlx::pugi::xml_document doc;
  tinyusdz::mtlx::pugi::xml_parse_result result = doc.load_string(str.c_str());
  if (!result) {
    std::string msg(result.description());
    PUSH_ERROR_AND_RETURN("Failed to parse XML: " + msg);
  }

  tinyusdz::mtlx::pugi::xml_node root = doc.child("materialx");
  if (!root) {
    PUSH_ERROR_AND_RETURN("<materialx> tag not found: " + asset_path);
  }

  // Attributes for a <materialx> element:
  //
  tinyusdz::mtlx::pugi::xml_attribute ver_attr = root.attribute("version");
  if (!ver_attr) {
    PUSH_ERROR_AND_RETURN("version attribute not found in <materialx>:" +
                          asset_path);
  }

  // parse version string as floating point
  {
    DCOUT("version = " << ver_attr.as_string());
    float ver{0.0};
    if (!detail::ParseMaterialXValue(ver_attr.as_string(), &ver, err)) {
      return false;
    }

    if (ver < 1.38f) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("TinyUSDZ only supports MaterialX version 1.38 or "
                      "greater, but got {}",
                      ver_attr.as_string()));
    }
    mtlx->version = ver_attr.as_string();
  }

  tinyusdz::mtlx::pugi::xml_attribute cms_attr = root.attribute("cms");
  if (cms_attr) {
    mtlx->cms = cms_attr.as_string();
  }

  tinyusdz::mtlx::pugi::xml_attribute cmsconfig_attr = root.attribute("cmsconfig");
  if (cmsconfig_attr) {
    mtlx->cmsconfig = cmsconfig_attr.as_string();
  }
  tinyusdz::mtlx::pugi::xml_attribute colorspace_attr = root.attribute("colorspace");
  if (colorspace_attr) {
    mtlx->color_space = colorspace_attr.as_string();
  }

  tinyusdz::mtlx::pugi::xml_attribute namespace_attr = root.attribute("namespace");
  if (namespace_attr) {
    mtlx->name_space = namespace_attr.as_string();
  }

  std::map<std::string, PrimSpec> nodegraph_map;

  // NodeGraph
  for (auto ng : root.children("nodegraph")) {
    std::string ng_name;

    // Get nodegraph name
    tinyusdz::mtlx::pugi::xml_attribute name_attr = ng.attribute("name");
    if (!name_attr) {
      PUSH_WARN("NodeGraph without name attribute. Skipping.\n");
      continue;
    }
    ng_name = name_attr.as_string();

    PrimSpec ng_ps;
    ng_ps.name() = ng_name;
    ng_ps.specifier() = Specifier::Def;
    ng_ps.typeName() = kNodeGraph;

    // Process all child nodes
    for (auto child : ng) {
      std::string child_name = child.name();

      if (child_name == "output") {
        // Handle output declarations
        std::string output_name;
        std::string output_type;
        std::string nodename_ref;

        tinyusdz::mtlx::pugi::xml_attribute out_name_attr = child.attribute("name");
        if (out_name_attr) {
          output_name = out_name_attr.as_string();
        }

        tinyusdz::mtlx::pugi::xml_attribute out_type_attr = child.attribute("type");
        if (out_type_attr) {
          output_type = out_type_attr.as_string();
        }

        tinyusdz::mtlx::pugi::xml_attribute nodename_attr = child.attribute("nodename");
        if (nodename_attr) {
          nodename_ref = nodename_attr.as_string();

          // Create connection to the referenced node
          std::string connection_path = nodename_ref + ".outputs:out";

          // Store output as a connection property
          std::string prop_name = "outputs:" + output_name;
          // For now, store as a string connection path
          ng_ps.props()[prop_name] = Property(Attribute::Uniform(connection_path));
        }
      } else if (child_name == "input") {
        // Handle nodegraph inputs
        // TODO: Implement if needed
      } else {
        // Process shader nodes
        PrimSpec child_ps;
        if (detail::ConvertNodeGraphRec(0, child, child_ps, config, warn, err)) {
          if (!child_ps.name().empty()) {
            ng_ps.children().emplace_back(std::move(child_ps));
          }
        }
      }
    }

    nodegraph_map[ng_name] = std::move(ng_ps);
  }

  // Store nodegraphs in the model
  mtlx->nodegraphs = std::move(nodegraph_map);

  // standard_surface (Autodesk StandardSurface)
  for (auto sd_surface : root.children("standard_surface")) {
    std::string surface_name;
    {
      std::string typeName;
      GET_ATTR_VALUE(sd_surface, "name", std::string, surface_name);
      GET_ATTR_VALUE(sd_surface, "type", std::string, typeName);

      if (typeName != "surfaceshader") {
        PUSH_ERROR_AND_RETURN(
            fmt::format("`surfaceshader` expected for type of "
                        "standard_surface, but got `{}`",
                        typeName));
      }
    }

    MtlxAutodeskStandardSurface surface;
    for (auto inp : sd_surface.children("input")) {
      std::string name;
      std::string typeName;
      std::string valueStr;
      std::string nodegraphRef;
      std::string outputRef;
      std::string nodenameRef;

      GET_ATTR_VALUE(inp, "name", std::string, name);
      GET_ATTR_VALUE(inp, "type", std::string, typeName);

      // Check for value attribute (direct value)
      tinyusdz::mtlx::pugi::xml_attribute value_attr = inp.attribute("value");
      if (value_attr) {
        valueStr = value_attr.as_string();
      }

      // Check for connection attributes
      tinyusdz::mtlx::pugi::xml_attribute nodegraph_attr = inp.attribute("nodegraph");
      if (nodegraph_attr) {
        nodegraphRef = nodegraph_attr.as_string();
      }

      tinyusdz::mtlx::pugi::xml_attribute output_attr = inp.attribute("output");
      if (output_attr) {
        outputRef = output_attr.as_string();
      }

      tinyusdz::mtlx::pugi::xml_attribute nodename_attr = inp.attribute("nodename");
      if (nodename_attr) {
        nodenameRef = nodename_attr.as_string();
      }

      // Handle connections vs values
      bool is_connection = !nodegraphRef.empty() || !nodenameRef.empty();

      if (is_connection) {
        // Store connection information
        MtlxShaderConnection conn;
        conn.input_name = name;
        conn.nodegraph = nodegraphRef;
        conn.output = outputRef;
        conn.nodename = nodenameRef;
        mtlx->shader_connections[surface_name].push_back(conn);
        continue;  // Skip value parsing for connections
      }

      // Parse standard_surface direct values
      GET_SHADER_PARAM(name, typeName, "base", "float", float, valueStr, surface.base)
      GET_SHADER_PARAM(name, typeName, "base_color", "color3", value::color3f, valueStr, surface.base_color)
      GET_SHADER_PARAM(name, typeName, "diffuse_roughness", "float", float, valueStr, surface.diffuse_roughness)
      GET_SHADER_PARAM(name, typeName, "metalness", "float", float, valueStr, surface.metalness)
      GET_SHADER_PARAM(name, typeName, "specular", "float", float, valueStr, surface.specular)
      GET_SHADER_PARAM(name, typeName, "specular_color", "color3", value::color3f, valueStr, surface.specular_color)
      GET_SHADER_PARAM(name, typeName, "specular_roughness", "float", float, valueStr, surface.specular_roughness)
      GET_SHADER_PARAM(name, typeName, "specular_IOR", "float", float, valueStr, surface.specular_IOR)
      GET_SHADER_PARAM(name, typeName, "specular_anisotropy", "float", float, valueStr, surface.specular_anisotropy)
      GET_SHADER_PARAM(name, typeName, "specular_rotation", "float", float, valueStr, surface.specular_rotation)
      GET_SHADER_PARAM(name, typeName, "transmission", "float", float, valueStr, surface.transmission)
      GET_SHADER_PARAM(name, typeName, "transmission_color", "color3", value::color3f, valueStr, surface.transmission_color)
      GET_SHADER_PARAM(name, typeName, "transmission_depth", "float", float, valueStr, surface.transmission_depth)
      GET_SHADER_PARAM(name, typeName, "transmission_scatter", "color3", value::color3f, valueStr, surface.transmission_scatter)
      GET_SHADER_PARAM(name, typeName, "transmission_scatter_anisotropy", "float", float, valueStr, surface.transmission_scatter_anisotropy)
      GET_SHADER_PARAM(name, typeName, "transmission_dispersion", "float", float, valueStr, surface.transmission_dispersion)
      GET_SHADER_PARAM(name, typeName, "transmission_extra_roughness", "float", float, valueStr, surface.transmission_extra_roughness)
      GET_SHADER_PARAM(name, typeName, "subsurface", "float", float, valueStr, surface.subsurface)
      GET_SHADER_PARAM(name, typeName, "subsurface_color", "color3", value::color3f, valueStr, surface.subsurface_color)
      GET_SHADER_PARAM(name, typeName, "subsurface_radius", "float", float, valueStr, surface.subsurface_radius)
      GET_SHADER_PARAM(name, typeName, "subsurface_scale", "float", float, valueStr, surface.subsurface_scale)
      GET_SHADER_PARAM(name, typeName, "subsurface_anisotropy", "float", float, valueStr, surface.subsurface_anisotropy)
      GET_SHADER_PARAM(name, typeName, "sheen", "float", float, valueStr, surface.sheen)
      GET_SHADER_PARAM(name, typeName, "sheen_color", "color3", value::color3f, valueStr, surface.sheen_color)
      GET_SHADER_PARAM(name, typeName, "sheen_roughness", "float", float, valueStr, surface.sheen_roughness)
      GET_SHADER_PARAM(name, typeName, "coat", "float", float, valueStr, surface.coat)
      GET_SHADER_PARAM(name, typeName, "coat_color", "color3", value::color3f, valueStr, surface.coat_color)
      GET_SHADER_PARAM(name, typeName, "coat_roughness", "float", float, valueStr, surface.coat_roughness)
      GET_SHADER_PARAM(name, typeName, "coat_anisotropy", "float", float, valueStr, surface.coat_anisotropy)
      GET_SHADER_PARAM(name, typeName, "coat_rotation", "float", float, valueStr, surface.coat_rotation)
      GET_SHADER_PARAM(name, typeName, "coat_IOR", "float", float, valueStr, surface.coat_IOR)
      GET_SHADER_PARAM(name, typeName, "coat_affect_color", "float", float, valueStr, surface.coat_affect_color)
      GET_SHADER_PARAM(name, typeName, "coat_affect_roughness", "float", float, valueStr, surface.coat_affect_roughness)
      GET_SHADER_PARAM(name, typeName, "thin_film_thickness", "float", float, valueStr, surface.thin_film_thickness)
      GET_SHADER_PARAM(name, typeName, "thin_film_IOR", "float", float, valueStr, surface.thin_film_IOR)
      GET_SHADER_PARAM(name, typeName, "emission", "float", float, valueStr, surface.emission)
      GET_SHADER_PARAM(name, typeName, "emission_color", "color3", value::color3f, valueStr, surface.emission_color)
      GET_SHADER_PARAM(name, typeName, "opacity", "color3", value::color3f, valueStr, surface.opacity)
      GET_SHADER_PARAM(name, typeName, "thin_walled", "boolean", bool, valueStr, surface.thin_walled)
      GET_SHADER_PARAM(name, typeName, "normal", "vector3", value::normal3f, valueStr, surface.normal)
      GET_SHADER_PARAM(name, typeName, "tangent", "vector3", value::vector3f, valueStr, surface.tangent)
      {
        PUSH_WARN(fmt::format("Unknown/unsupported standard_surface input `{}`", name));
      }
    }

    mtlx->shaders[surface_name] = surface;
    if (mtlx->shader_name.empty()) {
      mtlx->shader_name = kMtlxAutodeskStandardSurface;
      mtlx->shader = surface;  // Set the primary shader value
    }
  }

  // uniform_edf
  for (auto uniform_edf : root.children("uniform_edf")) {
    std::string node_name;
    {
      std::string typeName;
      GET_ATTR_VALUE(uniform_edf, "name", std::string, node_name);
      GET_ATTR_VALUE(uniform_edf, "type", std::string, typeName);

      if (typeName != "EDF") {
        PUSH_ERROR_AND_RETURN(
            fmt::format("`EDF` expected for type of uniform_edf, but got `{}`",
                        typeName));
      }
    }

    MtlxUniformEdf edf;
    for (auto inp : uniform_edf.children("input")) {
      std::string name;
      std::string typeName;
      std::string valueStr;
      GET_ATTR_VALUE(inp, "name", std::string, name);
      GET_ATTR_VALUE(inp, "type", std::string, typeName);
      GET_ATTR_VALUE(inp, "value", std::string, valueStr);

      GET_SHADER_PARAM(name, typeName, "color", "color3", value::color3f,
                       valueStr, edf.color) {
        PUSH_WARN("Unknown/unsupported input " << name);
      }
    }

    mtlx->light_shaders[node_name] = edf;
  }

  // conical_edf
  for (auto conical_edf : root.children("conical_edf")) {
    std::string node_name;
    {
      std::string typeName;
      GET_ATTR_VALUE(conical_edf, "name", std::string, node_name);
      GET_ATTR_VALUE(conical_edf, "type", std::string, typeName);

      if (typeName != "EDF") {
        PUSH_ERROR_AND_RETURN(
            fmt::format("`EDF` expected for type of conical_edf, but got `{}`",
                        typeName));
      }
    }

    MtlxConicalEdf edf;
    for (auto inp : conical_edf.children("input")) {
      std::string name;
      std::string typeName;
      std::string valueStr;
      GET_ATTR_VALUE(inp, "name", std::string, name);
      GET_ATTR_VALUE(inp, "type", std::string, typeName);
      GET_ATTR_VALUE(inp, "value", std::string, valueStr);

      GET_SHADER_PARAM(name, typeName, "color", "color3", value::color3f,
                       valueStr, edf.color)
      GET_SHADER_PARAM(name, typeName, "normal", "vector3", value::normal3f,
                       valueStr, edf.normal)
      GET_SHADER_PARAM(name, typeName, "inner_angle", "float", float, valueStr,
                       edf.inner_angle)
      GET_SHADER_PARAM(name, typeName, "outer_angle", "float", float, valueStr,
                       edf.outer_angle) {
        PUSH_WARN("Unknown/unsupported input " << name);
      }
    }

    mtlx->light_shaders[node_name] = edf;
  }

  // measured_edf
  for (auto measured_edf : root.children("measured_edf")) {
    std::string node_name;
    {
      std::string typeName;
      GET_ATTR_VALUE(measured_edf, "name", std::string, node_name);
      GET_ATTR_VALUE(measured_edf, "type", std::string, typeName);

      if (typeName != "EDF") {
        PUSH_ERROR_AND_RETURN(
            fmt::format("`EDF` expected for type of measured_edf, but got `{}`",
                        typeName));
      }
    }

    MtlxMeasuredEdf edf;
    for (auto inp : measured_edf.children("input")) {
      std::string name;
      std::string typeName;
      std::string valueStr;
      GET_ATTR_VALUE(inp, "name", std::string, name);
      GET_ATTR_VALUE(inp, "type", std::string, typeName);
      GET_ATTR_VALUE(inp, "value", std::string, valueStr);

      GET_SHADER_PARAM(name, typeName, "color", "color3", value::color3f,
                       valueStr, edf.color)
      // file is a filename type
      if (name == "file") {
        if (typeName != "filename") {
          PUSH_ERROR_AND_RETURN(
              fmt::format("type `{}` expected for input `{}`, but got `{}`",
                          "filename", "file", typeName));
        }
        std::string filepath;
        if (!detail::ParseMaterialXValue(valueStr, &filepath, err)) {
          return false;
        }
        edf.file.set_value(value::AssetPath(filepath));
      } else {
        PUSH_WARN("Unknown/unsupported input " << name);
      }
    }

    mtlx->light_shaders[node_name] = edf;
  }

  // light
  for (auto light : root.children("light")) {
    std::string node_name;
    {
      std::string typeName;
      GET_ATTR_VALUE(light, "name", std::string, node_name);
      GET_ATTR_VALUE(light, "type", std::string, typeName);

      if (typeName != "lightshader") {
        PUSH_ERROR_AND_RETURN(
            fmt::format("`lightshader` expected for type of light, but got `{}`",
                        typeName));
      }
    }

    MtlxLight light_shader;
    for (auto inp : light.children("input")) {
      std::string name;
      std::string typeName;
      std::string valueStr;
      mtlx::pugi::xml_attribute nodename_attr = inp.attribute("nodename");

      GET_ATTR_VALUE(inp, "name", std::string, name);
      GET_ATTR_VALUE(inp, "type", std::string, typeName);

      // Handle connections via nodename
      if (nodename_attr) {
        std::string nodename = nodename_attr.as_string();
        if (name == "edf") {
          light_shader.edf.set_value(value::token(nodename));
        } else {
          PUSH_WARN("Unknown/unsupported connection input " << name);
        }
      } else {
        // Handle direct values
        GET_ATTR_VALUE(inp, "value", std::string, valueStr);

        GET_SHADER_PARAM(name, typeName, "intensity", "color3", value::color3f,
                         valueStr, light_shader.intensity)
        GET_SHADER_PARAM(name, typeName, "exposure", "float", float, valueStr,
                         light_shader.exposure) {
          PUSH_WARN("Unknown/unsupported input " << name);
        }
      }
    }

    mtlx->light_shaders[node_name] = light_shader;
  }

  // standard_surface
  for (auto usd_surface : root.children("UsdPreviewSurface")) {
    std::string surface_name;
    {
      std::string typeName;

      GET_ATTR_VALUE(usd_surface, "name", std::string, surface_name);
      GET_ATTR_VALUE(usd_surface, "type", std::string, typeName);

      if (typeName != "surfaceshader") {
        PUSH_ERROR_AND_RETURN(
            fmt::format("`surfaceshader` expected for type of "
                        "UsdPreviewSurface, but got `{}`",
                        typeName));
      }
    }

    MtlxUsdPreviewSurface surface;
    for (auto inp : usd_surface.children("input")) {
      std::string name;
      std::string typeName;
      std::string valueStr;
      std::string nodegraphRef;
      std::string outputRef;
      std::string nodenameRef;

      GET_ATTR_VALUE(inp, "name", std::string, name);
      GET_ATTR_VALUE(inp, "type", std::string, typeName);

      // Check for value attribute (direct value)
      tinyusdz::mtlx::pugi::xml_attribute value_attr = inp.attribute("value");
      if (value_attr) {
        valueStr = value_attr.as_string();
      }

      // Check for connection attributes
      tinyusdz::mtlx::pugi::xml_attribute nodegraph_attr = inp.attribute("nodegraph");
      if (nodegraph_attr) {
        nodegraphRef = nodegraph_attr.as_string();
      }

      tinyusdz::mtlx::pugi::xml_attribute output_attr = inp.attribute("output");
      if (output_attr) {
        outputRef = output_attr.as_string();
      }

      tinyusdz::mtlx::pugi::xml_attribute nodename_attr = inp.attribute("nodename");
      if (nodename_attr) {
        nodenameRef = nodename_attr.as_string();
      }

      // Handle connections vs values
      bool is_connection = !nodegraphRef.empty() || !nodenameRef.empty();

      if (is_connection) {
        // This is a connection, not a direct value
        // Store connection information
        MtlxShaderConnection conn;
        conn.input_name = name;
        conn.nodegraph = nodegraphRef;
        conn.output = outputRef;
        conn.nodename = nodenameRef;
        mtlx->shader_connections[surface_name].push_back(conn);
        continue;  // Skip value parsing for connections
      }

      // Parse direct values
      GET_SHADER_PARAM(name, typeName, "diffuseColor", "color3", value::color3f,
                       valueStr, surface.diffuseColor)
      GET_SHADER_PARAM(name, typeName, "emissiveColor", "color3",
                       value::color3f, valueStr, surface.emissiveColor)
      GET_SHADER_PARAM(name, typeName, "useSpecularWorkflow", "integer", int,
                       valueStr, surface.useSpecularWorkflow)
      GET_SHADER_PARAM(name, typeName, "specularColor", "color3",
                       value::color3f, valueStr, surface.specularColor)
      GET_SHADER_PARAM(name, typeName, "metallic", "float", float, valueStr,
                       surface.metallic)
      GET_SHADER_PARAM(name, typeName, "roughness", "float", float, valueStr,
                       surface.roughness)
      GET_SHADER_PARAM(name, typeName, "clearcoat", "float", float, valueStr,
                       surface.clearcoat)
      GET_SHADER_PARAM(name, typeName, "clearcoatRoughness", "float", float,
                       valueStr, surface.clearcoatRoughness)
      GET_SHADER_PARAM(name, typeName, "opacity", "float", float, valueStr,
                       surface.opacity)
      GET_SHADER_PARAM(name, typeName, "opacityThreshold", "float", float,
                       valueStr, surface.opacityThreshold)
      GET_SHADER_PARAM(name, typeName, "ior", "float", float, valueStr,
                       surface.ior)
      GET_SHADER_PARAM(name, typeName, "normal", "vector3f", value::normal3f,
                       valueStr, surface.normal)
      GET_SHADER_PARAM(name, typeName, "displacement", "float", float, valueStr,
                       surface.displacement)
      GET_SHADER_PARAM(name, typeName, "occlusion", "float", float, valueStr,
                       surface.occlusion) {
        PUSH_WARN("Unknown/unsupported input " << name);
      }
    }

    mtlx->shaders[surface_name] = surface;
    if (mtlx->shader_name.empty()) {
      mtlx->shader_name = kUsdPreviewSurface;
      mtlx->shader = surface;  // Set the primary shader value
    }
  }

  // OpenPBR Surface - check both "OpenPBRSurface" and "open_pbr_surface"
  std::vector<tinyusdz::mtlx::pugi::xml_node> openpbr_nodes;
  {
    auto nodes1 = root.children("OpenPBRSurface");
    auto nodes2 = root.children("open_pbr_surface");
    openpbr_nodes.insert(openpbr_nodes.end(), nodes1.begin(), nodes1.end());
    openpbr_nodes.insert(openpbr_nodes.end(), nodes2.begin(), nodes2.end());
  }

  for (auto openpbr_surface : openpbr_nodes) {
    std::string surface_name;
    {
      std::string typeName;
      GET_ATTR_VALUE(openpbr_surface, "name", std::string, surface_name);
      GET_ATTR_VALUE(openpbr_surface, "type", std::string, typeName);
      if (typeName != "surfaceshader") {
        PUSH_ERROR_AND_RETURN(
            fmt::format("`surfaceshader` expected for type of "
                        "OpenPBRSurface, but got `{}`",
                        typeName));
      }
    }
    
    OpenPBRSurface surface;
    for (auto inp : openpbr_surface.children("input")) {
      std::string name;
      std::string typeName;
      std::string valueStr;
      std::string nodegraphRef;
      std::string outputRef;
      std::string nodenameRef;

      GET_ATTR_VALUE(inp, "name", std::string, name);
      GET_ATTR_VALUE(inp, "type", std::string, typeName);

      // Check for value attribute (direct value)
      tinyusdz::mtlx::pugi::xml_attribute value_attr = inp.attribute("value");
      if (value_attr) {
        valueStr = value_attr.as_string();
      }

      // Check for connection attributes
      tinyusdz::mtlx::pugi::xml_attribute nodegraph_attr = inp.attribute("nodegraph");
      if (nodegraph_attr) {
        nodegraphRef = nodegraph_attr.as_string();
      }

      tinyusdz::mtlx::pugi::xml_attribute output_attr = inp.attribute("output");
      if (output_attr) {
        outputRef = output_attr.as_string();
      }

      tinyusdz::mtlx::pugi::xml_attribute nodename_attr = inp.attribute("nodename");
      if (nodename_attr) {
        nodenameRef = nodename_attr.as_string();
      }

      // Handle connections vs values
      bool is_connection = !nodegraphRef.empty() || !nodenameRef.empty();

      if (is_connection) {
        // Store connection information
        MtlxShaderConnection conn;
        conn.input_name = name;
        conn.nodegraph = nodegraphRef;
        conn.output = outputRef;
        conn.nodename = nodenameRef;
        mtlx->shader_connections[surface_name].push_back(conn);
        continue;  // Skip value parsing for connections
      }

      // Parse OpenPBR direct values
      GET_SHADER_PARAM(name, typeName, "base_weight", "float", float, valueStr, surface.base_weight)
      GET_SHADER_PARAM(name, typeName, "base_color", "color3", value::color3f, valueStr, surface.base_color)
      GET_SHADER_PARAM(name, typeName, "base_roughness", "float", float, valueStr, surface.base_roughness)
      GET_SHADER_PARAM(name, typeName, "base_metalness", "float", float, valueStr, surface.base_metalness)
      GET_SHADER_PARAM(name, typeName, "specular_weight", "float", float, valueStr, surface.specular_weight)
      GET_SHADER_PARAM(name, typeName, "specular_color", "color3", value::color3f, valueStr, surface.specular_color)
      GET_SHADER_PARAM(name, typeName, "specular_roughness", "float", float, valueStr, surface.specular_roughness)
      GET_SHADER_PARAM(name, typeName, "specular_ior", "float", float, valueStr, surface.specular_ior)
      GET_SHADER_PARAM(name, typeName, "specular_ior_level", "float", float, valueStr, surface.specular_ior_level)
      GET_SHADER_PARAM(name, typeName, "specular_anisotropy", "float", float, valueStr, surface.specular_anisotropy)
      GET_SHADER_PARAM(name, typeName, "specular_rotation", "float", float, valueStr, surface.specular_rotation)
      GET_SHADER_PARAM(name, typeName, "transmission_weight", "float", float, valueStr, surface.transmission_weight)
      GET_SHADER_PARAM(name, typeName, "transmission_color", "color3", value::color3f, valueStr, surface.transmission_color)
      GET_SHADER_PARAM(name, typeName, "transmission_depth", "float", float, valueStr, surface.transmission_depth)
      GET_SHADER_PARAM(name, typeName, "transmission_scatter", "color3", value::color3f, valueStr, surface.transmission_scatter)
      GET_SHADER_PARAM(name, typeName, "transmission_scatter_anisotropy", "float", float, valueStr, surface.transmission_scatter_anisotropy)
      GET_SHADER_PARAM(name, typeName, "transmission_dispersion", "float", float, valueStr, surface.transmission_dispersion)
      GET_SHADER_PARAM(name, typeName, "subsurface_weight", "float", float, valueStr, surface.subsurface_weight)
      GET_SHADER_PARAM(name, typeName, "subsurface_color", "color3", value::color3f, valueStr, surface.subsurface_color)
      GET_SHADER_PARAM(name, typeName, "subsurface_radius", "float", float, valueStr, surface.subsurface_radius)
      GET_SHADER_PARAM(name, typeName, "subsurface_radius_scale", "color3", value::color3f, valueStr, surface.subsurface_radius_scale)
      GET_SHADER_PARAM(name, typeName, "subsurface_scale", "float", float, valueStr, surface.subsurface_scale)
      GET_SHADER_PARAM(name, typeName, "subsurface_anisotropy", "float", float, valueStr, surface.subsurface_anisotropy)
      GET_SHADER_PARAM(name, typeName, "sheen_weight", "float", float, valueStr, surface.sheen_weight)
      GET_SHADER_PARAM(name, typeName, "sheen_color", "color3", value::color3f, valueStr, surface.sheen_color)
      GET_SHADER_PARAM(name, typeName, "sheen_roughness", "float", float, valueStr, surface.sheen_roughness)
      GET_SHADER_PARAM(name, typeName, "coat_weight", "float", float, valueStr, surface.coat_weight)
      GET_SHADER_PARAM(name, typeName, "coat_color", "color3", value::color3f, valueStr, surface.coat_color)
      GET_SHADER_PARAM(name, typeName, "coat_roughness", "float", float, valueStr, surface.coat_roughness)
      GET_SHADER_PARAM(name, typeName, "coat_anisotropy", "float", float, valueStr, surface.coat_anisotropy)
      GET_SHADER_PARAM(name, typeName, "coat_rotation", "float", float, valueStr, surface.coat_rotation)
      GET_SHADER_PARAM(name, typeName, "coat_ior", "float", float, valueStr, surface.coat_ior)
      GET_SHADER_PARAM(name, typeName, "coat_affect_color", "float", float, valueStr, surface.coat_affect_color)
      GET_SHADER_PARAM(name, typeName, "coat_affect_roughness", "float", float, valueStr, surface.coat_affect_roughness)
      GET_SHADER_PARAM(name, typeName, "emission_luminance", "float", float, valueStr, surface.emission_luminance)
      GET_SHADER_PARAM(name, typeName, "emission_color", "color3", value::color3f, valueStr, surface.emission_color)
      GET_SHADER_PARAM(name, typeName, "opacity", "float", float, valueStr, surface.opacity)
      GET_SHADER_PARAM(name, typeName, "normal", "vector3", value::normal3f, valueStr, surface.normal)
      GET_SHADER_PARAM(name, typeName, "tangent", "vector3", value::vector3f, valueStr, surface.tangent)
      {
        PUSH_WARN(fmt::format("TODO: OpenPBR input `{}`", name));
      }
    }
    
    mtlx->shaders[surface_name] = surface;
    if (mtlx->shader_name.empty()) {
      mtlx->shader_name = kOpenPBRSurface;
      mtlx->shader = surface;  // Set the primary shader value
    }
  }

  // surfacematerial
  for (auto surfacematerial : root.children("surfacematerial")) {
    std::string material_name;
    {
      std::string typeName;
      GET_ATTR_VALUE(surfacematerial, "name", std::string, material_name);
      GET_ATTR_VALUE(surfacematerial, "type", std::string, typeName);

      if (typeName != "material") {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "`material` expected for type of surfacematerial, but got `{}`",
            typeName));
      }
    }

    std::string typeName;
    std::string nodename;
    for (auto inp : surfacematerial.children("input")) {
      std::string name;
      GET_ATTR_VALUE(inp, "name", std::string, name);
      GET_ATTR_VALUE(inp, "type", std::string, typeName);
      GET_ATTR_VALUE(inp, "nodename", std::string, nodename);

      if (name != "surfaceshader") {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Currently only `surfaceshader` supported for "
                        "`surfacematerial`'s input, but got `{}`",
                        name));
      }

      if (typeName != "surfaceshader") {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Currently only `surfaceshader` supported for "
                        "`surfacematerial` input type, but got `{}`",
                        typeName));
      }
    }

    MtlxMaterial mat;
    mat.name = material_name;
    mat.typeName = typeName;
    mat.nodename = nodename;
    mtlx->surface_materials[material_name] = mat;
  }

  // look.
  for (auto look : root.children("look")) {
    PUSH_WARN("TODO: `look`");
    (void)look;
  }

#undef GET_ATTR_VALUE

  return true;
}

bool ReadMaterialXFromFile(const AssetResolutionResolver &resolver,
                           const std::string &asset_path, MtlxModel *mtlx,
                           std::string *warn, std::string *err,
                           const MtlxConfig &config) {
  if (!mtlx) {
    PUSH_ERROR_AND_RETURN("mtlx output pointer is nullptr.");
  }

  std::string filepath = resolver.resolve(asset_path);
  if (filepath.empty()) {
    PUSH_ERROR_AND_RETURN("Asset not found: " + asset_path);
  }

  // up to 16MB xml
  size_t max_bytes = 1024 * 1024 * 16;

  std::vector<uint8_t> data;
  if (!io::ReadWholeFile(&data, err, filepath, max_bytes,
                         /* userdata */ nullptr)) {
    PUSH_ERROR_AND_RETURN("Read file failed: " + filepath);
  }

  if (data.empty()) {
    PUSH_ERROR_AND_RETURN("File is empty: " + filepath);
  }

  std::string str(reinterpret_cast<const char *>(data.data()), data.size());

  // Process <include filename="..."/> elements
  // Compute base directory from the resolved file path
  std::string base_dir;
  {
    size_t last_sep = filepath.find_last_of("/\\");
    if (last_sep != std::string::npos) {
      base_dir = filepath.substr(0, last_sep);
    } else {
      base_dir = ".";
    }
  }

  if (!detail::ProcessIncludes(base_dir, str, warn, err)) {
    return false;
  }

  return ReadMaterialXFromString(str, asset_path, mtlx, warn, err, config);
}

bool WriteMaterialXToString(const MtlxModel &mtlx, std::string &xml_str,
                            std::string *warn, std::string *err) {
  // Find shader name - use the first shader in the shaders map if available
  // Priority: shader key from shaders map > mtlx.shader_name
  std::string shader_name;
  if (!mtlx.shaders.empty()) {
    shader_name = mtlx.shaders.begin()->first;
  } else {
    shader_name = mtlx.shader_name;
  }

  // Get connections for this shader
  std::vector<MtlxShaderConnection> connections;
  auto it = mtlx.shader_connections.find(shader_name);
  if (it != mtlx.shader_connections.end()) {
    connections = it->second;
  }

  // OpenPBR is the primary active path (used by Blender exports)
  if (auto openpbr = mtlx.shader.as<MtlxOpenPBRSurface>()) {
    return detail::WriteMaterialXToString(*openpbr, shader_name, connections, mtlx.nodegraphs, mtlx.color_space, xml_str, warn, err);
  }

#if TINYUSDZ_MTLX_ENABLE_USDPREVIEWSURFACE_EXPORT
  if (auto usdps = mtlx.shader.as<MtlxUsdPreviewSurface>()) {
    return detail::WriteMaterialXToString(*usdps, shader_name, connections, mtlx.nodegraphs, mtlx.color_space, xml_str, warn, err);
  }
#endif

#if TINYUSDZ_MTLX_ENABLE_STANDARDSURFACE_EXPORT
  if (auto adskss = mtlx.shader.as<MtlxAutodeskStandardSurface>()) {
    return detail::WriteMaterialXToString(*adskss, shader_name, connections, mtlx.nodegraphs, mtlx.color_space, xml_str, warn, err);
  }
#endif

  // Fallback error for unsupported shader types
  PUSH_ERROR_AND_RETURN("Unknown/unsupported shader type: " << mtlx.shader_name);

  return false;
}

bool ToPrimSpec(const MtlxModel &model, PrimSpec &ps, std::string *err) {
  //
  // def "MaterialX" {
  //
  //   def "Materials" {
  //     def Material ... {
  //     }
  //   }
  //   def "Shaders" {
  //   }
  constexpr auto kAutodeskStandardSurface = "AutodeskStandardSurface";

  if (model.shader_name == kUsdPreviewSurface) {
    ps.props()["info:id"] =
        detail::MakeProperty(value::token(kUsdPreviewSurface));
  } else if (model.shader_name == kAutodeskStandardSurface) {
    ps.props()["info:id"] =
        detail::MakeProperty(value::token(kAutodeskStandardSurface));
  } else if (model.shader_name == kOpenPBRSurface) {
    ps.props()["info:id"] =
        detail::MakeProperty(value::token(kOpenPBRSurface));
  } else {
    PUSH_ERROR_AND_RETURN("Unsupported shader_name: " << model.shader_name);
  }

  PrimSpec materials;
  materials.name() = "Materials";
  materials.specifier() = Specifier::Def;

  for (const auto &item : model.surface_materials) {
    PrimSpec material;
    material.specifier() = Specifier::Def;
    material.typeName() = "Material";
    material.name() = item.second.name;

    // Add MaterialXConfigAPI with version from MaterialX
    if (!model.version.empty()) {
      material.props()["config:mtlx:version"] =
          detail::MakeProperty(model.version);
    }

    materials.children().push_back(std::move(material));
  }

  PrimSpec shaders;
  shaders.name() = "Shaders";
  shaders.specifier() = Specifier::Def;

  // Serialize shader values as child PrimSpecs
  for (const auto &shader_item : model.shaders) {
    PrimSpec shader_ps;
    shader_ps.name() = shader_item.first;
    shader_ps.specifier() = Specifier::Def;
    shader_ps.typeName() = kShader;

    // Set info:id based on shader type
    if (shader_item.second.as<MtlxOpenPBRSurface>()) {
      shader_ps.props()[kShaderInfoId] =
          detail::MakeProperty(value::token(kMtlxOpenPBRSurface));
    } else if (shader_item.second.as<MtlxAutodeskStandardSurface>()) {
      shader_ps.props()[kShaderInfoId] =
          detail::MakeProperty(value::token("AutodeskStandardSurface"));
    } else if (shader_item.second.as<MtlxUsdPreviewSurface>()) {
      shader_ps.props()[kShaderInfoId] =
          detail::MakeProperty(value::token(kUsdPreviewSurface));
    }

    shaders.children().push_back(std::move(shader_ps));
  }

  // Add NodeGraphs container
  PrimSpec nodegraphs;
  nodegraphs.name() = "NodeGraphs";
  nodegraphs.specifier() = Specifier::Def;

  // Add all nodegraphs
  for (const auto &ng_item : model.nodegraphs) {
    PrimSpec ng_copy = ng_item.second; // Copy the nodegraph PrimSpec
    nodegraphs.children().push_back(std::move(ng_copy));
  }

  PrimSpec root;
  root.name() = "MaterialX";
  root.specifier() = Specifier::Def;

  root.children().push_back(materials);
  root.children().push_back(shaders);
  if (!model.nodegraphs.empty()) {
    root.children().push_back(nodegraphs);
  }

  ps = std::move(root);

  return true;
}

bool LoadMaterialXFromAsset(const Asset &asset, const std::string &asset_path,
                            PrimSpec &ps /* inout */, std::string *warn,
                            std::string *err) {
  (void)asset_path;
  (void)warn;

  if (!asset.data() || asset.size() < 32) {
    if (err) {
      (*err) += "MaterialX: Asset data is null or too small.\n";
    }
    return false;
  }

  std::string str(reinterpret_cast<const char *>(asset.data()), asset.size());

  MtlxModel mtlx;
  if (!ReadMaterialXFromString(str, asset_path, &mtlx, warn, err)) {
    PUSH_ERROR_AND_RETURN("Failed to read MaterialX.");
  }

  if (!ToPrimSpec(mtlx, ps, err)) {
    PUSH_ERROR_AND_RETURN("Failed to convert MaterialX to USD PrimSpec.");
  }

  return true;
}

///
/// Convert MaterialX Light shader to UsdLux light
///
bool ConvertMtlxLightToUsdLux(const MtlxLight &mtlx_light,
                               const std::map<std::string, value::Value> &light_shaders,
                               value::Value *usd_light,
                               std::string *warn, std::string *err) {
  (void)warn;

  if (!usd_light) {
    PUSH_ERROR_AND_RETURN("usd_light is nullptr");
  }

  // Get the EDF node name from the light shader
  value::token edf_name;
  if (!mtlx_light.edf.get_value(&edf_name)) {
    PUSH_ERROR_AND_RETURN("Light shader has no EDF connection");
  }

  // Find the EDF node in light_shaders
  auto edf_it = light_shaders.find(edf_name.str());
  if (edf_it == light_shaders.end()) {
    PUSH_ERROR_AND_RETURN(fmt::format("EDF node '{}' not found", edf_name.str()));
  }

  const value::Value &edf_value = edf_it->second;

  // Get intensity and exposure from the light shader
  value::color3f intensity{1.0f, 1.0f, 1.0f};
  mtlx_light.intensity.get_value().get_scalar(&intensity);

  float exposure = 0.0f;
  if (mtlx_light.exposure.authored()) {
    if (auto exp_val = mtlx_light.exposure.get_value()) {
      exp_val.value().get_scalar(&exposure);
    }
  }

  // Convert based on EDF type
  if (auto uniform_edf = edf_value.as<MtlxUniformEdf>()) {
    // uniform_edf -> SphereLight (omnidirectional point light)
    SphereLight light;

    value::color3f edf_color{1.0f, 1.0f, 1.0f};
    uniform_edf->color.get_value().get_scalar(&edf_color);

    // Combine EDF color with light intensity
    value::color3f final_color{
      edf_color[0] * intensity[0],
      edf_color[1] * intensity[1],
      edf_color[2] * intensity[2]
    };

    light.color.set_value(final_color);
    light.exposure.set_value(exposure);
    light.intensity.set_value(1.0f); // Already baked into color

    (*usd_light) = light;
    return true;

  } else if (auto conical_edf = edf_value.as<MtlxConicalEdf>()) {
    // conical_edf -> RectLight with ShapingAPI (spot light effect)
    RectLight light;

    value::color3f edf_color{1.0f, 1.0f, 1.0f};
    conical_edf->color.get_value().get_scalar(&edf_color);

    // Combine EDF color with light intensity
    value::color3f final_color{
      edf_color[0] * intensity[0],
      edf_color[1] * intensity[1],
      edf_color[2] * intensity[2]
    };

    light.color.set_value(final_color);
    light.exposure.set_value(exposure);
    light.intensity.set_value(1.0f);

    // Set ShapingAPI attributes for cone control (inherited from BoundableLight)
    float inner_angle = 60.0f;
    conical_edf->inner_angle.get_value().get_scalar(&inner_angle);
    light.shapingConeAngle.set_value(inner_angle);

    if (conical_edf->outer_angle.authored()) {
      float outer_angle = 60.0f;
      if (auto oa_val = conical_edf->outer_angle.get_value()) {
        oa_val.value().get_scalar(&outer_angle);
        // Use softness to represent the difference between inner and outer angles
        float softness = (outer_angle - inner_angle) / inner_angle;
        light.shapingConeSoftness.set_value(std::max(0.0f, softness));
      }
    }

    (*usd_light) = light;
    return true;

  } else if (auto measured_edf = edf_value.as<MtlxMeasuredEdf>()) {
    // measured_edf -> RectLight or SphereLight with IES profile via ShapingAPI
    SphereLight light;

    value::color3f edf_color{1.0f, 1.0f, 1.0f};
    measured_edf->color.get_value().get_scalar(&edf_color);

    // Combine EDF color with light intensity
    value::color3f final_color{
      edf_color[0] * intensity[0],
      edf_color[1] * intensity[1],
      edf_color[2] * intensity[2]
    };

    light.color.set_value(final_color);
    light.exposure.set_value(exposure);
    light.intensity.set_value(1.0f);

    // Set ShapingAPI attributes with IES profile (inherited from BoundableLight)
    if (measured_edf->file.authored()) {
      if (auto file_val = measured_edf->file.get_value()) {
        value::AssetPath ies_file;
        if (file_val.value().get_scalar(&ies_file)) {
          light.shapingIesFile.set_value(ies_file);
          light.shapingIesNormalize.set_value(true);
        }
      }
    }

    (*usd_light) = light;
    return true;

  } else {
    PUSH_ERROR_AND_RETURN(fmt::format("Unknown EDF type for node '{}'", edf_name.str()));
  }

  return false;
}

//} // namespace usdMtlx
}  // namespace tinyusdz

#else

namespace tinyusdz {

bool ReadMaterialXFromString(const std::string &str,
                             const std::string &asset_name, MtlxModel *mtlx,
                             std::string *warn, std::string *err,
                             const MtlxConfig &config) {
  (void)str;
  (void)asset_name;
  (void)mtlx;
  (void)warn;
  (void)config;

  if (err) {
    (*err) += "MaterialX support is disabled in this build.\n";
  }
  return false;
}

bool ReadMaterialXFromFile(const AssetResolutionResolver &resolver,
                           const std::string &asset_path, MtlxModel *mtlx,
                           std::string *warn, std::string *err,
                           const MtlxConfig &config) {
  (void)resolver;
  (void)asset_path;
  (void)mtlx;
  (void)warn;
  (void)config;

  if (err) {
    (*err) += "MaterialX support is disabled in this build.\n";
  }
  return false;
}

bool WriteMaterialXToString(const MtlxModel &mtlx, std::string &xml_str,
                            std::string *warn, std::string *err) {
  (void)mtlx;
  (void)xml_str;
  (void)warn;

  if (err) {
    (*err) += "MaterialX support is disabled in this build.\n";
  }
  return false;
}

bool ToPrimSpec(const MtlxModel &model, PrimSpec &ps, std::string *err) {
  (void)model;
  (void)ps;

  if (err) {
    (*err) += "MaterialX support is disabled in this build.\n";
  }
  return false;
}

bool LoadMaterialXFromAsset(const Asset &asset, const std::string &asset_path,
                            PrimSpec &ps /* inout */, std::string *warn,
                            std::string *err) {
  (void)asset;
  (void)asset_path;
  (void)ps;
  (void)warn;

  if (err) {
    (*err) += "MaterialX support is disabled in this build.\n";
  }

  return false;
}

bool ConvertMtlxLightToUsdLux(const MtlxLight &mtlx_light,
                               const std::map<std::string, value::Value> &light_shaders,
                               value::Value *usd_light,
                               std::string *warn, std::string *err) {
  (void)mtlx_light;
  (void)light_shaders;
  (void)usd_light;
  (void)warn;

  if (err) {
    (*err) += "MaterialX support is disabled in this build.\n";
  }
  return false;
}

}  // namespace tinyusdz

#endif  // TINYUSDZ_USE_USDMTLX
