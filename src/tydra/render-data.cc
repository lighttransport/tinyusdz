#include "render-data.hh"

#include "prim-types.hh"
#include "pprinter.hh"

namespace tinyusdz {
namespace tydra {

namespace {

template<typename T>
inline T Get(const nonstd::optional<T> &nv, const T &default_value) {
  if (nv) {
    return nv.value();
  }
  return default_value;
}

} // namespace

nonstd::expected<RenderMesh, std::string> Convert(const GeomMesh &mesh) {

  RenderMesh dst;

  {
    dst.points.resize(mesh.points.size());
    memcpy(dst.points.data(), mesh.points.data(), sizeof(value::float3) * mesh.points.size());
  }

  // normals
  // Both `primvar::normals` and `normals` are defined, `primvar::normals` is used.
  // Do not compute smooth normals when neither `primvar::normals` nor `normals` defined(Only faceted normals are computed).
  auto GetNormals = [](const GeomMesh &m) -> nonstd::optional<PrimAttrib> {

    constexpr auto kPrimvarNormals = "primvar::normals";

    if (m.props.count(kPrimvarNormals)) {
      const auto prop = m.props.at(kPrimvarNormals);
      if (prop.IsRel()) {
        // TODO:
        return nonstd::nullopt;
      }

      return prop.attrib;
    }

    if (m.normals) {
      return m.normals.value();
    }

    return nonstd::nullopt;
  };

  if (auto nv = GetNormals(mesh)) {
    auto normalsAttr = nv.value();

    // normal3f
    const std::vector<value::normal3f> *normals = normalsAttr.var.as<std::vector<value::normal3f>>();
    if (normals == nullptr) {
      return nonstd::make_unexpected("`normal3f[]` type expected for `normals` attribute, but got `" + normalsAttr.var.type_name() + "`.\n");
    }

    auto interp = Get(normalsAttr.meta.interpolation, Interpolation::Vertex); // default 'vertex'

    if (interp == Interpolation::Vertex) {
      return nonstd::make_unexpected("TODO: `vertex` interpolation for `normals` attribute.\n");
    } else if (interp == Interpolation::FaceVarying) {
      dst.facevaryingNormals.resize(normals->size());
      memcpy(dst.facevaryingNormals.data(), normals, sizeof(value::normal3f) * normals->size());
    } else {
     return nonstd::make_unexpected("Unsupported/unimplemented interpolation for `normals` attribute: " + to_string(interp) + ".\n");
    }
  }

  return dst;
}


} // namespace tydra
} // namespace tinyusdz
