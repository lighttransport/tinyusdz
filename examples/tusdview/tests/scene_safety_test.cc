// SPDX-License-Identifier: Apache-2.0
#include "scene_optimize.hh"
#include "scene_validation.hh"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
}

tusdview::DrawMeshCPU Triangle(int materialId) {
  tusdview::DrawMeshCPU mesh;
  mesh.name = "triangle";
  mesh.absPath = "/triangle";
  mesh.vertices = {{0, 0, 0, 0, 0, 1, 0, 0},
                   {1, 0, 0, 0, 0, 1, 1, 0},
                   {0, 1, 0, 0, 0, 1, 0, 1}};
  mesh.indices = {0, 1, 2};
  mesh.submeshes.push_back({0, 3, materialId, -1});
  mesh.world[0] = mesh.world[5] = mesh.world[10] = mesh.world[15] = 1.0f;
  return mesh;
}

}  // namespace

int main() {
  std::string err;
  tusdview::DrawScene scene;
  scene.materials.resize(2);
  scene.materials[0].name = "first";
  scene.materials[0].absPath = "/Looks/first";
  scene.materials[1] = scene.materials[0];
  scene.materials[1].name = "second";
  scene.materials[1].absPath = "/Looks/second";
  scene.meshes.push_back(Triangle(1));

  Check(tusdview::ValidateDrawScene(scene, &err), "valid scene was rejected");
  Check(tusdview::DrawMaterialsRenderEquivalent(scene.materials[0],
                                                 scene.materials[1]),
        "names and paths must not prevent render-content deduplication");
  Check(tusdview::DeduplicateDrawMaterials(&scene) == 1,
        "duplicate material was not removed");
  Check(scene.materials.size() == 1 &&
            scene.meshes[0].submeshes[0].materialId == 0,
        "material bindings were not remapped");

  tusdview::DrawMaterialCPU distinct = scene.materials[0];
  distinct.roughness = 0.25f;
  Check(!tusdview::DrawMaterialsRenderEquivalent(scene.materials[0], distinct),
        "render-affecting material difference was deduplicated");

  tusdview::DrawMeshCPU badIndex = Triangle(0);
  badIndex.indices[2] = 3;
  Check(!tusdview::ValidateDrawMesh(badIndex, 1, &err),
        "out-of-range vertex index was accepted");

  tusdview::DrawMeshCPU badRange = Triangle(0);
  badRange.submeshes[0].indexOffset = UINT32_MAX - 1u;
  badRange.submeshes[0].indexCount = 6;
  Check(!tusdview::ValidateDrawMesh(badRange, 1, &err),
        "overflowing submesh range was accepted");

  tusdview::DrawMeshCPU badParallel = Triangle(0);
  badParallel.vertexColors = {1.0f, 1.0f};
  Check(!tusdview::ValidateDrawMesh(badParallel, 1, &err),
        "malformed parallel attribute was accepted");

  tusdview::DrawMeshCPU geomPropMesh = Triangle(0);
  tusdview::DrawGeomPropCPU temperature;
  temperature.name = "temperature";
  temperature.components = 1;
  temperature.values = {10.0f, 20.0f, 30.0f};
  geomPropMesh.geomProps.push_back(temperature);
  Check(tusdview::ValidateDrawMesh(geomPropMesh, 1, &err),
        "valid generic geomprop stream was rejected");

  tusdview::DrawMeshCPU badGeomProp = geomPropMesh;
  badGeomProp.geomProps[0].values.pop_back();
  Check(!tusdview::ValidateDrawMesh(badGeomProp, 1, &err),
        "malformed generic geomprop stream was accepted");

  tusdview::DrawMeshCPU badMorph = Triangle(0);
  badMorph.morphOffsetCount = {0, 2, 0, 0, 0, 0};
  badMorph.morphDeltaHalf = {0, 0, 0, 0};
  Check(!tusdview::ValidateDrawMesh(badMorph, 1, &err),
        "out-of-range morph CSR span was accepted");

  tusdview::DrawPointsCPU badPoints;
  badPoints.absPath = "/points";
  badPoints.points = {0.0f, 1.0f};
  Check(!tusdview::ValidateDrawPoints(badPoints, 1, &err),
        "incomplete progressive points payload was accepted");

  tusdview::DrawCurvesCPU badCurves;
  badCurves.absPath = "/curves";
  badCurves.points = {0.0f, 0.0f, 0.0f,
                      1.0f, 0.0f, 0.0f,
                      2.0f, 0.0f, 0.0f};
  badCurves.vertexCounts = {2};
  Check(!tusdview::ValidateDrawCurves(badCurves, 1, &err),
        "mismatched progressive curve counts were accepted");

  tusdview::DrawVolumeCPU badVolume;
  badVolume.name = "/volume";
  badVolume.dim[0] = badVolume.dim[1] = badVolume.dim[2] = 2;
  badVolume.density.resize(1);
  Check(!tusdview::ValidateDrawVolume(badVolume, &err),
        "undersized progressive volume payload was accepted");

  tusdview::DrawScene badTexture;
  badTexture.materials.resize(1);
  badTexture.materials[0].baseColorTex = 0;
  Check(!tusdview::ValidateDrawScene(badTexture, &err),
        "invalid texture binding was accepted");

  badTexture.materials[0].baseColorTex = -2;
  Check(!tusdview::ValidateDrawScene(badTexture, &err),
        "texture sentinel below -1 was accepted");

  badTexture.materials[0].baseColorTex = -1;
  badTexture.materials[0].normalSample.tex = -2;
  Check(!tusdview::ValidateDrawScene(badTexture, &err),
        "texture-sample sentinel below -1 was accepted");

  tusdview::DrawScene shortTexture;
  shortTexture.textures.resize(1);
  shortTexture.textures[0].image.width = 4;
  shortTexture.textures[0].image.height = 4;
  shortTexture.textures[0].image.channels = 4;
  shortTexture.textures[0].image.data.resize(4);
  Check(!tusdview::ValidateDrawScene(shortTexture, &err),
        "undersized RGBA texture payload was accepted");

  shortTexture.textures[0].image.data.resize(4u * 4u * 4u);
  shortTexture.textures[0].mipImages.resize(1);
  shortTexture.textures[0].mipImages[0].width = 2;
  shortTexture.textures[0].mipImages[0].height = 2;
  shortTexture.textures[0].mipImages[0].channels = 4;
  shortTexture.textures[0].mipImages[0].data.resize(3);
  Check(!tusdview::ValidateDrawScene(shortTexture, &err),
        "undersized RGBA mip payload was accepted");

  shortTexture.textures[0].mipImages.clear();
  Check(tusdview::ValidateDrawScene(shortTexture, &err),
        "valid RGBA texture payload was rejected");

  if (failures == 0) std::printf("scene safety tests passed\n");
  return failures == 0 ? 0 : 1;
}
