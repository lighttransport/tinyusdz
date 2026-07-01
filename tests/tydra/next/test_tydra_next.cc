// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Tydra Next - Unit Tests

#include <iostream>
#include <cassert>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <fstream>
#include <unordered_set>

#include "tydra/next/chunked-array.hh"
#include "tydra/next/materialx.hh"
#include "tydra/next/render-data.hh"
#include "tydra/next/scene-access.hh"
#include "tydra/next/render-extract.hh"
#include "tydra/next/render-converter.hh"
#include "next/reader/usda-reader.hh"

using namespace tinyusdz::tydra::next;
using namespace tinyusdz::next;

//
// ChunkedArray Tests
//

void TestChunkedArrayBasic() {
  std::cout << "Testing ChunkedArray basic operations...\n";

  ChunkedArray<float> arr;
  assert(arr.empty());
  assert(arr.size() == 0);

  // Push elements
  arr.push_back(1.0f);
  arr.push_back(2.0f);
  arr.push_back(3.0f);

  assert(arr.size() == 3);
  assert(!arr.empty());
  assert(arr[0] == 1.0f);
  assert(arr[1] == 2.0f);
  assert(arr[2] == 3.0f);

  // Front/back
  assert(arr.front() == 1.0f);
  assert(arr.back() == 3.0f);

  std::cout << "  ChunkedArray basic: PASSED\n";
}

void TestChunkedArrayLarge() {
  std::cout << "Testing ChunkedArray with large data...\n";

  constexpr size_t N = 100000;
  ChunkedArray<float> arr;
  arr.reserve(N);

  for (size_t i = 0; i < N; ++i) {
    arr.push_back(static_cast<float>(i));
  }

  assert(arr.size() == N);
  assert(arr.chunk_count() > 1);  // Should span multiple chunks

  // Verify data
  for (size_t i = 0; i < N; ++i) {
    assert(arr[i] == static_cast<float>(i));
  }

  // Test flatten
  std::vector<float> flat = arr.flatten();
  assert(flat.size() == N);
  for (size_t i = 0; i < N; ++i) {
    assert(flat[i] == static_cast<float>(i));
  }

  std::cout << "  ChunkedArray large: PASSED\n";
  std::cout << "    Chunk count: " << arr.chunk_count() << "\n";
  std::cout << "    Memory usage: " << arr.memory_usage() / 1024 << " KB\n";
}

void TestChunkedArrayAppend() {
  std::cout << "Testing ChunkedArray append...\n";

  ChunkedArray<int32_t> arr;
  std::vector<int32_t> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

  arr.append(data.data(), data.size());

  assert(arr.size() == 10);
  for (size_t i = 0; i < 10; ++i) {
    assert(arr[i] == static_cast<int32_t>(i + 1));
  }

  std::cout << "  ChunkedArray append: PASSED\n";
}

void TestChunkedArrayIterator() {
  std::cout << "Testing ChunkedArray iterator...\n";

  ChunkedArray<int> arr;
  for (int i = 0; i < 100; ++i) {
    arr.push_back(i);
  }

  int sum = 0;
  for (auto it = arr.begin(); it != arr.end(); ++it) {
    sum += *it;
  }
  assert(sum == 4950);  // Sum of 0..99

  // Range-based for
  sum = 0;
  for (int val : arr) {
    sum += val;
  }
  assert(sum == 4950);

  std::cout << "  ChunkedArray iterator: PASSED\n";
}

//
// RenderData Tests
//

void TestRenderMesh() {
  std::cout << "Testing RenderMesh...\n";

  RenderMesh mesh;
  mesh.name = "TestMesh";
  mesh.prim_path = "/World/TestMesh";

  // Add a simple quad
  mesh.face_vertex_counts.push_back(4);
  mesh.face_vertex_indices.push_back(0);
  mesh.face_vertex_indices.push_back(1);
  mesh.face_vertex_indices.push_back(2);
  mesh.face_vertex_indices.push_back(3);

  // Add points
  float points[] = {
    -1, -1, 0,
     1, -1, 0,
     1,  1, 0,
    -1,  1, 0
  };
  mesh.points.append(points, 12);

  assert(mesh.point_count() == 4);
  assert(mesh.face_count() == 1);
  assert(!mesh.has_normals());
  assert(!mesh.is_triangulated);

  std::cout << "  RenderMesh: PASSED\n";
  std::cout << "    Memory usage: " << mesh.memory_usage() << " bytes\n";
}

void TestRenderScene() {
  std::cout << "Testing RenderScene...\n";

  RenderScene scene;
  scene.name = "TestScene";
  scene.up_axis = RenderScene::UpAxis::Y;
  scene.meters_per_unit = 0.01f;

  // Add a node
  SceneNode node;
  node.name = "Root";
  node.prim_path = "/Root";
  node.type = NodeType::Xform;
  scene.nodes.push_back(std::move(node));
  scene.root_nodes.push_back(0);

  // Add a mesh
  RenderMesh mesh;
  mesh.name = "Cube";
  mesh.prim_path = "/Root/Cube";
  scene.meshes.push_back(std::move(mesh));

  auto stats = scene.get_stats();
  assert(stats.node_count == 1);
  assert(stats.mesh_count == 1);
  assert(scene.get_node(0) == &scene.nodes[0]);
  assert(scene.get_mesh(0) == &scene.meshes[0]);
  assert(scene.get_node(-1) == nullptr);
  assert(scene.get_mesh(9) == nullptr);
  assert(scene.get_material(-1) == nullptr);
  assert(scene.get_point_instancer(0) == nullptr);
  assert(scene.get_point_instance_draw(0) == nullptr);

  std::cout << "  RenderScene: PASSED\n";
}

//
// Scene Access Tests
//

void TestSceneAccess() {
  std::cout << "Testing Scene Access...\n";

  // Create a simple stage
  const char* usda = R"(#usda 1.0
(
    defaultPrim = "World"
    upAxis = "Y"
)

def Xform "World"
{
    def Mesh "Cube"
    {
        int[] faceVertexCounts = [4, 4, 4, 4, 4, 4]
        int[] faceVertexIndices = [0, 1, 3, 2, 2, 3, 5, 4, 4, 5, 7, 6, 6, 7, 1, 0, 1, 7, 5, 3, 6, 0, 2, 4]
        point3f[] points = [(-0.5, -0.5, 0.5), (0.5, -0.5, 0.5), (-0.5, 0.5, 0.5), (0.5, 0.5, 0.5), (-0.5, 0.5, -0.5), (0.5, 0.5, -0.5), (-0.5, -0.5, -0.5), (0.5, -0.5, -0.5)]
    }

    def Camera "MainCamera"
    {
        float focalLength = 50
        float horizontalAperture = 36
        float verticalAperture = 24
    }
}
)";

  LoadResult result = LoadUSDAFromString(usda, std::strlen(usda));
  if (!result.success) {
    std::cout << "  SKIPPED (failed to parse test USDA: " << result.error_summary << ")\n";
    return;
  }

  Stage& stage = result.stage;

  // Find meshes
  auto meshes = FindMeshes(stage);
  assert(meshes.size() == 1);
  assert(meshes[0].GetName() == "Cube");
  assert(IsMesh(meshes[0]));

  // Find cameras
  auto cameras = FindCameras(stage);
  assert(cameras.size() == 1);
  assert(cameras[0].GetName() == "MainCamera");
  assert(IsCamera(cameras[0]));

  // Test attribute access
  float focal_length = 0;
  bool got_fl = GetFloat(cameras[0], "focalLength", &focal_length);
  assert(got_fl);
  assert(std::abs(focal_length - 50.0f) < 0.001f);

  std::cout << "  Scene Access: PASSED\n";
}

void TestRenderExtract() {
  std::cout << "Testing RenderExtract...\n";

  const char* usda = R"(#usda 1.0
(
    defaultPrim = "World"
)

def Xform "World"
{
    token purpose = "render"

    def Mesh "MeshA"
    {
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    }

    def PointInstancer "Inst"
    {
        rel prototypes = </World/MeshA>
        point3f[] positions = [(0, 0, 0), (3, 0, 0)]
        int[] protoIndices = [0, 0]
        quatf[] orientations = [(1, 0, 0, 0), (1, 0, 0, 0)]
        float3[] scales = [(1, 1, 1), (2, 2, 2)]
        float3[] velocities = [(0, 0, 1), (0, 0, 2)]
        float3[] angularVelocities = [(0, 1, 0), (0, 2, 0)]
        int64[] ids = [10, 11]
        int64[] invisibleIds = [11]
    }

    def Camera "Cam" {}
    def RectLight "Key" {}
    def Material "Mat" {}
    def Volume "Fog" {}
    def BasisCurves "Curve" {}
}
)";

  LoadResult lr = LoadUSDAFromString(usda, std::strlen(usda));
  if (!lr.success) {
    std::cout << "  SKIPPED (failed to parse test USDA: " << lr.error_summary << ")\n";
    return;
  }

  RenderExtractOptions opts;
  opts.time_code = 0.0;
  opts.stop_at_point_instancers = true;
  opts.stop_at_native_instances = true;
  RenderExtractResult er;
  assert(CollectRenderPrims(lr.stage, opts, &er));
  assert(er.meshes.size() == 1);
  assert(er.point_instancers.size() == 1);
  assert(er.cameras.size() == 1);
  assert(er.lights.size() == 1);
  assert(er.materials.size() == 1);
  assert(er.volumes.size() == 1);
  assert(er.curves.size() == 1);
  assert(er.meshes[0].purpose == "render");
  assert(er.meshes[0].path == "/World/MeshA");
  PointInstancerData pid;
  assert(ReadPointInstancerData(er.point_instancers[0].prim, 0.0, &pid));
  assert(pid.valid);
  assert(pid.path == "/World/Inst");
  assert(pid.prototypes.size() == 1);
  assert(pid.prototypes[0].str() == "/World/MeshA");
  assert(pid.proto_indices == std::vector<int32_t>({0, 0}));
  assert(pid.positions == std::vector<float>({0, 0, 0, 3, 0, 0}));
  assert(pid.scales == std::vector<float>({1, 1, 1, 2, 2, 2}));
  assert(pid.velocities == std::vector<float>({0, 0, 1, 0, 0, 2}));
  assert(pid.angular_velocities == std::vector<float>({0, 1, 0, 0, 2, 0}));
  assert(pid.ids == std::vector<int64_t>({10, 11}));
  assert(pid.invisible_ids == std::vector<int64_t>({11}));
  assert(pid.transforms.size() == 2);
  assert(pid.transforms[1].matrix[0] == 2.0);
  assert(pid.transforms[1].matrix[12] == 3.0);

  std::unordered_set<std::string> prototypes;
  CollectPrototypePaths(lr.stage, &prototypes);
  assert(prototypes.empty());

  std::cout << "  RenderExtract: PASSED\n";
}

//
// Converter Tests
//

void TestRenderConverter() {
  std::cout << "Testing RenderConverter...\n";

  const char* usda = R"(#usda 1.0
(
    defaultPrim = "World"
    upAxis = "Y"
    metersPerUnit = 0.01
)

def Xform "World"
{
    def Xform "Animated"
    {
        double3 xformOp:translate.timeSamples = {
            0: (0, 0, 0),
            1: (1, 2, 3),
        }
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    def Xform "Rotating"
    {
        double xformOp:rotateY.timeSamples = {
            0: 0,
            1: 90,
        }
        uniform token[] xformOpOrder = ["xformOp:rotateY"]
    }

    def Xform "Hidden"
    {
        token visibility = "invisible"

        def Mesh "HiddenPlane"
        {
            int[] faceVertexCounts = [3]
            int[] faceVertexIndices = [0, 1, 2]
            point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
        }
    }

    def Mesh "Plane"
    {
        rel material:binding = </World/AnimatedMaterial>
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [(-1, 0, -1), (1, 0, -1), (1, 0, 1), (-1, 0, 1)]
        normal3f[] normals = [(0, 1, 0), (0, 1, 0), (0, 1, 0), (0, 1, 0)]
        texCoord2f[] primvars:st = [(0, 0), (1, 0), (1, 1), (0, 1)] (
            interpolation = "vertex"
        )
    }

    def Xform "ProtoGroup"
    {
        rel material:binding = </World/AnimatedMaterial>

        def Mesh "NestedPlane"
        {
            double3 xformOp:translate = (1, 0, 0)
            uniform token[] xformOpOrder = ["xformOp:translate"]
            int[] faceVertexCounts = [3]
            int[] faceVertexIndices = [0, 1, 2]
            point3f[] points = [(0, 0, 0), (0, 0, 1), (1, 0, 0)]
        }
    }

    def PointInstancer "Inst"
    {
        rel prototypes = [</World/Plane>, </World/ProtoGroup>]
        point3f[] positions = [(0, 0, 0), (4, 0, 0), (8, 0, 0)]
        int[] protoIndices = [0, 1, 1]
        quatf[] orientations = [(1, 0, 0, 0), (1, 0, 0, 0), (1, 0, 0, 0)]
        float3[] scales = [(1, 1, 1), (0.5, 0.5, 0.5), (2, 2, 2)]
        float3[] velocities = [(0, 0, 1), (0, 0, 2), (0, 0, 3)]
        float3[] angularVelocities = [(0, 1, 0), (0, 2, 0), (0, 3, 0)]
        int64[] ids = [20, 21, 22]
        int64[] invisibleIds = [22]
    }

    def Skeleton "Rig"
    {
        uniform token[] joints = ["Root", "Root/Spine"]
    }

    def Camera "AnimatedCamera"
    {
        float focalLength.timeSamples = {
            0: 35,
            1: 50,
        }
    }

    def SphereLight "AnimatedLight"
    {
        float intensity.timeSamples = {
            0: 10,
            1: 20,
        }
    }

    def Material "AnimatedMaterial"
    {
        float renderer:opacity.timeSamples = {
            0: 1,
            1: 0.5,
        }
        def Shader "PreviewSurface"
        {
            uniform token info:id = "UsdPreviewSurface"
            color3f inputs:diffuseColor = (0.18, 0.18, 0.18)
        }
    }
}
)";

  LoadResult load_result = LoadUSDAFromString(usda, std::strlen(usda));
  if (!load_result.success) {
    std::cout << "  SKIPPED (failed to parse test USDA)\n";
    return;
  }

  ConverterConfig config;
  config.mesh.triangulate = true;
  config.mesh.compute_normals = false;  // Already has normals

  RenderSceneConverter converter(config);
  ConvertResult result = converter.Convert(load_result.stage);

  if (!result.success) {
    std::cout << "  FAILED: " << result.error << "\n";
    return;
  }

  assert(result.scene.meshes.size() == 3);
  assert(result.scene.point_instancers.size() == 1);
  assert(result.scene.get_stats().point_instancer_count == 1);
  assert(result.scene.get_stats().point_instance_count == 3);
  assert(result.scene.get_stats().visible_point_instance_count == 2);
  assert(result.scene.get_stats().point_instance_draw_count == 2);
  assert(result.scene.has_valid_point_instance_draw_ranges());

  const RenderMesh* mesh = &result.scene.meshes[0];
  if (mesh->name != "Plane") {
    mesh = &result.scene.meshes[1];
  }
  assert(mesh->name == "Plane");
  assert(mesh->point_count() == 4);
  assert(mesh->is_triangulated);
  assert(mesh->triangulated_indices.size() == 6);  // 2 triangles

  // Verify triangulation
  // Quad 0,1,2,3 should become triangles (0,1,2) and (0,2,3)
  assert(mesh->triangulated_indices[0] == 0);
  assert(mesh->triangulated_indices[1] == 1);
  assert(mesh->triangulated_indices[2] == 2);
  assert(mesh->triangulated_indices[3] == 0);
  assert(mesh->triangulated_indices[4] == 2);
  assert(mesh->triangulated_indices[5] == 3);

  auto plane_node_it = result.scene.node_by_path.find("/World/Plane");
  assert(plane_node_it != result.scene.node_by_path.end());
  assert(result.scene.nodes[plane_node_it->second].data_id >= 0);
  assert(static_cast<size_t>(result.scene.nodes[plane_node_it->second].data_id) <
         result.scene.meshes.size());
  assert(result.scene.meshes[result.scene.nodes[plane_node_it->second].data_id].name ==
         "Plane");
  assert(result.scene.meshes[result.scene.nodes[plane_node_it->second].data_id].material_id >=
         0);

  auto inst_node_it = result.scene.node_by_path.find("/World/Inst");
  assert(inst_node_it != result.scene.node_by_path.end());
  const SceneNode& inst_node = result.scene.nodes[inst_node_it->second];
  assert(inst_node.type == NodeType::PointInstancer);
  assert(inst_node.data_id == 0);
  const RenderPointInstancer& instancer = result.scene.point_instancers[0];
  assert(instancer.name == "Inst");
  assert(instancer.prim_path == "/World/Inst");
  assert(instancer.valid);
  assert(instancer.prototype_paths ==
         std::vector<std::string>({"/World/Plane", "/World/ProtoGroup"}));
  assert(instancer.prototype_node_ids.size() == 2);
  assert(instancer.prototype_node_ids[0] >= 0);
  assert(instancer.prototype_node_ids[1] >= 0);
  assert(instancer.prototype_mesh_offsets == std::vector<uint32_t>({0, 1, 2}));
  assert(instancer.has_valid_prototype_mesh_bindings());
  assert(instancer.prototype_mesh_ids.size() == 2);
  assert(instancer.prototype_mesh_transforms.size() == 2);
  assert(instancer.prototype_mesh_count(0) == 1);
  assert(instancer.prototype_mesh_count(1) == 1);
  assert(std::abs(instancer.prototype_mesh_transforms[1].m[12] - 1.0f) < 0.001f);
  assert(instancer.proto_indices == std::vector<int32_t>({0, 1, 1}));
  assert(instancer.positions == std::vector<float>({0, 0, 0, 4, 0, 0, 8, 0, 0}));
  assert(instancer.scales == std::vector<float>({1, 1, 1, 0.5f, 0.5f, 0.5f, 2, 2, 2}));
  assert(instancer.velocities == std::vector<float>({0, 0, 1, 0, 0, 2, 0, 0, 3}));
  assert(instancer.angular_velocities == std::vector<float>({0, 1, 0, 0, 2, 0, 0, 3, 0}));
  assert(instancer.has_velocities());
  assert(instancer.has_angular_velocities());
  assert(instancer.ids == std::vector<int64_t>({20, 21, 22}));
  assert(instancer.invisible_ids == std::vector<int64_t>({22}));
  assert(instancer.instance_visible == std::vector<uint8_t>({1, 1, 0}));
  assert(instancer.visible_instance_count() == 2);
  assert(instancer.transforms.size() == 3);
  assert(std::abs(instancer.transforms[1].m[0] - 0.5f) < 0.001f);
  assert(std::abs(instancer.transforms[1].m[12] - 4.0f) < 0.001f);
  assert(result.scene.point_instance_draws.size() == 2);
  assert(instancer.draw_start == 0);
  assert(instancer.draw_count == 2);
  assert(instancer.has_valid_draw_range(result.scene.point_instance_draws.size()));
  assert(result.scene.get_point_instancer(0) == &result.scene.point_instancers[0]);
  assert(result.scene.get_point_instance_draw(0) == &result.scene.point_instance_draws[0]);
  assert(result.scene.get_point_instance_draw(9) == nullptr);
  RenderPointInstanceDrawRange draw_range = result.scene.get_point_instancer_draws(0);
  assert(draw_range.size == 2);
  assert(!draw_range.empty());
  assert(draw_range.data == result.scene.point_instance_draws.data());
  assert(draw_range.begin() == result.scene.point_instance_draws.data());
  assert(draw_range.end() == result.scene.point_instance_draws.data() + 2);
  assert(draw_range[1].instance_index == 1);
  assert(result.scene.get_point_instancer_draws(-1).empty());
  assert(result.scene.get_point_instancer_draws(9).empty());
  RenderPointInstanceDrawView draw_view = result.scene.get_point_instance_draw_view(0);
  assert(draw_view.valid());
  assert(draw_view.instancer == &instancer);
  assert(draw_view.mesh == result.scene.get_mesh(result.scene.point_instance_draws[0].mesh_id));
  assert(draw_view.material ==
         result.scene.get_material(result.scene.point_instance_draws[0].material_id));
  assert(!result.scene.get_point_instance_draw_view(9).valid());
  assert(result.scene.point_instance_draws[0].point_instancer_id == 0);
  assert(result.scene.point_instance_draws[0].instance_index == 0);
  assert(result.scene.point_instance_draws[0].prototype_index == 0);
  assert(result.scene.point_instance_draws[0].mesh_id == instancer.prototype_mesh_ids[0]);
  assert(result.scene.get_mesh(result.scene.point_instance_draws[0].mesh_id));
  assert(result.scene.point_instance_draws[0].material_id >= 0);
  assert(result.scene.get_material(result.scene.point_instance_draws[0].material_id));
  assert(result.scene.point_instance_draws[0].expanded_mesh_id == -1);
  assert(result.scene.point_instance_draws[1].point_instancer_id == 0);
  assert(result.scene.point_instance_draws[1].instance_index == 1);
  assert(result.scene.point_instance_draws[1].prototype_index == 1);
  assert(result.scene.point_instance_draws[1].mesh_id == instancer.prototype_mesh_ids[1]);
  assert(result.scene.point_instance_draws[1].material_id >= 0);
  assert(result.scene.point_instance_draws[0].material_id ==
         result.scene.point_instance_draws[1].material_id);
  assert(std::abs(result.scene.point_instance_draws[1].transform.m[12] - 4.5f) < 0.001f);

  ConverterConfig duplicate_config = config;
  duplicate_config.point_instancer.duplicate_meshes = true;
  RenderSceneConverter duplicate_converter(duplicate_config);
  ConvertResult duplicate_result = duplicate_converter.Convert(load_result.stage);
  assert(duplicate_result.success);
  assert(duplicate_result.scene.point_instance_draws.size() == 2);
  assert(duplicate_result.scene.meshes.size() == 5);
  assert(duplicate_result.scene.point_instance_draws[0].expanded_mesh_id >= 3);
  assert(duplicate_result.scene.point_instance_draws[1].expanded_mesh_id >= 3);
  RenderPointInstanceDrawView duplicate_view =
      duplicate_result.scene.get_point_instance_draw_view(1);
  assert(duplicate_view.valid());
  assert(duplicate_view.expanded_mesh);
  assert(duplicate_view.expanded_mesh->material_id ==
         duplicate_view.draw->material_id);
  assert(duplicate_view.expanded_mesh->point_count() == duplicate_view.mesh->point_count());
  assert(std::abs(duplicate_view.expanded_mesh->points[0] - 4.5f) < 0.001f);
  assert(std::abs(duplicate_view.expanded_mesh->points[1]) < 0.001f);
  assert(std::abs(duplicate_view.expanded_mesh->points[2]) < 0.001f);
  assert(duplicate_result.scene.has_valid_point_instance_draw_ranges());

  auto inst_lookup_it = result.scene.point_instancer_by_path.find("/World/Inst");
  assert(inst_lookup_it != result.scene.point_instancer_by_path.end());
  assert(inst_lookup_it->second == 0);

  auto hidden_it = result.scene.node_by_path.find("/World/Hidden/HiddenPlane");
  assert(hidden_it != result.scene.node_by_path.end());
  assert(!result.scene.nodes[hidden_it->second].visible);
  assert(result.scene.nodes[hidden_it->second].data_id >= 0);

  const AnimationClip* translate_clip = nullptr;
  const AnimationClip* rotate_clip = nullptr;
  const AnimationClip* camera_clip = nullptr;
  const AnimationClip* light_clip = nullptr;
  const AnimationClip* material_clip = nullptr;
  for (const AnimationClip& clip : result.scene.animations) {
    if (clip.prim_path == "/World/Animated") translate_clip = &clip;
    if (clip.prim_path == "/World/Rotating") rotate_clip = &clip;
    if (clip.prim_path == "/World/AnimatedCamera") camera_clip = &clip;
    if (clip.prim_path == "/World/AnimatedLight") light_clip = &clip;
    if (clip.prim_path == "/World/AnimatedMaterial") material_clip = &clip;
  }
  assert(translate_clip);
  assert(translate_clip->channels.size() == 1);
  assert(translate_clip->channels[0].target_path == AnimationChannel::TargetPath::Translation);
  assert(translate_clip->channels[0].keyframes.size() == 2);
  assert(std::abs(translate_clip->channels[0].keyframes[1].value.x - 1.0f) < 0.001f);
  assert(std::abs(translate_clip->channels[0].keyframes[1].value.y - 2.0f) < 0.001f);
  assert(std::abs(translate_clip->channels[0].keyframes[1].value.z - 3.0f) < 0.001f);

  assert(rotate_clip);
  assert(rotate_clip->channels.size() == 1);
  assert(rotate_clip->channels[0].target_path == AnimationChannel::TargetPath::Rotation);
  assert(rotate_clip->channels[0].keyframes.size() == 2);
  assert(std::abs(rotate_clip->channels[0].keyframes[1].value.x) < 0.001f);
  assert(std::abs(rotate_clip->channels[0].keyframes[1].value.y - 90.0f) < 0.001f);
  assert(std::abs(rotate_clip->channels[0].keyframes[1].value.z) < 0.001f);

  auto assert_custom_clip = [](const AnimationClip* clip,
                               const char* prop_name,
                               float expected_last) {
    assert(clip);
    assert(clip->channels.size() == 1);
    const AnimationChannel& ch = clip->channels[0];
    assert(ch.target_path == AnimationChannel::TargetPath::CustomProperty);
    assert(ch.property_name == prop_name);
    assert(ch.target_prim_path == clip->prim_path);
    assert(ch.keyframes.size() == 2);
    assert(std::abs(ch.keyframes[1].value.x - expected_last) < 0.001f);
  };
  assert_custom_clip(camera_clip, "focalLength", 50.0f);
  assert_custom_clip(light_clip, "intensity", 20.0f);
  assert_custom_clip(material_clip, "renderer:opacity", 0.5f);

  assert(result.scene.skeletons.size() == 1);
  assert(result.scene.skeletons[0].joints.size() == 2);
  assert(result.scene.skeletons[0].joints[1].parent_id == 0);
  auto rig_node_it = result.scene.node_by_path.find("/World/Rig");
  assert(rig_node_it != result.scene.node_by_path.end());
  assert(result.scene.nodes[rig_node_it->second].data_id == 0);

  std::cout << "  RenderConverter: PASSED\n";

  // Print warnings
  for (const auto& warn : result.warnings) {
    std::cout << "    Warning: " << warn << "\n";
  }
}

void TestRenderConverterMaterials() {
  std::cout << "Testing RenderConverter material extraction...\n";

  const char* usda = R"(#usda 1.0
(
    defaultPrim = "World"
)

def Xform "World"
{
    def Material "PreviewMat"
    {
        token outputs:surface.connect = </World/PreviewMat/Surface.outputs:surface>

        def Shader "Surface"
        {
            uniform token info:id = "UsdPreviewSurface"
            color3f inputs:diffuseColor = (0.2, 0.4, 0.6)
            float inputs:roughness = 0.25
            float inputs:metallic.connect = </World/PreviewMat/MetallicTex.outputs:r>
            float inputs:opacity = 0.5
            int inputs:useSpecularWorkflow = 1
            token outputs:surface
        }

        def Shader "MetallicTex"
        {
            uniform token info:id = "UsdUVTexture"
            asset inputs:file = @metallic.png@
            token inputs:wrapS = "repeat"
            token inputs:wrapT = "clamp"
            float4 inputs:scale = (2, 1, 1, 1)
            float4 inputs:bias = (0.1, 0, 0, 0)
            token inputs:sourceColorSpace = "raw"
            float outputs:r
        }
    }

    def Material "OpenPBRMat"
    {
        token outputs:surface.connect = </World/OpenPBRMat/Surface.outputs:surface>

        def Shader "Surface"
        {
            uniform token info:id = "ND_open_pbr_surface_surfaceshader"
            color3f inputs:base_color.connect = </World/OpenPBRMat/NG.outputs:out>
            float inputs:base_metalness = 0.8
            float inputs:base_roughness = 0.35
            float inputs:opacity = 1.0
            token outputs:surface
        }

        def NodeGraph "NG"
        {
            color3f outputs:out.connect = </World/OpenPBRMat/NG/Constant.outputs:out>

            def Shader "Constant"
            {
                color3f outputs:out = (0.7, 0.6, 0.5)
            }
        }
    }
}
)";

  LoadResult load_result = LoadUSDAFromString(usda, std::strlen(usda));
  if (!load_result.success) {
    std::cout << "  SKIPPED (failed to parse material USDA: "
              << load_result.error_summary << ")\n";
    return;
  }

  ConverterConfig config;
  config.material.load_textures = false;

  RenderSceneConverter converter(config);
  ConvertResult result = converter.Convert(load_result.stage);
  assert(result.success);
  assert(result.scene.materials.size() == 2);

  auto preview_it = result.scene.material_by_path.find("/World/PreviewMat");
  assert(preview_it != result.scene.material_by_path.end());
  const RenderMaterial& preview = result.scene.materials[preview_it->second];
  assert(preview.shader_type == RenderMaterial::ShaderType::PreviewSurface);
  assert(preview.preview_surface);
  assert(std::abs(preview.preview_surface->diffuse_color.value.x - 0.2f) < 0.001f);
  assert(std::abs(preview.preview_surface->diffuse_color.value.y - 0.4f) < 0.001f);
  assert(std::abs(preview.preview_surface->diffuse_color.value.z - 0.6f) < 0.001f);
  assert(std::abs(preview.preview_surface->roughness.value.x - 0.25f) < 0.001f);
  assert(preview.preview_surface->metallic.is_texture());
  assert(preview.preview_surface->use_specular_workflow);
  assert(preview.alpha_mode == RenderMaterial::AlphaMode::Blend);

  assert(result.scene.images.size() == 1);
  assert(result.scene.images[0].resolved_path == "metallic.png");
  assert(result.scene.images[0].color_space == ColorSpace::Raw);
  assert(result.scene.textures.size() == 1);
  const RenderTexture& tex = result.scene.textures[0];
  assert(tex.image_id == 0);
  assert(tex.output_channel == RenderTexture::Channel::R);
  assert(tex.wrap_s == WrapMode::Repeat);
  assert(tex.wrap_t == WrapMode::Clamp);
  assert(std::abs(tex.scale_value.x - 2.0f) < 0.001f);
  assert(std::abs(tex.bias.x - 0.1f) < 0.001f);

  auto openpbr_it = result.scene.material_by_path.find("/World/OpenPBRMat");
  assert(openpbr_it != result.scene.material_by_path.end());
  const RenderMaterial& openpbr = result.scene.materials[openpbr_it->second];
  assert(openpbr.shader_type == RenderMaterial::ShaderType::OpenPBR);
  assert(openpbr.openpbr);
  assert(std::abs(openpbr.openpbr->base_color.value.x - 0.7f) < 0.001f);
  assert(std::abs(openpbr.openpbr->base_color.value.y - 0.6f) < 0.001f);
  assert(std::abs(openpbr.openpbr->base_color.value.z - 0.5f) < 0.001f);
  assert(std::abs(openpbr.openpbr->base_metalness.value.x - 0.8f) < 0.001f);
  assert(std::abs(openpbr.openpbr->base_roughness.value.x - 0.35f) < 0.001f);

  std::cout << "  RenderConverter materials: PASSED\n";
}

void TestRenderConverterPointInstancerWarnings() {
  std::cout << "Testing RenderConverter PointInstancer diagnostics...\n";

  const char* usda = R"(#usda 1.0
(
    defaultPrim = "World"
)

def Xform "World"
{
    def PointInstancer "Inst"
    {
        rel prototypes = </World/Missing>
        point3f[] positions = [(0, 0, 0)]
        int[] protoIndices = [0]
    }
}
)";

  LoadResult load_result = LoadUSDAFromString(usda, std::strlen(usda));
  if (!load_result.success) {
    std::cout << "  SKIPPED (failed to parse diagnostic USDA)\n";
    return;
  }

  RenderSceneConverter converter;
  ConvertResult result = converter.Convert(load_result.stage);
  assert(result.success);
  assert(result.scene.point_instancers.size() == 1);
  assert(result.scene.point_instancers[0].valid);
  assert(result.scene.point_instancers[0].prototype_node_ids.size() == 1);
  assert(result.scene.point_instancers[0].prototype_node_ids[0] == -1);
  assert(result.scene.point_instancers[0].draw_count == 0);
  assert(result.scene.point_instance_draws.empty());
  assert(result.scene.get_point_instancer_draws(0).empty());
  assert(result.scene.has_valid_point_instance_draw_ranges());

  bool found_warning = false;
  for (const std::string& warning : result.warnings) {
    if (warning.find("Unresolved PointInstancer prototype") != std::string::npos &&
        warning.find("/World/Missing") != std::string::npos) {
      found_warning = true;
    }
  }
  assert(found_warning);

  std::cout << "  RenderConverter PointInstancer diagnostics: PASSED\n";
}

void TestRenderConverterPointInstancerIndexVisibility() {
  std::cout << "Testing RenderConverter PointInstancer index visibility...\n";

  const char* usda = R"(#usda 1.0
(
    defaultPrim = "World"
)

def Xform "World"
{
    def Mesh "Proto"
    {
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    }

    def PointInstancer "Inst"
    {
        rel prototypes = </World/Proto>
        point3f[] positions = [(0, 0, 0), (2, 0, 0), (4, 0, 0)]
        int[] protoIndices = [0, 0, 0]
        int64[] invisibleIds = [2]
        int64[] inactiveIds = [0]
    }
}
)";

  LoadResult load_result = LoadUSDAFromString(usda, std::strlen(usda));
  if (!load_result.success) {
    std::cout << "  SKIPPED (failed to parse index-visibility USDA)\n";
    return;
  }

  RenderSceneConverter converter;
  ConvertResult result = converter.Convert(load_result.stage);
  assert(result.success);
  assert(result.scene.point_instancers.size() == 1);
  assert(result.scene.point_instance_draws.size() == 1);
  assert(result.scene.get_stats().point_instance_count == 3);
  assert(result.scene.get_stats().visible_point_instance_count == 1);
  assert(result.scene.get_stats().point_instance_draw_count == 1);
  assert(result.scene.has_valid_point_instance_draw_ranges());

  const RenderPointInstancer& instancer = result.scene.point_instancers[0];
  assert(instancer.ids.empty());
  assert(instancer.invisible_ids == std::vector<int64_t>({2}));
  assert(instancer.inactive_ids == std::vector<int64_t>({0}));
  assert(instancer.instance_visible == std::vector<uint8_t>({0, 1, 0}));
  assert(instancer.draw_start == 0);
  assert(instancer.draw_count == 1);

  const RenderPointInstanceDraw& draw = result.scene.point_instance_draws[0];
  assert(draw.point_instancer_id == 0);
  assert(draw.instance_index == 1);
  assert(draw.prototype_index == 0);
  assert(std::abs(draw.transform.m[12] - 2.0f) < 0.001f);
  RenderPointInstanceDrawRange range = result.scene.get_point_instancer_draws(0);
  assert(range.size == 1);
  assert(range[0].instance_index == 1);

  std::cout << "  RenderConverter PointInstancer index visibility: PASSED\n";
}

void TestMaterialXUtilities() {
  std::cout << "Testing MaterialX utilities...\n";

  const char* mtlx = R"(<materialx version="1.38">
  <surfacematerial name="Mat" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="MatSurface" />
  </surfacematerial>
  <nodegraph name="NG">
    <output name="out" type="color3" nodename="Scale" />
    <node name="Color" category="constant" type="color3">
      <input name="value" type="color3" value="0.8, 0.4, 0.2" />
    </node>
    <node name="Scale" category="multiply" type="color3">
      <input name="in1" type="color3" nodename="Color" />
      <input name="in2" type="color3" value="0.5, 0.5, 0.5" />
    </node>
  </nodegraph>
  <node name="MatSurface" category="standard_surface" type="surfaceshader">
    <input name="base" type="float" value="1.0" />
    <input name="base_color" type="color3" nodegraph="NG" output="out" />
    <input name="metalness" type="float" value="0.25" />
    <input name="specular_roughness" type="float" value="0.35" />
    <input name="opacity" type="color3" value="0.5, 0.5, 0.5" />
  </node>
</materialx>
)";

  MtlxConverter converter;
  RenderMaterial from_string;
  if (!converter.ConvertToRenderMaterial(mtlx, "Mat", &from_string)) {
    std::cout << "  MaterialX string conversion failed: "
              << converter.GetError() << "\n";
    assert(false);
  }
  assert(from_string.shader_type == RenderMaterial::ShaderType::PreviewSurface);
  assert(from_string.preview_surface);
  assert(std::abs(from_string.preview_surface->diffuse_color.value.x - 0.4f) < 0.001f);
  assert(std::abs(from_string.preview_surface->diffuse_color.value.y - 0.2f) < 0.001f);
  assert(std::abs(from_string.preview_surface->diffuse_color.value.z - 0.1f) < 0.001f);
  assert(std::abs(from_string.preview_surface->metallic.value.x - 0.25f) < 0.001f);
  assert(std::abs(from_string.preview_surface->roughness.value.x - 0.35f) < 0.001f);
  assert(std::abs(from_string.preview_surface->opacity.value.x - 0.5f) < 0.001f);

  const char* path = "next_tydra_materialx_test.mtlx";
  {
    std::ofstream ofs(path, std::ios::out | std::ios::binary);
    assert(ofs);
    ofs << mtlx;
    assert(ofs.good());
  }

  RenderMaterial from_file;
  if (!converter.ConvertFileToRenderMaterial(path, "Mat", &from_file)) {
    std::cout << "  MaterialX file conversion failed: "
              << converter.GetError() << "\n";
    assert(false);
  }
  std::remove(path);
  assert(from_file.shader_type == RenderMaterial::ShaderType::PreviewSurface);
  assert(from_file.preview_surface);
  assert(std::abs(from_file.preview_surface->diffuse_color.value.x - 0.4f) < 0.001f);

  const char* usda = R"(#usda 1.0

def Material "Mat"
{
    string config:mtlx:sourceUri = "lookdev/materials.mtlx"
    token outputs:mtlx:surface.connect = </Mat/OpenPBR.outputs:out>

    def Shader "OpenPBR"
    {
        uniform token info:id = "ND_open_pbr_surface_surfaceshader"
        color3f inputs:base_color = (0.1, 0.2, 0.3)
        float inputs:base_metalness = 0.4
        token outputs:out
    }
}
)";

  LoadResult lr = LoadUSDAFromString(usda, std::strlen(usda));
  assert(lr.success);
  UsdPrim mat = lr.stage.GetPrimAtPath("/Mat");
  assert(mat);
  assert(HasMtlxBinding(mat));
  assert(GetMtlxFilePath(mat) == "lookdev/materials.mtlx");

  RenderMaterial usd_mat;
  if (!converter.ConvertUsdMtlxMaterial(lr.stage, mat, &usd_mat)) {
    std::cout << "  USD MaterialX conversion failed: "
              << converter.GetError() << "\n";
    assert(false);
  }
  assert(usd_mat.shader_type == RenderMaterial::ShaderType::OpenPBR);
  assert(usd_mat.openpbr);
  assert(std::abs(usd_mat.openpbr->base_color.value.x - 0.1f) < 0.001f);
  assert(std::abs(usd_mat.openpbr->base_metalness.value.x - 0.4f) < 0.001f);

  std::cout << "  MaterialX utilities: PASSED\n";
}

//
// Main
//

int main() {
  std::cout << "=== Tydra Next Unit Tests ===\n\n";

  // ChunkedArray tests
  TestChunkedArrayBasic();
  TestChunkedArrayLarge();
  TestChunkedArrayAppend();
  TestChunkedArrayIterator();

  std::cout << "\n";

  // RenderData tests
  TestRenderMesh();
  TestRenderScene();

  std::cout << "\n";

  // Scene Access tests
  TestSceneAccess();
  TestRenderExtract();

  std::cout << "\n";

  // Converter tests
  TestRenderConverter();
  TestRenderConverterMaterials();
  TestRenderConverterPointInstancerWarnings();
  TestRenderConverterPointInstancerIndexVisibility();
  TestMaterialXUtilities();

  std::cout << "\n=== All Tydra Next tests PASSED ===\n";
  return 0;
}
