// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Light Transport Entertainment Inc.

#pragma once

#include <string>

namespace tinyusdz {

// forward decl
class RenderScene;

namespace tydra {


// For debug
// Supported format: "kdl" (default. https://kdl.dev/), "json"
//
std::string DumpRenderScene(const RenderScene &scene,
                            const std::string &format = "kdl");

}  // namespace tydra
}  // namespace tinyusdz
