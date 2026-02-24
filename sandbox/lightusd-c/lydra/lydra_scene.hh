// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// Lydra - Scene module (stubs)
// Thin helpers to bridge lightusd Stage -> lydra types.
// This is the only module that knows about lightusd types.
// Implemented as stubs; filled in when lightusd Stage is functional.

#pragma once

namespace lydra {

// Forward declarations for lightusd types (uncomment when available):
// namespace lightusd { class Prim; }

// Extract triangle mesh data from a Mesh prim
// Result<MeshData> extract_mesh(const lightusd::Prim& prim);

// Extract material from a Material prim
// Result<FlatMaterial> extract_material(const lightusd::Prim& prim);

}  // namespace lydra
