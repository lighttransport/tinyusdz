// SPDX-License-Identifier: Apache-2.0
// App's McpHost implementation: the tool handlers. All run on the main thread
// (the MCP server marshals tool calls into the render loop), so they freely read
// the loaded scene / DrawScene and drive the camera and selection.
#include <cmath>
#include <memory>
#include <string>

#include "app.hh"
#include "light3d/math.h"
#include "tydra/mcp-tools.hh"  // tinyusdz::tydra::mcp::CallTool

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
  if (loaded_.comp.composed) {
    out["composed"] = true;
    out["deferred_payload_count"] = loaded_.comp.deferred.size();
    json deferred = json::array();
    for (const auto& d : loaded_.comp.deferred) {
      deferred.push_back(json{{"prim", d.primPath}, {"asset", d.assetPath}});
    }
    out["deferred_payloads"] = deferred;
  }
  return out;
}

json App::mcpLoadPayloads(const json& args, std::string& err) {
  if (!loaded_.comp.composed) {
    err = "load_payloads: scene was not composed (no deferred payloads)";
    return json::object();
  }
  std::set<std::string> add;
  if (args.contains("paths") && args["paths"].is_array() && !args["paths"].empty()) {
    for (const auto& p : args["paths"]) {
      if (p.is_string()) add.insert(p.get<std::string>());
    }
  } else {
    for (const auto& d : loaded_.comp.deferred) add.insert(d.primPath);
  }
  if (add.empty()) {
    return json{{"started", false}, {"reason", "no deferred payloads"}};
  }
  startRecomposeAsync(add);  // async; client polls get_scene_info
  return json{{"started", true}, {"count", add.size()}};
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
  } else if (op == "home") {
    camera_.setPreset(CameraViewPreset::Isometric);
    if (draw_.hasBounds) camera_.fitToScene(draw_.aabbMin, draw_.aabbMax);
  } else if (op == "isometric") {
    camera_.setPreset(CameraViewPreset::Isometric);
  } else if (op == "front") {
    camera_.setPreset(CameraViewPreset::Front);
  } else if (op == "back") {
    camera_.setPreset(CameraViewPreset::Back);
  } else if (op == "right") {
    camera_.setPreset(CameraViewPreset::Right);
  } else if (op == "left") {
    camera_.setPreset(CameraViewPreset::Left);
  } else if (op == "top") {
    camera_.setPreset(CameraViewPreset::Top);
  } else if (op == "bottom") {
    camera_.setPreset(CameraViewPreset::Bottom);
  } else if (op == "bookmark_save") {
    const int slot = args.value("slot", 1) - 1;
    if (slot < 0 || slot >= 3) {
      err = "bookmark_save: slot must be 1..3";
      return json::object();
    }
    gui_.saveCameraBookmark(slot);
  } else if (op == "bookmark_load") {
    const int slot = args.value("slot", 1) - 1;
    if (slot < 0 || slot >= 3) {
      err = "bookmark_load: slot must be 1..3";
      return json::object();
    }
    if (!gui_.loadCameraBookmark(slot)) {
      err = "bookmark_load: slot is empty";
      return json::object();
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
    err = "viewport: unknown op '" + op +
          "' (orbit|pan|dolly|fit|home|isometric|front|back|right|left|top|bottom|bookmark_save|bookmark_load|set)";
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

json App::mcpCallLibraryTool(const std::string& name, const json& args,
                             std::string& err) {
  // Lazily snapshot the loaded Stage into the library-tool Context (copied at
  // most once per loaded scene; the viewer's Stage is never disturbed).
  if (loaded_.ok) {
    if (mcpCtxGen_ != sceneGen_ || !mcpCtx_.stage) {
      mcpCtx_.stage = std::make_unique<tinyusdz::Stage>(loaded_.stage);
      mcpCtx_.stage_loaded = true;
      mcpCtxGen_ = sceneGen_;
    }
  } else {
    mcpCtx_.stage.reset();
    mcpCtx_.stage_loaded = false;
  }
  json result;
  if (!tinyusdz::tydra::mcp::CallTool(mcpCtx_, name, args, result, err)) {
    if (err.empty()) err = "unknown tool: " + name;
    return json::object();
  }
  return result;
}

}  // namespace tusdview
