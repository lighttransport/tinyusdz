#pragma once

#include <vector>

namespace tinyusdz {
namespace tydra {

struct RenderOption
{
  int spp{1}; // samples per pixel.
  int width{256};
  int height{256};
  
  // right-handed
  float eye[3] = {0.0f, 2.0f, 5.0f};
  float up[3] = {0.0f, 1.0f, 0.0f};
  float lookat[3] = {0.0f, 0.0f, 0.0f};
};


struct RenderBuffer
{
  std::vector<float> rgba;
  std::vector<float> depth;

};

bool Render(const RenderOption &option, RenderBuffer &result);


} // namespace tydra
} // namespace tinyusdz
