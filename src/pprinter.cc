
//#include <ctime>

//
#include "pprinter.hh"
#include "prim-types.hh"
#include "value-pprint.hh"
#include "str-util.hh"

namespace tinyusdz {
namespace {

// Path quote
std::string pquote(const Path &p) {
  return wquote(p.full_path_name(), "<", ">");
}

} // namespace
} // namespace tinyusdz

namespace std {


std::ostream &operator<<(std::ostream &ofs, tinyusdz::Visibility v) {
  ofs << to_string(v);
  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, tinyusdz::Extent v) {
  ofs << to_string(v);

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::Path &v) {
  ofs << tinyusdz::pquote(v);

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::StringData &v) {

  std::string delim = v.single_quote ? "'" : "\"";

  if (v.is_triple_quoted) {
    delim = v.single_quote ? "'''" : "\"\"\"";
  }

  ofs << delim;
  ofs << v.value;
  ofs << delim;

  return ofs;
}


} // namespace std


namespace tinyusdz {

namespace pprint {

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#pragma clang diagnostic ignored "-Wglobal-constructors"
#endif

static std::string sIndentString = "    ";

#ifdef __clang__
#pragma clang diagnostic pop
#endif

std::string Indent(uint32_t n) {
  std::stringstream ss;

  for (uint32_t i = 0; i < n; i++) {
    ss << sIndentString;
  }

  return ss.str();
}

void SetIndentString(const std::string &s) {
  sIndentString = s;
}

} // namespace pprint

namespace {


#if 0
// TODO: Triple @
std::string aquote(const value::AssetPath &p) {
  return wquote(p.GetAssetPath(), "@", "@");
}
#endif


std::string to_string(const double &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}

#if 0
template<typename T>
std::string to_string(const std::vector<T> &v) {
  std::stringstream ss;

  ss << "[";
  for (size_t i = 0; i < v.size(); i++) {
    ss << v[i];
    if (i != (v.size() - 1)) {
      ss << ", ";
    }
  }
  ss << "]";

  return ss.str();
}
#endif


#if 0
template<typename T>
std::string prefix(const Animatable<T> &v) {
  if (v.IsTimeSamples()) {
    return ".timeSamples";
  }
  return "";
}
#endif

template<typename T>
std::string print_typed_timesamples(const TypedTimeSamples<T> &v, const uint32_t indent = 0) {
  std::stringstream ss;

  ss << "{\n";

  const auto &samples = v.GetSamples();

  for (size_t i = 0; i < samples.size(); i++) {
    ss << pprint::Indent(indent+1) << samples[i].t << ": ";
    if (samples[i].blocked) {
      ss << "None";
    } else {
      ss << samples[i].value;
    }
    ss << ",\n";
  }

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

template<typename T>
std::string print_typed_token_timesamples(const TypedTimeSamples<T> &v, const uint32_t indent = 0) {
  std::stringstream ss;

  ss << "{\n";

  const auto &samples = v.GetSamples();

  for (size_t i = 0; i < samples.size(); i++) {
    ss << pprint::Indent(indent+1) << samples[i].t << ": ";
    if (samples[i].blocked) {
      ss << "None";
    } else {
      ss << quote(to_string(samples[i].value));
    }
    ss << ",\n";
  }

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

template<typename T>
std::string print_animatable(const Animatable<T> &v, const uint32_t indent = 0) {
  std::stringstream ss;

  if (v.IsTimeSamples()) {
    ss << print_typed_timesamples(v.ts, indent);
  } else if (v.IsBlocked()) {
    ss << "None";
  } else if (v.IsScalar()) {
    ss << v.value;
  } else {
    return "[FIXME: Invalid Animatable]";
  }

  return ss.str();
}

template<typename T>
std::string print_animatable_token(const Animatable<T> &v, const uint32_t indent = 0) {
  std::stringstream ss;

  if (v.IsTimeSamples()) {
    ss << print_typed_token_timesamples(v.ts, indent);
  } else if (v.IsBlocked()) {
    ss << "None";
  } else if (v.IsScalar()) {
    ss << quote(to_string(v.value));
  } else {
    return "[FIXME: Invalid Animatable]";
  }

  return ss.str();
}


std::string print_prim_metas(const PrimMeta &meta, const uint32_t indent) {

  std::stringstream ss;

  if (meta.active) {
    ss << pprint::Indent(indent) << "active = " << to_string(meta.active.value()) << "\n";
  }

  if (meta.kind) {
    ss << pprint::Indent(indent) << "kind = " << quote(to_string(meta.kind.value())) << "\n";
  }

  if (meta.assetInfo) {
    ss << print_customData(meta.assetInfo.value(), "assetInfo", indent);
  }

  if (meta.apiSchemas) {
    auto schemas = meta.apiSchemas.value();

    if (schemas.names.size()) {

      ss << pprint::Indent(indent) << to_string(schemas.listOpQual) << " apiSchemas = [";

      for (size_t i = 0; i < schemas.names.size(); i++) {
        if (i != 0) {
          ss << ", ";
        }

        auto name = std::get<0>(schemas.names[i]);
        ss << "\"" << to_string(name);

        auto instanceName = std::get<1>(schemas.names[i]);

        if (!instanceName.empty()) {
          ss << ":" << instanceName;
        }

        ss << "\"";
      }
      ss << "]\n";
    }
  }

  if (meta.doc) {
    ss << pprint::Indent(indent) << "doc = " << to_string(meta.doc.value()) << "\n";
  }

  if (meta.comment) {
    ss << pprint::Indent(indent) << "comment = " << to_string(meta.comment.value()) << "\n";
  }

  if (meta.customData) {
    ss << print_customData(meta.customData.value(), "customData", indent+1);
  }


  for (const auto &item : meta.meta) {
    ss << print_meta(item.second, indent+1);
  }

  for (const auto &item : meta.stringData) {
    ss << pprint::Indent(indent) << to_string(item) << "\n";
  }

  return ss.str();
}

std::string print_attr_metas(const AttrMeta &meta, const uint32_t indent) {

  std::stringstream ss;

  if (meta.interpolation) {
    ss << pprint::Indent(indent) << "interpolation = " << quote(to_string(meta.interpolation.value())) << "\n";
  }

  if (meta.elementSize) {
    ss << pprint::Indent(indent) << "elementSize = " << to_string(meta.elementSize.value()) << "\n";
  }

  if (meta.comment) {
    ss << pprint::Indent(indent) << "comment = " << to_string(meta.comment.value()) << "\n";
  }

  if (meta.customData) {
    ss << print_customData(meta.customData.value(), "customData", indent);
  }

  // other user defined metadataum.
  for (const auto &item : meta.meta) {
    ss << print_meta(item.second, indent+1);
  }

  for (const auto &item : meta.stringData) {
    ss << pprint::Indent(indent) << to_string(item) << "\n";
  }

  return ss.str();
}

template<typename T>
std::string print_typed_attr(const TypedAttribute<Animatable<T>> &attr, const std::string &name, const uint32_t indent) {

  std::stringstream ss;

  if (attr.authored()) {

    ss << pprint::Indent(indent);

    ss << value::TypeTrait<T>::type_name() << " " << name;

    if (attr.IsBlocked()) {
      ss << " = None";
    } else if (attr.IsConnection()) {
      ss << ".connect = ";
      const std::vector<Path> &paths = attr.GetConnections();
      if (paths.size() == 1) {
        ss << paths[0];
      } else if (paths.size() == 0) {
        ss << "[InternalError]";
      } else {
        ss << paths;
      }

    } else {
      auto pv = attr.GetValue();

      if (pv) {
        if (pv.value().IsTimeSamples()) {
          ss << ".timeSamples = " << print_typed_timesamples(pv.value().ts, indent+1);
        } else {
          ss << " = " << pv.value().value;
        }
      }
    }

    if (attr.meta.authored()) {
      ss << " (\n" << print_attr_metas(attr.meta, indent + 1) << pprint::Indent(indent) << ")";
    }
    ss << "\n";
  }

  return ss.str();
}

#if 0
template<typename T>
std::string print_typed_token_attr(const TypedAttribute<Animatable<T>> &attr, const std::string &name, const uint32_t indent) {

  std::stringstream ss;

  if (attr.value) {

    ss << pprint::Indent(indent);

    ss << "token " << name;

    if (attr.IsBlocked()) {
      ss << " = None";
    } else if (!attr.define_only) {
      ss << " = ";
      if (attr.value.value().IsTimeSamples()) {
        ss << print_token_timesamples(attr.value.value().ts, indent+1);
      } else {
        ss << quote(to_string(attr.value.value().value));
      }
    }

    if (attr.meta.authored()) {
      ss << " (\n" << print_attr_metas(attr.meta, indent + 1) << pprint::Indent(indent) << ")";
    }
    ss << "\n";
  }

  return ss.str();
}
#endif

template<typename T>
std::string print_typed_attr(const TypedAttribute<T> &attr, const std::string &name, const uint32_t indent) {

  std::stringstream ss;

  if (attr.authored()) {


    ss << pprint::Indent(indent);

    ss << "uniform ";

    ss << value::TypeTrait<T>::type_name() << " " << name;


    if (attr.IsBlocked()) {
      ss << " = None";
    } else if (attr.IsConnection()) {

      ss << ".connect = ";
      const std::vector<Path> &paths = attr.GetConnections();
      if (paths.size() == 1) {
        ss << paths[0];
      } else if (paths.size() == 0) {
        ss << "[InternalError]";
      } else {
        ss << paths;
      }
    } else if (attr.IsValueEmpty()) {
      // nothing to do 

    } else {
      auto pv = attr.GetValue();
      if (pv) {
        ss << " = " << pv.value();
      }
    }

    if (attr.meta.authored()) {
      ss << " (\n" << print_attr_metas(attr.meta, indent + 1) << pprint::Indent(indent) << ")";
    }
    ss << "\n";
  }

  return ss.str();
}

#if 0
template<typename T>
std::string print_typed_token_attr(const TypedAttribute<T> &attr, const std::string &name, const uint32_t indent) {

  std::stringstream ss;

  if (attr.authored()) {

    auto pv = attr.get();

    ss << pprint::Indent(indent);

    ss << "uniform token " << name;


    if (attr.IsBlocked()) {
      ss << " = None";
    } else {
      if (pv) {
        ss << " = " << to_string(pv.value());
      }
    }

    if (attr.meta.authored()) {
      ss << " (\n" << print_attr_metas(attr.meta, indent + 1) << pprint::Indent(indent) << ")";
    }
    ss << "\n";
  }

  return ss.str();
}
#endif

template<typename T>
std::string print_typed_attr(const TypedAttributeWithFallback<Animatable<T>> &attr, const std::string &name, const uint32_t indent) {

  std::stringstream ss;

  if (attr.authored()) {

    ss << pprint::Indent(indent);

    ss << value::TypeTrait<T>::type_name() << " " << name;

    if (attr.IsConnection()) {
      ss << ".connect = ";

      const std::vector<Path> &paths = attr.GetConnections();
      if (paths.size() == 1) {
        ss << paths[0];
      } else if (paths.size() == 0) {
        ss << "[InternalError]";
      } else {
        ss << paths;
      }

    } else if (attr.IsValueEmpty()) {
      // nothing to do 
    } else {
      auto v = attr.GetValue();

      if (v.IsTimeSamples()) {
        ss << ".timeSamples";
      }

      ss << " = " <<  print_animatable(v, indent+1);
    }

    if (attr.meta.authored()) {
      ss << " (\n" << print_attr_metas(attr.meta, indent + 1) << pprint::Indent(indent) << ")";
    }
    ss << "\n";
  }

  return ss.str();
}

template<typename T>
std::string print_typed_terminal_attr(const TypedTerminalAttribute<T> &attr, const std::string &name, const uint32_t indent) {

  std::stringstream ss;

  if (attr.authored()) {

    ss << pprint::Indent(indent);

    ss << value::TypeTrait<T>::type_name() << " " << name;

    if (attr.meta.authored()) {
      ss << " (\n" << print_attr_metas(attr.meta, indent + 1) << pprint::Indent(indent) << ")";
    }
    ss << "\n";
  }

  return ss.str();
}

template<typename T>
std::string print_typed_attr(const TypedAttributeWithFallback<T> &attr, const std::string &name, const uint32_t indent) {

  std::stringstream ss;

  if (attr.authored()) {

    ss << pprint::Indent(indent);

    ss << "uniform ";

    ss << value::TypeTrait<T>::type_name() << " " << name;

    if (attr.IsBlocked()) {
      ss << " = None";
    } else if (attr.IsConnection()) {
      ss << ".connect = ";

      const std::vector<Path> &paths = attr.GetConnections();
      if (paths.size() == 1) {
        ss << paths[0];
      } else if (paths.size() == 0) {
        ss << "[InternalError]";
      } else {
        ss << paths;
      }
    } else {
      ss << " = " << attr.GetValue();
    }

    if (attr.meta.authored()) {
      ss << " (\n" << print_attr_metas(attr.meta, indent + 1) << pprint::Indent(indent) << ")";
    }
    ss << "\n";
  }

  return ss.str();
}

template<typename T>
std::string print_typed_token_attr(const TypedAttributeWithFallback<Animatable<T>> &attr, const std::string &name, const uint32_t indent) {

  std::stringstream ss;

  if (attr.authored()) {

    if (attr.IsConnection()) {

      ss << pprint::Indent(indent);

      ss << "token " << name;

      ss << ".connect = ";
      const std::vector<Path> &paths = attr.GetConnections();
      if (paths.size() == 1) {
        ss << paths[0];
      } else if (paths.size() == 0) {
        ss << "[InternalError]";
      } else {
        ss << paths;
      }

    } else {
      auto v = attr.GetValue();

      ss << pprint::Indent(indent);

      ss << "token " << name;

      if (v.IsTimeSamples()) {
        ss << ".timeSamples";
      }

      ss << " = " <<  print_animatable_token(v, indent+1);
    }

    if (attr.meta.authored()) {
      ss << " (\n" << print_attr_metas(attr.meta, indent + 1) << pprint::Indent(indent) << ")";
    }
    ss << "\n";
  }

  return ss.str();
}

template<typename T>
std::string print_typed_token_attr(const TypedAttributeWithFallback<T> &attr, const std::string &name, const uint32_t indent) {

  std::stringstream ss;

  if (attr.authored()) {

    if (attr.IsConnection()) {
      ss << pprint::Indent(indent);

      ss << "token " << name;

      ss << ".connect = ";
      const std::vector<Path> &paths = attr.GetConnections();
      if (paths.size() == 1) {
        ss << paths[0];
      } else if (paths.size() == 0) {
        ss << "[InternalError]";
      } else {
        ss << paths;
      }
    } else {

      ss << pprint::Indent(indent);

      ss << "uniform token " << name;

      if (attr.IsBlocked()) {
        ss << " = None";
      } else {
        ss << " = " << quote(to_string(attr.GetValue()));
      }
    }

    if (attr.meta.authored()) {
      ss << " (\n" << print_attr_metas(attr.meta, indent + 1) << pprint::Indent(indent) << ")";
    }
    ss << "\n";
  }

  return ss.str();
}

std::string print_timesamples(const value::TimeSamples &v, const uint32_t indent) {
  std::stringstream ss;

  if (v.IsScalar()) {
    ss << value::pprint_value(v.values[0]);
  } else {

    if (!v.ValidTimeSamples()) {
      return "[Invalid TimeSamples data(internal error?)]";
    }

    ss << "{\n";

    for (size_t i = 0; i < v.times.size(); i++) {

      ss << pprint::Indent(indent+1);
      ss << v.times[i] << ": " << value::pprint_value(v.values[i]);
      ss << ",\n"; // USDA allow ',' for the last item
    }
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string print_rel_prop(const Property &prop, const std::string &name, uint32_t indent)
{
  std::stringstream ss;
  
  if (!prop.IsRel()) {
    return ss.str();
  }

  ss << pprint::Indent(indent);

  if (prop.HasCustom()) {
    ss << "custom ";
  }

  // List editing
  if (prop.listOpQual != ListEditQual::ResetToExplicit) {
    ss << to_string(prop.listOpQual) << " ";
  }
  
  ss << "rel " << name;

  const Relation &rel = prop.rel;


  if (rel.IsEmpty()) {
    // nothing todo
  } else if (rel.IsPath()) {
    ss << " = " << rel.targetPath;
  } else if (rel.IsPathVector()) {
    ss << " = " << rel.targetPathVector;
  } else if (rel.IsString()) {
    ss << " = " << quote(rel.targetString);
  } else {
    ss << "[InternalErrror]";
  }

  // Metadata is stored in attrib.meta.
  if (rel.meta.authored()) {
    ss << " (\n" << print_attr_metas(rel.meta, indent+1) << pprint::Indent(indent) << ")";
  }

  ss << "\n";

  return ss.str();
}


// Print user-defined (custom) properties.
std::string print_props(const std::map<std::string, Property> &props, uint32_t indent)
{
  std::stringstream ss;

  for (const auto &item : props) {

    const Property &prop = item.second;

    if (prop.IsRel()) {

      ss << print_rel_prop(prop, item.first, indent);

    } else {
      const PrimAttrib &attr = item.second.attrib;

      ss << pprint::Indent(indent);


      if (prop.HasCustom()) {
        ss << "custom ";
      }

      if (attr.variability == Variability::Uniform) {
        ss << "uniform ";
      }

      std::string ty;

      ty = attr.type_name();
      ss << ty << " " << item.first;

      if (prop.IsConnection()) {

        // Currently, ".connect" prefix included in property's name

        ss << " = ";
        if (prop.rel.IsPath()) {
          ss << prop.rel.targetPath;
        } else if (prop.rel.IsPathVector()) {
          ss << prop.rel.targetPathVector;
        }
      } else if (prop.IsEmpty()) {
        ss << "\n";
      } else {
        // has value content
        ss << " = ";

        if (attr.get_var().is_timesample()) {
          ss << print_timesamples(attr.get_var().var, indent+1);
        } else {
          // is_scalar
          ss << value::pprint_value(attr.get_var().var.values[0]);
        }
      }

      if (item.second.attrib.meta.authored()) {
        ss << " (\n" << print_attr_metas(item.second.attrib.meta, indent+1) << pprint::Indent(indent) << ")";
      }
      ss << "\n";
    }
  }

  return ss.str();
}

std::string print_xformOpOrder(const std::vector<XformOp> &xformOps, const uint32_t indent) {
  std::stringstream ss;

  if (xformOps.size()) {

    ss << pprint::Indent(indent) << "uniform token[] xformOpOrder = [";
    for (size_t i = 0; i < xformOps.size(); i++) {
      if (i > 0) {
        ss << ", ";
      }

      auto xformOp = xformOps[i];
      ss << "\"";
      if (xformOp.inverted) {
        ss << "!invert!";
      }
      ss << to_string(xformOp.op);
      if (!xformOp.suffix.empty()) {
        ss << ":" << xformOp.suffix;
      }
      ss << "\"";
    }
    ss << "]\n";
  }

  return ss.str();
}



std::string print_xformOps(const std::vector<XformOp>& xformOps, const uint32_t indent) {

  std::stringstream ss;

  // xforms props
  if (xformOps.size()) {
    for (size_t i = 0; i < xformOps.size(); i++) {
      const auto xformOp = xformOps[i];

      if (xformOp.op == XformOp::OpType::ResetXformStack) {
        // No need to print value.
        continue;
      }

      ss << pprint::Indent(indent);

      // TODO: Check if `type_name` is set correctly.
      ss << xformOp.type_name << " " ;

      ss << to_string(xformOp.op);
      if (!xformOp.suffix.empty()) {
        ss << ":" << xformOp.suffix;
      }

      if (xformOp.IsTimeSamples()) {
        ss << ".timeSamples";
      }

      ss << " = " << print_timesamples(xformOp.var, indent);

      ss << "\n";
    }
  }

  // uniform token[] xformOpOrder
  ss << print_xformOpOrder(xformOps, indent);

  return ss.str();
}

template<typename T>
std::string print_gprim_predefined(const T &gprim, const uint32_t indent) {
  std::stringstream ss;

  // properties
  ss << print_typed_attr(gprim.doubleSided, "doubleSided", indent);
  ss << print_typed_token_attr(gprim.orientation, "orientation", indent);
  ss << print_typed_attr(gprim.extent, "extent", indent);

  ss << print_typed_token_attr(gprim.visibility, "visibility", indent);

  if (gprim.materialBinding) {
    auto m = gprim.materialBinding.value();
    if (m.binding.IsValid()) {
      ss << pprint::Indent(indent) << "rel material:binding = " << wquote(to_string(m.binding), "<", ">") << "\n";
    }
  }

  // primvars
  ss << print_typed_attr(gprim.displayColor, "primvars:displayColor", indent);
  ss << print_typed_attr(gprim.displayOpacity, "primvars:displayOpacity", indent);

  ss << print_xformOps(gprim.xformOps, indent);

  return ss.str();
}

} // namespace local

std::string to_string(bool v) {
  if (v) {
    return "true";
  } else {
    return "false";
  }
}

std::string to_string(const APISchemas::APIName &name) {
  std::string s;

  switch (name) {
  case APISchemas::APIName::SkelBindingAPI: { s = "SkelBindingAPI"; break; }
  case APISchemas::APIName::MaterialBindingAPI: { s = "MaterialBindingAPI"; break; }
  case APISchemas::APIName::Preliminary_AnchoringAPI: { s = "Preliminary_AnchoringAPI"; break; }
  case APISchemas::APIName::Preliminary_PhysicsColliderAPI: { s = "Preliminary_PhysicsColliderAPI"; break; }
  case APISchemas::APIName::Preliminary_PhysicsRigidBodyAPI: { s = "Preliminary_PhysicsRigidBodyAPI"; break; }
  case APISchemas::APIName::Preliminary_PhysicsMaterialAPI: { s = "Preliminary_PhysicsMaterialAPI"; break; }
  }

  return s;
}

std::string to_string(const StringData &s) {
  if (s.is_triple_quoted) {
    return quote(s.value, "\"\"\"");
  } else {
    return quote(s.value);
  }
}


std::string print_customData(const CustomDataType &customData, const std::string &dict_name, const uint32_t indent) {
  std::stringstream ss;

  ss << pprint::Indent(indent);
  if (!dict_name.empty()) {
    ss << dict_name << " = {\n";
  } else {
    ss << "{\n";
  } 
  for (const auto &item : customData) {
    ss << print_meta(item.second, indent+1);
  }
  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

std::string print_meta(const MetaVariable &meta, const uint32_t indent) {
  std::stringstream ss;

  //ss << "TODO: isObject " << meta.IsObject() << ", isValue " << meta.IsValue() << "\n";

  if (auto pv = meta.Get<CustomDataType>()) {
    // dict
    ss << pprint::Indent(indent) << "dictionary " << meta.name << " {\n";
    for (const auto &item : pv.value()) {
      ss << print_meta(item.second, indent+1);
    }
    ss << pprint::Indent(indent) << "}\n";
  } else {
    ss << pprint::Indent(indent) << meta.type << " " << meta.name << " = " << pprint_value(meta.get_raw()) << "\n";
  }

  return ss.str();
}

std::string to_string(tinyusdz::GeomMesh::InterpolateBoundary v) {
  std::string s;

  switch (v) {
    case tinyusdz::GeomMesh::InterpolateBoundary::None: { s = "none"; break; }
    case tinyusdz::GeomMesh::InterpolateBoundary::EdgeAndCorner: {s = "edgeAndCorner"; break; }
    case tinyusdz::GeomMesh::InterpolateBoundary::EdgeOnly: { s = "edgeOnly"; break; }
  }

  return s;
}

std::string to_string(tinyusdz::GeomMesh::SubdivisionScheme v) {
  std::string s;

  switch (v) {
    case tinyusdz::GeomMesh::SubdivisionScheme::CatmullClark: { s = "catmullClark"; break; }
    case tinyusdz::GeomMesh::SubdivisionScheme::Loop: { s = "loop"; break; }
    case tinyusdz::GeomMesh::SubdivisionScheme::Bilinear: { s = "bilinear"; break; }
    case tinyusdz::GeomMesh::SubdivisionScheme::None: { s = "none"; break; }
  }

  return s;
}

std::string to_string(tinyusdz::GeomMesh::FaceVaryingLinearInterpolation v) {
  std::string s;

  switch (v) {
    case tinyusdz::GeomMesh::FaceVaryingLinearInterpolation::CornersPlus1: { s = "cornersPlus1"; break; }
    case tinyusdz::GeomMesh::FaceVaryingLinearInterpolation::CornersPlus2: { s = "cornersPlus2"; break; }
    case tinyusdz::GeomMesh::FaceVaryingLinearInterpolation::CornersOnly: { s = "cornersOnly"; break; }
    case tinyusdz::GeomMesh::FaceVaryingLinearInterpolation::Boundaries: { s = "boundaries"; break; }
    case tinyusdz::GeomMesh::FaceVaryingLinearInterpolation::None: { s = "none"; break; }
    case tinyusdz::GeomMesh::FaceVaryingLinearInterpolation::All: { s = "all"; break; }
  }

  return s;
}

std::string to_string(const tinyusdz::UsdUVTexture::SourceColorSpace v) {
  std::string s;

  switch (v) {
    case tinyusdz::UsdUVTexture::SourceColorSpace::Auto: { s = "auto"; break; }
    case tinyusdz::UsdUVTexture::SourceColorSpace::Raw: {s = "raw"; break; }
    case tinyusdz::UsdUVTexture::SourceColorSpace::SRGB: { s = "sRGB"; break; }
  }

  return s;
}

std::string to_string(const tinyusdz::UsdUVTexture::Wrap v) {
  std::string s;

  switch (v) {
    case tinyusdz::UsdUVTexture::Wrap::UseMetadata: { s = "useMetadata"; break; }
    case tinyusdz::UsdUVTexture::Wrap::Black: {s = "black"; break; }
    case tinyusdz::UsdUVTexture::Wrap::Clamp: { s = "clamp"; break; }
    case tinyusdz::UsdUVTexture::Wrap::Repeat: { s = "repeat"; break; }
    case tinyusdz::UsdUVTexture::Wrap::Mirror: { s = "mirror"; break; }
  }

  return s;
}

std::string to_string(tinyusdz::Kind v) {
  if (v == tinyusdz::Kind::Model) {
    return "model";
  } else if (v == tinyusdz::Kind::Group) {
    return "group";
  } else if (v == tinyusdz::Kind::Assembly) {
    return "assembly";
  } else if (v == tinyusdz::Kind::Component) {
    return "component";
  } else if (v == tinyusdz::Kind::Subcomponent) {
    return "subcomponent";
  } else if (v == tinyusdz::Kind::SceneLibrary) {
    return "sceneLibrary";
  } else {
    return "[[InvalidKind]]";
  }
}


std::string to_string(tinyusdz::Axis v) {
  if (v == tinyusdz::Axis::X) {
    return "X";
  } else if (v == tinyusdz::Axis::Y) {
    return "Y";
  } else if (v == tinyusdz::Axis::Z) {
    return "Z";
  } else {
    return "[[InvalidAxis]]";
  }
}

std::string to_string(tinyusdz::Visibility v) {
  if (v == tinyusdz::Visibility::Inherited) {
    return "inherited";
  } else {
    return "invisible";
  }
}

std::string to_string(tinyusdz::Orientation o) {
  if (o == tinyusdz::Orientation::RightHanded) {
    return "rightHanded";
  } else {
    return "leftHanded";
  }
}

std::string to_string(tinyusdz::ListEditQual v) {
  if (v == tinyusdz::ListEditQual::ResetToExplicit) {
    return ""; // unqualified
  } else if (v == tinyusdz::ListEditQual::Append) {
    return "append";
  } else if (v == tinyusdz::ListEditQual::Add) {
    return "add";
  } else if (v == tinyusdz::ListEditQual::Append) {
    return "append";
  } else if (v == tinyusdz::ListEditQual::Delete) {
    return "delete";
  } else if (v == tinyusdz::ListEditQual::Prepend) {
    return "prepend";
  } else if (v == tinyusdz::ListEditQual::Order) {
    return "order";
  }

  return "[[Invalid ListEditQual value]]";
}

std::string to_string(tinyusdz::Interpolation interp) {
  switch (interp) {
    case Interpolation::Invalid:
      return "[[Invalid interpolation value]]";
    case Interpolation::Constant:
      return "constant";
    case Interpolation::Uniform:
      return "uniform";
    case Interpolation::Varying:
      return "varying";
    case Interpolation::Vertex:
      return "vertex";
    case Interpolation::FaceVarying:
      return "faceVarying";
  }

  // Never reach here though
  return "[[Invalid interpolation value]]";
}

std::string to_string(tinyusdz::SpecType ty) {
  if (SpecType::Attribute == ty) {
    return "SpecTypeAttribute";
  } else if (SpecType::Connection == ty) {
    return "SpecTypeConnection";
  } else if (SpecType::Expression == ty) {
    return "SpecTypeExpression";
  } else if (SpecType::Mapper == ty) {
    return "SpecTypeMapper";
  } else if (SpecType::MapperArg == ty) {
    return "SpecTypeMapperArg";
  } else if (SpecType::Prim == ty) {
    return "SpecTypePrim";
  } else if (SpecType::PseudoRoot == ty) {
    return "SpecTypePseudoRoot";
  } else if (SpecType::Relationship == ty) {
    return "SpecTypeRelationship";
  } else if (SpecType::RelationshipTarget == ty) {
    return "SpecTypeRelationshipTarget";
  } else if (SpecType::Variant == ty) {
    return "SpecTypeVariant";
  } else if (SpecType::VariantSet == ty) {
    return "SpecTypeVariantSet";
  }
  return "SpecTypeInvalid";
}

std::string to_string(tinyusdz::Specifier s) {
  if (s == tinyusdz::Specifier::Def) {
    return "def";
  } else if (s == tinyusdz::Specifier::Over) {
    return "over";
  } else if (s == tinyusdz::Specifier::Class) {
    return "class";
  } else {
    return "[[SpecifierInvalid]]";
  }
}

std::string to_string(tinyusdz::Permission s) {
  if (s == tinyusdz::Permission::Public) {
    return "public";
  } else if (s == tinyusdz::Permission::Private) {
    return "private";
  } else {
    return "[[PermissionInvalid]]";
  }
}

std::string to_string(tinyusdz::Variability v) {
  if (v == tinyusdz::Variability::Varying) {
    return "varying";
  } else if (v == tinyusdz::Variability::Uniform) {
    return "uniform";
  } else if (v == tinyusdz::Variability::Config) {
    return "config";
  } else {
    return "\"[[VariabilityInvalid]]\"";
  }
}

std::string to_string(tinyusdz::Extent e) {
  std::stringstream ss;

  ss << "[" << e.lower << ", " << e.upper << "]";

  return ss.str();
}

#if 0
std::string to_string(const tinyusdz::AnimatableVisibility &v, const uint32_t indent) {
  if (auto p = nonstd::get_if<Visibility>(&v)) {
    return to_string(*p);
  }

  if (auto p = nonstd::get_if<TimeSampled<Visibility>>(&v)) {

    std::stringstream ss;

    ss << "{";

    for (size_t i = 0; i < p->times.size(); i++) {
      ss << pprint::Indent(indent+2) << p->times[i] << " : " << to_string(p->values[i]) << ", ";
      // TODO: indent and newline
    }

    ss << pprint::Indent(indent+1) << "}";

  }

  return "[[??? AnimatableVisibility]]";
}
#endif

std::string to_string(const tinyusdz::Klass &klass, uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << tinyusdz::pprint::Indent(indent) << "class " << klass.name << " (\n";
  ss << tinyusdz::pprint::Indent(indent) << ")\n";
  ss << tinyusdz::pprint::Indent(indent) << "{\n";

  for (auto prop : klass.props) {

    if (prop.second.IsRel()) {
        ss << "TODO: Rel\n";
    } else {
      //const PrimAttrib &attrib = prop.second.attrib;
#if 0 // TODO
      if (auto p = tinyusdz::primvar::as_basic<double>(&pattr->var)) {
        ss << tinyusdz::pprint::Indent(indent);
        if (pattr->custom) {
          ss << " custom ";
        }
        if (pattr->uniform) {
          ss << " uniform ";
        }
        ss << " double " << prop.first << " = " << *p;
      } else {
        ss << "TODO:" << pattr->type_name << "\n";
      }
#endif
    }

    ss << "\n";
  }

  if (closing_brace) {
    ss << tinyusdz::pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const Model &model, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  // Currently, `Model` is used for typeless `def`
  ss << pprint::Indent(indent) << "def \"" << model.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(model.meta, indent+1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // props
  // TODO:

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const Scope &scope, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Scope \"" << scope.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(scope.meta, indent+1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // props
  // TODO:

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GPrim &gprim, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def GPrim \"" << gprim.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  // args
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  ss << print_gprim_predefined(gprim, indent+1);

  // TODO:
  // props

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const Xform &xform, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Xform \"" << xform.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(xform.meta, indent+1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  ss << print_gprim_predefined(xform, indent+1);

  ss << print_props(xform.props, indent+1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCamera &camera, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Camera \"" << camera.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  // args
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(camera.clippingRange, "clippingRange", indent+1);
  ss << print_typed_attr(camera.clippingPlanes, "clippingPlanes", indent+1);
  ss << print_typed_attr(camera.focalLength, "focalLength", indent+1);
  ss << print_typed_attr(camera.horizontalAperture, "horizontalAperture", indent+1);
  ss << print_typed_attr(camera.horizontalApertureOffset, "horizontalApertureOffset", indent+1);
  ss << print_typed_attr(camera.verticalAperture, "verticalAperture", indent+1);
  ss << print_typed_attr(camera.verticalApertureOffset, "verticalApertureOffset", indent+1);

  ss << print_typed_token_attr(camera.projection, "projection", indent+1);
  ss << print_typed_token_attr(camera.stereoRole, "stereoRole", indent+1);

  ss << print_typed_attr(camera.shutterOpen, "shutter:open", indent+1);
  ss << print_typed_attr(camera.shutterClose, "shutter:close", indent+1);


  ss << print_gprim_predefined(camera, indent);

  ss << print_props(camera.props, indent+1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}


std::string to_string(const GeomSphere &sphere, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Sphere \"" << sphere.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  // args
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(sphere.radius, "radius", indent+1);

  ss << print_gprim_predefined(sphere, indent);

  ss << print_props(sphere.props, indent+1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomMesh &mesh, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Mesh \"" << mesh.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  if (mesh.meta.authored()) {
    ss << print_prim_metas(mesh.meta, indent+1);
  }
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(mesh.points, "points", indent+1);
  ss << print_typed_attr(mesh.normals, "normals", indent+1);
  ss << print_typed_attr(mesh.faceVertexIndices, "faceVertexIndices", indent+1);
  ss << print_typed_attr(mesh.faceVertexCounts, "faceVertexCounts", indent+1);

  if (mesh.skeleton) {
    ss << pprint::Indent(indent+1) << "rel skel:skeleton = " << mesh.skeleton.value() << "\n";
  }

  // subdiv
  ss << print_typed_attr(mesh.cornerIndices, "cornerIndices", indent+1);
  ss << print_typed_attr(mesh.cornerSharpnesses, "cornerSharpnesses", indent+1);
  ss << print_typed_attr(mesh.creaseIndices, "creaseIndices", indent+1);
  ss << print_typed_attr(mesh.creaseLengths, "creaseLengths", indent+1);
  ss << print_typed_attr(mesh.creaseSharpnesses, "creaseSharpnesses", indent+1);
  ss << print_typed_attr(mesh.holeIndices, "holeIndices", indent+1);

  ss << print_typed_token_attr(mesh.subdivisionScheme, "subdivisonScheme", indent+1);
  ss << print_typed_token_attr(mesh.interpolateBoundary, "interpolateBoundary", indent+1);
  ss << print_typed_token_attr(mesh.faceVaryingLinearInterpolation, "faceVaryingLinearInterpolation", indent+1);

  ss << print_gprim_predefined(mesh, indent+1);

  // GeomSubset.
  for (const auto &subset : mesh.geom_subset_children) {
    ss << to_string(subset, indent+1, /* closing_brace */true);
  }

  ss << print_props(mesh.props, indent+1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomSubset &geom, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def GeomSubset \"" << geom.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(geom.meta, indent+1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  ss << pprint::Indent(indent+1) << "[TODO] GeomSubset\n";

  ss << print_props(geom.props, indent+1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomPoints &geom, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Points \"" << geom.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(geom.meta, indent+1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(geom.points, "points", indent+1);
  ss << print_typed_attr(geom.normals, "normals", indent+1);
  ss << print_typed_attr(geom.widths, "widths", indent+1);
  ss << print_typed_attr(geom.ids, "ids", indent+1);
  ss << print_typed_attr(geom.velocities, "velocities", indent+1);
  ss << print_typed_attr(geom.accelerations, "accelerations", indent+1);

  ss << print_gprim_predefined(geom, indent+1);

  ss << print_props(geom.props, indent+1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomBasisCurves::Type &ty) {

  std::string s;

  switch(ty) {
    case GeomBasisCurves::Type::Cubic: { s = "cubic"; break; }
    case GeomBasisCurves::Type::Linear: { s = "linear"; break; }
  }

  return s;
}

std::string to_string(const GeomBasisCurves::Basis &ty) {

  std::string s;

  switch(ty) {
    case GeomBasisCurves::Basis::Bezier: { s = "bezier"; break; }
    case GeomBasisCurves::Basis::Bspline: { s = "bspline"; break; }
    case GeomBasisCurves::Basis::CatmullRom: { s = "catmullRom"; break; }
  }

  return s;
}

std::string to_string(const GeomBasisCurves::Wrap &ty) {

  std::string s;

  switch(ty) {
    case GeomBasisCurves::Wrap::Nonperiodic: { s = "nonperiodic"; break; }
    case GeomBasisCurves::Wrap::Periodic: { s = "periodic"; break; }
    case GeomBasisCurves::Wrap::Pinned: { s = "pinned"; break; }
  }

  return s;
}


std::string to_string(const GeomBasisCurves &geom, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def BasisCurves \"" << geom.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(geom.meta, indent+1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  if (geom.type) {
    ss << pprint::Indent(indent+1) << "uniform token type = " << quote(to_string(geom.type.value())) << "\n";
  }

  if (geom.basis) {
    ss << pprint::Indent(indent+1) << "uniform token basis = " << quote(to_string(geom.basis.value())) << "\n";
  }

  if (geom.wrap) {
    ss << pprint::Indent(indent+1) << "uniform token wrap = " << quote(to_string(geom.wrap.value())) << "\n";
  }

  ss << print_typed_attr(geom.points, "points", indent);
  ss << print_typed_attr(geom.normals, "normals", indent);
  ss << print_typed_attr(geom.widths, "widths", indent);
  ss << print_typed_attr(geom.velocities, "velocites", indent);
  ss << print_typed_attr(geom.accelerations, "accelerations", indent);
  ss << print_typed_attr(geom.curveVertexCounts, "curveVertexCounts", indent);

  ss << print_gprim_predefined(geom, indent+1);

  ss << print_props(geom.props, indent+1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCube &geom, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Cube \"" << geom.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(geom.meta, indent+1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(geom.size, "size", indent+1);

  ss << print_gprim_predefined(geom, indent+1);

  ss << print_props(geom.props, indent+1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCone &geom, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Cone \"" << geom.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(geom.meta, indent+1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(geom.radius, "radius", indent+1);
  ss << print_typed_attr(geom.height, "height", indent+1);

  if (geom.axis) {
    std::string axis;
    if (geom.axis.value() == Axis::X) {
      axis = "\"X\"";
    } else if (geom.axis.value() == Axis::Y) {
      axis = "\"Y\"";
    } else {
      axis = "\"Z\"";
    }
    ss << pprint::Indent(indent+1) << "uniform token axis = " << axis << "\n";
  }

  ss << print_gprim_predefined(geom, indent+1);
  ss << print_props(geom.props, indent+1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCylinder &geom, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Cylinder \"" << geom.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(geom.meta, indent+1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(geom.radius, "radius", indent+1);
  ss << print_typed_attr(geom.height, "height", indent+1);

  if (geom.axis) {
    std::string axis;
    if (geom.axis.value() == Axis::X) {
      axis = "\"X\"";
    } else if (geom.axis.value() == Axis::Y) {
      axis = "\"Y\"";
    } else {
      axis = "\"Z\"";
    }
    ss << pprint::Indent(indent+1) << "uniform token axis = " << axis << "\n";
  }


  ss << print_gprim_predefined(geom, indent+1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCapsule &geom, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Capsule \"" << geom.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(geom.meta, indent+1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(geom.radius, "radius", indent+1);
  ss << print_typed_attr(geom.height, "height", indent+1);

  if (geom.axis) {
    std::string axis;
    if (geom.axis.value() == Axis::X) {
      axis = "\"X\"";
    } else if (geom.axis.value() == Axis::Y) {
      axis = "\"Y\"";
    } else {
      axis = "\"Z\"";
    }
    ss << pprint::Indent(indent+1) << "uniform token axis = " << axis << "\n";
  }

  ss << print_gprim_predefined(geom, indent+1);
  ss << print_props(geom.props, indent+1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const SkelRoot &root, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def SkelRoot \"" << root.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(root.meta, indent+1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // TODO
  // Skeleton id
  //ss << pprint::Indent(indent) << "skelroot.skeleton_id << "\n"

  ss << print_xformOps(root.xformOps, indent+1);

  //ss << print_props(root.props, indent+1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const Skeleton &skel, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Skeleton \"" << skel.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(skel.meta, indent+1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  ss << print_typed_attr(skel.bindTransforms, "bindTransforms", indent+1);
  ss << print_typed_attr(skel.jointNames, "jointNames", indent+1);
  ss << print_typed_attr(skel.joints, "joints", indent+1);
  ss << print_typed_attr(skel.restTransforms, "restTransforms", indent+1);

  if (skel.animationSource) {
    ss << pprint::Indent(indent+1) << "rel skel:animationSource = " << pquote(skel.animationSource.value()) << "\n";
  }

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const SkelAnimation &skelanim, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def SkelAnimation \"" << skelanim.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(skelanim.meta, indent+1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  ss << print_typed_attr(skelanim.blendShapes, "blendShapes", indent+1);
  ss << print_typed_attr(skelanim.blendShapeWeights, "blendShapeWeights", indent+1);
  ss << print_typed_attr(skelanim.joints, "joints", indent+1);
  ss << print_typed_attr(skelanim.rotations, "rotations", indent+1);
  ss << print_typed_attr(skelanim.scales, "scales", indent+1);
  ss << print_typed_attr(skelanim.translations, "translations", indent+1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}


std::string to_string(const BlendShape &prim, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def BlendShape \"" << prim.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(prim.meta, indent+1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  ss << print_typed_attr(prim.offsets, "offsets", indent+1);
  ss << print_typed_attr(prim.normalOffsets, "normalOffsets", indent+1);
  ss << print_typed_attr(prim.pointIndices, "pointIndices", indent+1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const Material &material, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Material \"" << material.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(material.meta, indent+1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  if (material.surface) {
    ss << pprint::Indent(indent+1) << "token outputs:surface ";
    // Must have connection though.
    if (material.surface.value().target) {
      ss << "= " << pquote(material.surface.value().target.value());
    }
    ss << "\n";
  }

  if (material.volume) {
    ss << pprint::Indent(indent+1) << "token outputs:volume ";
    // Must have connection though.
    if (material.volume.value().target) {
      ss << "= " << pquote(material.volume.value().target.value());
    }
    ss << "\n";
  }

  ss << print_props(material.props, indent+1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

static std::string print_shader_params(const UsdPrimvarReader_float &shader, const uint32_t indent) {
  std::stringstream ss;

  ss << print_typed_attr(shader.varname, "inputs:varname", indent);
  ss << print_typed_attr(shader.fallback, "inputs:fallback", indent);
  ss << print_typed_terminal_attr(shader.result, "outputs:result", indent);

  ss << print_props(shader.props, indent);

  return ss.str();

}

static std::string print_shader_params(const UsdPrimvarReader_float2 &shader, const uint32_t indent) {
  std::stringstream ss;

  ss << print_typed_attr(shader.varname, "inputs:varname", indent);
  ss << print_typed_attr(shader.fallback, "inputs:fallback", indent);
  ss << print_typed_terminal_attr(shader.result, "outputs:result", indent);

  ss << print_props(shader.props, indent);

  return ss.str();
}

static std::string print_shader_params(const UsdPrimvarReader_float3 &shader, const uint32_t indent) {
  std::stringstream ss;

  ss << print_typed_attr(shader.varname, "inputs:varname", indent);
  ss << print_typed_attr(shader.fallback, "inputs:fallback", indent);
  ss << print_typed_terminal_attr(shader.result, "outputs:result", indent);

  ss << print_props(shader.props, indent);

  return ss.str();
}

static std::string print_shader_params(const UsdPrimvarReader_float4 &shader, const uint32_t indent) {
  std::stringstream ss;

  ss << print_typed_attr(shader.varname, "inputs:varname", indent);
  ss << print_typed_attr(shader.fallback, "inputs:fallback", indent);
  ss << print_typed_terminal_attr(shader.result, "outputs:result", indent);

  ss << print_props(shader.props, indent);

  return ss.str();
}

static std::string print_shader_params(const UsdPreviewSurface &shader, const uint32_t indent) {
  std::stringstream ss;

  ss << print_typed_attr(shader.diffuseColor, "inputs:diffuseColor", indent);
  ss << print_typed_attr(shader.emissiveColor, "inputs:emissiveColor", indent);
  ss << print_typed_attr(shader.useSpecularWorkflow, "inputs:useSpecularWorkflow", indent);
  ss << print_typed_attr(shader.ior, "inputs:ior", indent);
  ss << print_typed_attr(shader.specularColor, "inputs:specularColor", indent);
  ss << print_typed_attr(shader.metallic, "inputs:metallic", indent);
  ss << print_typed_attr(shader.clearcoat, "inputs:clearcoat", indent);
  ss << print_typed_attr(shader.clearcoatRoughness, "inputs:clearcoatRoughness", indent);
  ss << print_typed_attr(shader.roughness, "inputs:roughness", indent);
  ss << print_typed_attr(shader.opacity, "inputs:opacity", indent);
  ss << print_typed_attr(shader.opacityThreshold, "inputs:opacityThreshold", indent);
  ss << print_typed_attr(shader.normal, "inputs:normal", indent);
  ss << print_typed_attr(shader.displacement, "inputs:displacement", indent);
  ss << print_typed_attr(shader.occlusion, "inputs:occlusion", indent);

  // Outputs
  if (shader.outputsSurface) {
    ss << pprint::Indent(indent) << "token outputs:surface";
    if (shader.outputsSurface.value().IsPath()) {
      ss << ".connect = " << pquote(shader.outputsSurface.value().targetPath);
    }
    if (shader.outputsSurface.value().meta.authored()) {
      ss << "(\n" << print_attr_metas(shader.outputsSurface.value().meta, indent+1) << pprint::Indent(indent+1) << ")";
    }
    ss << "\n";
  }

  if (shader.outputsDisplacement) {
    ss << pprint::Indent(indent) << "token outputs:displacement";
    if (shader.outputsDisplacement.value().IsPath()) {
      ss << ".connect = " << pquote(shader.outputsDisplacement.value().targetPath) << "\n";
    }
    if (shader.outputsDisplacement.value().meta.authored()) {
      ss << "(\n" << print_attr_metas(shader.outputsDisplacement.value().meta, indent+1) << pprint::Indent(indent+1) << ")";
    }
    ss << "\n";
  }

  return ss.str();

}

static std::string print_shader_params(const UsdUVTexture &shader, const uint32_t indent) {
  std::stringstream ss;

  ss << print_typed_attr(shader.file, "inputs:file", indent);

  ss << print_typed_token_attr(shader.sourceColorSpace, "inputs:sourceColorSpace", indent);


  ss << print_typed_attr(shader.st, "inputs:st", indent);
  ss << print_typed_token_attr(shader.wrapS, "inputs:wrapT", indent);
  ss << print_typed_token_attr(shader.wrapT, "inputs:wrapS", indent);

  ss << print_typed_terminal_attr(shader.outputsR, "outputs:r", indent);
  ss << print_typed_terminal_attr(shader.outputsG, "outputs:g", indent);
  ss << print_typed_terminal_attr(shader.outputsB, "outputs:b", indent);
  ss << print_typed_terminal_attr(shader.outputsRGB, "outputs:rgb", indent);

  return ss.str();
}

std::string to_string(const Shader &shader, const uint32_t indent, bool closing_brace) {

  {
    // generic Shader class
    std::stringstream ss;

    ss << pprint::Indent(indent) << "def Shader \"" << shader.name << "\"\n";
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(shader.meta, indent+1);
    ss << pprint::Indent(indent) << ")\n";
    ss << pprint::Indent(indent) << "{\n";

    // members
    ss << pprint::Indent(indent+1) << "uniform token info:id = \"" << shader.info_id << "\"\n";

    if (auto pvr = shader.value.get_value<UsdPrimvarReader_float>()) {
      ss << print_shader_params(pvr.value(), indent+1);
    } else if (auto pvr2 = shader.value.get_value<UsdPrimvarReader_float2>()) {
      ss << print_shader_params(pvr2.value(), indent+1);
    } else if (auto pvr3 = shader.value.get_value<UsdPrimvarReader_float3>()) {
      ss << print_shader_params(pvr3.value(), indent+1);
    } else if (auto pvr4 = shader.value.get_value<UsdPrimvarReader_float4>()) {
      ss << print_shader_params(pvr4.value(), indent+1);
    } else if (auto pvtex = shader.value.get_value<UsdUVTexture>()) {
      ss << print_shader_params(pvtex.value(), indent+1);
    } else if (auto pvs = shader.value.get_value<UsdPreviewSurface>()) {
      ss << print_shader_params(pvs.value(), indent+1);
    } else {
      ss << pprint::Indent(indent+1) << "[TODO] Generic Shader\n";
    }


    if (closing_brace) {
      ss << pprint::Indent(indent) << "}\n";
    }

    return ss.str();
  }

}


std::string to_string(const LuxSphereLight &light, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def SphereLight \"" << light.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(light.meta, indent+1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(light.color, "inputs:color", indent+1);
  ss << print_typed_attr(light.intensity, "inputs:intensity", indent+1);
  ss << print_typed_attr(light.radius, "inputs:radius", indent+1);
  ss << print_typed_attr(light.specular, "inputs:specular", indent+1);

  ss << print_xformOps(light.xformOps, indent+1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const LuxDistantLight &light, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def DistantLight \"" << light.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(light.meta, indent+1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(light.color, "inputs:color", indent+1);
  ss << print_typed_attr(light.intensity, "inputs:intensity", indent+1);
  ss << print_typed_attr(light.specular, "inputs:specular", indent+1);

  ss << print_typed_attr(light.angle, "inputs:angle", indent+1);

  ss << print_xformOps(light.xformOps, indent+1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const LuxCylinderLight &light, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def CylinderLight \"" << light.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(light.meta, indent+1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(light.color, "inputs:color", indent+1);
  ss << print_typed_attr(light.intensity, "inputs:intensity", indent+1);
  ss << print_typed_attr(light.radius, "inputs:radius", indent+1);
  ss << print_typed_attr(light.specular, "inputs:specular", indent+1);

  ss << print_typed_attr(light.length, "inputs:length", indent+1);
  ss << print_typed_attr(light.radius, "inputs:radius", indent+1);

  ss << print_xformOps(light.xformOps, indent+1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const LuxDiskLight &light, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def DiskLight \"" << light.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(light.meta, indent+1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(light.color, "inputs:color", indent+1);
  ss << print_typed_attr(light.intensity, "inputs:intensity", indent+1);
  ss << print_typed_attr(light.radius, "inputs:radius", indent+1);
  ss << print_typed_attr(light.specular, "inputs:specular", indent+1);

  ss << print_typed_attr(light.radius, "inputs:radius", indent+1);

  ss << print_xformOps(light.xformOps, indent+1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const LuxDomeLight &light, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def DomeLight \"" << light.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(light.meta, indent+1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(light.color, "inputs:color", indent+1);
  ss << print_typed_attr(light.intensity, "inputs:intensity", indent+1);

  ss << print_xformOps(light.xformOps, indent+1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}


std::string to_string(const GeomCamera::Projection &proj) {

  if (proj == GeomCamera::Projection::Orthographic) {
    return "orthographic";
  } else {
    return "perspective";
  }
}

std::string to_string(const GeomCamera::StereoRole &role) {

  if (role == GeomCamera::StereoRole::Mono) {
    return "mono";
  } else if (role == GeomCamera::StereoRole::Right) {
    return "right";
  } else {
    return "left";
  }
}

std::string to_string(const Path &path, bool show_full_path) {
  if (show_full_path) {
    return path.full_path_name();
  } else {
    // TODO
    return path.full_path_name();
  }
}

std::string to_string(const std::vector<Path> &v, bool show_full_path) {

  // TODO(syoyo): indent
  std::stringstream ss;
  ss << "[";

  for (size_t i = 0; i < v.size(); i++) {
    ss << to_string(v[i], show_full_path);
    if (i != (v.size() -1)) {
      ss << ", ";
    }
  }
  ss << "]";
  return ss.str();
}

std::string to_string(const XformOp::OpType &op) {
  std::string ss;

  switch (op) {
  case XformOp::OpType::ResetXformStack: { ss = "!resetXformStack!"; break; }
  case XformOp::OpType::Transform: { ss = "xformOp:transform"; break; }
  case XformOp::OpType::Translate: { ss = "xformOp:translate"; break; }
  case XformOp::OpType::Scale: { ss = "xformOp:scale"; break; }
  case XformOp::OpType::RotateX: { ss = "xformOp:rotateX"; break; }
  case XformOp::OpType::RotateY: { ss = "xformOp:rotateY"; break; }
  case XformOp::OpType::RotateZ: { ss = "xformOp:rotateZ"; break; }
  case XformOp::OpType::RotateXYZ: { ss = "xformOp:rotateXYZ"; break; }
  case XformOp::OpType::RotateXZY: { ss = "xformOp:rotateXZY"; break; }
  case XformOp::OpType::RotateYXZ: { ss = "xformOp:rotateYXZ"; break; }
  case XformOp::OpType::RotateYZX: { ss = "xformOp:rotateYZX"; break; }
  case XformOp::OpType::RotateZXY: { ss = "xformOp:rotateZXY"; break; }
  case XformOp::OpType::RotateZYX: { ss = "xformOp:rotateZYX"; break; }
  case XformOp::OpType::Orient: { ss = "xformOp:orient"; break; }
  }

  return ss;
}

std::string to_string(const tinyusdz::value::token &v) {
  return v.str();
}

} // tinyusdz

