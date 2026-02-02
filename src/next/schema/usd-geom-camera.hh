// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - UsdGeomCamera Schema API
// Convenience accessors for USD camera

#pragma once

#include "../stage/stage.hh"
#include "../eval/attribute-eval.hh"

namespace tinyusdz {
namespace next {

/// Camera projection type
enum class CameraProjection {
  Perspective,
  Orthographic
};

/// Camera data
struct CameraData {
  CameraProjection projection = CameraProjection::Perspective;

  // Lens properties
  float focal_length = 50.0f;         // mm
  float horizontal_aperture = 36.0f;  // mm (35mm full frame width)
  float vertical_aperture = 24.0f;    // mm (35mm full frame height)
  float horizontal_aperture_offset = 0.0f;
  float vertical_aperture_offset = 0.0f;

  // Focus
  float focus_distance = 0.0f;        // Distance to focus plane (0 = infinity)
  float f_stop = 0.0f;                // F-stop (0 = no DOF)

  // Clipping
  float clipping_near = 0.1f;
  float clipping_far = 10000.0f;

  // Computed values (derived from above)
  float fov_horizontal = 0.0f;        // In radians
  float fov_vertical = 0.0f;          // In radians
  float aspect_ratio = 1.5f;
};

/// Check if prim is a Camera
bool IsCamera(const UsdPrim& prim);

/// Get camera data
bool GetCameraData(const Stage& stage, const UsdPrim& prim,
                   CameraData* out, double time = 0.0);

/// Get focal length in mm
float GetFocalLength(const Stage& stage, const UsdPrim& prim, double time = 0.0);

/// Get horizontal aperture in mm
float GetHorizontalAperture(const Stage& stage, const UsdPrim& prim, double time = 0.0);

/// Get vertical aperture in mm
float GetVerticalAperture(const Stage& stage, const UsdPrim& prim, double time = 0.0);

/// Get focus distance
float GetFocusDistance(const Stage& stage, const UsdPrim& prim, double time = 0.0);

/// Get f-stop
float GetFStop(const Stage& stage, const UsdPrim& prim, double time = 0.0);

/// Get near clipping distance
float GetClippingNear(const Stage& stage, const UsdPrim& prim, double time = 0.0);

/// Get far clipping distance
float GetClippingFar(const Stage& stage, const UsdPrim& prim, double time = 0.0);

/// Compute horizontal field of view in radians
float ComputeHorizontalFOV(float focal_length_mm, float horizontal_aperture_mm);

/// Compute vertical field of view in radians
float ComputeVerticalFOV(float focal_length_mm, float vertical_aperture_mm);

/// Compute perspective projection matrix (column-major, OpenGL style)
void ComputeProjectionMatrix(const CameraData& camera, float* matrix16);

/// Compute perspective projection matrix with custom aspect ratio
void ComputeProjectionMatrix(const CameraData& camera, float aspect_ratio, float* matrix16);

}  // namespace next
}  // namespace tinyusdz
