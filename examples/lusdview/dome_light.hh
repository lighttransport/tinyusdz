// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "gpu_scene.hh"

namespace lusdview {

// Build a runtime-editable dome from an image or a small procedural preset.
// The result owns its baked IBL data and can be passed directly to setLights().
bool BuildDomeLightFromFile(const std::string& path,
                            DrawLightCPU::DomeTextureFormat format,
                            bool highQuality, DrawLightCPU* out,
                            std::string* err);
bool BuildWhiteFurnaceDome(bool highQuality, DrawLightCPU* out,
                           std::string* err);
bool BuildSunSkyDome(bool highQuality, DrawLightCPU* out, std::string* err);

// Apply non-destructive UI controls after a dome has been built/selected.
void ApplyDomeLightControls(float intensity, float rotationDegrees,
                            const DrawLightCPU& base, DrawLightCPU* out);

}  // namespace lusdview
