// SPDX-License-Identifier: Apache-2.0
// App's McpHost implementation: the tool handlers. All run on the main thread
// (the MCP server marshals tool calls into the render loop), so they freely read
// the loaded scene / DrawScene and drive the camera and selection.
#include <cmath>
#include <string>

#include "app.hh"
#include "light3d/math.h"

namespace tusdview {

using nlohmann::json;

namespace {
json arr3(const float a[3]) { return json::array({a[0], a[1], a[2]}); }
json vec3json(const light3d::Vec3& v) { return json::array({v.x, v.y, v.z}); }

// Build the focused-prim payload from the current selection + DrawScene.
json focusedPayload(const Gui& gui, const DrawScene& draw) {
  const int mi = gui.selectedMeshIndex();
  const std::string path = gui.selectedPath();
  if (mi < 0 && path.empty()) return json{{"focused", false}};

  json out = {{"focused", true}, {"path", path}, {"mesh_index", mi}};
  if (mi >= 0 && static_cast<size_t>(mi) < draw.meshes.size()) {
    const DrawMeshCPU& m = draw.meshes[static_cast<size_t>(mi)];
    out["name"] = m.name;
    out["vertex_count"] = m.vertices.size();
    out["triangle_count"] = m.indices.size() / 3;
    out["aabb_min"] = arr3(m.aabbMin);
    out["aabb_max"] = arr3(m.aabbMax);
    out["double_sided"] = m.doubleSided;
    json world = json::array();
    for (int i = 0; i < 16; ++i) world.push_back(m.world[i]);
    out["world"] = world;
    const int matId = m.submeshes.empty() ? -1 : m.submeshes.front().materialId;
    out["submesh_count"] = m.submeshes.size();
    json mat = {{"id", matId}};
    if (matId >= 0 && static_cast<size_t>(matId) < draw.materials.size()) {
      const DrawMaterialCPU& dm = draw.materials[static_cast<size_t>(matId)];
      mat["name"] = dm.name;
      mat["base_color"] = arr3(dm.baseColor);
      mat["metallic"] = dm.metallic;
      mat["roughness"] = dm.roughness;
      mat["emissive"] = arr3(dm.emissive);
      mat["alpha"] = dm.alpha;
      mat["base_color_tex"] = dm.baseColorTex;
    }
    out["material"] = mat;
  }
  return out;
}
}  // namespace

json App::mcpLoadUsd(const json& args, std::string& err) {
  if (!args.contains("path") || !args["path"].is_string()) {
    err = "load_usd requires a string 'path'";
    return json::object();
  }
  const std::string path = args["path"].get<std::string>();
  startLoadAsync(path);  // async; client polls get_scene_info
  return json{{"started", true}, {"path", path}};
}

json App::mcpSceneInfo(const json&, std::string&) {
  json out = {{"loaded", loaded_.ok},
              {"filepath", loaded_.filepath},
              {"mesh_count", draw_.meshes.size()},
              {"triangle_count", draw_.triangleCount},
              {"material_count", draw_.materials.size()},
              {"upAxis", camera_.upAxis()},
              {"has_bounds", draw_.hasBounds}};
  out["aabb_min"] = arr3(draw_.aabbMin);
  out["aabb_max"] = arr3(draw_.aabbMax);
  if (draw_.truncated) out["truncated"] = true;
  return out;
}

json App::mcpGetFocusedPrim(const json&, std::string&) {
  return focusedPayload(gui_, draw_);
}

json App::mcpSetFocus(const json& args, std::string& err) {
  if (!args.contains("path") || !args["path"].is_string()) {
    err = "set_focus requires a string 'path'";
    return json::object();
  }
  gui_.selectByPath(args["path"].get<std::string>(), -1);
  return focusedPayload(gui_, draw_);
}

json App::mcpViewport(const json& args, std::string& err) {
  const std::string op = args.value("op", std::string());
  if (op == "orbit") {
    camera_.orbit(args.value("dx", 0.0f), args.value("dy", 0.0f));
  } else if (op == "pan") {
    camera_.pan(args.value("dx", 0.0f), args.value("dy", 0.0f));
  } else if (op == "dolly") {
    camera_.dolly(args.value("amount", 0.0f));
  } else if (op == "fit") {
    if (draw_.hasBounds) {
      camera_.fitToScene(draw_.aabbMin, draw_.aabbMax);
    } else {
      err = "no scene bounds to fit";
    }
  } else if (op == "set") {
    light3d::Vec3 tgt = camera_.target();
    if (args.contains("target") && args["target"].is_array() &&
        args["target"].size() == 3) {
      tgt.x = args["target"][0].get<float>();
      tgt.y = args["target"][1].get<float>();
      tgt.z = args["target"][2].get<float>();
    }
    camera_.setOrbit(tgt, args.value("yaw", camera_.yaw()),
                     args.value("pitch", camera_.pitch()),
                     args.value("distance", camera_.distance()));
  } else {
    err = "viewport: unknown op '" + op + "' (orbit|pan|dolly|fit|set)";
    return json::object();
  }
  // Return the resulting camera state.
  return json{{"target", vec3json(camera_.target())},
              {"yaw", camera_.yaw()},
              {"pitch", camera_.pitch()},
              {"distance", camera_.distance()},
              {"eye", vec3json(camera_.eye())}};
}

json App::mcpListPrims(const json& args, std::string&) {
  size_t cap = 1000;
  if (args.contains("max") && args["max"].is_number_integer()) {
    const long long m = args["max"].get<long long>();
    if (m > 0) cap = static_cast<size_t>(m);
  }
  json paths = json::array();
  for (const auto& m : draw_.meshes) {
    if (paths.size() >= cap) break;
    paths.push_back(m.absPath);
  }
  return json{{"count", paths.size()}, {"paths", paths}};
}

}  // namespace tusdview
