// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Tydra Next - Unit Tests

#include <algorithm>
#include <iostream>
#include <cassert>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <unordered_set>

#include "tydra/next/chunked-array.hh"
#include "tydra/next/materialx.hh"
#include "tydra/next/render-data.hh"
#include "tydra/next/scene-access.hh"
#include "tydra/next/render-extract.hh"
#include "tydra/next/render-converter.hh"
#include "tydra/next/urdf-to-usd.hh"
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

void TestChunkedArrayShrinkToFit() {
  std::cout << "Testing ChunkedArray shrink_to_fit (exact-size tail)...\n";

  // Small array: the 64KB minimum chunk must compact to the exact size.
  // (Thousands of small meshes each paying full chunks OOM'd wasm32.)
  ChunkedArray<float> small;
  for (size_t i = 0; i < 100; ++i) small.push_back(static_cast<float>(i));
  assert(small.memory_usage() >= 64 * 1024);
  small.shrink_to_fit();
  assert(small.memory_usage() < 100 * sizeof(float) + 2 * sizeof(small));
  assert(small.size() == 100);
  for (size_t i = 0; i < 100; ++i) {
    assert(small[i] == static_cast<float>(i));
  }

  // Appending after shrink transparently re-expands the tail chunk.
  small.push_back(100.0f);
  assert(small.size() == 101);
  for (size_t i = 0; i <= 100; ++i) {
    assert(small[i] == static_cast<float>(i));
  }

  // Multi-chunk array: full chunks stay, only the tail compacts; data intact.
  constexpr size_t kPerChunk = ChunkedArray<float>::kElementsPerChunk;
  const size_t n = kPerChunk * 2 + 7;
  ChunkedArray<float> big;
  for (size_t i = 0; i < n; ++i) big.push_back(static_cast<float>(i));
  big.shrink_to_fit();
  assert(big.size() == n);
  assert(big.chunk_count() == 3);
  assert(big.memory_usage() <
         2 * 64 * 1024 + 7 * sizeof(float) + 2 * sizeof(big));
  for (size_t i = 0; i < n; ++i) {
    assert(big[i] == static_cast<float>(i));
  }
  std::vector<float> flat = big.flatten();
  assert(flat.size() == n);
  assert(flat[n - 1] == static_cast<float>(n - 1));

  // Exact multiple of the chunk size: nothing to compact, nothing lost.
  ChunkedArray<float> exact;
  exact.resize(kPerChunk, 3.0f);
  exact.shrink_to_fit();
  assert(exact.size() == kPerChunk);
  assert(exact[kPerChunk - 1] == 3.0f);

  // Empty array: shrink frees everything.
  ChunkedArray<float> reserved;
  reserved.reserve(kPerChunk * 4);
  reserved.shrink_to_fit();
  assert(reserved.memory_usage() <= sizeof(reserved));
  assert(reserved.empty());

  std::cout << "  ChunkedArray shrink_to_fit: PASSED\n";
}

void TestChunkedArrayAllocFailure() {
  std::cout << "Testing ChunkedArray allocation-failure semantics...\n";

  // An impossible growth request must fail cleanly (nothrow) instead of
  // aborting: false return, alloc_failed() latched, contents untouched.
  ChunkedArray<float> arr;
  for (size_t i = 0; i < 10; ++i) arr.push_back(static_cast<float>(i));
  assert(!arr.alloc_failed());

  const size_t impossible = (std::numeric_limits<size_t>::max)() / 8;
  assert(!arr.resize(impossible));
  assert(arr.alloc_failed());
  assert(arr.size() == 10);
  for (size_t i = 0; i < 10; ++i) {
    assert(arr[i] == static_cast<float>(i));
  }

  // reserve/append on the same impossible scale also fail cleanly.
  assert(!arr.reserve(impossible));
  float dummy = 0.0f;
  assert(arr.append(&dummy, 0));  // zero-count append is a no-op success

  // Small growth still works after a failed attempt (failure is latched for
  // inspection but does not poison the array).
  const size_t idx = arr.push_back(10.0f);
  assert(idx == 10);
  assert(arr.size() == 11);
  assert(arr[10] == 10.0f);

  std::cout << "  ChunkedArray allocation failure: PASSED\n";
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
        double shutter:open = -0.25
        double shutter:close = 0.25
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
  // Euler rotate ops are emitted as xyzw quaternions: 90 deg about Y ->
  // (0, sin45, 0, cos45).
  assert(std::abs(rotate_clip->channels[0].keyframes[1].value.x) < 0.001f);
  assert(std::abs(rotate_clip->channels[0].keyframes[1].value.y - 0.70711f) < 0.001f);
  assert(std::abs(rotate_clip->channels[0].keyframes[1].value.z) < 0.001f);
  assert(std::abs(rotate_clip->channels[0].keyframes[1].value.w - 0.70711f) < 0.001f);

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
  assert(result.scene.cameras.size() == 1);
  assert(std::fabs(result.scene.cameras[0].shutter_open + 0.25) < 0.001);
  assert(std::fabs(result.scene.cameras[0].shutter_close - 0.25) < 0.001);

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

// Regression: a material whose `outputs:surface` connects to a path that is
// absent from the composed stage (Unreal USD exports connect to a
// reference-SOURCE path that vanishes after composition), but which carries
// the real UsdPreviewSurface as a child. The converter must fall back to the
// child shader and must NOT fail the whole scene load. An unsupported custom
// shader child is placed first to exercise candidate iteration.
void TestRenderConverterUnrealSurfaceFallback() {
  std::cout << "Testing RenderConverter Unreal-style surface fallback...\n";

  const char* usda = R"(#usda 1.0
(
    defaultPrim = "World"
)

def Xform "World"
{
    def Material "UnrealMat"
    {
        token outputs:surface.connect = </MI_Missing_Source/SurfaceShader.outputs:surface>

        def Shader "UnrealShader"
        {
            uniform token info:id = "UnrealCustomSurface"
            token outputs:surface
        }

        def Shader "SurfaceShader"
        {
            uniform token info:id = "UsdPreviewSurface"
            color3f inputs:diffuseColor = (0.5, 0.3, 0.1)
            float inputs:roughness = 0.4
            token outputs:surface
        }
    }

    def Mesh "M"
    {
        rel material:binding = </World/UnrealMat>
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    }
}
)";

  LoadResult load_result = LoadUSDAFromString(usda, std::strlen(usda));
  if (!load_result.success) {
    std::cout << "  SKIPPED (failed to parse Unreal-fallback USDA)\n";
    return;
  }

  ConverterConfig config;
  config.material.load_textures = false;

  RenderSceneConverter converter(config);
  ConvertResult result = converter.Convert(load_result.stage);

  // The broken outputs:surface connection must not abort the scene load.
  assert(result.success);

  auto it = result.scene.material_by_path.find("/World/UnrealMat");
  assert(it != result.scene.material_by_path.end());
  const RenderMaterial& mat = result.scene.materials[it->second];

  // Fell back to the child UsdPreviewSurface (not the broken connection, not
  // the unsupported UnrealShader child that iterates first).
  assert(mat.shader_type == RenderMaterial::ShaderType::PreviewSurface);
  assert(mat.preview_surface);
  assert(std::abs(mat.preview_surface->diffuse_color.value.x - 0.5f) < 0.001f);
  assert(std::abs(mat.preview_surface->diffuse_color.value.y - 0.3f) < 0.001f);
  assert(std::abs(mat.preview_surface->diffuse_color.value.z - 0.1f) < 0.001f);
  assert(std::abs(mat.preview_surface->roughness.value.x - 0.4f) < 0.001f);

  std::cout << "  RenderConverter Unreal surface fallback: PASSED\n";
}

// Regression: a Material whose only surface is an Unreal `sourceAsset` shader
// (info:implementationSource = "sourceAsset", no UsdPreviewSurface child) must
// not be dropped. UE MetaHuman exports bind such materials; the converter emits
// a neutral default PreviewSurface so the binding survives and the mesh keeps a
// material slot instead of rendering unmaterialed.
void TestRenderConverterUnrealSourceAssetFallback() {
  std::cout << "Testing RenderConverter Unreal source-asset default material...\n";

  const char* usda = R"(#usda 1.0
(
    defaultPrim = "World"
)

def Xform "World"
{
    def Material "M_Hide"
    {
        token outputs:unreal:surface.connect = </World/M_Hide/UnrealShader.outputs:out>

        def Shader "UnrealShader"
        {
            uniform token info:implementationSource = "sourceAsset"
            uniform asset info:unreal:sourceAsset = @/Game/Materials/M_Hide.M_Hide@
            token outputs:out
        }
    }

    def Mesh "M"
    {
        rel material:binding = </World/M_Hide>
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    }
}
)";

  LoadResult load_result = LoadUSDAFromString(usda, std::strlen(usda));
  if (!load_result.success) {
    std::cout << "  SKIPPED (failed to parse Unreal source-asset USDA)\n";
    return;
  }

  ConverterConfig config;
  config.material.load_textures = false;

  RenderSceneConverter converter(config);
  ConvertResult result = converter.Convert(load_result.stage);

  // A source-asset-only material must not abort or vanish.
  assert(result.success);

  auto it = result.scene.material_by_path.find("/World/M_Hide");
  assert(it != result.scene.material_by_path.end());
  const RenderMaterial& mat = result.scene.materials[it->second];

  // Neutral default PreviewSurface (no convertible shader was found).
  assert(mat.shader_type == RenderMaterial::ShaderType::PreviewSurface);
  assert(mat.preview_surface);

  std::cout << "  RenderConverter Unreal source-asset default material: PASSED\n";
}

// Regression: UsdSkel binding inheritance. `skel:skeleton` may be authored on
// an ancestor (the enclosing SkelRoot) rather than on the skinned mesh, in
// which case the mesh inherits it. The converter must resolve the inherited
// skeleton (skeleton_id >= 0) so the mesh skins instead of rendering in bind
// pose. MetaHuman's standalone face/body exports bind this way.
void TestRenderConverterInheritedSkelBinding() {
  std::cout << "Testing RenderConverter inherited skel:skeleton binding...\n";

  const char* usda = R"(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root"
{
    def SkelRoot "SR" (
        prepend apiSchemas = ["SkelBindingAPI"]
    )
    {
        rel skel:skeleton = </Root/SR/Skel>

        def Skeleton "Skel"
        {
            uniform token[] joints = ["root", "root/child"]
            uniform matrix4d[] bindTransforms = [
                ( (1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,0,1) ),
                ( (1,0,0,0),(0,1,0,0),(0,0,1,0),(0,1,0,1) ) ]
            uniform matrix4d[] restTransforms = [
                ( (1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,0,1) ),
                ( (1,0,0,0),(0,1,0,0),(0,0,1,0),(0,1,0,1) ) ]
        }

        def Mesh "M" (
            prepend apiSchemas = ["SkelBindingAPI"]
        )
        {
            int[] faceVertexCounts = [3]
            int[] faceVertexIndices = [0, 1, 2]
            point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
            int[] primvars:skel:jointIndices = [0, 0, 1] (
                elementSize = 1
                interpolation = "vertex"
            )
            float[] primvars:skel:jointWeights = [1, 1, 1] (
                elementSize = 1
                interpolation = "vertex"
            )
        }
    }
}
)";

  LoadResult load_result = LoadUSDAFromString(usda, std::strlen(usda));
  if (!load_result.success) {
    std::cout << "  SKIPPED (failed to parse inherited-skel USDA)\n";
    return;
  }

  RenderSceneConverter converter;
  ConvertResult result = converter.Convert(load_result.stage);
  assert(result.success);
  assert(result.scene.skeletons.size() == 1);

  const RenderMesh* mesh = nullptr;
  for (const auto& m : result.scene.meshes) {
    if (m.skin) { mesh = &m; break; }
  }
  assert(mesh && "mesh with a skin binding should exist");
  // The mesh authors no skel:skeleton of its own — it must inherit the one on
  // the SkelRoot and resolve to a real skeleton id.
  assert(mesh->skin->skeleton_id >= 0 &&
         "inherited skel:skeleton must resolve to a skeleton_id");

  std::cout << "  RenderConverter inherited skel binding: PASSED\n";
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

void TestRenderConverterPointInstancerInvalidArrays() {
  std::cout << "Testing RenderConverter PointInstancer invalid arrays...\n";

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
        point3f[] positions = [(0, 0, 0)]
        int[] protoIndices = [0, 0]
    }
}
)";

  LoadResult load_result = LoadUSDAFromString(usda, std::strlen(usda));
  if (!load_result.success) {
    std::cout << "  SKIPPED (failed to parse invalid-array USDA)\n";
    return;
  }

  RenderSceneConverter converter;
  ConvertResult result = converter.Convert(load_result.stage);
  assert(result.success);
  assert(result.scene.point_instancers.size() == 1);
  assert(result.scene.point_instance_draws.empty());
  assert(result.scene.get_stats().point_instance_count == 2);
  assert(result.scene.get_stats().point_instance_draw_count == 0);
  assert(result.scene.has_valid_point_instance_draw_ranges());

  const RenderPointInstancer& instancer = result.scene.point_instancers[0];
  assert(!instancer.valid);
  assert(instancer.validation_error == "positions size does not match protoIndices");
  assert(instancer.instance_count() == 2);
  assert(instancer.positions == std::vector<float>({0, 0, 0}));
  assert(instancer.transforms.empty());
  assert(instancer.draw_start == 0);
  assert(instancer.draw_count == 0);

  bool found_warning = false;
  for (const std::string& warning : result.warnings) {
    if (warning.find("Invalid PointInstancer data at /World/Inst") !=
            std::string::npos &&
        warning.find("positions size does not match protoIndices") !=
            std::string::npos) {
      found_warning = true;
    }
  }
  assert(found_warning);

  std::cout << "  RenderConverter PointInstancer invalid arrays: PASSED\n";
}

void TestRenderConverterPointInstancerDuplicateMeshMetadata() {
  std::cout << "Testing RenderConverter PointInstancer duplicate metadata...\n";

  const char* usda = R"(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root"
{
    def SkelRoot "SR"
    {
        def Mesh "Proto" (
            prepend apiSchemas = ["SkelBindingAPI"]
        )
        {
            uniform token orientation = "leftHanded"
            int[] faceVertexCounts = [4, 3]
            int[] faceVertexIndices = [0, 1, 2, 3, 0, 2, 4]
            int[] holeIndices = [1]
            point3f[] points = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0), (2, 0, 0)]
            float[] primvars:temperature = [0.25, 0.75] (
                interpolation = "vertex"
            )
            int[] primvars:temperature:indices = [0, 1, 0, 1, 0]
            uniform token[] skel:blendShapes = ["smile"]
            prepend rel skel:blendShapeTargets = </Root/SR/Proto/Smile>

            def BlendShape "Smile"
            {
                uniform vector3f[] offsets = [(0, 0.5, 0), (0, 0.25, 0)]
                uniform int[] pointIndices = [1, 3]
            }
        }
    }

    def PointInstancer "Inst"
    {
        rel prototypes = </Root/SR/Proto>
        point3f[] positions = [(10, 0, 0)]
        int[] protoIndices = [0]
    }
}
)";

  LoadResult load_result = LoadUSDAFromString(usda, std::strlen(usda));
  if (!load_result.success) {
    std::cout << "  SKIPPED (failed to parse duplicate-metadata USDA)\n";
    return;
  }

  ConverterConfig config;
  config.mesh.triangulate = true;
  config.point_instancer.duplicate_meshes = true;
  RenderSceneConverter converter(config);
  ConvertResult result = converter.Convert(load_result.stage);
  assert(result.success);
  assert(result.scene.point_instancers.size() == 1);
  assert(result.scene.point_instance_draws.size() == 1);
  assert(result.scene.point_instance_draws[0].expanded_mesh_id >= 0);

  const RenderPointInstanceDrawView view =
      result.scene.get_point_instance_draw_view(0);
  assert(view.valid());
  assert(view.mesh);
  assert(view.expanded_mesh);
  const RenderMesh& src = *view.mesh;
  const RenderMesh& expanded = *view.expanded_mesh;

  assert(src.left_handed);
  assert(expanded.left_handed);
  assert(src.hole_faces == std::vector<uint32_t>({1}));
  assert(expanded.hole_faces == src.hole_faces);
  assert(src.is_triangulated);
  assert(expanded.is_triangulated);
  assert(src.triangulated_indices.size() == 6);
  assert(expanded.triangulated_indices.size() == src.triangulated_indices.size());
  assert(expanded.triangulated_face_vertex_indices.size() ==
         src.triangulated_face_vertex_indices.size());
  for (size_t i = 0; i < src.triangulated_face_vertex_indices.size(); ++i) {
    assert(expanded.triangulated_face_vertex_indices[i] ==
           src.triangulated_face_vertex_indices[i]);
  }

  const VertexAttribute* temp_src = nullptr;
  const VertexAttribute* temp_expanded = nullptr;
  for (const VertexAttribute& pv : src.primvars) {
    if (pv.name == "temperature") temp_src = &pv;
  }
  for (const VertexAttribute& pv : expanded.primvars) {
    if (pv.name == "temperature") temp_expanded = &pv;
  }
  assert(temp_src);
  assert(temp_expanded);
  assert(temp_expanded->indices.size() == temp_src->indices.size());
  assert(temp_expanded->indices.size() == 5);
  assert(temp_expanded->indices[1] == 1);

  assert(src.blend_shapes.size() == 1);
  assert(expanded.blend_shapes.size() == 1);
  assert(src.blend_shapes[0].point_indices == std::vector<uint32_t>({1, 3}));
  assert(expanded.blend_shapes[0].point_indices == src.blend_shapes[0].point_indices);
  assert(expanded.blend_shapes[0].point_offsets.size() ==
         src.blend_shapes[0].point_offsets.size());

  assert(expanded.point_count() == src.point_count());
  assert(std::abs(expanded.points[0] - 10.0f) < 0.001f);
  assert(std::abs(expanded.points[3] - 11.0f) < 0.001f);
  assert(expanded.has_bbox);
  assert(result.scene.has_valid_point_instance_draw_ranges());

  std::cout << "  RenderConverter PointInstancer duplicate metadata: PASSED\n";
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


// 2026-07 tydra audit regressions: ChunkedArray over-reserve accounting,
// malformed topology sanitization, authored-normals interpolation, leftHanded
// winding, holeIndices, GeomSubset material bindings, NodeGraph texture
// indirection, UsdTransform2d + primvar-reader varname, purpose bindings,
// surface-output shader selection, spot lights, generic/indexed primvars.
void TestAudit2026_07() {
  std::cout << "TestAudit2026_07..." << std::endl;

  // --- ChunkedArray: reserve() beyond the fill must not leak whole chunks
  // into copy_to()/flatten() (was a heap-buffer-overflow through the C API).
  {
    ChunkedArray<uint32_t> a;
    a.reserve(20000);  // 2+ chunks
    for (uint32_t i = 0; i < 10; ++i) a.push_back(i);
    assert(a.size() == 10);
    assert(a.chunk_size(0) == 10);
    assert(a.chunk_size(1) == 0);
    std::vector<uint32_t> flat = a.flatten();
    assert(flat.size() == 10 && flat[9] == 9);
    uint32_t dst[10];
    a.copy_to(dst);
    assert(dst[0] == 0 && dst[9] == 9);
    assert(a.is_contiguous());  // logical size fits chunk 0
  }

  const char* usda = R"(#usda 1.0
def Xform "Root"
{
    def Mesh "M"
    {
        rel material:binding:preview = </Root/Mat>
        int[] faceVertexCounts = [4, 4]
        int[] faceVertexIndices = [0, 1, 2, 3, 1, 4, 5, 2]
        int[] holeIndices = [1]
        point3f[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0), (2,0,0), (2,1,0)]
        normal3f[] normals = [(0,0,1), (0,0,1)] (
            interpolation = "uniform"
        )
        float[] primvars:heat = [7, 9] (
            interpolation = "vertex"
        )
        int[] primvars:heat:indices = [0, 1, 1, 0, 1, 0]

        def GeomSubset "SubA"
        {
            uniform token familyName = "materialBind"
            int[] indices = [1]
            rel material:binding = </Root/Mat2>
        }
    }

    def Mesh "LH"
    {
        uniform token orientation = "leftHanded"
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points = [(0,0,0), (1,0,0), (0,1,0)]
    }

    def Mesh "Bad"
    {
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, -1, 99]
        point3f[] points = [(0,0,0), (1,0,0), (0,1,0)]
    }

    def Material "Mat"
    {
        token outputs:surface.connect = </Root/Mat/Graph.outputs:out>

        def Shader "PS"
        {
            uniform token info:id = "UsdPreviewSurface"
            color3f inputs:diffuseColor.connect = </Root/Mat/Graph.outputs:tex>
            token outputs:surface
        }

        def "Graph"
        {
            token outputs:out.connect = </Root/Mat/PS.outputs:surface>
            color3f outputs:tex.connect = </Root/Mat/Tex.outputs:rgb>
        }

        def Shader "Tex"
        {
            uniform token info:id = "UsdUVTexture"
            asset inputs:file = @checker.png@
            float2 inputs:st.connect = </Root/Mat/Xf.outputs:result>
            color3f outputs:rgb
        }

        def Shader "Xf"
        {
            uniform token info:id = "UsdTransform2d"
            float2 inputs:translation = (0.5, 0.25)
            float inputs:rotation = 90
            float2 inputs:scale = (2, 3)
            float2 inputs:in.connect = </Root/Mat/Reader.outputs:result>
            float2 outputs:result
        }

        def Shader "Reader"
        {
            uniform token info:id = "UsdPrimvarReader_float2"
            token inputs:varname = "uvSet1"
            float2 outputs:result
        }
    }

    def Material "Mat2"
    {
        def Shader "PS2"
        {
            uniform token info:id = "UsdPreviewSurface"
            color3f inputs:diffuseColor = (0, 1, 0)
            token outputs:surface
        }
    }

    def SphereLight "Spot"
    {
        float inputs:shaping:cone:angle = 45
        bool inputs:shadow:enable = false
    }
}
)";

  LoadResult lr = LoadUSDAFromString(usda, std::strlen(usda));
  assert(lr.success);

  ConverterConfig cfg;
  RenderSceneConverter conv(cfg);
  ConvertResult res = conv.Convert(lr.stage);
  assert(res.success);
  RenderScene& scene = res.scene;

  // Mesh M: hole face skipped in triangulation, uniform normals kept as 2
  // elements, subset binding on face 1, indexed generic primvar preserved.
  {
    auto it = scene.mesh_by_path.find("/Root/M");
    assert(it != scene.mesh_by_path.end());
    RenderMesh& m = scene.meshes[static_cast<size_t>(it->second)];
    assert(m.triangulated_indices.size() == 6);  // 1 quad, hole skipped
    assert(m.normals.size() == 6 &&
           m.normals_interp == Interpolation::Uniform);
    assert(m.material_id >= 0);  // purpose binding (material:binding:preview)
    assert(m.material_subsets.size() == 1);
    assert(m.material_subsets[0].face_start == 1 &&
           m.material_subsets[0].face_count == 1);
    assert(m.material_subsets[0].material_id != m.material_id);
    assert(m.primvars.size() == 1 && m.primvars[0].name == "heat");
    assert(m.primvars[0].indices.size() == 6);
  }

  // Mesh LH: reversed winding + flipped computed normal.
  {
    auto it = scene.mesh_by_path.find("/Root/LH");
    assert(it != scene.mesh_by_path.end());
    RenderMesh& m = scene.meshes[static_cast<size_t>(it->second)];
    assert(m.left_handed);
    assert(m.triangulated_indices.size() == 3);
    assert(m.triangulated_indices[1] == 2 && m.triangulated_indices[2] == 1);
    assert(m.normals.size() >= 3 && m.normals[2] < 0.0f);  // z of vertex 0
  }

  // Mesh Bad: invalid indices dropped without crashing.
  {
    auto it = scene.mesh_by_path.find("/Root/Bad");
    assert(it != scene.mesh_by_path.end());
    RenderMesh& m = scene.meshes[static_cast<size_t>(it->second)];
    assert(m.face_count() == 0);
  }

  // Material Mat: texture found through the NodeGraph indirection, with the
  // Transform2d UV transform and the primvar-reader varname.
  {
    auto it = scene.material_by_path.find("/Root/Mat");
    assert(it != scene.material_by_path.end());
    RenderMaterial& mat = scene.materials[static_cast<size_t>(it->second)];
    assert(mat.preview_surface);
    assert(mat.preview_surface->diffuse_color.is_texture());
    const RenderTexture& tex = scene.textures[static_cast<size_t>(
        mat.preview_surface->diffuse_color.texture_id)];
    assert(tex.offset.x == 0.5f && tex.offset.y == 0.25f);
    assert(tex.scale.x == 2.0f && tex.scale.y == 3.0f);
    assert(std::fabs(tex.rotation - 1.5707963f) < 1e-4f);
    assert(tex.uv_primvar == "uvSet1");
  }

  // Light: cone shaping -> Spot; shadow:enable honored.
  {
    assert(scene.lights.size() == 1);
    const RenderLight& l = scene.lights[0];
    assert(l.type == LightType::Spot);
    assert(std::fabs(l.params.spot.angle - 45.0f * 3.14159265f / 180.0f) < 1e-4f);
    assert(!l.enable_shadow);
  }

  std::cout << "  TestAudit2026_07 PASSED" << std::endl;
}


// 2026-07 tydra-next gap follow-ups: tangent generation, faceVarying corner
// remap table, DomeLight environment texture, blendshape extraction.
void TestAudit2026_07_Gaps() {
  std::cout << "TestAudit2026_07_Gaps..." << std::endl;

  const char* usda = R"(#usda 1.0
def "Root"
{
    def Mesh "Quad"
    {
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0)]
        texCoord2f[] primvars:st = [(0,0), (1,0), (1,1), (0,1)] (
            interpolation = "vertex"
        )
        normal3f[] normals = [(0,0,1), (0,0,1), (0,0,1), (0,0,1)] (
            interpolation = "vertex"
        )
    }

    def Mesh "Mirror"
    {
        int[] faceVertexCounts = [3, 3]
        int[] faceVertexIndices = [0, 1, 2, 0, 2, 3]
        point3f[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0)]
        texCoord2f[] primvars:st = [(0,0), (1,0), (1,1), (0,0), (-1,1), (0,1)] (
            interpolation = "faceVarying"
        )
        normal3f[] normals = [(0,0,1), (0,0,1), (0,0,1), (0,0,1)] (
            interpolation = "vertex"
        )
    }

    def Mesh "DegenerateUV"
    {
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points = [(0,0,0), (1,0,0), (0,1,0)]
        texCoord2f[] primvars:st = [(0,0), (0,0), (0,0)] (
            interpolation = "vertex"
        )
        normal3f[] normals = [(0,0,1), (0,0,1), (0,0,1)] (
            interpolation = "vertex"
        )
    }

    def SkelRoot "SR"
    {
        def Skeleton "Skel"
        {
            uniform token[] joints = ["root", "root/child"]
            rel skel:animationSource = </Root/Anim>
        }

        def Mesh "BSMesh" (
            prepend apiSchemas = ["SkelBindingAPI"]
        )
        {
            int[] faceVertexCounts = [3]
            int[] faceVertexIndices = [0, 1, 2]
            point3f[] points = [(0,0,0), (1,0,0), (0,1,0)]
            uniform token[] skel:blendShapes = ["smile"]
            prepend rel skel:blendShapeTargets = </Root/SR/BSMesh/Smile>

            def BlendShape "Smile"
            {
                uniform vector3f[] offsets = [(0, 0.5, 0), (0, 0.5, 0)]
                uniform int[] pointIndices = [0, 2]
            }
        }
    }

    def SkelAnimation "Anim"
    {
        uniform token[] joints = ["root", "root/child"]
        uniform token[] blendShapes = ["smile"]
        float3[] translations.timeSamples = {
            0: [(0, 0, 0), (1, 0, 0)],
            1: [(0, 1, 0), (1, 1, 0)]
        }
        quatf[] rotations.timeSamples = {
            0: [(1, 0, 0, 0), (1, 0, 0, 0)],
            1: [(1, 0, 0, 0), (0, 0, 0, 1)]
        }
        half3[] scales.timeSamples = {
            0: [(1, 1, 1), (1, 1, 1)],
            1: [(1, 1, 1), (2, 2, 2)]
        }
        float[] blendShapeWeights.timeSamples = {
            0: [0],
            1: [1]
        }
    }

    def DomeLight "Env"
    {
        asset inputs:texture:file = @env.hdr@
        rel light:link = </Root/Quad>
        rel shadow:link = </Root/SR/BSMesh>
        rel filters = </Root/EnvFilter>
    }
}
)";

  LoadResult lr = LoadUSDAFromString(usda, std::strlen(usda));
  assert(lr.success);

  ConverterConfig cfg;
  cfg.mesh.compute_tangents = true;
  RenderSceneConverter conv(cfg);
  ConvertResult res = conv.Convert(lr.stage);
  assert(res.success);
  RenderScene& scene = res.scene;

  // Tangents + faceVarying corner remap on the quad.
  {
    auto it = scene.mesh_by_path.find("/Root/Quad");
    assert(it != scene.mesh_by_path.end());
    RenderMesh& m = scene.meshes[static_cast<size_t>(it->second)];
    // Unit quad, axis-aligned UVs -> tangent +X, w=+1.
    assert(m.tangents.size() == m.point_count() * 4);
    assert(std::fabs(m.tangents[0] - 1.0f) < 1e-4f);
    assert(std::fabs(m.tangents[3] - 1.0f) < 1e-4f);  // handedness w
    // Corner remap parallel to triangulated_indices.
    assert(m.triangulated_face_vertex_indices.size() ==
           m.triangulated_indices.size());
    assert(m.triangulated_face_vertex_indices[0] == 0);
    assert(m.triangulated_face_vertex_indices[2] == 2);
    assert(m.triangulated_face_vertex_indices[5] == 3);
  }

  // Blendshape extraction (sparse, with pointIndices).
  {
    auto it = scene.mesh_by_path.find("/Root/SR/BSMesh");
    assert(it != scene.mesh_by_path.end());
    RenderMesh& m = scene.meshes[static_cast<size_t>(it->second)];
    assert(m.blend_shapes.size() == 1);
    assert(m.blend_shapes[0].name == "smile");
    assert(m.blend_shapes[0].point_offsets.size() == 6);  // 2 points x3
    assert(m.blend_shapes[0].point_indices.size() == 2 &&
           m.blend_shapes[0].point_indices[0] == 0 &&
           m.blend_shapes[0].point_indices[1] == 2);
  }

  // Face-varying mirrored UVs require per-corner tangents and negative
  // handedness on at least one corner.
  {
    auto it = scene.mesh_by_path.find("/Root/Mirror");
    assert(it != scene.mesh_by_path.end());
    RenderMesh& m = scene.meshes[static_cast<size_t>(it->second)];
    assert(m.tangents_interp == Interpolation::FaceVarying);
    assert(m.tangents.size() == m.triangulated_indices.size() * 4);
    bool saw_negative = false;
    for (size_t i = 3; i < m.tangents.size(); i += 4) {
      if (m.tangents[i] < 0.0f) saw_negative = true;
    }
    assert(saw_negative);
  }

  // Degenerate UVs should not produce NaN/Inf tangent data.
  {
    auto it = scene.mesh_by_path.find("/Root/DegenerateUV");
    assert(it != scene.mesh_by_path.end());
    RenderMesh& m = scene.meshes[static_cast<size_t>(it->second)];
    assert(!m.tangents.empty());
    for (size_t i = 0; i < m.tangents.size(); ++i) {
      assert(std::isfinite(m.tangents[i]));
    }
  }

  // SkelAnimation channels preserve full per-joint/per-weight arrays.
  {
    assert(scene.animations.size() == 1);
    const AnimationClip& clip = scene.animations[0];
    assert(clip.channels.size() == 4);
    bool saw_translations = false;
    bool saw_rotations = false;
    bool saw_scales = false;
    bool saw_weights = false;
    for (const AnimationChannel& ch : clip.channels) {
        assert(ch.is_skeletal);
        assert(ch.keyframes.size() == 2);
        assert(ch.target_skeleton == 0);
        assert(ch.target_skeleton_path == "/Root/SR/Skel");
        if (ch.property_name == "translations") {
          saw_translations = true;
          assert(ch.value_stride == 3);
          assert(ch.element_count == 2);
          assert(ch.joint_order.size() == 2);
          assert(ch.joint_remap.size() == 2);
          assert(ch.joint_remap[0] == 0);
          assert(ch.joint_remap[1] == 1);
          assert(ch.array_values.size() == 12);
          assert(std::fabs(ch.array_values[3] - 1.0f) < 1e-4f);
      } else if (ch.property_name == "rotations") {
        saw_rotations = true;
        assert(ch.value_stride == 4);
        assert(ch.element_count == 2);
        assert(ch.array_values.size() == 16);
        // Channels are xyzw (three.js quaternion order); authored USDA quats
        // are real-first. Frame 0 joint 0 = identity -> (0,0,0,1); frame 1
        // joint 1 authored (0,0,0,1) [180deg about Z] -> (0,0,1,0).
        assert(std::fabs(ch.array_values[3] - 1.0f) < 1e-4f);
        assert(std::fabs(ch.array_values[14] - 1.0f) < 1e-4f);
        assert(std::fabs(ch.array_values[15]) < 1e-4f);
      } else if (ch.property_name == "scales") {
        saw_scales = true;
        assert(ch.value_stride == 3);
        assert(ch.element_count == 2);
        assert(ch.array_values.size() == 12);
        assert(std::fabs(ch.array_values[9] - 2.0f) < 1e-4f);
      } else if (ch.property_name == "blendShapeWeights") {
        saw_weights = true;
        assert(ch.value_stride == 1);
        assert(ch.element_count == 1);
        assert(ch.blend_shape_order.size() == 1);
        assert(ch.array_values.size() == 2);
        assert(std::fabs(ch.array_values[1] - 1.0f) < 1e-4f);
      }
    }
    assert(saw_translations && saw_rotations && saw_scales && saw_weights);
    assert(scene.skeletons.size() == 1);
    assert(scene.skeletons[0].animation_id == 0);
    assert(scene.skeletons[0].animation_source_path == "/Root/Anim");
  }

  // DomeLight environment texture imported as an image.
  {
    assert(scene.lights.size() == 1);
    const RenderLight& l = scene.lights[0];
    assert(l.type == LightType::Dome);
    assert(l.params.dome.texture_id >= 0);
    assert(static_cast<size_t>(l.params.dome.texture_id) < scene.images.size());
    assert(l.light_link_targets.size() == 1 &&
           l.light_link_targets[0] == "/Root/Quad");
    assert(l.shadow_link_targets.size() == 1 &&
           l.shadow_link_targets[0] == "/Root/SR/BSMesh");
    assert(l.filter_targets.size() == 1 &&
           l.filter_targets[0] == "/Root/EnvFilter");
  }

  std::cout << "  TestAudit2026_07_Gaps PASSED" << std::endl;
}

void TestPhysicsAnnotations() {
  std::cout << "Testing USD Physics annotations...\n";

  const char* usda = R"(#usda 1.0
(
    defaultPrim = "Root"
    upAxis = "Z"
)

def Xform "Root"
{
    def PhysicsScene "Scene"
    {
    }

    def Xform "Body" (
        prepend apiSchemas = [
            "PhysicsRigidBodyAPI",
            "PhysicsMassAPI",
            "PhysicsArticulationRootAPI",
            "PhysicsFilteredPairsAPI"
        ]
    )
    {
        bool physics:kinematicEnabled = true
        vector3f physics:velocity = (1, 2, 3)
        float physics:mass = 4
        float3 physics:diagonalInertia = (5, 6, 7)
        uniform int mjc:freeJoint = 1
        rel physics:filteredPairs = </Root/Collider>
    }

    def Mesh "Collider" (
        prepend apiSchemas = [
            "PhysicsCollisionAPI",
            "PhysicsMeshCollisionAPI",
            "PhysicsMaterialAPI"
        ]
    )
    {
        bool physics:collisionEnabled = false
        token physics:approximation = "convexHull"
        float physics:staticFriction = 0.7
    }

    def PhysicsRevoluteJoint "Joint"
    {
        rel physics:body0 = </Root/Body>
        rel physics:body1 = </Root/Collider>
        token physics:axis = "Z"
        float physics:lowerLimit = -90
        float physics:upperLimit = 90
    }
}
)";

  LoadResult lr = LoadUSDAFromString(usda, std::strlen(usda));
  assert(lr.success);

  RenderSceneConverter conv;
  ConvertResult res = conv.Convert(lr.stage);
  assert(res.success);
  const RenderScene& scene = res.scene;

  assert(scene.physics.scenes.size() == 1);
  assert(scene.physics.scenes[0].prim_path == "/Root/Scene");
  assert(std::fabs(scene.physics.scenes[0].gravity_magnitude - 9.81f) <
         0.001f);
  assert(std::fabs(scene.physics.scenes[0].gravity_direction.z + 1.0f) <
         0.001f);

  assert(scene.physics.rigid_bodies.size() == 1);
  const PhysicsRigidBodyAnnotation& body = scene.physics.rigid_bodies[0];
  assert(body.prim_path == "/Root/Body");
  assert(body.kinematic_enabled);
  assert(body.has_mass);
  assert(std::fabs(body.mass - 4.0f) < 0.001f);
  assert(std::fabs(body.diagonal_inertia.z - 7.0f) < 0.001f);
  assert(!body.extension_properties.empty());
  assert(body.extension_properties[0].name == "mjc:freeJoint");

  assert(scene.physics.colliders.size() == 1);
  assert(scene.physics.colliders[0].prim_path == "/Root/Collider");
  assert(!scene.physics.colliders[0].collision_enabled);
  assert(scene.physics.colliders[0].has_mesh_collision);
  assert(scene.physics.colliders[0].approximation == "convexHull");

  assert(scene.physics.materials.size() == 1);
  assert(std::fabs(scene.physics.materials[0].static_friction - 0.7f) <
         0.001f);

  assert(scene.physics.joints.size() == 1);
  const PhysicsJointAnnotation& joint = scene.physics.joints[0];
  assert(joint.type_name == "PhysicsRevoluteJoint");
  assert(joint.body0 == "/Root/Body");
  assert(joint.body1 == "/Root/Collider");
  assert(std::fabs(joint.axis.z - 1.0f) < 0.001f);
  assert(std::fabs(joint.lower_limit + 90.0f) < 0.001f);

  assert(scene.physics.filtered_pairs.size() == 1);
  assert(scene.physics.filtered_pairs[0].filtered_pair_paths.size() == 1);
  assert(scene.physics.filtered_pairs[0].filtered_pair_paths[0] ==
         "/Root/Collider");
  assert(scene.physics.articulation_roots.size() == 1);
  assert(scene.physics.articulation_roots[0] == "/Root/Body");

  std::cout << "  USD Physics annotations: PASSED\n";
}

void TestRenderConverterCurves() {
  std::cout << "Testing RenderConverter curves...\n";

  const char* usda = R"(#usda 1.0
def Xform "World"
{
    def BasisCurves "Linear"
    {
        uniform token type = "linear"
        uniform token wrap = "nonperiodic"
        int[] curveVertexCounts = [3, 2]
        point3f[] points = [(0, 0, 0), (1, 0, 0), (1, 1, 0),
                            (2, 0, 0), (2, 1, 0)]
        float[] widths = [0.1, 0.2, 0.3, 0.4, 0.5]
        color3f[] primvars:displayColor = [(0.25, 0.5, 0.75)] (
            interpolation = "constant"
        )
    }

    def BasisCurves "Bezier"
    {
        uniform token type = "cubic"
        uniform token basis = "bezier"
        uniform token wrap = "nonperiodic"
        int[] curveVertexCounts = [4]
        point3f[] points = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (2, 1, 0)]
    }

    def BasisCurves "PeriodicCatmullRom"
    {
        uniform token type = "cubic"
        uniform token basis = "catmullRom"
        uniform token wrap = "periodic"
        int[] curveVertexCounts = [4]
        point3f[] points = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]
    }

    def NurbsCurves "Nurbs"
    {
        int[] curveVertexCounts = [4]
        int[] order = [3]
        double[] knots = [0, 0, 0, 1, 2, 2, 2]
        double2[] ranges = [(0, 2)]
        point3f[] points = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (2, 1, 0)]
    }

    def NurbsCurves "BadNurbs"
    {
        int[] curveVertexCounts = [4]
        int[] order = [3]
        double[] knots = [0, 0]
        point3f[] points = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (2, 1, 0)]
    }
}
)";

  auto test_urdf_mjcf_conversion = []() {
  std::cout << "Testing URDF/MJCF next-core conversion...\n";

  const char* payload = R"JSON({
    "name": "test_robot",
    "sourceFormat": "mjcf",
    "upAxis": "Z",
    "gravity": [0, 0, -1],
    "timestep": 0.001,
    "newton": {"selfCollisionEnabled": false},
    "links": [
      {
        "name": "base-link",
        "inertial": {
          "mass": 2.5,
          "centerOfMass": [0.1, 0.2, 0.3],
          "diagonalInertia": [1, 2, 3]
        },
        "visuals": [{"name": "body", "meshRef": "body_mesh"}],
        "collisions": [{
          "name": "collider",
          "shape": {"type": "sphere", "radius": 0.25},
          "mjc": {"group": 4, "contype": 2, "conaffinity": 3}
        }]
      },
      {"name": "tip", "visuals": [], "collisions": []}
    ],
    "joints": [{
      "name": "hinge",
      "type": "revolute",
      "parent": "base-link",
      "child": "tip",
      "axis": [0, 0, 1],
      "localPos0": [0, 0, 0.5],
      "localPos1": [0, 0, 0],
      "limit": {"lower": -1, "upper": 1},
      "dynamics": {"damping": 0.5, "stiffness": 3, "armature": 0.02}
    }],
    "tendons": [{"name": "cable", "stiffness": 10}],
    "mjcActuators": [{"name": "motor", "gear": [2]}],
    "keyframes": [{"name": "home", "qpos": [0]}],
    "sensors": [{"name": "position", "type": "jointpos"}]
  })JSON";

  tinyusdz::tydra::next::URDFMeshBuffer mesh;
  mesh.positions = {0, 0, 0, 1, 0, 0, 0, 1, 0};
  mesh.normals = {0, 0, 1, 0, 0, 1, 0, 0, 1};
  mesh.uvs = {0, 0, 1, 0, 0, 1};
  mesh.indices = {0, 1, 2};
  std::map<std::string, tinyusdz::tydra::next::URDFMeshBuffer> meshes;
  meshes["body_mesh"] = mesh;

  tinyusdz::next::Stage stage;
  std::string warn;
  std::string err;
  assert(tinyusdz::tydra::next::ConvertURDFJsonToUSDStage(
      payload, &meshes, &stage, &warn, &err));
  assert(err.empty());
  assert(stage.GetMeta().defaultPrim == "World");
  assert(stage.GetMeta().upAxis == "Z");

  UsdPrim scene = stage.GetPrimAtPath("/World/PhysicsScene");
  assert(scene.IsValid());
  assert(scene.GetTypeName() == "PhysicsScene");
  const Value* timestep = scene.GetPropertyValue("mjc:timestep");
  assert(timestep && timestep->as_double());
  assert(std::fabs(*timestep->as_double() - 0.001) < 1.0e-9);

  UsdPrim base = stage.GetPrimAtPath("/World/Links/base_link");
  assert(base.IsValid());
  const Value* mass = base.GetPropertyValue("physics:mass");
  assert(mass && mass->as_float());
  assert(std::fabs(*mass->as_float() - 2.5f) < 0.001f);
  assert(std::find(base.GetMeta().apiSchemas().begin(),
                   base.GetMeta().apiSchemas().end(),
                   "PhysicsRigidBodyAPI") != base.GetMeta().apiSchemas().end());

  UsdPrim visual = stage.GetPrimAtPath("/World/Links/base_link/body");
  assert(visual.IsValid());
  assert(visual.GetTypeName() == "Mesh");
  assert(visual.GetPropertyValue("points"));

  UsdPrim collider = stage.GetPrimAtPath("/World/Links/base_link/collider");
  assert(collider.IsValid());
  assert(collider.GetTypeName() == "Sphere");
  const Value* group = collider.GetPropertyValue("mjc:group");
  assert(group && group->as_int() && *group->as_int() == 4);

  UsdPrim joint = stage.GetPrimAtPath("/World/Joints/hinge");
  assert(joint.IsValid());
  assert(joint.GetTypeName() == "PhysicsRevoluteJoint");
  const std::vector<Path>* body0 = joint.GetRelationship("physics:body0");
  const std::vector<Path>* body1 = joint.GetRelationship("physics:body1");
  assert(body0 && body0->size() == 1 && (*body0)[0].str() == "/World/Links/base_link");
  assert(body1 && body1->size() == 1 && (*body1)[0].str() == "/World/Links/tip");
  const Value* damping = joint.GetPropertyValue("mjc:damping");
  assert(damping && damping->as_double());
  assert(std::fabs(*damping->as_double() - 0.5) < 1.0e-9);

  assert(stage.HasPrimAtPath("/World/Tendons/cable"));
  assert(stage.HasPrimAtPath("/World/MjcActuators/motor"));
  assert(stage.HasPrimAtPath("/World/Keyframes/home"));
  assert(stage.HasPrimAtPath("/World/Sensors/position"));

  std::cout << "  URDF/MJCF next-core conversion: PASSED\n";
  };
  test_urdf_mjcf_conversion();

  LoadResult lr = LoadUSDAFromString(usda, std::strlen(usda));
  assert(lr.success);

  ConverterConfig cfg;
  cfg.curves.tessellation_segments = 2;
  RenderSceneConverter converter(cfg);
  ConvertResult result = converter.Convert(lr.stage);
  assert(result.success);
  assert(result.scene.curves.size() == 5);
  assert(result.scene.get_stats().curves_count == 5);
  assert(result.scene.get_stats().curve_count == 6);

  auto curve = [&](const char* path) -> const RenderCurves& {
    auto it = result.scene.curves_by_path.find(path);
    assert(it != result.scene.curves_by_path.end());
    const RenderCurves* value = result.scene.get_curves(it->second);
    assert(value);
    return *value;
  };

  const RenderCurves& linear = curve("/World/Linear");
  assert(linear.type == CurveType::Linear);
  assert(linear.curve_vertex_counts == std::vector<uint32_t>({3, 2}));
  assert(linear.tessellated_vertex_counts ==
         std::vector<uint32_t>({3, 2}));
  assert(linear.tessellated_widths.size() == 5);
  assert(linear.colors.size() == 3);
  assert(linear.colors_interp == Interpolation::Constant);
  assert(linear.tessellated_colors.size() == 15);
  assert(linear.has_bbox);

  const RenderCurves& bezier = curve("/World/Bezier");
  assert(bezier.type == CurveType::Cubic);
  assert(bezier.basis == CurveBasis::Bezier);
  assert(bezier.tessellated_vertex_counts == std::vector<uint32_t>({3}));
  assert(std::fabs(bezier.tessellated_points[0]) < 0.001f);
  assert(std::fabs(bezier.tessellated_points[8] - 0.0f) < 0.001f);
  assert(std::fabs(bezier.tessellated_points[6] - 2.0f) < 0.001f);
  assert(std::fabs(bezier.tessellated_points[7] - 1.0f) < 0.001f);

  const RenderCurves& periodic = curve("/World/PeriodicCatmullRom");
  assert(periodic.wrap == CurveWrap::Periodic);
  assert(periodic.tessellated_vertex_counts == std::vector<uint32_t>({9}));
  const size_t last = periodic.tessellated_points.size() - 3;
  assert(std::fabs(periodic.tessellated_points[0] -
                   periodic.tessellated_points[last]) < 0.001f);
  assert(std::fabs(periodic.tessellated_points[1] -
                   periodic.tessellated_points[last + 1]) < 0.001f);

  const RenderCurves& nurbs = curve("/World/Nurbs");
  assert(nurbs.is_nurbs);
  assert(nurbs.tessellated_vertex_counts == std::vector<uint32_t>({5}));
  assert(std::fabs(nurbs.tessellated_points[0]) < 0.001f);
  assert(std::fabs(nurbs.tessellated_points[nurbs.tessellated_points.size() - 3] -
                   2.0f) < 0.001f);

  const RenderCurves& bad_nurbs = curve("/World/BadNurbs");
  assert(bad_nurbs.tessellated_vertex_counts ==
         std::vector<uint32_t>({4}));
  bool saw_fallback = false;
  for (const std::string& warning : result.warnings) {
    if (warning.find("BadNurbs") != std::string::npos &&
        warning.find("control-polygon passthrough") != std::string::npos) {
      saw_fallback = true;
    }
  }
  assert(saw_fallback);

  auto node_it = result.scene.node_by_path.find("/World/Bezier");
  assert(node_it != result.scene.node_by_path.end());
  const SceneNode& node = result.scene.nodes[node_it->second];
  assert(node.type == NodeType::Curves);
  assert(node.data_id >= 0);

  std::cout << "  RenderConverter curves: PASSED\n";
}

void TestLegacyParityExtraction() {
  std::cout << "Testing legacy schema parity extraction...\n";

  const char* usda = R"(#usda 1.0
def Xform "Root"
{
    def Material "Mat"
    {
        string config:mtlx:version = "1.38"
        token config:mtlx:namespace = "mtlx"
        token config:mtlx:colorspace = "lin_rec709"
        asset config:mtlx:sourceUri = @looks/test.mtlx@

        def Shader "Surface"
        {
            uniform token info:id = "UsdPreviewSurface"
            color3f inputs:diffuseColor = (0.25, 0.5, 0.75)
            token outputs:surface
        }
    }

    def Cube "Box"
    {
        rel material:binding = </Root/Mat>
        double size = 4
    }

    def Sphere "Ball"
    {
        double radius = 1.5
    }

    def Cylinder "Pipe"
    {
        token axis = "Z"
        double radius = 0.5
        double height = 3
    }

    def Plane "Ground"
    {
        token axis = "Z"
        double width = 6
        double length = 8
    }

    def PointLight "Point"
    {
        bool inputs:enableColorTemperature = true
        float inputs:colorTemperature = 4200
        float inputs:diffuse = 0.75
        float inputs:specular = 0.25
        float inputs:shaping:focus = 0.5
        color3f inputs:shaping:focusTint = (0.2, 0.3, 0.4)
        float inputs:shaping:cone:softness = 0.1
        asset inputs:shaping:ies:file = @profiles/key.ies@
        float inputs:shaping:ies:angleScale = 1.5
        bool inputs:shaping:ies:normalize = true
        color3f inputs:shadow:color = (0.1, 0.2, 0.3)
        float inputs:shadow:distance = 12
        float inputs:shadow:falloff = 2
        float inputs:shadow:falloffGamma = 1.5
    }
    def GeometryLight "GeoLight" {}
    def DomeLight_1 "Env" {}
    def LightFilter "Filter" {}

    def BasisCurves "Curve" {}
    def Points "Pts"
    {
        point3f[] points = [(0, 0, 0), (1, 2, 3), (-1, 0, 2)]
        float[] widths = [0.1, 0.2, 0.3]
        color3f[] primvars:displayColor = [(1, 0, 0), (0, 1, 0), (0, 0, 1)] (
            interpolation = "vertex"
        )
    }
    def Volume "Fog" {}
}
)";

  LoadResult lr = LoadUSDAFromString(usda, std::strlen(usda));
  assert(lr.success);

  RenderExtractOptions opts;
  opts.collect_other = true;
  RenderExtractResult er;
  assert(CollectRenderPrims(lr.stage, opts, &er));
  assert(er.meshes.size() == 4);
  assert(er.lights.size() == 4);
  assert(er.curves.size() == 1);
  assert(er.volumes.size() == 1);

  RenderSceneConverter conv;
  ConvertResult res = conv.Convert(lr.stage);
  assert(res.success);
  RenderScene& scene = res.scene;

  auto box_it = scene.mesh_by_path.find("/Root/Box");
  assert(box_it != scene.mesh_by_path.end());
  const RenderMesh& box = scene.meshes[static_cast<size_t>(box_it->second)];
  assert(box.point_count() == 8);
  assert(box.face_count() == 6);
  assert(box.is_triangulated);
  assert(box.material_id >= 0);
  assert(std::fabs(box.bbox_min.x + 2.0f) < 0.001f);
  assert(std::fabs(box.bbox_max.x - 2.0f) < 0.001f);

  auto pipe_it = scene.mesh_by_path.find("/Root/Pipe");
  assert(pipe_it != scene.mesh_by_path.end());
  const RenderMesh& pipe = scene.meshes[static_cast<size_t>(pipe_it->second)];
  assert(pipe.point_count() > 0);
  assert(pipe.has_bbox);
  assert(std::fabs(pipe.bbox_min.z + 1.5f) < 0.001f);
  assert(std::fabs(pipe.bbox_max.z - 1.5f) < 0.001f);

  auto mat_it = scene.material_by_path.find("/Root/Mat");
  assert(mat_it != scene.material_by_path.end());
  const RenderMaterial& mat =
      scene.materials[static_cast<size_t>(mat_it->second)];
  assert(mat.mtlx_config.authored);
  assert(mat.mtlx_config.version == "1.38");
  assert(mat.mtlx_config.name_space == "mtlx");
  assert(mat.mtlx_config.colorspace == "lin_rec709");
  assert(mat.mtlx_config.source_uri == "looks/test.mtlx");

  bool saw_point = false;
  bool saw_geometry = false;
  bool saw_dome = false;
  bool saw_filter_fallback = false;
  for (const RenderLight& light : scene.lights) {
    if (light.prim_path == "/Root/Point" && light.type == LightType::Point) {
      saw_point = true;
      assert(light.enable_color_temperature);
      assert(std::fabs(light.color_temperature - 4200.0f) < 0.001f);
      assert(std::fabs(light.diffuse - 0.75f) < 0.001f);
      assert(std::fabs(light.specular - 0.25f) < 0.001f);
      assert(std::fabs(light.shaping_focus - 0.5f) < 0.001f);
      assert(std::fabs(light.shaping_focus_tint.x - 0.2f) < 0.001f);
      assert(std::fabs(light.shaping_focus_tint.y - 0.3f) < 0.001f);
      assert(std::fabs(light.shaping_focus_tint.z - 0.4f) < 0.001f);
      assert(std::fabs(light.shaping_cone_softness - 0.1f) < 0.001f);
      assert(light.shaping_ies_file == "profiles/key.ies");
      assert(std::fabs(light.shaping_ies_angle_scale - 1.5f) < 0.001f);
      assert(light.shaping_ies_normalize);
      assert(std::fabs(light.shadow_color.x - 0.1f) < 0.001f);
      assert(std::fabs(light.shadow_color.y - 0.2f) < 0.001f);
      assert(std::fabs(light.shadow_color.z - 0.3f) < 0.001f);
      assert(std::fabs(light.shadow_distance - 12.0f) < 0.001f);
      assert(std::fabs(light.shadow_falloff - 2.0f) < 0.001f);
      assert(std::fabs(light.shadow_falloff_gamma - 1.5f) < 0.001f);
    }
    if (light.prim_path == "/Root/GeoLight" &&
        light.type == LightType::Geometry) {
      saw_geometry = true;
    }
    if (light.prim_path == "/Root/Env" && light.type == LightType::Dome) {
      saw_dome = true;
    }
    if (light.prim_path == "/Root/Filter" && light.type == LightType::Point &&
        light.intensity == 0.0f) {
      saw_filter_fallback = true;
    }
  }
  assert(saw_point);
  assert(saw_geometry);
  assert(saw_dome);
  assert(saw_filter_fallback);

  assert(scene.points.size() == 1);
  const RenderPoints& pts = scene.points[0];
  assert(pts.prim_path == "/Root/Pts");
  assert(pts.point_count() == 3);
  assert(pts.widths.size() == 3);
  assert(pts.colors.size() == 9);
  assert(pts.colors_interp == Interpolation::Vertex);
  assert(pts.has_bbox);
  assert(std::fabs(pts.bbox_min.x + 1.0f) < 0.001f);
  assert(std::fabs(pts.bbox_max.y - 2.0f) < 0.001f);
  auto pts_node = scene.node_by_path.find("/Root/Pts");
  assert(pts_node != scene.node_by_path.end());
  assert(scene.nodes[static_cast<size_t>(pts_node->second)].type == NodeType::Points);
  assert(scene.nodes[static_cast<size_t>(pts_node->second)].data_id == 0);

  // BasisCurves are converted now (the empty def fails conversion with a
  // warning), so only Volume remains unsupported.
  assert(scene.unsupported_renderables.size() == 1);
  assert(scene.unsupported_renderables[0].type_name == "Volume");
  assert(scene.curves.empty());  // "/Root/Curve" has no points/counts
  bool saw_curve_warning = false;
  for (const std::string& w : res.warnings) {
    if (w.find("Failed to convert curves prim: /Root/Curve") !=
        std::string::npos) {
      saw_curve_warning = true;
    }
  }
  assert(saw_curve_warning);

  std::cout << "  Legacy schema parity extraction: PASSED\n";
}

void TestValueClipBaking() {
  std::cout << "Testing next value-clip baking...\n";

  const std::string root = R"(#usda 1.0
(
    startTimeCode = 0
    endTimeCode = 2
)
def Xform "Root" (
    clips = {
        dictionary default = {
            double2[] active = [(0, 0), (2, 1)]
            asset[] assetPaths = [@clip_0.usda@, @clip_1.usda@]
            string primPath = "/Root"
            double2[] times = [(0, 0), (2, 2)]
            bool interpolateMissingClipValues = true
        }
    }
)
{
}
)";
  const std::map<std::string, std::string> assets = {
      {"clip_0.usda", R"(#usda 1.0
def Xform "Root"
{
    double3 xformOp:translate = (1, 2, 3)
}
)"},
      {"clip_1.usda", R"(#usda 1.0
def Xform "Root"
{
    double3 xformOp:translate = (5, 6, 7)
}
)"}};

  LoadResult loaded = LoadUSDAFromString(root);
  assert(loaded.success);
  ConverterConfig config;
  config.animation.clip_stage_loader =
      [&assets](const std::string& path, Stage* stage, std::string* warn,
                std::string* err) {
        const auto it = assets.find(path);
        if (it == assets.end()) {
          if (err) *err = "missing test clip";
          return false;
        }
        LoadResult clip = LoadUSDAFromString(it->second);
        if (!clip.success) {
          if (err) *err = clip.error_summary;
          return false;
        }
        if (warn && !clip.warnings.empty()) *warn = clip.warnings.front();
        *stage = std::move(clip.stage);
        return true;
      };
  RenderSceneConverter converter(config);
  ConvertResult converted = converter.Convert(loaded.stage);
  assert(converted.success);
  assert(converted.scene.animations.size() == 1);
  const AnimationClip& animation = converted.scene.animations[0];
  assert(animation.value_clip_baked);
  assert(animation.clip_asset_paths.size() == 2);
  assert(animation.start_time == 0.0);
  assert(animation.end_time == 2.0);
  assert(animation.channels.size() == 1);
  const AnimationChannel& channel = animation.channels[0];
  assert(channel.target_path == AnimationChannel::TargetPath::Translation);
  assert(channel.target_prim_path == "/Root");
  assert(channel.keyframes.size() == 3);
  assert(std::fabs(channel.keyframes[0].value.x - 1.0f) < 0.001f);
  assert(std::fabs(channel.keyframes[1].value.x - 1.0f) < 0.001f);
  assert(std::fabs(channel.keyframes[2].value.x - 5.0f) < 0.001f);

  std::cout << "  Next value-clip baking: PASSED\n";
}

void TestMeshParityCleanups() {
  std::cout << "Testing mesh parity cleanups...\n";
  const char* usda = R"(#usda 1.0
def Xform "World"
{
    def Mesh "Concave"
    {
        point3f[] points = [(0, 0, 0), (2, 0, 0), (2, 2, 0), (1, 1, 0), (0, 2, 0)]
        int[] faceVertexCounts = [5]
        int[] faceVertexIndices = [0, 1, 2, 3, 4]
        int[] primvars:skel:jointIndices = [0,1,2,3,4,5, 0,1,2,3,4,5, 0,1,2,3,4,5, 0,1,2,3,4,5, 0,1,2,3,4,5] (
            elementSize = 6
            interpolation = "vertex"
        )
        float[] primvars:skel:jointWeights = [.1,.3,.2,.05,.25,.1, .1,.3,.2,.05,.25,.1, .1,.3,.2,.05,.25,.1, .1,.3,.2,.05,.25,.1, .1,.3,.2,.05,.25,.1] (
            elementSize = 6
            interpolation = "vertex"
        )
        uniform token[] skel:blendShapes = ["Smile"]
        rel skel:blendShapeTargets = </World/Smile>
    }
    def BlendShape "Smile"
    {
        uniform vector3f[] offsets = [(0,0,1), (0,0,1), (0,0,1), (0,0,1), (0,0,1)]
        uniform vector3f[] inbetweens:half = [(0,0,.5), (0,0,.5), (0,0,.5), (0,0,.5), (0,0,.5)] (
            weight = 0.5
        )
    }
}
)";
  LoadResult loaded = LoadUSDAFromString(usda, std::strlen(usda));
  assert(loaded.success);
  ConverterConfig config;
  config.mesh.triangulation_method = MeshConfig::TriangulationMethod::Earcut;
  config.mesh.enable_bone_reduction = true;
  config.mesh.target_bone_count = 4;
  RenderSceneConverter converter(config);
  ConvertResult converted = converter.Convert(loaded.stage);
  assert(converted.success);
  assert(converted.scene.meshes.size() == 1);
  const RenderMesh& mesh = converted.scene.meshes[0];
  assert(mesh.triangulated_indices.size() == 9);
  for (size_t i = 0; i < mesh.triangulated_indices.size(); i += 3) {
    const uint32_t ia = mesh.triangulated_indices[i] * 3;
    const uint32_t ib = mesh.triangulated_indices[i + 1] * 3;
    const uint32_t ic = mesh.triangulated_indices[i + 2] * 3;
    const float area =
        (mesh.points[ib] - mesh.points[ia]) *
            (mesh.points[ic + 1] - mesh.points[ia + 1]) -
        (mesh.points[ib + 1] - mesh.points[ia + 1]) *
            (mesh.points[ic] - mesh.points[ia]);
    assert(std::fabs(area) > 0.001f);
  }
  assert(mesh.skin);
  assert(mesh.skin->influences_per_vertex == 4);
  assert(mesh.skin->joint_indices.size() == 20);
  assert(mesh.skin->joint_indices[0] == 1);
  assert(mesh.skin->joint_indices[1] == 4);
  float weight_sum = 0.0f;
  for (size_t i = 0; i < 4; ++i) weight_sum += mesh.skin->joint_weights[i];
  assert(std::fabs(weight_sum - 1.0f) < 0.001f);
  assert(mesh.blend_shapes.size() == 1);
  assert(mesh.blend_shapes[0].inbetweens.size() == 1);
  assert(mesh.blend_shapes[0].inbetweens[0].name == "half");
  assert(std::fabs(mesh.blend_shapes[0].inbetweens[0].weight - 0.5f) <
         0.001f);
  std::cout << "  Mesh parity cleanups: PASSED\n";
}

// Regression: authored half-precision xformOps (raw half-bit SBO scalars)
// must evaluate through the converting to_float* reads — as_float3() cannot
// see them, which used to collapse scale/rotate to identity.
void TestHalfPrecisionXformOps() {
  std::cout << "Testing half-precision xformOps...\n";

  const char* usda = R"(#usda 1.0
(
    defaultPrim = "World"
)

def Xform "World"
{
    def Xform "HalfScaled"
    {
        half3 xformOp:scale = (2, 3, 4)
        uniform token[] xformOpOrder = ["xformOp:scale"]
    }

    def Xform "HalfOriented"
    {
        # 180 degrees about Z: diag(-1, -1, 1) regardless of convention.
        quath xformOp:orient = (0, 0, 0, 1)
        uniform token[] xformOpOrder = ["xformOp:orient"]
    }
}
)";

  LoadResult lr = LoadUSDAFromString(usda, std::strlen(usda));
  if (!lr.success) {
    std::cout << "  SKIPPED (failed to parse test USDA: " << lr.error_summary << ")\n";
    return;
  }

  UsdPrim scaled = lr.stage.GetPrimAtPath("/World/HalfScaled");
  assert(scaled.IsValid());
  double m[16];
  assert(ComputeLocalTransform(scaled, m, 0.0));
  assert(std::fabs(m[0] - 2.0) < 0.001);
  assert(std::fabs(m[5] - 3.0) < 0.001);
  assert(std::fabs(m[10] - 4.0) < 0.001);

  UsdPrim oriented = lr.stage.GetPrimAtPath("/World/HalfOriented");
  assert(oriented.IsValid());
  assert(ComputeLocalTransform(oriented, m, 0.0));
  assert(std::fabs(m[0] + 1.0) < 0.001);
  assert(std::fabs(m[5] + 1.0) < 0.001);
  assert(std::fabs(m[10] - 1.0) < 0.001);

  std::cout << "  Half-precision xformOps: PASSED\n";
}

// Multi-skeleton joint-order remap: two skeletons whose SkelAnimations
// author `joints` in an order DIFFERENT from the skeleton's own joint order.
// Channels must target the right skeleton and joint_remap must map each
// animation-order token back to the skeleton's joint index.
void TestMultiSkeletonJointRemap() {
  std::cout << "Testing multi-skeleton joint-order remap...\n";

  const char* usda = R"(#usda 1.0
(
    defaultPrim = "World"
    startTimeCode = 1
    endTimeCode = 2
)

def Xform "World"
{
    def SkelRoot "CharA"
    {
        def Skeleton "SkelA"
        {
            uniform token[] joints = ["A_root", "A_root/A_mid", "A_root/A_mid/A_tip"]
            uniform matrix4d[] bindTransforms = [
                ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,0,1)),
                ((1,0,0,0),(0,1,0,0),(0,0,1,0),(1,0,0,1)),
                ((1,0,0,0),(0,1,0,0),(0,0,1,0),(2,0,0,1))
            ]
            uniform matrix4d[] restTransforms = [
                ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,0,1)),
                ((1,0,0,0),(0,1,0,0),(0,0,1,0),(1,0,0,1)),
                ((1,0,0,0),(0,1,0,0),(0,0,1,0),(1,0,0,1))
            ]
            rel skel:animationSource = </World/CharA/SkelA/AnimA>

            def SkelAnimation "AnimA"
            {
                # Reversed relative to SkelA's joint order.
                uniform token[] joints = ["A_root/A_mid/A_tip", "A_root/A_mid", "A_root"]
                float3[] translations.timeSamples = {
                    1: [(30, 0, 0), (20, 0, 0), (10, 0, 0)],
                    2: [(31, 0, 0), (21, 0, 0), (11, 0, 0)],
                }
                quatf[] rotations.timeSamples = {
                    1: [(1,0,0,0), (1,0,0,0), (1,0,0,0)],
                }
                half3[] scales.timeSamples = {
                    1: [(1,1,1), (1,1,1), (1,1,1)],
                }
            }
        }
    }

    def SkelRoot "CharB"
    {
        def Skeleton "SkelB"
        {
            uniform token[] joints = ["B_root", "B_root/B_tip"]
            uniform matrix4d[] bindTransforms = [
                ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,0,1)),
                ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,1,0,1))
            ]
            uniform matrix4d[] restTransforms = [
                ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,0,1)),
                ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,1,0,1))
            ]
            rel skel:animationSource = </World/CharB/SkelB/AnimB>

            def SkelAnimation "AnimB"
            {
                uniform token[] joints = ["B_root/B_tip", "B_root"]
                float3[] translations.timeSamples = {
                    1: [(200, 0, 0), (100, 0, 0)],
                }
            }
        }
    }
}
)";

  LoadResult lr = LoadUSDAFromString(usda, std::strlen(usda));
  if (!lr.success) {
    std::cout << "  SKIPPED (failed to parse test USDA: " << lr.error_summary << ")\n";
    return;
  }

  ConverterConfig config;
  RenderSceneConverter converter(config);
  ConvertResult result = converter.Convert(lr.stage);
  assert(result.success);

  assert(result.scene.skeletons.size() == 2);
  int skel_a = -1;
  int skel_b = -1;
  for (size_t si = 0; si < result.scene.skeletons.size(); ++si) {
    const std::string& path = result.scene.skeletons[si].prim_path;
    if (path == "/World/CharA/SkelA") skel_a = static_cast<int>(si);
    if (path == "/World/CharB/SkelB") skel_b = static_cast<int>(si);
  }
  assert(skel_a >= 0 && skel_b >= 0);

  const AnimationClip* clip_a = nullptr;
  const AnimationClip* clip_b = nullptr;
  for (const AnimationClip& clip : result.scene.animations) {
    if (clip.prim_path == "/World/CharA/SkelA/AnimA") clip_a = &clip;
    if (clip.prim_path == "/World/CharB/SkelB/AnimB") clip_b = &clip;
  }
  assert(clip_a && clip_b);

  auto find_channel = [](const AnimationClip* clip,
                         AnimationChannel::TargetPath path) -> const AnimationChannel* {
    for (const AnimationChannel& ch : clip->channels) {
      if (ch.target_path == path && ch.is_skeletal) return &ch;
    }
    return nullptr;
  };

  // AnimA: translation channel targets SkelA; remap reverses the order
  // (animation joint 0 = A_tip = skeleton joint 2, ...).
  const AnimationChannel* trans_a =
      find_channel(clip_a, AnimationChannel::TargetPath::Translation);
  assert(trans_a);
  assert(trans_a->target_skeleton == skel_a);
  assert(trans_a->joint_order.size() == 3);
  assert(trans_a->joint_remap.size() == 3);
  assert(trans_a->joint_remap[0] == 2);
  assert(trans_a->joint_remap[1] == 1);
  assert(trans_a->joint_remap[2] == 0);
  // Values stay in ANIMATION joint order: frame 0, joint 0 (A_tip) = 30.
  assert(trans_a->value_stride == 3);
  assert(trans_a->element_count == 3);
  assert(trans_a->array_values.size() >= 2u * 3u * 3u);
  assert(std::fabs(trans_a->array_values[0] - 30.0f) < 0.001f);
  assert(std::fabs(trans_a->array_values[3] - 20.0f) < 0.001f);
  assert(std::fabs(trans_a->array_values[6] - 10.0f) < 0.001f);
  // Frame 1 continues after all frame-0 elements.
  assert(std::fabs(trans_a->array_values[9] - 31.0f) < 0.001f);

  // half3[] scales must survive as a skeletal channel too (array half
  // materializes to float).
  const AnimationChannel* scale_a =
      find_channel(clip_a, AnimationChannel::TargetPath::Scale);
  assert(scale_a);
  assert(scale_a->target_skeleton == skel_a);
  assert(std::fabs(scale_a->array_values[0] - 1.0f) < 0.001f);

  // AnimB: targets SkelB (not SkelA), reversed remap for 2 joints.
  const AnimationChannel* trans_b =
      find_channel(clip_b, AnimationChannel::TargetPath::Translation);
  assert(trans_b);
  assert(trans_b->target_skeleton == skel_b);
  assert(trans_b->joint_remap.size() == 2);
  assert(trans_b->joint_remap[0] == 1);
  assert(trans_b->joint_remap[1] == 0);
  assert(std::fabs(trans_b->array_values[0] - 200.0f) < 0.001f);

  // Each skeleton is linked to its own animation.
  assert(result.scene.skeletons[static_cast<size_t>(skel_a)].animation_id >= 0);
  assert(result.scene.skeletons[static_cast<size_t>(skel_b)].animation_id >= 0);
  assert(result.scene.skeletons[static_cast<size_t>(skel_a)].animation_id !=
         result.scene.skeletons[static_cast<size_t>(skel_b)].animation_id);

  std::cout << "  Multi-skeleton joint-order remap: PASSED\n";
}

// UsdLux depth: DomeLight inputs:texture:format token and CollectionAPI
// light/shadow-link resolution to RenderScene mesh index sets.
void TestLightLinkingAndDomeFormat() {
  std::cout << "Testing light linking + dome texture format...\n";

  const char* usda = R"(#usda 1.0
(
    defaultPrim = "World"
)

def Xform "World"
{
    def Mesh "GroundPlane"
    {
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [(-1, 0, -1), (1, 0, -1), (1, 0, 1), (-1, 0, 1)]
    }

    def Xform "Props"
    {
        def Mesh "Crate"
        {
            int[] faceVertexCounts = [4]
            int[] faceVertexIndices = [0, 1, 2, 3]
            point3f[] points = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]
        }

        def Mesh "Barrel"
        {
            int[] faceVertexCounts = [4]
            int[] faceVertexIndices = [0, 1, 2, 3]
            point3f[] points = [(2, 0, 0), (3, 0, 0), (3, 1, 0), (2, 1, 0)]
        }
    }

    def SphereLight "KeyLight"
    {
        float inputs:intensity = 5
        rel collection:lightLink:includes = [</World/Props>]
        rel collection:lightLink:excludes = [</World/Props/Barrel>]
        uniform token collection:lightLink:expansionRule = "expandPrims"
        rel collection:shadowLink:includes = [</World/GroundPlane>]
        uniform token collection:shadowLink:expansionRule = "explicitOnly"
    }

    def SphereLight "FillLight"
    {
        float inputs:intensity = 1
    }

    def DomeLight "Sky"
    {
        asset inputs:texture:file = @sky.exr@
        token inputs:texture:format = "latlong"
    }
}
)";

  LoadResult lr = LoadUSDAFromString(usda, std::strlen(usda));
  if (!lr.success) {
    std::cout << "  SKIPPED (failed to parse test USDA: " << lr.error_summary << ")\n";
    return;
  }

  ConverterConfig config;
  RenderSceneConverter converter(config);
  ConvertResult result = converter.Convert(lr.stage);
  assert(result.success);
  assert(result.scene.meshes.size() == 3);

  int ground = -1;
  int crate = -1;
  int barrel = -1;
  for (size_t mi = 0; mi < result.scene.meshes.size(); ++mi) {
    const std::string& path = result.scene.meshes[mi].prim_path;
    if (path == "/World/GroundPlane") ground = static_cast<int>(mi);
    if (path == "/World/Props/Crate") crate = static_cast<int>(mi);
    if (path == "/World/Props/Barrel") barrel = static_cast<int>(mi);
  }
  assert(ground >= 0 && crate >= 0 && barrel >= 0);

  const RenderLight* key = nullptr;
  const RenderLight* fill = nullptr;
  const RenderLight* sky = nullptr;
  for (const RenderLight& light : result.scene.lights) {
    if (light.prim_path == "/World/KeyLight") key = &light;
    if (light.prim_path == "/World/FillLight") fill = &light;
    if (light.prim_path == "/World/Sky") sky = &light;
  }
  assert(key && fill && sky);

  // KeyLight: lightLink includes /World/Props subtree minus Barrel.
  assert(!key->light_links_all);
  assert(key->light_link_mesh_indices.size() == 1);
  assert(key->light_link_mesh_indices[0] == crate);
  // shadowLink explicitOnly: only the exact GroundPlane path matches.
  assert(!key->shadow_links_all);
  assert(key->shadow_link_mesh_indices.size() == 1);
  assert(key->shadow_link_mesh_indices[0] == ground);

  // FillLight: no collections authored -> links everything.
  assert(fill->light_links_all);
  assert(fill->shadow_links_all);

  // DomeLight format token.
  assert(sky->type == LightType::Dome);
  assert(sky->params.dome.texture_format ==
         RenderLight::DomeTextureFormat::Latlong);

  std::cout << "  Light linking + dome texture format: PASSED\n";
}

int main() {
  std::cout << "=== Tydra Next Unit Tests ===\n\n";

  // ChunkedArray tests
  TestChunkedArrayBasic();
  TestChunkedArrayShrinkToFit();
  TestChunkedArrayAllocFailure();
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
  TestRenderConverterUnrealSurfaceFallback();
  TestRenderConverterUnrealSourceAssetFallback();
  TestRenderConverterInheritedSkelBinding();
  TestRenderConverterPointInstancerWarnings();
  TestRenderConverterPointInstancerIndexVisibility();
  TestRenderConverterPointInstancerInvalidArrays();
  TestRenderConverterPointInstancerDuplicateMeshMetadata();
  TestMaterialXUtilities();
  TestAudit2026_07();
  TestAudit2026_07_Gaps();
  TestPhysicsAnnotations();
  TestRenderConverterCurves();
  TestValueClipBaking();
  TestMeshParityCleanups();
  TestHalfPrecisionXformOps();
  TestMultiSkeletonJointRemap();
  TestLightLinkingAndDomeFormat();
  TestLegacyParityExtraction();

  std::cout << "\n=== All Tydra Next tests PASSED ===\n";
  return 0;
}
