// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment, Inc.
//
#include "scene-access.hh"

#include "common-macros.inc"
#include "pprinter.hh"
#include "prim-pprint.hh"
#include "prim-types.hh"
#include "primvar.hh"
#include "tiny-format.hh"
#include "tydra/prim-apply.hh"
#include "usdGeom.hh"
#include "usdLux.hh"
#include "usdShade.hh"
#include "usdSkel.hh"
#include "value-pprint.hh"

namespace tinyusdz {
namespace tydra {

namespace {

// For PUSH_ERROR_AND_RETURN
#define PushError(msg) \
  if (err) {           \
    (*err) += msg;     \
  }

// Typed TimeSamples to typeless TimeSamples
template <typename T>
value::TimeSamples ToTypelessTimeSamples(const TypedTimeSamples<T> &ts) {
  const std::vector<typename TypedTimeSamples<T>::Sample> &samples =
      ts.get_samples();

  value::TimeSamples dst;

  for (size_t i = 0; i < samples.size(); i++) {
    dst.add_sample(samples[i].t, samples[i].value);
  }

  return dst;
}

// Enum TimeSamples to typeless TimeSamples
template <typename T>
value::TimeSamples EnumTimeSamplesToTypelessTimeSamples(
    const TypedTimeSamples<T> &ts) {
  const std::vector<typename TypedTimeSamples<T>::Sample> &samples =
      ts.get_samples();

  value::TimeSamples dst;

  for (size_t i = 0; i < samples.size(); i++) {
    // to token
    value::token tok(to_string(samples[i].value));
    dst.add_sample(samples[i].t, tok);
  }

  return dst;
}

template <typename T>
bool TraverseRec(const std::string &path_prefix, const tinyusdz::Prim &prim,
                 uint32_t depth, PathPrimMap<T> &itemmap) {
  if (depth > 1024 * 128) {
    // Too deep
    return false;
  }

  std::string prim_abs_path =
      path_prefix + "/" + prim.local_path().full_path_name();

  if (prim.is<T>()) {
    if (const T *pv = prim.as<T>()) {
      std::cout << "Path : <" << prim_abs_path << "> is "
                << tinyusdz::value::TypeTraits<T>::type_name() << ".\n";
      itemmap[prim_abs_path] = pv;
    }
  }

  for (const auto &child : prim.children()) {
    if (!TraverseRec(prim_abs_path, child, depth + 1, itemmap)) {
      return false;
    }
  }

  return true;
}

// Specialization for Shader type.
template <typename ShaderTy>
bool TraverseShaderRec(const std::string &path_prefix,
                       const tinyusdz::Prim &prim, uint32_t depth,
                       PathShaderMap<ShaderTy> &itemmap) {
  if (depth > 1024 * 128) {
    // Too deep
    return false;
  }

  std::string prim_abs_path =
      path_prefix + "/" + prim.local_path().full_path_name();

  // First check if type is Shader Prim.
  if (const Shader *ps = prim.as<Shader>()) {
    // Then check if wanted Shder type
    if (const ShaderTy *s = ps->value.as<ShaderTy>()) {
      itemmap[prim_abs_path] = std::make_pair(ps, s);
    }
  }

  for (const auto &child : prim.children()) {
    if (!TraverseShaderRec(prim_abs_path, child, depth + 1, itemmap)) {
      return false;
    }
  }

  return true;
}

bool ListSceneNamesRec(const tinyusdz::Prim &root, uint32_t depth,
                       std::vector<std::pair<bool, std::string>> *sceneNames) {
  if (!sceneNames) {
    return false;
  }

  if (depth > 1024 * 128) {
    // Too deep
    return false;
  }

  if (root.metas().sceneName.has_value()) {
    bool is_over = (root.specifier() == Specifier::Over);

    sceneNames->push_back(
        std::make_pair(is_over, root.metas().sceneName.value()));
  }

  return true;
}

}  // namespace

template <typename T>
bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<T> &m /* output */) {
  // Should report error at compilation stege.
  static_assert(
      (value::TypeId::TYPE_ID_MODEL_BEGIN <= value::TypeTraits<T>::type_id()) &&
          (value::TypeId::TYPE_ID_MODEL_END > value::TypeTraits<T>::type_id()),
      "Not a Prim type.");

  // Check at runtime. Just in case...
  if ((value::TypeId::TYPE_ID_MODEL_BEGIN <= value::TypeTraits<T>::type_id()) &&
      (value::TypeId::TYPE_ID_MODEL_END > value::TypeTraits<T>::type_id())) {
    // Ok
  } else {
    return false;
  }

  for (const auto &root_prim : stage.root_prims()) {
    TraverseRec(/* root path is empty */ "", root_prim, /* depth */ 0, m);
  }

  return true;
}

template <typename T>
bool ListShaders(const tinyusdz::Stage &stage,
                 PathShaderMap<T> &m /* output */) {
  // Concrete Shader type(e.g. UsdPreviewSurface) is classified as Imaging
  // Should report error at compilation stege.
  static_assert(
      (value::TypeId::TYPE_ID_IMAGING_BEGIN <= value::TypeTraits<T>::type_id()) &&
          (value::TypeId::TYPE_ID_IMAGING_END > value::TypeTraits<T>::type_id()),
      "Not a Shader type.");

  // Check at runtime. Just in case...
  if ((value::TypeId::TYPE_ID_IMAGING_BEGIN <= value::TypeTraits<T>::type_id()) &&
      (value::TypeId::TYPE_ID_IMAGING_END > value::TypeTraits<T>::type_id())) {
    // Ok
  } else {
    return false;
  }

  for (const auto &root_prim : stage.root_prims()) {
    TraverseShaderRec(/* root path is empty */ "", root_prim, /* depth */ 0, m);
  }

  return true;
}

const Prim *GetParentPrim(const tinyusdz::Stage &stage,
                          const tinyusdz::Path &path, std::string *err) {
  if (!path.is_valid()) {
    if (err) {
      (*err) = "Input Path " + tinyusdz::to_string(path) + " is invalid.\n";
    }
    return nullptr;
  }

  if (path.is_root_path()) {
    if (err) {
      (*err) = "Input Path is root(\"/\").\n";
    }
    return nullptr;
  }

  if (path.is_root_prim()) {
    if (err) {
      (*err) = "Input Path is root Prim, so no parent Prim exists.\n";
    }
    return nullptr;
  }

  if (!path.is_absolute_path()) {
    if (err) {
      (*err) = "Input Path must be absolute path(i.e. starts with \"/\").\n";
    }
    return nullptr;
  }

  tinyusdz::Path parentPath = path.get_parent_prim_path();

  nonstd::expected<const Prim *, std::string> ret =
      stage.GetPrimAtPath(parentPath);
  if (ret) {
    return ret.value();
  } else {
    if (err) {
      (*err) += "Failed to get parent Prim from Path " +
                tinyusdz::to_string(path) + ". Reason = " + ret.error() + "\n";
    }
    return nullptr;
  }
}

//
// Template Instanciations
//
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<Model> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<Scope> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<GPrim> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<Xform> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<GeomMesh> &m);
template bool ListPrims(const tinyusdz::Stage &stage,
                        PathPrimMap<GeomBasisCurves> &m);
template bool ListPrims(const tinyusdz::Stage &stage,
                        PathPrimMap<GeomSphere> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<GeomCone> &m);
template bool ListPrims(const tinyusdz::Stage &stage,
                        PathPrimMap<GeomCylinder> &m);
template bool ListPrims(const tinyusdz::Stage &stage,
                        PathPrimMap<GeomCapsule> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<GeomCube> &m);
template bool ListPrims(const tinyusdz::Stage &stage,
                        PathPrimMap<GeomPoints> &m);
template bool ListPrims(const tinyusdz::Stage &stage,
                        PathPrimMap<GeomSubset> &m);
template bool ListPrims(const tinyusdz::Stage &stage,
                        PathPrimMap<GeomCamera> &m);

template bool ListPrims(const tinyusdz::Stage &stage,
                        PathPrimMap<DomeLight> &m);
template bool ListPrims(const tinyusdz::Stage &stage,
                        PathPrimMap<CylinderLight> &m);
template bool ListPrims(const tinyusdz::Stage &stage,
                        PathPrimMap<SphereLight> &m);
template bool ListPrims(const tinyusdz::Stage &stage,
                        PathPrimMap<DiskLight> &m);
template bool ListPrims(const tinyusdz::Stage &stage,
                        PathPrimMap<DistantLight> &m);
template bool ListPrims(const tinyusdz::Stage &stage,
                        PathPrimMap<RectLight> &m);
template bool ListPrims(const tinyusdz::Stage &stage,
                        PathPrimMap<GeometryLight> &m);
template bool ListPrims(const tinyusdz::Stage &stage,
                        PathPrimMap<PortalLight> &m);
template bool ListPrims(const tinyusdz::Stage &stage,
                        PathPrimMap<PluginLight> &m);

template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<Material> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<Shader> &m);

template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<SkelRoot> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<Skeleton> &m);
template bool ListPrims(const tinyusdz::Stage &stage,
                        PathPrimMap<SkelAnimation> &m);
template bool ListPrims(const tinyusdz::Stage &stage,
                        PathPrimMap<BlendShape> &m);

template bool ListShaders(const tinyusdz::Stage &stage,
                          PathShaderMap<UsdPreviewSurface> &m);
template bool ListShaders(const tinyusdz::Stage &stage,
                          PathShaderMap<UsdUVTexture> &m);

template bool ListShaders(const tinyusdz::Stage &stage,
                          PathShaderMap<UsdPrimvarReader_int> &m);
template bool ListShaders(const tinyusdz::Stage &stage,
                          PathShaderMap<UsdPrimvarReader_float> &m);
template bool ListShaders(const tinyusdz::Stage &stage,
                          PathShaderMap<UsdPrimvarReader_float2> &m);
template bool ListShaders(const tinyusdz::Stage &stage,
                          PathShaderMap<UsdPrimvarReader_float3> &m);
template bool ListShaders(const tinyusdz::Stage &stage,
                          PathShaderMap<UsdPrimvarReader_float4> &m);

namespace {

bool VisitPrimsRec(const tinyusdz::Prim &root, int32_t level,
                   VisitPrimFunction visitor_fun, void *userdata) {
  bool ret = visitor_fun(root, level, userdata);
  if (!ret) {
    return false;
  }

  for (const auto &child : root.children()) {
    if (!VisitPrimsRec(child, level + 1, visitor_fun, userdata)) {
      return false;
    }
  }

  return true;
}

#if 0
// Scalar-valued attribute.
// TypedAttribute* => Attribute defined in USD schema, so not a custom attr.
template<typename T>
void ToProperty(
  const TypedAttributeWithFallback<T> &input,
  Property &output)
{
  if (input.IsBlocked()) {
    Attribute attr;
    attr.set_blocked(input.IsBlocked());
    attr.variability() = Variability::Uniform;
    output = Property(std::move(attr), /*custom*/ false);
  } else if (input.IsValueEmpty()) {
    // type info only
    output = Property(value::TypeTraits<T>::type_name(), /* custom */false);
  } else if (input.IsConnection()) {

    // Use Relation for Connection(as typed relationshipTarget)
    // Single connection targetPath only.
    Relation relTarget;
    if (auto pv = input.GetConnection()) {
      relTarget.targetPath = pv.value();
    } else {
      // ??? TODO: report internal error.
    }
    output = Property(relTarget, /* type */value::TypeTraits<T>::type_name(), /* custom */false);

  } else {
    // Includes !authored()
    value::Value val(input.GetValue());
    primvar::PrimVar pvar;
    pvar.set_value(val);
    Attribute attr;
    attr.set_var(std::move(pvar));
    attr.variability() = Variability::Uniform;
    output = Property(attr, /* custom */false);
  }
}
#endif

// Scalar-valued attribute.
// TypedAttribute* => Attribute defined in USD schema, so not a custom attr.
template <typename T>
void ToProperty(const TypedAttribute<T> &input, Property &output) {
  if (input.is_blocked()) {
    Attribute attr;
    attr.set_blocked(input.is_blocked());
    attr.variability() = Variability::Uniform;
    attr.set_type_name(value::TypeTraits<T>::type_name());
    output = Property(std::move(attr), /*custom*/ false);
  } else if (input.is_value_empty()) {
    // type info only
    output = Property(value::TypeTraits<T>::type_name(), /* custom */ false);
  } else if (input.is_connection()) {
    // Use Relation for Connection(as typed relationshipTarget)
    // Single connection targetPath only.
    Relationship relTarget;
    std::vector<Path> paths = input.get_connections();
    if (paths.empty()) {
      // ??? TODO: report internal error.
    } else if (paths.size() == 1) {
      output = Property(paths[0], /* type */ value::TypeTraits<T>::type_name(),
                        /* custom */ false);
    } else {
      output = Property(paths, /* type */ value::TypeTraits<T>::type_name(),
                        /* custom */ false);
    }

  } else {
    // Includes !authored()
    if (auto pv = input.get_value()) {
      value::Value val(pv.value());
      primvar::PrimVar pvar;
      pvar.set_value(val);
      Attribute attr;
      attr.set_var(std::move(pvar));
      attr.variability() = Variability::Uniform;
      output = Property(attr, /* custom */ false);
    } else {
      // TODO: Report error.
    }
  }
}

// Scalar or TimeSample-valued attribute.
// TypedAttribute* => Attribute defined in USD schema, so not a custom attr.
//
// TODO: Support timeSampled attribute.
template <typename T>
void ToProperty(const TypedAttribute<Animatable<T>> &input, Property &output) {
  if (input.is_blocked()) {
    Attribute attr;
    attr.set_blocked(input.is_blocked());
    attr.variability() = Variability::Uniform;
    attr.set_type_name(value::TypeTraits<T>::type_name());
    output = Property(std::move(attr), /*custom*/ false);
    return;
  } else if (input.is_value_empty()) {
    // type info only
    output = Property(value::TypeTraits<T>::type_name(), /* custom */ false);
    return;
  } else if (input.is_connection()) {
    DCOUT("IsConnection");

    // Use Relation for Connection(as typed relationshipTarget)
    // Single connection targetPath only.
    std::vector<Path> pv = input.get_connections();
    if (pv.empty()) {
      DCOUT("??? Empty connectionTarget.");
    }
    if (pv.size() == 1) {
      DCOUT("targetPath = " << pv[0]);
      output = Property(pv[0], /* type */ value::TypeTraits<T>::type_name(),
                        /* custom */ false);
    } else if (pv.size() > 1) {
      output = Property(pv, /* type */ value::TypeTraits<T>::type_name(),
                        /* custom */ false);
    } else {
      // ??? TODO: report internal error.
      DCOUT("??? GetConnection faile.");
    }

    return;

  } else {
    // Includes !authored()
    // FIXME: Currently scalar only.
    nonstd::optional<Animatable<T>> aval = input.get_value();
    if (aval) {
      if (aval.value().is_scalar()) {
        T a;
        if (aval.value().get_scalar(&a)) {
          value::Value val(a);
          primvar::PrimVar pvar;
          pvar.set_value(val);
          Attribute attr;
          attr.set_var(std::move(pvar));
          attr.variability() = Variability::Uniform;
          output = Property(attr, /* custom */ false);
          return;
        }
      } else if (aval.value().is_blocked()) {
        Attribute attr;
        attr.set_type_name(value::TypeTraits<T>::type_name());
        attr.set_blocked(true);
        attr.variability() = Variability::Uniform;
        output = Property(std::move(attr), /*custom*/ false);
        return;
      } else if (aval.value().is_timesamples()) {
        DCOUT("TODO: Convert typed TimeSamples to value::TimeSamples");
      }
    }
  }

  // fallback to Property with invalid value
  output = Property(value::TypeTraits<std::nullptr_t>::type_name(),
                    /*custom*/ false);
}

// Scalar or TimeSample-valued attribute.
// TypedAttribute* => Attribute defined in USD schema, so not a custom attr.
//
// TODO: Support timeSampled attribute.
template <typename T>
void ToProperty(const TypedAttributeWithFallback<Animatable<T>> &input,
                Property &output) {
  if (input.is_blocked()) {
    Attribute attr;
    attr.set_blocked(input.is_blocked());
    attr.variability() = Variability::Uniform;
    attr.set_type_name(value::TypeTraits<T>::type_name());
    output = Property(std::move(attr), /*custom*/ false);
  } else if (input.is_value_empty()) {
    // type info only
    output = Property(value::TypeTraits<T>::type_name(), /* custom */ false);
  } else if (input.is_connection()) {
    // Use Relation for Connection(as typed relationshipTarget)
    // Single connection targetPath only.
    Relationship rel;
    std::vector<Path> pv = input.get_connections();
    if (pv.empty()) {
      DCOUT("??? Empty connectionTarget.");
    }
    if (pv.size() == 1) {
      DCOUT("targetPath = " << pv[0]);
      output = Property(pv[0], /* type */ value::TypeTraits<T>::type_name(),
                        /* custom */ false);
    } else if (pv.size() > 1) {
      output = Property(pv, /* type */ value::TypeTraits<T>::type_name(),
                        /* custom */ false);
    } else {
      // ??? TODO: report internal error.
      DCOUT("??? GetConnection faile.");
    }

  } else {
    // Includes !authored()
    // FIXME: Currently scalar only.
    Animatable<T> v = input.get_value();

    primvar::PrimVar pvar;

    if (v.is_timesamples()) {
      value::TimeSamples ts = ToTypelessTimeSamples(v.get_timesamples());
      pvar.set_timesamples(ts);
    } else if (v.is_scalar()) {
      T a;
      if (v.get_scalar(&a)) {
        value::Value val(a);
        pvar.set_value(val);
      } else {
        DCOUT("??? Invalid Animatable value.");
      }
    } else {
      DCOUT("??? Invalid Animatable value.");
    }

    Attribute attr;
    attr.set_var(std::move(pvar));
    attr.variability() = Variability::Varying;
    output = Property(attr, /* custom */ false);
  }
}

// To Property with token type
template <typename T>
void ToTokenProperty(const TypedAttributeWithFallback<Animatable<T>> &input,
                     Property &output) {
  if (input.is_blocked()) {
    Attribute attr;
    attr.set_blocked(input.is_blocked());
    attr.variability() = Variability::Uniform;
    attr.set_type_name(value::kToken);
    output = Property(std::move(attr), /*custom*/ false);
  } else if (input.is_value_empty()) {
    // type info only
    output = Property(value::kToken, /* custom */ false);
  } else if (input.is_connection()) {
    // Use Relation for Connection(as typed relationshipTarget)
    // Single connection targetPath only.
    Relationship rel;
    std::vector<Path> pv = input.get_connections();
    if (pv.empty()) {
      DCOUT("??? Empty connectionTarget.");
    }
    if (pv.size() == 1) {
      output = Property(pv[0], /* type */ value::kToken, /* custom */ false);
    } else if (pv.size() > 1) {
      output = Property(pv, /* type */ value::kToken, /* custom */ false);
    } else {
      // ??? TODO: report internal error.
      DCOUT("??? GetConnection faile.");
    }

  } else {
    // Includes !authored()
    // FIXME: Currently scalar only.
    Animatable<T> v = input.get_value();

    primvar::PrimVar pvar;

    if (v.is_timesamples()) {
      value::TimeSamples ts =
          EnumTimeSamplesToTypelessTimeSamples(v.get_timesamples());
      pvar.set_timesamples(ts);
    } else if (v.is_scalar()) {
      T a;
      if (v.get_scalar(&a)) {
        // to token type
        value::token tok(to_string(a));
        value::Value val(tok);
        pvar.set_value(val);
      } else {
        DCOUT("??? Invalid Animatable value.");
      }
    } else {
      DCOUT("??? Invalid Animatable value.");
    }

    Attribute attr;
    attr.set_var(std::move(pvar));
    attr.variability() = Variability::Varying;
    output = Property(attr, /* custom */ false);
  }
}

// To Property with token type
template <typename T>
void ToTokenProperty(const TypedAttributeWithFallback<T> &input,
                     Property &output) {
  if (input.is_blocked()) {
    Attribute attr;
    attr.set_blocked(input.is_blocked());
    attr.variability() = Variability::Uniform;
    attr.set_type_name(value::kToken);
    output = Property(std::move(attr), /*custom*/ false);
  } else if (input.is_value_empty()) {
    // type info only
    output = Property(value::kToken, /* custom */ false);
  } else if (input.is_connection()) {
    // Use Relation for Connection(as typed relationshipTarget)
    // Single connection targetPath only.
    Relationship rel;
    std::vector<Path> pv = input.get_connections();
    if (pv.empty()) {
      DCOUT("??? Empty connectionTarget.");
    }
    if (pv.size() == 1) {
      output = Property(pv[0], /* type */ value::kToken, /* custom */ false);
    } else if (pv.size() > 1) {
      output = Property(pv, /* type */ value::kToken, /* custom */ false);
    } else {
      // ??? TODO: report internal error.
      DCOUT("??? GetConnection faile.");
    }

  } else {
    // Includes !authored()
    // FIXME: Currently scalar only.
    Animatable<T> v = input.get_value();

    primvar::PrimVar pvar;

    if (v.is_scalar()) {
      T a;
      if (v.get_scalar(&a)) {
        // to token type
        value::token tok(to_string(a));
        value::Value val(tok);
        pvar.set_value(val);
      } else {
        DCOUT("??? Invalid value.");
      }
    } else {
      DCOUT("??? Invalid value.");
    }

    Attribute attr;
    attr.set_var(std::move(pvar));
    attr.variability() = Variability::Uniform;
    output = Property(attr, /* custom */ false);
  }
}

bool ToTerminalAttributeValue(
    const Attribute &attr, TerminalAttributeValue *value, std::string *err,
    const double t, const value::TimeSampleInterpolationType tinterp) {
  if (!value) {
    // ???
    return false;
  }

  if (attr.is_blocked()) {
    PUSH_ERROR_AND_RETURN("Attribute is None(Value Blocked).");
  }

  const primvar::PrimVar &var = attr.get_var();

  value->meta() = attr.metas();
  value->variability() = attr.variability();

  if (!var.is_valid()) {
    PUSH_ERROR_AND_RETURN("[InternalError] Attribute is invalid.");
  } else if (var.is_scalar()) {
    const value::Value &v = var.value_raw();
    DCOUT("Attribute is scalar type:" << v.type_name());
    DCOUT("Attribute value = " << pprint_value(v));

    value->set_value(v);
  } else if (var.is_timesamples()) {
    value::Value v;
    if (!var.get_interpolated_value(t, tinterp, &v)) {
      PUSH_ERROR_AND_RETURN("Interpolate TimeSamples failed.");
      return false;
    }

    value->set_value(v);
  }

  return true;
}

bool XformOpToProperty(const XformOp &x, Property &prop) {
  primvar::PrimVar pv;

  Attribute attr;

  switch (x.op_type) {
    case XformOp::OpType::ResetXformStack: {
      // ??? Not exists in Prim's property
      return false;
    }
    case XformOp::OpType::Transform:
    case XformOp::OpType::Scale:
    case XformOp::OpType::Translate:
    case XformOp::OpType::RotateX:
    case XformOp::OpType::RotateY:
    case XformOp::OpType::RotateZ:
    case XformOp::OpType::Orient:
    case XformOp::OpType::RotateXYZ:
    case XformOp::OpType::RotateXZY:
    case XformOp::OpType::RotateYXZ:
    case XformOp::OpType::RotateYZX:
    case XformOp::OpType::RotateZXY:
    case XformOp::OpType::RotateZYX: {
      pv = x.get_var();
    }
  }

  attr.set_var(std::move(pv));
  // TODO: attribute meta

  prop = Property(attr, /* custom */ false);

  return true;
}

// Return true: Property found(`out_prop` filled)
// Return false: Property not found
// Return unexpected: Some eror happened.
template <typename T>
nonstd::expected<bool, std::string> GetPrimProperty(
    const T &prim, const std::string &prop_name, Property *out_prop);

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const Model &model, const std::string &prop_name, Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  const auto it = model.props.find(prop_name);
  if (it == model.props.end()) {
    // Attribute not found.
    return false;
  }

  (*out_prop) = it->second;

  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const Scope &scope, const std::string &prop_name, Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  const auto it = scope.props.find(prop_name);
  if (it == scope.props.end()) {
    // Attribute not found.
    return false;
  }

  (*out_prop) = it->second;

  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const Xform &xform, const std::string &prop_name, Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  if (prop_name == "xformOpOrder") {
    // To token[]
    std::vector<value::token> toks = xform.xformOpOrder();
    value::Value val(toks);
    primvar::PrimVar pvar;
    pvar.set_value(toks);

    Attribute attr;
    attr.set_var(std::move(pvar));
    attr.variability() = Variability::Uniform;
    Property prop;
    prop.set_attribute(attr);

    (*out_prop) = prop;

  } else {
    // XformOp?
    for (const auto &item : xform.xformOps) {
      std::string op_name = to_string(item.op_type);
      if (item.suffix.size()) {
        op_name += ":" + item.suffix;
      }

      if (op_name == prop_name) {
        return XformOpToProperty(item, *out_prop);
      }
    }

    const auto it = xform.props.find(prop_name);
    if (it == xform.props.end()) {
      // Attribute not found.
      return false;
    }

    (*out_prop) = it->second;
  }

  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const GeomMesh &mesh, const std::string &prop_name, Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  DCOUT("prop_name = " << prop_name);
  if (prop_name == "points") {
    ToProperty(mesh.points, *out_prop);
  } else if (prop_name == "faceVertexCounts") {
    ToProperty(mesh.faceVertexCounts, *out_prop);
  } else if (prop_name == "faceVertexIndices") {
    ToProperty(mesh.faceVertexIndices, *out_prop);
  } else if (prop_name == "normals") {
    ToProperty(mesh.normals, *out_prop);
  } else if (prop_name == "velocities") {
    ToProperty(mesh.velocities, *out_prop);
  } else if (prop_name == "cornerIndices") {
    ToProperty(mesh.cornerIndices, *out_prop);
  } else if (prop_name == "cornerSharpnesses") {
    ToProperty(mesh.cornerSharpnesses, *out_prop);
  } else if (prop_name == "creaseIndices") {
    ToProperty(mesh.creaseIndices, *out_prop);
  } else if (prop_name == "creaseSharpnesses") {
    ToProperty(mesh.creaseSharpnesses, *out_prop);
  } else if (prop_name == "holeIndices") {
    ToProperty(mesh.holeIndices, *out_prop);
  } else if (prop_name == "interpolateBoundary") {
    ToTokenProperty(mesh.interpolateBoundary, *out_prop);
  } else if (prop_name == "subdivisionScheme") {
    ToTokenProperty(mesh.subdivisionScheme, *out_prop);
  } else if (prop_name == "faceVaryingLinearInterpolation") {
    ToTokenProperty(mesh.faceVaryingLinearInterpolation, *out_prop);
  } else if (prop_name == "skeleton") {
    if (mesh.skeleton) {
      const Relationship &rel = mesh.skeleton.value();
      (*out_prop) = Property(rel, /* custom */ false);
    } else {
      // empty
      return false;
    }
  } else {
    const auto it = mesh.props.find(prop_name);
    if (it == mesh.props.end()) {
      // Attribute not found.
      return false;
    }

    (*out_prop) = it->second;
  }

  DCOUT("Prop found: " << prop_name
                       << ", ty = " << out_prop->value_type_name());
  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const GeomSubset &subset, const std::string &prop_name,
    Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  // Currently GeomSubset does not support TimeSamples and AttributeMeta

  DCOUT("prop_name = " << prop_name);
  if (prop_name == "indices") {
    primvar::PrimVar var;
    var.set_value(subset.indices);
    Attribute attr;
    attr.set_var(std::move(var));
    attr.variability() = Variability::Uniform;
    (*out_prop) = Property(attr, /* custom */ false);
  } else if (prop_name == "elementType") {
    value::token tok(to_string(subset.elementType));
    primvar::PrimVar var;
    var.set_value(tok);
    Attribute attr;
    attr.set_var(std::move(var));
    attr.variability() = Variability::Uniform;
    (*out_prop) = Property(attr, /* custom */ false);
  } else if (prop_name == "familyType") {
    value::token tok(to_string(subset.familyType));
    primvar::PrimVar var;
    var.set_value(tok);
    Attribute attr;
    attr.set_var(std::move(var));
    attr.variability() = Variability::Uniform;
    (*out_prop) = Property(attr, /* custom */ false);
  } else if (prop_name == "familyName") {
    if (subset.familyName) {
      value::token tok(subset.familyName.value());
      primvar::PrimVar var;
      var.set_value(tok);
      Attribute attr;
      attr.set_var(std::move(var));
      attr.variability() = Variability::Uniform;
      (*out_prop) = Property(attr, /* custom */ false);
    } else {
      return false;
    }
  } else {
    const auto it = subset.props.find(prop_name);
    if (it == subset.props.end()) {
      // Attribute not found.
      return false;
    }

    (*out_prop) = it->second;
  }

  DCOUT("Prop found: " << prop_name
                       << ", ty = " << out_prop->value_type_name());
  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const UsdUVTexture &tex, const std::string &prop_name, Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  DCOUT("prop_name = " << prop_name);
  if (prop_name == "inputs:file") {
    ToProperty(tex.file, (*out_prop));

  } else {
    const auto it = tex.props.find(prop_name);
    if (it == tex.props.end()) {
      // Attribute not found.
      return false;
    }

    (*out_prop) = it->second;
  }

  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const UsdPrimvarReader_float2 &preader, const std::string &prop_name,
    Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  DCOUT("prop_name = " << prop_name);
  if (prop_name == "inputs:varname") {
    ToProperty(preader.varname, (*out_prop));

  } else {
    const auto it = preader.props.find(prop_name);
    if (it == preader.props.end()) {
      // Attribute not found.
      return false;
    }

    (*out_prop) = it->second;
  }

  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const UsdPrimvarReader_float &preader, const std::string &prop_name,
    Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  DCOUT("prop_name = " << prop_name);
  if (prop_name == "inputs:varname") {
    ToProperty(preader.varname, (*out_prop));

  } else {
    const auto it = preader.props.find(prop_name);
    if (it == preader.props.end()) {
      // Attribute not found.
      return false;
    }

    (*out_prop) = it->second;
  }

  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const UsdPreviewSurface &surface, const std::string &prop_name,
    Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  DCOUT("prop_name = " << prop_name);
  if (prop_name == "diffuseColor") {
    ToProperty(surface.diffuseColor, *out_prop);
  } else if (prop_name == "emissiveColor") {
    ToProperty(surface.emissiveColor, *out_prop);
  } else if (prop_name == "specularColor") {
    ToProperty(surface.specularColor, *out_prop);
  } else if (prop_name == "useSpecularWorkflow") {
    ToProperty(surface.useSpecularWorkflow, *out_prop);
  } else if (prop_name == "metallic") {
    ToProperty(surface.metallic, *out_prop);
  } else if (prop_name == "clearcoat") {
    ToProperty(surface.clearcoat, *out_prop);
  } else if (prop_name == "clearcoatRoughness") {
    ToProperty(surface.clearcoatRoughness, *out_prop);
  } else if (prop_name == "roughness") {
    ToProperty(surface.roughness, *out_prop);
  } else if (prop_name == "opacity") {
    ToProperty(surface.opacity, *out_prop);
  } else if (prop_name == "opacityThreshold") {
    ToProperty(surface.opacityThreshold, *out_prop);
  } else if (prop_name == "ior") {
    ToProperty(surface.ior, *out_prop);
  } else if (prop_name == "normal") {
    ToProperty(surface.normal, *out_prop);
  } else if (prop_name == "displacement") {
    ToProperty(surface.displacement, *out_prop);
  } else if (prop_name == "occlusion") {
    ToProperty(surface.occlusion, *out_prop);
  } else if (prop_name == "outputsSurface") {
    if (surface.outputsSurface) {
      const Relationship &rel = surface.outputsSurface.value();
      if (!rel.has_value()) {
        // empty. type info only
        (*out_prop) = Property(value::kToken, /* custom */ false);
      } else if (rel.is_path()) {
        (*out_prop) =
            Property(rel.targetPath, value::kToken, /* custom */ false);
      } else if (rel.is_pathvector()) {
        (*out_prop) =
            Property(rel.targetPathVector, value::kToken, /* custom */ false);
      } else {
        return false;
      }
    } else {
      // Not authored
      return false;
    }
  } else if (prop_name == "outputsDisplacement") {
    if (surface.outputsDisplacement) {
      const Relationship &rel = surface.outputsDisplacement.value();
      if (!rel.has_value()) {
        // empty. type info only
        (*out_prop) = Property(value::kToken, /* custom */ false);
      } else if (rel.is_path()) {
        (*out_prop) =
            Property(rel.targetPath, value::kToken, /* custom */ false);
      } else if (rel.is_pathvector()) {
        (*out_prop) =
            Property(rel.targetPathVector, value::kToken, /* custom */ false);
      } else {
        return false;
      }
    } else {
      // Not authored
      return false;
    }
  } else {
    const auto it = surface.props.find(prop_name);
    if (it == surface.props.end()) {
      // Attribute not found.
      return false;
    }

    (*out_prop) = it->second;
  }

  DCOUT("Prop found: " << prop_name
                       << ", ty = " << out_prop->value_type_name());
  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const Material &material, const std::string &prop_name,
    Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  DCOUT("prop_name = " << prop_name);
  if (prop_name == "outputs:surface") {
    if (material.surface) {
      Connection<Path> conn = material.surface.value();
      if (conn.target) {
        (*out_prop) =
            Property(conn.target.value(), conn.type_name(), /* custom */ false);
      } else {
        // empty. type info only
        (*out_prop) = Property(conn.type_name(), /* custom */ false);
      }
    } else {
      // Not authored
      return false;
    }
  } else if (prop_name == "outputs:volume") {
    if (material.volume) {
      Connection<Path> conn = material.volume.value();
      if (conn.target) {
        (*out_prop) =
            Property(conn.target.value(), conn.type_name(), /* custom */ false);
      } else {
        // empty. type info only
        (*out_prop) = Property(conn.type_name(), /* custom */ false);
      }
    } else {
      // Not authored
      return false;
    }
  } else {
    const auto it = material.props.find(prop_name);
    if (it == material.props.end()) {
      // Attribute not found.
      return false;
    }

    (*out_prop) = it->second;
  }

  DCOUT("Prop found: " << prop_name
                       << ", ty = " << out_prop->value_type_name());
  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const SkelRoot &skelroot, const std::string &prop_name,
    Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  DCOUT("prop_name = " << prop_name);
  {
    const auto it = skelroot.props.find(prop_name);
    if (it == skelroot.props.end()) {
      // Attribute not found.
      return false;
    }

    (*out_prop) = it->second;
  }
  DCOUT("Prop found: " << prop_name
                       << ", ty = " << out_prop->value_type_name());

  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const BlendShape &blendshape, const std::string &prop_name,
    Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  DCOUT("prop_name = " << prop_name);
  if (prop_name == "offsets") {
    ToProperty(blendshape.offsets, (*out_prop));
  } else if (prop_name == "normalOffsets") {
    ToProperty(blendshape.normalOffsets, (*out_prop));
  } else if (prop_name == "pointIndices") {
    ToProperty(blendshape.pointIndices, (*out_prop));
  } else {
    const auto it = blendshape.props.find(prop_name);
    if (it == blendshape.props.end()) {
      // Attribute not found.
      return false;
    }

    (*out_prop) = it->second;
  }
  DCOUT("Prop found: " << prop_name
                       << ", ty = " << out_prop->value_type_name());
  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const Skeleton &skel, const std::string &prop_name, Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  DCOUT("prop_name = " << prop_name);
  if (prop_name == "bindTransforms") {
    ToProperty(skel.bindTransforms, (*out_prop));
  } else if (prop_name == "jointNames") {
    ToProperty(skel.jointNames, (*out_prop));
  } else if (prop_name == "joints") {
    ToProperty(skel.joints, (*out_prop));
  } else if (prop_name == "restTransforms") {
    ToProperty(skel.restTransforms, (*out_prop));
  } else if (prop_name == "animationSource") {
    if (skel.animationSource) {
      const Relationship &rel = skel.animationSource.value();
      (*out_prop) = Property(rel, /* custom */ false);
    } else {
      // empty
      return false;
    }
  } else {
    const auto it = skel.props.find(prop_name);
    if (it == skel.props.end()) {
      // Attribute not found.
      return false;
    }

    (*out_prop) = it->second;
  }
  DCOUT("Prop found: " << prop_name
                       << ", ty = " << out_prop->value_type_name());
  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const SkelAnimation &anim, const std::string &prop_name,
    Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  DCOUT("prop_name = " << prop_name);
  if (prop_name == "blendShapes") {
    ToProperty(anim.blendShapes, (*out_prop));
  } else if (prop_name == "blendShapeWeights") {
    ToProperty(anim.blendShapeWeights, (*out_prop));
  } else if (prop_name == "joints") {
    ToProperty(anim.joints, (*out_prop));
  } else if (prop_name == "rotations") {
    ToProperty(anim.rotations, (*out_prop));
  } else if (prop_name == "scales") {
    ToProperty(anim.scales, (*out_prop));
  } else if (prop_name == "translations") {
    ToProperty(anim.translations, (*out_prop));
  } else {
    const auto it = anim.props.find(prop_name);
    if (it == anim.props.end()) {
      // Attribute not found.
      return false;
    }

    (*out_prop) = it->second;
  }
  DCOUT("Prop found: " << prop_name
                       << ", ty = " << out_prop->value_type_name());
  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const Shader &shader, const std::string &prop_name, Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  if (const auto preader_f = shader.value.as<UsdPrimvarReader_float>()) {
    return GetPrimProperty(*preader_f, prop_name, out_prop);
  } else if (const auto preader_f2 =
                 shader.value.as<UsdPrimvarReader_float2>()) {
    return GetPrimProperty(*preader_f2, prop_name, out_prop);
  } else if (const auto ptex = shader.value.as<UsdUVTexture>()) {
    return GetPrimProperty(*ptex, prop_name, out_prop);
  } else if (const auto psurf = shader.value.as<UsdPreviewSurface>()) {
    return GetPrimProperty(*psurf, prop_name, out_prop);
  } else {
    return nonstd::make_unexpected("TODO: " + shader.value.type_name());
  }
}

// TODO: provide visit map to prevent circular referencing.
bool EvaluateAttributeImpl(
    const tinyusdz::Stage &stage, const tinyusdz::Prim &prim,
    const std::string &attr_name, TerminalAttributeValue *value,
    std::string *err, std::set<std::string> &visited_paths, const double t,
    const tinyusdz::value::TimeSampleInterpolationType tinterp) {
  // TODO:
  (void)tinterp;

  DCOUT("Prim : " << prim.element_path().element_name() << "("
                  << prim.type_name() << ") attr_name " << attr_name);

  Property prop;
  if (!GetProperty(prim, attr_name, &prop, err)) {
    return false;
  }

  if (prop.is_connection()) {
    // Follow connection target Path(singple targetPath only).
    std::vector<Path> pv = prop.get_relationTargets();
    if (pv.empty()) {
      PUSH_ERROR_AND_RETURN(fmt::format("Failed to get connection target"));
    }

    if (pv.size() > 1) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Multiple targetPaths assigned to .connection."));
    }

    auto target = pv[0];

    std::string targetPrimPath = target.prim_part();
    std::string targetPrimPropName = target.prop_part();
    DCOUT("connection targetPath : " << target << "(Prim: " << targetPrimPath
                                     << ", Prop: " << targetPrimPropName
                                     << ")");

    auto targetPrimRet =
        stage.GetPrimAtPath(Path(targetPrimPath, /* prop */ ""));
    if (targetPrimRet) {
      // Follow the connetion
      const Prim *targetPrim = targetPrimRet.value();

      std::string abs_path = target.full_path_name();

      if (visited_paths.count(abs_path)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Circular referencing detected. connectionTargetPath = {}",
            to_string(target)));
      }
      visited_paths.insert(abs_path);

      return EvaluateAttributeImpl(stage, *targetPrim, targetPrimPropName,
                                   value, err, visited_paths, t, tinterp);

    } else {
      PUSH_ERROR_AND_RETURN(targetPrimRet.error());
    }
  } else if (prop.is_relationship()) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Property `{}` is a Relation.", attr_name));
  } else if (prop.is_empty()) {
    PUSH_ERROR_AND_RETURN(fmt::format(
        "Attribute `{}` is a define-only attribute(no value assigned).",
        attr_name));
  } else if (prop.is_attribute()) {
    DCOUT("IsAttrib");

    const Attribute &attr = prop.get_attribute();

    if (attr.is_blocked()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Attribute `{}` is ValueBlocked(None).", attr_name));
    }

    if (!ToTerminalAttributeValue(attr, value, err, t, tinterp)) {
      return false;
    }

  } else {
    // ???
    PUSH_ERROR_AND_RETURN(
        fmt::format("[InternalError] Invalid Attribute `{}`.", attr_name));
  }

  return true;
}

}  // namespace

void VisitPrims(const tinyusdz::Stage &stage, VisitPrimFunction visitor_fun,
                void *userdata) {
  for (const auto &root : stage.root_prims()) {
    if (!VisitPrimsRec(root, /* root level */ 0, visitor_fun, userdata)) {
      return;
    }
  }
}

bool GetProperty(const tinyusdz::Prim &prim, const std::string &attr_name,
                 Property *out_prop, std::string *err) {
#define GET_PRIM_PROPERTY(__ty)                                         \
  if (prim.is<__ty>()) {                                                \
    auto ret = GetPrimProperty(*prim.as<__ty>(), attr_name, out_prop);  \
    if (ret) {                                                          \
      if (!ret.value()) {                                               \
        PUSH_ERROR_AND_RETURN(                                          \
            fmt::format("Attribute `{}` does not exist in Prim {}({})", \
                        attr_name, prim.element_path().prim_part(),     \
                        value::TypeTraits<__ty>::type_name()));         \
      }                                                                 \
    } else {                                                            \
      PUSH_ERROR_AND_RETURN(ret.error());                               \
    }                                                                   \
  } else

  GET_PRIM_PROPERTY(Model)
  GET_PRIM_PROPERTY(Xform)
  GET_PRIM_PROPERTY(Scope)
  GET_PRIM_PROPERTY(GeomMesh)
  GET_PRIM_PROPERTY(GeomSubset)
  GET_PRIM_PROPERTY(Shader)
  GET_PRIM_PROPERTY(Material)
  GET_PRIM_PROPERTY(SkelRoot)
  GET_PRIM_PROPERTY(BlendShape)
  GET_PRIM_PROPERTY(Skeleton)
  GET_PRIM_PROPERTY(SkelAnimation) {
    PUSH_ERROR_AND_RETURN("TODO: Prim type " << prim.type_name());
  }

  return true;
}

bool GetAttribute(const tinyusdz::Prim &prim, const std::string &attr_name,
                  Attribute *out_attr, std::string *err) {
  if (!out_attr) {
    PUSH_ERROR_AND_RETURN("`out_attr` argument is nullptr.");
  }

  // First lookup as Property, then check if its Attribute
  Property prop;
  if (!GetProperty(prim, attr_name, &prop, err)) {
    return false;
  }

  if (prop.is_attribute()) {
    (*out_attr) = std::move(prop.get_attribute());
    return true;
  }

  PUSH_ERROR_AND_RETURN(fmt::format("{} is not a Attribute.", attr_name));
}

bool GetRelationship(const tinyusdz::Prim &prim, const std::string &rel_name,
                     Relationship *out_rel, std::string *err) {
  if (!out_rel) {
    PUSH_ERROR_AND_RETURN("`out_rel` argument is nullptr.");
  }

  // First lookup as Property, then check if its Relationship
  Property prop;
  if (!GetProperty(prim, rel_name, &prop, err)) {
    return false;
  }

  if (prop.is_relationship()) {
    (*out_rel) = std::move(prop.get_relationship());
  }

  PUSH_ERROR_AND_RETURN(fmt::format("{} is not a Relationship.", rel_name));

  return true;
}

bool EvaluateAttribute(
    const tinyusdz::Stage &stage, const tinyusdz::Prim &prim,
    const std::string &attr_name, TerminalAttributeValue *value,
    std::string *err, const double t,
    const tinyusdz::value::TimeSampleInterpolationType tinterp) {
  std::set<std::string> visited_paths;

  return EvaluateAttributeImpl(stage, prim, attr_name, value, err,
                               visited_paths, t, tinterp);
}

bool ListSceneNames(const tinyusdz::Prim &root,
                    std::vector<std::pair<bool, std::string>> *sceneNames) {
  if (!sceneNames) {
    return false;
  }

  bool has_sceneLibrary = false;
  if (root.metas().kind.has_value()) {
    if (root.metas().kind.value() == Kind::SceneLibrary) {
      // ok
      has_sceneLibrary = true;
    }
  }

  if (!has_sceneLibrary) {
    return false;
  }

  for (const Prim &child : root.children()) {
    if (!ListSceneNamesRec(child, /* depth */ 0, sceneNames)) {
      return false;
    }
  }

  return true;
}

namespace {

bool BuildXformNodeFromStageRec(
  const tinyusdz::Stage &stage,
  const Path &parent_abs_path,
  const Prim &prim,
  XformNode *nodeOut, /* out */
  value::matrix4d rootMat,
  const double t, const tinyusdz::value::TimeSampleInterpolationType tinterp) {
  // TODO: time
  (void)t;
  (void)tinterp;
  (void)stage;

  if (!nodeOut) {
    return false;
  }

  XformNode node;

  if (prim.element_name().empty()) {
    // TODO: report error
  }

  node.element_name = prim.element_name();
  node.absolute_path = parent_abs_path.AppendPrim(prim.element_name());

  DCOUT(prim.element_name() << ": IsXformablePrim" << IsXformablePrim(prim));

  if (IsXformablePrim(prim)) {
    bool resetXformStack{false};

    // TODO: t, tinterp
    value::matrix4d localMat = GetLocalTransform(prim, &resetXformStack);
    DCOUT("local mat = " << localMat);

    value::matrix4d worldMat = rootMat;
    if (resetXformStack) {
      // FIXME. Is it correct to reset parent's world matrix?
      worldMat = value::matrix4d::identity();
    }

    value::matrix4d m = worldMat * localMat;

    node.set_parent_world_matrix(rootMat);
    node.set_local_matrix(localMat);
    node.set_world_matrix(m);
    node.has_xform() = true;
  } else {
    DCOUT("Not xformable");
    node.has_xform() = false;
    node.set_parent_world_matrix(rootMat);
    node.set_world_matrix(rootMat);
    node.set_local_matrix(value::matrix4d::identity());
  }

  for (const auto &childPrim : prim.children()) {
    XformNode childNode;
    if (!BuildXformNodeFromStageRec(stage, node.absolute_path, childPrim, &childNode, node.get_world_matrix(), t, tinterp)) {
      return false;
    }

    node.children.emplace_back(std::move(childNode));
  }

  (*nodeOut) = node;


  return true;
}

std::string DumpXformNodeRec(
  const XformNode &node,
  uint32_t indent)
{
  std::stringstream ss;

  ss << pprint::Indent(indent) << "Prim name: " << node.element_name << "(Path " << node.absolute_path << ") Xformable? " << node.has_xform() << " {\n";
  ss << pprint::Indent(indent+1) << "parent_world: " << node.get_parent_world_matrix() << "\n";
  ss << pprint::Indent(indent+1) << "world: " << node.get_world_matrix() << "\n";
  ss << pprint::Indent(indent+1) << "local: " << node.get_local_matrix() << "\n";

  for (const auto &child : node.children) {
    ss << DumpXformNodeRec(child, indent+1);
  }
  ss << pprint::Indent(indent+1) << "}\n";

  return ss.str();
}


} // namespace local

bool BuildXformNodeFromStage(
  const tinyusdz::Stage &stage,
  XformNode *rootNode, /* out */
  const double t, const tinyusdz::value::TimeSampleInterpolationType tinterp) {

  if (!rootNode) {
    return false;
  }

  XformNode stage_root;
  stage_root.element_name = ""; // Stage root element name is empty.
  stage_root.absolute_path = Path("/", "");
  stage_root.has_xform() = false;
  stage_root.parent = nullptr;
  stage_root.set_parent_world_matrix(value::matrix4d::identity());
  stage_root.set_world_matrix(value::matrix4d::identity());
  stage_root.set_local_matrix(value::matrix4d::identity());

  for (const auto &root : stage.root_prims()) {
    XformNode node;

    value::matrix4d rootMat{value::matrix4d::identity()};

    if (!BuildXformNodeFromStageRec(stage, stage_root.absolute_path, root, &node, rootMat, t, tinterp)) {
      return false;
    }

    stage_root.children.emplace_back(std::move(node));
  }

  (*rootNode) = stage_root;

  return true;
}

std::string DumpXformNode(
  const XformNode &node)
{
  return DumpXformNodeRec(node, 0);

}

}  // namespace tydra
}  // namespace tinyusdz
