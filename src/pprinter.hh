#pragma once

//
// pretty-print routine(using iostream) in non-intrusive way.
// Some build configuration may not want I/O module(e.g. mobile/embedded
// device), so provide print routines in separated file.
//
//

#include <ostream>
#include <sstream>
#include <string>

// Sub-headers (pprint-meta.hh transitively includes pprint-enum.hh)
#include "pprint-meta.hh"

// Domain headers (for prim-type to_string declarations)
#include "usdGeom.hh"
#include "usdLux.hh"
#include "usdShade.hh"
#include "usdSkel.hh"

namespace tinyusdz {

//
// Setting `closing_brace` false won't emit `}`(for printing USD scene graph
// recursively).
//

// Geom prim to_string (defined in pprint-geom.cc)
std::string to_string(const Model &model, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const Scope &scope, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const GPrim &gprim, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const Xform &xform, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const GeomSphere &sphere, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const GeomMesh &mesh, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const GeomPoints &pts, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const GeomBasisCurves &curves, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const GeomNurbsCurves &curves, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const GeomCapsule &geom, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const GeomCone &geom, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const GeomCylinder &geom, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const GeomCube &geom, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const GeomCamera &camera, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const GeomSubset &subset, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const GeomSubset::ElementType ty);
std::string to_string(const GeomSubset::FamilyType ty);
std::string to_string(const GeomBasisCurves::Wrap &v);
std::string to_string(const GeomBasisCurves::Type &v);
std::string to_string(const GeomBasisCurves::Basis &v);
std::string to_string(const GeomPointInstancer &instancer,
                      const uint32_t indent = 0, bool closing_brace = true);

std::string to_string(const GeomMesh::InterpolateBoundary interp_boundary);
std::string to_string(const GeomMesh::SubdivisionScheme subd_scheme);
std::string to_string(const GeomMesh::FaceVaryingLinearInterpolation fv);

std::string to_string(const GeomCamera::Projection &proj);
std::string to_string(const GeomCamera::StereoRole &role);

// Skel prim to_string (defined in pprint-skel.cc)
std::string to_string(const SkelRoot &root, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const Skeleton &skel, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const SkelAnimation &anim, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const BlendShape &bs, const uint32_t indent = 0,
                      bool closing_brace = true);

// Light prim to_string (defined in pprint-light.cc)
std::string to_string(const SphereLight &light, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const DomeLight &light, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const DiskLight &light, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const DistantLight &light, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const CylinderLight &light, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const RectLight &light, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const GeometryLight &light, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const PortalLight &light, const uint32_t indent = 0,
                      bool closing_brace = true);

std::string to_string(const DomeLight::TextureFormat &texformat);

// Shader/Material to_string (defined in pprint-shader.cc)
std::string to_string(const Material &material, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const NodeGraph &nodegraph, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const Shader &shader, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const UsdPreviewSurface &shader,
                      const uint32_t indent = 0, bool closing_brace = true);
std::string to_string(const UsdUVTexture &shader, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const UsdPrimvarReader_float2 &shader,
                      const uint32_t indent = 0, bool closing_brace = true);

std::string to_string(const UsdPreviewSurface::OpacityMode v);
std::string to_string(const UsdUVTexture::SourceColorSpace v);
std::string to_string(const UsdUVTexture::Wrap v);

// Layer printing (defined in pprinter.cc)
std::string print_layer_metas(const LayerMetas &metas, const uint32_t indent);
std::string print_layer(const Layer &layer, const uint32_t indent,
                        bool parallel = false);

std::string to_string(const Layer &layer, const uint32_t indent = 0,
                      bool closing_brace = true);
std::string to_string(const PrimSpec &primspec, const uint32_t indent = 0,
                      bool closing_brace = true);

}  // namespace tinyusdz
