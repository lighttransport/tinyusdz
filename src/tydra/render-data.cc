//
// TODO:
//   - [ ] Support time-varying shader attribute(timeSamples)
//
#include "pprinter.hh"
#include "prim-types.hh"
#include "tiny-format.hh"
#include "tinyusdz.hh"
#include "usdGeom.hh"
#include "usdShade.hh"
#include "value-pprint.hh"

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

//
#include "tydra/render-data.hh"
#include "tydra/scene-access.hh"
#include "tydra/shader-network.hh"

namespace tinyusdz {

extern template bool GeomPrimvar::flatten_with_indices(
    std::vector<value::texcoord2f> *dst, std::string *err);

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
// TODO: timeSamples, connected attribute.
//
nonstd::expected<VertexAttribute, std::string> GetTextureCoordinate(
    const Stage &state, const GeomMesh &mesh, const std::string &name) {
  VertexAttribute vattr;

  (void)state;

  GeomPrimvar primvar;
  if (!mesh.get_primvar(name, &primvar)) {
    return nonstd::make_unexpected(fmt::format("No primvars:{}", name));
  }

  if (!primvar.has_value()) {
    return nonstd::make_unexpected("No value exist for primvars:" + name);
  }

  if (primvar.get_type_id() !=
      value::TypeTraits<std::vector<value::texcoord2f>>::type_id()) {
    return nonstd::make_unexpected(
        "Texture coordinate primvar must be texCoord2f[] type, but got " +
        primvar.get_type_name());
  }

  if (primvar.get_interpolation() == Interpolation::Varying) {
    vattr.variability = VertexVariability::Varying;
  } else if (primvar.get_interpolation() == Interpolation::Constant) {
    vattr.variability = VertexVariability::Constant;
  } else if (primvar.get_interpolation() == Interpolation::Uniform) {
    vattr.variability = VertexVariability::Uniform;
  } else if (primvar.get_interpolation() == Interpolation::Vertex) {
    vattr.variability = VertexVariability::Vertex;
  } else if (primvar.get_interpolation() == Interpolation::FaceVarying) {
    vattr.variability = VertexVariability::FaceVarying;
  }

  std::vector<value::texcoord2f> uvs;
  if (!primvar.flatten_with_indices(&uvs)) {
    return nonstd::make_unexpected(
        "Failed to retrieve texture coordinate primvar with concrete type.");
  }

#if 0
  // TODO: timeSamples, connect
  {
    const std::string primvar_name = kPrimvars + name;
    if (mesh.props.count(primvar_name)) {
      const Property &prop = mesh.props.at(primvar_name);

      if (prop.is_relationship()) {
        return nonstd::make_unexpected(
            fmt::format("UV Primvar must not be Relation: {}", primvar_name));
      }

      if (prop.is_attribute()) {
        // pxrUSD only allow int[] for ":indices"
        // https://github.com/PixarAnimationStudios/USD/issues/859 TinyUSDZ
        // allow uint[]
        // TODO: Support timeSampled indices.
        if (auto pv = prop.get_attribute().get_value<std::vector<T>>()) {
          vattr.data = pv.value();
        } else {
          return nonstd::make_unexpected(
              fmt::format("UV Primvar must be type `{}`, but got `{}` for {}",
                          value::TypeTraits<T>::type_name(),
                          prop.get_attribute().type_name(), primvar_name));
        }
      } else if (prop.is_connection()) {
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
      auto interp = prop.get_attribute().metas().interpolation;
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

      if (prop.is_relationship()) {
        return nonstd::make_unexpected(fmt::format(
            "UV Primvar Indices must not be relation: {}", index_name));
      }

      if (prop.is_attribute()) {
        // pxrUSD only allow int[] for ":indices"
        // https://github.com/PixarAnimationStudios/USD/issues/859 TinyUSDZ
        // allow uint[]
        // TODO: Support timeSampled indices.
        // TODO: Need to check variability meta
        if (auto pv = prop.get_attribute().get_value<std::vector<int32_t>>()) {
          // convert to uint.
          vattr.indices.clear();
          for (const auto &item : pv.value()) {
            vattr.indices.push_back(uint32_t(item));
          }
        } else if (auto pvu =
                       prop.get_attribute().get_value<std::vector<uint32_t>>()) {
          vattr.indices = pvu.value();
        } else {
          return nonstd::make_unexpected(fmt::format(
              "Index must be type `int[]` or `uint[]`, but got `{}` for {}",
              prop.get_attribute().type_name(), index_name));
        }
      } else if (prop.is_connection()) {
        return nonstd::make_unexpected(fmt::format(
            "FIXME: Support Connection for Index property: {}", index_name));
      } else {
        return nonstd::make_unexpected(
            fmt::format("Invalid Index property: {}", index_name));
      }
    }
  }
#endif

  return std::move(vattr);
}

///
/// Input: points, faceVertexCounts, faceVertexIndices
/// Output: triangulated faceVertexCounts(all filled with 3), triangulated
/// faceVertexIndices, indexMap (length = triangulated faceVertexIndices.
/// indexMap[i] stores array index in original faceVertexIndices. For remapping
/// primvar attributes.)
///
/// Return false when a polygon is degenerated.
/// No overlap check at the moment
///
/// T = value::float3 or value::double3
/// BaseTy = float or double
template <typename T, typename BaseTy>
bool TriangulatePolygon(const std::vector<T> &points,
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
      err = fmt::format(
          "faceVertex count must be 3(triangle) or "
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
          err = fmt::format("Invalid vertex index.");
          return false;
        }

        if (vi0_2 >= points.size()) {
          err = fmt::format("Invalid vertex index.");
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
      BaseTy length_n = vlength(n);
      // Check if zero length normal
      if (std::fabs(length_n) < std::numeric_limits<BaseTy>::epsilon()) {
        err = "Degenerated polygon found.";
        return false;
      }

      // Negative is to flip the normal to the correct direction
      n = vnormalize(n);

      T axis_w, axis_v, axis_u;
      axis_w = n;
      T a;
      if (std::fabs(axis_w[0]) > BaseTy(0.9999999)) {  // TODO: use 1.0 - eps?
        a = {BaseTy(0), BaseTy(1), BaseTy(0)};
      } else {
        a = {BaseTy(1), BaseTy(0), BaseTy(0)};
      }
      axis_v = vnormalize(vcross(axis_w, a));
      axis_u = vcross(axis_w, axis_v);

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
        Point3D loc = {vdot(v, axis_u), vdot(v, axis_v), vdot(v, axis_w)};

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

  if (mesh.get_points().size()) {
    dst.points.resize(mesh.get_points().size());
    memcpy(dst.points.data(), mesh.get_points().data(),
           sizeof(value::float3) * mesh.get_points().size());
  }

  // normals
  {
    std::vector<value::normal3f> normals = mesh.get_normals();
    Interpolation interp = mesh.get_normalsInterpolation();

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
    const Relationship &materialBinding = mesh.materialBinding.value();
    DCOUT("TODO: materialBinding.");
    (void)materialBinding;
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
    auto ret = GetTextureCoordinate(stage, mesh, uvname);
    if (ret) {
      const VertexAttribute vattr = ret.value();

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
    if (!TriangulatePolygon<value::float3, float>(
            dst.points, dst.faceVertexCounts, dst.faceVertexIndices,
            triangulatedFaceVertexCounts, triangulatedFaceVertexIndices,
            faceVertexIndexMap, err)) {
      return nonstd::make_unexpected("Triangulation failed: " + err);
    }

  }  // triangulate

  return std::move(dst);
}

namespace {


#if 1
// Convert UsdTranform2d -> PrimvarReader_float2 shader network.
nonstd::expected<bool, std::string> ConvertTexTransform2d(
  const Stage &stage, const Path &tx_abs_path, const UsdTransform2d &tx,
  UVTexture *tex_out) {

  float rotation; // in angles
  if (!tx.rotation.get_value().get_scalar(&rotation)) {
    return nonstd::make_unexpected(fmt::format("Failed to retrieve rotation attribute from {}", tx_abs_path.full_path_name()));
  }

  value::float2 scale;
  if (!tx.scale.get_value().get_scalar(&scale)) {
    return nonstd::make_unexpected(fmt::format("Failed to retrieve scale attribute from {}", tx_abs_path.full_path_name()));
  }

  value::float2 translation;
  if (!tx.translation.get_value().get_scalar(&translation)) {
    return nonstd::make_unexpected(fmt::format("Failed to retrieve translation attribute from {}", tx_abs_path.full_path_name()));
  }



  // must be authored and connected to PrimvarReader.
  if (!tx.in.authored()) {
    return nonstd::make_unexpected("`inputs:in` must be authored.");
  }

  if (!tx.in.is_connection()) {
    return nonstd::make_unexpected("`inputs:in` must be a connection.");
  }

  const auto &paths = tx.in.get_connections();
  if (paths.size() != 1) {
    return nonstd::make_unexpected("`inputs:in` must be a single connection Path.");
  }

  std::string prim_part = paths[0].prim_part();
  std::string prop_part = paths[0].prop_part();

  if (prop_part != "outputs:result") {
    return nonstd::make_unexpected("`inputs:in` connection Path's property part must be `outputs:result`");
  }

  std::string err;

  const Prim *pprim{nullptr};
  if (!stage.find_prim_at_path(Path(prim_part, ""), pprim, &err)) {
    return nonstd::make_unexpected(fmt::format("`inputs:in` connection Path not found in the Stage. {}", prim_part));
  }

  if (!pprim) {
    return nonstd::make_unexpected(fmt::format("[InternalError] Prim is nullptr: {}", prim_part));
  }

  const Shader *pshader = pprim->as<Shader>();
  if (!pshader) {
    return nonstd::make_unexpected(fmt::format("{} must be Shader Prim, but got {}", prim_part, pprim->prim_type_name()));
  }

  const UsdPrimvarReader_float2 *preader = pshader->value.as<UsdPrimvarReader_float2>();
  if (preader) {
    return nonstd::make_unexpected(fmt::format("Shader {} must be UsdPrimvarReader_float2 type, but got {}", prim_part, pshader->info_id));
  }

  // Get value producing attribute(i.e, follow .connection and return
  // terminal Attribute value)
  value::token varname;
  if (!tydra::EvaluateShaderAttribute(stage, *pshader, "inputs:varname",
                                      &varname, &err)) {
    return nonstd::make_unexpected(
        fmt::format("Failed to evaluate UsdPrimvarReader_float2's "
                    "inputs:varname: {}\n",
                    err));
  }

  // Build transform matrix.
  // https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_texture_transform
  // Since USD uses post-multiply,
  //
  // matrix = scale * rotate * translate
  //
  {
    mat3 s;
    s.set_scale(scale[0], scale[1], 1.0f);

    mat3 r = mat3::identity();

    r.m[0][0] = std::cos(math::radian(rotation));
    r.m[0][1] = std::sin(math::radian(rotation));

    r.m[1][0] = -std::sin(math::radian(rotation));
    r.m[1][1] = std::cos(math::radian(rotation));

    mat3 t = mat3::identity();
    t.set_translation(translation[0], translation[1], 1.0f);

    tex_out->transform = s * r * t;
  }

  tex_out->tx_rotation = rotation;
  tex_out->tx_translation = translation;
  tex_out->tx_scale = scale;
  tex_out->has_transform2d = true;

  tex_out->varname_uv = varname.str();

  return true;
}
#endif

#if 1

// W.I.P.
// Convert UsdUVTexture shader node.
// @return true upon conversion success(textures.back() contains the converted UVTexture)
//
// Possible network configuration
//
// - UsdUVTexture -> UsdPrimvarReader
// - UsdUVTexture -> UsdTransform2d -> UsdPrimvarReader
nonstd::expected<bool, std::string> ConvertUVTexture(
    const Stage &stage, const Path &tex_abs_path, const UsdUVTexture &texture,
    TextureImageLoaderFunction textureLoaderFunction,
    void *textureLoaderUserData,
    StringAndIdMap &textureMap,          // [inout]
    StringAndIdMap &imageMap,            // [inout]
    StringAndIdMap &bufferMap,           // [inout]
    std::vector<UVTexture> &textures,    // [inout]
    std::vector<TextureImage> &images,   // [inout]
    std::vector<BufferData> &buffers) {  // [inout]
  (void)tex_abs_path;
  (void)textureMap;
  (void)imageMap;
  (void)bufferMap;
  (void)textures;
  (void)images;
  (void)buffers;

  std::string err;

  UVTexture tex;

  AssetInfo assetInfo = texture.meta.get_assetInfo();

  // First load texture file.
  if (!texture.file.authored()) {
    return nonstd::make_unexpected(fmt::format("`asset:file` is not authored. Path = {}", tex_abs_path.prim_part()));
  }

  value::AssetPath assetPath;
  if (auto apath = texture.file.get_value()) {
    if (!apath.value().get_scalar(&assetPath)) {
      return nonstd::make_unexpected(fmt::format("Failed to get `asset:file` value from Path {} (Maybe `asset:file` is timeSample value?)", tex_abs_path.prim_part()));
    }
  } else {
    return nonstd::make_unexpected(fmt::format("Failed to get `asset:file` value from Path {}", tex_abs_path.prim_part()));
  }

  TextureImage texImage;
  BufferData imageBuffer;
  // Texel data is treated as byte array
  imageBuffer.componentType = ComponentType::UInt8;
  imageBuffer.count = 1;

  std::string warn;
  bool tex_ok = textureLoaderFunction(assetPath, assetInfo, &texImage, &imageBuffer.data, textureLoaderUserData, &warn, &err);

  if (!tex_ok) {
    return nonstd::make_unexpected("Failed to load texture image: " + err + "\n");
  }

  if (warn.size()) {
    DCOUT("WARN: " << warn);
    // TODO: propagate warnining message
  }

  // TODO: Share image data as much as possible.
  // e.g. Texture A and B uses same image file, but texturing parameter is different.
  buffers.emplace_back(imageBuffer);

  // Overwrite colorSpace
  if (texture.sourceColorSpace.authored()) {
    UsdUVTexture::SourceColorSpace cs;
    if (texture.sourceColorSpace.get_value().get_scalar(&cs)) {
      if (cs == UsdUVTexture::SourceColorSpace::SRGB) {
        texImage.colorSpace = tydra::ColorSpace::sRGB;
      } else if (cs == UsdUVTexture::SourceColorSpace::Raw) {
        texImage.colorSpace = tydra::ColorSpace::Linear;
      } else if (cs == UsdUVTexture::SourceColorSpace::Auto) {
        // TODO: Read colorspace from a file.
        if ((texImage.texelComponentType == ComponentType::UInt8) ||
            (texImage.texelComponentType == ComponentType::Int8)) {
          texImage.colorSpace = tydra::ColorSpace::sRGB;
        } else {
          texImage.colorSpace = tydra::ColorSpace::Linear;
        }
      }
    }
  }

  if (texture.bias.authored()) {
    tex.bias = texture.bias.get_value();
  }

  if (texture.scale.authored()) {
    tex.scale = texture.scale.get_value();
  }

  if (texture.st.authored()) {
    if (texture.st.is_connection()) {
      const auto &paths = texture.st.get_connections();
      if (paths.size() != 1) {
        return nonstd::make_unexpected(
            "UsdUVTexture inputs:st connection must be single Path.");
      }
      const Path &path = paths[0];

      const Prim *readerPrim{nullptr};
      if (!stage.find_prim_at_path(Path(path.prim_part(), ""), readerPrim,
                                   &err)) {
        return nonstd::make_unexpected(
            "UsdUVTexture inputs:st connection targetPath not found in the "
            "Stage: " +
            err + "\n");
      }

      if (!readerPrim) {
        return nonstd::make_unexpected(
            "[InternlError] Invalid Prim connected to inputs:st\n");
      }

      const Shader *pshader = readerPrim->as<Shader>();
      if (!pshader) {
        return nonstd::make_unexpected(
            fmt::format("UsdUVTexture inputs:st connected Prim must be "
                        "Shader Prim, but got {} Prim\n",
                        readerPrim->prim_type_name()));
      }

      // currently UsdTranform2d or PrimvarReaer_float2 only for inputs:st
      if (const UsdPrimvarReader_float2 *preader = pshader->value.as<UsdPrimvarReader_float2>()) {
        if (!preader) {
          return nonstd::make_unexpected(
              fmt::format("Shader's info:id must be UsdPrimvarReader_float2, "
                          "but got {}\n",
                          pshader->info_id));
        }

        // Get value producing attribute(i.e, follow .connection and return
        // terminal Attribute value)
        value::token varname;
        if (!tydra::EvaluateShaderAttribute(stage, *pshader, "inputs:varname",
                                            &varname, &err)) {
          return nonstd::make_unexpected(
              fmt::format("Failed to evaluate UsdPrimvarReader_float2's "
                          "inputs:varname: {}\n",
                          err));
        }

        tex.varname_uv = varname.str();
      } else if (const UsdTransform2d *ptransform = pshader->value.as<UsdTransform2d>()) {

        auto result = ConvertTexTransform2d(stage, path, *ptransform, &tex);
        if (!result) {
          return nonstd::make_unexpected(result.error());
        }
      } else {
        return nonstd::make_unexpected("Unsupported Shader type for `inputs:st` connection: " + pshader->info_id + "\n");
      }

    } else {
      Animatable<value::texcoord2f> fallbacks = texture.st.get_value();
      value::texcoord2f uv;
      if (fallbacks.get_scalar(&uv)) {
        tex.fallback_uv[0] = uv[0];
        tex.fallback_uv[1] = uv[1];
      } else {
        // TODO: report warning.
      }
    }
  }

  if (texture.wrapS.authored()) {
    tinyusdz::UsdUVTexture::Wrap wrap;

    if (!texture.wrapS.get_value().get_scalar(&wrap)) {
      return nonstd::make_unexpected(
          "Invalid UsdUVTexture inputs:wrapS value.");
    }

    if (wrap == UsdUVTexture::Wrap::Repeat) {
      tex.wrapS = UVTexture::WrapMode::REPEAT;
    } else if (wrap == UsdUVTexture::Wrap::Mirror) {
      tex.wrapS = UVTexture::WrapMode::MIRROR;
    } else if (wrap == UsdUVTexture::Wrap::Clamp) {
      tex.wrapS = UVTexture::WrapMode::CLAMP_TO_EDGE;
    } else if (wrap == UsdUVTexture::Wrap::Black) {
      tex.wrapS = UVTexture::WrapMode::CLAMP_TO_BORDER;
    } else {
      tex.wrapS = UVTexture::WrapMode::CLAMP_TO_EDGE;
    }
  }

  if (texture.wrapT.authored()) {
    tinyusdz::UsdUVTexture::Wrap wrap;

    if (!texture.wrapT.get_value().get_scalar(&wrap)) {
      return nonstd::make_unexpected(
          "Invalid UsdUVTexture inputs:wrapT value.");
    }

    if (wrap == UsdUVTexture::Wrap::Repeat) {
      tex.wrapT = UVTexture::WrapMode::REPEAT;
    } else if (wrap == UsdUVTexture::Wrap::Mirror) {
      tex.wrapT = UVTexture::WrapMode::MIRROR;
    } else if (wrap == UsdUVTexture::Wrap::Clamp) {
      tex.wrapT = UVTexture::WrapMode::CLAMP_TO_EDGE;
    } else if (wrap == UsdUVTexture::Wrap::Black) {
      tex.wrapT = UVTexture::WrapMode::CLAMP_TO_BORDER;
    } else {
      tex.wrapT = UVTexture::WrapMode::CLAMP_TO_EDGE;
    }
  }


  return false;
}
#endif

template<typename T>
nonstd::expected<bool, std::string> GetConnectedUVTexture(
  const Stage &stage,
  const TypedAnimatableAttributeWithFallback<T> &src,
  Path *tex_abs_path,
  const UsdUVTexture **dst){

  if (!dst) {
    return nonstd::make_unexpected("[InternalError] dst is nullptr.\n");
  }

  if (!src.is_connection()) {
    return nonstd::make_unexpected("Attribute must be connection.\n");
  }

  if (src.get_connections().size() != 1) {
    return nonstd::make_unexpected("Attribute connections must be single connection Path.\n");
  }

  const Path &path = src.get_connections()[0];

  const std::string prim_part = path.prim_part();
  const std::string prop_part = path.prop_part();

  if (prop_part != "outputs:result") {
    return nonstd::make_unexpected("connection Path's property part must be `outputs:result` for UsdUVTexture.\n");
  }

  const Prim *prim{nullptr};
  std::string err;
  if (stage.find_prim_at_path(Path(prim_part, ""), prim, &err)) {
    return nonstd::make_unexpected(fmt::format("Prim {} not found in the Stage: {}\n", prim_part, err));
  }

  if (!prim) {
    return nonstd::make_unexpected("[InternalError] Prim ptr is null.\n");
  }

  if (const Shader *pshader = prim->as<Shader>()) {
    if (const UsdUVTexture *ptex = pshader->value.as<UsdUVTexture>()) {
      (*dst) = ptex;
      return true;
    }
  }

  if (tex_abs_path) {
    (*tex_abs_path) = Path(prim_part, "");
  }

  return nonstd::make_unexpected(fmt::format("Prim {} must be Shader, but got {}", prim_part, prim->prim_type_name()));

}

// TODO: timeSamples
nonstd::expected<bool, std::string> ConvertPreviewSurfaceShader(
    const Stage &stage, const Path &shader_abs_path,
    const UsdPreviewSurface &shader,
    TextureImageLoaderFunction textureLoaderFunction,
    void *textureLoaderUserData,
    StringAndIdMap &textureMap,          // [inout]
    StringAndIdMap &imageMap,            // [inout]
    StringAndIdMap &bufferMap,           // [inout]
    std::vector<UVTexture> &textures,    // [inout]
    std::vector<TextureImage> &images,   // [inout]
    std::vector<BufferData> &buffers) {  // [inout]

  (void)shader_abs_path;
  (void)imageMap;
  (void)bufferMap;
  (void)images;

  PreviewSurfaceShader rshader;


  if (shader.diffuseColor.authored()) {
    if (shader.diffuseColor.is_blocked()) {
      return nonstd::make_unexpected("diffuseColor attribute is blocked.\n");
    } else if (shader.diffuseColor.is_connection()) {
      UVTexture rtex;

      const UsdUVTexture *ptex{nullptr};
      Path texPath;
      auto result = GetConnectedUVTexture(stage, shader.diffuseColor, &texPath, &ptex);

      if (result) {

        auto texResult = ConvertUVTexture(stage, texPath, *ptex,
            textureLoaderFunction, textureLoaderUserData,
            textureMap, imageMap, bufferMap,
            textures, images, buffers);

        // textures.back() is newly added UVTexture, so subtract 1.
        rshader.diffuseColor.textureId = int(textures.size() - 1);
      }

      //textures.emplace_back(rtex);

      textureMap.add(uint64_t(rshader.diffuseColor.textureId), shader_abs_path.prim_part() + ".dffuseColor");


    } else {
      value::color3f col;
      if (!shader.diffuseColor.get_value().get_scalar(&col)) {
        return nonstd::make_unexpected("Failed to get diffuseColor at `default` timecode.\n");
      }

      rshader.diffuseColor.value[0] = col[0];
      rshader.diffuseColor.value[1] = col[1];
      rshader.diffuseColor.value[2] = col[2];

    }
  }

  return false;
}

}  // namespace

// W.I.P.
nonstd::expected<bool, std::string> ConvertMaterial(
  const MaterialConverterConfig &config,
    const Stage &stage, const Path &mat_abs_path,
    const tinyusdz::Material &material,
    StringAndIdMap &materialMap,             // [inout]
    StringAndIdMap &textureMap,              // [inout]
    StringAndIdMap &imageMap,                // [inout]
    StringAndIdMap &bufferMap,               // [inout]
    std::vector<RenderMaterial> &materials,  // [input]
    std::vector<UVTexture> &textures,        // [inout]
    std::vector<TextureImage> &images,       // [inout]
    std::vector<BufferData> &buffers) {
  (void)stage;
  (void)material;
  (void)materialMap;
  (void)textureMap;
  (void)imageMap;
  (void)bufferMap;
  (void)materials;
  (void)textures;
  (void)images;
  (void)buffers;
  (void)config;

  std::string err;

  Path surfacePath;

  //
  // surface shader
  {
    if (material.surface.authored()) {
      auto paths = material.surface.get_connections();
      // must have single targetPath.
      if (paths.size() != 0) {
        return nonstd::make_unexpected(
            fmt::format("{}'s outputs:surface must be connection with single "
                        "target Path.\n",
                        mat_abs_path.full_path_name()));
      }
      surfacePath = paths[0];
    } else {
      return nonstd::make_unexpected(
          fmt::format("{}'s outputs:surface isn't authored.\n",
                      mat_abs_path.full_path_name()));
    }

    const Prim *shaderPrim{nullptr};
    if (!stage.find_prim_at_path(
            Path(surfacePath.prim_part(), /* prop part */ ""), shaderPrim,
            &err)) {
      return nonstd::make_unexpected(fmt::format(
          "{}'s outputs:surface isn't connected to exising Prim path.\n",
          mat_abs_path.full_path_name()));
    }

    if (!shaderPrim) {
      // this should not happen though.
      return nonstd::make_unexpected("[InternalError] invalid Shader Prim.\n");
    }

    const Shader *shader = shaderPrim->as<Shader>();

    if (!shader) {
      return nonstd::make_unexpected(
          fmt::format("{}'s outputs:surface must be connected to Shader Prim, "
                      "but connected to `{}` Prim.\n",
                      shaderPrim->prim_type_name()));
    }

    // Currently must be UsdPreviewSurface
    const UsdPreviewSurface *psurface = shader->value.as<UsdPreviewSurface>();
    if (!psurface) {
      return nonstd::make_unexpected(
          fmt::format("Shader's info:id must be UsdPreviewSurface, but got {}",
                      shader->info_id));
    }

    // prop part must be `outputs:surface` for now.
    if (surfacePath.prop_part() != "outputs:surface") {
      return nonstd::make_unexpected(
          fmt::format("{}'s outputs:surface connection must point to property "
                      "`outputs:surface`, but got `{}`",
                      mat_abs_path.full_path_name(), surfacePath.prop_part()));
    }

    auto result = ConvertPreviewSurfaceShader(
      stage, surfacePath, *psurface,
      config.texture_image_loader_function,
      config.texture_image_loader_function_userdata,
   textureMap,          // [inout]
   imageMap,            // [inout]
   bufferMap,           // [inout]
    textures,    // [inout]
    images,   // [inout]
    buffers);  // [inout]

  }

#if 0

  // TODO: GeomMesh(per-face material)

  // Env
  struct UserData {
    const Stage *pstage{nullptr};
    StringAndIdMap *pmaterialMap{nullptr};
    std::vector<RenderMaterial> *pmaterials{nullptr};
  };

  // 1. Visit GeomMesh
  // 2. If the mesh has bound material
  //   1. Create Material

  // TODO:
  auto mesh_visit = [](const tinyusdz::Path &abs_path,
                       const tinyusdz::Prim &prim, const int32_t level,
                       void *userdata, std::string *err) -> bool {
    if (level > 1024 * 1024) {
      if (err) {
        (*err) += "Scene graph is too deep.\n";
      }
      // Too deep
      return false;
    }

    if (!userdata) {
      return false;
    }

    UserData *puser = reinterpret_cast<UserData *>(userdata);

    if (const tinyusdz::GeomMesh *pmesh = prim.as<tinyusdz::GeomMesh>()) {
      DCOUT("Material: " << abs_path);

      const std::string path_str = abs_path.full_path_name();
      const auto matIt = puser->pmaterialMap->find(path_str);

      const RenderMaterial *render_material{nullptr};
      if (matIt != puser->pmaterialMap->s_end()) {
        // Got material
        uint64_t mat_id = matIt->second;
        if (mat_id >=
            puser->pmaterials->size()) {  // this should not happen though
          if (err) {
            (*err) += "Material index out-of-range.\n";
          }
          return false;
        }
        render_material = &(*puser->pmaterials)[size_t(mat_id)];

      } else {
        // Assign new material ID
        uint64_t mat_id = puser->pmaterials->size();
        puser->pmaterialMap->add(path_str, mat_id);

        puser->pmaterials->push_back(RenderMaterial());

        render_material = &(*puser->pmaterials)[size_t(mat_id)];
      }

      if (!render_material) {
        if (err) {
          (*err) += "[InternalError] render_material ptr is null.\n";
        }
        return false;
      }

      tinyusdz::Path bound_material_path;
      const tinyusdz::Material *bound_material{nullptr};
      bool ret = tinyusdz::tydra::FindBoundMaterial(
          *puser->pstage, /* GeomMesh prim path */ abs_path, /* suffix */ "",
          &bound_material_path, &bound_material, err);

      if (ret && bound_material) {
        DCOUT("Bound material path: " << bound_material_path);
        // TODO
      }
    }

    return true;  // continue traversal
  };

  UserData uenv;
  uenv.pstage = &stage;
  uenv.pmaterialMap = &materialMap;
  uenv.pmaterials = &materials;

  tydra::VisitPrims(stage, mesh_visit, &uenv);
#endif

  return false;  // TODO
}

// W.I.P.
bool ConvertToRenderScene(const Stage &stage, RenderScene *scene,
                          std::string *warn, std::string *err_out) {
  (void)warn;

  if (!scene) {
    if (err_out) {
      (*err_out) += "nullptr for RenderScene argument.\n";
    }
    return false;
  }

  // Build Xform at default time.
  XformNode xform_node;
  if (!BuildXformNodeFromStage(stage, &xform_node)) {
    if (err_out) {
      (*err_out) += "Failed to build Xform node hierarchy.\n";
    }
  }

  StringAndIdMap materialMap;
  StringAndIdMap textureMap;
  StringAndIdMap imageMap;
  StringAndIdMap bufferMap;

  RenderScene render_scene;

  // Env
  struct UserData {
    const Stage *pstage{nullptr};
    StringAndIdMap *pmaterialMap{nullptr};
    StringAndIdMap *ptextureMap{nullptr};
    StringAndIdMap *pimageMap{nullptr};
    StringAndIdMap *pbufferMap{nullptr};
    // std::vector<RenderMaterial> *pmaterials{nullptr};
    RenderScene *prenderscene{nullptr};
  };

  // 1. Visit GeomMesh
  // 2. If the mesh has bound material
  //   1. Create Material

  auto mesh_visit = [](const tinyusdz::Path &abs_path,
                       const tinyusdz::Prim &prim, const int32_t level,
                       void *userdata, std::string *err) -> bool {
    if (level > 1024 * 1024) {
      if (err) {
        (*err) += "Scene graph is too deep.\n";
      }
      // Too deep
      return false;
    }

    if (!userdata) {
      return false;
    }

    UserData *puser = reinterpret_cast<UserData *>(userdata);

    if (const tinyusdz::GeomMesh *pmesh = prim.as<tinyusdz::GeomMesh>()) {
      DCOUT("Material: " << abs_path);

      const std::string path_str = abs_path.full_path_name();
      const auto matIt = puser->pmaterialMap->find(path_str);

      const RenderMaterial *render_material{nullptr};

      std::vector<RenderMaterial> &rmaterials = puser->prenderscene->materials;

      if (matIt != puser->pmaterialMap->s_end()) {
        // Got material
        uint64_t mat_id = matIt->second;
        if (mat_id >= puser->prenderscene->materials
                          .size()) {  // this should not happen though
          if (err) {
            (*err) += "Material index out-of-range.\n";
          }
          return false;
        }
        render_material = &(puser->prenderscene->materials[size_t(mat_id)]);

      } else {
        // Assign new material ID
        uint64_t mat_id = rmaterials.size();
        puser->pmaterialMap->add(path_str, mat_id);

        rmaterials.push_back(RenderMaterial());

        render_material = &(rmaterials[size_t(mat_id)]);
      }

      if (!render_material) {
        if (err) {
          (*err) += "[InternalError] render_material ptr is null.\n";
        }
        return false;
      }

      tinyusdz::Path bound_material_path;
      const tinyusdz::Material *bound_material{nullptr};
      bool ret = tinyusdz::tydra::FindBoundMaterial(
          *puser->pstage, /* GeomMesh prim path */ abs_path, /* suffix */ "",
          &bound_material_path, &bound_material, err);

      RenderScene *prenderscene = puser->prenderscene;

      // TODO: MaterialConverterConfig
      MaterialConverterConfig material_converter_config;

      if (ret && bound_material) {
        DCOUT("Bound material path: " << bound_material_path);

        auto result = ConvertMaterial(
            material_converter_config,
            *puser->pstage, bound_material_path, *bound_material, *puser->pmaterialMap,
            *puser->ptextureMap, *puser->pimageMap, *puser->pbufferMap,
            prenderscene->materials, prenderscene->textures,
            prenderscene->images, prenderscene->buffers);

        if (!result) {
          if (err) {
            // TODO: More expressive error report.
            (*err) += "Material conversion failed: " + result.error() + "\n";
          }
          return false;
        }
      }
    }

    return true;  // continue traversal
  };

  UserData uenv;
  uenv.pstage = &stage;
  uenv.pmaterialMap = &materialMap;
  uenv.ptextureMap = &textureMap;
  uenv.pimageMap = &imageMap;
  uenv.pbufferMap = &bufferMap;
  uenv.prenderscene = &render_scene;

  tydra::VisitPrims(stage, mesh_visit, &uenv);

  return false;  // TODO
}

}  // namespace tydra
}  // namespace tinyusdz
