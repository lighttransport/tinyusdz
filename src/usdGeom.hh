// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
//
// UsdGeom
#pragma once

#include "prim-types.hh"
#include "value-types.hh"

namespace tinyusdz {

constexpr auto kGPrim = "GPrim";
constexpr auto kGeomCube = "Cube";
constexpr auto kGeomXform = "Xform";
constexpr auto kGeomMesh = "Mesh";
constexpr auto kGeomSubset = "GeomSubset";
constexpr auto kGeomBasisCurves = "BasisCurves";
constexpr auto kGeomCylinder = "Cylinder";
constexpr auto kGeomCapsule = "Capsule";
constexpr auto kGeomCone = "Cone";
constexpr auto kGeomSphere = "Sphere";
constexpr auto kGeomCamera = "Camera";

// Generic Prim
struct GPrim {
  std::string name;

  int64_t parent_id{-1};  // Index to parent node

  std::string prim_type;  // Primitive type(if specified by `def`)

  // Gprim

  // nonstd::nullopt = not authorized.
  nonstd::optional<AnimatableExtent> extent;  // bounding extent. When authorized, the extent is the bounding box of whole its children.

  AttribWithFallback<bool> doubleSided{false};

  AttribWithFallback<Orientation> orientation{Orientation::RightHanded};
  AttribWithFallback<AnimatableVisibility> visibility{Visibility::Inherited};
  AttribWithFallback<Purpose> purpose{Purpose::Default};

  nonstd::optional<AnimatableVec3fArray> displayColor;    // primvars:displayColor
  nonstd::optional<AnimatableFloatArray> displayOpacity;  // primvars:displaOpacity

  nonstd::optional<Relation> proxyPrim;
  nonstd::optional<MaterialBindingAPI> materialBinding;
  std::vector<value::token> xformOpOrder;

  std::map<std::string, Property> props;

  std::map<std::string, value::Value> args;

  bool _valid{true};  // default behavior is valid(allow empty GPrim)

  bool active{true};

  // child nodes
  std::vector<GPrim> children;
};

struct Xform : GPrim {

  std::vector<XformOp> xformOps;

  Xform() {}

  ///
  /// Evaluate XformOps
  ///
  bool EvaluateXformOps(value::matrix4d *out_matrix) const;

  ///
  /// Get concatenated matrix.
  ///
  nonstd::optional<value::matrix4d> GetMatrix() const {
    if (_dirty) {
      value::matrix4d m;
      if (EvaluateXformOps(&m)) {
        _matrix = m;
        _dirty = false;
      } else {
        // TODO: Report an error.
        return nonstd::nullopt;
      }
    }
    return _matrix;
  }

  mutable bool _dirty{true};
  mutable value::matrix4d _matrix;  // Resulting matrix of evaluated XformOps.
};

// GeomSubset
struct GeomSubset {
  enum class ElementType { Face };

  enum class FamilyType {
    Partition,       // 'partition'
    NonOverlapping,  // 'nonOverlapping'
    Unrestricted,    // 'unrestricted' (fallback)
  };

  std::string name;

  int64_t parent_id{-1};  // Index to parent node

  ElementType elementType{ElementType::Face};  // must be face
  FamilyType familyType{FamilyType::Unrestricted};

  nonstd::expected<bool, std::string> SetElementType(const std::string &str) {
    if (str == "face") {
      elementType = ElementType::Face;
      return true;
    }

    return nonstd::make_unexpected(
        "Only `face` is supported for `elementType`, but `" + str +
        "` specified");
  }

  nonstd::expected<bool, std::string> SetFamilyType(const std::string &str) {
    if (str == "partition") {
      familyType = FamilyType::Partition;
      return true;
    } else if (str == "nonOverlapping") {
      familyType = FamilyType::NonOverlapping;
      return true;
    } else if (str == "unrestricted") {
      familyType = FamilyType::Unrestricted;
      return true;
    }

    return nonstd::make_unexpected("Invalid `familyType` specified: `" + str +
                                   "`.");
  }

  std::vector<uint32_t> indices;

  std::map<std::string, Property> props;  // custom Properties
};

// Polygon mesh geometry
struct GeomMesh : GPrim {
  enum class InterpolateBoundary {
    None, // "none"
    EdgeAndCorner, // "edgeAndCorner"
    EdgeOnly // "edgeOnly"
  };

  enum class FacevaryingLinearInterpolation {
    CornersPlus1, // "cornersPlus1"
    CornersPlus2,  // "cornersPlus2"
    CornersOnly, // "cornersOnly"
    Boundaries, // "boundaries"
    None, // "none"
    All, // "all"
  };

  enum class SubdivisionScheme {
    CatmullClark,  // "catmullClark"
    Loop, // "loop"
    Bilinear, // "bilinear"
    None, // "none"
  };


  //
  // Predefined attribs.
  //
  TypedAttribute<std::vector<value::point3f>> points;    // point3f[]
  TypedAttribute<std::vector<value::normal3f>> normals;  // normal3f[] (NOTE: "primvars:normals" are stored in `GPrim::props`)

  TypedAttribute<std::vector<value::vector3f>> velocities; // vector3f[]

  TypedAttribute<std::vector<int32_t>> faceVertexCounts;
  TypedAttribute<std::vector<int32_t>> faceVertexIndices;

  //
  // Utility functions
  //

  // Initialize GeomMesh by GPrim(prepend references)
  void Initialize(const GPrim &pprim);

  // Update GeomMesh by GPrim(append references)
  void UpdateBy(const GPrim &pprim);

  ///
  /// @brief Returns normals vector. Precedence order: `primvars:normals` then
  /// `normals`.
  ///
  /// @return normals vector(copied). Returns empty normals vector when neither
  /// `primvars:normals` nor `normals` attribute defined, attribute is a
  /// relation or normals attribute have invalid type(other than `normal3f`).
  ///
  std::vector<value::normal3f> GetNormals() const;

  ///
  /// @brief Get interpolation of normals.
  /// @return Interpolation of normals. `vertex` by defaut.
  ///
  Interpolation GetNormalsInterpolation() const;

  //
  // SubD attribs.
  //
  TypedAttribute<std::vector<int32_t>> cornerIndices;
  TypedAttribute<std::vector<float>> cornerSharpnesses;
  TypedAttribute<std::vector<int32_t>> creaseIndices;
  TypedAttribute<std::vector<int32_t>> creaseLengths;
  TypedAttribute<std::vector<float>> creaseSharpnesses;
  TypedAttribute<std::vector<int32_t>> holeIndices;
  AttribWithFallback<InterpolateBoundary> interpolateBoundary{InterpolateBoundary::EdgeAndCorner};
  AttribWithFallback<SubdivisionScheme> subdivisionScheme{SubdivisionScheme::CatmullClark};
  AttribWithFallback<FacevaryingLinearInterpolation> facevaryingLinearInterpolation{FacevaryingLinearInterpolation::CornersPlus1};

  //
  // GeomSubset
  //
  // uniform token `subsetFamily:materialBind:familyType`
  GeomSubset::FamilyType materialBindFamilyType{
      GeomSubset::FamilyType::Partition};

  std::vector<GeomSubset> geom_subset_children;

  ///
  /// Validate GeomSubset data whose are attached to this GeomMesh.
  ///
  nonstd::expected<bool, std::string> ValidateGeomSubset();

};

struct GeomCamera : public GPrim {

  enum class Projection {
    perspective,   // "perspective"
    orthographic,  // "orthographic"
  };

  enum class StereoRole {
    mono,   // "mono"
    left,   // "left"
    right,  // "right"
  };

  //
  // Properties
  //

  // TODO: Animatable?
  std::vector<value::float4> clippingPlanes;
  value::float2 clippingRange{{0.1f, 1000000.0f}};
  float exposure{0.0f};  // in EV
  float focalLength{50.0f};
  float focusDistance{0.0f};
  float horizontalAperture{20.965f};
  float horizontalApertureOffset{0.0f};
  float verticalAperture{15.2908f};
  float verticalApertureOffset{0.0f};
  float fStop{0.0f};  // 0.0 = no focusing
  Projection projection;

  float shutterClose = 0.0f;  // shutter:close
  float shutterOpen = 0.0f;   // shutter:open


};

struct GeomBoundable : GPrim {
};

struct GeomCone : public GPrim {


  //
  // Properties
  //
  AnimatableDouble height{2.0};
  AnimatableDouble radius{1.0};
  Axis axis{Axis::Z};


};

struct GeomCapsule : public GPrim {

  //
  // Properties
  //
  AnimatableDouble height{2.0};
  AnimatableDouble radius{0.5};
  Axis axis{Axis::Z};


};

struct GeomCylinder : public GPrim {

  //
  // Properties
  //
  AnimatableDouble height{2.0};
  AnimatableDouble radius{1.0};
  Axis axis{Axis::Z};

};

struct GeomCube : public GPrim {

  //
  // Properties
  //
  AnimatableDouble size{2.0};

};

struct GeomSphere : public GPrim {

  //
  // Predefined attribs.
  //
  AnimatableDouble radius{1.0};

};

//
// Basis Curves(for hair/fur)
//
struct GeomBasisCurves : public GPrim {

  enum class Type {
    Cubic, // "cubic"(default)
    Linear, // "linear"
  };

  enum class Basis {
    Bezier, // "bezier"(default)
    Bspline, // "bspline"
    CatmullRom, // "catmullRom"
  };

  enum class Wrap {
    Nonperiodic, // "nonperiodic"(default)
    Periodic, // "periodic"
    Pinned, // "pinned"
  };

  nonstd::optional<Type> type;
  nonstd::optional<Basis> basis;
  nonstd::optional<Wrap> wrap;

  //
  // Predefined attribs.
  //
  TypedAttribute<std::vector<value::point3f>> points;    // point3f
  TypedAttribute<std::vector<value::normal3f>> normals;  // normal3f
  TypedAttribute<std::vector<int>> curveVertexCounts;
  TypedAttribute<std::vector<float>> widths;
  TypedAttribute<std::vector<value::vector3f>> velocities;     // vector3f
  TypedAttribute<std::vector<value::vector3f>> accelerations;  // vector3f


};

//
// Points primitive.
//
struct GeomPoints : public GPrim {

  //
  // Predefined attribs.
  //
  TypedAttribute<std::vector<value::point3f>> points;   // point3f
  TypedAttribute<std::vector<value::normal3f>> normals;  // normal3f
  TypedAttribute<std::vector<float>> widths;
  TypedAttribute<std::vector<int64_t>> ids;                  // per-point ids. 
  TypedAttribute<std::vector<value::vector3f>> velocities;     // vector3f
  TypedAttribute<std::vector<value::vector3f>> accelerations;  // vector3f

};

// import DEFINE_TYPE_TRAIT and DEFINE_ROLE_TYPE_TRAIT
#include "define-type-trait.inc"

namespace value {

// Geom
DEFINE_TYPE_TRAIT(GPrim, kGPrim, TYPE_ID_GPRIM, 1);

DEFINE_TYPE_TRAIT(Xform, kGeomXform, TYPE_ID_GEOM_XFORM, 1);
DEFINE_TYPE_TRAIT(GeomMesh, kGeomMesh, TYPE_ID_GEOM_MESH, 1);
DEFINE_TYPE_TRAIT(GeomBasisCurves, kGeomBasisCurves, TYPE_ID_GEOM_BASIS_CURVES, 1);
DEFINE_TYPE_TRAIT(GeomSphere, kGeomSphere, TYPE_ID_GEOM_SPHERE, 1);
DEFINE_TYPE_TRAIT(GeomCube, kGeomCube, TYPE_ID_GEOM_CUBE, 1);
DEFINE_TYPE_TRAIT(GeomCone, kGeomCone, TYPE_ID_GEOM_CONE, 1);
DEFINE_TYPE_TRAIT(GeomCylinder, kGeomCylinder, TYPE_ID_GEOM_CYLINDER, 1);
DEFINE_TYPE_TRAIT(GeomCapsule, kGeomCapsule, TYPE_ID_GEOM_CAPSULE, 1);
DEFINE_TYPE_TRAIT(GeomSubset, kGeomSubset, TYPE_ID_GEOM_GEOMSUBSET, 1);
DEFINE_TYPE_TRAIT(GeomCamera, kGeomCamera, TYPE_ID_GEOM_CAMERA, 1);

#undef DEFINE_TYPE_TRAIT
#undef DEFINE_ROLE_TYPE_TRAIT

}  // namespace value

}  // namespace tinyusdz
