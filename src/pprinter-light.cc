// Light pretty-printer implementations split from pprinter.cc
// SPDX-License-Identifier: Apache 2.0
#include "value-pprint.hh" // for quote, dtos
#include "pprinter-util.hh"
#include "pprinter.hh"
#include "prim-pprint.hh"
#include <sstream>

namespace tinyusdz {

std::string to_string(const SphereLight &light, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;
  ss << pprint::Indent(indent) << to_string(light.spec) << " SphereLight \""
     << light.name << "\"\n";
  if (light.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(light.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";
  ss << print_typed_attr(light.color, "inputs:color", indent + 1);
  ss << print_typed_attr(light.colorTemperature, "inputs:colorTemperature", indent + 1);
  ss << print_typed_attr(light.diffuse, "inputs:diffuse", indent + 1);
  ss << print_typed_attr(light.enableColorTemperature, "inputs:enableColorTemperature", indent + 1);
  ss << print_typed_attr(light.exposure, "inputs:exposure", indent + 1);
  ss << print_typed_attr(light.intensity, "inputs:intensity", indent + 1);
  ss << print_typed_attr(light.normalize, "inputs:normalize", indent + 1);
  ss << print_typed_attr(light.specular, "inputs:specular", indent + 1);
  ss << print_typed_attr(light.radius, "inputs:radius", indent + 1);
  ss << print_typed_attr(light.extent, "extent", indent + 1);
  ss << print_typed_token_attr(light.visibility, "visibility", indent + 1);
  ss << print_typed_token_attr(light.purpose, "purpose", indent + 1);
  ss << print_xformOps(light.xformOps, indent + 1);
  ss << print_props(light.props, indent + 1);
  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }
  return ss.str();
}

std::string to_string(const DistantLight &light, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;
  ss << pprint::Indent(indent) << to_string(light.spec) << " DistantLight \""
     << light.name << "\"\n";
  if (light.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(light.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";
  ss << print_typed_attr(light.color, "inputs:color", indent + 1);
  ss << print_typed_attr(light.colorTemperature, "inputs:colorTemperature", indent + 1);
  ss << print_typed_attr(light.diffuse, "inputs:diffuse", indent + 1);
  ss << print_typed_attr(light.enableColorTemperature, "inputs:enableColorTemperature", indent + 1);
  ss << print_typed_attr(light.exposure, "inputs:exposure", indent + 1);
  ss << print_typed_attr(light.intensity, "inputs:intensity", indent + 1);
  ss << print_typed_attr(light.normalize, "inputs:normalize", indent + 1);
  ss << print_typed_attr(light.specular, "inputs:specular", indent + 1);
  ss << print_typed_attr(light.angle, "inputs:angle", indent + 1);
  ss << print_typed_token_attr(light.visibility, "visibility", indent + 1);
  ss << print_typed_token_attr(light.purpose, "purpose", indent + 1);
  ss << print_xformOps(light.xformOps, indent + 1);
  ss << print_props(light.props, indent + 1);
  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }
  return ss.str();
}

std::string to_string(const CylinderLight &light, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;
  ss << pprint::Indent(indent) << to_string(light.spec) << " CylinderLight \""
     << light.name << "\"\n";
  if (light.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(light.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";
  ss << print_typed_attr(light.color, "inputs:color", indent + 1);
  ss << print_typed_attr(light.colorTemperature, "inputs:colorTemperature", indent + 1);
  ss << print_typed_attr(light.diffuse, "inputs:diffuse", indent + 1);
  ss << print_typed_attr(light.enableColorTemperature, "inputs:enableColorTemperature", indent + 1);
  ss << print_typed_attr(light.exposure, "inputs:exposure", indent + 1);
  ss << print_typed_attr(light.intensity, "inputs:intensity", indent + 1);
  ss << print_typed_attr(light.normalize, "inputs:normalize", indent + 1);
  ss << print_typed_attr(light.specular, "inputs:specular", indent + 1);
  ss << print_typed_attr(light.length, "inputs:length", indent + 1);
  ss << print_typed_attr(light.radius, "inputs:radius", indent + 1);
  ss << print_typed_attr(light.extent, "extent", indent + 1);
  ss << print_typed_token_attr(light.visibility, "visibility", indent + 1);
  ss << print_typed_token_attr(light.purpose, "purpose", indent + 1);
  ss << print_xformOps(light.xformOps, indent + 1);
  ss << print_props(light.props, indent + 1);
  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }
  return ss.str();
}

std::string to_string(const DiskLight &light, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;
  ss << pprint::Indent(indent) << to_string(light.spec) << " DiskLight \""
     << light.name << "\"\n";
  if (light.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(light.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";
  ss << print_typed_attr(light.color, "inputs:color", indent + 1);
  ss << print_typed_attr(light.colorTemperature, "inputs:colorTemperature", indent + 1);
  ss << print_typed_attr(light.diffuse, "inputs:diffuse", indent + 1);
  ss << print_typed_attr(light.enableColorTemperature, "inputs:enableColorTemperature", indent + 1);
  ss << print_typed_attr(light.exposure, "inputs:exposure", indent + 1);
  ss << print_typed_attr(light.intensity, "inputs:intensity", indent + 1);
  ss << print_typed_attr(light.normalize, "inputs:normalize", indent + 1);
  ss << print_typed_attr(light.specular, "inputs:specular", indent + 1);
  ss << print_typed_attr(light.radius, "inputs:radius", indent + 1);
  ss << print_typed_attr(light.extent, "extent", indent + 1);
  ss << print_typed_token_attr(light.visibility, "visibility", indent + 1);
  ss << print_typed_token_attr(light.purpose, "purpose", indent + 1);
  ss << print_xformOps(light.xformOps, indent + 1);
  ss << print_props(light.props, indent + 1);
  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }
  return ss.str();
}

std::string to_string(const DomeLight &light, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;
  ss << pprint::Indent(indent) << to_string(light.spec) << " DomeLight \""
     << light.name << "\"\n";
  if (light.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(light.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";
  ss << print_typed_attr(light.color, "inputs:color", indent + 1);
  ss << print_typed_attr(light.colorTemperature, "inputs:colorTemperature", indent + 1);
  ss << print_typed_attr(light.diffuse, "inputs:diffuse", indent + 1);
  ss << print_typed_attr(light.enableColorTemperature, "inputs:enableColorTemperature", indent + 1);
  ss << print_typed_attr(light.exposure, "inputs:exposure", indent + 1);
  ss << print_typed_attr(light.intensity, "inputs:intensity", indent + 1);
  ss << print_typed_attr(light.normalize, "inputs:normalize", indent + 1);
  ss << print_typed_attr(light.specular, "inputs:specular", indent + 1);
  ss << print_typed_attr(light.guideRadius, "inputs:guideRadius", indent + 1);
  ss << print_typed_attr(light.file, "inputs:file", indent + 1);
  ss << print_typed_token_attr(light.textureFormat, "inputs:textureFormat", indent + 1);
  ss << print_typed_token_attr(light.visibility, "visibility", indent + 1);
  ss << print_typed_token_attr(light.purpose, "purpose", indent + 1);
  ss << print_xformOps(light.xformOps, indent + 1);
  ss << print_props(light.props, indent + 1);
  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }
  return ss.str();
}

std::string to_string(const RectLight &light, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;
  ss << pprint::Indent(indent) << to_string(light.spec) << " RectLight \""
     << light.name << "\"\n";
  if (light.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(light.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";
  ss << print_typed_attr(light.color, "inputs:color", indent + 1);
  ss << print_typed_attr(light.colorTemperature, "inputs:colorTemperature", indent + 1);
  ss << print_typed_attr(light.diffuse, "inputs:diffuse", indent + 1);
  ss << print_typed_attr(light.enableColorTemperature, "inputs:enableColorTemperature", indent + 1);
  ss << print_typed_attr(light.exposure, "inputs:exposure", indent + 1);
  ss << print_typed_attr(light.intensity, "inputs:intensity", indent + 1);
  ss << print_typed_attr(light.normalize, "inputs:normalize", indent + 1);
  ss << print_typed_attr(light.specular, "inputs:specular", indent + 1);
  ss << print_typed_attr(light.file, "inputs:file", indent + 1);
  ss << print_typed_attr(light.height, "inputs:height", indent + 1);
  ss << print_typed_attr(light.width, "inputs:width", indent + 1);
  ss << print_typed_attr(light.height, "inputs:height", indent + 1);
  ss << print_typed_attr(light.extent, "extent", indent + 1);
  ss << print_typed_token_attr(light.visibility, "visibility", indent + 1);
  ss << print_typed_token_attr(light.purpose, "purpose", indent + 1);
  ss << print_xformOps(light.xformOps, indent + 1);
  ss << print_props(light.props, indent + 1);
  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }
  return ss.str();
}

} // namespace tinyusdz
