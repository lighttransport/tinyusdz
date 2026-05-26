// SPDX-License-Identifier: Apache 2.0
// Copyright 2023 - Present, Light Transport Entertainment, Inc.

#include <sstream>

#include "usdMtlx.hh"
#include "usdShade.hh"
#include "usdLux.hh"  // SphereLight/RectLight (no longer re-exported by tinyusdz.hh)
#include "safe-arithmetic.hh"

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
#include "security-policy.hh"
#include "str-util.hh"  // For dragonbox-based dtos()
#include "tiny-format.hh"
#include "value-pprint.hh"

// Use dragonbox-based dtos from str-util.hh for shortest representation
// No need for local dtos() or float_to_xml_string() - dtos() already
// produces the shortest round-trip-correct representation without trailing zeros

#define PushError(msg) \
  do {                 \
    if (err) {         \
      (*err) += msg;   \
    }                  \
  } while (0);

// MaterialX WRITE path (USD -> .mtlx XML), split from usdMtlx.cc to divide back-end
// codegen. Read and write paths share no statics. See usdMtlx.cc for the read path.

namespace tinyusdz {
namespace detail {

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

}  // namespace detail

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

}  // namespace tinyusdz

#endif  // TINYUSDZ_USE_USDMTLX
