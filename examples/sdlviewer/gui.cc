#include "imgui.h"

#include "gui.hh"

namespace example {

bool material_ui(tinyusdz::PreviewSurface &material)
{

  if (!material.diffuseColor.HasTexture()) {
    ImGui::InputFloat("diffuseColor", material.diffuseColor.color.data());
  }
}

} // namespace example
