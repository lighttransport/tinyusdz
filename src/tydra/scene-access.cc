#include "scene-access.hh"

#include "usdGeom.hh"
#include "usdLux.hh"
#include "usdShade.hh"
#include "usdSkel.hh"
#include "pprinter.hh"
#include "prim-pprint.hh"
#include "value-pprint.hh"

namespace tinyusdz {
namespace tydra {

namespace {

template <typename T>
bool TraverseRec(const std::string &path_prefix,
                        const tinyusdz::Prim &prim, uint32_t depth,
                        PathPrimMap<T> &itemmap) {
  if (depth > 1024 * 128) {
    // Too deep
    return false;
  }

  std::string prim_abs_path = path_prefix + "/" + prim.local_path().full_path_name();

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

  std::string prim_abs_path = path_prefix + "/" + prim.local_path().full_path_name();

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

} // namespace local


template<typename T>
bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<T> &m /* output */) {
  // Should report error at compilation stege.
  static_assert((value::TypeId::TYPE_ID_MODEL_BEGIN <= value::TypeTraits<T>::type_id) &&
      (value::TypeId::TYPE_ID_MODEL_END > value::TypeTraits<T>::type_id), "Not a Prim type.");

  // Check at runtime. Just in case...
  if ((value::TypeId::TYPE_ID_MODEL_BEGIN <= value::TypeTraits<T>::type_id) &&
      (value::TypeId::TYPE_ID_MODEL_END > value::TypeTraits<T>::type_id)) {
    // Ok
  } else {
    return false;
  }

  for (const auto &root_prim : stage.GetRootPrims()) {
    TraverseRec(/* root path is empty */"", root_prim, /* depth */0, m);
  }

  return true;
}

template<typename T>
bool ListShaders(const tinyusdz::Stage &stage, PathShaderMap<T> &m /* output */) {
  // Concrete Shader type(e.g. UsdPreviewSurface) is classified as Imaging
  // Should report error at compilation stege.
  static_assert((value::TypeId::TYPE_ID_IMAGING_BEGIN <= value::TypeTraits<T>::type_id) &&
      (value::TypeId::TYPE_ID_IMAGING_END > value::TypeTraits<T>::type_id), "Not a Shader type.");

  // Check at runtime. Just in case...
  if ((value::TypeId::TYPE_ID_IMAGING_BEGIN <= value::TypeTraits<T>::type_id) &&
      (value::TypeId::TYPE_ID_IMAGING_END > value::TypeTraits<T>::type_id)) {
    // Ok
  } else {
    return false;
  }

  for (const auto &root_prim : stage.GetRootPrims()) {
    TraverseShaderRec(/* root path is empty */"", root_prim, /* depth */0, m);
  }

  return true;
}

const Prim *GetParentPrim(const tinyusdz::Stage &stage, const tinyusdz::Path &path, std::string *err) {
  if (!path.IsValid()) {
    if (err) {
      (*err) = "Input Path " + tinyusdz::to_string(path) + " is invalid.\n";
    }
    return nullptr;
  }

  if (path.IsRootPath()) {
    if (err) {
      (*err) = "Input Path is root(\"/\").\n";
    }
    return nullptr;
  }

  if (path.IsRootPrim()) {
    if (err) {
      (*err) = "Input Path is root Prim, so no parent Prim exists.\n";
    }
    return nullptr;
  }

  if (!path.IsAbsolutePath()) {
    if (err) {
      (*err) = "Input Path must be absolute path(i.e. starts with \"/\").\n";
    }
    return nullptr;
  }

  tinyusdz::Path parentPath = path.GetParentPrimPath();

  nonstd::expected<const Prim *, std::string> ret = stage.GetPrimAtPath(parentPath);
  if (ret) {
    return ret.value();
  } else {
    if (err) {
      (*err) += "Failed to get parent Prim from Path " + tinyusdz::to_string(path) + ". Reason = " + ret.error() + "\n";
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
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<GeomBasisCurves> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<GeomSphere> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<GeomCone> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<GeomCylinder> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<GeomCapsule> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<GeomCube> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<GeomPoints> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<GeomSubset> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<GeomCamera> &m);

template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<DomeLight> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<CylinderLight> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<SphereLight> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<DiskLight> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<DistantLight> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<RectLight> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<GeometryLight> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<PortalLight> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<PluginLight> &m);

template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<Material> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<Shader> &m);

template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<SkelRoot> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<Skeleton> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<SkelAnimation> &m);
template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<BlendShape> &m);

template bool ListShaders(const tinyusdz::Stage &stage, PathShaderMap<UsdPreviewSurface> &m);
template bool ListShaders(const tinyusdz::Stage &stage, PathShaderMap<UsdUVTexture> &m);

template bool ListShaders(const tinyusdz::Stage &stage, PathShaderMap<UsdPrimvarReader_int> &m);
template bool ListShaders(const tinyusdz::Stage &stage, PathShaderMap<UsdPrimvarReader_float> &m);
template bool ListShaders(const tinyusdz::Stage &stage, PathShaderMap<UsdPrimvarReader_float2> &m);
template bool ListShaders(const tinyusdz::Stage &stage, PathShaderMap<UsdPrimvarReader_float3> &m);
template bool ListShaders(const tinyusdz::Stage &stage, PathShaderMap<UsdPrimvarReader_float4> &m);

} // namespace tydra
} // namespace tinyusdz
