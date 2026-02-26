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
#define TINYUSDZ_MTLX_ENABLE_USDPREVIEWSURFACE_EXPORT 0
#define TINYUSDZ_MTLX_ENABLE_STANDARDSURFACE_EXPORT 0

#include "ascii-parser.hh"  // To parse color3f value
#include "common-macros.inc"
#include "io-util.hh"
#include "pprinter.hh"
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

  // TODO
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
  (void)value;

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
            if (attr.get_var().is_scalar()) {
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

static bool ConvertPlace2d(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                           std::string *warn, std::string *err) {
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
    PUSH_WARN("TODO: `texcoord` attribute.\n");
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

  ps.specifier() = Specifier::Def;
  ps.typeName() = kShader;
  ps.props()[kShaderInfoId] =
      Property(Attribute::Uniform(value::token(kUsdTransform2d)));

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

    // Map MaterialX inputs to USD inputs
    if (input_name == "file" && input_type == "filename") {
      // Convert filename to asset path
      ps.props()["inputs:file"] = Property(Attribute::Uniform(value::AssetPath(input_value)));
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
  }

  ps.specifier() = Specifier::Def;
  ps.typeName() = kShader;
  ps.props()[kShaderInfoId] = Property(Attribute::Uniform(value::token(kUsdUVTexture)));

  return true;
}

// Convert MaterialX image node to USD UsdUVTexture
static bool ConvertImage(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                         std::string *warn, std::string *err) {
  // Image node is similar to tiledimage but without tiling parameters
  return ConvertTiledImage(node, ps, warn, err);
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

  ps.specifier() = Specifier::Def;
  ps.typeName() = kShader;
  ps.props()[kShaderInfoId] = Property(Attribute::Uniform(value::token(kUsdPrimvarReader_float2)));

  return true;
}

// Convert MaterialX constant node - stores a constant value
static bool ConvertConstant(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                            std::string *warn, std::string *err) {
  (void)warn;

  tinyusdz::mtlx::pugi::xml_attribute name_attr = node.attribute("name");
  if (name_attr) {
    ps.name() = name_attr.as_string();
  }

  // Get the type attribute to determine what kind of constant
  tinyusdz::mtlx::pugi::xml_attribute type_attr = node.attribute("type");
  std::string node_type = type_attr ? type_attr.as_string() : "float";

  // Parse value from input child or value attribute
  for (auto inp : node.children("input")) {
    tinyusdz::mtlx::pugi::xml_attribute inp_name_attr = inp.attribute("name");
    if (!inp_name_attr || std::string(inp_name_attr.as_string()) != "value") continue;

    tinyusdz::mtlx::pugi::xml_attribute value_attr = inp.attribute("value");
    if (!value_attr) continue;

    std::string value_str = value_attr.as_string();

    if (node_type == "float") {
      float val;
      if (ParseMaterialXValue(value_str, &val, err)) {
        ps.props()["inputs:value"] = Property(Attribute::Uniform(val));
      }
    } else if (node_type == "color3") {
      value::color3f val;
      if (ParseMaterialXValue(value_str, &val, err)) {
        ps.props()["inputs:value"] = Property(Attribute::Uniform(val));
      }
    } else if (node_type == "vector3") {
      value::float3 val;
      if (ParseMaterialXValue(value_str, &val, err)) {
        ps.props()["inputs:value"] = Property(Attribute::Uniform(val));
      }
    }
  }

  ps.specifier() = Specifier::Def;
  ps.typeName() = kShader;
  // Use a generic shader ID for constant nodes
  ps.props()[kShaderInfoId] = Property(Attribute::Uniform(value::token("MaterialXConstant")));

  return true;
}

// Convert MaterialX multiply node
static bool ConvertMultiply(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                            std::string *warn, std::string *err) {
  (void)warn;

  tinyusdz::mtlx::pugi::xml_attribute name_attr = node.attribute("name");
  if (name_attr) {
    ps.name() = name_attr.as_string();
  }

  // Parse inputs (in1, in2)
  for (auto inp : node.children("input")) {
    std::string input_name;
    std::string input_value;

    tinyusdz::mtlx::pugi::xml_attribute inp_name_attr = inp.attribute("name");
    if (inp_name_attr) {
      input_name = inp_name_attr.as_string();
    }

    tinyusdz::mtlx::pugi::xml_attribute value_attr = inp.attribute("value");
    if (value_attr) {
      input_value = value_attr.as_string();

      // Store as shader input
      std::string prop_name = "inputs:" + input_name;

      tinyusdz::mtlx::pugi::xml_attribute type_attr = inp.attribute("type");
      if (type_attr) {
        std::string type_str = type_attr.as_string();
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
        }
      }
    }
  }

  ps.specifier() = Specifier::Def;
  ps.typeName() = kShader;
  ps.props()[kShaderInfoId] = Property(Attribute::Uniform(value::token("MaterialXMultiply")));

  return true;
}

// Convert MaterialX add node
static bool ConvertAdd(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                       std::string *warn, std::string *err) {
  // Same pattern as multiply
  return ConvertMultiply(node, ps, warn, err);  // Reuse multiply logic
}

// Convert MaterialX mix node (linear blend)
static bool ConvertMix(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                       std::string *warn, std::string *err) {
  (void)warn;

  tinyusdz::mtlx::pugi::xml_attribute name_attr = node.attribute("name");
  if (name_attr) {
    ps.name() = name_attr.as_string();
  }

  // Parse inputs (fg, bg, mix)
  for (auto inp : node.children("input")) {
    std::string input_name;
    std::string input_value;

    tinyusdz::mtlx::pugi::xml_attribute inp_name_attr = inp.attribute("name");
    if (inp_name_attr) {
      input_name = inp_name_attr.as_string();
    }

    tinyusdz::mtlx::pugi::xml_attribute value_attr = inp.attribute("value");
    if (value_attr) {
      input_value = value_attr.as_string();
      std::string prop_name = "inputs:" + input_name;

      tinyusdz::mtlx::pugi::xml_attribute type_attr = inp.attribute("type");
      if (type_attr) {
        std::string type_str = type_attr.as_string();
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
        }
      }
    }
  }

  ps.specifier() = Specifier::Def;
  ps.typeName() = kShader;
  ps.props()[kShaderInfoId] = Property(Attribute::Uniform(value::token("MaterialXMix")));

  return true;
}

// Convert MaterialX noise2d/noise3d nodes - simple procedural noise
static bool ConvertNoise(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                         std::string *warn, std::string *err) {
  (void)warn;

  tinyusdz::mtlx::pugi::xml_attribute name_attr = node.attribute("name");
  if (name_attr) {
    ps.name() = name_attr.as_string();
  }

  // Parse noise parameters
  for (auto inp : node.children("input")) {
    std::string input_name;
    std::string input_value;

    tinyusdz::mtlx::pugi::xml_attribute inp_name_attr = inp.attribute("name");
    if (inp_name_attr) {
      input_name = inp_name_attr.as_string();
    }

    tinyusdz::mtlx::pugi::xml_attribute value_attr = inp.attribute("value");
    if (value_attr) {
      input_value = value_attr.as_string();
      std::string prop_name = "inputs:" + input_name;

      // Common noise params: amplitude, pivot, lacunarity, octaves, etc.
      float val;
      if (ParseMaterialXValue(input_value, &val, err)) {
        ps.props()[prop_name] = Property(Attribute::Uniform(val));
      }
    }
  }

  ps.specifier() = Specifier::Def;
  ps.typeName() = kShader;
  ps.props()[kShaderInfoId] = Property(Attribute::Uniform(value::token("MaterialXNoise")));

  return true;
}

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

  ps.specifier() = Specifier::Def;
  ps.typeName() = kShader;
  ps.props()[kShaderInfoId] = Property(Attribute::Uniform(value::token(info_id)));

  return true;
}

// Convert binary operations (divide, power, min, max, modulo, atan2, dotproduct, crossproduct)
static bool ConvertBinaryOp(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                            const std::string &op_name,
                            std::string *warn, std::string *err) {
  // Get output type from node's type attribute
  tinyusdz::mtlx::pugi::xml_attribute type_attr = node.attribute("type");
  std::string type_str = type_attr ? type_attr.as_string() : "float";
  std::string info_id = "ND_" + op_name + "_" + type_str;
  return ConvertGenericNode(node, ps, info_id, warn, err);
}

// Convert unary operations (sqrt, absval, sin, cos, tan, floor, ceil, round, normalize, magnitude, luminance, invert, saturate)
static bool ConvertUnaryOp(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                           const std::string &op_name,
                           std::string *warn, std::string *err) {
  tinyusdz::mtlx::pugi::xml_attribute type_attr = node.attribute("type");
  std::string type_str = type_attr ? type_attr.as_string() : "float";
  std::string info_id = "ND_" + op_name + "_" + type_str;
  return ConvertGenericNode(node, ps, info_id, warn, err);
}

// Convert clamp operation (in, low, high)
static bool ConvertClamp(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                         std::string *warn, std::string *err) {
  tinyusdz::mtlx::pugi::xml_attribute type_attr = node.attribute("type");
  std::string type_str = type_attr ? type_attr.as_string() : "float";
  std::string info_id = "ND_clamp_" + type_str;
  return ConvertGenericNode(node, ps, info_id, warn, err);
}

// Convert remap operation (in, inlow, inhigh, outlow, outhigh)
static bool ConvertRemap(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                         std::string *warn, std::string *err) {
  tinyusdz::mtlx::pugi::xml_attribute type_attr = node.attribute("type");
  std::string type_str = type_attr ? type_attr.as_string() : "float";
  std::string info_id = "ND_remap_" + type_str;
  return ConvertGenericNode(node, ps, info_id, warn, err);
}

// Convert extract operation (extracts single channel from color3/vector3)
static bool ConvertExtract(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                           std::string *warn, std::string *err) {
  // Input type determines the info:id (e.g., ND_extract_color3)
  // Find the 'in' input to determine source type
  std::string source_type = "color3";
  for (auto inp : node.children("input")) {
    tinyusdz::mtlx::pugi::xml_attribute inp_name = inp.attribute("name");
    if (inp_name && std::string(inp_name.as_string()) == "in") {
      tinyusdz::mtlx::pugi::xml_attribute type_attr = inp.attribute("type");
      if (type_attr) {
        source_type = type_attr.as_string();
      }
      break;
    }
  }
  std::string info_id = "ND_extract_" + source_type;
  return ConvertGenericNode(node, ps, info_id, warn, err);
}

// Convert combine operations (combine2, combine3, combine4)
static bool ConvertCombine(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                           const std::string &combine_type,  // "combine2", "combine3", "combine4"
                           std::string *warn, std::string *err) {
  tinyusdz::mtlx::pugi::xml_attribute type_attr = node.attribute("type");
  std::string type_str = type_attr ? type_attr.as_string() : "color3";
  std::string info_id = "ND_" + combine_type + "_" + type_str;
  return ConvertGenericNode(node, ps, info_id, warn, err);
}

// Convert HSV adjust operation
static bool ConvertHsvAdjust(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                             std::string *warn, std::string *err) {
  return ConvertGenericNode(node, ps, "ND_hsvadjust_color3", warn, err);
}

// Convert type conversion operations (e.g., color3 to vector3)
static bool ConvertConvert(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                           std::string *warn, std::string *err) {
  // Determine from/to types
  std::string from_type = "color3";
  std::string to_type = "vector3";

  for (auto inp : node.children("input")) {
    tinyusdz::mtlx::pugi::xml_attribute inp_name = inp.attribute("name");
    if (inp_name && std::string(inp_name.as_string()) == "in") {
      tinyusdz::mtlx::pugi::xml_attribute type_attr = inp.attribute("type");
      if (type_attr) {
        from_type = type_attr.as_string();
      }
      break;
    }
  }

  tinyusdz::mtlx::pugi::xml_attribute type_attr = node.attribute("type");
  if (type_attr) {
    to_type = type_attr.as_string();
  }

  std::string info_id = "ND_convert_" + from_type + "_" + to_type;
  return ConvertGenericNode(node, ps, info_id, warn, err);
}

// Convert geometry nodes (position, normal, tangent, bitangent, texcoord with space param)
static bool ConvertGeometryNode(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                                const std::string &geom_type,
                                std::string *warn, std::string *err) {
  tinyusdz::mtlx::pugi::xml_attribute type_attr = node.attribute("type");
  std::string type_str = type_attr ? type_attr.as_string() : "vector3";
  std::string info_id = "ND_" + geom_type + "_" + type_str;
  return ConvertGenericNode(node, ps, info_id, warn, err);
}

// Convert rotate3d operation
static bool ConvertRotate3d(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                            std::string *warn, std::string *err) {
  return ConvertGenericNode(node, ps, "ND_rotate3d_vector3", warn, err);
}

// Convert swizzle operation
static bool ConvertSwizzle(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                           std::string *warn, std::string *err) {
  tinyusdz::mtlx::pugi::xml_attribute type_attr = node.attribute("type");
  std::string type_str = type_attr ? type_attr.as_string() : "color3";
  std::string info_id = "ND_swizzle_" + type_str;
  return ConvertGenericNode(node, ps, info_id, warn, err);
}

// Convert conditional nodes (ifgreater, ifless, ifequal, etc.)
static bool ConvertConditional(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                               const std::string &cond_type,
                               std::string *warn, std::string *err) {
  tinyusdz::mtlx::pugi::xml_attribute type_attr = node.attribute("type");
  std::string type_str = type_attr ? type_attr.as_string() : "float";
  std::string info_id = "ND_" + cond_type + "_" + type_str;
  return ConvertGenericNode(node, ps, info_id, warn, err);
}

// Convert smoothstep operation
static bool ConvertSmoothstep(const tinyusdz::mtlx::pugi::xml_node &node, PrimSpec &ps,
                              std::string *warn, std::string *err) {
  tinyusdz::mtlx::pugi::xml_attribute type_attr = node.attribute("type");
  std::string type_str = type_attr ? type_attr.as_string() : "float";
  std::string info_id = "ND_smoothstep_" + type_str;
  return ConvertGenericNode(node, ps, info_id, warn, err);
}

// Helper to convert a single MaterialX node to PrimSpec
// Returns true if successful (including skip case), false on error
// Sets is_skip to true if the node should be skipped (input/output/unknown)
static bool ConvertSingleNode(const tinyusdz::mtlx::pugi::xml_node &node,
                              PrimSpec &ps, bool &is_skip,
                              const MtlxConfig &config,
                              std::string *warn, std::string *err) {
  is_skip = false;
  std::string node_name = node.name();

  // Convert MaterialX nodes to USD shader nodes
  if (node_name == "place2d") {
    if (!ConvertPlace2d(node, ps, warn, err)) {
      return false;
    }
  } else if (node_name == "tiledimage") {
    if (!ConvertTiledImage(node, ps, warn, err)) {
      return false;
    }
  } else if (node_name == "image") {
    if (!ConvertImage(node, ps, warn, err)) {
      return false;
    }
  } else if (node_name == "texcoord") {
    if (!ConvertTexCoord(node, ps, config, warn, err)) {
      return false;
    }
  } else if (node_name == "constant") {
    if (!ConvertConstant(node, ps, warn, err)) {
      return false;
    }
  } else if (node_name == "multiply") {
    if (!ConvertMultiply(node, ps, warn, err)) {
      return false;
    }
  } else if (node_name == "add") {
    if (!ConvertAdd(node, ps, warn, err)) {
      return false;
    }
  } else if (node_name == "subtract") {
    // Subtract uses same logic as add
    if (!ConvertAdd(node, ps, warn, err)) {
      return false;
    }
  } else if (node_name == "mix") {
    if (!ConvertMix(node, ps, warn, err)) {
      return false;
    }
  } else if (node_name == "noise2d" || node_name == "noise3d" ||
             node_name == "cellnoise2d" || node_name == "cellnoise3d" ||
             node_name == "worleynoise2d" || node_name == "worleynoise3d" ||
             node_name == "fractal3d") {
    if (!ConvertNoise(node, ps, warn, err)) {
      return false;
    }
  //
  // Binary operations (two inputs: in1, in2)
  //
  } else if (node_name == "divide") {
    if (!ConvertBinaryOp(node, ps, "divide", warn, err)) return false;
  } else if (node_name == "power") {
    if (!ConvertBinaryOp(node, ps, "power", warn, err)) return false;
  } else if (node_name == "min") {
    if (!ConvertBinaryOp(node, ps, "min", warn, err)) return false;
  } else if (node_name == "max") {
    if (!ConvertBinaryOp(node, ps, "max", warn, err)) return false;
  } else if (node_name == "modulo") {
    if (!ConvertBinaryOp(node, ps, "modulo", warn, err)) return false;
  } else if (node_name == "atan2") {
    if (!ConvertBinaryOp(node, ps, "atan2", warn, err)) return false;
  } else if (node_name == "dotproduct") {
    if (!ConvertBinaryOp(node, ps, "dotproduct", warn, err)) return false;
  } else if (node_name == "crossproduct") {
    if (!ConvertBinaryOp(node, ps, "crossproduct", warn, err)) return false;
  //
  // Unary operations (single input: in)
  //
  } else if (node_name == "sqrt") {
    if (!ConvertUnaryOp(node, ps, "sqrt", warn, err)) return false;
  } else if (node_name == "absval") {
    if (!ConvertUnaryOp(node, ps, "absval", warn, err)) return false;
  } else if (node_name == "sign") {
    if (!ConvertUnaryOp(node, ps, "sign", warn, err)) return false;
  } else if (node_name == "floor") {
    if (!ConvertUnaryOp(node, ps, "floor", warn, err)) return false;
  } else if (node_name == "ceil") {
    if (!ConvertUnaryOp(node, ps, "ceil", warn, err)) return false;
  } else if (node_name == "round") {
    if (!ConvertUnaryOp(node, ps, "round", warn, err)) return false;
  } else if (node_name == "sin") {
    if (!ConvertUnaryOp(node, ps, "sin", warn, err)) return false;
  } else if (node_name == "cos") {
    if (!ConvertUnaryOp(node, ps, "cos", warn, err)) return false;
  } else if (node_name == "tan") {
    if (!ConvertUnaryOp(node, ps, "tan", warn, err)) return false;
  } else if (node_name == "asin") {
    if (!ConvertUnaryOp(node, ps, "asin", warn, err)) return false;
  } else if (node_name == "acos") {
    if (!ConvertUnaryOp(node, ps, "acos", warn, err)) return false;
  } else if (node_name == "atan") {
    if (!ConvertUnaryOp(node, ps, "atan", warn, err)) return false;
  } else if (node_name == "exp") {
    if (!ConvertUnaryOp(node, ps, "exp", warn, err)) return false;
  } else if (node_name == "ln") {
    if (!ConvertUnaryOp(node, ps, "ln", warn, err)) return false;
  } else if (node_name == "log2") {
    if (!ConvertUnaryOp(node, ps, "log2", warn, err)) return false;
  } else if (node_name == "normalize") {
    if (!ConvertUnaryOp(node, ps, "normalize", warn, err)) return false;
  } else if (node_name == "magnitude") {
    if (!ConvertUnaryOp(node, ps, "magnitude", warn, err)) return false;
  } else if (node_name == "luminance") {
    if (!ConvertUnaryOp(node, ps, "luminance", warn, err)) return false;
  } else if (node_name == "invert") {
    if (!ConvertUnaryOp(node, ps, "invert", warn, err)) return false;
  } else if (node_name == "saturate") {
    if (!ConvertUnaryOp(node, ps, "saturate", warn, err)) return false;
  } else if (node_name == "hueshift") {
    if (!ConvertUnaryOp(node, ps, "hueshift", warn, err)) return false;
  //
  // Clamp and remap operations
  //
  } else if (node_name == "clamp") {
    if (!ConvertClamp(node, ps, warn, err)) return false;
  } else if (node_name == "remap" || node_name == "range") {
    if (!ConvertRemap(node, ps, warn, err)) return false;
  } else if (node_name == "smoothstep") {
    if (!ConvertSmoothstep(node, ps, warn, err)) return false;
  //
  // Channel operations (extract, combine)
  //
  } else if (node_name == "extract") {
    if (!ConvertExtract(node, ps, warn, err)) return false;
  } else if (node_name == "combine2") {
    if (!ConvertCombine(node, ps, "combine2", warn, err)) return false;
  } else if (node_name == "combine3") {
    if (!ConvertCombine(node, ps, "combine3", warn, err)) return false;
  } else if (node_name == "combine4") {
    if (!ConvertCombine(node, ps, "combine4", warn, err)) return false;
  } else if (node_name == "swizzle") {
    if (!ConvertSwizzle(node, ps, warn, err)) return false;
  //
  // Color/HSV operations
  //
  } else if (node_name == "hsvadjust") {
    if (!ConvertHsvAdjust(node, ps, warn, err)) return false;
  } else if (node_name == "rgbtohsv") {
    if (!ConvertUnaryOp(node, ps, "rgbtohsv", warn, err)) return false;
  } else if (node_name == "hsvtorgb") {
    if (!ConvertUnaryOp(node, ps, "hsvtorgb", warn, err)) return false;
  //
  // Type conversion
  //
  } else if (node_name == "convert") {
    if (!ConvertConvert(node, ps, warn, err)) return false;
  //
  // Geometry nodes
  //
  } else if (node_name == "position") {
    if (!ConvertGeometryNode(node, ps, "position", warn, err)) return false;
  } else if (node_name == "normal") {
    if (!ConvertGeometryNode(node, ps, "normal", warn, err)) return false;
  } else if (node_name == "tangent") {
    if (!ConvertGeometryNode(node, ps, "tangent", warn, err)) return false;
  } else if (node_name == "bitangent") {
    if (!ConvertGeometryNode(node, ps, "bitangent", warn, err)) return false;
  } else if (node_name == "geomcolor") {
    if (!ConvertGeometryNode(node, ps, "geomcolor", warn, err)) return false;
  } else if (node_name == "geompropvalue") {
    if (!ConvertGeometryNode(node, ps, "geompropvalue", warn, err)) return false;
  //
  // Rotation
  //
  } else if (node_name == "rotate3d") {
    if (!ConvertRotate3d(node, ps, warn, err)) return false;
  //
  // Conditional operations
  //
  } else if (node_name == "ifgreater") {
    if (!ConvertConditional(node, ps, "ifgreater", warn, err)) return false;
  } else if (node_name == "ifgreatereq") {
    if (!ConvertConditional(node, ps, "ifgreatereq", warn, err)) return false;
  } else if (node_name == "ifless") {
    if (!ConvertConditional(node, ps, "ifless", warn, err)) return false;
  } else if (node_name == "iflesseq") {
    if (!ConvertConditional(node, ps, "iflesseq", warn, err)) return false;
  } else if (node_name == "ifequal") {
    if (!ConvertConditional(node, ps, "ifequal", warn, err)) return false;
  } else if (node_name == "switch") {
    if (!ConvertConditional(node, ps, "switch", warn, err)) return false;
  //
  // Skip input/output nodes - they are handled separately
  //
  } else if (node_name == "input" || node_name == "output") {
    is_skip = true;
    return true;
  } else {
    // Unknown node - try generic conversion instead of skipping
    tinyusdz::mtlx::pugi::xml_attribute type_attr = node.attribute("type");
    std::string type_str = type_attr ? type_attr.as_string() : "float";
    std::string info_id = "ND_" + node_name + "_" + type_str;
    PUSH_WARN(fmt::format("Unknown node type '{}', using generic conversion with info:id='{}'.\n", node_name, info_id));
    if (!ConvertGenericNode(node, ps, info_id, warn, err)) return false;
  }

  return true;
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

#if 0  // TODO
static bool ConvertPlace2d(const tinyusdz::mtlx::pugi::xml_node &node, UsdTransform2d &tx, std::string *warn, std::string *err) {
  // texcoord(vector2). default index=0 uv coordinate
  // pivot(vector2). default (0, 0)
  // scale(vector2). default (1, 1)
  // rotate(float). in degrees, Conter-clockwise
  // offset(vector2)
  if (tinyusdz::mtlx::pugi::xml_attribute texcoord_attr = node.attribute("texcoord")) {
    PUSH_WARN("TODO: `texcoord` attribute.\n");
  }

  if (tinyusdz::mtlx::pugi::xml_attribute pivot_attr = node.attribute("pivot")) {
    PUSH_WARN("TODO: `pivot` attribute.\n");
  }

  if (tinyusdz::mtlx::pugi::xml_attribute scale_attr = node.attribute("scale")) {
    value::float2 value;
    if (!ParseMaterialXValue(scale_attr.as_string(), &value, err)) {
      PUSH_ERROR_AND_RETURN("Failed to parse `rotate` attribute of `place2d`.\n");
    }
    tx.scale = value;
  }

  if (tinyusdz::mtlx::pugi::xml_attribute rotate_attr = node.attribute("rotate")) {
    float value;
    if (!ParseMaterialXValue(rotate_attr.as_string(), &value, err)) {
      PUSH_ERROR_AND_RETURN("Failed to parse `rotate` attribute of `place2d`.\n");
    }
    tx.rotation = value;
  }

  tinyusdz::mtlx::pugi::xml_attribute offset_attr = node.attribute("offset");
  if (offset_attr) {
    PUSH_WARN("TODO: `offset` attribute.\n");
  }

  return true;
}

static bool ConvertTiledImage(const tinyusdz::mtlx::pugi::xml_node &node, UsdUVTexture &tex, std::string *err) {
  (void)tex;
  // file: uniform filename
  // default: float or colorN or vectorN
  // texcoord: vector2
  // uvtiling: vector2(default 1.0, 1.0)
  // uvoffset: vector2(default 0.0, 0.0)
  // realworldimagesize: vector2
  // realworldtilesize: vector2
  // filtertype: string: "closest", "linear" or "cubic"
  if (tinyusdz::mtlx::pugi::xml_attribute file_attr = node.attribute("file")) {
    std::string filename;
    if (!ParseMaterialXValue(file_attr.as_string(), &filename, err)) {
      PUSH_ERROR_AND_RETURN("Failed to parse `file` attribute in `tiledimage`.\n");
    }
  } else {
    PUSH_ERROR_AND_RETURN("`file` attribute not found.");
  }

  // TODO...

  return true;

}
#endif

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
  // - [x] version(string, required)
  //   - [x] validate version string
  // - [x] cms(string, optional)
  // - [x] cmsconfig(filename, optional)
  // - [x] colorspace(string, optional)
  // - [x] namespace(string, optional)

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

  tinyusdz::mtlx::pugi::xml_attribute cmsconfig_attr = root.attribute("cms");
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
      GET_SHADER_PARAM(name, typeName, "coat_affect_color", "color3", value::color3f, valueStr, surface.coat_affect_color)
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
    // TODO
    (void)look;
  }

#undef GET_ATTR_VALUE

  return true;
}

bool ReadMaterialXFromFile(const AssetResolutionResolver &resolver,
                           const std::string &asset_path, MtlxModel *mtlx,
                           std::string *warn, std::string *err,
                           const MtlxConfig &config) {
  std::string filepath = resolver.resolve(asset_path);
  if (filepath.empty()) {
    PUSH_ERROR_AND_RETURN("Asset not found: " + asset_path);
  }

  // up to 16MB xml
  size_t max_bytes = 1024 * 1024 * 16;

  std::vector<uint8_t> data;
  if (!io::ReadWholeFile(&data, err, filepath, max_bytes,
                         /* userdata */ nullptr)) {
    PUSH_ERROR_AND_RETURN("Read file failed.");
  }

  std::string str(reinterpret_cast<const char *>(&data[0]), data.size());
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
  PUSH_ERROR_AND_RETURN("Unknown/unsupported shader type: " << mtlx.shader_name
    << " (Note: UsdPreviewSurface and StandardSurface export are currently disabled)");

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

  // Add shader nodes (e.g., UsdPreviewSurface, OpenPBRSurface)
  // TODO: Convert shader value to PrimSpec
  // For now, we skip this as shaders are typically referenced in materials
  (void)model.shaders;  // Avoid unused variable warning

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

  if (asset.size() < 32) {
    if (err) {
      (*err) += "MateiralX: Asset size too small.\n";
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

    // Add ShapingAPI for cone control
    ShapingAPI shaping;

    float inner_angle = 60.0f;
    conical_edf->inner_angle.get_value().get_scalar(&inner_angle);
    shaping.shapingConeAngle.set_value(inner_angle);

    if (conical_edf->outer_angle.authored()) {
      float outer_angle = 60.0f;
      if (auto oa_val = conical_edf->outer_angle.get_value()) {
        oa_val.value().get_scalar(&outer_angle);
        // Use softness to represent the difference between inner and outer angles
        float softness = (outer_angle - inner_angle) / inner_angle;
        shaping.shapingConeSoftness.set_value(std::max(0.0f, softness));
      }
    }

    light.shaping = shaping;

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

    // Add ShapingAPI with IES profile
    if (measured_edf->file.authored()) {
      ShapingAPI shaping;

      if (auto file_val = measured_edf->file.get_value()) {
        value::AssetPath ies_file;
        if (file_val.value().get_scalar(&ies_file)) {
          shaping.shapingIesFile.set_value(ies_file);
          shaping.shapingIesNormalize.set_value(true);
        }
      }

      light.shaping = shaping;
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

#if 0
bool ToPrimSpec(const MtlxModel &model, PrimSpec &ps, std::string *err)
  (void)model;
  (void)ps;

  if (err) {
    (*err) += "MaterialX support is disabled in this build.\n";
  }
  return false;

}
#endif

}  // namespace tinyusdz

#endif  // TINYUSDZ_USE_USDMTLX
