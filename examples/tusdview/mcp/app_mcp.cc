// SPDX-License-Identifier: Apache-2.0
// App's McpHost implementation: the tool handlers. All run on the main thread
// (the MCP server marshals tool calls into the render loop), so they freely read
// the loaded scene / DrawScene and drive the camera and selection.
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>

#include "app.hh"
#include "light3d/math.h"
#include "next/tinyusdz-next.hh"
#include "tydra/mcp-tools.hh"  // tinyusdz::tydra::mcp::CallTool

namespace tusdview {

using nlohmann::json;

namespace {
json arr3(const float a[3]) { return json::array({a[0], a[1], a[2]}); }
json vec3json(const light3d::Vec3& v) { return json::array({v.x, v.y, v.z}); }

json nextValueJson(const tinyusdz::next::Value& value) {
  if (value.is_array()) {
    return json{{"type", tinyusdz::next::GetTypeName(value.type_id())},
                {"count", value.array_size()}};
  }
  if (const bool* v = value.as_bool()) return *v;
  if (const int32_t* v = value.as_int()) return *v;
  if (const int64_t* v = value.as_int64()) return *v;
  if (const float* v = value.as_float()) return *v;
  if (const double* v = value.as_double()) return *v;
  if (const std::string* v = value.as_string()) return *v;
  if (const std::string* v = value.as_token()) return *v;
  if (const std::string* v = value.as_asset_path()) return *v;
  return json{{"type", tinyusdz::next::GetTypeName(value.type_id())}};
}

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
  const bool hasPath = args.contains("path") && args["path"].is_string();
  const bool hasUsda = args.contains("usda") && args["usda"].is_string();
  if (hasPath == hasUsda) {
    err = "load_usd requires exactly one string 'path' or 'usda'";
    return json::object();
  }
  std::string path;
  if (hasPath) {
    path = args["path"].get<std::string>();
  } else {
    const std::string source = args["usda"].get<std::string>();
    constexpr size_t kMaxInlineUsdaBytes = 16u * 1024u * 1024u;
    if (source.empty() || source.size() > kMaxInlineUsdaBytes) {
      err = "load_usd: inline USDA must be 1 byte..16 MiB";
      return json::object();
    }
    static uint64_t serial = 0;
    const std::filesystem::path temp =
        std::filesystem::temp_directory_path() /
        ("tusdview-mcp-" +
         std::to_string(reinterpret_cast<uintptr_t>(this)) + "-" +
         std::to_string(++serial) + ".usda");
    std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
    stream.write(source.data(), static_cast<std::streamsize>(source.size()));
    if (!stream) {
      err = "load_usd: could not materialize inline USDA";
      return json::object();
    }
    path = temp.string();
    mcpTempFiles_.push_back(temp);
  }
  startLoadAsync(path);  // async; client polls get_scene_info
  return json{{"started", true}, {"path", path}, {"inline", hasUsda}};
}

json App::mcpSceneInfo(const json&, std::string&) {
  json out = {{"loaded", loaded_.ok},
              {"loading", loadActive_},
              {"scene_generation", sceneGen_},
              {"window_generation", windowGeneration_},
              {"renderer_generation", rendererGeneration_},
              {"backend", backend_ == Backend::GL ? "gl" : "vk"},
              {"ray_tracing", rtPath_},
              {"filepath", loaded_.filepath},
              {"mesh_count", draw_.meshes.size()},
              {"triangle_count", draw_.triangleCount},
              {"material_count", draw_.materials.size()},
              {"upAxis", camera_.upAxis()},
              {"has_bounds", draw_.hasBounds}};
  if (loadActive_) out["loading_path"] = loadingPath_;
  if (!loaded_.err.empty()) out["error"] = loaded_.err;
  out["aabb_min"] = arr3(draw_.aabbMin);
  out["aabb_max"] = arr3(draw_.aabbMax);
  out["skinning_requested"] = skinningModeName(skinningRequested_);
  out["skinning_effective"] = skinningModeName(skinningEffective_);
  out["skinning_reason"] = skinningReason_;
  if (draw_.truncated) out["truncated"] = true;
  if (hasAnimation_) {
    out["has_animation"] = true;
    out["time"] = animTime_;
    out["start_time"] = animStart_;
    out["end_time"] = animEnd_;
    out["fps"] = animFps_;
    out["playing"] = animPlaying_;
  }
  if (loaded_.comp.composed) {
    out["composed"] = true;
    out["deferred_payload_count"] = loaded_.comp.deferred.size();
    json deferred = json::array();
    for (const auto& d : loaded_.comp.deferred) {
      deferred.push_back(
          json{{"prim", d.primPath}, {"asset", d.assetPath}, {"arc", d.arc}});
    }
    out["deferred_payloads"] = deferred;
  } else if (nextSession_) {
    out["composed"] = nextSession_->IsComposed();
    const std::vector<tinyusdz::next::Path> deferred =
        nextSession_->GetDeferredPayloadPaths();
    out["deferred_payload_count"] = deferred.size();
    json paths = json::array();
    for (const tinyusdz::next::Path& path : deferred) {
      paths.push_back(json{{"prim", path.str()}, {"arc", "payload"}});
    }
    out["deferred_payloads"] = std::move(paths);
  }
  return out;
}

json App::mcpSkinning(const json& args, std::string& err) {
  if (args.contains("mode")) {
    if (!args["mode"].is_string()) {
      err = "skinning: mode must be auto, cpu, or gpu";
      return json::object();
    }
    const std::string mode = args["mode"].get<std::string>();
    if (mode == "auto") {
      skinningRequested_ = SkinningMode::Auto;
    } else if (mode == "cpu") {
      skinningRequested_ = SkinningMode::CPU;
    } else if (mode == "gpu") {
      skinningRequested_ = SkinningMode::GPU;
    } else {
      err = "skinning: mode must be auto, cpu, or gpu";
      return json::object();
    }
    updateSkinningEffective();
    if (skinningEffective_ == SkinningMode::GPU) {
      cancelAndJoinReconvert();
      updateGpuSkinningFrameIfNeeded();
    } else if (hasAnimation_) {
      requestReconvert(animTime_);
    }
  }
  return json{{"requested", skinningModeName(skinningRequested_)},
              {"effective", skinningModeName(skinningEffective_)},
              {"reason", skinningReason_}};
}

json App::mcpRenderSettings(const json& args, std::string& err) {
  struct ModeName {
    const char* name;
    RenderMode mode;
  };
  static constexpr ModeName kModes[] = {
      {"shaded", RenderMode::Shaded},
      {"wireframe", RenderMode::Wireframe},
      {"normals", RenderMode::Normals},
      {"material-id", RenderMode::MaterialId},
      {"geom-normal", RenderMode::GeomNormal},
      {"uv", RenderMode::Uv},
      {"depth", RenderMode::Depth},
      {"albedo", RenderMode::Albedo},
      {"facing", RenderMode::Facing},
      {"roughness", RenderMode::Roughness},
      {"metallic", RenderMode::Metallic},
      {"emissive", RenderMode::Emissive},
      {"opacity", RenderMode::Opacity},
      {"mesh-id", RenderMode::MeshId},
      {"picking", RenderMode::MeshId},
      {"pick-id", RenderMode::MeshId},
      {"coat-normal", RenderMode::CoatNormal},
      {"coat-weight", RenderMode::CoatWeight},
      {"coat-color", RenderMode::CoatColor},
      {"coat-roughness", RenderMode::CoatRoughness},
      {"specular-f0", RenderMode::SpecularF0},
      {"ior-f0", RenderMode::IorF0},
  };

  if (args.contains("mode")) {
    if (!args["mode"].is_string()) {
      err = "render_settings: mode must be a string";
      return json::object();
    }
    const std::string requested = args["mode"].get<std::string>();
    bool found = false;
    for (const ModeName& entry : kModes) {
      if (requested == entry.name) {
        gui_.setRenderMode(entry.mode);
        found = true;
        break;
      }
    }
    if (!found) {
      err = "render_settings: unsupported mode '" + requested + "'";
      return json::object();
    }
  }
  if (args.contains("grid")) {
    if (!args["grid"].is_boolean()) {
      err = "render_settings: grid must be boolean";
      return json::object();
    }
    gui_.setShowGrid(args["grid"].get<bool>());
  }
  if (args.contains("camera")) {
    if (!args["camera"].is_string()) {
      err = "render_settings: camera must be a string";
      return json::object();
    }
    // Named USD cameras are resolved as part of the next scene load. Batch
    // manifests set this in load_settings before calling load_usd.
    cameraName_ = args["camera"].get<std::string>();
  }
  if (args.contains("adaptive_quality")) {
    if (!args["adaptive_quality"].is_boolean()) {
      err = "render_settings: adaptive_quality must be boolean";
      return json::object();
    }
    adaptiveQuality_ = args["adaptive_quality"].get<bool>();
  }
  if (args.contains("target_render_fps")) {
    if (!args["target_render_fps"].is_number()) {
      err = "render_settings: target_render_fps must be numeric";
      return json::object();
    }
    const float value = args["target_render_fps"].get<float>();
    if (!(value >= 1.0f && value <= 240.0f)) {
      err = "render_settings: target_render_fps must be in [1, 240]";
      return json::object();
    }
    adaptiveTargetFps_ = value;
  }

  const RenderMode current = gui_.renderMode();
  const char* currentName = "unknown";
  for (const ModeName& entry : kModes) {
    if (current == entry.mode) {
      currentName = entry.name;
      break;
    }
  }
  return json{{"mode", currentName}, {"camera", cameraName_},
              {"adaptive_quality", adaptiveQuality_},
              {"target_render_fps", adaptiveTargetFps_},
              {"render_scale", adaptiveRenderScale_}};
}

json App::mcpShaderReload(const json& args, std::string& err) {
  finishLiveShaderReloads();
  const std::string action = args.value("action", "status");
  std::string backend = args.value("backend", "active");
  const std::string source = args.value("source", std::string());
  if (backend == "active") backend = activeLiveShaderBackend();
  if (backend.empty() && action != "status") {
    err = "shader_reload: no active GPU RT/path-trace backend";
    return json::object();
  }
  if (backend != "vulkan" && backend != "cuda" && backend != "hip" &&
      backend != "all" && !(backend.empty() && action == "status")) {
    err = "shader_reload: backend must be active, vulkan, cuda, hip, or all";
    return json::object();
  }
  if (!source.empty() && backend == "all") {
    err = "shader_reload: source cannot be shared with backend=all";
    return json::object();
  }

  auto stateJson = [&](const char* name, const LiveShaderState& state) {
    uint64_t generation = state.successes;
    double runtimeCompileMs = state.lastCompileMs;
    if (std::strcmp(name, "cuda") == 0) {
      generation = cudaTracer_.kernelGeneration();
      if (cudaTracer_.lastKernelCompileMs() > 0.0)
        runtimeCompileMs = cudaTracer_.lastKernelCompileMs();
    } else if (std::strcmp(name, "hip") == 0) {
      generation = hipTracer_.kernelGeneration();
      if (hipTracer_.lastKernelCompileMs() > 0.0)
        runtimeCompileMs = hipTracer_.lastKernelCompileMs();
    }
    return json{{"backend", name},
                {"source", state.source},
                {"watch", state.watch},
                {"pending", static_cast<bool>(state.pending)},
                {"attempts", state.attempts},
                {"successes", state.successes},
                {"generation", generation},
                {"last_compile_ms", runtimeCompileMs},
                {"last_error", state.lastError}};
  };
  auto allStatus = [&]() {
    return json{{"active_backend", activeLiveShaderBackend()},
                {"transactional", true},
                {"poll_interval_ms", 250},
                {"backends", json::array({
                     stateJson("vulkan", liveVulkanShader_),
                     stateJson("cuda", liveCudaKernel_),
                     stateJson("hip", liveHipKernel_)})}};
  };

  if (action == "status") return allStatus();
  if (action == "watch") {
    if (!args.contains("watch") || !args["watch"].is_boolean()) {
      err = "shader_reload: action=watch requires boolean watch";
      return json::object();
    }
    const bool enabled = args["watch"].get<bool>();
    auto configure = [&](LiveShaderState* state) {
      if (!source.empty()) state->source = source;
      state->watch = enabled;
      state->haveTimestamp = false;
      if (enabled && !state->source.empty()) {
        std::error_code ec;
        state->timestamp =
            std::filesystem::last_write_time(state->source, ec);
        state->haveTimestamp = !ec;
        if (ec) state->lastError = "watch cannot stat source: " + state->source;
      }
    };
    if (backend == "all" || backend == "vulkan") configure(&liveVulkanShader_);
    if (backend == "all" || backend == "cuda") configure(&liveCudaKernel_);
    if (backend == "all" || backend == "hip") configure(&liveHipKernel_);
    return allStatus();
  }
  if (action != "reload") {
    err = "shader_reload: action must be status, reload, or watch";
    return json::object();
  }

  if (backend != "all") {
    if (!reloadLiveShader(backend, source, &err)) return allStatus();
    return allStatus();
  }
  std::string failures;
  for (const char* name : {"vulkan", "cuda", "hip"}) {
    std::string oneError;
    if (!reloadLiveShader(name, std::string(), &oneError)) {
      if (!failures.empty()) failures += "; ";
      failures += std::string(name) + ": " + oneError;
    }
  }
  if (!failures.empty()) err = failures;
  return allStatus();
}

json App::mcpRenderStats(const json&, std::string&) {
  const char* tier = adaptiveTier_ == 2 ? "interactive" :
                     (adaptiveTier_ == 1 ? "balanced" : "full");
  return json{{"ui_fps", ImGui::GetIO().Framerate},
              {"render_fps", renderFps_},
              {"threaded", renderThreadActive_},
              {"adaptive_quality", adaptiveQuality_},
              {"adaptive_tier", tier},
              {"render_scale", adaptiveRenderScale_},
              {"target_render_fps", adaptiveTargetFps_}};
}

json App::mcpLoadPayloads(const json& args, std::string& err) {
  if (!loaded_.comp.composed && !nextSession_) {
    err = "load_payloads: scene was not composed (no deferred payloads)";
    return json::object();
  }
  std::set<std::string> add;
  if (args.contains("paths") && args["paths"].is_array() && !args["paths"].empty()) {
    for (const auto& p : args["paths"]) {
      if (p.is_string()) add.insert(p.get<std::string>());
    }
  } else {
    if (nextSession_) {
      for (const tinyusdz::next::Path& path :
           nextSession_->GetDeferredPayloadPaths()) {
        add.insert(path.str());
      }
    } else {
      for (const auto& d : loaded_.comp.deferred) add.insert(d.primPath);
    }
  }
  if (add.empty()) {
    return json{{"started", false}, {"reason", "no deferred payloads"}};
  }
  startRecomposeAsync(add);  // async; client polls get_scene_info
  return json{{"started", true}, {"count", add.size()}};
}

json App::mcpTimeline(const json& args, std::string& err) {
  if (!hasAnimation_) {
    err = "timeline: scene has no animation";
    return json::object();
  }
  const std::string op = args.value("op", std::string());
  if (op == "play") {
    animPlaying_ = true;
  } else if (op == "pause") {
    animPlaying_ = false;
  } else if (op == "stop") {
    animPlaying_ = false;
    animTime_ = animStart_;
    requestReconvert(animTime_);
  } else if (op == "seek") {
    double t = args.value("time", animTime_);
    if (t < animStart_) t = animStart_;
    if (t > animEnd_) t = animEnd_;
    animTime_ = t;
    requestReconvert(animTime_);
  } else {
    err = "timeline: unknown op '" + op + "' (play|pause|stop|seek)";
    return json::object();
  }
  return json{{"playing", animPlaying_}, {"time", animTime_},
              {"start", animStart_},     {"end", animEnd_},
              {"fps", animFps_}};
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
  } else if (op == "forward") {
    camera_.moveForward(std::abs(args.value("amount", 1.0f)));
  } else if (op == "backward") {
    camera_.moveForward(-std::abs(args.value("amount", 1.0f)));
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
  } else if (op == "focus_dof") {
    float distance = 0.0f;
    if (!gui_.focusDofOnSelection(&distance)) {
      err = "focus_dof: pick an object or select a renderable prim in front "
            "of the camera first";
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
          "' (orbit|pan|dolly|forward|backward|fit|home|isometric|front|back|right|left|top|bottom|bookmark_save|bookmark_load|focus_dof|set)";
    return json::object();
  }
  // Return the resulting camera state.
  return json{{"target", vec3json(camera_.target())},
              {"yaw", camera_.yaw()},
              {"pitch", camera_.pitch()},
              {"distance", camera_.distance()},
              {"eye", vec3json(camera_.eye())},
              {"near", camera_.nearPlane()},
              {"far", camera_.farPlane()},
              {"focus_distance", gui_.dofFocusDistance()}};
}

json App::mcpScreenshot(const json& args, std::string& err) {
  const std::string path = args.value("path", std::string());
  if (path.empty()) {
    err = "screenshot: 'path' is required";
    return json::object();
  }
  if (!renderer_) {
    err = "screenshot: no renderer";
    return json::object();
  }
  // Capture the offscreen viewport (the last rendered frame). Camera ops issued
  // via the 'viewport' tool take effect on the next frame, so a typical debug
  // loop is: viewport(orbit ...) -> screenshot (separate calls = separate frames).
  std::vector<uint8_t> rgba;
  int w = 0, h = 0;
  if (!renderer_->captureViewport(&rgba, &w, &h)) {
    err = "screenshot: viewport capture failed (no rendered frame yet?)";
    return json::object();
  }
  // During the first few windowed frames (and briefly after a scene reload),
  // ImGui may report a collapsed viewport while its dock layout settles. Such
  // captures are technically non-empty -- commonly 1x10 -- but are not useful
  // screenshots and can make consecutive AOVs appear byte-identical. Report a
  // retryable not-ready result instead of writing misleading image data.
  if (w <= 1 || h <= 1) {
    return json{{"written", false},
                {"ready", false},
                {"reason", "viewport layout is not ready"},
                {"width", w},
                {"height", h}};
  }
  std::string werr;
  if (!WriteScreenshotImage(path, rgba, w, h, &werr)) {
    err = "screenshot: write failed: " + werr;
    return json::object();
  }
  return json{{"written", true},
              {"ready", true},
              {"path", path},
              {"width", w},
              {"height", h}};
}

json App::mcpInput(const json& args, std::string& err) {
  const std::string key = args.value("key", std::string());
  if (key.empty()) {
    err = "input: 'key' is required (e.g. v|w|s|f|a|r|G|V|0|1|3|5|7)";
    return json::object();
  }
  std::string action;
  if (key == "v") {
    action = "wireframe=" + std::to_string(gui_.cycleWireframe());
  } else if (key == "w") {
    camera_.moveForward(1.0f);
    action = "forward";
  } else if (key == "s") {
    camera_.moveForward(-1.0f);
    action = "backward";
  } else if (key == "f" || key == "a") {
    if (draw_.hasBounds) camera_.fitToScene(draw_.aabbMin, draw_.aabbMax);
    action = "frame_all";
  } else if (key == "i") {
    if (!gui_.isolateSelected()) {
      err = "input: no selected renderable prim to isolate";
      return json::object();
    }
    action = "show_selected_only";
  } else if (key == "u") {
    gui_.showAllRenderables();
    action = "show_all";
  } else if (key == "0") {
    camera_.setPreset(CameraViewPreset::Isometric);
    if (draw_.hasBounds) camera_.fitToScene(draw_.aabbMin, draw_.aabbMax);
    action = "home";
  } else if (key == "5") {
    camera_.setPreset(CameraViewPreset::Isometric);
    action = "isometric";
  } else if (key == "1") {
    camera_.setPreset(CameraViewPreset::Front);
    action = "front";
  } else if (key == "3") {
    camera_.setPreset(CameraViewPreset::Right);
    action = "right";
  } else if (key == "7") {
    camera_.setPreset(CameraViewPreset::Top);
    action = "top";
  } else if (key == "G" || key == "V") {
    // Scriptable window-owner switch (GL raster / Vulkan raster), so the
    // GL<->Vulkan teardown+rebuild path is reachable from a test harness.
    // Applied immediately, like 'r' above (this drain runs after present, i.e.
    // between ImGui frames -- the switch must never land mid-frame).
    applyTechniqueSwitch(key == "G" ? RenderTechnique::GLRaster
                                    : RenderTechnique::VulkanRaster);
    action = std::string("technique=") + RenderTechniqueLabel(activeTechnique_);
  } else if (key == "r") {
    // Same toggle the viewport 'r' keybinding drives: into CPU RT, or back to
    // whatever technique was active before. Applied immediately (the MCP tool
    // handlers already run on the main thread) rather than through Gui's
    // one-shot request flag, so the caller can screenshot right after.
    const RenderTechnique want = (activeTechnique_ == RenderTechnique::CpuRT)
                                     ? previousTechnique_
                                     : RenderTechnique::CpuRT;
    applyTechniqueSwitch(want);
    action = std::string("technique=") + RenderTechniqueLabel(activeTechnique_);
  } else {
    err = "input: unhandled key '" + key + "' (v|w|s|f|a|r|G|V|0|1|3|5|7)";
    return json::object();
  }
  return json{{"key", key}, {"action", action}, {"wireframe", gui_.wireframeMode()}};
}

json App::mcpMouse(const json& args, std::string& err) {
  const std::string type = args.value("type", std::string());
  if (type != "move" && type != "button") {
    err = "mouse: type must be 'move' or 'button'";
    return json::object();
  }
  const float x = args.value("x", -1.0f);
  const float y = args.value("y", -1.0f);
  int w = 0, h = 0;
  gui_.viewportPixelSize(&w, &h);
  if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0f || y < 0.0f ||
      x >= static_cast<float>(w) ||
      y >= static_cast<float>(h)) {
    err = "mouse: coordinates are outside the viewport";
    return json::object();
  }
  ImGuiIO& io = ImGui::GetIO();
  float screenX = x, screenY = y;
  gui_.viewportPixelToScreen(x, y, &screenX, &screenY);
  io.AddMousePosEvent(screenX, screenY);
  if (type == "button") {
    const int button = args.value("button", 0);
    if (button < 0 || button > 2) {
      err = "mouse: button must be 0 (left), 1 (middle), or 2 (right)";
      return json::object();
    }
    const int imguiButton = button == 1 ? 2 : button == 2 ? 1 : 0;
    io.AddKeyEvent(ImGuiMod_Shift, args.value("shift", false));
    io.AddKeyEvent(ImGuiMod_Ctrl, args.value("ctrl", false));
    io.AddKeyEvent(ImGuiMod_Alt, args.value("alt", false));
    io.AddMouseButtonEvent(imguiButton, args.value("down", false));
  }
  return json{{"type", type}, {"x", x}, {"y", y},
              {"viewport_width", w}, {"viewport_height", h}};
}

json App::mcpPick(const json& args, std::string& err) {
  const float x = args.value("x", -1.0f);
  const float y = args.value("y", -1.0f);
  int w = 0, h = 0;
  gui_.viewportPixelSize(&w, &h);
  if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0f || y < 0.0f ||
      x >= static_cast<float>(w) ||
      y >= static_cast<float>(h)) {
    err = "pick: coordinates are outside the viewport";
    return json::object();
  }
  const bool region = args.contains("x1") || args.contains("y1");
  const float x1 = args.value("x1", x);
  const float y1 = args.value("y1", y);
  if (region && (!std::isfinite(x1) || !std::isfinite(y1) || x1 < 0.0f ||
                 y1 < 0.0f || x1 > static_cast<float>(w) ||
                 y1 > static_cast<float>(h))) {
    err = "pick: region coordinates are outside the viewport";
    return json::object();
  }
  const Gui::PickReport report = region
      ? gui_.pickViewportRegion(x, y, x1, y1, args.value("select", true))
      : gui_.pickViewportPixel(x, y, args.value("select", true));
  return json{{"hit", !report.path.empty()},
              {"path", report.path},
              {"kind", report.kind},
              {"mesh_index", report.meshIndex},
              {"x", report.x},
              {"y", report.y},
              {"viewport_width", report.viewportWidth},
              {"viewport_height", report.viewportHeight},
              {"covered_pixels", report.coveredPixels},
              {"hit_distance", report.hitDistance},
              {"selected", args.value("select", true) && !report.path.empty()}};
}

json App::mcpListPrims(const json& args, std::string&) {
  size_t cap = 1000;
  if (args.contains("max") && args["max"].is_number_integer()) {
    const long long m = args["max"].get<long long>();
    if (m > 0) cap = static_cast<size_t>(m);
  }
  json paths = json::array();
  if (nextSession_) {
    nextSession_->GetStage().Traverse([&](const tinyusdz::next::UsdPrim& prim) {
      if (paths.size() >= cap) return false;
      paths.push_back(prim.GetPath().str());
      return true;
    });
  } else {
    for (const auto& m : draw_.meshes) {
      if (paths.size() >= cap) break;
      paths.push_back(m.absPath);
    }
  }
  return json{{"count", paths.size()}, {"paths", paths}};
}

json App::mcpCallLibraryTool(const std::string& name, const json& args,
                             std::string& err) {
  if (nextSession_) {
    const tinyusdz::next::Stage& stage = nextSession_->GetStage();
    if (name == "stage_info") {
      const tinyusdz::next::StageMeta& meta = stage.GetMeta();
      return json{{"loaded", true},
                  {"defaultPrim", meta.defaultPrim},
                  {"upAxis", meta.upAxis},
                  {"metersPerUnit", meta.metersPerUnit},
                  {"startTimeCode", meta.startTimeCode},
                  {"endTimeCode", meta.endTimeCode},
                  {"timeCodesPerSecond", meta.timeCodesPerSecond},
                  {"primCount", stage.GetPrimCount()}};
    }

    if (name == "prim_list" || name == "query_prims_by_type" ||
        name == "search") {
      const std::string root = args.value("path", std::string("/"));
      const std::string type = args.value("type", std::string());
      const std::string query = args.value("query", std::string());
      json prims = json::array();
      stage.Traverse([&](const tinyusdz::next::UsdPrim& prim) {
        const std::string path = prim.GetPath().str();
        if (root != "/" && path != root &&
            path.compare(0, root.size() + 1, root + "/") != 0) {
          return true;
        }
        if (!type.empty() && prim.GetTypeName() != type) return true;
        if (!query.empty() && path.find(query) == std::string::npos &&
            prim.GetName().find(query) == std::string::npos) {
          return true;
        }
        prims.push_back(json{{"path", path},
                             {"name", prim.GetName()},
                             {"type", prim.GetTypeName()},
                             {"active", prim.IsActive()}});
        return true;
      });
      return json{{"path", root}, {"prims", prims}, {"count", prims.size()}};
    }

    if (!args.contains("path") || !args["path"].is_string()) {
      err = "Missing 'path' argument";
      return json::object();
    }
    const std::string path = args["path"].get<std::string>();
    const tinyusdz::next::UsdPrim prim = stage.GetPrimAtPath(path);
    if (!prim.IsValid()) {
      err = "Prim not found: " + path;
      return json::object();
    }

    if (name == "prim_get") {
      return json{{"path", path},
                  {"name", prim.GetName()},
                  {"type", prim.GetTypeName()},
                  {"active", prim.IsActive()},
                  {"propertyCount", prim.GetPropertyNames().size()},
                  {"childCount", prim.GetChildCount()}};
    }
    if (name == "attr_list") {
      json attributes = json::array();
      for (const std::string& attr : prim.GetPropertyNames()) {
        const tinyusdz::next::Value* value = prim.GetPropertyValue(attr);
        attributes.push_back(
            json{{"name", attr},
                 {"type", value ? tinyusdz::next::GetTypeName(value->type_id())
                                : "unknown"},
                 {"hasValue", value != nullptr}});
      }
      return json{{"path", path},
                  {"attributes", attributes},
                  {"count", attributes.size()}};
    }
    if (name == "attr_get") {
      const std::string attr = args.value("attr_name", std::string());
      if (attr.empty()) {
        err = "Missing 'attr_name' argument";
        return json::object();
      }
      const tinyusdz::next::Value* value = prim.GetPropertyValue(attr);
      if (!value) {
        err = "Attribute not found: " + attr;
        return json::object();
      }
      return json{{"path", path},
                  {"attr_name", attr},
                  {"value", nextValueJson(*value)}};
    }
    if (name == "variant_list_sets" || name == "variant_get_selection") {
      json sets = json::object();
      for (const tinyusdz::next::VariantSetData& set :
           prim.GetMeta().variantSets()) {
        json variants = json::array();
        for (const tinyusdz::next::VariantData& variant : set.variants) {
          variants.push_back(variant.name);
        }
        sets[set.name] = json{{"selection", set.selected},
                              {"variants", variants}};
      }
      if (name == "variant_get_selection") {
        const std::string set = args.value("variant_set", std::string());
        return json{{"path", path},
                    {"variant_set", set},
                    {"selection", sets.contains(set)
                                      ? sets[set].value("selection", "")
                                      : ""}};
      }
      return json{{"path", path},
                  {"variantSets", sets},
                  {"count", sets.size()}};
    }
    if (name == "variant_set_selection") {
      const std::string set = args.value("variant_set", std::string());
      const std::string variant = args.value("variant", std::string());
      if (set.empty() || variant.empty()) {
        err = "variant_set_selection requires variant_set and variant";
        return json::object();
      }
      loadOpts_.variantOverrides[path][set] = variant;
      startRecomposeAsync(std::set<std::string>());
      return json{{"success", true}, {"started", true}, {"path", path}};
    }

    err = "Tool is not available on the read-only next document: " + name;
    return json::object();
  }

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
