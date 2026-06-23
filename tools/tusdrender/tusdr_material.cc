// SPDX-License-Identifier: Apache-2.0
// tusdrender — tydra RenderScene material accessors (legacy shading path).
#include "tusdr_context.hh"

namespace tusdr {

Vec3 MaterialColor(const RenderScene &scene, const RenderMesh &mesh,
                   int material_id) {
  if (material_id >= 0 && size_t(material_id) < scene.materials.size()) {
    const RenderMaterial &mat = scene.materials[size_t(material_id)];
    if (mat.openPBRShader.has_value()) {
      return Clamp01(FromFloat3(mat.openPBRShader->base_color.value));
    }
    if (mat.surfaceShader.has_value()) {
      return Clamp01(FromFloat3(mat.surfaceShader->diffuseColor.value));
    }
  }
  color3f c = mesh.displayColor;
  return Clamp01(Vec3{c[0], c[1], c[2]});
}

Vec3 MaterialEmission(const RenderScene &scene, int material_id) {
  if (material_id >= 0 && size_t(material_id) < scene.materials.size()) {
    const RenderMaterial &mat = scene.materials[size_t(material_id)];
    if (mat.surfaceShader.has_value()) {
      return FromFloat3(mat.surfaceShader->emissiveColor.value);
    }
  }
  return Vec3{0.0f, 0.0f, 0.0f};
}

float MaterialRoughness(const RenderScene &scene, int material_id) {
  if (material_id >= 0 && size_t(material_id) < scene.materials.size()) {
    const RenderMaterial &mat = scene.materials[size_t(material_id)];
    if (mat.openPBRShader.has_value()) {
      return ClampFloat(mat.openPBRShader->base_roughness.value, 0.02f, 1.0f);
    }
    if (mat.surfaceShader.has_value()) {
      return ClampFloat(mat.surfaceShader->roughness.value, 0.02f, 1.0f);
    }
  }
  return 0.55f;
}

float MaterialMetallic(const RenderScene &scene, int material_id) {
  if (material_id >= 0 && size_t(material_id) < scene.materials.size()) {
    const RenderMaterial &mat = scene.materials[size_t(material_id)];
    if (mat.openPBRShader.has_value()) {
      return ClampFloat(mat.openPBRShader->base_metalness.value, 0.0f, 1.0f);
    }
    if (mat.surfaceShader.has_value()) {
      return ClampFloat(mat.surfaceShader->metallic.value, 0.0f, 1.0f);
    }
  }
  return 0.0f;
}

Vec3 MeshLightEmission(const RenderScene &scene, const RenderMesh &mesh,
                       int material_id, float total_area) {
  if (!mesh.is_area_light) return Vec3{0.0f, 0.0f, 0.0f};
  auto light_color = mesh.get_effective_light_color();
  Vec3 effective{light_color[0], light_color[1], light_color[2]};
  Vec3 material_emission = MaterialEmission(scene, material_id);
  Vec3 result = effective;
  if (mesh.light_material_sync_mode == "independent") {
    result = Add(effective, material_emission);
  } else if (mesh.light_material_sync_mode != "noMaterialResponse") {
    Vec3 tint = material_emission;
    if (Luminance(tint) <= 1.0e-6f) {
      tint = MaterialColor(scene, mesh, material_id);
    }
    result = Mul(effective, tint);
  }
  if (mesh.light_normalize && total_area > 1.0e-8f) {
    result = Mul(result, 1.0f / total_area);
  }
  return result;
}

}  // namespace tusdr
