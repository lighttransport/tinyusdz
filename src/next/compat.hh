// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// LightUSD Compatibility Header
//
// This header provides seamless switching between the original and "next"
// architectures. Use -DLIGHTUSD_USE_NEXT=ON to enable the new architecture.
//
// Usage:
//   #include "next/compat.hh"
//   using namespace lightusd::compat;
//
//   Stage stage;
//   LoadUSD("model.usda", &stage);
//
// Or use explicit namespaces:
//   #ifdef LIGHTUSD_USE_NEXT
//   lightusd::next::Stage stage;
//   #else
//   lightusd::Stage stage;
//   #endif

#pragma once

#ifdef LIGHTUSD_USE_NEXT

// Use the new "next" architecture
#include "lightusd-next.hh"

namespace lightusd {
namespace compat {

// Import next types into compat namespace
using Stage = next::Stage;
using UsdPrim = next::UsdPrim;
using Layer = next::Layer;
using PrimSpec = next::PrimSpec;
using Path = next::Path;
using Value = next::Value;
using TypeId = next::TypeId;

// Loading functions
using next::LoadUSD;
using next::LoadUSDA;
using next::LoadUSDC;
using next::LoadOptions;

// Writing functions
using next::WriteUSDA;
using next::WriteUSDC;
using next::WriteOptions;

// Schema APIs
using next::IsMaterial;
using next::IsShader;
using next::GetShaderId;
using next::IsPreviewSurface;
using next::GetPreviewSurfaceData;
using next::PreviewSurfaceData;

using next::IsCamera;
using next::GetCameraData;
using next::CameraData;

using next::IsLight;
using next::GetLightType;
using next::LightType;
using next::GetLightData;
using next::LightData;

using next::IsPointInstancer;
using next::UsdGeomPointInstancer;
using next::PointInstancerTransform;
using next::GetAllPointInstancers;

// Attribute evaluation
using next::AttributeEval;
using next::EvalResult;
using next::EvalOptions;
using next::TimeQuery;
using next::ValueClipStageCache;

}  // namespace compat
}  // namespace lightusd

#else  // !LIGHTUSD_USE_NEXT

// Use the original architecture
#include "../lightusd.hh"

namespace lightusd {
namespace compat {

// Import original types into compat namespace
using Stage = lightusd::Stage;
using Path = lightusd::Path;
using Value = lightusd::value::Value;

// Note: The original architecture has different type names/structures
// Users should use #ifdef LIGHTUSD_USE_NEXT for type-specific code

// Loading functions wrapper
inline bool LoadUSD(const std::string& filename, Stage* stage,
                    std::string* warn = nullptr, std::string* err = nullptr) {
  return lightusd::LoadUSDFromFile(filename, stage, warn, err);
}

inline bool LoadUSDA(const std::string& filename, Stage* stage,
                     std::string* warn = nullptr, std::string* err = nullptr) {
  return lightusd::LoadUSDAFromFile(filename, stage, warn, err);
}

inline bool LoadUSDC(const std::string& filename, Stage* stage,
                     std::string* warn = nullptr, std::string* err = nullptr) {
  return lightusd::LoadUSDCFromFile(filename, stage, warn, err);
}

}  // namespace compat
}  // namespace lightusd

#endif  // LIGHTUSD_USE_NEXT
