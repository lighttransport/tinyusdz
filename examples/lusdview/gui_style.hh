// SPDX-License-Identifier: Apache-2.0
// lusdview - ImGui color style (Maya-like dark theme).
// Adapted from lusdview (gui_style.cc) / https://gist.github.com/meshula
#pragma once

namespace lusdview {

// Apply the Maya-like dark color theme to the current ImGui style. Call before
// ScaleAllSizes() so rounding/spacing scale correctly for HiDPI.
void StyleMaya();

}  // namespace lusdview
