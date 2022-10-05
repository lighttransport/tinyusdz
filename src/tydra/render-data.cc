#include "render-data.hh"

#include "pprinter.hh"
#include "prim-types.hh"
#include "tiny-format.hh"

#if defined(TINYUSDZ_WITH_COLORIO)
#include "external/tiny-color-io.h"
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// For triangulation.
// TODO: Use tinyobjloader's triangulation 
#include "external/mapbox/earcut/earcut.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

//
#include "common-macros.inc"

namespace tinyusdz {
namespace tydra {

namespace {

#if 0
template <typename T>
inline T Get(const nonstd::optional<T> &nv, const T &default_value) {
  if (nv) {
    return nv.value();
  }
  return default_value;
}
#endif

//
// name does not include "primvars:" prefix.
//
template <typename T>
nonstd::expected<VertexAttribute<T>, std::string> GetTextureCoordinate(
    const Stage &state, const GeomMesh &mesh, const std::string &name) {
  VertexAttribute<T> vattr;

  (void)state;
  (void)mesh;

  constexpr auto kPrimvars = "primvars:";
  constexpr auto kIndices = ":indices";

  // TODO: timeSamples, connect
  {
    const std::string primvar_name = kPrimvars + name;
    if (mesh.props.count(primvar_name)) {
      const Property &prop = mesh.props.at(primvar_name);

      if (prop.IsRel()) {
        return nonstd::make_unexpected(
            fmt::format("UV Primvar must not be Relation: {}", primvar_name));
      }

      if (prop.IsAttrib()) {
        // pxrUSD only allow int[] for ":indices"
        // https://github.com/PixarAnimationStudios/USD/issues/859 TinyUSDZ
        // allow uint[]
        // TODO: Support timeSampled indices.
        if (auto pv = prop.GetAttrib().get_value<std::vector<T>>()) {
          vattr.data = pv.value();
        } else {
          return nonstd::make_unexpected(
              fmt::format("UV Primvar must be type `{}`, but got `{}` for {}",
                          value::TypeTrait<T>::type_name(),
                          prop.GetAttrib().type_name(), primvar_name));
        }
      } else if (prop.IsConnection()) {
        return nonstd::make_unexpected(
            fmt::format("FIXME: Support Connection for UV Primvar property: {}",
                        primvar_name));
      } else {
        return nonstd::make_unexpected(
            fmt::format("Invalid Index property: {}", primvar_name));
      }

      // variability
      // In usdGeom, Default interpolation is "Constant"
      // TinyUSDZ currently reports error when `interpolation` is missing.
      auto interp = prop.GetAttrib().meta.interpolation;
      if (interp) {
      } else {
        return nonstd::make_unexpected(
            fmt::format("`interpolation` Attribute metadataum is required for "
                        "UV primvar. {}",
                        primvar_name));
      }
    } else {
      return nonstd::make_unexpected(
          fmt::format("UV Primvar not found. name: {}", primvar_name));
    }
  }

  //
  // has custom indices?
  //
  {
    const std::string index_name = kPrimvars + name + kIndices;
    if (mesh.props.count(index_name)) {
      const Property &prop = mesh.props.at(index_name);

      if (prop.IsRel()) {
        return nonstd::make_unexpected(fmt::format(
            "UV Primvar Indices must not be relation: {}", index_name));
      }

      if (prop.IsAttrib()) {
        // pxrUSD only allow int[] for ":indices"
        // https://github.com/PixarAnimationStudios/USD/issues/859 TinyUSDZ
        // allow uint[]
        // TODO: Support timeSampled indices.
        // TODO: Need to check variability meta
        if (auto pv = prop.GetAttrib().get_value<std::vector<int32_t>>()) {
          // convert to uint.
          vattr.indices.clear();
          for (const auto &item : pv.value()) {
            vattr.indices.push_back(uint32_t(item));
          }
        } else if (auto pvu =
                       prop.GetAttrib().get_value<std::vector<uint32_t>>()) {
          vattr.indices = pvu.value();
        } else {
          return nonstd::make_unexpected(fmt::format(
              "Index must be type `int[]` or `uint[]`, but got `{}` for {}",
              prop.GetAttrib().type_name(), index_name));
        }
      } else if (prop.IsConnection()) {
        return nonstd::make_unexpected(fmt::format(
            "FIXME: Support Connection for Index property: {}", index_name));
      } else {
        return nonstd::make_unexpected(
            fmt::format("Invalid Index property: {}", index_name));
      }
    }
  }

  return vattr;
}

}  // namespace

nonstd::expected<Node, std::string> Convert(const Stage &stage,
                                            const Xform &xform) {
  (void)stage;

  Node node;
  if (auto m = xform.GetLocalMatrix()) {
    node.local_matrix = m.value();
  }

  return std::move(node);
}

nonstd::expected<RenderMesh, std::string> Convert(const Stage &stage,
                                                  const GeomMesh &mesh) {
  RenderMesh dst;

  if (mesh.GetPoints().size()) {
    dst.points.resize(mesh.GetPoints().size());
    memcpy(dst.points.data(), mesh.GetPoints().data(),
           sizeof(value::float3) * mesh.GetPoints().size());
  }

  // normals
  {
    std::vector<value::normal3f> normals = mesh.GetNormals();
    Interpolation interp = mesh.GetNormalsInterpolation();

    if (interp == Interpolation::Vertex) {
      return nonstd::make_unexpected(
          "TODO: `vertex` interpolation for `normals` attribute.\n");
    } else if (interp == Interpolation::FaceVarying) {
      dst.facevaryingNormals.resize(normals.size());
      memcpy(dst.facevaryingNormals.data(), normals.data(),
             sizeof(value::normal3f) * normals.size());
    } else {
      return nonstd::make_unexpected(
          "Unsupported/unimplemented interpolation for `normals` attribute: " +
          to_string(interp) + ".\n");
    }
  }

  // Material/Shader
  if (mesh.materialBinding) {
    const MaterialBindingAPI &materialBinding = mesh.materialBinding.value();
    if (materialBinding.binding.IsValid()) {
      const Path &matPath = materialBinding.binding;
      DCOUT("materialBinding = " << to_string(matPath));
    }

    // stage.GetPrimAtPath
  }

  // Compute total faceVarying elements;
  size_t num_fvs = 0;
  for (size_t i = 0; i < dst.faceVertexIndices.size(); i++) {
    if (dst.faceVertexIndices[i] > dst.faceVertexCounts.size()) {
      return nonstd::make_unexpected(fmt::format(
          "faceVertexIndices[{}] is out-of-bounds(faceVertexCounts.size {})",
          dst.faceVertexIndices[i], dst.faceVertexCounts.size()));

      //
    }
    num_fvs += dst.faceVertexIndices[i];
  }

  DCOUT("num_fvs = " << num_fvs);

  // uvs
  // Procedure:
  // - Find Shader
  // - Lookup PrimvarReader
  //

  // TEST
  // UsdPrimvarReader_float2 uv;
  // if (value::TypeTrait<decltype(uv)>::type_id ==
  // value::TypeTrait<UsdPrimarReader_float2>::type_id) {
  //}
  {
    std::string uvname = "st";
    auto ret = GetTextureCoordinate<vec2>(stage, mesh, uvname);
    if (ret) {
      const VertexAttribute<vec2> vattr = ret.value();

      if (vattr.variability != VertexVariability::FaceVarying) {
        return nonstd::make_unexpected(
            fmt::format("TODO: non-facevarying UV texcoord attribute is not "
                        "support yet: {}.",
                        uvname));
      }

      if (vattr.indices.size()) {
        // TODO: Reorder
      } else {
        if (vattr.data.size() != num_fvs) {
          return nonstd::make_unexpected(
              fmt::format("The number of UV texcoord attribute {} does not match to the number of facevarying elements {}", vattr.data.size(), num_fvs));
        }
      }

    } else {
      return nonstd::make_unexpected(ret.error());
    }
  }

  // TODO: Triangulate.

  return std::move(dst);
}

}  // namespace tydra
}  // namespace tinyusdz
