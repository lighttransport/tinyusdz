#include "render-data.hh"

#include "pprinter.hh"
#include "prim-types.hh"
#include "tiny-format.hh"
#include "tinyusdz.hh"
#include "usdShade.hh"

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
#include "math-util.inc"

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
                          value::TypeTraits<T>::type_name(),
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

  return std::move(vattr);
}

///
/// Input: points, faceVertexCounts, faceVertexIndices
/// Output: triangulated faceVertexCounts(all filled with 3), triangulated faceVertexIndices, indexMap
/// (length = triangulated faceVertexIndices. indexMap[i] stores array index in original faceVertexIndices.
/// For remapping primvar attributes.)
///
/// Return false when a polygon is degenerated.
/// No overlap check at the moment
///
/// T = value::float3 or value::double3
/// BaseTy = float or double
template<typename T, typename BaseTy>
bool TriangulatePolygon(
    const std::vector<T> &points,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices,
    std::vector<uint32_t> &triangulatedFaceVertexCounts,
    std::vector<uint32_t> &triangulatedFaceVertexIndices,
    std::vector<size_t> &faceVertexIndexMap,
    std::string &err) {

    triangulatedFaceVertexCounts.clear();
    triangulatedFaceVertexIndices.clear();

    faceVertexIndexMap.clear();

    size_t faceIndexOffset = 0;

    // For each polygon(face)
    for (size_t i = 0; i < faceVertexIndices.size(); i++) {
      uint32_t npolys = faceVertexCounts[i];

      if (npolys < 3) {
        err = fmt::format("faceVertex count must be 3(triangle) or "
                        "more(polygon), but got faceVertexCounts[{}] = {}",
                        i, npolys);
        return false;
      }

      if (faceIndexOffset + npolys > faceVertexIndices.size()) {
        err = fmt::format(
            "Invalid faceVertexIndices or faceVertexCounts. faceVertex index "
            "exceeds faceVertexIndices.size() at [{}]",
            i);
        return false;
      }

      if (npolys == 3) {
        // No need for triangulation.
        triangulatedFaceVertexCounts.push_back(3);
        triangulatedFaceVertexIndices.push_back(
            faceVertexIndices[faceIndexOffset + 0]);
        triangulatedFaceVertexIndices.push_back(
            faceVertexIndices[faceIndexOffset + 1]);
        triangulatedFaceVertexIndices.push_back(
            faceVertexIndices[faceIndexOffset + 2]);
        faceVertexIndexMap.push_back(i);
#if 0
      } else if (npolys == 4) {
        // Use simple split
        // TODO: Split at shortest edge?
        triangulatedFaceVertexCounts.push_back(3);
        triangulatedFaceVertexCounts.push_back(3);

        triangulatedFaceVertexIndices.push_back(dst.faceVertexIndices[faceIndexOffset + 0]);
        triangulatedFaceVertexIndices.push_back(dst.faceVertexIndices[faceIndexOffset + 1]);
        triangulatedFaceVertexIndices.push_back(dst.faceVertexIndices[faceIndexOffset + 2]);

        triangulatedFaceVertexIndices.push_back(dst.faceVertexIndices[faceIndexOffset + 0]);
        triangulatedFaceVertexIndices.push_back(dst.faceVertexIndices[faceIndexOffset + 2]);
        triangulatedFaceVertexIndices.push_back(dst.faceVertexIndices[faceIndexOffset + 3]);

        faceVertexIndexMap.push_back(i);
        faceVertexIndexMap.push_back(i);
#endif
      } else {
        // Find the normal axis of the polygon using Newell's method
        T n = {BaseTy(0), BaseTy(0), BaseTy(0)};

        size_t vi0;
        size_t vi0_2;

        for (size_t k = 0; k < npolys; ++k) {
          vi0 = faceVertexIndices[faceIndexOffset + k];

          size_t j = (k + 1) % npolys;
          vi0_2 = faceVertexIndices[faceIndexOffset + j];

          if (vi0 >= points.size()) {
            err =
                fmt::format("Invalid vertex index.");
            return false;
          }

          if (vi0_2 >= points.size()) {
            err =
                fmt::format("Invalid vertex index.");
            return false;
          }

          T v0 = points[vi0];
          T v1 = points[vi0_2];

          const T point1 = {v0[0], v0[1], v0[2]};
          const T point2 = {v1[0], v1[1], v1[2]};

          T a = {point1[0] - point2[0], point1[1] - point2[1],
                             point1[2] - point2[2]};
          T b = {point1[0] + point2[0], point1[1] + point2[1],
                             point1[2] + point2[2]};

          n[0] += (a[1] * b[2]);
          n[1] += (a[2] * b[0]);
          n[2] += (a[0] * b[1]);
        }
        BaseTy length_n = math::vlength(n);
        // Check if zero length normal
        if (std::fabs(length_n) < std::numeric_limits<BaseTy>::epsilon()) {
          err = "Degenerated polygon found.";
          return false;
        }

        // Negative is to flip the normal to the correct direction
        n = math::vnormalize(n);

        T axis_w, axis_v, axis_u;
        axis_w = n;
        T a;
        if (std::fabs(axis_w[0]) > BaseTy(0.9999999)) {  // TODO: use 1.0 - eps?
          a = {BaseTy(0), BaseTy(1), BaseTy(0)};
        } else {
          a = {BaseTy(1), BaseTy(0), BaseTy(0)};
        }
        axis_v = math::vnormalize(math::vcross(axis_w, a));
        axis_u = math::vcross(axis_w, axis_v);

        using Point3D = std::array<BaseTy, 3>;
        using Point2D = std::array<BaseTy, 2>;
        std::vector<Point2D> polyline;

        // TMW change: Find best normal and project v0x and v0y to those
        // coordinates, instead of picking a plane aligned with an axis (which
        // can flip polygons).

        // Fill polygon data.
        for (size_t k = 0; k < npolys; k++) {
          size_t vidx = faceVertexIndices[faceIndexOffset + k];

          value::float3 v = points[vidx];
          // Point3 polypoint = {v0[0],v0[1],v0[2]};

          // world to local
          Point3D loc = {math::vdot(v, axis_u), math::vdot(v, axis_v),
                         math::vdot(v, axis_w)};

          polyline.push_back({loc[0], loc[1]});
        }

        std::vector<std::vector<Point2D>> polygon_2d;
        // Single polygon only(no holes)

        std::vector<uint32_t> indices = mapbox::earcut<uint32_t>(polygon_2d);
        //  => result = 3 * faces, clockwise

        if ((indices.size() % 3) != 0) {
          // This should not be happen, though.
          err = "Failed to triangulate.\n";
          return false;
        }

        size_t ntris = indices.size() / 3;

        for (size_t k = 0; k < ntris; k++) {
          triangulatedFaceVertexCounts.push_back(3);
          triangulatedFaceVertexIndices.push_back(
              faceVertexIndices[faceIndexOffset + indices[3 * k + 0]]);
          triangulatedFaceVertexIndices.push_back(
              faceVertexIndices[faceIndexOffset + indices[3 * k + 1]]);
          triangulatedFaceVertexIndices.push_back(
              faceVertexIndices[faceIndexOffset + indices[3 * k + 2]]);

          faceVertexIndexMap.push_back(i);
        }
      }

      faceIndexOffset += npolys;
    }

  return true;
}

#if 0
//
// `Shader` may be nested, so first list up all Shader nodes under Material.
//
struct ShaderNode
{
  std::name
};

nonstd::optional<UsdPrimvarReader_float2> FindPrimvarReader_float2Rec(
  const Prim &root,
  RenderMesh &mesh)
{
  if (auto sv = root.data.as<Shader>()) {
    const Shader &shader = (*sv);

    if (auto pv = shader.value.as<UsdUVTexture>()) {
      const UsdUVTexture &tex = (*pv);
      (void)tex;
    }
  }

  for (const auto &child : root.children) {
    auto ret = ListUpShaderGraphRec(child, mesh);
    if (!ret) {
      return nonstd::make_unexpected(ret.error());
    }
  }

  return true;
}
#endif


}  // namespace

// Currently float2 only
std::vector<UsdPrimvarReader_float2> ExtractPrimvarReadersFromMaterialNode(
    const Prim &node) {
  std::vector<UsdPrimvarReader_float2> dst;

  if (!node.is<Material>()) {
    return dst;
  }

  for (const auto &child : node.children()) {
    (void)child;
  }

  // Traverse and find PrimvarReader_float2 shader.
  return dst;
}

nonstd::expected<Node, std::string> Convert(const Stage &stage,
                                            const Xform &xform) {
  (void)stage;

  // TODO: timeSamples

  Node node;
  if (auto m = xform.GetLocalMatrix()) {
    node.local_matrix = m.value();
  }

  return std::move(node);
}

nonstd::expected<RenderMesh, std::string> Convert(const Stage &stage,
                                                  const GeomMesh &mesh,
                                                  bool triangulate) {
  RenderMesh dst;

  // TODO: timeSamples

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
      DCOUT("materialBinding = " << to_string(materialBinding.binding));
    } else {
      return nonstd::make_unexpected(
          fmt::format("material:binding has invalid Path."));
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
              fmt::format("The number of UV texcoord attribute {} does not "
                          "match to the number of facevarying elements {}",
                          vattr.data.size(), num_fvs));
        }
      }

    } else {
      return nonstd::make_unexpected(ret.error());
    }
  }

  if (triangulate) {
    // TODO: Triangulate.

    std::string err;

    std::vector<uint32_t> triangulatedFaceVertexCounts;
    std::vector<uint32_t> triangulatedFaceVertexIndices;
    std::vector<size_t> faceVertexIndexMap;
    if (!TriangulatePolygon<value::float3, float>(dst.points, dst.faceVertexCounts, dst.faceVertexIndices,
      triangulatedFaceVertexCounts, triangulatedFaceVertexIndices, faceVertexIndexMap, err)) {

      return nonstd::make_unexpected("Triangulation failed: " + err);
    }

  }  // triangulate

  return std::move(dst);
}

}  // namespace tydra
}  // namespace tinyusdz
