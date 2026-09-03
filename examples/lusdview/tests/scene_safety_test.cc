// SPDX-License-Identifier: Apache-2.0
#include "scene_optimize.hh"
#include "scene_validation.hh"

#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
}

lusdview::DrawMeshCPU Triangle(int materialId) {
  lusdview::DrawMeshCPU mesh;
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
  lusdview::DrawScene scene;
  scene.materials.resize(2);
  scene.materials[0].name = "first";
  scene.materials[0].absPath = "/Looks/first";
  scene.materials[1] = scene.materials[0];
  scene.materials[1].name = "second";
  scene.materials[1].absPath = "/Looks/second";
  scene.meshes.push_back(Triangle(1));

  Check(lusdview::ValidateDrawScene(scene, &err), "valid scene was rejected");
  Check(lusdview::DrawMaterialsRenderEquivalent(scene.materials[0],
                                                 scene.materials[1]),
        "names and paths must not prevent render-content deduplication");
  Check(lusdview::CanonicalizeDrawMaterials(&scene) == 1,
        "duplicate material payload was not canonicalized");
  const lusdview::DrawMaterialTable materialTable =
      lusdview::BuildDrawMaterialTable(scene.materials);
  Check(scene.materials.size() == 2 &&
            scene.meshes[0].submeshes[0].materialId == 1 &&
            materialTable.logicalToCanonical.size() == 2 &&
            materialTable.logicalToCanonical[0] ==
                materialTable.logicalToCanonical[1],
        "canonicalization changed authored material identity");

  lusdview::DrawMaterialCPU distinct = scene.materials[0];
  distinct.roughness = 0.25f;
  Check(!lusdview::DrawMaterialsRenderEquivalent(scene.materials[0], distinct),
        "render-affecting material difference was deduplicated");

  lusdview::DrawMaterialCPU heuristic = scene.materials[0];
  heuristic.alphaMaskHeuristic = true;
  Check(!lusdview::DrawMaterialsRenderEquivalent(scene.materials[0], heuristic),
        "alpha classification heuristic was omitted from the render key");

  lusdview::DrawMaterialCPU graphTexture = scene.materials[0];
  graphTexture.materialXGraph.valid = true;
  lusdview::MaterialXGraphNodeCPU imageNode;
  imageNode.op = lusdview::MaterialXGraphOpCPU::Image;
  imageNode.textureId = 7;
  graphTexture.materialXGraph.nodes.push_back(imageNode);
  Check(!lusdview::DrawMaterialsRenderEquivalent(scene.materials[0], graphTexture),
        "MaterialX graph texture was omitted from the render key");

  lusdview::DrawMaterialCPU nonFinite = scene.materials[0];
  nonFinite.roughness = std::numeric_limits<float>::quiet_NaN();
  Check(!lusdview::DrawMaterialsRenderEquivalent(nonFinite, nonFinite),
        "non-finite material payload was shared");

  lusdview::DrawMeshCPU badIndex = Triangle(0);
  badIndex.indices[2] = 3;
  Check(!lusdview::ValidateDrawMesh(badIndex, 1, &err),
        "out-of-range vertex index was accepted");

  lusdview::DrawMeshCPU badRange = Triangle(0);
  badRange.submeshes[0].indexOffset = UINT32_MAX - 1u;
  badRange.submeshes[0].indexCount = 6;
  Check(!lusdview::ValidateDrawMesh(badRange, 1, &err),
        "overflowing submesh range was accepted");

  lusdview::DrawMeshCPU badParallel = Triangle(0);
  badParallel.vertexColors = {1.0f, 1.0f};
  Check(!lusdview::ValidateDrawMesh(badParallel, 1, &err),
        "malformed parallel attribute was accepted");

  lusdview::DrawMeshCPU geomPropMesh = Triangle(0);
  lusdview::DrawGeomPropCPU temperature;
  temperature.name = "temperature";
  temperature.components = 1;
  temperature.values = {10.0f, 20.0f, 30.0f};
  geomPropMesh.geomProps.push_back(temperature);
  Check(lusdview::ValidateDrawMesh(geomPropMesh, 1, &err),
        "valid generic geomprop stream was rejected");

  lusdview::DrawMeshCPU matrixGeomPropMesh = Triangle(0);
  lusdview::DrawGeomPropCPU matrixGeomProp;
  matrixGeomProp.name = "matrix_attr";
  matrixGeomProp.components = 16;
  matrixGeomProp.values.resize(3 * matrixGeomProp.components, 0.0f);
  matrixGeomProp.values[0] = matrixGeomProp.values[5] =
      matrixGeomProp.values[10] = matrixGeomProp.values[15] = 1.0f;
  matrixGeomPropMesh.geomProps.push_back(matrixGeomProp);
  Check(lusdview::ValidateDrawMesh(matrixGeomPropMesh, 1, &err),
        "valid matrix generic geomprop stream was rejected");

  lusdview::DrawMeshCPU badGeomProp = geomPropMesh;
  badGeomProp.geomProps[0].values.pop_back();
  Check(!lusdview::ValidateDrawMesh(badGeomProp, 1, &err),
        "malformed generic geomprop stream was accepted");

  lusdview::DrawMeshCPU badMorph = Triangle(0);
  badMorph.morphOffsetCount = {0, 2, 0, 0, 0, 0};
  badMorph.morphDeltaHalf = {0, 0, 0, 0};
  Check(!lusdview::ValidateDrawMesh(badMorph, 1, &err),
        "out-of-range morph CSR span was accepted");

  lusdview::DrawPointsCPU badPoints;
  badPoints.absPath = "/points";
  badPoints.points = {0.0f, 1.0f};
  Check(!lusdview::ValidateDrawPoints(badPoints, 1, &err),
        "incomplete progressive points payload was accepted");

  lusdview::DrawCurvesCPU badCurves;
  badCurves.absPath = "/curves";
  badCurves.points = {0.0f, 0.0f, 0.0f,
                      1.0f, 0.0f, 0.0f,
                      2.0f, 0.0f, 0.0f};
  badCurves.vertexCounts = {2};
  Check(!lusdview::ValidateDrawCurves(badCurves, 1, &err),
        "mismatched progressive curve counts were accepted");

  lusdview::DrawVolumeCPU badVolume;
  badVolume.name = "/volume";
  badVolume.dim[0] = badVolume.dim[1] = badVolume.dim[2] = 2;
  badVolume.density.resize(1);
  Check(!lusdview::ValidateDrawVolume(badVolume, &err),
        "undersized progressive volume payload was accepted");

  lusdview::DrawScene badTexture;
  badTexture.materials.resize(1);
  badTexture.materials[0].baseColorTex = 0;
  Check(!lusdview::ValidateDrawScene(badTexture, &err),
        "invalid texture binding was accepted");

  badTexture.materials[0].baseColorTex = -2;
  Check(!lusdview::ValidateDrawScene(badTexture, &err),
        "texture sentinel below -1 was accepted");

  badTexture.materials[0].baseColorTex = -1;
  badTexture.materials[0].normalSample.tex = -2;
  Check(!lusdview::ValidateDrawScene(badTexture, &err),
        "texture-sample sentinel below -1 was accepted");

  lusdview::DrawScene shortTexture;
  shortTexture.textures.resize(1);
  shortTexture.textures[0].image.width = 4;
  shortTexture.textures[0].image.height = 4;
  shortTexture.textures[0].image.channels = 4;
  shortTexture.textures[0].image.data.resize(4);
  Check(!lusdview::ValidateDrawScene(shortTexture, &err),
        "undersized RGBA texture payload was accepted");

  shortTexture.textures[0].image.data.resize(4u * 4u * 4u);
  shortTexture.textures[0].mipImages.resize(1);
  shortTexture.textures[0].mipImages[0].width = 2;
  shortTexture.textures[0].mipImages[0].height = 2;
  shortTexture.textures[0].mipImages[0].channels = 4;
  shortTexture.textures[0].mipImages[0].data.resize(3);
  Check(!lusdview::ValidateDrawScene(shortTexture, &err),
        "undersized RGBA mip payload was accepted");

  shortTexture.textures[0].mipImages.clear();
  Check(lusdview::ValidateDrawScene(shortTexture, &err),
        "valid RGBA texture payload was rejected");

  if (failures == 0) std::printf("scene safety tests passed\n");
  return failures == 0 ? 0 : 1;
}
