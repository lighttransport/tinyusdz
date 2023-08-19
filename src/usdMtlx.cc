
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
#include "ascii-parser.hh" // To parse color3f value
#include "common-macros.inc"
#include "usdMtlx.hh"

#define PushError(msg) do { \
  if (err) { \
    (*err) += msg; \
  } \
} while(0);

namespace tinyusdz {

namespace detail {

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
