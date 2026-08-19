// SPDX-License-Identifier: Apache-2.0
// Bounded, dependency-free IES LM-63 profile loading and angular evaluation.
#pragma once

#include <string>

#include "gpu_scene.hh"

namespace tusdview {

// Loads an IES LM-63 photometric profile into the canonical DrawLightCPU
// record. The parser intentionally accepts only TILT=NONE profiles for now;
// the authored light remains valid and cone shaping remains active when a
// profile cannot be loaded.
bool LoadIesProfile(const std::string& path, DrawLightCPU* light,
                    std::string* err = nullptr);

// Evaluates normalized candela at vertical/horizontal angles in degrees.
// Angles are expressed in the light's local photometric frame. Returns 1 for
// an absent profile so callers can multiply it into existing cone shaping.
float EvaluateIesProfile(const DrawLightCPU& light, float verticalDeg,
                         float horizontalDeg);

}  // namespace tusdview
