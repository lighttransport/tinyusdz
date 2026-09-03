// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "resource_ids.h"

namespace lusdview {

// Returns the embedded CUDA/HIP trace kernel source (Windows RCDATA
// resource IDR_RAYTRACER_KERNEL). Windows-only; see kernel_resource.cc.
const std::string& GetEmbeddedKernelSource();

}  // namespace lusdview
