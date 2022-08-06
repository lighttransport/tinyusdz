// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
//
// UsdGeom
#pragma once

#include "prim-types.hh"

namespace tinyusdz {

// Generic Prim
struct GPrim {
  std::string name;

  int64_t parent_id{-1};  // Index to parent node

  std::string prim_type;  // Primitive type(if specified by `def`)

  // Gprim
  AnimatableExtent extent;  // bounding extent(in local coord?).
  bool doubleSided{false};
  Orientation orientation{Orientation::RightHanded};
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};
  AnimatableVec3fArray displayColor;    // primvars:displayColor
  AnimatableFloatArray displayOpacity;  // primvars:displaOpacity

  MaterialBindingAPI materialBinding;

  std::map<std::string, Property> props;

  std::map<std::string, value::Value> args;

  bool _valid{true};  // default behavior is valid(allow empty GPrim)

  bool active{true};

  // child nodes
  std::vector<GPrim> children;
};

struct Xform {
  std::string name;
  int64_t parent_id{-1};  // Index to xform node

  std::vector<XformOp> xformOps;

  Orientation orientation{Orientation::RightHanded};
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

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

  std::map<std::string, PrimAttrib> attribs;  // custom Attrs
};

// Polygon mesh geometry
struct GeomMesh : GPrim {
  //
  // Predefined attribs.
  //
  std::vector<value::point3f> points;    // point3f
  nonstd::optional<PrimAttrib> normals;  // normal3f[]

  //
  // Utility functions
  //

  // Initialize GeomMesh by GPrim(prepend references)
  void Initialize(const GPrim &pprim);

  // Update GeomMesh by GPrim(append references)
  void UpdateBy(const GPrim &pprim);

  ///
  /// @brief Returns normals vector. Precedence order: `primvar::normals` then
  /// `normals`.
  ///
  /// @return normals vector(copied). Returns empty normals vector when neither
  /// `primvar::normals` nor `normals` attribute defined, attribute is a
  /// relation or normals attribute have invalid type(other than `normal3f`).
  ///
  std::vector<value::normal3f> GetNormals() const;

  ///
  /// @brief Get interpolation of normals.
  /// @return Interpolation of normals. `vertex` by defaut.
  ///
  Interpolation GetNormalsInterpolation() const;

#if 0
  // Get `normals` as float3 array + facevarying
  // Return false if `normals` is neither float3[] type nor `varying`
  bool GetFacevaryingNormals(std::vector<float> *v) const;
#endif

  // Get `texcoords` as float2 array + facevarying
  // Return false if `texcoords` is neither float2[] type nor `varying`
  bool GetFacevaryingTexcoords(std::vector<float> *v) const;

  // Primary UV coords(TODO: Remove. Read uv coords through PrimVarReader)
  UVCoords st;

  PrimAttrib velocitiess;  // Usually float3[], varying

  std::vector<int32_t> faceVertexCounts;
  std::vector<int32_t> faceVertexIndices;

  //
  // Properties
  //
  // AnimatableExtent extent;  // bounding extent(in local coord?).
  std::string facevaryingLinearInterpolation = "cornerPlus1";
  // AnimatableVisibility visibility{Visibility::Inherited};
  // Purpose purpose{Purpose::Default};

  // Gprim
  // bool doubleSided{false};
  // Orientation orientation{Orientation::RightHanded};
  // AnimatableVec3fArray displayColor; // primvars:displayColor
  // AnimatableFloatArray displayOpacity; // primvars:displaOpacity

  // MaterialBindingAPI materialBinding;

  //
  // SubD attribs.
  //
  std::vector<int32_t> cornerIndices;
  std::vector<float> cornerSharpnesses;
  std::vector<int32_t> creaseIndices;
  std::vector<int32_t> creaseLengths;
  std::vector<float> creaseSharpnesses;
  std::vector<int32_t> holeIndices;
  std::string interpolateBoundary =
      "edgeAndCorner";  // "none", "edgeAndCorner" or "edgeOnly"
  SubdivisionScheme subdivisionScheme{SubdivisionScheme::CatmullClark};

  //
  // GeomSubset
  //
  // uniform token `subsetFamily:materialBind:familyType`
  GeomSubset::FamilyType materialBindFamilyType{
      GeomSubset::FamilyType::Partition};

  std::vector<GeomSubset> geom_subset_children;

  ///
  /// Validate GeomSubset data attached to this GeomMesh.
  ///
  nonstd::expected<bool, std::string> ValidateGeomSubset();

  // List of Primitive attributes(primvars)
  // std::map<std::string, PrimAttrib> attribs;
};

struct GeomCamera {
  std::string name;

  int64_t parent_id{-1};  // Index to parent node

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
  AnimatableExtent extent;  // bounding extent(in local coord?).
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

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

  std::vector<value::token> xformOpOrder;
  // xformOpOrder

  // List of Primitive attributes(primvars)
  // NOTE: `primvar:widths` are not stored here(stored in `widths`)
  std::map<std::string, PrimAttrib> attribs;
};

struct GeomBoundable {
  std::string name;

  int64_t parent_id{-1};  // Index to parent node

  //
  // Properties
  //
  AnimatableExtent extent;  // bounding extent(in local coord?).
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  MaterialBindingAPI materialBinding;

  // List of Primitive attributes(primvars)
  // NOTE: `primvar:widths` are not stored here(stored in `widths`)
  std::map<std::string, PrimAttrib> attribs;
};

struct GeomCone {
  std::string name;

  int64_t parent_id{-1};  // Index to parent node

  //
  // Properties
  //
  AnimatableDouble height{2.0};
  AnimatableDouble radius{1.0};
  Axis axis{Axis::Z};

  AnimatableExtent extent{
      Extent({-1.0, -1.0, -1.0},
             {1.0, 1.0, 1.0})};  // bounding extent(in local coord?).
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  // Gprim
  bool doubleSided{false};
  Orientation orientation{Orientation::RightHanded};
  AnimatableVec3fArray displayColor;    // primvars:displayColor
  AnimatableFloatArray displayOpacity;  // primvars:displaOpacity

  MaterialBindingAPI materialBinding;

  // List of Primitive attributes(primvars)
  // NOTE: `primvar:widths` are not stored here(stored in `widths`)
  std::map<std::string, PrimAttrib> attribs;
};

struct GeomCapsule {
  std::string name;

  int64_t parent_id{-1};  // Index to parent node

  //
  // Properties
  //
  AnimatableDouble height{2.0};
  AnimatableDouble radius{0.5};
  Axis axis{Axis::Z};

  AnimatableExtent extent{
      Extent({-0.5, -0.5, -1.0},
             {0.5, 0.5, 1.0})};  // bounding extent(in local coord?).
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  // Gprim
  bool doubleSided{false};
  Orientation orientation{Orientation::RightHanded};
  AnimatableVec3fArray displayColor;    // primvars:displayColor
  AnimatableFloatArray displayOpacity;  // primvars:displaOpacity

  MaterialBindingAPI materialBinding;

  // List of Primitive attributes(primvars)
  // NOTE: `primvar:widths` are not stored here(stored in `widths`)
  std::map<std::string, PrimAttrib> attribs;
};

struct GeomCylinder {
  std::string name;

  int64_t parent_id{-1};  // Index to parent node

  //
  // Properties
  //
  AnimatableDouble height{2.0};
  AnimatableDouble radius{1.0};
  Axis axis{Axis::Z};
  AnimatableExtent extent{
      Extent({-1.0, -1.0, -1.0},
             {1.0, 1.0, 1.0})};  // bounding extent(in local coord?).
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  // Gprim
  bool doubleSided{false};
  Orientation orientation{Orientation::RightHanded};
  AnimatableVec3fArray displayColor;    // primvars:displayColor
  AnimatableFloatArray displayOpacity;  // primvars:displaOpacity

  MaterialBindingAPI materialBinding;

  // List of Primitive attributes(primvars)
  // NOTE: `primvar:widths` are not stored here(stored in `widths`)
  std::map<std::string, PrimAttrib> attribs;
};

struct GeomCube {
  std::string name;

  int64_t parent_id{-1};  // Index to parent node

  //
  // Properties
  //
  AnimatableDouble size{2.0};
  AnimatableExtent extent{
      Extent({-1.0, -1.0, -1.0},
             {1.0, 1.0, 1.0})};  // bounding extent(in local coord?).
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  // Gprim
  bool doubleSided{false};
  Orientation orientation{Orientation::RightHanded};
  AnimatableVec3fArray displayColor;    // primvars:displayColor
  AnimatableFloatArray displayOpacity;  // primvars:displaOpacity

  MaterialBindingAPI materialBinding;

  // List of Primitive attributes(primvars)
  // NOTE: `primvar:widths` are not stored here(stored in `widths`)
  std::map<std::string, PrimAttrib> attribs;
};

struct GeomSphere {
  std::string name;

  int64_t parent_id{-1};  // Index to parent node

  //
  // Predefined attribs.
  //
  AnimatableDouble radius{1.0};

  //
  // Properties
  //
  AnimatableExtent extent{
      Extent({-1.0, -1.0, -1.0},
             {1.0, 1.0, 1.0})};  // bounding extent(in local coord?).
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  // Gprim
  bool doubleSided{false};
  Orientation orientation{Orientation::RightHanded};
  AnimatableVec3fArray displayColor;    // primvars:displayColor
  AnimatableFloatArray displayOpacity;  // primvars:displaOpacity

  MaterialBindingAPI materialBinding;

  // List of Primitive attributes(primvars)
  // NOTE: `primvar:widths` are not stored here(stored in `widths`)
  std::map<std::string, PrimAttrib> attribs;
};

//
// Basis Curves(for hair/fur)
//
struct GeomBasisCurves {
  std::string name;

  int64_t parent_id{-1};  // Index to parent node

  // Interpolation attribute
  std::string type = "cubic";  // "linear", "cubic"

  std::string basis =
      "bspline";  // "bezier", "catmullRom", "bspline" ("hermite" and "power" is
                  // not supported in TinyUSDZ)

  std::string wrap = "nonperiodic";  // "nonperiodic", "periodic", "pinned"

  //
  // Predefined attribs.
  //
  std::vector<value::float3> points;
  std::vector<value::float3> normals;  // normal3f
  std::vector<int> curveVertexCounts;
  std::vector<float> widths;
  std::vector<value::float3> velocities;     // vector3f
  std::vector<value::float3> accelerations;  // vector3f

  //
  // Properties
  //
  AnimatableExtent extent;  // bounding extent(in local coord?).
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  // Gprim
  bool doubleSided{false};
  Orientation orientation{Orientation::RightHanded};
  AnimatableVec3fArray displayColor;    // primvars:displayColor
  AnimatableFloatArray displayOpacity;  // primvars:displaOpacity

  MaterialBindingAPI materialBinding;

  // List of Primitive attributes(primvars)
  // NOTE: `primvar:widths` are not stored here(stored in `widths`)
  std::map<std::string, PrimAttrib> attribs;
};

//
// Points primitive.
//
struct GeomPoints {
  std::string name;

  int64_t parent_id{-1};  // Index to xform node

  //
  // Predefined attribs.
  //
  std::vector<value::float3> points;   // float3
  std::vector<value::float3> normals;  // normal3f
  std::vector<float> widths;
  std::vector<int64_t> ids;                  // per-point ids
  std::vector<value::float3> velocities;     // vector3f
  std::vector<value::float3> accelerations;  // vector3f

  //
  // Properties
  //
  AnimatableExtent extent;  // bounding extent(in local coord?).
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  // Gprim
  bool doubleSided{false};
  Orientation orientation{Orientation::RightHanded};
  AnimatableVec3fArray displayColor;    // primvars:displayColor
  AnimatableFloatArray displayOpacity;  // primvars:displaOpacity

  // List of Primitive attributes(primvars)
  // NOTE: `primvar:widths` may exist(in that ase, please ignore `widths`
  // parameter)
  std::map<std::string, PrimAttrib> attribs;
};

// import DEFINE_TYPE_TRAIT and DEFINE_ROLE_TYPE_TRAIT
#include "define-type-trait.inc"

namespace value {

// Geom
DEFINE_TYPE_TRAIT(GPrim, "GPRIM", TYPE_ID_GPRIM, 1);

DEFINE_TYPE_TRAIT(Xform, "Xform", TYPE_ID_GEOM_XFORM, 1);
DEFINE_TYPE_TRAIT(GeomMesh, "Mesh", TYPE_ID_GEOM_MESH, 1);

#undef DEFINE_TYPE_TRAIT
#undef DEFINE_ROLE_TYPE_TRAIT

}  // namespace value

}  // namespace tinyusdz
