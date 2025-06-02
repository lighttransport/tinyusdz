// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2022, Syoyo Fujita.
// Copyright 2023, Light Transport Entertainment Inc.
//
// USD ASCII pretty printer.
//
//
#include "pprinter.hh"
#include "pprinter-util.hh"

#include "prim-pprint.hh"
#include "prim-types.hh"
#include "str-util.hh"
#include "tiny-format.hh"
#include "usdShade.hh"
#include "value-pprint.hh"
//
#include "common-macros.inc"

// For fast int/float to ascii
// Default disabled.
// #define TINYUSDZ_LOCAL_USE_JEAIII_ITOA

#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
#include "external/jeaiii_to_text.h"
#endif

// dtoa_milo does not work well for float types
// (e.g. it prints float 0.01 as 0.009999999997),
// so use floaxie for float types
// TODO: Use floaxie also for double?
#include "external/dtoa_milo.h"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// #include "external/floaxie/floaxie/ftoa.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

// TODO:
// - [ ] Print properties based on lexcographically(USDA)
// - [ ] Refactor variantSet stmt print.
// - [ ] wrap float/double print with `dtos` for accurate float/double value
// stringify.

namespace tinyusdz {

namespace {

#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
void itoa(uint32_t n, char *b) { *jeaiii::to_text_from_integer(b, n) = '\0'; }
void itoa(int32_t n, char *b) { *jeaiii::to_text_from_integer(b, n) = '\0'; }
void itoa(uint64_t n, char *b) { *jeaiii::to_text_from_integer(b, n) = '\0'; }
void itoa(int64_t n, char *b) { *jeaiii::to_text_from_integer(b, n) = '\0'; }
#endif

#if 0
inline std::string dtos(const float v) {

  char buf[floaxie::max_buffer_size<float>()];
  size_t n = floaxie::ftoa(v, buf);

  return std::string(buf, buf + n);
}
#endif

#if 0
inline std::string dtos(const double v) {
  char buf[128];
  dtoa_milo(v, buf);

  return std::string(buf);
}
#endif

// Path quote
std::string pquote(const Path &p) {
  return wquote(p.full_path_name(), "<", ">");
}

}  // namespace
}  // namespace tinyusdz

namespace std {

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::Visibility v) {
  ofs << to_string(v);
  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::Extent v) {
  ofs << to_string(v);

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::Interpolation v) {
  ofs << to_string(v);

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::Path &v) {
  ofs << tinyusdz::pquote(v);

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::LayerOffset &v) {
  bool print_offset{true};
  bool print_scale{true};

  if (std::fabs(v._offset) < std::numeric_limits<double>::epsilon()) {
    print_offset = false;
  }

  if (std::fabs(v._scale - 1.0) < std::numeric_limits<double>::epsilon()) {
    print_scale = false;
  }

  if (!print_offset && !print_scale) {
    // No need to print LayerOffset.
    return ofs;
  }

  // TODO: Do not print scale when it is 1.0
  ofs << "(";
  if (print_offset && print_scale) {
    ofs << "offset = " << tinyusdz::dtos(v._offset)
        << ", scale = " << tinyusdz::dtos(v._scale);
  } else if (print_offset) {
    ofs << "offset = " << tinyusdz::dtos(v._offset);
  } else {  // print_scale
    ofs << "scale = " << tinyusdz::dtos(v._scale);
  }
  ofs << ")";

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::Reference &v) {
  ofs << v.asset_path;
  if (v.prim_path.is_valid()) {
    ofs << v.prim_path;
  }
  ofs << v.layerOffset;
  if (!v.customData.empty()) {
    ofs << tinyusdz::print_customData(v.customData, "customData",
                                      /* indent */ 0);
  }

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::Payload &v) {
  if (v.is_none()) {
    ofs << "None";
  } else {
    ofs << v.asset_path;
    if (v.prim_path.is_valid()) {
      ofs << v.prim_path;
    }
    ofs << v.layerOffset;
  }

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::SubLayer &v) {
  ofs << v.assetPath << v.layerOffset;

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs,
                         const tinyusdz::value::StringData &v) {
#if 0
  std::string delim = v.single_quote ? "'" : "\"";

  if (v.is_triple_quoted) {
    if (v.single_quote) {
      if (tinyusdz::hasEscapedTripleQuotes(v.value, /* double quote */false)) {
        // Change to use """
        delim = "\"\"\"";
      } else {
        delim = "'''";
      }
    } else {
      delim = "\"\"\"";
    }
  }

  ofs << delim;
  ofs << tinyusdz::escapeBackslash(v.value, v.is_triple_quoted);
  ofs << delim;
#else
  ofs << tinyusdz::buildEscapedAndQuotedStringForUSDA(v.value);
#endif

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::Layer &layer) {
  ofs << to_string(layer);
  return ofs;
}

}  // namespace std

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

void SetIndentString(const std::string &s) { sIndentString = s; }

}  // namespace pprint

namespace {

std::string print_references(const prim::ReferenceList &references,
                             const uint32_t indent) {
  std::stringstream ss;

  auto listEditQual = std::get<0>(references);
  auto vars = std::get<1>(references);

  ss << pprint::Indent(indent);

  if (listEditQual != ListEditQual::ResetToExplicit) {
    ss << to_string(listEditQual) << " ";
  }

  ss << "references = ";

  if (vars.empty()) {
    ss << "None";
  } else {
    if (vars.size() == 1) {
      ss << vars[0];
    } else {
      ss << vars;
    }
  }
  ss << "\n";

  return ss.str();
}

/*
std::string print_rel_only(const Relationship &rel, const std::string &name,
                           uint32_t indent) {
  std::stringstream ss;

  ss << "rel " << name;

  if (!rel.has_value()) {
    // nothing todo
  } else if (rel.is_path()) {
    ss << " = " << rel.targetPath;
  } else if (rel.is_pathvector()) {
    ss << " = " << rel.targetPathVector;
  } else if (rel.is_blocked()) {
    ss << " = None";
  } else {
    ss << "[InternalErrror]";
  }

  if (rel.metas().authored()) {
    ss << " (\n"
       << print_attr_metas(rel.metas(), indent + 1) << pprint::Indent(indent)
       << ")";
  }

  ss << "\n";

  return ss.str();
}
*/

}  // namespace

std::string print_payload(const prim::PayloadList &payload,
                          const uint32_t indent) {
  std::stringstream ss;

  auto listEditQual = std::get<0>(payload);
  auto vars = std::get<1>(payload);

  ss << pprint::Indent(indent);

  if (listEditQual != ListEditQual::ResetToExplicit) {
    ss << to_string(listEditQual) << " ";
  }

  ss << "payload = ";
  if (vars.empty()) {
    ss << "None";
  } else {
    if (vars.size() == 1) {
      ss << vars[0];
    } else {
      ss << vars;
    }
  }
  ss << "\n";

  return ss.str();
}

std::string print_prim_metas(const PrimMeta &meta, const uint32_t indent) {
  std::stringstream ss;

  if (meta.active) {
    ss << pprint::Indent(indent)
       << "active = " << to_string(meta.active.value()) << "\n";
  }

  if (meta.clips) {
    ss << print_customData(meta.clips.value(), "clips", indent);
  }

  if (meta.instanceable) {
    ss << pprint::Indent(indent)
       << "instanceable = " << to_string(meta.instanceable.value()) << "\n";
  }

  if (meta.hidden) {
    ss << pprint::Indent(indent)
       << "hidden = " << to_string(meta.hidden.value()) << "\n";
  }

  if (meta.kind) {
    ss << pprint::Indent(indent) << "kind = " << quote(meta.get_kind()) << "\n";
  }

  // TODO: UTF-8 ready pprint
  if (meta.sceneName) {
    ss << pprint::Indent(indent)
       << "sceneName = " << quote(meta.sceneName.value()) << "\n";
  }

  // TODO: UTF-8 ready pprint
  if (meta.displayName) {
    ss << pprint::Indent(indent)
       << "displayName = " << quote(meta.displayName.value()) << "\n";
  }

  if (meta.assetInfo) {
    ss << print_customData(meta.assetInfo.value(), "assetInfo", indent);
  }

  if (meta.inherits) {
    ss << pprint::Indent(indent);
    auto listEditQual = std::get<0>(meta.inherits.value());
    auto var = std::get<1>(meta.inherits.value());

    if (listEditQual != ListEditQual::ResetToExplicit) {
      ss << to_string(listEditQual) << " ";
    }

    if (var.size() == 1) {
      // print as scalar
      ss << "inherits = " << var[0];
    } else {
      ss << "inherits = " << var;
    }
    ss << "\n";
  }

  if (meta.specializes) {
    ss << pprint::Indent(indent);
    auto listEditQual = std::get<0>(meta.specializes.value());
    auto var = std::get<1>(meta.specializes.value());

    if (listEditQual != ListEditQual::ResetToExplicit) {
      ss << to_string(listEditQual) << " ";
    }

    if (var.size() == 1) {
      // print as scalar
      ss << "specializes = " << var[0];
    } else {
      ss << "specializes = " << var;
    }
    ss << "\n";
  }

  if (meta.references) {
    ss << print_references(meta.references.value(), indent);
  }

  if (meta.payload) {
    ss << print_payload(meta.payload.value(), indent);
  }

  // TODO: only print in usdShade Prims.
  if (meta.sdrMetadata) {
    ss << print_customData(meta.sdrMetadata.value(), "sdrMetadata", indent);
  }

  if (meta.variants) {
    ss << print_variantSelectionMap(meta.variants.value(), indent);
  }

  if (meta.variantSets) {
    ss << pprint::Indent(indent);
    auto listEditQual = std::get<0>(meta.variantSets.value());
    const std::vector<std::string> &vs =
        std::get<1>(meta.variantSets.value());  // string[]

    if (listEditQual != ListEditQual::ResetToExplicit) {
      ss << to_string(listEditQual) << " ";
    }

    ss << "variantSets = ";

    if (vs.empty()) {
      ss << "None";
    } else {
      ss << to_string(vs);
    }

    ss << "\n";
  }

  if (meta.apiSchemas) {
    auto schemas = meta.apiSchemas.value();

    if (schemas.names.size()) {
      ss << pprint::Indent(indent) << to_string(schemas.listOpQual)
         << " apiSchemas = [";

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
    ss << pprint::Indent(indent) << "doc = " << to_string(meta.doc.value())
       << "\n";
  }

  if (meta.comment) {
    ss << pprint::Indent(indent)
       << "comment = " << to_string(meta.comment.value()) << "\n";
  }

  if (meta.customData) {
    ss << print_customData(meta.customData.value(), "customData", indent);
  }

  for (const auto &item : meta.unregisteredMetas) {
    // do not quote
    ss << pprint::Indent(indent) << item.first << " = " << item.second << "\n";
  }

  // TODO: deprecate meta.meta and remove it.
  for (const auto &item : meta.meta) {
    ss << print_meta(item.second, indent + 1, true, item.first);
  }

  // for (const auto &item : meta.stringData) {
  //   ss << pprint::Indent(indent) << to_string(item) << "\n";
  // }

  return ss.str();
}

#if 0
static std::string print_str_attr(
    const TypedAttribute<Animatable<std::string>> &attr,
    const std::string &name, const uint32_t indent) {
  std::stringstream ss;

  if (attr.authored()) {

    bool is_value_empty = attr.is_value_empty();
    bool is_connection = attr.is_connection();
    bool has_default{false};
    bool has_timesamples{false};
    bool is_timesamples{false};
    const auto &pv = attr.get_value();
    DCOUT("is_value_empty " << is_value_empty);
    DCOUT("is_connection " << is_connection);
    DCOUT("is_timesamples " << is_timesamples);
    DCOUT("has_default " << has_default);

    has_default = (pv && pv.value().has_default());
    has_timesamples = (pv && pv.value().has_timesamples());
    is_timesamples = (pv && pv.value().is_timesamples());

    if (attr.metas().authored() || attr.is_blocked() || has_default || is_value_empty || ((!is_connection) && (!is_timesamples))) {
      ss << pprint::Indent(indent);
      ss << value::TypeTraits<std::string>::type_name() << " " << name;

      if (attr.is_blocked()) {
        ss << " = None";
      } else if (pv.has_value()) {

        std::string a;
        if (pv.value().get_scalar(&a)) {
          // Do not use operator<<(std::string)
          ss << " = " << tinyusdz::buildEscapedAndQuotedStringForUSDA(a);
        } else {
          ss << " = [InternalError]";
        }
      }

      if (attr.metas().authored()) {
        ss << "(\n"
           << print_attr_metas(attr.metas(), indent + 1) << pprint::Indent(indent)
           << ")";
      }
      ss << "\n";
    }

    if (has_timesamples) {
      ss << pprint::Indent(indent);
      ss << value::TypeTraits<std::string>::type_name() << " " << name;

      ss << ".timeSamples = "
               << print_str_timesamples(pv.value().get_timesamples(), indent);

      ss << "\n";
    }

    if (attr.has_connections()) {

      ss << pprint::Indent(indent);
      ss << value::TypeTraits<std::string>::type_name() << " " << name;

      ss << ".connect = ";
      const std::vector<Path> &paths = attr.get_connections();
      if (paths.size() == 1) {
        ss << paths[0];
      } else if (paths.size() == 0) {
        ss << "[InternalError]";
      } else {
        ss << paths;
      }

      ss << "\n";
    }
  }

  return ss.str();
}
#endif

#if 0
template<typename T>
std::string print_typed_token_attr(const TypedAttribute<Animatable<T>> &attr, const std::string &name, const uint32_t indent) {

  std::stringstream ss;

  if (attr.value) {

    ss << pprint::Indent(indent);

    ss << "token " << name;

    if (attr.is_blocked()) {
      ss << " = None";
    } else if (!attr.define_only) {
      ss << " = ";
      if (attr.value.value().is_timesamples()) {
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

template <typename T>
std::string print_typed_terminal_attr(const TypedTerminalAttribute<T> &attr,
                                      const std::string &name,
                                      const uint32_t indent) {
  std::stringstream ss;

  if (attr.authored()) {
    ss << pprint::Indent(indent);

    if (attr.has_actual_type()) {
      ss << attr.get_actual_type_name() << " " << name;
    } else {
      ss << value::TypeTraits<T>::type_name() << " " << name;
    }

    if (attr.metas().authored()) {
      ss << " (\n"
         << print_attr_metas(attr.metas(), indent + 1) << pprint::Indent(indent)
         << ")";
    }
    ss << "\n";
  }

  return ss.str();
}

#if 0
// Implementation of to_string for SphereLight moved to pprinter-light.cc
// Implementation of to_string for DistantLight moved to pprinter-light.cc
// Implementation of to_string for CylinderLight moved to pprinter-light.cc
// Implementation of to_string for DiskLight moved to pprinter-light.cc
// Implementation of to_string for DomeLight moved to pprinter-light.cc
// Implementation of to_string for RectLight moved to pprinter-light.cc
#endif

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
    if (i != (v.size() - 1)) {
      ss << ", ";
    }
  }
  ss << "]";
  return ss.str();
}

std::string to_string(const XformOp::OpType &op) {
  std::string ss;

  switch (op) {
    case XformOp::OpType::ResetXformStack: {
      ss = "!resetXformStack!";
      break;
    }
    case XformOp::OpType::Transform: {
      ss = "xformOp:transform";
      break;
    }
    case XformOp::OpType::Translate: {
      ss = "xformOp:translate";
      break;
    }
    case XformOp::OpType::Scale: {
      ss = "xformOp:scale";
      break;
    }
    case XformOp::OpType::RotateX: {
      ss = "xformOp:rotateX";
      break;
    }
    case XformOp::OpType::RotateY: {
      ss = "xformOp:rotateY";
      break;
    }
    case XformOp::OpType::RotateZ: {
      ss = "xformOp:rotateZ";
      break;
    }
    case XformOp::OpType::RotateXYZ: {
      ss = "xformOp:rotateXYZ";
      break;
    }
    case XformOp::OpType::RotateXZY: {
      ss = "xformOp:rotateXZY";
      break;
    }
    case XformOp::OpType::RotateYXZ: {
      ss = "xformOp:rotateYXZ";
      break;
    }
    case XformOp::OpType::RotateYZX: {
      ss = "xformOp:rotateYZX";
      break;
    }
    case XformOp::OpType::RotateZXY: {
      ss = "xformOp:rotateZXY";
      break;
    }
    case XformOp::OpType::RotateZYX: {
      ss = "xformOp:rotateZYX";
      break;
    }
    case XformOp::OpType::Orient: {
      ss = "xformOp:orient";
      break;
    }
  }

  return ss;
}

//std::string to_string(const tinyusdz::value::token &v) { return v.str(); }

std::string to_string(const DomeLight::TextureFormat &texformat) {
  std::string s = "[InvalidTextureFormat]";

  switch (texformat) {
    case DomeLight::TextureFormat::Automatic: {
      s = "automatic";
      break;
    }
    case DomeLight::TextureFormat::Latlong: {
      s = "latlong";
      break;
    }
    case DomeLight::TextureFormat::MirroredBall: {
      s = "mirroedBall";
      break;
    }
    case DomeLight::TextureFormat::Angular: {
      s = "angular";
      break;
    }
  }

  return s;
}

std::string dump_path(const Path &path) {
  std::stringstream ss;
  ss << "Path: Prim part = " << path.prim_part();
  ss << ", Prop part = " << path.prop_part();
  ss << ", Variant part = " << path.variant_part();
  ss << ", elementName = " << path.element_name();
  ss << ", isValid = " << path.is_valid();
  ss << ", isAbsolute = " << path.is_absolute_path();
  ss << ", isRelative = " << path.is_relative_path();

  return ss.str();
}

std::string print_layer_metas(const LayerMetas &metas, const uint32_t indent) {
  std::stringstream meta_ss;

  if (metas.doc.value.empty()) {
    // ss << pprint::Indent(1) << "doc = \"Exporterd from TinyUSDZ v" <<
    // tinyusdz::version_major
    //    << "." << tinyusdz::version_minor << "." << tinyusdz::version_micro
    //    << tinyusdz::version_rev << "\"\n";
  } else {
    meta_ss << pprint::Indent(indent) << "doc = " << to_string(metas.doc)
            << "\n";
  }

  if (metas.metersPerUnit.authored()) {
    meta_ss << pprint::Indent(indent)
            << "metersPerUnit = " << metas.metersPerUnit.get_value() << "\n";
  }

  if (metas.kilogramsPerUnit.authored()) {
    meta_ss << pprint::Indent(indent)
            << "kilogramsPerUnit = " << metas.kilogramsPerUnit.get_value() << "\n";
  }

  if (metas.upAxis.authored()) {
    meta_ss << pprint::Indent(indent)
            << "upAxis = " << quote(to_string(metas.upAxis.get_value()))
            << "\n";
  }

  if (metas.timeCodesPerSecond.authored()) {
    meta_ss << pprint::Indent(indent)
            << "timeCodesPerSecond = " << metas.timeCodesPerSecond.get_value()
            << "\n";
  }

  if (metas.startTimeCode.authored()) {
    meta_ss << pprint::Indent(indent)
            << "startTimeCode = " << metas.startTimeCode.get_value() << "\n";
  }

  if (metas.endTimeCode.authored()) {
    meta_ss << pprint::Indent(indent)
            << "endTimeCode = " << metas.endTimeCode.get_value() << "\n";
  }

  if (metas.framesPerSecond.authored()) {
    meta_ss << pprint::Indent(indent)
            << "framesPerSecond = " << metas.framesPerSecond.get_value()
            << "\n";
  }

  // TODO: Do not print subLayers when consumed(after composition evaluated)
  if (metas.subLayers.size()) {
    meta_ss << pprint::Indent(indent) << "subLayers = " << metas.subLayers
            << "\n";
  }

  if (metas.defaultPrim.str().size()) {
    meta_ss << pprint::Indent(1)
            << "defaultPrim = " << tinyusdz::quote(metas.defaultPrim.str())
            << "\n";
  }

  if (metas.autoPlay.authored()) {
    meta_ss << pprint::Indent(1)
            << "autoPlay = " << to_string(metas.autoPlay.get_value()) << "\n";
  }

  if (metas.playbackMode.authored()) {
    auto v = metas.playbackMode.get_value();
    if (v == LayerMetas::PlaybackMode::PlaybackModeLoop) {
      meta_ss << pprint::Indent(indent) << "playbackMode = \"loop\"\n";
    } else {  // None
      meta_ss << pprint::Indent(indent) << "playbackMode = \"none\"\n";
    }
  }

  if (!metas.comment.value.empty()) {
    // Stage meta omits 'comment'
    meta_ss << pprint::Indent(indent) << to_string(metas.comment) << "\n";
  }

  if (metas.customLayerData.size()) {
    meta_ss << print_customData(metas.customLayerData, "customLayerData",
                                /* indent */ 1);
  }

  return meta_ss.str();
}

std::string print_layer(const Layer &layer, const uint32_t indent) {
  std::stringstream ss;

  // FIXME: print magic-header outside of this function?
  ss << pprint::Indent(indent) << "#usda 1.0\n";

  std::stringstream meta_ss;
  meta_ss << print_layer_metas(layer.metas(), indent + 1);

  if (meta_ss.str().size()) {
    ss << "(\n";
    ss << meta_ss.str();
    ss << ")\n";
  }

  ss << "\n";

  if (layer.metas().primChildren.size() == layer.primspecs().size()) {
    std::map<std::string, const PrimSpec *> primNameTable;
    for (const auto &item : layer.primspecs()) {
      primNameTable.emplace(item.first, &item.second);
    }

    for (size_t i = 0; i < layer.metas().primChildren.size(); i++) {
      value::token nameTok = layer.metas().primChildren[i];
      // DCOUT(fmt::format("primChildren  {}/{} = {}", i,
      //                   layer.metas().primChildren.size(), nameTok.str()));
      const auto it = primNameTable.find(nameTok.str());
      if (it != primNameTable.end()) {
        ss << prim::print_primspec((*it->second), indent);
        if (i != (layer.metas().primChildren.size() - 1)) {
          ss << "\n";
        }
      } else {
        // TODO: Report warning?
      }
    }
  } else {
    size_t i = 0;
    for (const auto &item : layer.primspecs()) {
      ss << prim::print_primspec(item.second, indent);
      if (i != (layer.primspecs().size() - 1)) {
        ss << "\n";
      }
    }
  }

  return ss.str();
}

// prim-pprint.hh
namespace prim {

std::string print_prim(const Prim &prim, const uint32_t indent) {
  std::stringstream ss;

  // Currently, Prim's elementName is read from name variable in concrete Prim
  // class(e.g. Xform::name).
  // TODO: use prim.elementPath for elementName.
  std::string s = pprint_value(prim.data(), indent, /* closing_brace */ false);

  bool require_newline = true;

  // Check last 2 chars.
  // if it ends with '{\n', no properties are authored so do not emit blank line
  // before printing VariantSet or child Prims.
  if (s.size() > 2) {
    if ((s[s.size() - 2] == '{') && (s[s.size() - 1] == '\n')) {
      require_newline = false;
    }
  }

  ss << s;

  //
  // print variant
  //
  if (prim.variantSets().size()) {
    if (require_newline) {
      ss << "\n";
    }

    // need to add blank line after VariantSet stmt and before child Prims,
    // so set require_newline true
    require_newline = true;

    for (const auto &variantSet : prim.variantSets()) {
      ss << pprint::Indent(indent + 1) << "variantSet "
         << quote(variantSet.first) << " = {\n";

      for (const auto &variantItem : variantSet.second.variantSet) {
        ss << pprint::Indent(indent + 2) << quote(variantItem.first);

        const Variant &variant = variantItem.second;

        if (variant.metas().authored()) {
          ss << " (\n";
          ss << print_prim_metas(variant.metas(), indent + 3);
          ss << pprint::Indent(indent + 2) << ")";
        }

        ss << " {\n";

        ss << print_props(variant.properties(), indent + 3);

        if (variant.metas().variantChildren.has_value() &&
            (variant.metas().variantChildren.value().size() ==
             variant.primChildren().size())) {
          std::map<std::string, const Prim *> primNameTable;
          for (size_t i = 0; i < variant.primChildren().size(); i++) {
            primNameTable.emplace(variant.primChildren()[i].element_name(),
                                  &variant.primChildren()[i]);
          }

          for (size_t i = 0; i < variant.metas().variantChildren.value().size();
               i++) {
            value::token nameTok = variant.metas().variantChildren.value()[i];
            DCOUT(fmt::format("variantPrimChildren  {}/{} = {}", i,
                              variant.metas().variantChildren.size(),
                              nameTok.str()));
            const auto it = primNameTable.find(nameTok.str());
            if (it != primNameTable.end()) {
              ss << print_prim(*(it->second), indent + 3);
              if (i != (variant.primChildren().size() - 1)) {
                ss << "\n";
              }
            } else {
              // TODO: Report warning?
            }
          }

        } else {
          for (size_t i = 0; i < variant.primChildren().size(); i++) {
            ss << print_prim(variant.primChildren()[i], indent + 3);
            if (i != (variant.primChildren().size() - 1)) {
              ss << "\n";
            }
          }
        }

        // ss << "# variantSet end\n";
        ss << pprint::Indent(indent + 2) << "}\n";
      }

      ss << pprint::Indent(indent + 1) << "}\n";
    }
  }

  //
  // primChildren
  //
  if (prim.children().size()) {
    if (require_newline) {
      ss << "\n";
      require_newline = false;
    }
    if (prim.metas().primChildren.size() == prim.children().size()) {
      // Use primChildren info to determine the order of the traversal.

      std::map<std::string, const Prim *> primNameTable;
      for (size_t i = 0; i < prim.children().size(); i++) {
        primNameTable.emplace(prim.children()[i].element_name(),
                              &prim.children()[i]);
      }

      for (size_t i = 0; i < prim.metas().primChildren.size(); i++) {
        if (i > 0) {
          ss << "\n";
        }
        value::token nameTok = prim.metas().primChildren[i];
        DCOUT(fmt::format("primChildren  {}/{} = {}", i,
                          prim.metas().primChildren.size(), nameTok.str()));
        const auto it = primNameTable.find(nameTok.str());
        if (it != primNameTable.end()) {
          ss << print_prim(*(it->second), indent + 1);
        } else {
          // TODO: Report warning?
        }
      }

    } else {
      for (size_t i = 0; i < prim.children().size(); i++) {
        if (i > 0) {
          ss << "\n";
        }
        ss << print_prim(prim.children()[i], indent + 1);
      }
    }
  }

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

std::string print_primspec(const PrimSpec &primspec, const uint32_t indent) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(primspec.specifier()) << " ";
  if (primspec.typeName().empty() || primspec.typeName() == "Model") {
    // do not emit typeName
  } else {
    ss << primspec.typeName() << " ";
  }

  ss << "\"" << primspec.name() << "\"\n";

  if (primspec.metas().authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(primspec.metas(), indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  ss << print_props(primspec.props(), indent + 1);

  // TODO: print according to primChildren metadatum
  for (size_t i = 0; i < primspec.children().size(); i++) {
    if (i > 0) {
      ss << pprint::Indent(indent) << "\n";
    }
    ss << print_primspec(primspec.children()[i], indent + 1);
  }

  // ss << "# variant \n";
  ss << print_variantSetSpecStmt(primspec.variantSets(), indent + 1);

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}
} // namespace prim
} // namespace tinyusdz
