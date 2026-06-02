// SPDX-License-Identifier: Apache-2.0
// tusdview - ImGui color style (Maya-like dark theme).
// Adapted from tinyusdview (gui_style.cc) / https://gist.github.com/meshula
#pragma once

namespace tusdview {

// Apply the Maya-like dark color theme to the current ImGui style. Call before
// ScaleAllSizes() so rounding/spacing scale correctly for HiDPI.
void StyleMaya();

}  // namespace tusdview
