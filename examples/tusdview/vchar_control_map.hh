// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

namespace tinyusdz {
class Stage;
namespace next { class Stage; }
}

namespace tusdview {

struct VcharControl {
  std::string name;
  std::string blendshape;
  float minimum{-1.0f};
  float maximum{1.0f};
  float defaultValue{0.0f};
};

std::vector<VcharControl> ReadVcharControls(const tinyusdz::Stage& stage);
std::vector<VcharControl> ReadVcharControls(const tinyusdz::next::Stage& stage);

}  // namespace tusdview
