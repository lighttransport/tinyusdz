// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Tydra Next - Render Data Structures
//
// Design goals:
// - Minimal intermediate copies
// - Chunked arrays for WASM heap efficiency
// - GPU-friendly data layout
// - Runtime type dispatch (no templates in API)

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <utility>

#include "chunked-array.hh"

namespace lightusd {
namespace tydra {
namespace next {

//
// Forward declarations
//
class RenderScene;
struct RenderMesh;
struct RenderMaterial;
struct RenderTexture;

//
// Enums
//

enum class ColorSpace : uint8_t {
  sRGB = 0,
  Linear,
  Raw,
  ACEScg,
  Rec709,
  Rec2020,
  DisplayP3,
  Unknown
};

enum class WrapMode : uint8_t {
  Repeat = 0,
  Clamp,
  Mirror,
  Black
};

enum class ComponentType : uint8_t {
  UInt8 = 0,
  Int8,
  UInt16,
  Int16,
  UInt32,
  Int32,
  Float16,
  Float32,
  Float64
};

enum class VertexFormat : uint8_t {
  Float = 0,
  Vec2,
  Vec3,
  Vec4,
  Int,
  IVec2,
  IVec3,
  IVec4,
  UInt,
  UVec2,
  UVec3,
  UVec4,
  Matrix33,
  Matrix44
};

enum class Interpolation : uint8_t {
  Constant = 0,   // One value for entire prim
  Uniform,        // One value per face
  Vertex,         // One value per vertex (shared)
  FaceVarying,    // One value per face-vertex
  Varying         // Bilinear interpolation
};

enum class NodeType : uint8_t {
  Xform = 0,
  Mesh,
  Points,
  PointInstancer,
  Camera,
  PointLight,
  DirectionalLight,
  SpotLight,
  RectLight,
  DiskLight,
  DomeLight,
  SphereLight,
  Skeleton,
  Curves
};

//
// Curve enums (UsdGeomBasisCurves / UsdGeomNurbsCurves)
//

enum class CurveType : uint8_t {
  Linear = 0,  // Polyline segments between control points
  Cubic        // Cubic spans (see CurveBasis)
};

enum class CurveBasis : uint8_t {
  Bezier = 0,
  BSpline,
  CatmullRom
};

enum class CurveWrap : uint8_t {
  Nonperiodic = 0,
  Periodic,   // Curve closes back onto its first point
  Pinned      // bspline/catmullRom interpolate their end control points
};

enum class LightType : uint8_t {
  Point = 0,
  Directional,
  Spot,
  Rect,
  Disk,
  Dome,
  Sphere,
  Cylinder,
  Geometry
};

enum class CameraType : uint8_t {
  Perspective = 0,
  Orthographic
};

#include "render-data-math.hh"


#include "render-data-scene-nodes.hh"

#include "render-data-scene.hh"


}  // namespace next
}  // namespace tydra
}  // namespace lightusd
