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

}  // namespace

template <typename T>
bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<T> &m /* output */) {
  // Should report error at compilation stege.
  static_assert(
      (value::TypeId::TYPE_ID_MODEL_BEGIN <= value::TypeTraits<T>::type_id) &&
          (value::TypeId::TYPE_ID_MODEL_END > value::TypeTraits<T>::type_id),
      "Not a Prim type.");

  // Check at runtime. Just in case...
  if ((value::TypeId::TYPE_ID_MODEL_BEGIN <= value::TypeTraits<T>::type_id) &&
      (value::TypeId::TYPE_ID_MODEL_END > value::TypeTraits<T>::type_id)) {
    // Ok
  } else {
    return false;
  }

  for (const auto &root_prim : stage.GetRootPrims()) {
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
      (value::TypeId::TYPE_ID_IMAGING_BEGIN <= value::TypeTraits<T>::type_id) &&
          (value::TypeId::TYPE_ID_IMAGING_END > value::TypeTraits<T>::type_id),
      "Not a Shader type.");

  // Check at runtime. Just in case...
  if ((value::TypeId::TYPE_ID_IMAGING_BEGIN <= value::TypeTraits<T>::type_id) &&
      (value::TypeId::TYPE_ID_IMAGING_END > value::TypeTraits<T>::type_id)) {
    // Ok
  } else {
    return false;
  }

  for (const auto &root_prim : stage.GetRootPrims()) {
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
    pvar.set_scalar(val);
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
      relTarget.set(paths[0]);
    } else {
      relTarget.set(paths);
    }
    output = Property(relTarget, /* type */ value::TypeTraits<T>::type_name(),
                      /* custom */ false);

  } else {
    // Includes !authored()
    if (auto pv = input.get_value()) {
      value::Value val(pv.value());
      primvar::PrimVar pvar;
      pvar.set_scalar(val);
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
    Relationship rel;
    std::vector<Path> pv = input.get_connections();
    if (pv.empty()) {
      DCOUT("??? Empty connectionTarget.");
    }
    if (pv.size() == 1) {
      rel.set(pv[0]);
      DCOUT("targetPath = " << rel.targetPath);
    } else if (pv.size() > 1) {
      rel.set(pv);
    } else {
      // ??? TODO: report internal error.
      DCOUT("??? GetConnection faile.");
    }

    output = Property(rel, /* type */ value::TypeTraits<T>::type_name(),
                      /* custom */ false);
    return;

  } else {
    // Includes !authored()
    // FIXME: Currently scalar only.
    nonstd::optional<Animatable<T>> aval = input.get_value();
    if (aval) {
      if (aval.value().is_scalar()) {
        value::Value val(aval.value().value);
        primvar::PrimVar pvar;
        pvar.set_scalar(val);
        Attribute attr;
        attr.set_var(std::move(pvar));
        attr.variability() = Variability::Uniform;
        output = Property(attr, /* custom */ false);
        return;
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

#if 0
// Scalar or TimeSample-valued attribute.
// TypedAttribute* => Attribute defined in USD schema, so not a custom attr.
//
// TODO: Support timeSampled attribute.
template<typename T>
void ToProperty(
  const TypedAttributeWithFallback<Animatable<T>> &input,
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
    // FIXME: Currently scalar only.
    value::Value val(input.GetValue());
    primvar::PrimVar pvar;
    pvar.set_scalar(val);
    Attribute attr;
    attr.set_var(std::move(pvar));
    attr.variability() = Variability::Uniform;
    output = Property(attr, /* custom */false);
  }
}
#endif

bool ToTerminalAttributeValue(const Attribute &attr,
                              TerminalAttributeValue *value, std::string *err,
                              value::TimeCode tc,
                              TimeSampleInterpolationType tinterp) {
  if (!value) {
    // ???
    return false;
  }

  if (attr.blocked()) {
    PUSH_ERROR_AND_RETURN("Attribute is None(Value Blocked).");
  }

  const primvar::PrimVar &var = attr.get_var();

  value->meta() = attr.meta;
  value->variability() = attr.variability();

  if (!var.is_valid()) {
    PUSH_ERROR_AND_RETURN("[InternalError] Attribute is invalid.");
  } else if (var.is_scalar()) {
    const value::TimeSamples &ts = var.var();
    DCOUT("Attribute is scalar type:" << ts.values[0].type_name());
    DCOUT("Attribute value = " << pprint_value(ts.values[0]));

    value->set_value(ts.values[0]);
  } else if (var.is_timesample()) {
    (void)tc;
    (void)tinterp;
    // TODO:
    // Evaluate timeSample value at specified timeCode.
    PUSH_ERROR_AND_RETURN("TODO: TimeSamples.");
  }

  return true;
}

bool XformOpToProperty(const XformOp &x, Property &prop) {
  primvar::PrimVar pv;

  Attribute attr;

  switch (x.op) {
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
      pv.var() = x.var();
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
    pvar.set_scalar(toks);

    Attribute attr;
    attr.set_var(std::move(pvar));
    attr.variability() = Variability::Uniform;
    Property prop;
    prop.set_attribute(attr);

    (*out_prop) = prop;

  } else {
    // XformOp?
    for (const auto &item : xform.xformOps) {
      std::string op_name = to_string(item.op);
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
    const Material &material, const std::string &prop_name,
    Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  DCOUT("prop_name = " << prop_name);
  if (prop_name == "outputs:surface") {
    // Terminal attribute.
    if (!material.surface) {
    }
  }
  // if (prop_name == "outputs:volume") {

  // ToProperty(preader.varname, (*out_prop));

  {
    const auto it = material.props.find(prop_name);
    if (it == material.props.end()) {
      // Attribute not found.
      return false;
    }

    (*out_prop) = it->second;
    DCOUT("Prop found: " << prop_name
                         << ", ty = " << it->second.value_type_name());
  }

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
    DCOUT("Prop found: " << prop_name
                         << ", ty = " << it->second.value_type_name());
  }

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
    DCOUT("Prop found: " << prop_name
                         << ", ty = " << it->second.value_type_name());
  }
  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const Skeleton &skel, const std::string &prop_name,
    Property *out_prop) {
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
      Relationship rel;
      rel.set(skel.animationSource.value());
      (*out_prop) = Property(rel, /* custom */false);
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
    DCOUT("Prop found: " << prop_name
                         << ", ty = " << it->second.value_type_name());
  }
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
    DCOUT("Prop found: " << prop_name
                         << ", ty = " << it->second.value_type_name());
  }
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
  } else {
    return nonstd::make_unexpected("TODO: " + shader.value.type_name());
  }
}

// TODO: provide visit map to prevent circular referencing.
bool EvaluateAttributeImpl(
    const tinyusdz::Stage &stage, const tinyusdz::Prim &prim,
    const std::string &attr_name, TerminalAttributeValue *value,
    std::string *err, std::set<std::string> &visited_paths,
    const tinyusdz::value::TimeCode tc,
    const tinyusdz::TimeSampleInterpolationType tinterp) {
  // TODO:
  (void)tc;
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
                                   value, err, visited_paths, tc, tinterp);

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

    if (attr.blocked()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Attribute `{}` is ValueBlocked(None).", attr_name));
    }

    if (!ToTerminalAttributeValue(attr, value, err, tc, tinterp)) {
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
  for (const auto &root : stage.GetRootPrims()) {
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

  GET_PRIM_PROPERTY(Xform)
  GET_PRIM_PROPERTY(Shader)
  GET_PRIM_PROPERTY(Material)
  GET_PRIM_PROPERTY(SkelRoot)
  GET_PRIM_PROPERTY(BlendShape)
  GET_PRIM_PROPERTY(Skeleton)
  GET_PRIM_PROPERTY(SkelAnimation)
  {
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

bool EvaluateAttribute(const tinyusdz::Stage &stage, const tinyusdz::Prim &prim,
                       const std::string &attr_name,
                       TerminalAttributeValue *value, std::string *err,
                       const tinyusdz::value::TimeCode tc,
                       const tinyusdz::TimeSampleInterpolationType tinterp) {
  std::set<std::string> visited_paths;

  return EvaluateAttributeImpl(stage, prim, attr_name, value, err,
                               visited_paths, tc, tinterp);
}

}  // namespace tydra
}  // namespace tinyusdz
