// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// Lydra - Material types
// Minimal PBR material struct, no scene-graph dependency

#pragma once

#include <cstdint>

namespace lydra {

struct TextureRef {
    int32_t index = -1;  // index into user's texture array, -1 = none
};

struct FlatMaterial {
    float base_color[4]  = {0.8f, 0.8f, 0.8f, 1.0f};
    float metallic       = 0.0f;
    float roughness      = 0.5f;
    float emissive[3]    = {0.0f, 0.0f, 0.0f};
    float occlusion      = 1.0f;
    float normal_scale   = 1.0f;
    float alpha_cutoff   = 0.5f;
    bool  double_sided   = false;

    TextureRef base_color_tex;
    TextureRef metallic_roughness_tex;
    TextureRef normal_tex;
    TextureRef emissive_tex;
    TextureRef occlusion_tex;
};

}  // namespace lydra
