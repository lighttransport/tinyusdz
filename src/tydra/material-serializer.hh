// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Light Transport Entertainment, Inc.
//
// Material serialization utilities for WASM/JS bindings

#pragma once

#include <string>
#include "nonstd/expected.hpp"
#include "render-data.hh"

namespace tinyusdz {
namespace tydra {

enum class SerializationFormat {
  JSON,
  XML
};

// Serialize a RenderMaterial to JSON or XML format
// Returns serialized string on success, error message on failure
nonstd::expected<std::string, std::string> serializeMaterial(
    const RenderMaterial& material,
    SerializationFormat format);

} // namespace tydra
} // namespace tinyusdz
