// SPDX-License-Identifier: Apache-2.0
// tusdquicklook MCP host implementation. All handlers run on App's UI thread.
#include "app.hh"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

namespace tusdql {

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kPitchLimit = 1.5533f;

json Vec3(const float v[3]) { return json::array({v[0], v[1], v[2]}); }

json Bounds(const QlAabb& bounds) {
  if (!bounds.valid) return json{{"valid", false}};
  return json{{"valid", true}, {"min", Vec3(bounds.lo)},
              {"max", Vec3(bounds.hi)}};
}

const char* BackendName(BackendChoice backend) {
  switch (backend) {
    case BackendChoice::Auto: return "auto";
    case BackendChoice::Cpu: return "cpu";
    case BackendChoice::Gl: return "gl";
  }
  return "auto";
}

json CameraJson(const OrbitCamera& camera, bool moving) {
  float eye[3];
  camera.Eye(eye);
  return json{{"target", Vec3(camera.target)},
              {"yaw", camera.yaw},
              {"pitch", camera.pitch},
              {"distance", camera.distance},
              {"eye", Vec3(eye)},
              {"moving", moving}};
}

}  // namespace

json App::mcpLoadUsd(const json& args, std::string& err) {
  if (!args.contains("path") || !args["path"].is_string()) {
    err = "load_usd requires a string 'path'";
    return json::object();
  }
  const std::string path = args["path"].get<std::string>();
  std::error_code ec;
  const fs::path file(path);
  if (!fs::is_regular_file(file, ec)) {
    err = "load_usd: file does not exist: " + path;
    return json::object();
  }
  if (!IsUsdPath(path)) {
    err = "load_usd: unsupported file extension: " + path;
    return json::object();
  }

  uint64_t size = 0;
  const uintmax_t file_size = fs::file_size(file, ec);
  if (!ec) size = static_cast<uint64_t>(file_size);
  FileEntry entry;
  entry.name = file.filename().string();
  entry.path = path;
  entry.size = size;
  entry.projected_bytes = ProjectMemoryForFile(path, size);
  entry.over_budget = entry.projected_bytes > budget_.total;
  if (entry.over_budget) {
    err = "load_usd: projected memory " + FormatBytes(entry.projected_bytes) +
          " exceeds budget " + FormatBytes(budget_.total);
    return json::object();
  }

  status_left_ = path;
  PreviewFile(entry);
  needs_redraw_ = true;
  return json{{"started", true}, {"path", path}};
}

json App::mcpSceneInfo(const json&, std::string&) {
  json out = {
      {"path", loader_.path()},
      {"loading", loader_.running()},
      {"complete", scene_complete_},
      {"phase", "idle"},
      {"backend", BackendStatusText()},
      {"desired_backend", BackendName(desired_backend_)},
      {"mesh_count", scene_.meshes.size()},
      {"triangle_count", scene_.stats.triangle_count},
      {"vertex_count", scene_.stats.vertex_count},
      {"material_count", scene_.materials.size()},
      {"texture_count", scene_.textures.size()},
      {"light_count", scene_.lights.size()},
      {"geometry_bytes", scene_.stats.geometry_bytes},
      {"texture_bytes", scene_.stats.texture_bytes},
      {"bounds", Bounds(scene_.bounds)},
      {"degraded", scene_.degraded.any()},
      {"memory_budget", budget_.total},
      {"gpu_memory_budget", opts_.max_gpu_mem_bytes},
      {"render_samples", render_status_.samples_done},
      {"render_target", render_status_.samples_target},
      {"render_converged", render_status_.converged},
  };
  if (loader_.control_valid()) {
    const LoadControl& control = loader_.control();
    out["phase"] = LoadPhaseName(control.phase.load());
    out["progress_permille"] = control.phase_permille.load();
    out["meshes_done"] = control.meshes_done.load();
    out["meshes_total"] = control.meshes_total.load();
    out["triangles_done"] = control.triangles_done.load();
  }
  if (scene_.degraded.any()) {
    json reasons = json::array();
    if (scene_.degraded.textures_dropped) reasons.push_back("textures_dropped");
    if (scene_.degraded.proxy_geometry) reasons.push_back("proxy_geometry");
    if (scene_.degraded.uncomposed) reasons.push_back("uncomposed");
    if (scene_.degraded.geometry_skipped) reasons.push_back("geometry_skipped");
    if (scene_.degraded.triangle_cap_hit) reasons.push_back("triangle_cap_hit");
    out["degradation_reasons"] = reasons;
    out["degradation_detail"] = scene_.degraded.detail;
  }
  return out;
}

json App::mcpListPrims(const json& args, std::string&) {
  size_t max_count = 1000;
  if (args.contains("max") && args["max"].is_number_integer()) {
    const long long requested = args["max"].get<long long>();
    if (requested > 0) {
      max_count = std::min<size_t>(static_cast<size_t>(requested), 10000);
    }
  }
  json meshes = json::array();
  for (size_t i = 0; i < scene_.meshes.size() && meshes.size() < max_count; i++) {
    const QlMesh& mesh = scene_.meshes[i];
    meshes.push_back(json{{"index", i},
                          {"name", mesh.name},
                          {"path", mesh.prim_path},
                          {"vertex_count", mesh.vertex_count()},
                          {"triangle_count", mesh.triangle_count()},
                          {"material_id", mesh.material_id},
                          {"proxy", mesh.is_proxy},
                          {"bounds", Bounds(mesh.bounds)}});
  }
  return json{{"count", meshes.size()},
              {"total", scene_.meshes.size()},
              {"meshes", meshes}};
}

json App::mcpViewport(const json& args, std::string& err) {
  const std::string op = args.value("op", std::string());
  OrbitCamera& camera = camera_goal_;
  const Layout layout = ComputeLayout(theme_, surf_w_, surf_h_, left_pane_w_);
  if (op == "orbit") {
    camera.Orbit(args.value("dx", 0.0f) * kPi /
                     static_cast<float>(std::max(1, layout.viewport.width)),
                 args.value("dy", 0.0f) * kPi /
                     static_cast<float>(std::max(1, layout.viewport.height)));
  } else if (op == "pan") {
    camera.Pan(args.value("dx", 0.0f), args.value("dy", 0.0f),
               std::max(1, layout.viewport.height));
  } else if (op == "dolly") {
    const float amount = args.value("amount", 0.0f);
    // Positive values zoom in, matching the intuitive MCP spelling. The
    // camera primitive itself takes a factor where values above one move away.
    const float factor = amount >= 0.0f ? 1.0f / (1.0f + amount)
                                       : 1.0f - amount;
    camera.Dolly(std::max(0.05f, factor));
  } else if (op == "fit" || op == "home") {
    if (!scene_.bounds.valid) {
      err = "viewport: no scene bounds to frame";
      return json::object();
    }
    camera.y_up = scene_.y_up;
    if (op == "home") {
      camera.yaw = 0.7f;
      camera.pitch = 0.32f;
    }
    const float aspect = layout.viewport.height > 0
                             ? float(layout.viewport.width) /
                                   float(layout.viewport.height)
                             : 1.0f;
    camera.FrameBounds(scene_.bounds, aspect);
  } else if (op == "isometric" || op == "front" || op == "back" ||
             op == "right" || op == "left" || op == "top" || op == "bottom") {
    camera.y_up = scene_.y_up;
    camera.pitch = 0.0f;
    if (op == "isometric") {
      camera.yaw = 0.7f;
      camera.pitch = 0.32f;
    } else if (op == "front") {
      camera.yaw = 0.0f;
    } else if (op == "back") {
      camera.yaw = kPi;
    } else if (op == "right") {
      camera.yaw = 0.5f * kPi;
    } else if (op == "left") {
      camera.yaw = -0.5f * kPi;
    } else if (op == "top") {
      camera.pitch = 1.2f;
    } else {
      camera.pitch = -1.2f;
    }
  } else if (op == "set") {
    if (args.contains("target") && args["target"].is_array() &&
        args["target"].size() == 3) {
      for (int i = 0; i < 3; i++) {
        if (!args["target"][i].is_number()) {
          err = "viewport: target must contain numbers";
          return json::object();
        }
        camera.target[i] = args["target"][i].get<float>();
      }
    }
    camera.yaw = args.value("yaw", camera.yaw);
    camera.pitch = std::max(-kPitchLimit,
                            std::min(kPitchLimit, args.value("pitch", camera.pitch)));
    camera.distance = std::max(1e-5f, args.value("distance", camera.distance));
  } else {
    err = "viewport: unknown op '" + op +
          "' (fit|home|isometric|front|back|right|left|top|bottom|orbit|pan|dolly|set)";
    return json::object();
  }

  camera_user_controlled_ = true;
  camera_framed_ = true;
  camera_animating_ = camera_.Differs(camera_goal_);
  needs_redraw_ = true;
  return CameraJson(camera_goal_, camera_animating_);
}

json App::mcpScreenshot(const json& args, std::string& err) {
  const std::string path = args.value("path", std::string());
  if (path.empty()) {
    err = "screenshot requires a 'path'";
    return json::object();
  }
  int width = args.value("width", surf_w_);
  int height = args.value("height", surf_h_);
  if (width < 64 || height < 64 || width > 4096 || height > 4096) {
    err = "screenshot dimensions must be between 64 and 4096";
    return json::object();
  }
  const uint64_t surface_bytes = uint64_t(width) * uint64_t(height) * 4;
  if (surface_bytes > budget_.total) {
    err = "screenshot surface " + FormatBytes(surface_bytes) +
          " exceeds memory budget " + FormatBytes(budget_.total);
    return json::object();
  }

  lvg_surface_t* surface = lvg_surface_create(width, height);
  if (!surface) {
    err = "screenshot: could not allocate offscreen surface";
    return json::object();
  }
  const int old_width = surf_w_;
  const int old_height = surf_h_;
  surf_w_ = width;
  surf_h_ = height;
  DrawFrame(surface);
  const int result = lvg_surface_save_png(surface, path.c_str());
  surf_w_ = old_width;
  surf_h_ = old_height;
  lvg_surface_destroy(surface);
  needs_redraw_ = true;
  if (result != 0) {
    err = "screenshot: failed to write " + path;
    return json::object();
  }
  return json{{"written", true}, {"path", path}, {"width", width},
              {"height", height}, {"backend", BackendStatusText()}};
}

json App::mcpRenderSettings(const json& args, std::string& err) {
  if (args.contains("mode")) {
    if (!args["mode"].is_string() ||
        !ParseShadingMode(args["mode"].get<std::string>(), &shading_mode_)) {
      err = "render_settings: mode must be shaded, albedo, normal, uv, "
            "roughness, metallic, or depth";
      return json::object();
    }
  }
  if (args.contains("shadows")) {
    if (!args["shadows"].is_boolean()) {
      err = "render_settings: shadows must be boolean";
      return json::object();
    }
    shadows_enabled_ = args["shadows"].get<bool>();
  }
  if (args.contains("ibl")) {
    if (!args["ibl"].is_boolean()) {
      err = "render_settings: ibl must be boolean";
      return json::object();
    }
    ibl_enabled_ = args["ibl"].get<bool>();
  }
  if (args.contains("exposure")) {
    if (!args["exposure"].is_number()) {
      err = "render_settings: exposure must be numeric";
      return json::object();
    }
    exposure_ = std::max(-16.0f, std::min(16.0f, args["exposure"].get<float>()));
  }
  ApplyRenderSettings();
  needs_redraw_ = true;
  return json{{"mode", ShadingModeName(shading_mode_)},
              {"shadows", shadows_enabled_},
              {"ibl", ibl_enabled_},
              {"exposure", exposure_}};
}

json App::mcpQuit(const json&, std::string&) {
  quit_ = true;
  return json{{"stopping", true}};
}

}  // namespace tusdql
