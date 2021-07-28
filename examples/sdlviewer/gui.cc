#include "imgui.h"

#include "gui.hh"

namespace example {

bool material_ui(tinyusdz::PreviewSurface &material)
{
  bool changed = false;

  if (!material.diffuseColor.HasTexture()) {
    ImGui::InputFloat("diffuseColor", material.diffuseColor.color.data());
  }

  return changed;
}

} // namespace example
