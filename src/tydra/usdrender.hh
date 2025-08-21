#pragma once

#include <vector>

//
// Basic USD render using NanoRT raytracer
// 
// This provides software-based raytracing to render USD scenes.
// Takes RenderMesh geometry from Tydra render-data and outputs
// RGBA color and depth buffers.
//
// Usage:
// 1. Load USD file and convert to RenderScene using RenderSceneConverter
// 2. Setup RenderOption with camera and image parameters  
// 3. Call Render() to generate RGBA and depth buffers
// 4. Process output buffers (e.g. save to image file)
//

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

class RenderScene; // forward declaration

bool Render(const RenderScene &scene, const RenderOption &option, RenderBuffer &result);


} // namespace tydra
} // namespace tinyusdz
