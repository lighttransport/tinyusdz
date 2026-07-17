// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openpbr-params-converter.hh"

namespace tinyusdz {
namespace tydra {
namespace next {

/// Maps a RenderMaterial to a LightRtOpenPBRParams block.
/// This is the shared logic for both tusdview and tusdrender.
inline bool MapRenderMaterialToLightRtOpenPBR(
    const RenderMaterial& mat, tydra::LightRtOpenPBRParams* p) {
  return BuildLightRtOpenPBRParams(mat, p);
}

}  // namespace next
}  // namespace tydra
}  // namespace tinyusdz
